#include "perft.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "movegen.h"

uint64_t perft(Position& pos, int depth) {
    MoveList list;
    generate_legal(pos, list);
    if (depth <= 1) return depth == 1 ? uint64_t(list.size) : 1;  // bulk counting at the last ply
    uint64_t n = 0;
    for (int i = 0; i < list.size; ++i) {
        pos.do_move(list.moves[i].move);
        n += perft(pos, depth - 1);
        pos.undo_move(list.moves[i].move);
    }
    return n;
}

std::vector<std::pair<Move, uint64_t>> perft_divide(Position& pos, int depth) {
    std::vector<std::pair<Move, uint64_t>> out;
    MoveList list;
    generate_legal(pos, list);
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i].move;
        pos.do_move(m);
        out.emplace_back(m, depth > 1 ? perft(pos, depth - 1) : 1);
        pos.undo_move(m);
    }
    return out;
}

bool run_perft_suite(const std::string& path, int maxDepth) {
    std::ifstream in(path);
    if (!in) {
        std::cout << "cannot open " << path << "\n";
        return false;
    }
    std::string line;
    int passed = 0, failed = 0;
    uint64_t totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ';')) parts.push_back(tok);
        if (parts.size() < 2) continue;

        Position pos;
        std::string err;
        if (!pos.set(parts[0], &err)) {
            std::cout << "bad fen: " << parts[0] << " (" << err << ")\n";
            ++failed;
            continue;
        }
        for (size_t i = 1; i < parts.size(); ++i) {
            std::stringstream ps(parts[i]);
            std::string d;
            uint64_t expected;
            ps >> d >> expected;
            int depth = std::stoi(d.substr(1));
            if (depth > maxDepth) continue;
            uint64_t got = perft(pos, depth);
            totalNodes += got;
            bool ok = got == expected;
            (ok ? passed : failed)++;
            std::cout << (ok ? "[PASS] " : "[FAIL] ") << "depth " << depth << " expected " << expected << " got " << got
                      << "  " << parts[0] << "\n";
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "\n" << passed << " passed, " << failed << " failed. " << totalNodes << " nodes in " << secs << "s ("
              << uint64_t(totalNodes / (secs > 0 ? secs : 1)) << " nps)\n";
    return failed == 0;
}
