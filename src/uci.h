// Universal Chess Interface front-end (for GUIs such as Arena, Cute Chess, Banksia).
#pragma once

#include <string>
#include <vector>

#include "position.h"

void uci_loop(const std::vector<std::string>& initialCommands);
void run_bench(int depth);

// Applies "startpos|fen <fen>" plus an optional move list. Returns false with error set on failure.
bool setup_position(Position& pos, const std::string& fen, const std::vector<std::string>& moves, std::string& error);

extern const char* ENGINE_NAME;
extern const char* ENGINE_AUTHOR;
