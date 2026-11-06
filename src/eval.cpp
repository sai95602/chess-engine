#include "eval.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "bitboard.h"
#include "position.h"

Score PSQT[PIECE_NB][64];

namespace {

// ---------------------------------------------------------------------------
// Material and piece-square tables (PeSTO values, tuned by Ronald Friederich).
// Tables are laid out visually: index 0 = a8 ... index 63 = h1.
// ---------------------------------------------------------------------------
const int MgValue[6] = {82, 337, 365, 477, 1025, 0};
const int EgValue[6] = {94, 281, 297, 512, 936, 0};

const int MgPawn[64] = {
      0,   0,   0,   0,   0,   0,  0,   0,
     98, 134,  61,  95,  68, 126, 34, -11,
     -6,   7,  26,  31,  65,  56, 25, -20,
    -14,  13,   6,  21,  23,  12, 17, -23,
    -27,  -2,  -5,  12,  17,   6, 10, -25,
    -26,  -4,  -4, -10,   3,   3, 33, -12,
    -35,  -1, -20, -23, -15,  24, 38, -22,
      0,   0,   0,   0,   0,   0,  0,   0};
const int EgPawn[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0};
const int MgKnight[64] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  25,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23};
const int EgKnight[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64};
const int MgBishop[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21};
const int EgBishop[64] = {
    -14, -21, -11,  -8, -7,  -9, -17, -24,
     -8,  -4,   7, -12, -3, -13,  -4, -14,
      2,  -8,   0,  -1, -2,   6,   0,   4,
     -3,   9,  12,   9, 14,  10,   3,   2,
     -6,   3,  13,  19,  7,  10,  -3,  -9,
    -12,  -3,   8,  10, 13,   3,  -7, -15,
    -14, -18,  -7,  -1,  4,  -9, -15, -27,
    -23,  -9, -23,  -5, -9, -16,  -5, -17};
const int MgRook[64] = {
     32,  42,  32,  51, 63,  9,  31,  43,
     27,  32,  58,  62, 80, 67,  26,  44,
     -5,  19,  26,  36, 17, 45,  61,  16,
    -24, -11,   7,  26, 24, 35,  -8, -20,
    -36, -26, -12,  -1,  9, -7,   6, -23,
    -45, -25, -16, -17,  3,  0,  -5, -33,
    -44, -16, -20,  -9, -1, 11,  -6, -71,
    -19, -13,   1,  17, 16,  7, -37, -26};
const int EgRook[64] = {
    13, 10, 18, 15, 12,  12,   8,   5,
    11, 13, 13, 11, -3,   3,   8,   3,
     7,  7,  7,  5,  4,  -3,  -5,  -3,
     4,  3, 13,  1,  2,   1,  -1,   2,
     3,  5,  8,  4, -5,  -6,  -8, -11,
    -4,  0, -5, -1, -7, -12,  -8, -16,
    -6, -6,  0,  2, -9,  -9, -11,  -3,
    -9,  2,  3, -1, -5, -13,   4, -20};
const int MgQueen[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50};
const int EgQueen[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41};
const int MgKing[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14};
const int EgKing[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43};

const int* MgTables[6] = {MgPawn, MgKnight, MgBishop, MgRook, MgQueen, MgKing};
const int* EgTables[6] = {EgPawn, EgKnight, EgBishop, EgRook, EgQueen, EgKing};

const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};

// ---------------------------------------------------------------------------
// Positional terms
// ---------------------------------------------------------------------------
constexpr Score BishopPair      = S(30, 55);
constexpr Score DoubledPawn     = S(-8, -20);
constexpr Score IsolatedPawn    = S(-10, -12);
constexpr Score BackwardPawn    = S(-6, -8);
constexpr Score PassedRank[8]   = {S(0, 0), S(0, 5), S(2, 10), S(8, 20), S(20, 38), S(38, 65), S(55, 100), S(0, 0)};
constexpr Score PassedFreePath  = S(0, 12);  // per rank weight, if the stop square is free
constexpr Score UnstoppablePawn = S(0, 500);
constexpr Score RookOpenFile    = S(25, 8);
constexpr Score RookSemiOpen    = S(12, 6);
constexpr Score RookOn7th       = S(5, 20);
constexpr Score KnightOutpost   = S(22, 12);
constexpr Score BishopOutpost   = S(12, 6);
constexpr Score ThreatByPawn    = S(45, 35);
constexpr Score MinorOnMajor    = S(30, 22);
constexpr Score RookOnQueen     = S(30, 18);
constexpr Score HangingPiece    = S(18, 10);
constexpr Score ShieldPawn1     = S(14, 0);
constexpr Score ShieldPawn2     = S(7, 0);
constexpr Score ShieldMissing   = S(-14, 0);
constexpr Score KingOpenFile    = S(-12, 0);
constexpr int   Tempo           = 14;

// Mobility: bonus per reachable square relative to a typical count.
constexpr Score MobWeight[6] = {S(0, 0), S(4, 4), S(5, 5), S(2, 4), S(1, 2), S(0, 0)};
constexpr int   MobBase[6]   = {0, 4, 6, 7, 13, 0};

// King attack weights (per attacked king-zone square)
constexpr int KingAttackWeight[6] = {0, 2, 2, 3, 5, 0};

inline int center_distance(int s) {
    int f = file_of(s), r = rank_of(s);
    return std::max(3 - f, f - 4) + std::max(3 - r, r - 4);
}

struct EvalInfo {
    Bitboard pawnAttacks[2];
    Bitboard attacked[2];       // all squares attacked by each side
    Bitboard attackedBy[2][6];  // squares attacked by each piece type
    Bitboard kingZone[2];
    int      kingAttackers[2];  // number of pieces attacking the enemy king zone (index = attacker)
    int      kingAttackUnits[2];
};

Score eval_pawns(const Position& pos, Color us, EvalInfo& ei, Bitboard& passed) {
    Color    them     = ~us;
    Bitboard ourPawns = pos.pieces(us, PAWN), theirPawns = pos.pieces(them, PAWN);
    Score    s;
    Bitboard b = ourPawns;
    while (b) {
        int sq = pop_lsb(b);
        int f  = file_of(sq);
        bool isolated = !(ourPawns & AdjacentFiles[f]);
        bool doubled  = ForwardFile[us][sq] & ourPawns;
        if (isolated) s += IsolatedPawn;
        if (doubled) s += DoubledPawn;

        // Backward: no friendly pawn on adjacent files behind or level, and the stop square is attacked by an enemy pawn.
        if (!isolated) {
            Bitboard support = PassedMask[them][sq + pawn_push(us)] & AdjacentFiles[f] & ourPawns;
            int stop = sq + pawn_push(us);
            if (!support && (ei.pawnAttacks[them] & sq_bb(stop))) s += BackwardPawn;
        }

        if (!(PassedMask[us][sq] & theirPawns) && !doubled) passed |= sq_bb(sq);
    }
    return s;
}

Score eval_passed(const Position& pos, Color us, Bitboard passed, const EvalInfo& ei) {
    Color them = ~us;
    Score s;
    int ourK = pos.king_sq(us), theirK = pos.king_sq(them);
    bool theyHavePieces = pos.non_pawn_material(them) > 0;
    while (passed) {
        int sq = pop_lsb(passed);
        int r  = relative_rank(us, sq);
        s += PassedRank[r];
        if (r >= 3) {
            int w = r - 2;
            int stop = sq + pawn_push(us);
            s += S(0, (std::min(SquareDistance[theirK][stop], 5) * 5 - std::min(SquareDistance[ourK][stop], 5) * 2) * w);
            if (!(pos.pieces() & sq_bb(stop)) && !(ei.attacked[them] & sq_bb(stop))) s += PassedFreePath * w;
        }
        // Rule of the square vs. a bare king (pawn path must be clear).
        if (!theyHavePieces && !(ForwardFile[us][sq] & pos.pieces())) {
            int promo = make_sq(file_of(sq), us == WHITE ? 7 : 0);
            int pawnDist = std::min(5, 7 - r);
            int kingDist = SquareDistance[theirK][promo] - (pos.side_to_move() == them ? 1 : 0);
            if (pawnDist < kingDist) s += UnstoppablePawn;
        }
    }
    return s;
}

Score eval_pieces(const Position& pos, Color us, EvalInfo& ei) {
    Color    them = ~us;
    Score    s;
    Bitboard occ = pos.pieces();
    Bitboard mobilityArea = ~(pos.pieces(us, PAWN) | pos.pieces(us, KING) | ei.pawnAttacks[them]);
    Bitboard theirPawns = pos.pieces(them, PAWN), ourPawns = pos.pieces(us, PAWN);
    Bitboard outpostRanks = us == WHITE ? (Rank4BB | Rank5BB | Rank6BB) : (Rank5BB | Rank4BB | Rank3BB);

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
        Bitboard b = pos.pieces(us, pt);
        while (b) {
            int sq = pop_lsb(b);
            Bitboard att;
            // X-ray through our own queen/rooks for sliders gives a better mobility estimate.
            if (pt == BISHOP) att = bishop_attacks(sq, occ ^ pos.pieces(us, QUEEN));
            else if (pt == ROOK) att = rook_attacks(sq, occ ^ pos.pieces(us, QUEEN, ROOK));
            else att = attacks_bb(pt, sq, occ);

            ei.attackedBy[us][pt] |= att;
            ei.attacked[us] |= att;

            int mob = popcount(att & mobilityArea);
            s += MobWeight[pt] * (mob - MobBase[pt]);

            Bitboard kz = att & ei.kingZone[them];
            if (kz) {
                ei.kingAttackers[us]++;
                ei.kingAttackUnits[us] += KingAttackWeight[pt] * popcount(kz);
            }

            if (pt == KNIGHT || pt == BISHOP) {
                // Outpost: supported by a pawn and cannot be chased away by enemy pawns.
                if ((outpostRanks & sq_bb(sq)) && (ei.pawnAttacks[us] & sq_bb(sq)) &&
                    !(PassedMask[us][sq] & AdjacentFiles[file_of(sq)] & theirPawns))
                    s += pt == KNIGHT ? KnightOutpost : BishopOutpost;
            } else if (pt == ROOK) {
                if (!(file_bb(sq) & ourPawns)) s += (file_bb(sq) & theirPawns) ? RookSemiOpen : RookOpenFile;
                if (relative_rank(us, sq) == 6 && relative_rank(us, pos.king_sq(them)) == 7) s += RookOn7th;
            }
        }
    }
    return s;
}

Score eval_king(const Position& pos, Color us, const EvalInfo& ei) {
    Color them = ~us;
    Score s;
    int ksq = pos.king_sq(us);
    Bitboard ourPawns = pos.pieces(us, PAWN), theirPawns = pos.pieces(them, PAWN);

    // Pawn shield (only meaningful while the king sits near its back rank)
    if (relative_rank(us, ksq) <= 1) {
        int kf = std::clamp(file_of(ksq), 1, 6);
        for (int f = kf - 1; f <= kf + 1; ++f) {
            Bitboard fileB = FileABB << f;
            int s1 = make_sq(f, rank_of(ksq)) + pawn_push(us);
            int s2 = s1 + pawn_push(us);
            if (ourPawns & sq_bb(s1)) s += ShieldPawn1;
            else if (s2 >= 0 && s2 < 64 && (ourPawns & sq_bb(s2))) s += ShieldPawn2;
            else s += ShieldMissing;
            if (!(fileB & ourPawns)) s += KingOpenFile;
            if (!(fileB & (ourPawns | theirPawns))) s += KingOpenFile;
        }
    }

    // Attack units accumulated by the enemy
    int attackers = ei.kingAttackers[them];
    if (attackers >= 2 || (attackers >= 1 && pos.pieces(them, QUEEN))) {
        int units = ei.kingAttackUnits[them];
        // Weak squares: king-zone squares attacked by the enemy and not defended by our pawns
        units += popcount(ei.kingZone[us] & ei.attacked[them] & ~ei.pawnAttacks[us]) / 2;
        // Safe checks increase danger
        Bitboard safe = ~ei.attacked[us] & ~pos.pieces(them);
        Bitboard occ  = pos.pieces();
        if (KnightAttacks[ksq] & ei.attackedBy[them][KNIGHT] & safe) units += 3;
        if (bishop_attacks(ksq, occ) & (ei.attackedBy[them][BISHOP] | ei.attackedBy[them][QUEEN]) & safe) units += 2;
        if (rook_attacks(ksq, occ) & (ei.attackedBy[them][ROOK] | ei.attackedBy[them][QUEEN]) & safe) units += 3;
        int danger = units * units / 4;
        if (!pos.pieces(them, QUEEN)) danger /= 2;
        danger = std::min(danger, 700);
        s -= S(danger, danger / 8);
    }
    return s;
}

Score eval_threats(const Position& pos, Color us, const EvalInfo& ei) {
    Color them = ~us;
    Score s;
    // Our pieces attacked by enemy pawns
    Bitboard nonPawn = pos.pieces(us) & ~pos.pieces(us, PAWN) & ~pos.pieces(us, KING);
    s -= ThreatByPawn * popcount(nonPawn & ei.pawnAttacks[them]);
    // Our majors attacked by enemy minors
    Bitboard minorAtt = ei.attackedBy[them][KNIGHT] | ei.attackedBy[them][BISHOP];
    s -= MinorOnMajor * popcount(pos.pieces(us, ROOK, QUEEN) & minorAtt);
    s -= RookOnQueen * popcount(pos.pieces(us, QUEEN) & ei.attackedBy[them][ROOK]);
    // Hanging: attacked and undefended
    Bitboard defended = ei.attacked[us] | ei.pawnAttacks[us] | KingAttacks[pos.king_sq(us)];
    s -= HangingPiece * popcount(nonPawn & ei.attacked[them] & ~defended);
    return s;
}

// Scale factor (out of 64) for drawish endgames.
int scale_factor(const Position& pos, int eg) {
    Color strong = eg > 0 ? WHITE : BLACK, weak = ~strong;
    int npmS = pos.non_pawn_material(strong), npmW = pos.non_pawn_material(weak);

    if (!pos.pieces(strong, PAWN)) {
        if (npmS - npmW <= 330) return npmS < 500 ? 0 : 16;
        if (npmS == 640 && popcount(pos.pieces(strong, KNIGHT)) == 2 && npmW == 0) return 2;
    }
    // Opposite-coloured bishops
    if (npmS == 330 && npmW == 330 && popcount(pos.pieces(BISHOP)) == 2) {
        Bitboard wb = pos.pieces(WHITE, BISHOP), bb = pos.pieces(BLACK, BISHOP);
        if (bool(wb & DarkSquares) != bool(bb & DarkSquares)) return 24;
    }
    return 64;
}

struct Terms {
    Score psq, pawns[2], passed[2], pieces[2], king[2], threats[2], extra;
    int   phase, sf, mopup;
};

int evaluate_impl(const Position& pos, Terms* t) {
    EvalInfo ei{};
    for (Color c : {WHITE, BLACK}) {
        ei.pawnAttacks[c] = pawn_attacks_bb(c, pos.pieces(c, PAWN));
        ei.attackedBy[c][PAWN] = ei.pawnAttacks[c];
        ei.attackedBy[c][KING] = KingAttacks[pos.king_sq(c)];
        ei.attacked[c] = ei.pawnAttacks[c] | ei.attackedBy[c][KING];
        Bitboard kz = KingAttacks[pos.king_sq(c)] | sq_bb(pos.king_sq(c));
        ei.kingZone[c] = kz | shift_up(c, kz);
    }

    Score score = pos.psq_score();
    Score extra;

    // Bishop pair
    if (popcount(pos.pieces(WHITE, BISHOP)) >= 2) extra += BishopPair;
    if (popcount(pos.pieces(BLACK, BISHOP)) >= 2) extra -= BishopPair;

    Bitboard passed[2] = {0, 0};
    Score pw = eval_pawns(pos, WHITE, ei, passed[WHITE]);
    Score pb = eval_pawns(pos, BLACK, ei, passed[BLACK]);
    Score piw = eval_pieces(pos, WHITE, ei);
    Score pib = eval_pieces(pos, BLACK, ei);
    Score paw = eval_passed(pos, WHITE, passed[WHITE], ei);
    Score pab = eval_passed(pos, BLACK, passed[BLACK], ei);
    Score kw = eval_king(pos, WHITE, ei);
    Score kb = eval_king(pos, BLACK, ei);
    Score tw = eval_threats(pos, WHITE, ei);
    Score tb = eval_threats(pos, BLACK, ei);

    score += extra + pw - pb + piw - pib + paw - pab + kw - kb + tw - tb;

    int phase = 0;
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) phase += PhaseInc[pt] * popcount(pos.pieces(pt));
    phase = std::min(phase, 24);

    // Mop-up: drive a bare enemy king to the edge and bring our king closer.
    int mopup = 0;
    for (Color c : {WHITE, BLACK}) {
        Color w = ~c;
        if (!(pos.pieces(w) & ~pos.pieces(w, KING)) && pos.non_pawn_material(c) >= 500) {
            int wk = pos.king_sq(w), sk = pos.king_sq(c);
            int v = 10 * center_distance(wk) + 4 * (14 - (std::abs(file_of(wk) - file_of(sk)) + std::abs(rank_of(wk) - rank_of(sk))));
            mopup += c == WHITE ? v : -v;
        }
    }

    int sf = scale_factor(pos, score.eg);
    int eg = score.eg * sf / 64 + mopup;
    int v  = (score.mg * phase + eg * (24 - phase)) / 24;

    if (t) {
        t->psq = pos.psq_score(); t->extra = extra;
        t->pawns[0] = pw; t->pawns[1] = pb; t->passed[0] = paw; t->passed[1] = pab;
        t->pieces[0] = piw; t->pieces[1] = pib; t->king[0] = kw; t->king[1] = kb;
        t->threats[0] = tw; t->threats[1] = tb; t->phase = phase; t->sf = sf; t->mopup = mopup;
    }

    v = pos.side_to_move() == WHITE ? v : -v;
    v += Tempo;
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

}  // namespace

void init_eval() {
    for (int pt = PAWN; pt <= KING; ++pt) {
        for (int sq = 0; sq < 64; ++sq) {
            // Tables are visual (a8 first): white uses sq ^ 56, black uses sq directly.
            Score w(MgValue[pt] + MgTables[pt][sq ^ 56], EgValue[pt] + EgTables[pt][sq ^ 56]);
            Score b(MgValue[pt] + MgTables[pt][sq], EgValue[pt] + EgTables[pt][sq]);
            PSQT[make_piece(WHITE, PieceType(pt))][sq] = w;
            PSQT[make_piece(BLACK, PieceType(pt))][sq] = -b;
        }
    }
}

int evaluate(const Position& pos) { return evaluate_impl(pos, nullptr); }

std::string eval_trace(const Position& pos) {
    Terms t{};
    int v = evaluate_impl(pos, &t);
    std::ostringstream os;
    auto row = [&](const char* name, Score w, Score b) {
        os << "  " << name;
        for (size_t i = std::string(name).size(); i < 14; ++i) os << ' ';
        os << "| " << w.mg << " " << w.eg << "  | " << b.mg << " " << b.eg << "  | " << (w - b).mg << " " << (w - b).eg << "\n";
    };
    os << "  Term          | White mg eg | Black mg eg | Total mg eg\n";
    os << "  Material+PSQT | " << t.psq.mg << " " << t.psq.eg << "\n";
    os << "  Bishop pair   | " << t.extra.mg << " " << t.extra.eg << "\n";
    row("Pawns", t.pawns[0], t.pawns[1]);
    row("Passed", t.passed[0], t.passed[1]);
    row("Pieces", t.pieces[0], t.pieces[1]);
    row("King safety", t.king[0], t.king[1]);
    row("Threats", t.threats[0], t.threats[1]);
    os << "  Phase " << t.phase << "/24, scale " << t.sf << "/64, mop-up " << t.mopup << "\n";
    os << "  Final (side to move): " << v << " cp\n";
    return os.str();
}
