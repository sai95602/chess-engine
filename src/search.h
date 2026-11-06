// Alpha-beta (PVS) search with iterative deepening and Lazy SMP.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "position.h"
#include "types.h"

struct SearchLimits {
    int      depth     = 0;   // 0 = no depth limit
    int64_t  movetime  = 0;   // exact time per move (ms)
    int64_t  time[2]   = {0, 0};
    int64_t  inc[2]    = {0, 0};
    int      movestogo = 0;
    uint64_t nodes     = 0;   // 0 = no node limit
    bool     infinite  = false;
};

struct SearchInfo {
    int      depth    = 0;
    int      seldepth = 0;
    int      score    = 0;
    uint64_t nodes    = 0;
    int64_t  timeMs   = 0;
    uint64_t nps      = 0;
    int      hashfull = 0;
    std::vector<Move> pv;
};

struct SearchResult {
    Move     bestMove = MOVE_NONE;
    Move     ponder   = MOVE_NONE;
    int      score    = 0;
    int      depth    = 0;
    int      seldepth = 0;
    uint64_t nodes    = 0;
    int64_t  timeMs   = 0;
    std::vector<Move> pv;
};

class Worker;

class Searcher {
public:
    Searcher();
    ~Searcher();

    void set_threads(int n);
    int  threads() const { return int(workers.size()); }
    void new_game();  // clears TT and history tables

    // Blocking search. onInfo is called by the main thread after each completed iteration.
    SearchResult search(const Position& pos, const SearchLimits& limits,
                        const std::function<void(const SearchInfo&)>& onInfo = nullptr);
    void stop() { stopFlag.store(true, std::memory_order_relaxed); }

    int moveOverhead = 50;  // ms safety margin for communication lag

    // Shared state read by workers
    std::atomic<bool> stopFlag{false};
    uint64_t total_nodes() const;

private:
    friend class Worker;
    std::vector<std::unique_ptr<Worker>> workers;
};

extern Searcher Engine;

// Formats a score for UCI ("cp 34" / "mate 3").
std::string uci_score(int score);

void init_search();
