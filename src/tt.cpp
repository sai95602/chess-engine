#include "tt.h"

#include <cstdlib>
#include <cstring>
#include <new>

TranspositionTable TT;

TranspositionTable::~TranspositionTable() { std::free(table); }

void TranspositionTable::resize(size_t mb) {
    std::free(table);
    if (mb < 1) mb = 1;
    clusterCount = mb * 1024 * 1024 / sizeof(Cluster);
    table = static_cast<Cluster*>(std::malloc(clusterCount * sizeof(Cluster)));
    if (!table) throw std::bad_alloc();
    clear();
}

void TranspositionTable::clear() {
    std::memset(table, 0, clusterCount * sizeof(Cluster));
    generation8 = 0;
}

TTEntry* TranspositionTable::probe(Key key, bool& found) const {
    TTEntry* tte = first_entry(key);
    uint16_t k16 = uint16_t(key);

    for (int i = 0; i < ClusterSize; ++i) {
        if (tte[i].key16 == k16 && tte[i].depth8) {
            // Refresh generation so the entry isn't considered stale.
            tte[i].genBound = uint8_t(generation8 | (tte[i].genBound & 3));
            found = true;
            return &tte[i];
        }
    }

    // Replace the least valuable entry: shallow and/or from an old search.
    TTEntry* replace = tte;
    auto worth = [&](const TTEntry& e) {
        int age = uint8_t(generation8 - (e.genBound & 0xFC)) & 0xFC;  // 4 per generation
        return int(e.depth8) - age * 2;
    };
    for (int i = 1; i < ClusterSize; ++i)
        if (worth(tte[i]) < worth(*replace)) replace = &tte[i];
    found = false;
    return replace;
}

void TranspositionTable::store(TTEntry* e, Key key, int score, int eval, Bound b, int depth, Move m) {
    uint16_t k16 = uint16_t(key);
    // Keep the old best move if we have none for this position
    if (m != MOVE_NONE || e->key16 != k16) e->move = m;

    if (b == BOUND_EXACT || e->key16 != k16 || depth + TTEntry::DEPTH_OFFSET + 4 > e->depth8 ||
        (e->genBound & 0xFC) != generation8) {
        e->key16    = k16;
        e->score    = int16_t(score);
        e->eval     = int16_t(eval);
        e->depth8   = uint8_t(depth + TTEntry::DEPTH_OFFSET);
        e->genBound = uint8_t(generation8 | b);
    }
}

int TranspositionTable::hashfull() const {
    int cnt = 0;
    for (size_t i = 0; i < 1000 && i < clusterCount; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            cnt += table[i].entry[j].depth8 && (table[i].entry[j].genBound & 0xFC) == generation8;
    return cnt / ClusterSize;
}
