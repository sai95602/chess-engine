#!/usr/bin/env python3
"""Tactical sanity test: positions from the "Win At Chess" (WAC) suite.

Runs the engine over UCI with a fixed time per position and checks the best move.
Usage: python3 tests/tactics.py [./engine] [movetime_ms]
"""
import subprocess
import sys

POSITIONS = [
    ("2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - -", "g3g6", "WAC.001 Qg6"),
    ("8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - -", "b3b2", "WAC.002 Rxb2"),
    ("5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - -", "e3g3", "WAC.003 Rg3"),
    ("r1bq2rk/pp3pbp/2p1p1pQ/7P/3P4/2PB1N2/PP3PPR/2KR4 w - -", "h6h7", "WAC.004 Qxh7+"),
    ("5k2/6pp/p1qN4/1p1p4/3P4/2PKP2Q/PP3r2/3R4 b - -", "c6c4", "WAC.005 Qc4+"),
    ("7k/p7/1R5K/6r1/6p1/6P1/8/8 w - -", "b6b7", "WAC.006 Rb7"),
    ("rnbqkb1r/pppp1ppp/8/4P3/6n1/7P/PPPNPPP1/R1BQKBNR b KQkq -", "g4e3", "WAC.007 Ne3"),
    ("r4q1k/p2bR1rp/2p2Q1N/5p2/5p2/2P5/PP3PPP/R5K1 w - -", "e7f7", "WAC.008 Rf7"),
    ("3q1rk1/p4pp1/2pb3p/3p4/6Pr/1PNQ4/P1PB1PP1/4RRK1 b - -", "d6h2", "WAC.009 Bh2+"),
    ("2br2k1/2q3rn/p2NppQ1/2p1P3/Pp5R/4P3/1P3PPP/3R2K1 w - -", "h4h7", "WAC.010 Rxh7"),
    ("r1b1kb1r/3q1ppp/pBp1pn2/8/Np3P2/5B2/PPP3PP/R2Q1RK1 w kq -", "f3c6", "WAC.011 Bxc6"),
    ("4k1r1/2p3r1/1pR1p3/3pP2p/3P2qP/P4N2/1PQ4P/5R1K b - -", "g4f3", "WAC.012 Qxf3+"),
    ("5rk1/pp4p1/2n1p2p/2Npq3/2p5/6P1/P3P1BP/R4Q1K w - -", "f1f8", "WAC.013 Qxf8+"),
    ("r2rb1k1/pp1q1p1p/2n1p1p1/2bp4/5P2/PP1BPR1Q/1BPN2PP/R5K1 w - -", "h3h7", "WAC.014 Qxh7+"),
    ("1R6/1brk2p1/4p2p/p1P1Pp2/P7/6P1/1P4P1/2R3K1 w - -", "b8b7", "WAC.015 Rxb7"),
    ("r4rk1/ppp2ppp/2n5/2bqp3/8/P2PB3/1PP1NPPP/R2Q1RK1 w - -", "e2c3", "WAC.016 Nc3"),
    ("R7/P4k2/8/8/8/8/r7/6K1 w - -", "a8h8", "WAC.018 Rh8"),
    ("r1b2rk1/ppbn1ppp/4p3/1QP4q/3P4/N4N2/5PPP/R1B2RK1 w - -", "c5c6", "WAC.019 c6"),
    ("r2qkb1r/1ppb1ppp/p7/4p3/P1Q1P3/2P5/5PPP/R1B2KNR b kq -", "d7b5", "WAC.020 Bb5"),
]


def main():
    engine = sys.argv[1] if len(sys.argv) > 1 else "./engine"
    movetime = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    proc = subprocess.Popen([engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(cmd):
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()

    def wait_for(prefix):
        while True:
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError("engine exited")
            if line.startswith(prefix):
                return line.strip()

    send("uci")
    wait_for("uciok")
    solved = 0
    for fen, expected, name in POSITIONS:
        send("ucinewgame")
        send("position fen " + fen)
        send("isready")
        wait_for("readyok")
        send(f"go movetime {movetime}")
        best = wait_for("bestmove").split()[1]
        ok = best == expected
        solved += ok
        print(f"{'PASS' if ok else 'FAIL'}  {name:16s} expected {expected}  got {best}")
    send("quit")
    print(f"\nSolved {solved}/{len(POSITIONS)} at {movetime} ms per position")
    return 0 if solved >= len(POSITIONS) - 2 else 1


if __name__ == "__main__":
    sys.exit(main())
