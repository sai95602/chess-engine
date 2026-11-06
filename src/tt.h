// Shared transposition table. Lock-free: entries may be torn by concurrent
// writes, so every probed move is re-validated against the move list.
#pragma once

#include <cstddef>
#include <cstdint>

#include "types.h"

#if defined(_MSC_VER)
#include <intrin.h>
#include <xmmintrin.h>
#endif

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    uint16_t key16;
    uint16_t move;
    int16_t  score;
    int16_t  eval;
    uint8_t  depth8;    // depth + DEPTH_OFFSET; 0 = empty
    uint8_t  genBound;  // generation (upper 6 bits) | bound (lower 2 bits)

    static constexpr int DEPTH_OFFSET = 8;
    int   depth() const { return int(depth8) - DEPTH_OFFSET; }
    Bound bound() const { return Bound(genBound & 3); }
};

class TranspositionTable {
public:
    static constexpr int ClusterSize = 3;
    struct Cluster {
        TTEntry entry[ClusterSize];
        char    padding[2];
    };
    static_assert(sizeof(Cluster) == 32, "Cluster must be 32 bytes");

    ~TranspositionTable();
    void resize(size_t mb);
    void clear();
    void new_search() { generation8 += 4; }

    TTEntry* probe(Key key, bool& found) const;
    void store(TTEntry* e, Key key, int score, int eval, Bound b, int depth, Move m);
    int hashfull() const;  // permille

    void prefetch(Key key) const {
#if defined(__GNUC__)
        __builtin_prefetch(first_entry(key));
#elif defined(_MSC_VER)
        _mm_prefetch(reinterpret_cast<const char*>(first_entry(key)), _MM_HINT_T0);
#endif
    }

private:
    TTEntry* first_entry(Key key) const {
#if defined(__SIZEOF_INT128__)
        size_t idx = size_t((unsigned __int128)key * (unsigned __int128)clusterCount >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
        size_t idx = size_t(__umulh(key, uint64_t(clusterCount)));
#else
        size_t idx = size_t(key % clusterCount);
#endif
        return &table[idx].entry[0];
    }

    Cluster* table        = nullptr;
    size_t   clusterCount = 0;
    uint8_t  generation8  = 0;
};

extern TranspositionTable TT;

// Mate scores are stored relative to the node, not the root.
inline int score_to_tt(int v, int ply) {
    return v >= VALUE_MATE_IN_MAX_PLY ? v + ply : v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
}
inline int score_from_tt(int v, int ply, int rule50) {
    if (v == VALUE_NONE) return VALUE_NONE;
    if (v >= VALUE_MATE_IN_MAX_PLY) {
        // Don't trust a mate score that the 50-move rule might invalidate
        if (VALUE_MATE - v > 99 - rule50) return VALUE_MATE_IN_MAX_PLY - 1;
        return v - ply;
    }
    if (v <= VALUE_MATED_IN_MAX_PLY) {
        if (VALUE_MATE + v > 99 - rule50) return VALUE_MATED_IN_MAX_PLY + 1;
        return v + ply;
    }
    return v;
}
