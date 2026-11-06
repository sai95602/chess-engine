// Core types shared by every part of the engine.
#pragma once

#include <cstdint>
#include <string>

using Bitboard = uint64_t;
using Key      = uint64_t;

constexpr int MAX_PLY   = 128;
constexpr int MAX_MOVES = 256;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB, NO_PIECE_TYPE = 6 };

enum Piece : int {
    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE, PIECE_NB = 12
};

constexpr Piece     make_piece(Color c, PieceType pt) { return Piece(c * 6 + pt); }
constexpr PieceType type_of(Piece p)  { return PieceType(p % 6); }   // only valid for real pieces
constexpr Color     color_of(Piece p) { return Color(p / 6); }       // only valid for real pieces

// Squares: a1 = 0, b1 = 1, ..., h8 = 63 (little-endian rank-file mapping).
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQ = 64
};

constexpr int file_of(int s) { return s & 7; }
constexpr int rank_of(int s) { return s >> 3; }
constexpr int make_sq(int f, int r) { return r * 8 + f; }
constexpr int relative_rank(Color c, int s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
constexpr int relative_sq(Color c, int s) { return c == WHITE ? s : s ^ 56; }
constexpr int pawn_push(Color c) { return c == WHITE ? 8 : -8; }

// Castling rights bit flags.
enum : int { WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8, ALL_CASTLING = 15 };

// ---------------------------------------------------------------------------
// Move encoding (16 bits):
//   bits 0-5   from square
//   bits 6-11  to square
//   bits 12-13 promotion piece (0=N,1=B,2=R,3=Q)
//   bits 14-15 move type (normal, promotion, en passant, castling)
// Castling is encoded as the king moving two squares (e1g1, e1c1, ...).
// ---------------------------------------------------------------------------
using Move = uint16_t;

enum MoveType : int { NORMAL = 0, PROMOTION = 1 << 14, EN_PASSANT = 2 << 14, CASTLING = 3 << 14 };

constexpr Move MOVE_NONE = 0;
constexpr Move MOVE_NULL = 65;  // from = b1, to = b1: never a real move

constexpr int       from_sq(Move m)      { return m & 63; }
constexpr int       to_sq(Move m)        { return (m >> 6) & 63; }
constexpr int       move_type(Move m)    { return m & (3 << 14); }
constexpr PieceType promotion_type(Move m) { return PieceType(((m >> 12) & 3) + KNIGHT); }

constexpr Move make_move(int from, int to) { return Move(from | (to << 6)); }
constexpr Move make_special(int type, int from, int to, PieceType promo = KNIGHT) {
    return Move(type | ((promo - KNIGHT) << 12) | (to << 6) | from);
}

// ---------------------------------------------------------------------------
// Scores
// ---------------------------------------------------------------------------
constexpr int VALUE_ZERO     = 0;
constexpr int VALUE_DRAW     = 0;
constexpr int VALUE_MATE     = 32000;
constexpr int VALUE_INFINITE = 32001;
constexpr int VALUE_NONE     = 32002;
constexpr int VALUE_MATE_IN_MAX_PLY  = VALUE_MATE - MAX_PLY;
constexpr int VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

constexpr int mate_in(int ply)  { return VALUE_MATE - ply; }
constexpr int mated_in(int ply) { return -VALUE_MATE + ply; }

// Tapered (middlegame, endgame) score pair.
struct Score {
    int mg = 0, eg = 0;
    constexpr Score() = default;
    constexpr Score(int m, int e) : mg(m), eg(e) {}
    constexpr Score operator+(const Score& o) const { return Score(mg + o.mg, eg + o.eg); }
    constexpr Score operator-(const Score& o) const { return Score(mg - o.mg, eg - o.eg); }
    constexpr Score operator-() const { return Score(-mg, -eg); }
    constexpr Score operator*(int k) const { return Score(mg * k, eg * k); }
    Score& operator+=(const Score& o) { mg += o.mg; eg += o.eg; return *this; }
    Score& operator-=(const Score& o) { mg -= o.mg; eg -= o.eg; return *this; }
};
constexpr Score S(int mg, int eg) { return Score(mg, eg); }

// Square / move string helpers
inline std::string square_to_string(int s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

inline std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    if (m == MOVE_NULL) return "0000";
    std::string s = square_to_string(from_sq(m)) + square_to_string(to_sq(m));
    if (move_type(m) == PROMOTION) s += "nbrq"[promotion_type(m) - KNIGHT];
    return s;
}
