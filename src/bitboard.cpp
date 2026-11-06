#include "bitboard.h"

#include <cstdlib>

Bitboard PawnAttacks[COLOR_NB][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
Bitboard PassedMask[COLOR_NB][64];
Bitboard AdjacentFiles[8];
Bitboard ForwardFile[COLOR_NB][64];
int      SquareDistance[64][64];
Magic    RookMagics[64];
Magic    BishopMagics[64];

namespace {

Bitboard RookTable[0x19000];   // 102400 entries: total size of all rook attack sets
Bitboard BishopTable[0x1480];  // 5248 entries

const int RookDirs[4][2]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

Bitboard sliding_attack(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard att = 0;
    for (int d = 0; d < 4; ++d) {
        int f = file_of(sq) + dirs[d][0], r = rank_of(sq) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            Bitboard b = sq_bb(make_sq(f, r));
            att |= b;
            if (occ & b) break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return att;
}

struct PRNG {
    uint64_t s;
    explicit PRNG(uint64_t seed) : s(seed) {}
    uint64_t rand() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    uint64_t sparse() { return rand() & rand() & rand(); }
};

// Finds "fancy" magic numbers at startup. With a fixed seed this is deterministic
// and takes only a few milliseconds.
void init_magics(Magic* magics, Bitboard* table, const int dirs[4][2]) {
    static Bitboard occupancy[4096], reference[4096];
    static int epoch[4096];
    int cnt = 0, size = 0;
    PRNG rng(0x9E3779B97F4A7C15ULL);

    for (int s = 0; s < 64; ++s) {
        Magic& m = magics[s];
        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));
        m.mask    = sliding_attack(s, 0, dirs) & ~edges;
        m.shift   = 64 - popcount(m.mask);
        m.attacks = (s == 0) ? table : magics[s - 1].attacks + size;

        // Enumerate all subsets of the mask (Carry-Rippler trick).
        size = 0;
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(s, b, dirs);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;) m.magic = rng.sparse();
            for (++cnt, i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx]     = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i])
                    break;  // collision: try another magic
            }
        }
    }
}

Bitboard step_attacks(int s, const int (*steps)[2], int n) {
    Bitboard b = 0;
    for (int i = 0; i < n; ++i) {
        int f = file_of(s) + steps[i][0], r = rank_of(s) + steps[i][1];
        if (f >= 0 && f < 8 && r >= 0 && r < 8) b |= sq_bb(make_sq(f, r));
    }
    return b;
}

}  // namespace

void init_bitboards() {
    static const int KnightSteps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static const int KingSteps[8][2]   = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

    for (int s = 0; s < 64; ++s) {
        KnightAttacks[s]      = step_attacks(s, KnightSteps, 8);
        KingAttacks[s]        = step_attacks(s, KingSteps, 8);
        PawnAttacks[WHITE][s] = pawn_attacks_bb(WHITE, sq_bb(s));
        PawnAttacks[BLACK][s] = pawn_attacks_bb(BLACK, sq_bb(s));
    }

    init_magics(RookMagics, RookTable, RookDirs);
    init_magics(BishopMagics, BishopTable, BishopDirs);

    for (int f = 0; f < 8; ++f)
        AdjacentFiles[f] = (f > 0 ? FileABB << (f - 1) : 0) | (f < 7 ? FileABB << (f + 1) : 0);

    for (int s1 = 0; s1 < 64; ++s1) {
        for (int s2 = 0; s2 < 64; ++s2) {
            int df = std::abs(file_of(s1) - file_of(s2)), dr = std::abs(rank_of(s1) - rank_of(s2));
            SquareDistance[s1][s2] = df > dr ? df : dr;
            BetweenBB[s1][s2] = LineBB[s1][s2] = 0;
            if (s1 == s2) continue;
            if (bishop_attacks(s1, 0) & sq_bb(s2)) {
                LineBB[s1][s2]    = (bishop_attacks(s1, 0) & bishop_attacks(s2, 0)) | sq_bb(s1) | sq_bb(s2);
                BetweenBB[s1][s2] = bishop_attacks(s1, sq_bb(s2)) & bishop_attacks(s2, sq_bb(s1));
            } else if (rook_attacks(s1, 0) & sq_bb(s2)) {
                LineBB[s1][s2]    = (rook_attacks(s1, 0) & rook_attacks(s2, 0)) | sq_bb(s1) | sq_bb(s2);
                BetweenBB[s1][s2] = rook_attacks(s1, sq_bb(s2)) & rook_attacks(s2, sq_bb(s1));
            }
        }

        // Forward file / passed-pawn masks
        for (int c = 0; c < 2; ++c) {
            Bitboard fwd = 0;
            int s = s1, r = rank_of(s);
            for (int rr = (c == WHITE ? r + 1 : r - 1); rr >= 0 && rr < 8; rr += (c == WHITE ? 1 : -1))
                fwd |= Rank1BB << (8 * rr);
            ForwardFile[c][s] = fwd & file_bb(s);
            PassedMask[c][s]  = fwd & (file_bb(s) | AdjacentFiles[file_of(s)]);
        }
    }
}

std::string bb_to_string(Bitboard b) {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) s += (b & sq_bb(make_sq(f, r))) ? "X " : ". ";
        s += '\n';
    }
    return s;
}
