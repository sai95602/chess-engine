#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "bitboard.h"
#include "eval.h"
#include "http_server.h"
#include "perft.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

namespace {

void usage() {
    std::cout << "Usage:\n"
                 "  engine                         UCI mode (for chess GUIs)\n"
                 "  engine serve [--host H] [--port P] [--threads N] [--hash MB]\n"
                 "                                 REST API server (env: HOST, PORT, THREADS, HASH)\n"
                 "  engine perft <depth> [fen]     count leaf nodes\n"
                 "  engine perftsuite <file> [maxdepth]\n"
                 "  engine bench [depth]           fixed-depth search benchmark\n"
                 "  engine <uci commands...>       e.g. engine \"position startpos\" \"go depth 10\"\n";
}

int env_int(const char* name, int def) {
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : def;
}

void on_signal(int) { std::_Exit(0); }

}  // namespace

int main(int argc, char** argv) {
    init_bitboards();
    Position::init();
    init_eval();
    init_search();
    TT.resize(64);
    Engine.set_threads(1);

    std::vector<std::string> args(argv + 1, argv + argc);

    if (!args.empty() && (args[0] == "-h" || args[0] == "--help" || args[0] == "help")) {
        usage();
        return 0;
    }

    if (!args.empty() && args[0] == "serve") {
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        std::string host = std::getenv("HOST") ? std::getenv("HOST") : "0.0.0.0";
        int port    = env_int("PORT", 8080);
        int threads = env_int("THREADS", 1);
        int hash    = env_int("HASH", 128);
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            if (args[i] == "--host") host = args[i + 1];
            else if (args[i] == "--port") port = std::atoi(args[i + 1].c_str());
            else if (args[i] == "--threads") threads = std::atoi(args[i + 1].c_str());
            else if (args[i] == "--hash") hash = std::atoi(args[i + 1].c_str());
            else {
                usage();
                return 1;
            }
        }
        Engine.set_threads(threads);
        TT.resize(size_t(std::max(1, hash)));
        return run_http_server(host, port);
    }

    if (!args.empty() && args[0] == "perft") {
        int depth = args.size() > 1 ? std::atoi(args[1].c_str()) : 5;
        std::string fen = Position::StartFEN;
        if (args.size() > 2) {
            fen.clear();
            for (size_t i = 2; i < args.size(); ++i) fen += args[i] + " ";
        }
        uci_loop({"position fen " + fen, "go perft " + std::to_string(depth)});
        return 0;
    }

    if (!args.empty() && args[0] == "perftsuite") {
        std::string file = args.size() > 1 ? args[1] : "tests/perft_suite.epd";
        int maxDepth = args.size() > 2 ? std::atoi(args[2].c_str()) : 99;
        return run_perft_suite(file, maxDepth) ? 0 : 1;
    }

    if (!args.empty() && args[0] == "bench") {
        run_bench(args.size() > 1 ? std::atoi(args[1].c_str()) : 12);
        return 0;
    }

    if (!args.empty() && args[0] == "uci") args.clear();  // interactive UCI (e.g. inside Docker)
    uci_loop(args);
    return 0;
}
