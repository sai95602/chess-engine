#include "http_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "eval.h"
#include "json.h"
#include "movegen.h"
#include "perft.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#define SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static void close_socket(socket_t s) { ::close(s); }
static constexpr socket_t INVALID_SOCK = -1;
#define SEND_FLAGS MSG_NOSIGNAL
#endif

namespace {

constexpr size_t  MaxHeaderBytes = 16 * 1024;
constexpr size_t  MaxBodyBytes   = 256 * 1024;
constexpr int64_t MaxMoveTimeMs  = 60'000;
constexpr int     MaxPerftDepth  = 7;
constexpr int     MaxConnections = 64;

std::mutex        engineMutex;  // one search at a time (each search already uses all threads)
std::atomic<int>  activeConnections{0};

const char* status_text(int code) {
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}

HttpResponse json_error(int status, const std::string& msg) {
    return {status, "{\"error\":" + json_str(msg) + "}"};
}

// Unified parameter access: JSON body first, then query string.
struct Params {
    JsonValue json;
    const std::map<std::string, std::string>* query;

    bool has(const std::string& k) const { return json.has(k) || query->count(k); }

    std::string str(const std::string& k, const std::string& def = "") const {
        if (json.has(k)) {
            const JsonValue& v = json[k];
            if (v.type == JsonValue::String) return v.str;
            if (v.type == JsonValue::Number) return std::to_string((long long)v.num);
            if (v.type == JsonValue::Null) return def;
            throw std::runtime_error("parameter '" + k + "' must be a string");
        }
        auto it = query->find(k);
        return it == query->end() ? def : it->second;
    }

    int64_t num(const std::string& k, int64_t def) const {
        if (json.has(k)) {
            const JsonValue& v = json[k];
            if (v.type == JsonValue::Number) return int64_t(v.num);
            if (v.type == JsonValue::Null) return def;
            if (v.type == JsonValue::String) return std::stoll(v.str);
            throw std::runtime_error("parameter '" + k + "' must be a number");
        }
        auto it = query->find(k);
        if (it == query->end() || it->second.empty()) return def;
        try {
            return std::stoll(it->second);
        } catch (...) {
            throw std::runtime_error("parameter '" + k + "' must be a number");
        }
    }

    std::vector<std::string> moves() const {
        std::vector<std::string> out;
        if (json.has("moves")) {
            const JsonValue& v = json["moves"];
            if (v.type == JsonValue::Array) {
                for (auto& e : v.arr) {
                    if (e.type != JsonValue::String) throw std::runtime_error("'moves' must contain strings");
                    out.push_back(e.str);
                }
                return out;
            }
            if (v.type == JsonValue::String) {
                std::istringstream is(v.str);
                std::string t;
                while (is >> t) out.push_back(t);
                return out;
            }
            if (v.type != JsonValue::Null) throw std::runtime_error("'moves' must be an array of UCI moves");
        }
        auto it = query->find("moves");
        if (it != query->end()) {
            std::string s = it->second;
            std::replace(s.begin(), s.end(), ',', ' ');
            std::istringstream is(s);
            std::string t;
            while (is >> t) out.push_back(t);
        }
        return out;
    }
};

std::string moves_json(const std::vector<Move>& moves) {
    std::string s = "[";
    for (size_t i = 0; i < moves.size(); ++i) s += (i ? "," : "") + json_str(move_to_uci(moves[i]));
    return s + "]";
}

std::string score_json(int score) {
    if (score >= VALUE_MATE_IN_MAX_PLY) return "{\"type\":\"mate\",\"value\":" + std::to_string((VALUE_MATE - score + 1) / 2) + "}";
    if (score <= VALUE_MATED_IN_MAX_PLY) return "{\"type\":\"mate\",\"value\":" + std::to_string(-(VALUE_MATE + score) / 2) + "}";
    return "{\"type\":\"cp\",\"value\":" + std::to_string(score) + "}";
}

std::string game_status(const Position& pos) {
    MoveList legal;
    generate_legal(pos, legal);
    if (legal.size == 0) return pos.in_check() ? "checkmate" : "stalemate";
    if (pos.rule50() >= 100) return "draw_fifty_move";
    if (pos.has_repeated()) return "draw_repetition";
    if (pos.insufficient_material()) return "draw_insufficient_material";
    return "ongoing";
}

bool load_position(const Params& p, Position& pos, std::string& err) {
    return setup_position(pos, p.str("fen", "startpos"), p.moves(), err);
}

HttpResponse api_move(const Params& p) {
    Position pos;
    std::string err;
    if (!load_position(p, pos, err)) return json_error(400, err);

    std::string status = game_status(pos);
    if (status == "checkmate" || status == "stalemate")
        return {200, "{\"bestmove\":null,\"status\":" + json_str(status) + ",\"fen\":" + json_str(pos.fen()) + "}"};

    SearchLimits limits;
    Color us = pos.side_to_move();
    limits.depth     = int(std::clamp<int64_t>(p.num("depth", 0), 0, MAX_PLY - 1));
    limits.nodes     = uint64_t(std::max<int64_t>(0, p.num("nodes", 0)));
    limits.time[WHITE] = std::max<int64_t>(0, p.num("wtime", 0));
    limits.time[BLACK] = std::max<int64_t>(0, p.num("btime", 0));
    limits.inc[WHITE]  = std::max<int64_t>(0, p.num("winc", 0));
    limits.inc[BLACK]  = std::max<int64_t>(0, p.num("binc", 0));
    limits.movestogo   = int(std::max<int64_t>(0, p.num("movestogo", 0)));
    int64_t movetime   = p.num("movetime", -1);

    bool hasClock = limits.time[us] > 0;
    if (movetime > 0) limits.movetime = std::min(movetime, MaxMoveTimeMs);
    else if (!hasClock) {
        // No clock given: default to 1s, or a safety cap when only depth/nodes was requested.
        limits.movetime = (limits.depth || limits.nodes) ? MaxMoveTimeMs : 1000;
    }
    if (hasClock) limits.time[us] = std::min(limits.time[us], int64_t(3600'000));

    SearchResult r;
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        r = Engine.search(pos, limits);
    }

    std::ostringstream os;
    os << "{\"bestmove\":" << json_str(move_to_uci(r.bestMove))
       << ",\"ponder\":" << (r.ponder != MOVE_NONE ? json_str(move_to_uci(r.ponder)) : "null")
       << ",\"score\":" << score_json(r.score) << ",\"depth\":" << r.depth << ",\"seldepth\":" << r.seldepth
       << ",\"nodes\":" << r.nodes << ",\"nps\":" << (r.timeMs > 0 ? r.nodes * 1000 / uint64_t(r.timeMs) : r.nodes)
       << ",\"time_ms\":" << r.timeMs << ",\"pv\":" << moves_json(r.pv) << ",\"fen\":" << json_str(pos.fen())
       << ",\"status\":" << json_str(status) << ",\"threads\":" << Engine.threads() << "}";
    return {200, os.str()};
}

HttpResponse api_legal_moves(const Params& p) {
    Position pos;
    std::string err;
    if (!load_position(p, pos, err)) return json_error(400, err);
    MoveList legal;
    generate_legal(pos, legal);
    std::vector<Move> mv;
    for (int i = 0; i < legal.size; ++i) mv.push_back(legal.moves[i].move);
    std::sort(mv.begin(), mv.end(), [](Move a, Move b) { return move_to_uci(a) < move_to_uci(b); });
    std::ostringstream os;
    os << "{\"fen\":" << json_str(pos.fen()) << ",\"side_to_move\":\"" << (pos.side_to_move() == WHITE ? "white" : "black")
       << "\",\"in_check\":" << (pos.in_check() ? "true" : "false") << ",\"status\":" << json_str(game_status(pos))
       << ",\"count\":" << mv.size() << ",\"moves\":" << moves_json(mv) << "}";
    return {200, os.str()};
}

HttpResponse api_eval(const Params& p) {
    Position pos;
    std::string err;
    if (!load_position(p, pos, err)) return json_error(400, err);
    int v = evaluate(pos);
    int white = pos.side_to_move() == WHITE ? v : -v;
    std::ostringstream os;
    os << "{\"fen\":" << json_str(pos.fen()) << ",\"eval_cp\":" << v << ",\"eval_white_cp\":" << white
       << ",\"breakdown\":" << json_str(eval_trace(pos)) << "}";
    return {200, os.str()};
}

HttpResponse api_perft(const Params& p) {
    Position pos;
    std::string err;
    if (!load_position(p, pos, err)) return json_error(400, err);
    int depth = int(p.num("depth", 4));
    if (depth < 1 || depth > MaxPerftDepth)
        return json_error(400, "depth must be between 1 and " + std::to_string(MaxPerftDepth));
    std::lock_guard<std::mutex> lock(engineMutex);
    auto t0 = std::chrono::steady_clock::now();
    auto div = perft_divide(pos, depth);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    uint64_t total = 0;
    std::ostringstream d;
    d << "{";
    std::sort(div.begin(), div.end(), [](auto& a, auto& b) { return move_to_uci(a.first) < move_to_uci(b.first); });
    for (size_t i = 0; i < div.size(); ++i) {
        total += div[i].second;
        d << (i ? "," : "") << json_str(move_to_uci(div[i].first)) << ":" << div[i].second;
    }
    d << "}";
    std::ostringstream os;
    os << "{\"fen\":" << json_str(pos.fen()) << ",\"depth\":" << depth << ",\"nodes\":" << total << ",\"time_ms\":" << ms
       << ",\"nps\":" << (ms > 0 ? total * 1000 / uint64_t(ms) : total) << ",\"divide\":" << d.str() << "}";
    return {200, os.str()};
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else
            out += s[i];
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& q) {
    std::map<std::string, std::string> out;
    std::stringstream ss(q);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq == std::string::npos) out[url_decode(pair)] = "";
        else out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
    }
    return out;
}

}  // namespace

HttpResponse handle_api_request(const std::string& method, const std::string& path,
                                const std::map<std::string, std::string>& query, const std::string& body) {
    if (method == "OPTIONS") return {204, ""};

    if (path == "/" || path == "/health") {
        if (method != "GET" && method != "HEAD") return json_error(405, "use GET");
        if (path == "/health") return {200, "{\"status\":\"ok\"}"};
        std::ostringstream os;
        os << "{\"engine\":" << json_str(ENGINE_NAME) << ",\"threads\":" << Engine.threads() << ",\"endpoints\":{"
           << "\"GET /health\":\"liveness check\","
           << "\"POST /move\":\"best move: {fen, moves[], movetime|depth|nodes|wtime/btime/winc/binc}\","
           << "\"POST /legal-moves\":\"legal moves and game status: {fen, moves[]}\","
           << "\"POST /eval\":\"static evaluation: {fen, moves[]}\","
           << "\"POST /perft\":\"perft node count: {fen, moves[], depth<=7}\"},"
           << "\"note\":\"every endpoint also accepts GET with query parameters, e.g. /move?fen=...&movetime=500\"}";
        return {200, os.str()};
    }

    using Handler = HttpResponse (*)(const Params&);
    Handler h = nullptr;
    if (path == "/move" || path == "/bestmove") h = api_move;
    else if (path == "/legal-moves" || path == "/legal_moves" || path == "/moves") h = api_legal_moves;
    else if (path == "/eval" || path == "/evaluate") h = api_eval;
    else if (path == "/perft") h = api_perft;
    if (!h) return json_error(404, "unknown endpoint " + path);
    if (method != "GET" && method != "POST") return json_error(405, "use GET or POST");

    try {
        Params p;
        p.query = &query;
        std::string trimmed = body;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        if (method == "POST" && !trimmed.empty()) {
            p.json = json_parse(trimmed);
            if (p.json.type != JsonValue::Object) return json_error(400, "request body must be a JSON object");
        }
        return h(p);
    } catch (const std::exception& e) {
        return json_error(400, e.what());
    }
}

namespace {

bool send_all(socket_t s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = int(::send(s, data.data() + sent, int(data.size() - sent), SEND_FLAGS));
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

void send_response(socket_t s, const HttpResponse& r, bool headOnly) {
    std::ostringstream os;
    os << "HTTP/1.1 " << r.status << ' ' << status_text(r.status) << "\r\n"
       << "Content-Type: " << r.contentType << "\r\n"
       << "Content-Length: " << r.body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n\r\n";
    if (!headOnly) os << r.body;
    send_all(s, os.str());
}

void handle_connection(socket_t client) {
    struct Guard {
        socket_t s;
        ~Guard() { close_socket(s); activeConnections--; }
    } guard{client};

#if defined(_WIN32)
    DWORD tv = 15000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    timeval tv{15, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string data;
    char buf[8192];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = int(::recv(client, buf, sizeof(buf), 0));
        if (n <= 0) return;
        data.append(buf, size_t(n));
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd == std::string::npos && data.size() > MaxHeaderBytes) {
            send_response(client, json_error(413, "headers too large"), false);
            return;
        }
    }

    std::istringstream hs(data.substr(0, headerEnd));
    std::string requestLine;
    std::getline(hs, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();
    std::istringstream rl(requestLine);
    std::string method, target, version;
    rl >> method >> target >> version;
    if (method.empty() || target.empty()) {
        send_response(client, json_error(400, "malformed request line"), false);
        return;
    }

    size_t contentLength = 0;
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        if (name == "content-length") {
            try {
                contentLength = size_t(std::stoull(value));
            } catch (...) {
                send_response(client, json_error(400, "bad Content-Length"), false);
                return;
            }
        }
    }
    if (contentLength > MaxBodyBytes) {
        send_response(client, json_error(413, "body too large"), false);
        return;
    }

    std::string body = data.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        int n = int(::recv(client, buf, sizeof(buf), 0));
        if (n <= 0) return;
        body.append(buf, size_t(n));
    }
    body.resize(std::min(body.size(), contentLength));

    std::string path = target, query;
    size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        path  = target.substr(0, qpos);
        query = target.substr(qpos + 1);
    }
    if (path.size() > 1 && path.back() == '/') path.pop_back();

    auto t0 = std::chrono::steady_clock::now();
    HttpResponse resp;
    try {
        resp = handle_api_request(method == "HEAD" ? "GET" : method, path, parse_query(query), body);
    } catch (const std::exception& e) {
        resp = json_error(500, e.what());
    }
    send_response(client, resp, method == "HEAD");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << method << ' ' << path << " -> " << resp.status << " (" << ms << " ms)" << std::endl;
}

}  // namespace

int run_http_server(const std::string& host, int port) {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif
    socket_t server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCK) {
        std::cerr << "socket() failed\n";
        return 1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(uint16_t(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid host address " << host << "\n";
        return 1;
    }
    if (::bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "bind() failed on " << host << ":" << port << "\n";
        return 1;
    }
    if (::listen(server, 128) != 0) {
        std::cerr << "listen() failed\n";
        return 1;
    }
    std::cout << ENGINE_NAME << " REST API listening on http://" << host << ":" << port << " (threads "
              << Engine.threads() << ")" << std::endl;

    while (true) {
        sockaddr_in caddr{};
        socklen_t   clen = sizeof(caddr);
        socket_t client = ::accept(server, (sockaddr*)&caddr, &clen);
        if (client == INVALID_SOCK) continue;
        if (activeConnections.load() >= MaxConnections) {
            send_response(client, json_error(503, "server busy"), false);
            close_socket(client);
            continue;
        }
        activeConnections++;
        std::thread(handle_connection, client).detach();
    }
    close_socket(server);
    return 0;
}
