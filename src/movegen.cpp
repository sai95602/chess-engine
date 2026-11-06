#include "movegen.h"

#include "bitboard.h"
#include "position.h"

namespace {

inline void add_promotions(MoveList& list, int from, int to, bool queens, bool unders) {
    if (queens) list.add(make_special(PROMOTION, from, to, QUEEN));
    if (unders) {
        list.add(make_special(PROMOTION, from, to, KNIGHT));
        list.add(make_special(PROMOTION, from, to, ROOK));
        list.add(make_special(PROMOTION, from, to, BISHOP));
    }
}

// target: allowed destination squares for non-king pieces
void generate_pawn_moves(const Position& pos, MoveList& list, Color us, bool caps, bool quiets, bool evasion,
                         Bitboard target) {
    Color    them    = ~us;
    Bitboard pawns   = pos.pieces(us, PAWN);
    Bitboard rank7   = us == WHITE ? Rank7BB : Rank2BB;
    Bitboard rank3   = us == WHITE ? Rank3BB : Rank6BB;
    Bitboard empty   = ~pos.pieces();
    Bitboard enemies = pos.pieces(them);
    if (evasion) enemies &= target;
    int up = pawn_push(us);

    Bitboard on7  = pawns & rank7;
    Bitboard not7 = pawns & ~rank7;

    // Captures toward the "west" (file-1) and "east" (file+1) from the mover's point of view.
    auto capL = [&](Bitboard b) { return us == WHITE ? shift_nw(b) : shift_sw(b); };
    auto capR = [&](Bitboard b) { return us == WHITE ? shift_ne(b) : shift_se(b); };
    const int dL = us == WHITE ? 7 : -9;
    const int dR = us == WHITE ? 9 : -7;

    if (quiets) {
        Bitboard b1 = shift_up(us, not7) & empty;
        Bitboard b2 = shift_up(us, b1 & rank3) & empty;
        if (evasion) {
            b1 &= target;
            b2 &= target;
        }
        while (b1) { int to = pop_lsb(b1); list.add(make_move(to - up, to)); }
        while (b2) { int to = pop_lsb(b2); list.add(make_move(to - 2 * up, to)); }
    }

    if (on7) {
        // In evasion mode we generate every promotion; otherwise queen promos go with
        // captures and under-promotions with quiets.
        bool queens = caps || evasion;
        bool unders = quiets || evasion;
        Bitboard push = shift_up(us, on7) & empty;
        if (evasion) push &= target;
        Bitboard l = capL(on7) & enemies;
        Bitboard r = capR(on7) & enemies;
        while (push) { int to = pop_lsb(push); add_promotions(list, to - up, to, queens, unders); }
        while (l) { int to = pop_lsb(l); add_promotions(list, to - dL, to, queens, unders); }
        while (r) { int to = pop_lsb(r); add_promotions(list, to - dR, to, queens, unders); }
    }

    if (caps) {
        Bitboard l = capL(not7) & enemies;
        Bitboard r = capR(not7) & enemies;
        while (l) { int to = pop_lsb(l); list.add(make_move(to - dL, to)); }
        while (r) { int to = pop_lsb(r); list.add(make_move(to - dR, to)); }

        int ep = pos.ep_square();
        if (ep != NO_SQ) {
            int capsq = ep - up;
            // When in check, en passant only helps if it captures the checker or blocks.
            if (!evasion || (target & (sq_bb(ep) | sq_bb(capsq)))) {
                Bitboard b = not7 & PawnAttacks[them][ep];
                while (b) list.add(make_special(EN_PASSANT, pop_lsb(b), ep));
            }
        }
    }
}

void generate_piece_moves(const Position& pos, MoveList& list, Color us, PieceType pt, Bitboard target) {
    Bitboard bb  = pos.pieces(us, pt);
    Bitboard occ = pos.pieces();
    while (bb) {
        int from = pop_lsb(bb);
        Bitboard att = attacks_bb(pt, from, occ) & target;
        while (att) list.add(make_move(from, pop_lsb(att)));
    }
}

}  // namespace

void generate(const Position& pos, MoveList& list, GenType type) {
    Color    us   = pos.side_to_move();
    Color    them = ~us;
    int      ksq  = pos.king_sq(us);
    Bitboard own  = pos.pieces(us);
    Bitboard enemy = pos.pieces(them);
    Bitboard empty = ~pos.pieces();

    if (pos.in_check()) {
        Bitboard checkers = pos.checkers();
        // King moves (legality filters attacked squares)
        Bitboard k = KingAttacks[ksq] & ~own;
        while (k) list.add(make_move(ksq, pop_lsb(k)));
        if (more_than_one(checkers)) return;  // double check: only king moves

        int checker = lsb(checkers);
        Bitboard target = BetweenBB[ksq][checker] | sq_bb(checker);
        generate_pawn_moves(pos, list, us, true, true, true, target);
        for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) generate_piece_moves(pos, list, us, pt, target);
        return;
    }

    bool caps   = type != GEN_QUIETS;
    bool quiets = type != GEN_CAPTURES;
    Bitboard target = type == GEN_ALL ? ~own : type == GEN_CAPTURES ? enemy : empty;

    generate_pawn_moves(pos, list, us, caps, quiets, false, 0);
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) generate_piece_moves(pos, list, us, pt, target);

    Bitboard k = KingAttacks[ksq] & target;
    while (k) list.add(make_move(ksq, pop_lsb(k)));

    if (quiets) {
        int cr = pos.castling_rights();
        Bitboard occ = pos.pieces();
        if (us == WHITE) {
            if ((cr & WHITE_OO) && !(occ & (sq_bb(F1) | sq_bb(G1)))) list.add(make_special(CASTLING, E1, G1));
            if ((cr & WHITE_OOO) && !(occ & (sq_bb(B1) | sq_bb(C1) | sq_bb(D1)))) list.add(make_special(CASTLING, E1, C1));
        } else {
            if ((cr & BLACK_OO) && !(occ & (sq_bb(F8) | sq_bb(G8)))) list.add(make_special(CASTLING, E8, G8));
            if ((cr & BLACK_OOO) && !(occ & (sq_bb(B8) | sq_bb(C8) | sq_bb(D8)))) list.add(make_special(CASTLING, E8, C8));
        }
    }
}

void generate_legal(const Position& pos, MoveList& list) {
    MoveList pseudo;
    generate(pos, pseudo, GEN_ALL);
    list.size = 0;
    for (int i = 0; i < pseudo.size; ++i)
        if (pos.legal(pseudo.moves[i].move)) list.add(pseudo.moves[i].move);
}
