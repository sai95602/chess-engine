#!/usr/bin/env python3
"""End-to-end tests for the REST API (standard library only).

Starts `engine serve` on a free port, exercises every endpoint, then shuts it down.
Usage: python3 tests/api_test.py [./engine]
"""
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ENGINE = sys.argv[1] if len(sys.argv) > 1 else "./engine"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PORT = free_port()
BASE = f"http://127.0.0.1:{PORT}"


def request(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


failures = 0


def check(name, cond, detail=""):
    global failures
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"  ({detail})" if not cond else ""))
    failures += not cond


def main():
    proc = subprocess.Popen([ENGINE, "serve", "--host", "127.0.0.1", "--port", str(PORT), "--threads", "2"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                if request("GET", "/health")[0] == 200:
                    break
            except OSError:
                time.sleep(0.05)

        s, r = request("GET", "/health")
        check("health", s == 200 and r["status"] == "ok", r)

        s, r = request("POST", "/move", {"fen": "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
                                         "movetime": 300})
        check("finds mate in one (Qxf7#)", s == 200 and r["bestmove"] == "h5f7" and r["score"]["type"] == "mate", r)

        s, r = request("POST", "/move", {"moves": ["e2e4", "e7e5"], "depth": 8})
        check("move from startpos + moves", s == 200 and r["depth"] >= 8 and len(r["bestmove"]) >= 4, r)

        s, r = request("GET", "/move?fen=6k1/5ppp/8/8/8/8/5PPP/3R2K1%20w%20-%20-%200%201&movetime=200")
        check("GET with query string (back-rank mate)", s == 200 and r["bestmove"] == "d1d8", r)

        s, r = request("POST", "/move", {"fen": "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"})
        check("checkmated position reports status", s == 200 and r["bestmove"] is None and r["status"] == "checkmate", r)

        s, r = request("POST", "/legal-moves", {"fen": "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1"})
        check("legal moves incl. castling", s == 200 and r["count"] == 26 and "e1g1" in r["moves"] and "e1c1" in r["moves"], r)

        s, r = request("POST", "/perft", {"depth": 4})
        check("perft depth 4 = 197281", s == 200 and r["nodes"] == 197281, r)

        s, r = request("POST", "/eval", {})
        check("eval of start position is small", s == 200 and abs(r["eval_cp"]) < 60, r)

        s, r = request("POST", "/move", {"fen": "not a fen"})
        check("bad FEN -> 400", s == 400 and "error" in r, r)

        s, r = request("POST", "/move", {"moves": ["e2e5"]})
        check("illegal move -> 400", s == 400 and "illegal" in r["error"], r)

        s, r = request("POST", "/perft", {"depth": 99})
        check("perft depth limit -> 400", s == 400, r)

        s, r = request("GET", "/does-not-exist")
        check("unknown path -> 404", s == 404, r)
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print("\nAll API tests passed" if failures == 0 else f"\n{failures} API test(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
