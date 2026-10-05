# Supercube solver (S0 -> solved)

## Production web build (2026-10-04) -- the page `SuperCubeSolver444/index.html` (titled "4x4x4 Seven-Phase Solver")
The solver runs in the browser as WebAssembly inside a web worker. Nothing else of the dev tooling (CLI, self-tests, profile, REPL, S6/S7 ending) is in the build.

**Files.** Deployed (all in `SuperCubeSolver444/`): `index.html` (UI), `worker.js` (loads the WASM, builds tables, runs solves; bump its `VERSION` whenever the
three files below are rebuilt, it is appended to their URLs as cache buster), `solver.js` + `solver.wasm` + `solver.data` (built; 66 KB + 425 KB + 1.77 MB, the data file is
the two cached S3'->S4 tables `s4_sym_*.bin`, preloaded to `/cache/`; a stale or missing cache is detected by its fingerprint and rebuilt in the browser, +5 s).
Sources: `cpp/web_solver.h` (the production layer, below), `cpp/web_main.cpp` (3 exported C functions: `sc_init(safe)`, `sc_solve(...)` -> JSON, plus 2 diagnostics), and the
solver headers it includes (no `main.cpp`, no `selftest.h`). Build: `powershell -File cpp\build_wasm.ps1` (uses `..\emsdk`, ~15-80 s). Tests: `g++ -O2 -std=c++17 -o web_test web_test.cpp`
then `web_test --units` (notation, rotations, random states, rotation-free output on 4000 random paths), `web_test --solve N [--normal] [--given] [--prefix ".."] [--suffix ".."]`,
`web_test --dump-states N FILE` (random states, checked against the group with sympy: 400/400 members), and in Node `node web_node_test.js [safe|normal] N`, `node web_node_stats.js [safe|normal] N`.

**UI.** Title, toggle random-state scramble (default) / solve given position (a scramble box appears; blank = random state), checkbox "Supercube-safe?" (default off),
"Start Padding" / "End Padding" (= the forced prefix / suffix; placeholder "Leave blank for no padding"), Run. Status line: "Loading tables" (cycling dots) while tables are built (at page load, both modes; no rebuild when the checkbox changes),
then "Solving", "Solving.", "Solving..", "Solving..." cycling every 0.4 s (each period is its own span: the font merges adjacent periods into an ellipsis otherwise) while the worker computes.

**Modes** (`web::set_flags`, `web::build_tables`). Supercube-safe (exact supercube search): `--cost12 7 --cost23p 7 --cost34p 11 --cost45 8 --ls --cost-ls 9 --ls-depth 9`
(NISS is always on; penalty threshold / rate are the defaults 7 / 1). Not safe (solves the normal 4x4x4, centers permuted inside faces allowed): `--cost12 8 --cost23p 6 --cost34p 10 --cost45 9 --ls --cost-ls 9 --ls-depth 9 --niss --random-start --rs-max-d 10 --s1-depth 7 --penalty-threshold 8 --penalty-rate 1`.
The tables of BOTH modes are built once at page load (~5 s, status "Loading tables" with the cycling dots; the page content is shown first): one S1 endtable (depth 7) serves both
modes (the supercube costs were tuned with depth 6; depth 7 gives the same average length, 52.83, with ~40% fewer nodes: median 0.64M instead of 1.01M, max 3.6M instead of 6.1M, median time 0.29 s), and a mode switch only sets the costs and rebuilds the
2520-entry penalty tables (instant).

**Position.** Given scramble: moves `U D R L F B`, wide `Rw` or `r` (also `Lw Dw Bw`), rotations `x y z`, suffixes `' 2 2'` and typographic primes; `Lw^k = Rw^k x^-k`, `Dw^k = Uw^k y^-k`,
`Bw^k = Fw^k z^-k` (checked against `archive/cube_model.py`), then the solver's own `parse_alg`. Slices (`M E S`) and `3Rw` are rejected with a message.
Random state (`web::random_cube`): a uniformly random element of the group the solver works in: Fisher-Yates over the 8 corners, twists summing to 0 mod 3, Fisher-Yates over the 24 wings
(any permutation), Fisher-Yates over the 24 centers, then one swap if the center parity differs from the corner parity (those two are tied; the group has order 8! 3^7 24! 24!/2, computed with sympy,
and 400/400 generated states are members, while a lone center swap or two swapped corner stickers are not). It contains the whole-cube rotations, so the orientation of the state is random too.
Wings are distinct pieces (the solver reaches the literal solved cube); the visible normal-cube state is uniform as well (equal fibers).

**Forced ends.** Given position: prefix P / suffix L go to the solver as they are (answer P Z L). Random state: the solver gets inverse(L) as prefix and inverse(P) as suffix and the printed scramble is
the inverse of its solution, i.e. P ... L. The solver's cube is L S P (`cube_then`).

**Output** (`to_rotation_free`): moves only. rho[f] = the face of the never-rotated output cube that is face f of the path's cube; a path rotation re-labels rho (`R y Fw2` -> `R Rw2`); a wide move of the B / L / D side
(none of the solver's 27 moves, but a rotation turns `Rw2` into `Lw2`...) is printed as `Fw / Rw / Uw` and the rotation that this leaves on the output cube is folded into rho. The printed moves reach the same state up to a whole-cube rotation
(a forced suffix may thus come out rotated). `web::run` checks everything before answering: the solution solves the position literally (visibly, for the normal cube) and the printed moves reproduce the state up to rotation; otherwise it answers "internal error".

**Numbers** (100 random-state solves per row; native = g++ -O2, WASM = Node 24 / V8 on the same machine; a hidden browser tab is throttled 2-3x, a foreground one should match Node):
| | avg length | nodes median / p90 / max | time median / p90 / max | tables | WASM heap |
|---|---|---|---|---|---|
| safe, native | 52.52 | 1.31M / 3.7M / 8.7M | 0.77 s / 2.2 s / 9.2 s | 1.9 s | |
| safe, WASM (first config, cost12 8 / 23p 6 / 34p 10 / 45 9, penalty 8) | 52.57 | 0.87M / 4.4M / 7.9M | 0.80 s / 4.0 s / 10.1 s | 0.9-1.1 s | 160-176 MB |
| safe, WASM (current config above, depth 7: 52.83, 0.64M / 2.1M / 3.6M nodes, 0.29 s / 0.78 s / 3.3 s; with depth 6:) | 52.83 | 1.01M / 2.7M / 6.1M | 0.58 s / 1.8 s / 5.2 s | 1.1 s (now ~5 s: both modes are built at load) | 160 MB (now 192-214 MB) |
| not safe, native | 49.14 | 1.28M / 3.6M / 5.5M | 0.85 s / 2.3 s / 4.5 s | | |
| not safe, WASM | 49.17 | 1.17M / 3.9M / 7.0M | 0.88 s / 3.1 s / 5.4 s | 5.7 s | 192-214 MB |
(The spec's 52.55 / 49.26 are matched.) The same fixed scramble gives the identical solution, node for node (965,885), in native and WASM in supercube-safe mode; the normal mode's random start uses `std::shuffle`,
which differs between libstdc++ and libc++, so its runs are not comparable bit for bit. A rare heavy tail exists: one normal-mode state took 85M nodes (55 s) among ~215 normal-mode solves (WASM + native); all the others stayed below 6 s (supercube-safe: the worst of 200 was 10 s).
Both modes' tables are built together at load (~5 s in Node, WASM heap 192-214 MB); switching the checkbox is instant.

**WASM-specific change.** WebAssembly has no prefetch instruction, and the 35M random Bloom inserts of the depth-7 S1 table took 8.2 s. `BlockedBloom::add_all` (phase1.h) groups the keys of a chunk by filter block
(radix sort on the block number) under `__EMSCRIPTEN__` -> 2.9 s; the native build keeps the prefetching loop. Memory: 160 MB initial, growth allowed up to 1 GB, 8 MB stack.

**Archive (not needed by the web build):** `cpp/main.cpp`, `cpp/selftest.h`, `cpp/solver.exe`, `cpp/web_test.exe`, `cpp/empty_stdin.txt`, the four stale 124 MB `cpp/s4_*layer*ud*.bin` / `s4_eqpll*_ud*.bin` (not `s4_sym_*`), `phase_distance_study/`.

## Read this first: current state (2026-10-04)
A tiered IDA* solver for the 4x4x4 SUPERCUBE (every center piece distinguishable): S0 -> S1 -> S2 -> S3' -> S4 -> S5 -> (S6 -> S7 | LS -> solved),
one search with flat COSTxy gaps between the tiers, NISS (normal/inverse switching), rotation forks, forced prefix/suffix and a mode for the
NORMAL 4x4x4 (`--random-start`). Single translation unit: `g++ -O3 -std=c++17 -o solver main.cpp` (`solver.exe` in this folder is a build of the current sources).
Everything below is the design log of what exists, newest changes in the last sections (start-up cost, speed work, LS).

**Endings.** The default ending is the old S5 -> S6 -> S7. `--ls` selects S5 -> LS (leave slice) -> solved (last-but-one section): shorter
solutions and fewer nodes at the same costs, and it builds ~14 MB fewer tables (no S6/S7). LS is expected to become the default.

**The configuration this README was last measured with** (random-start, i.e. solving the normal cube):

    solver --cost12 8 --cost23p 6 --cost34p 10 --cost45 9 --ls --cost-ls 9 --ls-depth 9 --niss --random-start --rs-max-d 10 \
           --s1-depth 7 --profile 200 --penalty-threshold 8 --penalty-rate 1 --tier-stats

Flags worth knowing (`solver --help` lists all; unknown flags are an error): `--ls --cost-ls N --ls-depth N|--ls-entries N --ls-niss-all --ls-lookup M`,
`--s1-depth N` (exact distances to S1: 6 default, 7 = 48.7 MB / ~1 s start-up, shorter solutions in `--random-start`), `--p2-depth N`,
`--penalty-threshold N --penalty-rate N` (whole number, 1), `--cost12 --cost23p --cost34p --cost45 --cost56 --cost67` (legacy ending; LS replaces cost56+cost67 by
`--cost-ls`), `--random-start --rs-trials N --rs-max-d N`, `--prefix/--suffix`, `--niss` (always on), `--profile N --tier-stats --print-solutions`,
`--check-replay`, `--check-s4-sym`, `--skip-selftest`, `--startup-times`, `--no-checkpoints`, `--no-s2-fast` (A/B switches), `--cache-dir DIR`.

### Resource usage of that configuration (measured; this machine's timings drift 2-3x between runs, so read the ranges as "this machine, one afternoon")
**Start-up** (tables ready, self-tests off, the two cache files present): about 1.7-2.7 s. Step by step (one run, 2.66 s): `p1::build` (S1 endtable, depth 7)
1,281 ms (35M random Bloom insertions; depth 6 would be ~0.25 s and 45 MB less), S5 table 151, wing coset table 110 (+ printout 10-85),
S2 joint endtable 125, S2-switch tables 110, LS tables 107, S3'->S4 symmetry setup + cache load 92, S4 coordinates / centers / wing-good ~100, the rest < 20 ms each.
The startup self-tests (left on by default) add ~2-4 s: use `--skip-selftest` in production. Without the two cache files (first run) the
symmetry-reduced S3'->S4 tables are built from the full BFS: +5.3 s (T1 3.9 s, T2 1.0 s) and the files are written next to the binary; a failed write is only a warning.
**Data pushed to the site:** the two cache files `s4_sym_layer_ud.bin` (1,270,088 B) and `s4_sym_eqpllLRFB_ud.bin` (495,368 B) = 1.77 MB, plus the program
itself (`solver.exe` is 0.87 MB native; the WASM size is not measured). Every other table is computed at start-up. (The four `s4_layer_ud*.bin` /
`s4_eqpllLRFB_ud*.bin` files, 124 MB, in this folder are stale and not read by anything; `pairing_dist_io.h`'s load/save code is unused legacy.)
**Memory while running** (Windows working set, `GetProcessMemoryInfo`): 120 MB steady after start-up, 142-144 MB peak during start-up (transient BFS
buffers), 152 MB peak while solving (25 random-start solves). Main parts: S1 endtable 48.7 MB (depth 7; depth 6 ~4 MB), wing coset distance table 8.4 MB,
LS endtable 6.0 MB (depth 9) + LS transition tables ~4 MB, S2 joint endtable 5.7 MB, F/B transitions 3.4 MB, S3'->S4 reduced tables + group maps 3.2 MB,
S2-switch tables 2.9 MB, S5 table 2.2 MB: ~85 MB; the other ~35 MB is the remaining coordinate tables, transitions and allocator overhead (not itemized;
the program's own "~71 MB of tables" line undercounts). A bug fixed on 2026-10-04: `--random-start` allocated a ~1.3 KB `RootEntry` for EVERY candidate coset
(several hundred thousand) and peaked at 554 MB; it is now a lazy map (`rs::solve_random_start`), peak 152 MB, results identical.
**Search** (25 random-start solves of the configuration above, seed 2024): avg length 49.00 (46-51), avg nodes 1,434,169 (median 978,038), avg solve
time ~2.2 s with this machine's noise (the full `--profile 200` run itself was not repeated for these numbers).

### How to re-verify after a change
Node counts and lengths are exact, wall-clock is not. References: `solver --profile 200 --seed 21` (default ending) = avg nodes 266,381, avg length 57.56, 200/200
literal; the random-start line above with `--profile 40` and `--cost23p 7 --cost-ls 10` = 321,078 nodes (321,061 since the LS endtable's last layer is
sized by an estimate), length 50.65. Every table has an exhaustive self-test (distance-field characterization for S5, wing coset, S2-switch wing classes, the S3'->S4
tables via `--check-s4-sym`: 0 of 62.4M entries differ from a fresh BFS). `--check-replay` compares every NISS branch and every checkpointed state rebuild with a
from-the-root replay (0 mismatches). A/B timing: `--no-checkpoints`, `--no-s2-fast`, `--ls-lookup`, `--ls-niss-all` switch single optimizations off.

### Where the speed work stands (all in the sections below)
Search: checkpointed state rebuilds at tier crossings (~12% faster), integer penalties, budgeted S2 heuristic with a table-based wing index (~10%), one-probe LS endtable
lookups; profile = S1 child steps ~34%, S2 ~27%, tier crossings ~28% (the NISS variants are the biggest piece still unoptimized, ~7%). Start-up: see "Start-up cost".
Tried and rejected: one-probe S1 endtable (no measurable gain), fingerprint table for the LS endtable (slower), multithreaded Bloom inserts (not for a browser build).

Run from this folder (cached tables are written next to the binary, or into `--cache-dir`; the 120 MB `s3prime_wing_dist.bin` of the earlier normal S2 -> S3' route is no longer needed).

S3'->S4 heuristic (see `s4_tables.h`; the 406M-entry alternative is archived in
`../archive/cpp/variant_b_big_tables`):

    T1 = layer x ud                    20,160 x 2,520 =  50,803,200 entries (avg 12.60, max 17)
    T2 = (eq x pll x lr x fb) x ud      4,608 x 2,520 =  11,612,160 entries (avg 12.34, max 17)
    h  = max(T1, T2)                   tier-3 state = {layer 20,160, ud 2,520, rest 4,608}

The tables are stored symmetry-reduced (last section): the search looks up a (class of ud, relabeled b) in ~1.8 MB of nibble-packed tables (4 bits per entry, distances capped at 15), cached as
`s4_sym_layer_ud.bin` (1.27 MB) and `s4_sym_eqpllLRFB_ud.bin` (0.50 MB). A cache carries a fingerprint of the coordinate transitions, the goals and the
symmetry maps and is rebuilt automatically if they change (first run: ~5 s, a BFS over the full 62 MB tables that never leave memory). The older
`s4_layer_ud*.bin` / `s4_eqpllLRFB_ud*.bin` (full tables, 62 MB each pair) are no longer used and can be deleted.

## Usage
    solver --profile 30 --tier-stats --cost12 9 --cost23p 7 --cost34p 11 --cost45 9 --cost56 8 --cost67 2 \
           --penalty-threshold 7 --penalty-rate 1 --max-len 100          # the legacy S5 -> S6 -> S7 ending
    solver --ls --cost-ls 11 --profile 30                                 # the leave-slice ending (see its section)
    solver --help          # all options; unknown options are an error (they used to be ignored silently)
    solver                 # REPL: type a scramble (27 moves + x/y/z), get a verified solution
    solver --prefix "Rw' U' Fw" --suffix "R2 U'"   # force the solution to start / end with these moves
    solver --random-start --profile 30             # solve the NORMAL 4x4x4 (centers interchangeable inside a face)
    # in the REPL: `prefix U F`, `suffix R2`, and a bare `prefix` / `suffix` to clear

Startup runs the self-tests (about 1.5-4 s: they include exhaustive checks of the big tables); `--skip-selftest` disables them.

## Files (each header includes the previous one)
    common.h  phase1.h  phase2.h  coords.h  s3p.h  s4_coords.h  s4_tables.h  s4_sym.h  s5.h s6.h s7.h  lazy67.h
    state.h   raw.h     niss.h    ls.h  forced.h  search.h  random_start.h  phase2_endtable.h  s2switch.h  selftest.h  main.cpp
    tables*.h rotations.h symtab.h pairing_dist_io.h   -- generated data (see ../archive/README.md; symtab.h: archive/generate_sym_tables.py)
Module map is at the top of `main.cpp`. `search.h` is one templated `dfs_tier<K>` for all seven
tiers; `Tier<K>` traits in it (and the lazy state / step functions in `state.h`) are the only
per-tier code. To add a feature to a tier, touch its `Tier<K>` traits and lazy state; the recursion,
fork handling, drop credit, tier stats and path bookkeeping are shared.

## Verification recipe (how the refactor was checked)
The pre-refactor solver's per-trial nodes/lengths/tier stats/fork stats were reproduced exactly
(12 trials, seed 2024) by this code with a "legacy" `s4_tables.h` (WING4/CENTER4-equivalent
tables built with the same generic machinery). Do the same after any structural change: keep the
heuristic fixed, compare `--profile N --tier-stats` node counts trial by trial. Wall-clock times on
this machine are noisy (3x swings between identical runs), node counts are exact.

## Reference results (30 trials, seed 2024, --cost12 9 --cost23p 7 --cost45 9 --cost56 8 --cost67 2)
| COST3'4 | tables            | avg nodes | avg len |
|---------|-------------------|-----------|---------|
| 11      | legacy (old)      | 3,403,571 | 56.07   |
| 11      | current           | 2,500,278 | 56.07   |
| 10      | legacy (old)      | 5,086,942 | 55.10   |
| 10      | current           | 4,145,521 | 55.10   |
At COST3'4=10, S3' nodes drop 29.1M -> 0.83M (35x); tier 4->5 (S5, 76.8M nodes) now dominates.

## NISS (always on; `--no-niss` was removed with the normal S2 -> S3' route, the plain search lives in `archive/cpp/s3_route_normal_and_both/`)
NISS (`--niss` is still accepted, it is the default) lets the search invert the cube state for free (normal/inverse scramble switching): at the root
and whenever a move enters S1, S2, S4 or S5 (at most one switch per such move; a solution can contain many; with `--ls` the S5 entry does not switch,
`--ls-niss-all` restores it; the S2 -> S3' crossing is always a forced switch).
The search keeps ONE cube state and appends moves on its right; a switch replaces it by its inverse. With
N/I the move words played on the normal/inverse side, the answer is `N` followed by the inverse of `I`
(`A (B C) D` = play A, switch, B C, switch, D -> `A D C' B'`). `--profile ... --print-solutions` prints the
executed solution and this parenthesised shorthand. Details (all in `niss.h` / `search.h`):
- **Free moves.** A switch right after a quarter turn m entered the tier leaves a free move m^2 on the other
  side (the path ending in m^-1 enters the same tier). Only ONE orientation of m is allowed to switch (the
  other is reached by flipping), otherwise both nodes explore the same class. The free move is a `Pending`:
  it rides along (conjugated by every rotation fork) and is resolved into a {no flip, flip} branch when
  (a) the search switches back, (b) a wide free move (Rw2/Uw2/Fw2) reaches S2, or (c) a face-half-turn free
  move reaches S6 (the last tier; the S5->S6 fast path does this with lazy left-multiplication tables,
  `T6_LEFT_*`). Flipping a move = replacing that letter by its inverse in the final word.
- **Rotation forks** insert their rotation letter into BOTH words, so the closing rotation comes out of the
  inverse of I and no explicit close is emitted in NISS mode.
- **Not handled:** commuting trailing moves (e.g. `F' B` vs `F B`) still duplicate after a switch; about 7%
  of switch crossings have one (`NISS:` line of `--profile`).
- **Checks:** self-tests cover inversion / left multiplication / conjugation against raw simulation, the
  tier-6 left-multiplication tables, free-move invariance per tier, and solution assembly (the worked
  examples). `--check-replay` additionally compares every explored NISS branch (including the lazy tier-6
  flip branch and the pending free move) with a from-scratch replay of its path. `--profile` reports how many
  solutions reach the literal solved cube.

## Forced prefix / suffix (`--prefix`, `--suffix`, REPL `prefix` / `suffix`)
The solution can be forced to start with an algorithm P and end with an algorithm L (moves and x/y/z
rotations; the solution printed is then P, the searched part Z, L). The search solves the cube
**L S P** (L applied first, then the scramble S, then P): L S P Z = id, and rotating that identity
cyclically gives S P Z L = id, so P Z L solves S. (It is L, not L^-1: L^-1 S P Z = id only gives
S P Z L^-1 = id.) Z must not cancel with P or L: a move of Z may not merge into a move of P, or a move
of L into Z, even through moves in between that commute with it (P = "U F" forbids a Z starting with F,
F', F2, "B F", "B' F2", ...). `forced_cancels` (`forced.h`) checks that exactly, on the finished solution,
when the search reaches the solved cube; such a solution is just not accepted and the search goes on.
It compares cube elements, not letters, so it is also right across the whole-cube rotations in the
solution: moves are rewritten in the frame the solution started in, and two moves merge iff they turn
the same layers (any powers) with only commuting moves between them.

Pruning (otherwise slow): refusing only at the goal makes IDA* exhaust whole thresholds, and one
unlucky trial cost 1.4 billion nodes. So the existing "previous move" canonical-order pruning is reused:
the search pretends the previous move at the start of the normal scramble was the move of highest
rank in P's trailing same-axis block, and at the start of the inverse scramble (--niss; it reads the end
of the solution backwards) the same for L's leading block (`Forced::virtual_last`; rotations next to
those moves are pushed through exactly; it forbids slightly more than cancelling, costing +1 move in
1 of 100 scrambles). With --niss the two words each start with their own pretended move, so both ends are
covered whenever both words are non-empty; a normal-only solution's end (and an inverse-only solution's
start) is still only caught at the goal. NISS fork rotations go into BOTH words, so one that fires while
a word is still empty lands at the boundary of Z (Z ends / starts with a rotation) and changes which
layer the boundary move turns; the pretended move of a word with no move yet is therefore recomputed from
the rotation letters already in it (`refresh_virtual_wl`, called when a crossing switches or opens a
fork). Without that, such solutions slipped through the pruning and --niss with a suffix had trials of
30-70M nodes (thousands of refused solutions at one threshold). Measured (100 scrambles, prefix
"Rw' U' Fw", suffix "R2 U'"): the first solution found cancels in 29% (plain) / 19% (--niss) of scrambles;
avg nodes 0.85M plain / 0.61M niss vs 0.78M / 0.57M with cancelling accepted; worst trial 3.3M / 2.6M;
refused solved cubes 80 / 9 over 100 scrambles. On 40-scramble runs with awkward ends (rotations in P/L,
blocks like "B F") --niss averages 0.45-0.8M nodes, worst trial 3.3M.
The solution is ~|P|+|L| moves longer.
Checks: `--profile` with `--prefix/--suffix` (every solution verified to solve S literally, with and without
`--no-niss`), and without prefix/suffix the output is node-for-node identical to before.

## Phase 1 is an endtable over the center masks (`phase1.h`)
Whether a cube is d moves from S1 (mod whole-cube rotation) depends only on the unordered set of the three
24-bit center masks {ud, lr, fb} (a 1 where a UD / LR / FB center sits; they partition the 24 positions) and
on the wing parity. `p1::MaskState` is those masks + parity, a move permutes the bits (byte lookup tables),
and a BFS over canonical keys (min mask, max mask, parity) from S1 gives exact layers that are tiny:
distance 0..6 = 1, 3, 54, 717, 9042, 118,602, 1,539,456 states (1.67M). One cumulative blocked Bloom filter
per distance holds every state at distance <= k (all of them ~3.5 MB, built in ~0.3 s at start-up, measured
false positive rate 0.00104 for a 0.001 target; a false positive only makes a state look closer). Tier 0 of
the search is this table and nothing else: the heuristic is the exact distance (7 = "farther") plus the COSTs.
It replaced the 735,471-entry per-coordinate transition and distance tables (archived in
`../archive/cpp/phase1_coordinate_tables/`): same solutions, **-32% nodes and 1.9x less time** on 60 default
scrambles (avg 534k -> 361k nodes, 363 -> 191 ms in a first experiment), and `State6.s1` is just the masks.
Checks: self-tests compare the mask step with the masks of the real cube, the tier-0 lazy walk with
re-extraction, the rotation classes (their masks come from the rotation tables) and the endtable against an
independent DFS over cube arrays; `--check-replay` and `--no-niss` still pass.

## Randomized start (`--random-start`): solve the normal 4x4x4
For a normal cube only the visible state counts, so the search may start from any within-face permutation of
the centers. With C = L S P (L = forced suffix, P = forced prefix, S = scramble) the inverse scramble is
P^-1 S^-1 L^-1, and P^-1 S^-1 [centers permuted inside their faces] L^-1 is solved by W exactly iff
P W^-1 L solves S on the normal cube. The permutation can be moved behind L^-1, where it only permutes pieces
inside the sets U_i = L^-1(T_i) (T_i = the 4 center positions of face i, mapped by the move word L^-1). So the
start is X0 = P^-1 S^-1 L^-1 with arbitrary pieces swapped inside each U_i; the normal solver runs from the
inverse root only (NISS may still switch later) and its answer Z is already inverse(W); the solution is P Z L.
- **Only even permutations are reachable.** Every move has the same permutation sign on the centers as on the
  corners (sympy on the 72-point group: 3-cycles, double transpositions and one transposition in each of two
  faces are in the group, a lone transposition is not), so a candidate must be an even number of center
  transpositions away from X0. The walk (one transposition per step, the next transposition of a set shares
  a position with the previous one) steps through the odd states but never uses them.
- **Candidates.** The walk is done on the masks (one bit swap per step), candidates are deduplicated by their
  S1 coset (the three masks + parity) and ranked by the exact endtable distance; typically ~40k distinct
  cosets per 100k steps, the closest 5-6 moves from S1 (a normal scramble averages 9.4).
- **Passes.** for d = 0, 1, 2, ...: every candidate whose S1 distance D is <= d is searched with IDA* at
  threshold h0 + (d - D) (smallest d first, so shorter solutions come first; the per-candidate loop of the
  first sketch would let an early D = 4 candidate win over a later D = 1 one). After `--rs-max-d` passes
  (default 8) the ordinary supercube search runs, which is also a valid solution of the normal cube.
- **Checks.** `is_visibly_solved` (a normal-cube model: centers only need to be on a position of their own
  face) verifies every solution; self-tests cover the candidates (even, inside their sets, distinct) and the
  visible-solved predicate. Works with `--prefix/--suffix` (the sets depend on the suffix).
Knobs (`--random-start --profile 40`, seed 2024; length / time / fallbacks to the ordinary search):

| setting                                                                   | avg length | avg time | fell back to the ordinary search |
|---------------------------------------------------------------------------|-----------:|---------:|---------------------------------:|
| plain search (no `--random-start`), same 40 scrambles                     | 57.13      | 0.25 s   |                                  |
| `--rs-trials 100000`                                                      | 55.33      | 0.27 s   | 8/40                             |
| default: 1M walk steps, exact S1 depth 6, `--rs-max-d 8`                  | 54.30      | 0.61 s   | 5/40                             |
| + `--s1-depth 7` (layer 7 = 19.4M states, 49 MB, +1.1 s start-up)       | 53.77      | 0.51 s   | 1/40                             |
| + `--rs-max-d 10`                                                         | 53.75      | 0.46 s   | 0/40                             |
| `--rs-trials 5000000 --s1-depth 7 --rs-max-d 10`                          | 53.85      | 1.24 s   | 0/40                             |
Depth 7 builds in ~1.1 s: layers up to 6 are sorted/deduplicated (radix sort); the last layer is never expanded, so its raw
neighbour keys go straight into a 64-byte-aligned blocked filter with prefetched inserts (no sort, size estimated from the
previous layer's duplicate ratio; first version: 6 s, of which the sort was 4 s and unprefetched inserts 3.7 s).
Depth 7 and more steps give more candidates at distance 4-5; 5M steps costs far more walk time for no gain.

## The wing coset is a 24-bit mask, not a ranked index (`phase2.h`)
Phase 2's wing coordinate (which 12 of the 24 wing slots hold the "positive" wings, C(24,12) = 2,704,156 states)
used to be a combinatorial rank with a 292 MB move-transition table (2.70M x 27 x 4 B, built by a BFS at every
start-up: 5-6 s). It is now the 24-bit mask itself (bit s = 1 iff slot s holds a positive wing). A move permutes
the bits with one lookup per mask byte (`WING_STEP[move][byte][value]`, 83 KB), so there is no transition table
at all. The distance table is indexed by the low 23 bits: the mask has exactly 12 ones, so the number of ones in
the low 23 bits says what bit 23 was and the index is unique (2^23 bytes for 2.70M states, 3.1x sparse, unused
entries 255; the self-tests check uniqueness over all 2.70M masks). Same heuristic, same solutions, identical node
counts; phase 1/2 start-up 6.2 s -> 0.4 s (the whole table build is now ~3.3 s), the tables are ~280 MB smaller, and
search throughput rose slightly because the transition lookup no longer misses the cache.
Distribution of the wing coset distance (all 27 moves; 0..8, avg 6.0772, none unreached):
`0:1 1:5 2:89 3:1441 4:22955 5:310270 6:1799686 7:569705 8:4`.

## Phase 2 joint endtable (`phase2_endtable.h`, `--p2-depth N`, default 5)
The wing distance table and the F/B-center distance table each see half of the phase-2 coordinate, so
`max(wing, fb)` is exact only up to distance 3 (at distance 6, 46% of the states are underestimated). The joint
endtable is the exact distance to S2 of the pair (wing mask, F/B permutation) over the 21 phase-2 moves, as
cumulative blocked Bloom filters like phase 1 (key = wing mask + F/B rank): layers 0..6 = 96, 192, 2784, 28032,
234816, 2060160, 19384800 states (layer 7 would be ~180M). Depth 5 = 5.7 MB / ~0.15 s to build (default), depth 6 =
52 MB / ~1.2 s. The heuristic is max(endtable, wing table, F/B table); the filter is only probed when that max is
<= depth (it cannot raise it further). `--p2-depth 0` turns it off, `--p2-fpr` sets the false positive rate,
`--p2-tables wing,fb|wing|fb|none` chooses which of the old tables run next to it (to measure what the endtable
achieves alone). Self-tests: no false negatives against random walks from S2, exactness up to distance 4 against an
independent DFS, the layer sizes, false positive rate.
Ablation (60 scrambles, seed 5, `--profile 60`; nodes are exact, times noisy, 2 rounds averaged):

| configuration                                   | nodes (avg) | vs old   | solve ms (avg of 2 rounds) | endtable        |
|-------------------------------------------------|-------------|----------|----------------------------|-----------------|
| old: wing + F/B tables (`--p2-depth 0`)         | 471,081     | -        | 321                        | -               |
| depth 5, endtable + wing + F/B (**default**)    | 296,704     | -37%     | 257                        | 5.7 MB, 0.13 s  |
| depth 5, endtable only (`--p2-tables none`)     | 1,001,690   | +113%    | 748                        |                 |
| depth 5, endtable + F/B table                   | 324,989     | -31%     | 310                        |                 |
| depth 5, endtable + wing table                  | 1,000,659   | +112%    | 863                        |                 |
| depth 6, endtable + wing + F/B                  | 198,290     | -58%     | 230                        | 52 MB, 1.1-1.6 s|
| depth 6, endtable only                          | 252,478     | -46%     | 328                        |                 |
| depth 6, endtable + F/B table                   | 198,304     | -58%     | 220                        |                 |
| depth 6, endtable + wing table                  | 252,377     | -46%     | 347                        |                 |

All 60 scrambles solved in every configuration; average length 57.27-57.33 throughout.
At depth 6 the wing table adds nothing (198,304 vs 198,290 nodes); the F/B table does (252k without it).

## Reaching S3' by switching (`s2switch.h`) -- the only route to S3'
S3' is not closed under inverses, so the NISS search could not switch on landing in S3'. The way around it: from
an S2 state X, the INVERSE of X is in S3' iff two small conditions hold on X itself, and then X must be switched to
reach S3' (the search goes on from the inverted cube, in tier 3):
- **L/R centers**: solvable with [R or nothing][L or nothing][180-degree turns], i.e. X's L/R arrangement is (half
  turns) then (R^a L^b): 96 x 4 = 384 of the 8! arrangements. (The F/B part is automatic: CENTER_GOOD is a product of
  24 L/R and 24 F/B classes and every S2 state has an F/B arrangement whose inverse is good.) Table: the arrangement
  itself (40320 entries, no 2520-reduction), a plain transition table (40320 x 17) and a multi-source BFS distance from
  the 384 goal arrangements (avg 6.49, max 10).
- **Wings**: the four wing pairs (edges) belonging in the equator slice are paired amongst each other and either solved
  or two 2-cycles. With A_e = the edge position holding the positive wing of equator edge e and B_e = the position of its
  negative wing: A_e = B_v(e) for all four e, v the identity or a double transposition (a single 2-cycle does NOT
  qualify; such states occur, ~0.6% of random S2 states, and are not "inverse in S3'"). A raw state is (A, B),
  (12*11*10*9)^2 = 141M; the condition is invariant under relabeling the four edges (S4, jointly for A and B) and
  under relabeling only the B's by the Klein four-group, so there are 141M / 96 = C(12,4)^2 * 6 = 1,470,150 classes. The
  index: the position sets of A and of B (two 4096-entry rank tables -> 495 each) and the relative order of the B's in
  the order of the sorted A's, modulo V4 (6 cosets, a 256-entry lookup keyed by the 4 ranks). It is computed from the 8
  positions with 24 comparisons and 3 lookups, no popcount and no transition table (a move maps the 8 positions through
  12-entry tables). BFS from the 495 goal classes: avg 5.23, max 7.
Both conditions were checked against the real test ("invert the cube, is it in S3'?") on 400k random S2 states
(326k positives), 0 mismatches; the self-tests redo it (30000 states), check the lazy step, the S4 x V4 invariance of the
index (it commutes with moves), distances against random walks and a DFS (exact up to 5) and the goal set sizes.
Search: tier code 7 = tier 2 with this state (`Tier2AltState`: L/R arrangement, U/D class for the center penalty, 8
wing positions) and tier 2's flat costs; its crossing is only taken together with a switch (`build_niss_variants`,
`force_sw`), so the crossing goes through the same flip/pending machinery as any NISS switch (`--check-replay`:
0 mismatches over tens of thousands of branches). Routes: `normal` (tier-2 search, the default), `switch` (only tier 7
from an S2 entry) and `both` (a fork at each S2 entry: normal first, then switch, same threshold). A root that is
already in S2 searches the switch route too. All start-up cost: 0.18 s, 5 MB.
Profile (100 scrambles, seed 2024, NISS; nodes and lengths are exact, times are the average of the per-run averages of 5
alternating rounds, which vary 15-30% on this machine):

| route  | nodes (avg) | solve ms | length | S2 entries won by normal / switch |
|--------|-------------|----------|--------|-----------------------------------|
| normal | 282,297     | 175      | 57.37  | 100 / 0                           |
| switch | 318,221 (+13%) | 113 (-36%) | 57.53 | 0 / 100                        |
| both   | 298,835 (+6%)  | 142 (-19%) | 57.41 | 69 / 31                        |

The switch route needs more nodes in S2 (56k vs 35k per scramble: its heuristic is a little weaker) but each is much
cheaper (no wing-pairing rank per node), so it is faster overall; `both` searches both subtrees under the same
threshold and wins in between.

### Main now supports only the switch route
The normal route (tier-2 search by moves alone, `--s3-route normal|both`, `--no-niss`) needed the 120 MB
`s3prime_wing_dist.bin` (distance of every wing pairing to S3') plus a center table; it is archived with everything else
(sources, README, that .bin) in `archive/cpp/s3_route_normal_and_both/`. Main has tier 2 = the switch route (`Tier<2>` is
`s2sw::Tier2AltState`; tier code 7 and the route flags are gone), needs NISS (S3' is only reached by a switch), and keeps the
S3'-membership tests (`WING_GOOD`, 30 MB built at start-up, `CENTER_GOOD`) for classifying states. Same solutions as the
earlier `--s3-route switch` (100 scrambles, seed 2024: 318,221 nodes avg, length 57.53, 100/100 literal-solved,
0 replay mismatches); tables then totalled ~99 MB, of which the S3'->S4 pair tables were 62 MB (shrunk by symmetry, see the last section).
Reference for the choice (archive build, 100 scrambles): normal 282k nodes / 175 ms / 57.37 moves, switch 318k / 113 ms /
57.53, both 299k / 142 ms / 57.41.

## S4 up to a whole-cube half turn (`state.h`: `s4_k_class`, `init_s4_goals`; `s4_tables.h`: `tier3_s4_class`)
S1 is defined up to whole-cube rotations (six classes); S4 has the same freedom left over: after S1 the only rotations that keep
the cube in S1 are the Klein four-group {id, x2, y2, z2}, and a state that is S4 after one of them is as good as a literal S4 one.
Example: `Rw2 U2 D2 F2 B2 Rw2` is only in S3' (ud 2218, fb 16), but followed by `x2` it is in S4, S5 and even S6. So the S3'->S4
crossing now accepts four goals: the literal S4 and its images under x2, y2, z2 (signatures (ud, lr, fb) = (0,0,0), (2218,0,16),
(0,2306,16), (2218,2306,0), all with the wing pairing solved). On crossing into an image the state is turned back by the half turn
(`rotate_close_state6`, a free rotation letter of rotation class 6/7/8 = x2/y2/z2 in the path, printed `[x2]` etc., replayed by
`reconstruct_full_state`, handled like the S1 rotation letters in NISS words, forced-end checks and verification); tier 4 onwards
only ever sees literal S4. A root scramble that is itself an S4 image is canonicalized in `extract6`/`invert_state6`
(`canonicalize_if_rotated(.., with_k)`; `apply_move` leaves it off so that the search path records the letter).
The S3'->S4 pair tables are now the distance to the NEAREST of the four goals (multi-source BFS, `Goals` in `s4_coords.h`; the cache
key includes the goal set; the files then were `*_modk.bin`, the old ones were distances to the literal S4 and would silently mislead):
a lower bound of the old tables, so still admissible, and exactly the distance to the real goal now. In T1 only 0.15% of the entries
drop (mean 0.0016), in T2 23% (mean 0.30).
Effect (200 scrambles, `--profile 200 --seed 21`): nodes avg 341,832 -> 266,381 (-22%), median 260k -> 203k, time about -30%,
length 57.50 -> 57.56 (noise), S3' entries per scramble 1449 -> 605, all 200 solutions replay to the literal solved cube
(`verification` now replays the raw cube, not a canonicalized copy), `--check-replay` 13,111 NISS branches 0 mismatches, forced
prefix/suffix and random start unchanged. Self-tests: `test_s4_images` (the three images are recognized by the full-cube test and by the
lazy coordinates, have distance 0 in both tables, and `extract6` turns them back to exactly the literal state).
Known limit: a root that is both an S1 rotation class (x, y, z, ...) and an S4 image is only canonicalized for the S1 class (one rotation
letter per root); the search still solves it, only through the longer literal S4.
This is also the right goal for the planned symmetry reduction of the pair tables (the 16 symmetries are exact on these tables).

## S3'->S4 tables reduced by symmetry (`s4_sym.h`, `symtab.h`): 62 MB -> 1.8 MB
Because S4 is defined up to the half turns, T1 and T2 are invariant under symmetries of the coordinates (every one verified on every entry):
- **the 16 cube symmetries** that keep y (U/D) and keep or swap x and z (the sign flips of the axes: identity, x2, y2, z2 and the four axis-aligned
  reflections, and the same with x and z swapped; `symtab.h`, generated by `archive/generate_sym_tables.py`), acting by conjugation (pieces and slots
  relabeled alike). They map the move set onto itself only up to whole-cube half turns (a wide move goes to the opposite-side wide move, i.e. the wide
  move followed by a half turn), which the "S4 up to a half turn" goals absorb: before that change they were not symmetries of the tables (T2 equal on only
  ~65% of the states), after it they are exact;
- **the left multiplication by x2, y2, z2**, which permutes the four goals. It is total on the layer and ud coordinates but can leave the 24 lr / fb classes
  that the moves reach, so it acts on T1 only. T1's group has 32 elements, T2's 16.
The symmetries act on each coordinate separately (ud 2520, layer 20160, rest = eq,pll,lr,fb 4608). They are not derived by hand: a coordinate value is realized
as real pieces (for lr/fb a real arrangement the moves reach, a BFS finds one per class pair: an arbitrary representative of the class can be sent outside the
domain by an axis-swapping symmetry), the symmetry is applied, the coordinates are read back, and the group is the closure of these maps.
The ud coordinate falls into **126 classes** under T1's group and **215** under T2's. The reduced table of class c is the full table's column of the class
representative (`R.table[c * nb + b]`), a lookup canonicalizes (b, ud) to (class, h(b)) with h the element mapping ud to its representative: 2,540,160 + 990,720
entries instead of 50.8M + 11.6M, nibble-packed (cap 15) in cache files of 1.27 + 0.50 MB.
The cap only touches the far states: the full tables go up to 17 (T1) / 16 (T2), 10.3% / 10.9% of the reduced entries sit at the cap (true value >= 15;
only the 16s and 17s actually lose anything). A capped value is still an admissible lower bound and still 1-Lipschitz, and it changed nothing measurable: 200 scrambles, seed 21,
266,381 average nodes and 57.56 average length, node for node the same as with the byte tables (distances above ~12 are pruned anyway at the
thresholds the search runs).
- **They are not graph automorphisms**, so the reduced table cannot come from a BFS inside the reduced space (that was tried first: T1 happened to come out
  exact, T2 left 11% of the entries unreached): a wide move is sent to "that move plus a half turn", which is no generator; only the distance to the goal SET is
  invariant. So the reduced tables are cut out of the full table, which a multi-source BFS from the four goals builds in ~5 s on the first run (and in
  `--check-s4-sym`, which compares every one of the 62.4M entries with a fresh full BFS: 0 differ).
- Effect: node-for-node identical searches (the distances are identical), same speed (tier 3 is ~1% of the nodes; the tables now fit in the cache), S3'->S4
  tables 62 MB -> 3.2 MB in memory (the maps of the group elements are 1.5 MB of it) and 1.8 MB on disk, +0.3 s start-up for building the group maps. Self-test:
  `test_s4_sym` checks the reduced tables as a distance field on random states of the full product space (goal <=> 0, neighbours differ by <= 1, every non-goal has a
  neighbour one closer) and their invariance under random group elements.

## Leave slice: S5 -> LS -> solved (`--ls`, `ls.h`)
Opt-in ending that replaces S5 -> S6 -> S7 (the default pipeline is unchanged, node for node).
`LS = <U'D, R2 U'D B2 U D', B2 U'D L2 U D', L2 U'D F2 U D', F2 U'D R2 U D'>`, a group of order 384 inside S5 = <U,D,R2,L2,F2,B2>
(verified: closure of the generators on the 96-sticker model has 384 elements; index in S5 = 8! * 8! * 16 / 4 / 2).
U'D turns the two outer layers the same way; the other generators act on the middle slice only. A state of S5 splits into
- the **UD part**: corners, the 8 U/D-layer dedges, the U and D centers, and
- the **E part**: the 4 equatorial dedges (8 wings) and the 16 L/R/F/B centers (a group of 768 elements once the E-slice turn is added),
and it is in LS iff its UD part is the same power of U'D away from solved for corners, edges and centers.

**Phase A (S5 -> LS)**, tier 5 of the search, on the UD part only with U, U', U2, R2, L2, F2, B2 (no D: up to the E slice and a
whole-cube rotation D = U y'). Lazy state = corner rank (8!), dedge rank (8!), U and D center turns. The state is canonicalized
by relabeling the pieces by (U'D)^j so that the U center is solved (a LEFT multiplication, which commutes with the moves appended on
the right; a right multiplication would not, the distance is not invariant under it). Heuristic = max(Bloom endtable, corner table):
the endtable is exact BFS layers around the canonical solved state in cumulative blocked Bloom filters, cut where the total is closest to
`--ls-entries` (default 1M: depth 8, 465,422 entries, 1.16 MB; `--ls-depth 9` = 2.07M entries), the corner table is the distance (max 13) to the
nearest power of U'D over the 40,320 corner permutations (40 KB).

**Phase B (LS -> solved)**, the terminal tier: the word X found by phase A is REPLACED by an equivalent word Y of at most
`threshold - g_at_S5_entry` letters (the depth phase A ran at). The letters of Y are R2 L2 F2 B2, U U' U2 and the widened U turns
(D y) (D' y') (D2 y2), i.e. a turn of the top three layers: it acts on the UD part exactly like U but also turns the E slice, so
the E part can be fixed without disturbing what X solved. A stack holds the inverse of X; every letter is pushed with adjacent
cancellations ((D y) counts as U) and the heuristic is the stack size; at an empty stack the UD part is solved up to y^k, the
y rotations of Y are summed and a closing rotation makes their sum 0, the cube is solved iff the E part (looked up in a 768-element
Cayley table) is solved and sum(y) == the U center turns left by X. It never looks at the cube. Previous-move guard as in every
other phase: no repeated face letter, R2/L2 and F2/B2 in a fixed order, and on the U axis only "plain U, then widened" (never the
reverse: they commute). Rotations are close sentinels (class 3 / class 7) in the current word, so NISS keeps working (they are
inverted with the word); a pending NISS FLIP token stays after Y; forced prefix/suffix works (`goal_accepted` on the replaced path).

**Cost.** `--cost-ls N` (default 11; 13 was the first guess) is the flat gap stacked on tier 4 and replaces COST56 + COST67 (set to N and 0 in LS mode).
60 scrambles, seed 21, default costs otherwise (baseline: 57.60 moves, 254,758 nodes; node counts are exact, wall-clock on this
machine swings 3x between identical runs so it is not quoted):

| --cost-ls | avg length | avg nodes |
|-----------|-----------|-----------|
| 15        | 59.20     | 164,715   |
| 14        | 58.47     | 165,529   |
| 13        | 57.63     | 168,400   |
| 12        | 56.62     | 206,086   |
| 11        | 55.62     | 264,890   |
| 10        | 54.72     | 367,288   |
| 9         | 53.97     | 1,009,251 |
| 8         | (killed after 240 s) |   |

At cost 13 it is the same length as the baseline with 34% fewer nodes; at 12 one move shorter with 19% fewer; at 11 two moves shorter
for about the same nodes. LS nodes cost more each (the S5 nodes of the default pipeline are the cheapest ones in the solver), but
there are 3.5x fewer of them (S5 tier: 6.3M -> 1.8M nodes over 40 trials) and the S0-S4 nodes shrink a little too.
Phase B is ~200 nested nodes per search and ~3% of all nodes.

**Checks** (all run at start-up with `--ls`, `ls::g_built`): generator words are members / random S5 words and single moves are not,
lazy transitions == re-extraction from the full cube (12,000 steps), endtable+corner heuristic admissible by witness (600), and
short S5 cases (`U D`, `U' D`, `R2 U R2`, `R2 U' D R2`, `R2 U' D B2`, `U R2 U2 F2 U`, ..., plus 40 random ones) solved by the LS
pipeline alone, replayed literally and equal to the optimum of a plain IDDFS (52/52). A Python model of the same pipeline
(scratch work, not kept) matched BFS optimum on all tested cases. Random S5 states: 12/12 solved in 14-17 moves (milliseconds).
End to end: 200/200 literal solves at cost 12, `--check-replay` 10,899 NISS branches 0 mismatches, forced prefix/suffix 40/40.

**Follow-ups (2026-10-04).** Defaults are now `--cost-ls 11`, depth-8 endtable, no NISS switch on the move that enters S5 (earlier
boundaries and the root keep theirs; `--ls-niss-all` restores the S5 switch). 100 scrambles, seed 21:

| config | avg length | avg nodes |
|--------|-----------|-----------|
| cost 11, depth 8, no S5 switch (default) | 55.60 | 249,110 |
| cost 11, depth 8, S5 switch (before)     | 55.66 | 275,977 |
| cost 11, depth 9, no S5 switch           | 55.60 | 213,123 |
| cost 11, depth 9, S5 switch              | 55.66 | 221,467 |
| cost 10, depth 8, no S5 switch           | 54.67 | 393,032 |
| cost 12, depth 8, no S5 switch           | 56.64 | 188,546 |

- **Endtable size** (`--ls-depth N`, or `--ls-entries N` = the depth whose total is closest to N): depth 8 = 465,422 entries, 1.16 MB,
  ~0.17 s to build; depth 9 = 2,069,950 entries, 5.17 MB, ~0.40 s (about +0.23 s of start-up; 14% fewer nodes); depth 10 = 9.06M entries,
  22.7 MB, ~1.45 s. Depth 9 is the better trade-off if 5 MB is acceptable.
- **Phase A transition tables.** Phase A always had corner and dedge transition tables (`TRC`, `TRE`: 40,320 x 10 each; the centers are
  two additions). The canonicalization by (U'D)^j used to cost two more lookups per node; it is now fused into the tables
  (`TRCc`, `TREc`, canonical states {corner, dedge, 0, u+d} all the way through the search): 27-29 ns per node instead of 34-43 ns
  in a micro-benchmark (apply + membership + heuristic). A centers table is not worth it (an add).

**Endtable lookups (`--ls-lookup`).** The filters were already blocked Bloom filters (`p1::BlockedBloom`: all probes of a key in one 64-byte
block). What made a HIT expensive was the layer search after it: the cumulative filters are separate arrays, so a binary search cost up to 4
more cache lines (a miss stops at the first probe). The search only needs to know whether h <= r, r = the budget left for the child, so the
default `bloom-budget` makes ONE probe, in the cumulative filter of distance <= r (hit: expand, miss: prune), and none at all when r exceeds
the endtable depth (the table can only say "farther than depth" then, never above r). tier 5 never uses the exact h otherwise (the drop
credit is 0 there), so the search is node for node the same (321,078 vs 321,077 nodes). Phase A standalone, best of 8 rounds, ns per node
(apply + membership + heuristic, 150 random S5 states): depth 9: binary 95.0 / linear walk 104.3 / budget 77.3; depth 8: 88.8 / 89.2 / 79.1.
`fingerprint` (one table of 12-bit fingerprint + 4-bit distance entries in 64-byte buckets, overflow in a hash map; 6.2 MB at depth 9) is
slower (218 / 143 ns) and has a 0.54% false positive rate (Bloom 0.13%): kept as an option only. In a whole solve phase A is only a few % of
the time (S5 tier nodes cost ~0.1 us, S1/S2 nodes ~1 us and dominate), so depth 9 vs 8 shows up in node counts (-8..-14%) but not in wall-clock.

## Where the time goes, and checkpointed state rebuilds (2026-10-04)
Profile of a 40-scramble run (`--ls --cost-ls 10 --ls-depth 9`, NISS on, 16.6M nodes; a spinning sampler thread attributed wall-clock to what
the search was doing, so the percentages are approximate): S1 child steps 34%, S2 child steps 27%, S0/S3'/S4/S5/terminal ~11% (LS phase A 2%,
phase B 0.2%), and the TIER CROSSINGS ~28%: rebuilding the full cube by replaying the whole path from the root 17%, the NISS variants 7%, the
full-state heuristic of the child 4%. Per child step (replayed recorded node sequences): S1 ~5 ns apply + ~21 ns heuristic (the joint
endtable ~10 of it), S2 ~2.6 ns apply + ~16 ns heuristic (the wing class index ~7.6, penalty `ceil` ~5.6).
- **Checkpoints** (`search.h`: `g_ck`, `reconstruct_g_path`): every crossing knows the full state its path leads to (the child it is about to
  search), so `try_variant` pushes (path length, state, live pending free move) for the duration of that subtree and a later crossing replays
  only the tokens since the innermost checkpoint instead of the whole path. Node for node identical (266,381 / 57.56 on the default 200
  scrambles); `--check-replay` also compares every checkpointed rebuild with the from-the-root replay (71k compared, 0 mismatches).
  `--no-checkpoints` turns it off for A/B timing: in-process, alternating, paired per round it was faster in 7 of 8 rounds (about 10-30%,
  median ratio 0.88; the machine's speed state changes mid-run, so only paired comparisons mean anything).
- **S1 endtable as one budgeted probe (tried, not kept).** Computing the cheap tables and the penalty first leaves "is h1 <= r1?" for the
  endtable, one probe at layer r1; only a tight / not-tight ambiguity (needed for `chain_first`) needs a second probe, and that happened for
  851 of 88.7M children. Node counts identical, wall-clock within noise (best of 4: 10.37 s vs 10.31 s): 92% of children are already decided
  by the cheap tables, so it is not worth the code.

**Penalty arithmetic and the S2 wing index (2026-10-04).** All node counts are unchanged (266,381 default, 321,078 on the random-start line).
- **Integer penalty.** `--penalty-rate` must now be a whole number (1 is the tuned value; fractions are rejected). The early center penalty is a
  table lookup, `coords::PEN_UD[ud2520]` / `PEN_LR[lr2520]` (built with the final threshold after the distance tables), and h = h1 + the integer
  costs + penalties; the double sum with `std::ceil` is gone (it cost ~5.6 ns per S2 child and ~2.3 ns per S1 child).
- **S2 wing index.** The class index of the 4+4 wing positions (`s2sw::wing_index`) takes the rank of a position inside its set from a table
  (`BELOW495`, 6 KB) instead of 4 compares each (about -25% on the index). The original is kept as `wing_index_ref` (a self-test checks that
  they agree, 60,000 cases).
- **Budget shortcut.** `s2sw::at_goal_else_h_budget`: the search sets `s2sw::g_r` = the child's budget (threshold - g - 1) before stepping it; when
  the L/R table alone already puts h above it the child is pruned without computing the wing index (a goal is never pruned). On recorded
  real S2 children that decides 63.1% of them (the wing index is needed for 36.9%; the wing table's max distance is 7, so "no wing needed
  because the budget is large" never happens).
- **Timing.** In-process, alternating, paired per round (`--no-s2-fast` = reference index and no shortcut): the new code was faster in all 8 rounds,
  median ratio 0.90, best 6.4 s vs 7.1 s for 40 solves on the random-start configuration (the penalty change is in both columns).

## Start-up cost (`--startup-times`)
`--startup-times` prints the time of every table-building step. Before the S5 change (total 4.7 s on this machine): `build_s5_dist` 1,183 ms,
`p2j::build` (phase 2 joint endtable, depth 5) 854, `s2sw::build` (S2 switch tables) 793, `init_niss_tables` 249, `build_s7_dist` 215,
`p1::build` 205, `s4sym::build` (cache load + group maps) 209, wing coset distance table 233; everything else < 40 ms each.
- **`build_s5_dist` 1,183 -> 215 ms.** It is a BFS over the 4,330,260 (corner twist x L/R x equatorial occupancy) states; it decoded the twist rank
  and called `corner_ori_trans` for every one of the 14 moves of every state (60M slow calls, plus a `std::queue`). It is now a level-synchronous BFS
  over the precomputed `CORNER_ORI_TRANS` table. Same table (avg 8.3985, max 11); `test_s5_dist` checks it on every state against the distance-field
  characterization (zero only at the goal, neighbours differ by <= 1, every other state has a neighbour one closer): 4,330,260/4,330,260.
- **Bloom filter parameters (`BlockedBloom::init`).** Choosing bits-per-element and k searched with `fpr_of` (a 400-term sum with `pow`) every time,
  ~14 ms per filter, and every table makes 6-10 filters with the same target false-positive rate. The answer is now remembered per target rate.
  This took ~85 ms off `p2j::build` and about as much off `p1::build` and the LS endtable.
- **`p2j::build`** (phase 2 joint endtable): 215 -> ~128 ms in the same machine state (laps: exact layers 17 ms, filters allocated 8 ms, exact layers
  inserted 13 ms, last layer streamed 81 ms). `--startup-times` prints these laps (`LapTimer` in `common.h`).
- **`s2sw::build`** (S2 switch tables): 274 -> ~145 ms. The wing class BFS (1,470,150 classes x 17 moves) no longer expands its last two layers
  (distances end at 7, a fixed fact of the table: layers 0..5 are expanded, what they reach is layer 6, everything left over is 7). `test_s2_wing_dist`
  checks the whole table against the distance-field characterization (all classes x 17 moves): 1,470,150/1,470,150. (An off-by-one in the first version
  of the loop bound was caught by exactly that test.)
- (The items that were still open here, the wing coset table and its printout, `s4sym::build`, `init_niss_tables`, `build_s7_dist`, are covered by part 3 below;
  `init_niss_tables` and `build_s7_dist` are skipped altogether with `--ls`.) Timings on this machine drift by 2-3x between runs; compare steps within one run.

**Start-up part 3 (2026-10-04): LS mode builds only what it uses, and more steps are faster.** All search results are unchanged (default 266,381
nodes / 57.56; LS runs identical node for node up to Bloom false positives where a filter is sized by an estimate).
- **`--ls` skips the S6/S7 machinery** (`g_legacy_tail == false`): the corner coset / S6 / S7 distance tables, the M/S-slice and composite
  coordinates, the H and EC transitions and the T6 left-multiplication tables of `init_niss_tables` are not generated (~14 MB and several
  hundred ms). `is_in_s7` is then the literal identity of the arrays (what S7 means), `classify_tier` / `heuristic` use the LS tiers, and the
  self-tests of the S6/S7 machinery are skipped. Without `--ls` nothing changes.
- **Wing coset table** (`p2full::build_wing_distance_table`): ~230-360 -> ~100 ms. Layers 0..5 top-down, layer 7 BOTTOM-UP (the ~570k still unlabeled
  cosets look for a neighbour in layer 6 instead of expanding the 1.8M cosets of layer 6), the 4 left over are layer 8 (the maximum is fixed). The histogram
  is counted on the way, so `print_wing_dist_stats` costs nothing (it was a 172 ms scan). `test_wing_dist_field` checks all 2,704,156 cosets x 27 moves.
- **S5 table:** 205 -> ~114 ms (layers 0..7 top-down, 9 and 10 bottom-up, 338 left over = 11). **S2-switch wing classes:** 125 -> ~80 ms (layer 6 bottom-up,
  the rest is 7). Both are covered by the exhaustive distance-field tests.
- **S3'->S4 symmetry setup** (`s4sym::build`): 198-245 -> ~95 ms. Each generator map costs ~4 ms (a full pass over 22,680 values), and 19 / 16 were built; now a
  generating subset is picked on the 24-slot permutations (`pick_generators`, a handful per table). The cache fingerprint covers the symmetry definitions and
  the classes instead of the element-order-dependent maps, so the cache files were regenerated once (same sizes, same tables; `--check-s4-sym`: 0 of 62.4M differ).
- **LS endtable** (`ls::build`): 281 -> ~95 ms with `--ls-depth` (radix sort; the last layer is streamed into its filter with an estimated size, like p2j) and ~140 ms
  with the default size rule (it needs the exact size of the next layer to decide).
- **`p1::build`** with `--s1-depth 7`: the last layer is streamed in chunks (no 280 MB vector of raw keys) and `BlockedBloom::add_all` prefetches 64 keys deep (was 16):
  ~1.6 s -> ~0.9-1.0 s. (35M random Bloom insertions are what is left; depth 6, the default, takes ~0.25 s.)
- **Totals** (`all tables ready`, same machine state, 3 runs): `--ls` defaults ~1.4 s; legacy S6/S7 ending ~1.6-1.9 s; `--ls --ls-depth 9 --s1-depth 7` ~2.6-3.2 s (p1 ~1 s of it).
