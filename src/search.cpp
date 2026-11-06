#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "eval.h"
#include "movegen.h"
#include "tt.h"

Searcher Engine;

namespace {

using Clock = std::chrono::steady_clock;

int Reductions[MAX_PLY + 1][MAX_MOVES];

int64_t elapsed_ms(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

inline int history_bonus(int depth) { return std::min(2000, 160 * depth - 120); }

inline void update_stat(int16_t& h, int bonus) {
    constexpr int MAX = 16384;
    bonus = std::clamp(bonus, -MAX, MAX);
    h = int16_t(h + bonus - h * std::abs(bonus) / MAX);
}

constexpr int CONT_SIZE = PIECE_NB * 64;  // one continuation-history block

}  // namespace

void init_search() {
    for (int d = 1; d <= MAX_PLY; ++d)
        for (int m = 1; m < MAX_MOVES; ++m)
            Reductions[d][m] = int(0.77 + std::log(double(d)) * std::log(double(m)) / 2.36);
    for (int m = 0; m < MAX_MOVES; ++m) Reductions[0][m] = 0;
    for (int d = 0; d <= MAX_PLY; ++d) Reductions[d][0] = 0;
}

std::string uci_score(int v) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string((VALUE_MATE - v + 1) / 2);
    if (v <= VALUE_MATED_IN_MAX_PLY) return "mate " + std::to_string(-(VALUE_MATE + v) / 2);
    return "cp " + std::to_string(v);
}

// ---------------------------------------------------------------------------
// Worker: one search thread. Each has its own position copy and history
// tables; all workers share the transposition table (Lazy SMP).
// ---------------------------------------------------------------------------
struct Stack {
    int      ply;
    Move     currentMove;
    Piece    movedPiece;
    Move     excluded;
    int      staticEval;
    Move     killers[2];
    int16_t* contHist;  // continuation history block for (movedPiece, to)
};

class Worker {
public:
    Worker(Searcher& s, int id) : searcher(s), id(id) {
        contHist.assign(size_t(CONT_SIZE) * CONT_SIZE, 0);
        std::memset(dummyCont, 0, sizeof(dummyCont));
        clear();
    }

    void clear() {
        std::memset(history, 0, sizeof(history));
        std::memset(captureHistory, 0, sizeof(captureHistory));
        std::memset(counterMoves, 0, sizeof(counterMoves));
        std::fill(contHist.begin(), contHist.end(), int16_t(0));
    }

    void start(const Position& p, const SearchLimits& lim, Clock::time_point t0, int64_t soft, int64_t hard) {
        pos = p;
        limits = lim;
        startTime = t0;
        softLimit = soft;
        hardLimit = hard;
        nodes.store(0, std::memory_order_relaxed);
        completedDepth = 0;
        bestMove = MOVE_NONE;
        bestScore = -VALUE_INFINITE;
        bestPV.clear();
        seldepth = 0;
    }

    void iterative_deepening(const std::function<void(const SearchInfo&)>* onInfo);

    Searcher&     searcher;
    int           id;
    Position      pos;
    SearchLimits  limits;
    Clock::time_point startTime;
    int64_t       softLimit = 0, hardLimit = 0;
    std::atomic<uint64_t> nodes{0};

    int  completedDepth = 0;
    Move bestMove       = MOVE_NONE;
    int  bestScore      = -VALUE_INFINITE;
    int  bestSeldepth   = 0;
    std::vector<Move> bestPV;

private:
    int  search(int alpha, int beta, int depth, Stack* ss, bool cutNode);
    int  qsearch(int alpha, int beta, Stack* ss);
    void score_moves(MoveList& list, Move ttMove, Stack* ss);
    void update_quiet_stats(Stack* ss, Move best, const Move* quiets, int nq, int depth);
    void update_capture_stats(Move best, const Move* caps, int nc, int depth);
    int  quiet_history(Stack* ss, Piece pc, Move m) const;
    void check_time();
    bool stopped() const { return searcher.stopFlag.load(std::memory_order_relaxed); }
    void inc_nodes() { nodes.store(nodes.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }

    static Move pick_next(MoveList& list, int idx) {
        int best = idx;
        for (int j = idx + 1; j < list.size; ++j)
            if (list.moves[j].score > list.moves[best].score) best = j;
        std::swap(list.moves[idx], list.moves[best]);
        return list.moves[idx].move;
    }

    int16_t* cont_block(Piece pc, int to) { return &contHist[size_t(pc * 64 + to) * CONT_SIZE]; }

    int16_t history[COLOR_NB][64][64];
    int16_t captureHistory[PIECE_NB][64][PIECE_TYPE_NB];
    Move    counterMoves[PIECE_NB][64];
    std::vector<int16_t> contHist;
    int16_t dummyCont[CONT_SIZE];

    Move pvTable[MAX_PLY + 2][MAX_PLY + 2];
    int  pvLength[MAX_PLY + 2];

    int  rootDepth = 0;
    int  seldepth  = 0;
    int  checkCounter = 0;
    Move rootIterBest = MOVE_NONE;
    int  rootIterScore = -VALUE_INFINITE;
    std::vector<Move> rootIterPV;
};

// ---------------------------------------------------------------------------
// Move ordering
// ---------------------------------------------------------------------------
int Worker::quiet_history(Stack* ss, Piece pc, Move m) const {
    int to = to_sq(m);
    int idx = pc * 64 + to;
    return history[pos.side_to_move()][from_sq(m)][to] + (ss - 1)->contHist[idx] + (ss - 2)->contHist[idx] +
           (ss - 4)->contHist[idx] / 2;
}

void Worker::score_moves(MoveList& list, Move ttMove, Stack* ss) {
    Move counter = MOVE_NONE;
    if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->currentMove != MOVE_NULL)
        counter = counterMoves[(ss - 1)->movedPiece][to_sq((ss - 1)->currentMove)];

    for (int i = 0; i < list.size; ++i) {
        ExtMove& em = list.moves[i];
        Move m = em.move;
        if (m == ttMove) {
            em.score = 1 << 30;
            continue;
        }
        Piece pc = pos.moved_piece(m);
        if (move_type(m) == PROMOTION) {
            if (promotion_type(m) == QUEEN) {
                int cap = pos.piece_on(to_sq(m)) != NO_PIECE ? SeeValue[pos.piece_on(to_sq(m))] : 0;
                em.score = 600'000'000 + cap * 8;
            } else
                em.score = -300'000'000 + promotion_type(m);
            continue;
        }
        if (pos.is_capture(m)) {
            Piece captured = move_type(m) == EN_PASSANT ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to_sq(m));
            int mvv = SeeValue[captured] * 8 + captureHistory[pc][to_sq(m)][type_of(captured)] / 16;
            em.score = (pos.see_ge(m, -50) ? 500'000'000 : -100'000'000) + mvv;
            continue;
        }
        if (m == ss->killers[0]) em.score = 400'000'000;
        else if (m == ss->killers[1]) em.score = 399'000'000;
        else if (m == counter) em.score = 398'000'000;
        else {
            em.score = quiet_history(ss, pc, m);
            if (pos.check_squares(type_of(pc)) & sq_bb(to_sq(m))) em.score += 6000;  // direct check
        }
    }
}

void Worker::update_quiet_stats(Stack* ss, Move best, const Move* quiets, int nq, int depth) {
    Color us = pos.side_to_move();
    int bonus = history_bonus(depth);

    if (ss->killers[0] != best) {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = best;
    }
    if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->currentMove != MOVE_NULL)
        counterMoves[(ss - 1)->movedPiece][to_sq((ss - 1)->currentMove)] = best;

    auto update_cont = [&](Piece pc, int to, int b) {
        int idx = pc * 64 + to;
        for (int k : {1, 2, 4})
            if ((ss - k)->contHist != dummyCont) update_stat((ss - k)->contHist[idx], b);
    };

    Piece bpc = pos.moved_piece(best);
    update_stat(history[us][from_sq(best)][to_sq(best)], bonus);
    update_cont(bpc, to_sq(best), bonus);

    for (int i = 0; i < nq; ++i) {
        Move m = quiets[i];
        if (m == best) continue;
        update_stat(history[us][from_sq(m)][to_sq(m)], -bonus);
        update_cont(pos.moved_piece(m), to_sq(m), -bonus);
    }
}

void Worker::update_capture_stats(Move best, const Move* caps, int nc, int depth) {
    int bonus = history_bonus(depth);
    auto upd = [&](Move m, int b) {
        Piece pc = pos.moved_piece(m);
        Piece captured = move_type(m) == EN_PASSANT ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to_sq(m));
        if (captured == NO_PIECE) return;
        update_stat(captureHistory[pc][to_sq(m)][type_of(captured)], b);
    };
    if (best != MOVE_NONE) upd(best, bonus);
    for (int i = 0; i < nc; ++i)
        if (caps[i] != best) upd(caps[i], -bonus);
}

// ---------------------------------------------------------------------------
// Time / node control (main thread only)
// ---------------------------------------------------------------------------
void Worker::check_time() {
    if (--checkCounter > 0) return;
    checkCounter = 1024;
    if (limits.infinite) return;
    if (limits.nodes && searcher.total_nodes() >= limits.nodes) searcher.stop();
    if (hardLimit > 0 && elapsed_ms(startTime) >= hardLimit) searcher.stop();
}

// ---------------------------------------------------------------------------
// Quiescence search: resolve captures so we only evaluate quiet positions.
// ---------------------------------------------------------------------------
int Worker::qsearch(int alpha, int beta, Stack* ss) {
    const bool pvNode = beta - alpha > 1;
    if (id == 0) check_time();
    inc_nodes();
    if (ss->ply > seldepth) seldepth = ss->ply;
    pvLength[ss->ply] = 0;

    if (pos.is_draw(ss->ply)) return VALUE_DRAW;
    if (ss->ply >= MAX_PLY - 1) return pos.in_check() ? VALUE_DRAW : evaluate(pos);

    bool     ttHit;
    TTEntry* tte     = TT.probe(pos.key(), ttHit);
    Move     ttMove  = ttHit ? tte->move : MOVE_NONE;
    int      ttScore = ttHit ? score_from_tt(tte->score, ss->ply, pos.rule50()) : VALUE_NONE;
    Bound    ttBound = ttHit ? tte->bound() : BOUND_NONE;

    if (!pvNode && ttHit && ttScore != VALUE_NONE && (ttBound & (ttScore >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttScore;

    const bool inCheck = pos.in_check();
    int bestScore, staticEval;
    if (inCheck) {
        bestScore = staticEval = -VALUE_INFINITE;
    } else {
        staticEval = (ttHit && tte->eval != VALUE_NONE) ? tte->eval : evaluate(pos);
        bestScore  = staticEval;
        if (ttScore != VALUE_NONE && (ttBound & (ttScore > bestScore ? BOUND_LOWER : BOUND_UPPER))) bestScore = ttScore;
        if (bestScore >= beta) {
            if (!ttHit) TT.store(tte, pos.key(), score_to_tt(bestScore, ss->ply), staticEval, BOUND_LOWER, 0, MOVE_NONE);
            return bestScore;
        }
        if (bestScore > alpha) alpha = bestScore;
    }

    MoveList list;
    generate(pos, list, GEN_CAPTURES);  // evasions if in check

    // Score: MVV-LVA for captures, history for quiet evasions
    for (int i = 0; i < list.size; ++i) {
        ExtMove& em = list.moves[i];
        Move m = em.move;
        if (m == ttMove) { em.score = 1 << 30; continue; }
        Piece pc = pos.moved_piece(m);
        if (pos.is_capture(m) || move_type(m) == PROMOTION) {
            Piece captured = move_type(m) == EN_PASSANT ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to_sq(m));
            int victim = captured == NO_PIECE ? 0 : SeeValue[captured];
            if (move_type(m) == PROMOTION) victim += promotion_type(m) == QUEEN ? 900 : -1000;
            em.score = 100'000'000 + victim * 16 - type_of(pc) +
                       (captured != NO_PIECE ? captureHistory[pc][to_sq(m)][type_of(captured)] / 32 : 0);
        } else {
            em.score = quiet_history(ss, pc, m);
        }
    }

    Move bestMove = MOVE_NONE;
    int  moveCount = 0;
    const int futilityBase = staticEval + 150;

    for (int i = 0; i < list.size; ++i) {
        Move m = pick_next(list, i);
        if (!pos.legal(m)) continue;
        ++moveCount;

        if (!inCheck && bestScore > VALUE_MATED_IN_MAX_PLY) {
            bool givesCheck = pos.gives_check(m);
            // Delta / futility pruning: even winning the piece can't reach alpha
            if (!givesCheck && move_type(m) != PROMOTION) {
                Piece captured = move_type(m) == EN_PASSANT ? make_piece(~pos.side_to_move(), PAWN) : pos.piece_on(to_sq(m));
                int futilityValue = futilityBase + SeeValue[captured];
                if (futilityValue <= alpha) {
                    bestScore = std::max(bestScore, futilityValue);
                    continue;
                }
            }
            // Skip captures that lose material
            if (!pos.see_ge(m, 0)) continue;
        }

        ss->currentMove = m;
        ss->movedPiece  = pos.moved_piece(m);
        ss->contHist    = cont_block(ss->movedPiece, to_sq(m));
        pos.do_move(m);
        TT.prefetch(pos.key());
        int v = -qsearch(-beta, -alpha, ss + 1);
        pos.undo_move(m);

        if (stopped()) return 0;

        if (v > bestScore) {
            bestScore = v;
            if (v > alpha) {
                bestMove = m;
                if (pvNode) {
                    pvTable[ss->ply][0] = m;
                    std::memcpy(&pvTable[ss->ply][1], pvTable[ss->ply + 1], sizeof(Move) * pvLength[ss->ply + 1]);
                    pvLength[ss->ply] = pvLength[ss->ply + 1] + 1;
                }
                if (v >= beta) break;
                alpha = v;
            }
        }
    }

    if (inCheck && moveCount == 0) return mated_in(ss->ply);

    Bound b = bestScore >= beta ? BOUND_LOWER : BOUND_UPPER;
    TT.store(tte, pos.key(), score_to_tt(bestScore, ss->ply), inCheck ? VALUE_NONE : staticEval, b, 0, bestMove);
    return bestScore;
}

// ---------------------------------------------------------------------------
// Main search: principal variation search (negamax alpha-beta).
// ---------------------------------------------------------------------------
int Worker::search(int alpha, int beta, int depth, Stack* ss, bool cutNode) {
    const bool pvNode   = beta - alpha > 1;
    const bool rootNode = ss->ply == 0;

    if (depth <= 0) return qsearch(alpha, beta, ss);

    if (id == 0) check_time();
    inc_nodes();
    pvLength[ss->ply] = 0;
    if (ss->ply > seldepth) seldepth = ss->ply;

    if (!rootNode) {
        if (stopped()) return 0;
        if (pos.is_draw(ss->ply)) return VALUE_DRAW + 1 - int(nodes.load(std::memory_order_relaxed) & 2);
        if (ss->ply >= MAX_PLY - 1) return pos.in_check() ? VALUE_DRAW : evaluate(pos);

        // Mate distance pruning
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta) return alpha;
    }

    const Color us       = pos.side_to_move();
    const bool  inCheck  = pos.in_check();
    const Move  excluded = ss->excluded;
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = MOVE_NONE;
    (ss + 1)->excluded = MOVE_NONE;

    // --- Transposition table lookup -------------------------------------
    bool     ttHit = false;
    TTEntry* tte   = TT.probe(pos.key(), ttHit);
    if (excluded) ttHit = false;  // singular search: don't use / overwrite the TT for this node
    Move  ttMove  = rootNode ? this->bestMove : (ttHit ? tte->move : MOVE_NONE);
    if (rootNode && ttMove == MOVE_NONE && ttHit) ttMove = tte->move;
    int   ttScore = ttHit ? score_from_tt(tte->score, ss->ply, pos.rule50()) : VALUE_NONE;
    int   ttDepth = ttHit ? tte->depth() : -99;
    Bound ttBound = ttHit ? tte->bound() : BOUND_NONE;
    bool  ttCapture = ttMove != MOVE_NONE && pos.is_capture_or_promotion(ttMove);

    if (!pvNode && ttHit && ttDepth >= depth && ttScore != VALUE_NONE &&
        (ttBound & (ttScore >= beta ? BOUND_LOWER : BOUND_UPPER)) && pos.rule50() < 90)
        return ttScore;

    // --- Static evaluation ----------------------------------------------
    int  eval;
    bool improving;
    if (inCheck) {
        ss->staticEval = eval = VALUE_NONE;
        improving = false;
    } else {
        if (excluded) {
            eval = ss->staticEval;  // same position: reuse
        } else if (ttHit) {
            ss->staticEval = eval = tte->eval != VALUE_NONE ? tte->eval : evaluate(pos);
            if (ttScore != VALUE_NONE && (ttBound & (ttScore > eval ? BOUND_LOWER : BOUND_UPPER))) eval = ttScore;
        } else {
            ss->staticEval = eval = evaluate(pos);
            TT.store(tte, pos.key(), VALUE_NONE, eval, BOUND_NONE, -TTEntry::DEPTH_OFFSET + 1, MOVE_NONE);
        }
        if ((ss - 2)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 2)->staticEval;
        else if ((ss - 4)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 4)->staticEval;
        else improving = true;
    }

    // --- Whole-node pruning ------------------------------------------------
    if (!pvNode && !inCheck && !excluded) {
        // Reverse futility pruning: we're so far above beta a quiet move won't drop us below it.
        if (depth <= 8 && eval - 75 * (depth - improving) >= beta && eval < VALUE_MATE_IN_MAX_PLY)
            return (eval + beta) / 2;

        // Razoring: hopeless position at low depth -> verify with qsearch.
        if (depth <= 3 && eval + 200 + 220 * depth < alpha) {
            int v = qsearch(alpha - 1, alpha, ss);
            if (v < alpha) return v;
        }

        // Null-move pruning: give the opponent a free move; if we still beat beta, cut.
        if ((ss - 1)->currentMove != MOVE_NULL && depth >= 3 && eval >= beta && ss->staticEval >= beta - 20 * depth + 180 &&
            pos.non_pawn_material(us) && beta > VALUE_MATED_IN_MAX_PLY) {
            int R = 3 + depth / 3 + std::min((eval - beta) / 200, 3);
            ss->currentMove = MOVE_NULL;
            ss->movedPiece  = NO_PIECE;
            ss->contHist    = dummyCont;
            pos.do_null_move();
            int v = -search(-beta, -beta + 1, depth - R, ss + 1, !cutNode);
            pos.undo_null_move();
            if (stopped()) return 0;
            if (v >= beta) return v >= VALUE_MATE_IN_MAX_PLY ? beta : v;
        }

        // ProbCut: a good capture that beats beta by a margin at reduced depth likely cuts.
        int probBeta = beta + 200;
        if (depth >= 5 && std::abs(beta) < VALUE_MATE_IN_MAX_PLY &&
            !(ttHit && ttDepth >= depth - 3 && ttScore != VALUE_NONE && ttScore < probBeta)) {
            MoveList caps;
            generate(pos, caps, GEN_CAPTURES);
            for (int i = 0; i < caps.size; ++i) {
                Move m = caps.moves[i].move;
                if (!pos.is_capture(m) || !pos.see_ge(m, probBeta - ss->staticEval)) continue;
                if (!pos.legal(m)) continue;
                ss->currentMove = m;
                ss->movedPiece  = pos.moved_piece(m);
                ss->contHist    = cont_block(ss->movedPiece, to_sq(m));
                pos.do_move(m);
                int v = -qsearch(-probBeta, -probBeta + 1, ss + 1);
                if (v >= probBeta) v = -search(-probBeta, -probBeta + 1, depth - 4, ss + 1, !cutNode);
                pos.undo_move(m);
                if (stopped()) return 0;
                if (v >= probBeta) {
                    TT.store(tte, pos.key(), score_to_tt(v, ss->ply), ss->staticEval, BOUND_LOWER, depth - 3, m);
                    return v;
                }
            }
        }
    }

    // Internal iterative reduction: no TT move means ordering will be poor; search shallower.
    if (!rootNode && depth >= 4 && ttMove == MOVE_NONE && (pvNode || cutNode)) --depth;

    // --- Move loop -------------------------------------------------------
    MoveList list;
    generate(pos, list, GEN_ALL);
    score_moves(list, ttMove, ss);

    Move quietsTried[64], capturesTried[32];
    int  nQuiets = 0, nCaptures = 0;
    int  moveCount = 0;
    int  bestScore = -VALUE_INFINITE;
    Move bestMove  = MOVE_NONE;
    bool skipQuiets = false;
    const int lmpLimit = (3 + depth * depth) / (2 - improving);

    for (int i = 0; i < list.size; ++i) {
        Move m = pick_next(list, i);
        if (m == excluded) continue;
        if (!pos.legal(m)) continue;

        const bool capture    = pos.is_capture(m);
        const bool promotion  = move_type(m) == PROMOTION;
        const bool quiet      = !capture && !promotion;
        if (quiet && skipQuiets) continue;

        ++moveCount;
        const bool  givesCheck = pos.gives_check(m);
        const Piece pc         = pos.moved_piece(m);
        const int   hist       = quiet ? quiet_history(ss, pc, m) : 0;

        // --- Shallow-depth pruning ---------------------------------------
        if (!rootNode && bestScore > VALUE_MATED_IN_MAX_PLY && pos.non_pawn_material(us)) {
            int lmrDepth = std::max(depth - 1 - Reductions[std::min(depth, MAX_PLY)][std::min(moveCount, MAX_MOVES - 1)], 0);
            if (quiet && !givesCheck) {
                // Late move pruning
                if (depth <= 8 && moveCount >= lmpLimit) {
                    skipQuiets = true;
                    continue;
                }
                // Futility pruning
                if (!inCheck && lmrDepth <= 8 && ss->staticEval + 100 + 110 * lmrDepth <= alpha) continue;
                // History pruning
                if (lmrDepth <= 4 && hist < -3500 * depth) continue;
                // SEE pruning for quiets that hang the piece
                if (!pos.see_ge(m, -55 * lmrDepth * lmrDepth)) continue;
            } else if (capture && depth <= 8) {
                if (!pos.see_ge(m, -95 * depth)) continue;
            }
        }

        // --- Extensions ----------------------------------------------------
        int extension = 0;
        if (!rootNode && ss->ply < 2 * rootDepth) {
            // Singular extension: if the TT move is much better than all alternatives, extend it.
            if (depth >= 7 && m == ttMove && !excluded && (ttBound & BOUND_LOWER) && ttDepth >= depth - 3 &&
                ttScore != VALUE_NONE && std::abs(ttScore) < VALUE_MATE_IN_MAX_PLY) {
                int sBeta  = ttScore - 2 * depth;
                int sDepth = (depth - 1) / 2;
                ss->excluded = m;
                int v = search(sBeta - 1, sBeta, sDepth, ss, cutNode);
                ss->excluded = MOVE_NONE;
                if (stopped()) return 0;
                if (v < sBeta) {
                    extension = 1;
                    if (!pvNode && v < sBeta - 25) extension = 2;
                } else if (sBeta >= beta) {
                    return sBeta;  // multi-cut: several moves beat beta
                } else if (ttScore >= beta) {
                    extension = -1;
                }
            } else if (givesCheck) {
                extension = 1;
            }
        }
        // Avoid runaway double extensions
        if (extension == 2 && ss->ply > rootDepth) extension = 1;

        int newDepth = depth - 1 + extension;

        ss->currentMove = m;
        ss->movedPiece  = pc;
        ss->contHist    = cont_block(pc, to_sq(m));
        pos.do_move(m);
        TT.prefetch(pos.key());

        int v = 0;
        // --- Late move reductions -------------------------------------
        if (depth >= 2 && moveCount > 1 + int(rootNode) && (quiet || list.moves[i].score < 0)) {
            int r = Reductions[std::min(depth, MAX_PLY)][std::min(moveCount, MAX_MOVES - 1)];
            r += !pvNode;
            r += !improving;
            r += cutNode;
            r += ttCapture;
            if (givesCheck) --r;
            if (inCheck) --r;
            if (m == ss->killers[0] || m == ss->killers[1]) --r;
            if (quiet) r -= hist / 8000;
            else r -= 1;
            int d = std::clamp(newDepth - r, 1, newDepth + 1);
            v = -search(-alpha - 1, -alpha, d, ss + 1, true);
            if (v > alpha && d < newDepth) v = -search(-alpha - 1, -alpha, newDepth, ss + 1, !cutNode);
        } else if (!pvNode || moveCount > 1) {
            v = -search(-alpha - 1, -alpha, newDepth, ss + 1, !cutNode);
        }

        if (pvNode && (moveCount == 1 || (v > alpha && (rootNode || v < beta)))) {
            v = -search(-beta, -alpha, newDepth, ss + 1, false);
        }
        pos.undo_move(m);

        if (stopped()) return 0;

        if (v > bestScore) {
            bestScore = v;
            if (v > alpha) {
                bestMove = m;
                if (pvNode) {
                    pvTable[ss->ply][0] = m;
                    std::memcpy(&pvTable[ss->ply][1], pvTable[ss->ply + 1], sizeof(Move) * pvLength[ss->ply + 1]);
                    pvLength[ss->ply] = pvLength[ss->ply + 1] + 1;
                }
                if (rootNode) {
                    rootIterBest  = m;
                    rootIterScore = v;
                    rootIterPV.assign(pvTable[0], pvTable[0] + pvLength[0]);
                }
                if (v >= beta) break;
                alpha = v;
            }
        }

        if (m != bestMove) {
            if (quiet && nQuiets < 64) quietsTried[nQuiets++] = m;
            else if (capture && nCaptures < 32) capturesTried[nCaptures++] = m;
        }
    }

    if (moveCount == 0) return excluded ? alpha : inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // --- History updates on a fail-high ------------------------------------
    if (bestMove != MOVE_NONE && bestScore >= beta) {
        if (!pos.is_capture_or_promotion(bestMove)) {
            update_quiet_stats(ss, bestMove, quietsTried, nQuiets, depth);
            update_capture_stats(MOVE_NONE, capturesTried, nCaptures, depth);
        } else if (pos.is_capture(bestMove)) {
            update_capture_stats(bestMove, capturesTried, nCaptures, depth);
        }
    }

    if (!excluded) {
        Bound b = bestScore >= beta ? BOUND_LOWER : (pvNode && bestMove != MOVE_NONE) ? BOUND_EXACT : BOUND_UPPER;
        TT.store(tte, pos.key(), score_to_tt(bestScore, ss->ply), ss->staticEval, b, depth, bestMove);
    }
    return bestScore;
}

// ---------------------------------------------------------------------------
// Iterative deepening with aspiration windows
// ---------------------------------------------------------------------------
void Worker::iterative_deepening(const std::function<void(const SearchInfo&)>* onInfo) {
    Stack stackBuf[MAX_PLY + 10];
    std::memset(stackBuf, 0, sizeof(stackBuf));
    Stack* ss = stackBuf + 4;  // allow (ss-4) at the root
    for (int i = -4; i < MAX_PLY + 6; ++i) {
        (ss + i)->ply        = i;
        (ss + i)->contHist   = dummyCont;
        (ss + i)->staticEval = VALUE_NONE;
        (ss + i)->movedPiece = NO_PIECE;
    }

    const int maxDepth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    int  prevScore = 0;
    Move prevBest = MOVE_NONE;
    int  stability = 0;
    checkCounter = 1024;

    for (rootDepth = 1; rootDepth <= maxDepth; ++rootDepth) {
        // Helper diversity: odd helpers occasionally skip a depth.
        if (id > 0 && rootDepth > 2 && rootDepth < maxDepth && ((rootDepth + id) % 4 == 0)) continue;

        seldepth = 0;
        rootIterBest = MOVE_NONE;
        rootIterScore = -VALUE_INFINITE;

        int delta = 15;
        int alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (rootDepth >= 5) {
            alpha = std::max(prevScore - delta, -VALUE_INFINITE);
            beta  = std::min(prevScore + delta, VALUE_INFINITE);
        }

        int score = 0;
        while (true) {
            score = search(alpha, beta, rootDepth, ss, false);
            if (stopped()) break;
            if (score <= alpha) {
                beta  = (alpha + beta) / 2;
                alpha = std::max(score - delta, -VALUE_INFINITE);
            } else if (score >= beta) {
                beta = std::min(score + delta, VALUE_INFINITE);
            } else
                break;
            delta += delta / 2 + 5;
            if (delta > 1000) { alpha = -VALUE_INFINITE; beta = VALUE_INFINITE; }
        }

        if (stopped()) {
            // Use a partially searched iteration only if it produced a new best root move.
            if (rootIterBest != MOVE_NONE && (bestMove == MOVE_NONE || rootIterScore > bestScore - 30)) {
                bestMove  = rootIterBest;
                bestScore = rootIterScore;
                bestPV    = rootIterPV;
            }
            break;
        }

        completedDepth = rootDepth;
        if (rootIterBest != MOVE_NONE) {
            bestMove = rootIterBest;
            bestPV   = rootIterPV;
        }
        bestScore    = score;
        bestSeldepth = seldepth;

        if (id != 0) continue;

        // Main thread: report and decide whether to continue.
        if (onInfo && *onInfo) {
            SearchInfo info;
            info.depth    = rootDepth;
            info.seldepth = seldepth;
            info.score    = score;
            info.nodes    = searcher.total_nodes();
            info.timeMs   = elapsed_ms(startTime);
            info.nps      = info.timeMs > 0 ? info.nodes * 1000 / uint64_t(info.timeMs) : info.nodes;
            info.hashfull = TT.hashfull();
            info.pv       = bestPV;
            (*onInfo)(info);
        }

        if (limits.infinite) continue;

        // Found a forced mate and searched deep enough to trust it.
        if (std::abs(score) >= VALUE_MATE_IN_MAX_PLY && rootDepth >= 2 * (VALUE_MATE - std::abs(score)) + 6) break;

        if (softLimit > 0) {
            stability = (bestMove == prevBest) ? std::min(stability + 1, 6) : 0;
            static const double stabFactor[7] = {1.6, 1.3, 1.1, 0.95, 0.85, 0.75, 0.65};
            double scoreFactor = std::clamp(1.0 + (prevScore - score) / 120.0, 0.85, 1.6);
            double budget = double(softLimit) * stabFactor[stability] * scoreFactor;
            if (double(elapsed_ms(startTime)) >= budget * 0.6) break;
        }
        prevBest  = bestMove;
        prevScore = score;
    }
}

// ---------------------------------------------------------------------------
// Searcher: thread pool driving the workers
// ---------------------------------------------------------------------------
Searcher::Searcher() = default;  // workers are created by set_threads() after engine init
Searcher::~Searcher() = default;

void Searcher::set_threads(int n) {
    n = std::clamp(n, 1, 256);
    workers.clear();
    for (int i = 0; i < n; ++i) workers.emplace_back(std::make_unique<Worker>(*this, i));
}

void Searcher::new_game() {
    TT.clear();
    for (auto& w : workers) w->clear();
}

uint64_t Searcher::total_nodes() const {
    uint64_t n = 0;
    for (auto& w : workers) n += w->nodes.load(std::memory_order_relaxed);
    return n;
}

SearchResult Searcher::search(const Position& pos, const SearchLimits& limits,
                              const std::function<void(const SearchInfo&)>& onInfo) {
    auto t0 = Clock::now();
    if (workers.empty()) set_threads(1);
    stopFlag.store(false);
    TT.new_search();

    SearchResult result;
    MoveList legal;
    generate_legal(pos, legal);
    if (legal.size == 0) {
        result.score = pos.in_check() ? -VALUE_MATE : VALUE_DRAW;
        return result;
    }

    // Time allocation
    int64_t soft = 0, hard = 0;
    Color us = pos.side_to_move();
    if (limits.movetime > 0) {
        soft = hard = std::max<int64_t>(1, limits.movetime - moveOverhead);
    } else if (limits.time[us] > 0) {
        int64_t t     = limits.time[us];
        int64_t inc   = limits.inc[us];
        int     mtg   = limits.movestogo > 0 ? std::min(limits.movestogo, 50) : 30;
        int64_t avail = std::max<int64_t>(1, t - moveOverhead);
        soft = avail / mtg + inc * 3 / 4;
        hard = std::min<int64_t>(int64_t(avail * (mtg == 1 ? 0.85 : 0.45)), soft * 5);
        hard = std::max<int64_t>(1, hard);
        soft = std::min(soft, hard);
    }
    if (legal.size == 1 && (limits.time[us] > 0 || limits.movetime > 0) && !limits.infinite) {
        soft = std::min<int64_t>(soft, 1);
        hard = std::min<int64_t>(hard, std::max<int64_t>(1, std::min<int64_t>(hard, 50)));
    }
    // movetime: soft is effectively disabled, the hard limit stops the search
    if (limits.movetime > 0) soft = 0;

    for (auto& w : workers) w->start(pos, limits, t0, soft, hard);

    std::vector<std::thread> helpers;
    for (size_t i = 1; i < workers.size(); ++i)
        helpers.emplace_back([this, i] { workers[i]->iterative_deepening(nullptr); });

    workers[0]->iterative_deepening(&onInfo);

    // For "go infinite" we must not return until told to stop.
    while (limits.infinite && !stopFlag.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    stop();
    for (auto& t : helpers) t.join();

    // Pick the best thread: the deepest completed iteration wins (ties keep the main thread).
    Worker* best = workers[0].get();
    for (auto& w : workers)
        if (w->bestMove != MOVE_NONE && w->completedDepth > best->completedDepth) best = w.get();
    if (best->bestMove == MOVE_NONE) best = workers[0].get();

    result.bestMove = best->bestMove != MOVE_NONE ? best->bestMove : legal.moves[0].move;
    result.score    = best->bestScore;
    result.depth    = best->completedDepth;
    result.seldepth = best->bestSeldepth;
    result.pv       = best->bestPV;
    if (result.pv.empty() || result.pv[0] != result.bestMove) result.pv = {result.bestMove};
    result.ponder   = result.pv.size() > 1 ? result.pv[1] : MOVE_NONE;
    result.nodes    = total_nodes();
    result.timeMs   = elapsed_ms(t0);
    return result;
}
