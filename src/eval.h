// Hand-crafted, tapered evaluation function.
#pragma once

#include <string>

#include "types.h"

class Position;

void init_eval();

// Static evaluation in centipawns from the side-to-move's point of view.
int evaluate(const Position& pos);

// Human-readable breakdown of the evaluation terms (white's point of view).
std::string eval_trace(const Position& pos);
