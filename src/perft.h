// Perft: counts leaf nodes of the legal move tree to verify move generation.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "position.h"

uint64_t perft(Position& pos, int depth);
std::vector<std::pair<Move, uint64_t>> perft_divide(Position& pos, int depth);

// Runs a suite file ("fen ; D1 n ; D2 n ..." per line). Returns true if all pass.
bool run_perft_suite(const std::string& path, int maxDepth);
