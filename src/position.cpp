#include "position.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "movegen.h"

const char* Position::StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace Zobrist {
Key psq[PIECE_NB][64];
Key enpassant[8];
Key castling[16];
Key side;
}  // namespace Zobrist

namespace {

int CastlingMask[64];  // rights removed when a piece moves from/to this square

const std::string PieceChars = "PNBRQKpnbrqk";

}  // namespace

void Position::init() {
    uint64_t s = 1070372ULL;
    auto rnd = [&]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    };
    for (int p = 0; p < PIECE_NB; ++p)
        for (int sq = 0; sq < 64; ++sq) Zobrist::psq[p][sq] = rnd();
    for (int f = 0; f < 8; ++f) Zobrist::enpassant[f] = rnd();
    for (int c = 0; c < 16; ++c) Zobrist::castling[c] = rnd();
    Zobrist::side = rnd();

    for (int sq = 0; sq < 64; ++sq) CastlingMask[sq] = 0;
    CastlingMask[A1] = WHITE_OOO;
    CastlingMask[H1] = WHITE_OO;
    CastlingMask[E1] = WHITE_OO | WHITE_OOO;
    CastlingMask[A8] = BLACK_OOO;
    CastlingMask[H8] = BLACK_OO;
    CastlingMask[E8] = BLACK_OO | BLACK_OOO;
}

void Position::put_piece(Piece p, int s) {
    board[s] = p;
    byType[type_of(p)] |= sq_bb(s);
    byColor[color_of(p)] |= sq_bb(s);
    psq += PSQT[p][s];
}

void Position::remove_piece(int s) {
    Piece p = board[s];
    byType[type_of(p)] ^= sq_bb(s);
    byColor[color_of(p)] ^= sq_bb(s);
    board[s] = NO_PIECE;
    psq -= PSQT[p][s];
}

void Position::move_piece(int from, int to) {
    Piece    p  = board[from];
    Bitboard ft = sq_bb(from) | sq_bb(to);
    byType[type_of(p)] ^= ft;
    byColor[color_of(p)] ^= ft;
    board[from] = NO_PIECE;
    board[to]   = p;
    psq += PSQT[p][to] - PSQT[p][from];
}

int Position::non_pawn_material(Color c) const {
    return popcount(pieces(c, KNIGHT)) * 320 + popcount(pieces(c, BISHOP)) * 330 +
           popcount(pieces(c, ROOK)) * 500 + popcount(pieces(c, QUEEN)) * 950;
}

static bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

bool Position::set(const std::string& fenStr, std::string* err) {
    std::istringstream ss(fenStr);
    std::string placement, side, castle, ep, half, full;
    ss >> placement >> side >> castle >> ep >> half >> full;
    if (placement.empty()) return fail(err, "empty FEN");
    if (side.empty()) side = "w";
    if (castle.empty()) castle = "-";
    if (ep.empty()) ep = "-";

    // Parse into temporaries first so a bad FEN never corrupts the position.
    Piece tmp[64];
    for (auto& p : tmp) p = NO_PIECE;
    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8) return fail(err, "bad rank length in FEN");
            --rank;
            file = 0;
            if (rank < 0) return fail(err, "too many ranks in FEN");
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fail(err, "bad rank length in FEN");
        } else {
            size_t idx = PieceChars.find(c);
            if (idx == std::string::npos) return fail(err, std::string("invalid piece character '") + c + "'");
            if (file > 7) return fail(err, "bad rank length in FEN");
            tmp[make_sq(file, rank)] = Piece(idx);
            ++file;
        }
    }
    if (rank != 0 || file != 8) return fail(err, "FEN must describe exactly 8 ranks");
    if (side != "w" && side != "b") return fail(err, "side to move must be 'w' or 'b'");

    int wk = 0, bk = 0;
    for (int s = 0; s < 64; ++s) {
        if (tmp[s] == W_KING) ++wk;
        if (tmp[s] == B_KING) ++bk;
        if ((tmp[s] == W_PAWN || tmp[s] == B_PAWN) && (rank_of(s) == 0 || rank_of(s) == 7))
            return fail(err, "pawns cannot be on the first or last rank");
    }
    if (wk != 1 || bk != 1) return fail(err, "each side must have exactly one king");

    // Commit
    std::memset(byType, 0, sizeof(byType));
    std::memset(byColor, 0, sizeof(byColor));
    for (auto& p : board) p = NO_PIECE;
    psq = Score();
    for (int s = 0; s < 64; ++s)
        if (tmp[s] != NO_PIECE) put_piece(tmp[s], s);
    stm = side == "w" ? WHITE : BLACK;

    states.clear();
    states.reserve(1024 + MAX_PLY);
    states.emplace_back();
    StateInfo& si = states.back();
    std::memset(&si, 0, sizeof(si));
    si.captured = NO_PIECE;

    si.castling = 0;
    for (char c : castle) {
        if (c == 'K' && board[E1] == W_KING && board[H1] == W_ROOK) si.castling |= WHITE_OO;
        if (c == 'Q' && board[E1] == W_KING && board[A1] == W_ROOK) si.castling |= WHITE_OOO;
        if (c == 'k' && board[E8] == B_KING && board[H8] == B_ROOK) si.castling |= BLACK_OO;
        if (c == 'q' && board[E8] == B_KING && board[A8] == B_ROOK) si.castling |= BLACK_OOO;
    }

    si.ep = NO_SQ;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && (ep[1] == '3' || ep[1] == '6')) {
        int epsq = make_sq(ep[0] - 'a', ep[1] - '1');
        Color them = ~stm;
        // Only keep it if a capture is actually possible (keeps hash keys canonical).
        int pushed = epsq - pawn_push(stm);  // square of the pawn that just double-pushed
        if (relative_rank(stm, epsq) == 5 && board[pushed] == make_piece(them, PAWN) &&
            (PawnAttacks[them][epsq] & pieces(stm, PAWN)) && board[epsq] == NO_PIECE)
            si.ep = epsq;
    }

    si.rule50 = half.empty() ? 0 : std::atoi(half.c_str());
    if (si.rule50 < 0) si.rule50 = 0;
    int fm = full.empty() ? 1 : std::atoi(full.c_str());
    if (fm < 1) fm = 1;
    gamePly = 2 * (fm - 1) + (stm == BLACK);
    si.pliesFromNull = 0;

    compute_keys();
    set_check_info();

    // The side that just moved must not be in check.
    if (attackers_to(king_sq(~stm)) & pieces(stm)) return fail(err, "side not to move is in check");
    return true;
}

void Position::compute_keys() {
    StateInfo& si = st();
    si.key = si.pawnKey = 0;
    for (int s = 0; s < 64; ++s) {
        if (board[s] == NO_PIECE) continue;
        si.key ^= Zobrist::psq[board[s]][s];
        if (type_of(board[s]) == PAWN) si.pawnKey ^= Zobrist::psq[board[s]][s];
    }
    if (stm == BLACK) si.key ^= Zobrist::side;
    si.key ^= Zobrist::castling[si.castling];
    if (si.ep != NO_SQ) si.key ^= Zobrist::enpassant[file_of(si.ep)];
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Piece p = board[make_sq(f, r)];
            if (p == NO_PIECE) { ++empty; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            s += PieceChars[p];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    int cr = st().castling;
    if (!cr) s += '-';
    if (cr & WHITE_OO) s += 'K';
    if (cr & WHITE_OOO) s += 'Q';
    if (cr & BLACK_OO) s += 'k';
    if (cr & BLACK_OOO) s += 'q';
    s += ' ';
    s += st().ep == NO_SQ ? "-" : square_to_string(st().ep);
    s += ' ' + std::to_string(st().rule50) + ' ' + std::to_string(1 + gamePly / 2);
    return s;
}

std::string Position::pretty() const {
    std::ostringstream os;
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            Piece p = board[make_sq(f, r)];
            os << " | " << (p == NO_PIECE ? ' ' : PieceChars[p]);
        }
        os << " | " << (r + 1) << "\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "   a   b   c   d   e   f   g   h\n\nFen: " << fen() << "\nKey: " << std::hex << key() << std::dec << "\n";
    return os.str();
}

Bitboard Position::attackers_to(int s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
           (KnightAttacks[s] & byType[KNIGHT]) | (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN])) |
           (rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN])) | (KingAttacks[s] & byType[KING]);
}

Bitboard Position::slider_blockers(Bitboard sliders, int s, Bitboard& pinners) const {
    Bitboard blk = 0;
    pinners      = 0;
    Bitboard snipers = ((rook_attacks(s, 0) & pieces(QUEEN, ROOK)) | (bishop_attacks(s, 0) & pieces(QUEEN, BISHOP))) & sliders;
    Bitboard occ = pieces() ^ snipers;
    while (snipers) {
        int      sn = pop_lsb(snipers);
        Bitboard b  = BetweenBB[s][sn] & occ;
        if (b && !more_than_one(b)) {
            blk |= b;
            if (b & pieces(color_of(board[s]))) pinners |= sq_bb(sn);
        }
    }
    return blk;
}

void Position::set_check_info() {
    StateInfo& si = st();
    Color us = stm, them = ~stm;
    si.checkers = attackers_to(king_sq(us)) & pieces(them);
    si.blockers[WHITE] = slider_blockers(pieces(BLACK), king_sq(WHITE), si.pinners[BLACK]);
    si.blockers[BLACK] = slider_blockers(pieces(WHITE), king_sq(BLACK), si.pinners[WHITE]);

    int ek = king_sq(them);
    si.checkSquares[PAWN]   = PawnAttacks[them][ek];
    si.checkSquares[KNIGHT] = KnightAttacks[ek];
    si.checkSquares[BISHOP] = bishop_attacks(ek, pieces());
    si.checkSquares[ROOK]   = rook_attacks(ek, pieces());
    si.checkSquares[QUEEN]  = si.checkSquares[BISHOP] | si.checkSquares[ROOK];
    si.checkSquares[KING]   = 0;
}

bool Position::legal(Move m) const {
    Color us = stm, them = ~stm;
    int from = from_sq(m), to = to_sq(m);
    int ksq = king_sq(us);

    if (move_type(m) == EN_PASSANT) {
        int capsq = to - pawn_push(us);
        Bitboard occ = (pieces() ^ sq_bb(from) ^ sq_bb(capsq)) | sq_bb(to);
        return !(rook_attacks(ksq, occ) & pieces(them, QUEEN, ROOK)) &&
               !(bishop_attacks(ksq, occ) & pieces(them, QUEEN, BISHOP));
    }

    if (move_type(m) == CASTLING) {
        // King may not pass through or land on an attacked square (not-in-check is ensured by movegen).
        int step = to > from ? 1 : -1;
        for (int s = from + step; s != to + step; s += step)
            if (attackers_to(s) & pieces(them)) return false;
        return true;
    }

    if (type_of(board[from]) == KING) return !(attackers_to(to, pieces() ^ sq_bb(from)) & pieces(them));

    // A pinned piece may only move along the pin line.
    return !(st().blockers[us] & sq_bb(from)) || aligned(from, to, ksq);
}

bool Position::gives_check(Move m) const {
    int from = from_sq(m), to = to_sq(m);
    Color us = stm;
    int ek = king_sq(~us);

    if (st().checkSquares[type_of(board[from])] & sq_bb(to)) return true;

    // Discovered check
    if (st().blockers[~us] & sq_bb(from)) return !aligned(from, to, ek) || move_type(m) == CASTLING;

    switch (move_type(m)) {
    case NORMAL: return false;
    case PROMOTION: return attacks_bb(promotion_type(m), to, pieces() ^ sq_bb(from)) & sq_bb(ek);
    case EN_PASSANT: {
        int capsq = make_sq(file_of(to), rank_of(from));
        Bitboard b = (pieces() ^ sq_bb(from) ^ sq_bb(capsq)) | sq_bb(to);
        return (rook_attacks(ek, b) & pieces(us, QUEEN, ROOK)) | (bishop_attacks(ek, b) & pieces(us, QUEEN, BISHOP));
    }
    case CASTLING: {
        int rto = to > from ? from + 1 : from - 1;
        Bitboard occ = (pieces() ^ sq_bb(from)) | sq_bb(to);
        return rook_attacks(rto, occ) & sq_bb(ek);
    }
    }
    return false;
}

void Position::do_move(Move m) {
    StateInfo ns = st();  // copy previous state, then modify
    ns.rule50++;
    ns.pliesFromNull++;
    ns.captured = NO_PIECE;

    Key   k = ns.key ^ Zobrist::side;
    Color us = stm, them = ~stm;
    int   from = from_sq(m), to = to_sq(m), type = move_type(m);
    Piece pc = board[from];

    if (type == CASTLING) {
        bool kingSide = to > from;
        int  rfrom = kingSide ? from + 3 : from - 4;
        int  rto   = kingSide ? from + 1 : from - 1;
        Piece rook = make_piece(us, ROOK);
        move_piece(from, to);
        move_piece(rfrom, rto);
        k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to] ^ Zobrist::psq[rook][rfrom] ^ Zobrist::psq[rook][rto];
    } else {
        Piece captured = type == EN_PASSANT ? make_piece(them, PAWN) : board[to];
        if (captured != NO_PIECE) {
            int capsq = type == EN_PASSANT ? to - pawn_push(us) : to;
            if (type_of(captured) == PAWN) ns.pawnKey ^= Zobrist::psq[captured][capsq];
            remove_piece(capsq);
            k ^= Zobrist::psq[captured][capsq];
            ns.rule50   = 0;
            ns.captured = captured;
        }
        move_piece(from, to);
        k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

        if (type_of(pc) == PAWN) {
            ns.rule50 = 0;
            ns.pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
            if (type == PROMOTION) {
                Piece promo = make_piece(us, promotion_type(m));
                remove_piece(to);
                put_piece(promo, to);
                k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promo][to];
                ns.pawnKey ^= Zobrist::psq[pc][to];
            }
        }
    }

    if (ns.ep != NO_SQ) {
        k ^= Zobrist::enpassant[file_of(ns.ep)];
        ns.ep = NO_SQ;
    }
    if (type_of(pc) == PAWN && (to ^ from) == 16) {
        int epsq = (from + to) / 2;
        if (PawnAttacks[us][epsq] & pieces(them, PAWN)) {
            ns.ep = epsq;
            k ^= Zobrist::enpassant[file_of(epsq)];
        }
    }

    int cm = CastlingMask[from] | CastlingMask[to];
    if (ns.castling && cm) {
        k ^= Zobrist::castling[ns.castling];
        ns.castling &= ~cm;
        k ^= Zobrist::castling[ns.castling];
    }

    ns.key = k;
    stm = them;
    ++gamePly;
    states.push_back(ns);
    set_check_info();
}

void Position::undo_move(Move m) {
    stm = ~stm;
    Color us = stm;
    int from = from_sq(m), to = to_sq(m), type = move_type(m);
    Piece captured = st().captured;

    if (type == CASTLING) {
        bool kingSide = to > from;
        int  rfrom = kingSide ? from + 3 : from - 4;
        int  rto   = kingSide ? from + 1 : from - 1;
        move_piece(to, from);
        move_piece(rto, rfrom);
    } else {
        if (type == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (captured != NO_PIECE) {
            int capsq = type == EN_PASSANT ? to - pawn_push(us) : to;
            put_piece(captured, capsq);
        }
    }
    states.pop_back();
    --gamePly;
}

void Position::do_null_move() {
    StateInfo ns = st();
    ns.key ^= Zobrist::side;
    if (ns.ep != NO_SQ) {
        ns.key ^= Zobrist::enpassant[file_of(ns.ep)];
        ns.ep = NO_SQ;
    }
    ns.rule50++;
    ns.pliesFromNull = 0;
    ns.captured      = NO_PIECE;
    stm = ~stm;
    ++gamePly;
    states.push_back(ns);
    set_check_info();
}

void Position::undo_null_move() {
    states.pop_back();
    stm = ~stm;
    --gamePly;
}

bool Position::insufficient_material() const {
    if (pieces(PAWN) | pieces(ROOK) | pieces(QUEEN)) return false;
    Bitboard minors = pieces(KNIGHT) | pieces(BISHOP);
    if (popcount(minors) <= 1) return true;
    // Only bishops, all on the same square color
    if (!pieces(KNIGHT)) {
        Bitboard b = pieces(BISHOP);
        if (!(b & DarkSquares) || !(b & ~DarkSquares)) return true;
    }
    return false;
}

bool Position::is_draw(int /*ply*/) const {
    const StateInfo& si = st();
    if (si.rule50 >= 100) {
        if (!si.checkers) return true;
        MoveList list;
        generate_legal(*this, list);
        return list.size > 0;  // checkmate takes precedence over the 50-move rule
    }
    int end = si.rule50 < si.pliesFromNull ? si.rule50 : si.pliesFromNull;
    int n   = int(states.size()) - 1;
    if (end > n) end = n;
    for (int i = 4; i <= end; i += 2)
        if (states[n - i].key == si.key) return true;
    return insufficient_material();
}

bool Position::has_repeated() const {
    // True on a threefold repetition (the current position occurred twice before).
    const StateInfo& si = st();
    int end = si.rule50 < si.pliesFromNull ? si.rule50 : si.pliesFromNull;
    int n   = int(states.size()) - 1;
    if (end > n) end = n;
    int count = 0;
    for (int i = 4; i <= end; i += 2)
        if (states[n - i].key == si.key && ++count >= 2) return true;
    return false;
}

// Static Exchange Evaluation: does move m win at least `threshold` material
// after the full sequence of captures on its destination square?
bool Position::see_ge(Move m, int threshold) const {
    if (move_type(m) != NORMAL) return 0 >= threshold;

    int from = from_sq(m), to = to_sq(m);
    int swap = SeeValue[board[to]] - threshold;
    if (swap < 0) return false;
    swap = SeeValue[board[from]] - swap;
    if (swap <= 0) return true;

    Bitboard occ       = pieces() ^ sq_bb(from) ^ sq_bb(to);
    Color    side      = stm;
    Bitboard attackers = attackers_to(to, occ);
    Bitboard sideAttackers, bb;
    int res = 1;

    while (true) {
        side = ~side;
        attackers &= occ;
        if (!(sideAttackers = attackers & pieces(side))) break;
        if (st().pinners[~side] & occ) sideAttackers &= ~st().blockers[side];
        if (!sideAttackers) break;
        res ^= 1;

        if ((bb = sideAttackers & pieces(PAWN))) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = sideAttackers & pieces(KNIGHT))) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
        } else if ((bb = sideAttackers & pieces(BISHOP))) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = sideAttackers & pieces(ROOK))) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= rook_attacks(to, occ) & pieces(ROOK, QUEEN);
        } else if ((bb = sideAttackers & pieces(QUEEN))) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & pieces(BISHOP, QUEEN)) | (rook_attacks(to, occ) & pieces(ROOK, QUEEN));
        } else {
            // King: can only capture if the opponent has no attackers left.
            return (attackers & ~pieces(side)) ? res ^ 1 : res;
        }
    }
    return bool(res);
}

Move Position::parse_uci_move(const std::string& s) const {
    if (s.size() < 4) return MOVE_NONE;
    std::string str = s;
    for (auto& c : str) c = char(std::tolower(c));
    // Accept king-captures-rook castling notation (e1h1) as well.
    int from = make_sq(str[0] - 'a', str[1] - '1');
    if (from >= 0 && from < 64 && type_of(board[from]) == KING && board[from] != NO_PIECE) {
        if (str == "e1h1" && board[H1] == W_ROOK) str = "e1g1";
        if (str == "e1a1" && board[A1] == W_ROOK) str = "e1c1";
        if (str == "e8h8" && board[H8] == B_ROOK) str = "e8g8";
        if (str == "e8a8" && board[A8] == B_ROOK) str = "e8c8";
    }
    MoveList list;
    generate_legal(*this, list);
    for (int i = 0; i < list.size; ++i)
        if (move_to_uci(list.moves[i].move) == str) return list.moves[i].move;
    return MOVE_NONE;
}
