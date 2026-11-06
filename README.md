# Bitboard Chess Engine

This is a chess engine I wrote in C++17. It doesn't use any outside libraries, just the
standard library. Under the hood it's a pretty classic modern design: bitboards with magic
move generation, a PVS alpha-beta search with most of the usual pruning tricks, Lazy SMP for
multithreading, and a hand-tuned tapered evaluation.

You can use it in three ways:

- Plug it into a chess GUI over **UCI** (Arena, Cute Chess, Banksia, En Croissant, etc.)
- Talk to it over a small **REST API**. I wrote the HTTP server myself, so there are no dependencies there either
- Run the API in **Docker** if you don't want to build anything

## How well does it work?

Here's what I've measured so far:

| What | Result |
|---|---|
| Estimated strength | **~2950 Elo**, from 128 games vs. Stockfish 19 at fixed `UCI_Elo` levels (details below) |
| Perft correctness | 51/51 reference counts match (7 standard positions to depth 5–6, plus 14 en passant / castling / promotion edge cases) |
| Perft speed | ~197M leaf nodes/s (bulk counting, 1 thread) |
| Search speed | ~2.2–2.8M nodes/s per thread; reaches depth 22 from the start position in about 1.4 s |
| Tactics (Win At Chess sample) | 18/19 at 1 s/move (the Windows build also gets 18/19 at 300 ms) |
| Lazy SMP | 4 threads vs 1: +123 Elo (100 games, 5s+0.05s) |
| Evaluation terms | full eval vs. plain PeSTO tables: +179 Elo (160 games) |
| Singular extensions | on vs. off: +35 Elo (160 games) |
| Sanitizers | clean under ASan + UBSan (perft, bench, 4-thread search, API) |

### Rating against Stockfish

To get a rough overall rating, I played the engine (1 thread, 64 MB hash) against Stockfish
19 with its strength limited to set `UCI_Elo` levels. Each opening was played twice so both
sides got White.

| Stockfish level | Games | W / D / L | Score |
|---|---|---|---|
| 2200 | 16 | 16 / 0 / 0 | 100% |
| 2600 | 16 | 10 / 6 / 0 | 81% |
| 2800 | 64 | 38 / 15 / 11 | 71% |
| 3000 | 64 | 10 / 36 / 18 | 44% |

The 2800 and 3000 matches both work out to a performance of about **2950**, so that's my
current estimate. The lower levels don't say much: once the engine wins nearly every game,
the score only shows it's a lot stronger than that level, not how much. Take the number with
a grain of salt. It's measured against Stockfish's weakened mode in fast games, not taken
from an official rating list like CCRL, and it's nothing like a human FIDE rating.

The other Elo numbers in the table come from self-play A/B matches. They show how much each
feature helped, not how strong the engine is overall, and with 160 games the error bars are
roughly ±40 Elo.

## Building it

**Linux / macOS / WSL** (g++ 9+ or clang 10+):
```bash
make                  # optimised for your CPU (-march=native)
make ARCH=x86-64-v2   # portable build
make test             # perft suite up to depth 5, takes a few seconds
make test-full        # the full perft suite
make tactics          # WAC tactics test (needs python3)
make api-test         # end-to-end REST API tests (needs python3)
make bench            # fixed-depth benchmark, prints nodes/second
make debug            # ASan + UBSan build
```

**Windows (MSVC)**: run `build_windows.bat`. It finds Visual Studio 2019 or 2022 through
vswhere and builds `engine.exe`. The Windows and Linux builds search exactly the same way, so
`bench 12` gives the same node count on both, which is a nice sanity check.

## Running it

```bash
./engine                               # UCI mode, point your GUI at this
./engine serve --port 8080 --threads 4 --hash 256
./engine perft 6                       # perft from the start position
./engine perft 5 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
./engine bench 13
./engine "position startpos moves e2e4" "go movetime 2000"   # one-off UCI commands
```

Besides the standard UCI commands, I added a few that help with debugging: `d` prints the
board, `eval` shows the evaluation broken down by term, `perft N` and `go perft N` (divide),
and `bench`. The supported options are `Hash`, `Threads`, `Move Overhead` and `Clear Hash`.

## REST API

Every endpoint takes either a `POST` with a JSON body or a `GET` with query parameters. If
you leave out `fen` it uses the start position, and `moves` is an optional list of UCI moves
to play from there.

| Endpoint | Body | Returns |
|---|---|---|
| `GET /health` | — | `{"status":"ok"}` |
| `GET /` | — | engine info and a list of endpoints |
| `POST /move` | `fen`, `moves`, plus one of `movetime` (ms), `depth`, `nodes`, or `wtime`/`btime`/`winc`/`binc`/`movestogo` | `bestmove`, `ponder`, `score` (`cp`/`mate`), `depth`, `seldepth`, `nodes`, `nps`, `time_ms`, `pv`, `status` |
| `POST /legal-moves` | `fen`, `moves` | legal moves, `in_check`, `status` (`ongoing`/`checkmate`/`stalemate`/`draw_*`) |
| `POST /eval` | `fen`, `moves` | static eval (from the side to move and from White) with a breakdown by term |
| `POST /perft` | `fen`, `moves`, `depth` (1–7) | node count, per-move divide, nps |

```bash
curl -X POST localhost:8080/move -H 'Content-Type: application/json' \
     -d '{"fen":"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4","movetime":500}'
# {"bestmove":"h5f7","ponder":null,"score":{"type":"mate","value":1},"depth":9, ...}

curl "localhost:8080/move?moves=e2e4,e7e5&movetime=1000"
```

If you send something bad (an invalid FEN, an illegal move, broken JSON) you get a `400`
back with `{"error": "..."}`. Searches run one at a time, since each search already uses all
the threads you gave it. To stop the server from being abused, `movetime` is capped at 60
seconds, and body size, header size and concurrent connections are all limited.

## Docker

```bash
docker build -t chess-engine .                           # portable (x86-64-v2 / armv8-a)
docker build -t chess-engine --build-arg ARCH=native .   # faster, but only for this CPU
docker run -p 8080:8080 -e THREADS=4 -e HASH=256 chess-engine
docker compose up                                        # same thing via docker-compose.yml
docker run -i --rm chess-engine uci                      # UCI mode inside the container
```

The Dockerfile is a multi-stage build. It compiles a static binary and copies it into a slim
image that runs as a non-root user. One thing I like about it: the build runs the perft suite,
so if move generation is ever broken, the image won't build.

---

## How it works

### 1. Bitboards (`bitboard.*`, `position.*`)
Every piece type and color gets its own 64-bit board, and there's also a 64-square array for
quickly answering "what's on this square?". Pawn, knight and king attacks come from
precomputed tables. Sliding pieces use fancy magic bitboards:
`attacks = table[((occupied & mask) * magic) >> shift]`. The engine finds its magic numbers at
startup with a fixed-seed random number generator, which takes a few milliseconds.
`BetweenBB` and `LineBB` tables turn most pin and check logic into a couple of ANDs.

A move fits in 16 bits (`from | to<<6 | promo<<12 | type<<14`). `do_move` and `undo_move`
update the bitboards, Zobrist hash, pawn hash and material/PST score incrementally. Anything
that can't be undone (castling rights, en passant square, 50-move counter, captured piece)
lives on a state stack, along with checkers, pinned pieces and check squares for the side to
move.

### 2. Search (`search.cpp`)
The core is a negamax Principal Variation Search. The first move at each node gets a full
window, the rest get a null window, and if one of them beats alpha it gets re-searched. On
top of that:

- **Transposition table**: 32-byte buckets holding 3 entries each, replaced by depth and age.
  It's lock-free, and mate scores are stored relative to the node.
- **Quiescence search**: captures, queen promotions and check evasions, with SEE and delta
  pruning.
- **Pruning**: null move (adaptive R = 3 + d/3 + a margin term), reverse futility, razoring,
  ProbCut, late move pruning, futility pruning, history pruning, SEE pruning for captures and
  quiets, and mate distance pruning.
- **Reductions and extensions**: late move reductions from a log·log table, adjusted for
  history, PV/cut nodes, whether the position is improving, killers and checks. Also internal
  iterative reduction, singular extensions (including double/negative extensions and
  multi-cut), and check extensions.
- **Draws**: repetition, the 50-move rule (checkmate wins if both happen at once) and
  insufficient material.

### 3. Move ordering
Each node scores its moves once and then picks the best remaining one each time (a lazy
selection sort). The order is:

1. The TT move (or at the root, the best move from the last iteration)
2. Queen promotions
3. Good captures (SEE ≥ −50), sorted by MVV plus capture history
4. Killer moves (2 per ply)
5. The counter move (the quiet move that last refuted the opponent's previous move)
6. Other quiet moves, sorted by butterfly history plus continuation history (1, 2 and 4 plies
   back), with a bonus for moves that give direct check
7. Bad captures (SEE < −50)
8. Underpromotions

The history tables use "gravity" updates (`h += bonus − h·|bonus|/16384`). When a quiet move
causes a cutoff it gets a bonus, and the quiet moves tried before it get the same amount as a
penalty.

### 4. Iterative deepening and time management
The engine searches depth 1, then 2, then 3 and so on, using aspiration windows (±15 cp,
widened when the search fails high or low). Time is split into a soft limit and a hard limit.
The soft limit is roughly time/30 + ¾ of the increment, and it gets stretched or shrunk based
on how stable the best move is and whether the score is dropping. Once about 60% of it is used,
the engine won't start a new iteration. The hard limit is checked every 1024 nodes and stops
the search immediately. If an iteration gets cut off, its best move is still used, but only if
that move was fully searched and doesn't score much worse than the previous best.

### 5. Multithreading with Lazy SMP
With `Threads = N`, the engine starts N workers. Each one has its own copy of the position and
its own history, killer and counter-move tables, and they all search the same root position.
The only thing they share is the transposition table, so when one thread finishes a line, the
others can get a cutoff or a good hash move out of it. Some helper threads skip certain depths
so they aren't all doing identical work. The main thread watches the clock and sets a shared
atomic stop flag, and the final move comes from whichever thread completed the deepest
iteration.

Why Lazy SMP? Older approaches like YBWC split the tree at specific nodes, which means locks,
work stealing and a lot of bookkeeping. Lazy SMP barely needs any synchronisation. Even the TT
has no locks: if an entry gets half-written by two threads, that's fine, because every TT move
is checked against the real move list before it's played. It scales well on modern CPUs, and
it's what Stockfish uses too. In my tests, 4 threads came out +123 Elo ahead of 1.

### 6. Evaluation (`eval.cpp`)
The evaluation is tapered. It keeps a middlegame score and an endgame score and blends them by
game phase (N=B=1, R=2, Q=4, max 24). It looks at:

- **Material and piece-square tables** (PeSTO values), updated incrementally in `do_move`
- **Pawn structure**: doubled, isolated and backward pawns
- **Passed pawns**: a bonus by rank, king distance to the square in front, a bonus for a clear
  path, and the rule of the square against a lone king
- **Mobility** for knights, bishops, rooks and queens (counting squares not attacked by enemy
  pawns; sliders can see through their own queen and rooks)
- **King safety**: attack units from pieces hitting the king zone, weak squares and safe
  checks, scaled quadratically, plus pawn shield and open files near the king
- **Pieces**: bishop pair, knight and bishop outposts, rooks on open or semi-open files and on
  the 7th rank
- **Threats**: pieces attacked by pawns, minors attacking majors, rooks attacking queens, and
  hanging pieces
- **Endgames**: scaling down drawish material (KNK, KBK, KNNK, KR vs minor, opposite-coloured
  bishops) and mop-up terms that push a lone king to the edge
- A small **tempo** bonus for the side to move

### 7. Perft testing (`perft.cpp`, `tests/perft_suite.epd`)
`./engine perftsuite tests/perft_suite.epd` checks the move generator against published node
counts. I picked positions that cover the stuff move generators usually get wrong: en passant
that exposes the king along a rank, en passant that gives check, castling through or out of
check, losing castling rights when a rook gets captured, promotions that capture or give
check, and escaping double check. All 51 match, including 193,690,690 nodes for Kiwipete at
depth 5.

### 8. REST API and Docker (`http_server.cpp`, `json.cpp`, `Dockerfile`)
The HTTP/1.1 server sits directly on BSD sockets (Winsock on Windows). It uses one thread per
connection and sends CORS headers, with limits on request size. I also wrote a small JSON
parser, which is why the project needs nothing beyond the C++ standard library.

## Project layout

```
src/
  types.h            core types, move encoding, scores
  bitboard.{h,cpp}   bitboard helpers, magic bitboards, attack tables
  position.{h,cpp}   board state, FEN, make/unmake, legality, SEE, draw detection
  movegen.{h,cpp}    pseudo-legal / legal move generation
  eval.{h,cpp}       evaluation
  tt.{h,cpp}         transposition table
  search.{h,cpp}     search, move ordering, time management, Lazy SMP
  perft.{h,cpp}      perft + suite runner
  uci.{h,cpp}        UCI protocol, bench
  http_server.{h,cpp}, json.{h,cpp}   REST API
  main.cpp           command-line entry point
tests/
  perft_suite.epd    reference perft counts
  tactics.py         WAC tactical test (UCI)
  api_test.py        REST API end-to-end tests
Dockerfile, docker-compose.yml, Makefile, build_windows.bat
```
