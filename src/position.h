// Board representation: piece bitboards + mailbox, make/unmake, legality, SEE.
#pragma once

#include <string>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace Zobrist {
extern Key psq[PIECE_NB][64];
extern Key enpassant[8];
extern Key castling[16];
extern Key side;
}  // namespace Zobrist

// Material+piece-square values (white's perspective), defined in eval.cpp.
extern Score PSQT[PIECE_NB][64];

// Per-ply irreversible state, kept on a stack so moves can be undone.
struct StateInfo {
    Key      key;
    Key      pawnKey;
    int      castling;
    int      ep;
    int      rule50;
    int      pliesFromNull;
    Piece    captured;
    Bitboard checkers;
    Bitboard blockers[COLOR_NB];   // pieces blocking a slider attack on king of color c
    Bitboard pinners[COLOR_NB];    // sliders of color c pinning an enemy piece to the enemy king
    Bitboard checkSquares[PIECE_TYPE_NB];
};

class Position {
public:
    static void init();

    Position() : byType{}, byColor{}, stm(WHITE), gamePly(0) {
        for (auto& p : board) p = NO_PIECE;
        states.resize(1);
    }  // empty board: call set() before use
    bool set(const std::string& fen, std::string* error = nullptr);
    std::string fen() const;
    std::string pretty() const;

    // Accessors
    Bitboard pieces() const { return byColor[WHITE] | byColor[BLACK]; }
    Bitboard pieces(Color c) const { return byColor[c]; }
    Bitboard pieces(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType[a] | byType[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const { return byColor[c] & (byType[a] | byType[b]); }
    Piece    piece_on(int s) const { return board[s]; }
    Color    side_to_move() const { return stm; }
    int      king_sq(Color c) const { return lsb(pieces(c, KING)); }
    int      ep_square() const { return st().ep; }
    int      castling_rights() const { return st().castling; }
    int      rule50() const { return st().rule50; }
    int      game_ply() const { return gamePly; }
    Key      key() const { return st().key; }
    Key      pawn_key() const { return st().pawnKey; }
    Bitboard checkers() const { return st().checkers; }
    bool     in_check() const { return st().checkers != 0; }
    Bitboard blockers(Color c) const { return st().blockers[c]; }
    Bitboard check_squares(PieceType pt) const { return st().checkSquares[pt]; }
    Piece    captured_piece() const { return st().captured; }
    Score    psq_score() const { return psq; }
    int      non_pawn_material(Color c) const;

    Bitboard attackers_to(int s, Bitboard occ) const;
    Bitboard attackers_to(int s) const { return attackers_to(s, pieces()); }

    // Move properties
    bool legal(Move m) const;          // m must be pseudo-legal
    bool gives_check(Move m) const;
    bool is_capture(Move m) const {
        return (board[to_sq(m)] != NO_PIECE && move_type(m) != CASTLING) || move_type(m) == EN_PASSANT;
    }
    bool is_capture_or_promotion(Move m) const { return is_capture(m) || move_type(m) == PROMOTION; }
    Piece moved_piece(Move m) const { return board[from_sq(m)]; }
    bool see_ge(Move m, int threshold) const;

    // Make / unmake
    void do_move(Move m);
    void undo_move(Move m);
    void do_null_move();
    void undo_null_move();

    bool is_draw(int ply) const;
    bool has_repeated() const;
    bool insufficient_material() const;

    Move parse_uci_move(const std::string& s) const;  // MOVE_NONE if illegal

    static const char* StartFEN;

private:
    void put_piece(Piece p, int s);
    void remove_piece(int s);
    void move_piece(int from, int to);
    void set_check_info();
    Bitboard slider_blockers(Bitboard sliders, int s, Bitboard& pinners) const;
    void compute_keys();

    const StateInfo& st() const { return states.back(); }
    StateInfo&       st() { return states.back(); }

    Bitboard byType[PIECE_TYPE_NB];
    Bitboard byColor[COLOR_NB];
    Piece    board[64];
    Color    stm;
    int      gamePly;
    Score    psq;
    std::vector<StateInfo> states;
};

// Piece values used by SEE and move ordering.
constexpr int SeeValue[PIECE_NB + 1] = {100, 320, 330, 500, 950, 20000, 100, 320, 330, 500, 950, 20000, 0};
