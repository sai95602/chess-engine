// Bitboard utilities and precomputed attack tables (magic bitboards for sliders).
#pragma once

#include "types.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;
constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank4BB = Rank1BB << 24;
constexpr Bitboard Rank5BB = Rank1BB << 32;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;
constexpr Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

constexpr Bitboard sq_bb(int s)   { return 1ULL << s; }
constexpr Bitboard file_bb(int s) { return FileABB << file_of(s); }
constexpr Bitboard rank_bb(int s) { return Rank1BB << (8 * rank_of(s)); }

inline int popcount(Bitboard b) {
#if defined(_MSC_VER)
    return int(__popcnt64(b));
#else
    return __builtin_popcountll(b);
#endif
}

inline int lsb(Bitboard b) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return int(idx);
#else
    return __builtin_ctzll(b);
#endif
}

inline int msb(Bitboard b) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return int(idx);
#else
    return 63 ^ __builtin_clzll(b);
#endif
}

inline int pop_lsb(Bitboard& b) {
    int s = lsb(b);
    b &= b - 1;
    return s;
}

constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

// Directional shifts that do not wrap around the board edges.
constexpr Bitboard shift_north(Bitboard b) { return b << 8; }
constexpr Bitboard shift_south(Bitboard b) { return b >> 8; }
constexpr Bitboard shift_east(Bitboard b)  { return (b & ~FileHBB) << 1; }
constexpr Bitboard shift_west(Bitboard b)  { return (b & ~FileABB) >> 1; }
constexpr Bitboard shift_ne(Bitboard b)    { return (b & ~FileHBB) << 9; }
constexpr Bitboard shift_nw(Bitboard b)    { return (b & ~FileABB) << 7; }
constexpr Bitboard shift_se(Bitboard b)    { return (b & ~FileHBB) >> 7; }
constexpr Bitboard shift_sw(Bitboard b)    { return (b & ~FileABB) >> 9; }

constexpr Bitboard pawn_attacks_bb(Color c, Bitboard b) {
    return c == WHITE ? shift_ne(b) | shift_nw(b) : shift_se(b) | shift_sw(b);
}
constexpr Bitboard shift_up(Color c, Bitboard b) { return c == WHITE ? shift_north(b) : shift_south(b); }

struct Magic {
    Bitboard  mask;
    Bitboard  magic;
    Bitboard* attacks;
    unsigned  shift;
    unsigned index(Bitboard occ) const { return unsigned(((occ & mask) * magic) >> shift); }
};

extern Bitboard PawnAttacks[COLOR_NB][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];   // squares strictly between two aligned squares
extern Bitboard LineBB[64][64];      // full line through two aligned squares (0 if not aligned)
extern Bitboard PassedMask[COLOR_NB][64];   // squares in front on same + adjacent files
extern Bitboard AdjacentFiles[8];
extern Bitboard ForwardFile[COLOR_NB][64];
extern int      SquareDistance[64][64];
extern Magic    RookMagics[64];
extern Magic    BishopMagics[64];

inline Bitboard bishop_attacks(int s, Bitboard occ) {
    const Magic& m = BishopMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard rook_attacks(int s, Bitboard occ) {
    const Magic& m = RookMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard queen_attacks(int s, Bitboard occ) { return bishop_attacks(s, occ) | rook_attacks(s, occ); }

inline Bitboard attacks_bb(PieceType pt, int s, Bitboard occ) {
    switch (pt) {
    case KNIGHT: return KnightAttacks[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK:   return rook_attacks(s, occ);
    case QUEEN:  return queen_attacks(s, occ);
    case KING:   return KingAttacks[s];
    default:     return 0;
    }
}

inline bool aligned(int a, int b, int c) { return LineBB[a][b] & sq_bb(c); }

void init_bitboards();
std::string bb_to_string(Bitboard b);
