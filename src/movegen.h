// Pseudo-legal move generation using bitboards.
#pragma once

#include "types.h"

class Position;

struct ExtMove {
    Move move;
    int  score;
};

struct MoveList {
    ExtMove moves[MAX_MOVES];
    int     size = 0;
    void add(Move m) { moves[size++].move = m; }
};

enum GenType {
    GEN_CAPTURES,  // captures + queen promotions
    GEN_QUIETS,    // non-captures + under-promotions
    GEN_ALL        // everything
};

// Generates pseudo-legal moves (legality is checked with Position::legal()).
// When the side to move is in check, only check evasions are generated,
// regardless of the requested type.
void generate(const Position& pos, MoveList& list, GenType type);

// Generates fully legal moves.
void generate_legal(const Position& pos, MoveList& list);
