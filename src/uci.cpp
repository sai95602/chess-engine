#include "uci.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "eval.h"
#include "movegen.h"
#include "perft.h"
#include "search.h"
#include "tt.h"

const char* ENGINE_NAME   = "Bitboard Chess Engine 1.0";
const char* ENGINE_AUTHOR = "Rithik Polavaram";

bool setup_position(Position& pos, const std::string& fen, const std::vector<std::string>& moves, std::string& error) {
    if (!pos.set(fen.empty() || fen == "startpos" ? Position::StartFEN : fen, &error)) return false;
    for (const auto& ms : moves) {
        Move m = pos.parse_uci_move(ms);
        if (m == MOVE_NONE) {
            error = "illegal move '" + ms + "' in position " + pos.fen();
            return false;
        }
        pos.do_move(m);
    }
    return true;
}

namespace {

void print_info(const SearchInfo& info) {
    std::ostringstream os;
    os << "info depth " << info.depth << " seldepth " << info.seldepth << " score " << uci_score(info.score)
       << " nodes " << info.nodes << " nps " << info.nps << " hashfull " << info.hashfull << " time " << info.timeMs
       << " pv";
    for (Move m : info.pv) os << ' ' << move_to_uci(m);
    std::cout << os.str() << std::endl;
}

void parse_position(Position& pos, std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = Position::StartFEN;
        is >> token;  // "moves" (optional)
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
    } else
        return;
    std::vector<std::string> moves;
    while (is >> token) moves.push_back(token);
    std::string err;
    Position tmp;
    if (setup_position(tmp, fen, moves, err)) pos = tmp;
    else std::cout << "info string error: " << err << std::endl;
}

SearchLimits parse_go(std::istringstream& is, int& perftDepth) {
    SearchLimits l;
    std::string token;
    perftDepth = 0;
    while (is >> token) {
        if (token == "wtime") is >> l.time[WHITE];
        else if (token == "btime") is >> l.time[BLACK];
        else if (token == "winc") is >> l.inc[WHITE];
        else if (token == "binc") is >> l.inc[BLACK];
        else if (token == "movestogo") is >> l.movestogo;
        else if (token == "depth") is >> l.depth;
        else if (token == "nodes") is >> l.nodes;
        else if (token == "movetime") is >> l.movetime;
        else if (token == "infinite" || token == "ponder") l.infinite = true;
        else if (token == "perft") is >> perftDepth;
    }
    return l;
}

}  // namespace

void run_bench(int depth) {
    static const char* BenchFens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
        "2r3k1/pp3ppp/1qn1p3/3pP3/3P4/P1NQ1N2/1P3PPP/R4RK1 b - - 0 18",
        "8/8/1p1k4/p2p4/P2P1P2/1P1K4/8/8 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 9",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    };
    Engine.new_game();
    uint64_t totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char* fen : BenchFens) {
        Position pos;
        pos.set(fen);
        SearchLimits l;
        l.depth = depth;
        SearchResult r = Engine.search(pos, l);
        totalNodes += r.nodes;
        std::cout << "depth " << r.depth << " bestmove " << move_to_uci(r.bestMove) << " score " << uci_score(r.score)
                  << " nodes " << r.nodes << "  " << fen << std::endl;
    }
    double ms = double(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    std::cout << "\nTotal nodes : " << totalNodes << "\nTime (ms)   : " << ms
              << "\nNodes/second: " << uint64_t(totalNodes * 1000.0 / std::max(1.0, ms)) << std::endl;
}

void uci_loop(const std::vector<std::string>& initialCommands) {
    Position pos;
    pos.set(Position::StartFEN);
    std::thread searchThread;
    size_t nextInitial = 0;

    auto wait_search = [&]() {
        if (searchThread.joinable()) searchThread.join();
    };

    std::string line;
    while (true) {
        if (nextInitial < initialCommands.size()) line = initialCommands[nextInitial++];
        else if (!initialCommands.empty()) line = "quit";
        else if (!std::getline(std::cin, line)) line = "quit";

        std::istringstream is(line);
        std::string token;
        is >> token;

        if (token == "quit") {
            Engine.stop();
            wait_search();
            break;
        } else if (token == "stop") {
            Engine.stop();
            wait_search();
        } else if (token == "ponderhit") {
            Engine.stop();
        } else if (token == "uci") {
            std::cout << "id name " << ENGINE_NAME << "\nid author " << ENGINE_AUTHOR << "\n"
                      << "option name Hash type spin default 64 min 1 max 65536\n"
                      << "option name Threads type spin default 1 min 1 max 256\n"
                      << "option name Move Overhead type spin default 50 min 0 max 5000\n"
                      << "option name Clear Hash type button\n"
                      << "uciok" << std::endl;
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (token == "ucinewgame") {
            wait_search();
            Engine.new_game();
        } else if (token == "setoption") {
            wait_search();
            std::string name, value, t;
            is >> t;  // "name"
            while (is >> t && t != "value") name += (name.empty() ? "" : " ") + t;
            while (is >> t) value += (value.empty() ? "" : " ") + t;
            std::string lname = name;
            std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            try {
                if (lname == "hash") TT.resize(size_t(std::max(1, std::stoi(value))));
                else if (lname == "threads") Engine.set_threads(std::stoi(value));
                else if (lname == "move overhead") Engine.moveOverhead = std::stoi(value);
                else if (lname == "clear hash") Engine.new_game();
                else std::cout << "info string unknown option " << name << std::endl;
            } catch (...) {
                std::cout << "info string invalid value for " << name << std::endl;
            }
        } else if (token == "position") {
            wait_search();
            parse_position(pos, is);
        } else if (token == "go") {
            wait_search();
            int perftDepth;
            SearchLimits limits = parse_go(is, perftDepth);
            if (perftDepth > 0) {
                auto t0 = std::chrono::steady_clock::now();
                uint64_t total = 0;
                for (auto& [m, n] : perft_divide(pos, perftDepth)) {
                    std::cout << move_to_uci(m) << ": " << n << "\n";
                    total += n;
                }
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                std::cout << "\nNodes searched: " << total << "\nTime: " << ms << " ms" << std::endl;
                continue;
            }
            Position rootPos = pos;
            searchThread = std::thread([rootPos, limits]() {
                SearchResult r = Engine.search(rootPos, limits, print_info);
                std::cout << "bestmove " << move_to_uci(r.bestMove);
                if (r.ponder != MOVE_NONE) std::cout << " ponder " << move_to_uci(r.ponder);
                std::cout << std::endl;
            });
            // When commands come from the command line, finish each search before the next command.
            if (!initialCommands.empty()) wait_search();
        } else if (token == "d") {
            std::cout << pos.pretty() << std::endl;
        } else if (token == "eval") {
            std::cout << eval_trace(pos) << std::endl;
        } else if (token == "perft") {
            int d = 1;
            is >> d;
            auto t0 = std::chrono::steady_clock::now();
            uint64_t n = perft(pos, d);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "perft " << d << ": " << n << " (" << ms << " ms)" << std::endl;
        } else if (token == "bench") {
            wait_search();
            int d = 12;
            is >> d;
            run_bench(d);
        } else if (!token.empty()) {
            std::cout << "info string unknown command: " << token << std::endl;
        }
    }
    wait_search();
}
