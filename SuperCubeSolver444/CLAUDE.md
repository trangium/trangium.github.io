# 4x4x4 Supercube Solver — Project Notes

This file is the persistent memory for this project: what it is, what's
built, what was tried and rejected, and what's next. Read this before
doing anything else in this folder.

## Goal

A 4x4x4 **supercube** solver: like a normal 4x4x4, but every center piece
is distinguishable (not just by which face it's on — same-face centers are
distinguishable too), so the state space is vastly larger than a normal
4x4x4. Constraints (from the original brief):

- Runs in-browser (eventually WASM), can use up to ~4GB RAM (ideally
  ≤500MB), should solve in ~5s average case.
- Solutions should be as short as possible under those constraints — not
  necessarily optimal, but not padded either.
- Center **orientation** (in-place rotation) does *not* matter — only
  which of the 24 slots each center piece occupies. Confirmed explicitly
  by the user early on.
- Wings (edges) are treated normally (twin pairs interchangeable, like a
  regular 4x4x4) — only centers are made fully distinguishable.

## Key references

- Jaap's puzzle theory page (redirects to "Computer Puzzling" on
  jaapsch.net): indexing, pruning tables, transition tables,
  Thistlethwaite's algorithm, Kociemba's algorithm, multi-phase search.
  General techniques, not 4x4x4-specific.
- https://github.com/cs0x7f/TPR-4x4x4-Solver — Java 4x4x4 solver (Tsai's
  8-step method compressed to 3 phases). Doesn't handle supercubes.
- https://github.com/cs0x7f/min2phase (and the min2phase.js wrapper) —
  Herbert Kociemba's two-phase algorithm, Chen Shuang's optimized
  implementation. Read `Search.java` in full to understand the classic
  phase-handoff design (see "Unified vs sequential" section below).

## CURRENT CODE LAYOUT (2026-09-29 refactor -- read this first)

The solver was rewritten from the 6000-line `chain1234prime.cpp` family into a modular
single-translation-unit codebase that now lives directly in `cpp/` (README.md there has build,
options, module map and reference numbers). Two S3'->S4 table candidates were built and
compared; **the "split" tables won and are the main solver**: T1 = layer x ud (50.8M) +
T2 = (eq,pll,lr,fb) x ud (11.6M) (`cpp/s4_tables.h`). The 406M-entry alternative
(T1 = (layer,eq,pll) x ud) cut nodes by <1% and was slower per node (cache misses); it is kept
in `archive/cpp/variant_b_big_tables/`. Everything superseded -- the old `chain1234prime.cpp`
(only had lazy tiers 0-1), the phase-1a/1b and soft-min/move-lock experiments, the table
explorer -- is under `archive/cpp/`.

What the refactor did:
- Kept only the ida-inadm search. Removed vanilla IDA*, RBFS, staged beam, dfs_inadmissible
  (unreachable once every tier has a lazy recursion), `--bench`, `--search`, `--weight`,
  `--no-rotation`, `--progress`, `--verify-heuristic`, microbenchmarks. Unknown CLI flags are
  now an error (they were silently ignored). Namespace `p3prime` renamed `coords`.
- Base = the fully lazy version (lazy evaluation for ALL tiers 0-6). NOTE: the file
  `cpp/chain1234prime.cpp` on disk only has lazy tiers 0-1 (it is ~60x slower per trial); do not
  use it as a base -- it is archived in `archive/cpp/old_main/`. The soft-min / move-lock and phase-1a/1b
  experiments were NOT carried over (see `archive/cpp/phase1ab_experiment` and `archive/cpp/phase1_softmin_experiment`; they can be re-applied on the new base).
- One templated `dfs_tier<K>` (search.h) replaces the seven copy-pasted `dfs_phaseN`;
  per-tier code is just `Tier<K>` traits + lazy states in state.h. `heuristic(State6)` now
  classifies the tier and evaluates the same lazy heuristic the search uses; the independent
  full-cube ground truth (`is_in_s*`, `s5/6/7_dist_of`) lives on for the self-tests.
- New S3'->S4 machinery (s4_coords.h): six coordinates (layer 20160, eq 4, pll 2, ud 2520,
  lr 24, fb 24) each derived by a generic "BFS the orbit under the 13 S3' moves" builder (sizes
  are checked at startup), `compose()` for products, and `load_or_build_pair_table` (two-factor
  distance tables, level-scan BFS, ~40 s for 406M entries, fingerprinted cache files).
  Distance stats of every combination of the 6 coordinates within [161,280 .. 500M] entries are
  in `archive/cpp/s3p_table_experiment/` (`table_explorer.cpp`, `out.txt`).
- selftest.h: per-tier lazy-vs-full-cube checks from randomized tier entries (tiers 0-6, the
  5->6 handoff), rotation/conjugation checks, h7 cases, and an admissibility witness for the
  S3'->S4 tables. They run at startup in ~0.1 s.
- Verification: with a "legacy" `s4_tables.h` (old WING4/CENTER4-equivalent tables), the new
  code reproduces the old solver's per-trial scramble/length/nodes/tier stats/fork stats exactly
  (12 trials). Do this after any structural change (node counts are exact; wall-clock on this
  machine varies ~3x between identical runs).

Reference numbers (30 trials, seed 2024, COST12 9, COST23' 7, COST45 9, COST56 8, COST67 2):
avg nodes, legacy/current/big-table: COST3'4=11: 3.40M/2.50M/2.48M (len 56.07); COST3'4=10:
5.09M/4.15M/4.13M (len 55.10). The big table's extra 356 MB bought <1% fewer nodes. With the new tables S3' nodes nearly
vanish (29M -> 0.8M at COST3'4=10) and tier 4->5 (S5) is now ~60% of all nodes.

NISS (added 2026-09-30; ON BY DEFAULT since the forced-ends work, `--no-niss` gives the plain search, `--niss` is still accepted; details in `cpp/README.md` and the header comment of
`cpp/niss.h`): the search may invert the cube state for free at the root and on entering S1/S2/S4/S5. One
cube state is kept and moves are appended on its right; final solution = N word followed by the inverse of
the I word. A switch right after a quarter turn m leaves a free move m^2 (the path ending in m^-1) as a
`Pending`: only ONE orientation of m may switch (else both nodes explore the same class); the pending free
move is conjugated by rotation forks and resolved into {flip, no flip} branches on a switch back, on a wide
free move reaching S2, and on a face-half-turn free move reaching S6 (lazy left-multiplication tables in
niss.h keep the S5->S6 fast path lazy). Rotation forks insert their letter into both words. Result on 100
scrambles (seed 2024, COST 9/6/10/9/8/2, penalty 7/1): avg length 54.51 -> 53.99, avg nodes 8.11M -> 4.92M,
avg time 2.37 s -> 1.06 s, 100/100 literal-solved. Not handled: commuting trailing moves (F' B vs F B) still
duplicate after a switch (~7% of switch crossings). Within-phase NISS (switching after short unpruned move sequences inside a phase) was implemented, tested per tier and in total, found neutral (length 53.99 -> 53.96..54.03, nodes +-5%, up to 18-26% extra full-state rebuilds) and reverted; the code is kept in archive/cpp/niss_within_phase/. Verification: self-tests (algebra vs raw simulation,
tier-6 tables, free-move invariance, assembly of the worked examples), `--check-replay` (every explored NISS
branch vs a replay of its path: 0 mismatches over 376k branches), and `--no-niss` reproduces the old (plain) solver
node for node.

Forced prefix/suffix (2026-09-30, `--prefix`/`--suffix`, REPL `prefix`/`suffix`; `cpp/forced.h`, details in `cpp/README.md`): to force a solution P Z L, solve the scramble `L S P` (NOT `L^-1 S P`: L S P Z = id gives S P Z L = id by a cyclic shift; with L^-1 you would get S P Z L^-1). Z may not cancel with P or L, even through commuting moves and any powers (P = "U F" forbids Z starting F, F', F2, B F, B' F2, ...). It is enforced by refusing the solved cube in dfs_tier<6> (`goal_accepted`) when `forced_cancels` finds a merging pair in the finished solution (fork rotations closed / NISS assembled, root rotation prepended); the check compares cube elements in the solution's initial frame, so it is exact across rotations. Without the flags the solver is node-for-node identical. Refusing only at the goal blew up (one trial 1.4G nodes), so `Forced::virtual_last` also pretends the previous move at the start of the normal scramble was the highest-rank move of P's trailing same-axis block and, on the inverse side (--niss), of L's leading block (rotations pushed through exactly); it fully handles the prefix and, with --niss, a suffix whenever the I word is non-empty (+1 move in 1/100 scrambles from over-forbidding). First solution cancels in 29% (plain) / 19% (niss) of scrambles for P="Rw' U' Fw" L="R2 U'"; the raw refused/reached counts (e.g. 99/129) are mostly the same solutions rediscovered per threshold/fork. Fix for the NISS heavy tail (trials of 30-70M nodes, thousands of refused solutions): fork rotations go into both words, so one fired while a word is still empty lands at the boundary of Z (Z starts/ends with a rotation) and shifts the frame of the boundary move; `refresh_virtual_wl` now recomputes the pretended move of a move-less word from the rotation letters in it (at crossings that switch or open a fork). NISS refusals 254 -> 9 per 100 scrambles, worst trial 2.6M nodes. Remaining gap: a normal-only solution's end and an inverse-only solution's start are only caught at the goal. Verified: literal solves with and without --niss, unforced runs node-for-node identical.

Phase 1 is now ENDTABLE-ONLY (2026-10-03, `cpp/phase1.h`): exact distance to S1 from cumulative blocked Bloom filters over the three center bitmasks + wing parity (depth 6 = 1.67M states, ~3.5 MB, 0.3 s to build), replacing the 735,471-entry coordinate transition/distance tables for every mode (archived in `archive/cpp/phase1_coordinate_tables/`): same solutions, -32% nodes, 1.9x faster; `State6.s1` is a `p1::MaskState`. The earlier "no endtables" rule applied only to hash-map depth-3 endtables bolted onto a tight coordinate heuristic; this endtable REPLACES a weak heuristic over a tiny space and wins (user asked for it explicitly).

Randomized start (2026-10-03, `--random-start`, `cpp/random_start.h`, full write-up in `cpp/README.md`): solves the NORMAL 4x4x4 by trying many within-face center permutations of the inverse scramble P^-1 S^-1 [perm] L^-1, ranked by exact S1 distance; answer P Z L; verified with `is_visibly_solved`. Facts worth remembering: only EVEN center permutations are reachable supercube states (center sign == corner sign for every move; sympy-checked), so the one-transposition walk uses every second state; the permutation behind L^-1 acts inside U_i = L^-1(face center positions); S1 layers to depth 6 are 1,3,54,717,9042,118602,1539456.

Wing coset (phase 2) is now a 24-bit mask of the slots holding the 12 positive wings (2026-10-03, `cpp/phase2.h`): moves permute the bits with byte lookup tables (no 292 MB transition table, no BFS at start-up), the distance table is indexed by the low 23 bits (unique because the mask has exactly 12 ones; 2^23 bytes, 3.1x sparse). Same node counts; phase 1/2 start-up 6.2 s -> 0.4 s. Wing coset distance histogram: 0:1 1:5 2:89 3:1441 4:22955 5:310270 6:1799686 7:569705 8:4 (avg 6.0772, max 8). `WING_SOLVED_IDX`/`NUM_WING_COSETS` in `tables2.h` are no longer used for the state (only NUM_WING_COSETS in a self-test).

Phase 2 joint endtable (2026-10-03, `cpp/phase2_endtable.h`, `--p2-depth`, default 5): exact distance to S2 of the pair (wing mask, F/B permutation) over the 21 phase-2 moves, cumulative Bloom filters (layers 96,192,2784,28032,234816,2060160,19384800); heuristic h1 = max(endtable, wing table, F/B table). Depth 5: -37% nodes, depth 6: -58% nodes (ablation table in cpp/README.md). `--p2-tables` switches the old tables off for ablations.

S2 -> S3' by switching (2026-10-03, `cpp/s2switch.h`, `--s3-route normal|switch|both`, write-up + profile table in cpp/README.md): from an S2 state the INVERSE is in S3' iff (a) its L/R arrangement is (half turns) then R^a L^b (384 of 8! arrangements) and (b) with A_e/B_e = the edge positions of the positive/negative wing of equator edge e, A_e = B_v(e) with v in the Klein four-group (a lone 2-cycle does not count; the F/B centers are automatic). Tables: 40320-entry L/R distance + transitions, 1,470,150-class wing distance (141M raw states / (S4 x V4) = 96, index from the 8 positions, no transition table). Tier code 7 = tier 2 searched this way; its crossing is a forced switch (force_sw in build_niss_variants). 100 scrambles: switch -36% time / +13% nodes / +0.16 moves, both -19% time / +6% nodes / +0.04 moves vs normal (default stays normal).

MAIN = SWITCH ROUTE ONLY (2026-10-03): the S2 -> S3' step is searched only through the switch route (`cpp/s2switch.h`, tier 2); the normal route and `--s3-route`/`--no-niss` are archived with their 120 MB `s3prime_wing_dist.bin` in `archive/cpp/s3_route_normal_and_both/` (reason: the website cannot afford the huge table; the remaining S3'->S4 tables are to shrink via symmetry later). Main needs NISS. Tried and rejected on the switch route (each admissible, tiny node gains, not worth it): +1 on the centers heuristic is NOT admissible (Uw2/Fw2 enter/leave the L/R goal), a wing "optimal path ends in Uw2/Fw2" bit (-1.5% nodes), R/L quarter-turn counting tables (-6.6% nodes, +2.5 s start-up).

S4 mod whole-cube half turns (2026-10-03, `cpp/state.h` `s4_k_class`, `cpp/README.md` last section): S3'->S4 crossing accepts the literal S4 and its images under x2/y2/z2 (rotation classes 6..8, rotations.h regenerated by archive/generate_rotation_tables.py, sentinel layout follows NUM_ROTATE_CLASSES), canonicalizes with a free rotation letter; S3'->S4 pair tables are distances to the nearest of the 4 goals (files `*_modk.bin`, goal set is in the cache fingerprint). 200 scrambles: nodes -22%, all verified. `Rw2 U2 D2 F2 B2 Rw2` + x2 is S4/S5/S6. 16-fold symmetry of those tables is exact only because of this quotient (see the symmetry analysis in the chat history).

S3'->S4 tables reduced by symmetry (2026-10-03, `cpp/s4_sym.h`, `cpp/symtab.h` from `archive/generate_sym_tables.py`, README last section): T1/T2 are invariant under 16 conjugations by cube symmetries (+ the left half turns x2/y2/z2 for T1) once S4 is defined mod half turns; ud splits into 126 / 215 classes; reduced tables 2.54M + 0.99M entries in `s4_sym_*.bin`, nibble-packed with distances capped at 15 (1.8 MB, was 62 MB; the cap changed no node count); same nodes, same speed. The group elements are NOT graph automorphisms (wide moves go to move + half turn), so the reduced tables are cut out of the full BFS table, not built by a BFS in the reduced space (that leaves T2 entries unreached). `--check-s4-sym` verifies every entry. The old `s4_*ud*.bin` files are unused.

Leave slice, `--ls` (2026-10-04, `cpp/ls.h`, README last section): opt-in S5 -> LS -> solved ending, LS = <U'D, R2 U'D B2 U D', ...> of order 384. Phase A = IDA* on the UD part (corners, 8 layer dedges, U/D centers) with U,R2,L2,F2,B2 (no D), heuristic max(Bloom endtable of 465k canonical states, corner table), canonicalized by LEFT multiplication with (U'D)^j (right multiplication does NOT commute with the appended moves). Phase B replaces the found word X by a word Y over R2 L2 F2 B2 U U' U2 (D y) (D' y') (D2 y2) [(D y) = top three layers] with a stack heuristic (inverse of X, adjacent cancellation) and the previous-move guard; y rotations are summed and closed so their total is 0 (rotations as close sentinels in the current word, NISS-safe). Y must start from the cube BEFORE X. `--cost-ls N` (default 11 since the follow-ups; no NISS switch on the move entering S5; `--ls-depth N` endtable depth, 9 = 5.2 MB; phase-A transitions fused with the canonicalization) replaces COST56+COST67; cost 12 / 11 give 1 / 2 moves shorter solutions than the default pipeline with fewer / equal nodes (60 scrambles seed 21: baseline 57.60 moves 254,758 nodes; ls12 56.62 / 206,086; ls11 55.62 / 264,890; ls10 54.72 / 367,288; ls9 53.97 / 1.0M). The default pipeline is unchanged.

Profile + checkpoints (2026-10-04, README last section): S1 + S2 child steps ~61% of the time, tier crossings ~28% (17% was replaying the whole path to rebuild the full cube); `search.h` now keeps a checkpoint stack (`g_ck`) so a crossing replays only the tokens since the innermost checkpoint (`--no-checkpoints` to A/B; node for node identical, ~12% faster paired). A one-probe S1 endtable was tried and gave nothing measurable.

Integer penalty + S2 fast path (2026-10-04, README last section): `--penalty-rate` is a whole number now (PEN_UD/PEN_LR tables, no double math); S2 wing index from a rank table, and a budgeted S2 heuristic (`s2sw::g_r`, 63% of S2 children pruned by the L/R table alone). Node for node identical, ~10% faster paired (`--no-s2-fast` for A/B).

Start-up (2026-10-04, README last section; `--startup-times` lists every step): build_s5_dist 1,183 -> 215 ms (BFS over a precomputed corner-twist table, exact-distance-field self-test). Remaining big steps: p2j::build ~850 ms, s2sw::build ~790, init_niss_tables ~250, wing coset table ~230, s7 ~215, p1 ~205, s4sym ~210.

Start-up part 2 (README last section): BlockedBloom::init remembers its (bits, k) answer per target FPR (~14 ms per filter saved), p2j::build ~215 -> ~128 ms, s2sw::build ~274 -> ~145 ms (wing BFS skips its last two layers, max distance 7 is fixed; exhaustive test_s2_wing_dist).

Start-up part 3 (README last section): `--ls` no longer builds the S6/S7 tables/composites/T6 left-mult (g_legacy_tail=false; is_in_s7 = literal identity); wing coset 100 ms, S5 114 ms, s2sw wing 80 ms (bottom-up last layers + exhaustive field tests), s4sym 95 ms (generating subset; cache fingerprint now hashes the symmetry inputs, caches regenerated once), ls::build 95 ms (streamed last layer), p1 depth 7 streamed in chunks + 64-deep prefetch. `--ls` defaults start in ~1.4 s.

README.md in cpp/ now starts with a "Read this first" section: current configuration, measured start-up / shipped files / memory, verification references. Random-start memory fix (lazy RootEntry map, 554 -> 152 MB peak) is recorded there.

PRODUCTION WEB BUILD (2026-10-04): `index.html` + `worker.js` + `solver.js/.wasm/.data` in this folder, sources `cpp/web_solver.h` + `cpp/web_main.cpp`, build `cpp/build_wasm.ps1`, tests `cpp/web_test.cpp` and `cpp/web_node_*.js`; full description (UI, modes, random-state generation, rotation-free output, numbers) in the first section of `cpp/README.md`. Dev-only files (main.cpp, selftest.h, CLI) are not part of it.

The older sections below are the historical design log; file names in them (chain*.cpp) refer to
the pre-refactor code in `archive/` and `cpp/chain1234prime.cpp`.

## The phase chain (S0 ⊃ S1 ⊃ ... ⊃ S7)

The overall strategy is Thistlethwaite/Kociemba-style: a chain of nested
subgroups, each phase solving from the previous group into the next.
This was worked out *before* any code was written, by computing exact
group orders with a Python script using `sympy`'s `PermutationGroup`
(exact, via Schreier-Sims — not sampled). See `phase_sizes.py`.

Final chain (after iterating to keep every phase's coset size under
roughly 10^12, small enough for IDA* with decent heuristics):

```
S0: <U, D, R, L, F, B, Rw, Uw, Fw>                    (Lw,Dw,Bw dropped: redundant
                                                        up to a whole-cube rotation)
S1: <U, D, R, L, F, B, Rw2, Uw2, Fw2>
S2: <U, D, R, L, F2, B2, Rw2, Uw2, Fw2>
S3: <U, D, R, L, F2, B2, Rw2, Lw2>                    (Lw2 turns out to be
                                                        REDUNDANT here after
                                                        all — see correction
                                                        below; harmless to
                                                        list, just unneeded)
S4: <U, D, R, L, F2, B2>
S5: <U, D, R2, L2, F2, B2>
S6: <U2, D2, R2, L2, F2, B2>
S7: <>
```

| n | [S(n-1):Sn] | ~10^ |
|---|---|---|
| 1 | 18,931,023,540 | 10.28 |
| 2 | 1,135,745,520 | 9.06 |
| 3 | 10,080 | 4.00 |
| 4 | 7,242,504,192,000 | **12.86** (only phase over the 10^12 target, ~7x) |
| 5 | 8,660,520 | 6.94 |
| 6 | 235,200 | 5.37 |
| 7 | 5,308,416 | 6.73 |

Phase 4 (S3→S4) is "the big kahuna" — the wing-pairing step, expected to
be the hardest phase to implement well.

**Correction (found 2026-09-18, while starting phase 4): Lw2 is actually
REDUNDANT for S3, contradicting the earlier "verified NOT redundant" claim
from the original chain-design stage.** Rechecked from scratch with a
fresh script (`check_lw2.py`) computing exact group orders directly from
cube_model.py's real move permutations via sympy: `|<U,D,R,L,F2,B2,Rw2,
Lw2>|` and `|<U,D,R,L,F2,B2,Rw2>|` are EXACTLY EQUAL (~7.83e31), and
`Lw2 ∈ <U,D,R,L,F2,B2,Rw2>` is `True`. This does NOT change S3 itself or
the `[S2:S3]=10,080` quotient (a redundant generator never changes the
group it generates) — it just means **phase 4's search needs only the 7
already-existing reduced moves (U,D,R,L,F2,B2,Rw2), with no reconstruction
of a dropped wide move required at all** (unlike phase 3, which did need
to reconstruct Lw2 via `cm.make_move` just to compute S3's target sets —
that reconstruction was harmless/correct, just, in hindsight, unnecessary
for that purpose too, since any BFS orbit computed with a redundant extra
generator gives the identical correct orbit). Moral: don't take an earlier
"verified by direct computation" claim on faith if it becomes load-bearing
for new work — rerun the check with fresh, current code before depending
on it, especially since this project's cube_model.py has been edited since
that original claim was made (the handedness fix). See "What didn't work"
below.

**Phases 1, 2, and 3 are implemented so far. Phase 4 is now in progress.**

## Repository layout

```
SuperCubeSolver444/
  phase_sizes.py          -- one-off: computes S0..S7 group orders via sympy
  cube_model.py            -- CORE: 96-sticker cube representation, all 27
                              moves, 24 whole-cube rotations, canonical move
                              ordering, piece extraction. Everything else
                              imports this. Read its module docstring.
  combinatorics.py         -- C(n,k) rank/unrank (combinatorial number
                              system) and n! rank/unrank (Lehmer code)
  phase1.py                -- S0->S1 solver (Python prototype, validated)
  phase2.py                -- S1->S2 solver (Python prototype, validated)
  phase3.py                -- S2->S3 solver (Python prototype, validated)
  phase4.py                -- S3->S4 solver (Python prototype -- logic
                              validated via spot-checks; the full 12! table
                              is only built in C++, too slow in Python)
  phase4_explore.py        -- one-off: verifies the wing-twin-adjacency
                              geometry (which moves can/can't split a wing
                              pair) before phase4.py depends on it
  check_lw2.py             -- one-off: re-verifies (and corrects) whether
                              Lw2 is redundant for S3 -- see the correction
                              note in the phase chain section above
  check_s4_order.py        -- one-off: re-verifies |S3|, |S4|, [S3:S4]
                              fresh against current code (sanity check
                              prompted by the Lw2 surprise -- came back
                              matching the original value exactly)
  check_pairing_parity.py,
  check_s4_membership.py   -- one-off: investigate/confirm the "only half
                              of 12! reachable" wing-pairing finding (see
                              phase 4 section) -- the latter cross-checks
                              against sympy's own group membership test
  phase12_chain.py         -- unified S0->S2 attempt (Python) -- HAS A
                              KNOWN UNFIXED BUG, see below. Superseded by
                              the working C++ version.
  generate_tables.py       -- dumps cpp/tables.h from cube_model.py +
                              phase1.py (move perms, canonical order info,
                              LR/FB reuse mapping, etc.)
  generate_tables2.py      -- dumps cpp/tables2.h from phase2.py
  generate_tables3.py      -- dumps cpp/tables3.h from phase3.py
  generate_tables4.py      -- dumps cpp/tables4.h from phase4.py
  cpp/
    phase1.cpp / .exe      -- C++ port of phase1.py. Interactive REPL:
                              type a scramble, see the phase-1 solution.
    phase2.cpp / .exe      -- C++ port of phase2.py (S1-restricted moves).
                              Same REPL pattern.
    phase3.cpp / .exe      -- C++ port of phase3.py (S2's own moves, minus
                              the empirically-verified U/D no-ops). Same
                              REPL pattern; solves are near-instant (phase
                              is tiny -- 10,080-state quotient).
    phase4.cpp / .exe      -- C++ port of phase4.py (S3's own moves). Same
                              REPL pattern. Builds a ~479MB 12!-state BFS
                              distance table on startup (the current long
                              pole -- see "Phase 4" section for timing once
                              known). --heuristic max|sum|weighted --w1 --w2.
    chain_test.cpp / .exe  -- SEQUENTIAL baseline: raw-cube-state chain
                              test, phase1 -> phase2 -> phase3 solved in
                              turn. This is the "known-good" reference to
                              beat. Instrumented with node counters for
                              all three phases. (Not yet extended to
                              phase 4.)
    chain12.cpp / .exe     -- UNIFIED S0->S2 solver: single IDA* over all
                              27 moves, combined heuristic. Has its own
                              REPL and --bench mode. Instrumented with
                              node counters + a compute_fb2 microbenchmark.
    chain123.cpp / .exe    -- UNIFIED S0->S3 solver: extends chain12.cpp
                              with a third heuristic tier for phase 3 (see
                              "Unified vs sequential, extended to phase 3"
                              below). --cost12/--cost23 CLI args, same
                              REPL/--bench pattern. Settled: COST12=9,
                              COST23=7.
    chain1234.cpp / .exe   -- UNIFIED S0->S4 solver: extends chain123.cpp
                              with a fourth heuristic tier for phase 4 (see
                              "chain1234.cpp" section below). Adds raw
                              wing_slot[24] to the unified state (needed
                              for wing-pairing, unlike ud/lr/fb which
                              derive from center_slot). --cost12/--cost23/
                              --cost34/--heuristic/--w1/--w2 CLI args.
                              COST34 untuned (starts at user's guess of 13).
    pairing_dist_io.h      -- shared packed/dense/cached storage for phase
                              4's 12! wing-pairing distance table -- used
                              by BOTH phase4.cpp and chain1234.cpp (they
                              build the identical table). See "Packed +
                              cached pairing table" below.
    pairing_dist_p4.bin    -- GENERATED on first run of phase4.exe or
                              chain1234.exe (whichever runs first), ~120MB.
                              Not checked into source control tracking
                              expectations here, but lives in cpp/ per the
                              folder-scoping rule. Delete to force a
                              rebuild (e.g. after changing tables4.h).
    tables.h / tables2.h /
    tables3.h / tables4.h  -- AUTO-GENERATED, do not hand-edit. Regenerate
                              via the generate_tables*.py scripts after any
                              change to cube_model.py/phase1.py/phase2.py/
                              phase3.py/phase4.py.
  cost_sweep.txt, cost23_sweep*.txt,
  cost12_sweep_re.txt,
  sweep_c34_*.txt,
  sweep_c23_*_c34_*.txt      -- logs from the various COST12/COST23/COST34
                              tuning sweeps (see "Unified vs sequential"
                              and "chain1234.cpp" below). The cost34/c23
                              sweep logs were run via PowerShell's
                              Start-Process with file-redirected stdin/
                              stdout/stderr (empty_stdin.txt is the empty-
                              input helper file) -- see the note on Bash
                              vs PowerShell below for why.
```

## Core design decisions (cube_model.py)

- **State representation**: a length-96 "sticker permutation" (matches the
  4x4x4's 96 real facelets: 8 corners x3 + 24 wings x2 + 24 centers x1).
  `state[i]` = current position of the sticker that starts at index `i`.
- **Geometry**: coordinates x,y,z in {0,1,2,3}. A cubie is visible iff at
  least one coordinate is extreme (0 or 3); classified as corner (3
  extreme)/wing (2 extreme)/center (1 extreme).
- **Handedness bug (found and fixed by the user mid-project)**: the
  original `rotate2()` used one fixed rotation convention for all axes.
  This is fine for computing abstract group orders (any consistent
  rotation gives an isomorphic group) but WRONG if you want moves to match
  real-world WCA notation. Fixed by adding a `positive_side` parameter to
  `make_move()`: U/R/F (and their wide versions) need the REVERSE cyclic
  axis order, D/L/B need FORWARD — because they're viewed from opposite
  sides of their axis for "clockwise" to mean the same physical rotation.
  `cube_model.py`'s `__main__` now has exhaustive regression tests for
  this (checks all 6 faces + wide moves against independently-derived
  standard formulas, not just internal self-consistency). **Rerun this
  file's self-tests after ANY geometry change.**
- **Reduced move set**: only 9 "families" (U, Uw, D, R, Rw, L, F, Fw, B) x
  3 powers = 27 moves. Lw/Dw/Bw are dropped — proven redundant (S0's order
  is unchanged whether or not they're included as generators, and since
  S0 with them added is trivially rotation-invariant, S0 without them
  must equal it exactly, not just "the same up to rotation").
- **Canonical move ordering**: same-axis moves must strictly increase in
  "rank" (U-family=0 < Uw-family=1 < D-family=2, etc.) — prevents
  redundant sequences like `U U2` or `U D U` (since same-axis moves
  provably commute — any two rotations about the same physical axis
  commute regardless of which layers they touch, a fact worth re-deriving
  if you doubt it, it's not obvious from cubing intuition). This is
  `cm.ALLOWED_NEXT` / `cm.canonical_ok()`.

## Phase 1 (S0->S1) — implemented, Python + C++, validated

Tracks 4 coordinates: `ud`, `lr`, `fb` (which 8-of-24 center slots are
occupied by the U/D-, L/R-, F/B-family pieces respectively — C(24,8) =
735,471 each) plus `parity` (0/1).

- **Key finding**: the user's stated "S0/S1 parity" invariant (flips only
  under Rw/Rw'/Uw/Uw'/Fw/Fw') is exactly the **permutation parity of the
  24 wings**. Verified computationally against all 27 moves, not assumed.
- **Table reuse trick**: `lr` and `fb` don't get their own 735K-entry
  tables. They reuse the SAME `ud` table via a slot relabeling (through a
  120-degree whole-cube rotation) plus a **family+power remapping** that
  had to be derived empirically (e.g. real move `R` corresponds to
  looking up `U'` in the `ud` table, not `U`) — verified exhaustively
  against direct simulation, zero mismatches. This works because S0 is
  provably rotation-invariant (see above), so the reuse isn't just a
  heuristic shortcut, it's exact.
- **Endtable**: BFS from the identity 4-tuple, depth ≤3, ~784 entries
  (well under the naive 20,440 bound).
- C++ port: builds all tables in <1s (vs ~70s in Python), 79.4MB memory,
  avg solve ~3.7ms, worst observed ~39-83ms.
- Interactive tool: `cpp/phase1.exe` (build with
  `g++ -O3 -std=c++17 -o phase1 phase1.cpp`), type a scramble, see the
  solution and per-coordinate distances.

## Phase 2 (S1->S2) — implemented, Python + C++, validated

**Critical, non-obvious design point**: phase 2 must search using ONLY
S1's own 21 legal moves (U/D/R/L/F/B at all 3 powers + Uw2/Rw2/Fw2 only),
NOT the full 27. Every S1 generator is itself an element of S1, so any
product of them stays in S1 — this is what lets phase 1's work survive
while phase 2 progresses. This was derived, not given, and it matters a
lot for what follows (see "Unified vs sequential" below).

Two coordinates:
- `wing_idx`: the 24 wings split into two orbits of 12 under S2's
  generators (verified computationally — matches the user's stated fact
  exactly). C(24,12) = 2,704,156 states.
- `fb_idx`: permutation of the 8 F/B-center pieces among their 8 slots
  (8! = 40,320, Lehmer-code ranked). "Solved" = any of the 96
  S2-reachable permutations (verified computationally, matches exactly).

**A real bug, caught by testing the full chain, not by unit-testing each
phase in isolation**: `apply_family_to_perm8` initially indexed the
family permutation by piece identity instead of by slot. This silently
collapsed the reachable F/B-permutation space from 40,320 down to 6. It
was NOT caught by phase2.py's own self-tests (which only checked
structural facts like table sizes) — it was caught by
`phase12_chain.py`'s attempt to chain phase1→phase2 on a real scrambled
state. **Lesson: always build an end-to-end test that starts from a raw
scramble and independently re-extracts each phase's coordinates, not just
tests each phase against its own tables.** `chain_test.cpp` is this test
for phases 1+2; 100/100 trials verified correct.

C++ port: wing table 1.8s to build (97.3MB, 9-family-compressed), fb
table near-instant. Sequential-chained solve (phase1 then phase2, fresh
IDA* each): current numbers (from the just-added node-count
instrumentation) are in `cost_sweep.txt`'s sibling run — see the
Performance section below for the actual comparison numbers once
`chain_test.exe`'s instrumented run finishes.

## Phase 3 (S2->S3) — implemented, Python + C++, validated

Per the original spec: "so small it's hard to mess up as long as you know
what is actually in S3." Two coordinates, both reusing the exact same
"8!-permutation, Lehmer-ranked, slot-indexed material-flow" machinery
phase 2's `fb_idx` already established:

- `lr_idx`: permutation of the 8 L/R-center pieces among their 8 home
  slots. Unlike `fb_idx`, this is a BRAND NEW coordinate (L/R permutation
  was never tracked before) -- but its transition-table code is 100%
  reused from phase 2's `apply_family_to_perm8`/family-perm machinery,
  just pointed at `p1.LR_TARGET` instead of `p1.FB_TARGET`. Free of any
  S1/S2-level restriction: all 40320 permutations are reachable merely by
  being inside S2.
- `fb_idx`: the SAME coordinate phase 2 tracks, just with a smaller target
  set.

**S3-solved target sets** (the two "extra conditions" from the spec):
`lr_idx` restricted from all 40320 down to 16 valid permutations; `fb_idx`
restricted from the 96 S2-valid permutations down to 24 (a subset,
verified by direct set-containment check). Both computed by BFS-ing the
orbit of the identity permutation under S3's OWN generators
`<U,D,R,L,F2,B2,Rw2,Lw2>` -- note this needs **Lw2**, which is not one of
the 27 reduced moves (Lw was dropped from cube_model.py as
redundant-with-Rw-up-to-rotation). Rather than reconstruct it via rotation
conjugation, it's simplest to just call `cm.make_move(0, {0,1}, False)`
directly (mirrors L's own construction, axis=0/negative-side, just widened
to the two leftmost layers exactly like Rw widens R) -- make_move works
for any (axis, layer_set, positive_side) triple, dropped-from-MOVES or
not. Verified: 16 and 24 exactly, matching the spec, and the 24 is a
verified subset of phase 2's 96.

**Non-obvious optimization, found by measuring rather than guessing**: U
and D are complete NO-OPS for this phase's own `(lr_idx, fb_idx)`
coordinate. Centers have exactly one extreme axis, so an L/R-family piece
(x-extreme) or F/B-family piece (z-extreme) never has an extreme
y-coordinate -- meaning neither ever sits in the single layer U or D
turns. Verified computationally (not just argued): for each of S2's 17
generators, check whether its transition column is the identity map over
the full 40320-entry domain for BOTH `lr_idx` and `fb_idx`; U/U2/U'/D/D2/D'
are the only 6 that are. Excluding them shrinks phase 3's own search
branching factor from 17 to **11**, with zero solutions lost -- U/D remain
perfectly legal moves for other phases, just useless for this one's narrow
subgoal. `compute_phase3_moves()` in phase3.py does this check and asserts
the expected result; `generate_tables3.py` bakes the reduced 11-move list
directly into `P3_MOVE_NAMES` so the C++ side never has to re-derive it.

**Heuristic: one exact joint distance table, not max-of-separate-tables +
endtable.** Since the whole phase is tiny (quotient size 10,080), a full
BFS over the joint `(lr_idx, fb_idx)` state space -- 40320 x 96 = 3,870,720
pairs, restricted to `fb_idx` in phase 2's 96-valid set since that's the
only domain reachable once actually inside S2 -- is completely tractable
(3.87MB as `uint8_t`, computed in ~150-200ms in C++ from raw `CENTER_PERM`,
~1s in the C++ unified solver where it's rebuilt from scratch each run).
This gives an EXACT admissible distance-to-S3 lower bound, strictly better
than the max-of-two-tables-plus-endtable pattern phases 1/2 use (which
exists specifically because THEIR phases are too big for an exact joint
table). Max joint distance observed: 12.

Python (`phase3.py`) and C++ (`phase3.cpp`) were cross-validated the same
way phases 1/2 were: independent end-to-end raw-state chains
(phase1->phase2->phase3) in both languages, both 100%/20-for-20 correct,
run with matched design choices but independently re-derived numbers (no
copy-pasted "expected" constants). Standalone C++ solve performance:
avg 0.0004ms, max 0.0044ms, avg solution length 4.00, max 9 (200 random
S2-entry scrambles) -- as expected for a 10^4-scale phase with an exact
heuristic.

## Phase 4 (S3->S4) — "the big kahuna" — in progress

S3's own generators, AFTER the Lw2-redundancy correction: `<U,D,R,L,F2,B2,
Rw2>` (7 real, already-existing reduced moves; see the correction note in
the phase-chain section above). Legal powers: U,D,R,L at 1,2,3; F2,B2,Rw2
at just their given power — **15 moves total**, this is phase 4's own
search move set (`phase4.py`'s `S3_MOVES`). S4 = `<U,D,R,L,F2,B2>` — S3
minus Rw2 specifically, which lines up exactly with what Rw2 turns out to
be geometrically necessary for (see below).

Two conditions define S4:

**(a) Wing pairing** — each of the 24 wings must be paired with its
physical twin (the two halves of one real 4x4x4 edge). Represented as a
permutation of 1..12 over the positive-orbit wing slots, per the original
spec: `perm[i]` = index `j` such that negative-orbit slot `n_j` currently
holds the twin of whatever sits at positive-orbit slot `p_i`. Solved =
identity. 12! = 479,001,600 states, no restriction going into this phase
(matches `[S3:S4]`'s 12! factor exactly).

**Key geometric fact, verified computationally in `phase4_explore.py`, not
assumed**: single-layer moves (U,D,R,L,F,B, any power) can NEVER separate
a wing pair — both physical halves of an edge always share the same two
extreme coordinates, so a single-layer move's layer set either contains
both halves or neither. Wide moves that grab only PART of that shared
extreme axis (Rw/Rw'/Rw2 — Uw/Fw aren't in S3's generator set at all) DO
separate pairs, confirmed for all three powers including Rw2 (squaring
does not "cancel out" the split). This is exactly why phase 4 exists at
all (phases 1-3's own solving can use Rw2 and leave pairing scrambled),
and exactly why S4 drops Rw2 once pairing is restored — "no longer needing
Rw2" and "pairing is fixed" are the same milestone.

**No transition table for the 12! coordinate** (per spec, far too big) —
moves are applied DIRECTLY to the 12-element array instead. For a move `m`
that preserves the positive/negative wing-orbit split as sets (true for
all of S3's generators), `m` induces two 12-element INDEX permutations:
`SIGMA_POS[m]` (how positive-slot indices move) and `SIGMA_NEG[m]` (how
negative-slot indices move) — identical to each other for "safe"
(non-splitting) moves, since a pair's two halves move in lockstep, but
genuinely different for Rw2, which moves each half independently. Update
rule (works for both cases uniformly): `new_perm[SIGMA_POS[m][i]] =
SIGMA_NEG[m][perm[i]]`. Verified EXACTLY against independent re-extraction
from a full 24-wing raw state (`perm12_of_state`) over 30 random 25-move
S3-legal walks, zero mismatches — this is the trickiest, most novel piece
of the whole project so far, and it validated clean on the first attempt
once the ADJ-commutation analysis above was worked out correctly.

The 479,001,600-entry `uint8_t` BFS distance table (index space size;
~479MB allocated) is built in `cpp/phase4.cpp` (not attempted in Python —
far too slow there; this is the first phase where the Python prototype
validates LOGIC via spot-checks only, deferring the full table build to
C++ entirely). Actual build time: **998.5 seconds (~16.6 minutes)** on
first run. Ranking uses a plain O(n^2) Lehmer code (144 comparisons per
permutation) — not yet optimized; the 16.6-minute build time is a
candidate for later optimization (a smarter rank/unrank, or avoiding
re-ranking already-known states) if it becomes a workflow annoyance, but
it's a ONE-TIME cost per process run, not per solve.

**Genuinely surprising finding, thoroughly cross-checked before trusting
it: only HALF of the 12! index space (239,500,800, not 479,001,600) is
ever reachable from a real S3 element.** First reaction was "this must be
a bug" (the BFS's own `n_reached` count came up exactly 12!/2, suspicious
enough to warrant real investigation rather than just relaxing an
assertion). Investigation: every one of S3's own moves preserves the
wing-pairing permutation's PARITY — for "safe" (non-pair-splitting) moves
this is immediate (`SIGMA_POS == SIGMA_NEG` identically, so the update
can't change parity at all); Rw2 moves the two orbits independently
(`SIGMA_POS != SIGMA_NEG`) but its own combined effect still happens to
be parity-preserving. Starting from the identity (even, trivially), only
the alternating-group half of `S_12` is ever produced. Verified two ways,
not just reasoned about: (1) `check_pairing_parity.py` samples 300 random
walks each under S2's and S3's own generators — pairing-parity is 0 in
**all 600/600** samples; (2) far more decisively, `check_s4_membership.py`
cross-checks the FULL 3-condition S4-membership test (this coordinate +
`ud_idx` + `fb_idx`) against **sympy's own independent
`PermutationGroup.contains()`** over 500 random S3 elements (338 of which
were genuinely in S4) — **zero mismatches**, plus 200/200 known-S4
elements (built from S4's own generators) correctly recognized. This is
about as strong a confirmation as this project has produced for any
single fact — an independent, trusted, unrelated verification method
(sympy's own group machinery, not anything built for this project) agreed
completely. Conclusion: real, harmless structural fact, not a bug — the
unreachable half of the distance table sits at the 255 sentinel forever
and is simply never queried by a genuinely-in-S3 state. Fixed the
code/warning to expect exactly 239,500,800, not 12!.

**An unresolved (but non-blocking) curiosity**: naively trying to
reconcile this against `[S3:S4]` by multiplying "wing-orbit-size" by
"ud/fb-orbit-size / ud/fb-target-size" comes up short by EXACTLY 2x
(`239,500,800 * 967,680/64 = 3,621,252,096,000`, half of the true
`7,242,504,192,000`) — meaning wing-pairing and (ud_idx, fb_idx) are
evidently NOT independent axes in the simple product-of-quotients sense
that worked cleanly for phase 3's lr_idx/fb_idx pair. Since the actual
3-condition membership test is independently confirmed correct against
sympy (see above), this doesn't block anything — but it's a reminder that
this kind of clean multiplicative quotient-factor reasoning, while a
great DESIGN tool (it's how the whole phase chain was originally sized),
isn't automatically a valid CORRECTNESS check once coordinates stop being
simple independent permutations — direct ground-truth cross-validation
(sympy's `contains()`, or the raw-state re-extraction pattern used
throughout this project) is the reliable fallback when the arithmetic
doesn't reconcile cleanly. Worth understanding fully before leaning on a
similar decomposition for phase 5+, but not a priority right now.

**(b) F/B and U/D centers**: `fb_idx` (phase 2/3's existing coordinate)
further restricts from the 24 S3-valid permutations down to **4**; a
BRAND NEW `ud_idx` coordinate (permutation of the 8 U/D-center pieces
among their 8 home slots — same 8!-Lehmer-rank machinery as lr_idx/fb_idx,
reusing phase 3's generic `build_perm8_table`/`apply_family_to_perm8`
verbatim, just pointed at `p1.UD_TARGET`) restricts from all 40320
(unconstrained before this phase) down to **16**. Both counts verified by
BFS-ing the orbit of identity under S4's own generators, matching the
spec's "4 * 16" exactly.

**A real bug, caught the same way phase 3's fb-confinement bug was**: the
first attempt at the `(ud_idx, fb_idx)` joint distance table built the
target-set orbit using S4's OWN (small) generators, then mistakenly
re-used those SAME generators to BFS the distance table outward from that
target set. Since an orbit is by construction closed under the generators
that built it, this produced a "distance table" covering only the 64
target states themselves (max dist 0) — a silent, plausible-looking
failure caught only because the code asserts the full domain size is
reached (`64/967680`, immediately visible) rather than trusting the BFS
blindly. Fix: BFS the distance table using `S3_MOVES` (phase 4's actual,
larger search domain), not `S4_GENS`. The corrected table reaches all
40320 x 24 = 967,680 states, max distance 16, in ~17s in Python (this one
IS small enough for Python; ported to C++ using the identical structure).

**Heuristic**: per the spec ("this step is big, so a non-admissible
heuristic might be in order"), `chain phase4.cpp` supports max / sum /
weighted-sum combination of the two lower bounds via `--heuristic
max|sum|weighted --w1 W --w2 W`, not yet tuned. The spec's third idea
(a separate "minimum Rw2 moves needed" distance table, since Rw2 is the
only pairing-relevant move, giving a lower bound of roughly `2n-1` or a
faster-but-less-tight `4n`/`5n` for `n` needed Rw2 moves) is NOT yet
implemented — flagged as a follow-up once the basic max/sum heuristic's
behavior is characterized.

**Status as of 2026-09-18: fully working standalone, but the FIRST
benchmark run was misleading — corrected below.** Python prototype
validated (all target set sizes and the wing-pairing move formula
confirmed exact against both direct re-extraction AND sympy's independent
group membership check). C++ port builds its tables in ~16.6-21.6 minutes
depending on run (UD/FB/joint tables: <50ms combined; pairing: ~993s-1295s
across two runs, total memory 482.9MB).

**First `--bench` result (avg len 3.91, avg time 0.67ms) was an artifact
of a broken benchmark, not a real property of the phase** — caught by the
user questioning how a ~7.24-trillion-element coset space could plausibly
average under 4 moves to solve (correct instinct: `log(7.24e12)/log(13)
≈ 11`, nowhere near 4). Root cause: the benchmark scrambled with only
UP TO 40 uniformly-random, non-canonical moves from solved, which barely
explores the space before self-cancelling (e.g. `Rw2 Rw2`, `U U'`) —
nowhere near enough to reach representative states. Also, the originally-
reported "max pairing dist=255" was itself a red herring — 255 is the
UNREACHED-sentinel value dominating a naive `max_element` over the full
479,001,600-entry array (half of which is the unreachable odd-parity
half, see above); the TRUE distribution among the 239,500,800 reached
states was never actually computed in that run.

**Corrected measurements**, after (a) building a proper histogram over
reached states only and (b) widening the benchmark's scramble length to
up to 200 moves:

```
pairing distance histogram (239,500,800 reached states):
  true max = 18, average = 14.611
   0:1  1:1  2:6  3:40  4:287  5:1266  6:5249  7:27036  8:115707
   9:425266  10:1525890  11:4945519  12:12361180  13:27989987
  14:53745671  15:66248918  16:59550676  17:12552824  18:5276
```

I.e. the OVERWHELMING majority of reachable states sit at depth 13-17 —
matching the volume-argument expectation, not the earlier "mostly under
4" impression. The single known worst-case pairing state (dist=18, paired
with solved ud/fb) solves in 19 moves, 7.6ms.

Widened `--bench` (scramble length 1-200, 100 trials, still the plain
`max` heuristic, COST/weights untouched):

| | value |
|---|---|
| avg h0 (initial heuristic) | 10.65 |
| max h0 | 17 |
| avg solve time | 458.69 ms |
| max solve time | 16,159.02 ms (~16.2s) |
| avg solution length | 13.49 |
| max solution length | 21 |

So phase 4 behaves like the other substantial phases after all — NOT
"near-instant" as first (wrongly) reported. This matters directly for the
next step: unlike the earlier premature conclusion, heuristic tuning
(`--heuristic sum|weighted`, and the spec's "minimum Rw2 moves" idea) is
very much still worth trying here, and should be evaluated against THESE
corrected numbers, not the retracted ones.

**Lesson**: a benchmark that scrambles with the SAME small move set it
will use to solve, for only a short fixed length, can silently undersample
a huge state space and produce a misleadingly optimistic picture — always
sanity-check an average solve length/cost against the state space's size
via a basic volume/branching-factor argument before trusting it, and
prefer either (a) much longer/more scrambling, or (b) directly
constructing and testing known-far states from the distance table itself
(as done here) over relying on random-walk sampling alone.

A real design question for the eventual WASM pass: the ~17-22 minute
one-time table build is fine for a native benchmark loop but would be a
real problem for an interactive/WASM solver if repeated per session.

### Packed + cached pairing table (cpp/pairing_dist_io.h)

Fixed the above directly, per user request (2026-09-18): the 12! pairing
distance table is now stored 4-bit-packed and cached to a file
(`cpp/pairing_dist_p4.bin`) so `phase4.cpp` and `chain1234.cpp` (which
build the IDENTICAL table — same `SIGMA_POS`/`SIGMA_NEG` move basis from
tables4.h) only pay the ~17-22 minute BFS cost ONCE; every subsequent run
of either binary just reads the ~119.75MB file in well under a second.

Two stacked memory-halvings, both implemented in the shared header:

1. **Dense reachable-half indexing** (479,001,600 raw indices ->
   239,500,800 used ones). Rather than store a separate compression
   lookup table, this exploits a clean fact about the Lehmer-code ranking
   itself: swapping a 12-permutation's LAST TWO elements always flips its
   parity, and — because the factorial place-value for that position is
   exactly `1!=1` (the only odd weight among the 12 digits; `0!` is also
   odd but its digit is always 0) — always changes the raw rank by
   EXACTLY 1. So raw ranks pair up as `(even, even+1)`, one member of
   each pair even-parity (reachable) and the other odd (unreachable), and
   `raw_rank >> 1` is therefore a collision-free dense index over exactly
   the reachable half — no separate mapping table needed, just one bit
   shift. (Reconstructing the actual reachable permutation for a given
   dense index, needed only for diagnostics, requires checking parity of
   both `2*dense` and `2*dense+1` candidates and returning the even one —
   see `pairing_unrank_dense`.)
2. **4-bit packing, capping distances at 15** (the spec's own original
   suggestion). **Correction to the user's stated assumption**: distances
   >15 are NOT rare — measured directly, true max is 18 and ~30% of
   reachable states (72.1M of 239.5M) are >=16. Capping to 15 for storage
   stays admissible regardless (storing a smaller-than-true lower bound is
   always still a valid lower bound, just less tight for that ~30%) — this
   doesn't change correctness, just slightly weakens pruning for those
   states. Proceeded exactly as asked despite the corrected assumption,
   since it's harmless.

Final size: 239,500,800 / 2 = 119,750,400 bytes (~119.75MB, matching the
"around 120MB" target exactly).

**Two future compression ideas, explicitly requested to be documented but
NOT implemented unless the current table becomes an actual blocker**:
- **Distance-mod-3 packing** (2 bits/entry instead of 4, ~60MB): store
  only `distance mod 3`, recovering the true distance at query time via a
  short greedy walk toward the distance-0 solved state (adjacent states
  always differ by exactly 1 in true distance, so `mod 3` plus the
  search's own current depth disambiguates which multiple of 3 applies).
- **Symmetry reduction** (mirror/rotational, beyond phase 1's single
  120-degree reuse) — real optimal solvers get major further table-size
  wins from the full 48-element symmetry group.

Both are listed in "Techniques NOT yet tried" below too — do not build
either preemptively; the current packed+cached table already solves the
"repeat runs are slow" problem this was meant to address.

## chain1234.cpp — unified S0->S4, four heuristic tiers

Direct extension of chain123.cpp with a 4th tier, same architecture:

```
h(state) = phase4_heuristic(state)                             if state in S3
         = phase3_heuristic(state) + COST34                      if state in S2
         = phase2_heuristic(state) + COST34 + COST23               if state in S1
         = phase1_heuristic(state) + COST34 + COST23 + COST12    otherwise
```

COST12=9, COST23=7 carried over from the 3-phase chain (chain123.cpp) as
a starting point; COST34 starts at the user's guess of 13. **Both COST23
and COST34 get re-tuned below once phase 4 is in the mix — COST23=7 was
chain123.cpp's own settled value for a 3-phase chain, but the 4-phase
chain's own boundary search lands on COST23=8, not 7** (see "COST34 x
COST23 boundary search results" below). This directly confirms, yet
again, the standing lesson: a constant settled for a shorter chain is a
starting point for the next phase's tuning, never an assumption.

**One real structural addition beyond chain123.cpp**: `State6` didn't
carry the raw `wing_slot[24]` array before (only phase 2/3's aggregate
`wing` coset value) — but phase 4's wing-pairing coordinate needs to
identify SPECIFIC pieces and look up their physical twins, which can't be
derived from the coset value alone. Added `wing_slot[24]` to `State6`,
updated via `WING_PERM` every move exactly as safely as `center_slot`
always has been (no domain restriction on maintaining the RAW array —
only DERIVING wing-pairing from it needs a guard). That guard is
`is_in_s2` specifically (matches phase4.py's `perm12_of_state`
requirement — positive/negative wing-orbit confinement, not merely
`is_in_s1`), computed lazily via `p4full::compute_pairing`, exactly
mirroring how `compute_fb2`/`compute_lr2`/`compute_ud2` are all lazily
derived from `center_slot` guarded by `is_in_s1`.

The 12!-state pairing distance table (~479MB, the dominant startup cost)
is rebuilt here under `SIGMA_POS`/`SIGMA_NEG` from tables4.h exactly as
phase4.cpp does it. First full build+sanity run reproduced phase4.cpp's
numbers exactly (`reached=239500800`, `true_max=18`, `avg=14.611`,
`max joint4(S3)=16`) — good cross-validation that the two independent
ports agree.

### Objective clarified mid-sweep: minimize length subject to MEDIAN time <= ~5s

The user's actual optimization target (stated explicitly during tuning,
2026-09-19): **shorter solutions are strictly better, as long as the
MEDIAN solve time stays under ~5 seconds** — not the mean, and not the
worst case. This matters a lot in practice because the per-trial time
distribution is heavily right-skewed (a handful of very slow trials can
double or triple the mean while barely moving the median), so `--bench`
was extended to track and print `median`/`p90`/`p99` time and length
alongside the existing mean/max (see the `all_ms`/`all_len` vectors + the
percentile block at the end of the `if (bench)` branch). **Always use
median for comparing configs going forward, not mean** — a config that
looks bad on mean/max can be perfectly fine on median, and vice versa
(this reversed at least one real decision during tuning, see below).

Also fixed mid-sweep: neither `phase4.cpp` nor `chain1234.cpp` called
`fflush`/set line-buffering consistently, so a slow run's progress could
sit completely invisible in a redirected-to-file log for many minutes,
looking indistinguishable from a hang. Added `setvbuf(stdout, NULL,
_IOLBF, 0)` as the first line of `main()` in both files — always do this
for any long-running CLI tool whose output might be redirected to a file
and checked mid-run.

### A real, measured cost of the 4-bit/capped-at-15 pairing table

Discovered directly while re-checking a previously-tested config: the
SAME exact config (COST23=7, COST34=11) that showed a roughly 2.4s median
under the ORIGINAL uncapped 479MB pairing table showed ~6.9s median under
the CURRENT 4-bit-packed, capped-at-15 table — a genuine ~3x regression,
not noise. Mechanism: capping distances >=16 down to 15 weakens the
phase-4 heuristic specifically for the ~30% of wing-pairing states that
need it (see the packing section above), and low-COST34 configs lean on
that heuristic precision more heavily (they rely on the EXACT phase-4
heuristic kicking in early and often). **This means the packing tradeoff
requested by the user is not free — it has a measurable, non-trivial
performance cost for some configurations**, which is exactly the kind of
thing to keep in mind if the mod-3 or symmetry-reduction follow-ups are
ever revisited: they'd need to weigh this same kind of admissibility-vs-
size tradeoff.

### COST34 x COST23 boundary search results

Methodology: for each COST23, find the LOWEST integer COST34 such that
median time stays under ~5s (lower COST34 -> shorter solutions but slower;
this direction was already established for COST23 alone and holds for
COST34 too), by testing candidate values and killing runs early once a
handful of trials already imply a median clearly over/under the 5s line
(no need to wait for all 100 trials — this saved a lot of wall-clock time
across the sweep; several 20+ minute-long bad configs were killed after
just a few minutes once the trend was unambiguous).

| COST23 | boundary COST34 (fails at COST34-1) | avg len | median len | median time |
|---|---|---|---|---|
| 6 | >=14 (13 already fails, ~6.4s median) | not fully explored | | |
| 7 | 12 (11 fails, ~6.9s median) | 38.53 | 39 | 3553ms |
| **8** | **11 (10 fails, ~7.8s median)** | **38.45** | **39** | **2049ms** |
| 9 | 11 (10 fails, ~7.2s median) | 39.30 | 40 | 1922ms |
| 10 | 11 | 40.16 | 41 | 2368ms |

**COST23=8, COST34=11 is the peak** — shortest length found (38.45 avg /
39 median) with comfortable median headroom (2049ms vs the ~5000ms
budget). Both directions get worse: lower COST23 needs a compensating
HIGHER COST34 to stay under the median budget, and a higher COST34 always
costs length directly (established pattern); higher COST23 (9, 10, ...)
lengthens solutions with the SAME COST34=11 boundary and no further speed
gain. Neither direction improves on COST23=8.

**Settled: COST12=9, COST23=8, COST34=11** for the full S0->S4 unified
chain. Not yet compared against a sequential 4-phase baseline (chain_test.cpp
only extends through phase 3 currently) — extending it to phase 4 would
give the proper apples-to-apples point of comparison, matching how phases
1-3 were evaluated, and is the natural next step if this chain's overall
architecture (unified vs sequential) needs to be re-justified again now
that all 4 phases are in play.

## Search method alternatives: RBFS and beam search

chain1234.cpp's search algorithm is now pluggable via `--search
{ida,rbfs,beam}` (default `ida`), dispatched through a thin `solve()`
wrapper (`solve_ida`/`solve_rbfs`/`solve_beam`) — all three share the exact
same `heuristic()`/`is_in_s4()` and the same canonical-move-order pruning
table (`ALLOWED_LIST`), so they're a clean apples-to-apples comparison of
search STRATEGY alone, holding the heuristic fixed.

- **RBFS** (Korf/AIMA recursive best-first search): O(depth) memory like
  IDA*, but explores in best-first order (lowest backed-up f=g+h) instead of
  IDA*'s fixed canonical move order within a flat depth threshold. Each call
  generates ALL of the current node's successors and their h-values up
  front (unlike IDA*'s lazy one-at-a-time expansion), then repeatedly
  recurses into whichever successor currently has the lowest backed-up f,
  backtracking and remembering the f it returned whenever that subtree's
  cost exceeds the second-best sibling's f. Implementation note: the
  caller-supplied `node_f` parameter is provably always <= `f_limit` for
  every NON-root call (derivation: the caller only recurses into the
  lowest-f successor after confirming its f <= the caller's own f_limit,
  and the new f_limit passed down is `min(old_f_limit, second-lowest-f)` >=
  that successor's own f) — so the `node_f > f_limit` early-exit check only
  ever fires at the root, mirroring IDA*'s dfs() rejecting a root whose own
  heuristic already exceeds the search budget.
- **Beam search**: level-synchronous local beam search — expand every
  surviving state one move deeper, keep only the `--beam-width` lowest-h
  survivors (via `std::nth_element`, not a full sort) before continuing.
  Bounded memory, no iterative deepening, but **incomplete**: a good state
  can get pruned out of the beam and the search then fails outright even
  though a solution exists within `--max-len`. No de-duplication of
  identical states within a level (state space is astronomically larger
  than any practical beam width, so collisions are rare and not worth the
  bookkeeping).

### Measured comparison (COST12=9, COST23=8, COST34=11, seed 2024, 100 trials)

| Method | Solved | Median time | p90 time | Median len | Throughput |
|---|---|---|---|---|---|
| ida (baseline) | 100/100 | 1.70s | 5.72s | 39 | 17.5M nodes/s |
| rbfs | 100/100 | 4.97s | 15.6s | 39 | 0.34M nodes/s |
| beam, width=1000 | 11/100 | 0.38s* | 0.45s* | 49* | 1.35M nodes/s |

*Beam's time/length stats are only over the 11 successful trials; the other
89 found no solution at all (`TRIAL N: NO SOLUTION`).

**Findings:**
- RBFS finds solutions of IDENTICAL median length to IDA* (both are
  exhaustive within their budget, same heuristic, same goal) but is ~3x
  slower in wall-clock median. **Important correction to an earlier draft of
  this section**: the naive explanation ("51x-lower g_nodes/s throughput")
  is misleading, because `g_nodes` counts a fundamentally different unit of
  work in each algorithm. Added a `g_heuristic_calls` global counter
  (incremented at the top of `heuristic()`, printed as `hcalls=...` in
  `--bench` trial lines) and ran both algorithms on the IDENTICAL scramble
  (seed 2024, trial 0: scramble=118, h0=36, solution len=40) to get a
  same-problem comparison:
  | | node visits | heuristic() calls | time | calls/node |
  |---|---|---|---|---|
  | ida | 29,648,929 | 29,647,122 | 1905.62ms | ~1.0 |
  | rbfs | 2,378,406 | 48,890,308 | 3588.03ms | ~20.6 |

  IDA*'s `dfs(s)` calls `heuristic(s)` on the node it's already standing on
  (1 call per node, hence the ~1:1 ratio — the small gap is nodes that
  short-circuit on the goal-test or depth-exhaustion check first). RBFS's
  `rbfs(s)` never re-evaluates `s` itself (its parent already scored it
  during lookahead) — instead it eagerly calls `heuristic()` on ALL ~20 of
  `s`'s children up front (one-ply lookahead in every direction, every
  node) to decide which to descend into. That eager lookahead is exactly
  where the ~20.6 calls-per-node ratio comes from, and it means RBFS visits
  12.5x FEWER distinct nodes (its best-first ordering really is smarter
  than IDA*'s fixed canonical-axis order — its genuine advantage) but does
  **1.65x MORE total heuristic work** overall (48.89M vs 29.65M calls) — the
  "fewer nodes" framing was misleading; a node means ~20x more work under
  RBFS. On top of that, RBFS's raw throughput per heuristic call is also
  ~12-14% slower (13.63M/s vs 15.56M/s) from its bookkeeping overhead: a
  heap-allocated `std::vector<Succ>` per node holding up to ~20 full
  State6 copies, plus linear best/second-best scans over it (possibly
  repeated several times per node while backtracking between siblings) —
  IDA*'s `dfs()` never allocates anything. Multiplying the two factors
  (1.65x more calls x 1.14x slower per call ~= 1.88x) lands almost exactly
  on the measured wall-clock ratio for this trial (3588/1906 ~= 1.88x).
  Bottom line: RBFS trades a 12.5x reduction in visited states for a 20x
  increase in per-state lookahead cost, and with this heuristic's branching
  factor (~20 legal moves after canonical-order pruning) that trade loses.
  It would only start winning if the heuristic were expensive enough that
  avoiding wasted evaluations mattered more than the lookahead tax, or if
  the branching factor were much smaller.
- Beam search at width=1000 is fast per attempt (~380ms) but fails 89% of
  the time — the heuristic misleads it into pruning the only viable path
  off the beam long before reaching the goal. When it succeeds, solutions
  are noticeably longer (42-60 moves vs ~39) since there's no admissibility
  guarantee at all.
  Tried width=2^18=262144 next (one trial, same scramble=118/h0=36 as
  above, then killed early per usual practice once the trend was clear):
  found a length-39 solution (matching IDA*'s quality exactly — wide enough
  to stop losing the good path) but took **153.0 seconds** (183.7M nodes,
  1.20M nodes/s) — ~30x over the ~5s budget, and node count scaled almost
  linearly with beam width (183.7M vs ~1M at width=1000, tracking the 262x
  width increase). Throughput also dropped slightly (1.20M/s vs 2.5M/s at
  width=1000), plausibly from `nth_element`'s O(n) cost over ~4.7M
  candidates/level plus heavier allocator churn (each candidate carries its
  own copied path vector). So beam search can't hit both "always finds a
  solution" and "competitive speed" here — somewhere between 1000 (89%
  failure) and 262144 (30x over budget) there's a completeness/speed knee,
  but it's very unlikely to beat IDA*'s 1.7s median regardless; not swept
  further.
- **IDA* remains the right default** for this heuristic/state-space
  combination. Kept RBFS and beam as `--search` options for future
  experimentation (e.g. if a much cheaper phase-4 heuristic ever makes
  RBFS's per-node cost competitive, or if beam width tuning closes the
  completeness gap enough to be worth the length tradeoff).

### CLI reference (chain1234.cpp)

```
--search {ida,rbfs,beam,ida-inadm}   default ida
--beam-width N             default 1000, beam only
--cost12 / --cost23 / --cost34 N
--heuristic {max,sum,weighted}   combine_phase4's mode for h_pair vs h_udfb
--w1 / --w2                weighted mode only
--max-len N                default 60 -- raise this when testing large
                           COSTxy values, since h0 can exceed the default
                           cap before the outer threshold loop ever runs
                           (silently "succeeds" as 100/100 NO SOLUTION)
--bench                    100-trial benchmark (median/p90/p99 time+length;
                           trial lines also print hcalls=... -- total
                           heuristic() calls, the fair cross-algorithm work
                           unit, since g_nodes means different things per
                           search method -- see "Search method alternatives")
```
Interactive REPL (typing moves at the `chain1234>` prompt) also respects
whichever `--search`/cost/weight flags were passed at startup.

## Search method alternatives: IDA* for inadmissible heuristics

A fourth `--search` option, `ida-inadm` (`solve_ida_inadmissible`/
`dfs_inadmissible` in chain1234.cpp), targets a specific weakness in vanilla
IDA*: it implicitly assumes f=g+h is roughly non-decreasing along a path
(true for a CONSISTENT heuristic), but THIS heuristic's flat additive tier
constants (COSTxy) make that false -- crossing a phase boundary (otherwise
-> S1, S1 -> S2, S2 -> S3) makes an entire COSTxy term vanish in a single
move, so f can suddenly DROP by (COSTxy - 1) at exactly that step. Vanilla
IDA* just treats the post-drop node as "still allowed under the current
threshold" and explores its subtree with the same generous depth budget the
rest of the pass has, even though the heuristic there just became far more
trustworthy (an exact/near-exact joint-table lookup instead of a padded
flat guess).

**The fix, v1 (user-specified, later found to be wrong):** whenever a
child's f falls STRICTLY below the threshold currently governing the search
(a "drop"), don't fold it into the current pass -- immediately re-run
iterative deepening LOCALLY on that subtree, starting at its own (lower) f
and escalating by 1 each time, capped at the threshold the parent context
allows.

**v1's bug, per the user's own correction:** starting the nested sweep at
the child's raw f trusts the ENTIRE observed drop as if it were genuine
tightened information. But the drop can include, on top of the one KNOWN
fixed COSTxy shed by the tier crossing, some unrelated slack from the
non-flat part of the heuristic (h1' vs h2' differing at the crossing point,
endgame-table boundary quirks, etc.) -- v1 credits ALL of that, not just the
provably-real part.

**The fix, v2 (corrected):** the heuristic "shouldn't go all the way down,
just down by the amount of the inadmissibility." Added
`tier_inadmissibility(s)` -- returns the exact flat additive total
heuristic() bakes in for whatever tier `s` resolves to (g_cost34+g_cost23+
g_cost12 if "otherwise", g_cost34+g_cost23 if S1, g_cost34 if S2, 0 if S3;
mirrors heuristic()'s own tier detection). The nested sweep for a
parent->child edge now starts at
`max(f(child), local_threshold - (tier_inadmissibility(parent) -
tier_inadmissibility(child)))` instead of at `f(child)` directly -- i.e. only
the EXACTLY-KNOWN amount shed by that specific tier crossing gets credited,
capped so it never starts below f(child) itself (which would be pure
wasted iterations, always failing immediately by definition). Concretely:
the same tier-crossing (fixed shed amount, say 7) starting a threshold=30
pass begins its nested sweep at 23, but starting a threshold=31 pass begins
it at 24, not 23 again -- the correction scales with the CURRENT governing
threshold, not with the node's absolute value. Degenerates cleanly to a
single ordinary iteration when no tier crossing explains the drop (nothing
to correct for). Verified correct: 100/100 trials at the settled constants.

### Measured comparison (100-trial or partial-sample bench, seed 2024)

| Config (12/23/34) | Algorithm | Median time | Avg len | Sample |
|---|---|---|---|---|
| 9/8/11 | ida (baseline) | 1.70s | 38.45 | 100/100 |
| 9/8/11 | ida-inadm **v1 (buggy)** | 5.65s | 37.48 | 100/100 |
| 9/8/11 | ida-inadm **v2 (corrected)** | **4.20s** | **37.48** | 100/100 |
| 9/8/**10** (lower) | ida-inadm v2 | ~6.1s+, climbing (outliers to 17.8s) | ~38.2* | 16 (killed early) |
| 9/8/**12** (higher) | ida-inadm v2 | ~1.97s | ~40+ | 47 (killed early) |
| 9/**9**/11 (higher) | ida-inadm v2 | ~2.24s | ~40 | 39 (killed early) |

*Matched-trial averages (same 15 seed-2024 scrambles across configs), not
directly comparable to the full-100-trial 37.48 baseline average -- only
the relative direction across configs is meaningful here.

**Findings:**
- v2 is a strict improvement over v1 with NO quality loss: identical
  average solution length (37.48, confirmed trial-by-trial identical on
  matched scrambles) but ~26% faster median (4.20s vs 5.65s) and fewer
  nodes/heuristic calls per trial throughout -- exactly as expected, since
  v2 eliminates the wasted early nested-sweep iterations that v1's
  overly-optimistic starting threshold caused.
- **This flips the earlier verdict entirely.** v1 was 13% over the ~5s
  budget, making vanilla IDA* the safer default despite v1's shorter
  solutions. v2's 4.20s median is comfortably UNDER budget (~2.5x headroom
  before hitting 5s) while still giving the shortest solutions of anything
  tested -- **v2 at the unchanged settled constants is now the best result
  seen for this heuristic overall.**
- **User's hypothesis going in was that the optimal COSTxy's would be
  slightly HIGHER (0-1) under ida-inadm. Still doesn't hold, even under v2.**
  Raising either COST23 or COST34 by 1 collapses the median back to
  vanilla-IDA*-like speed (~2.0-2.2s) while making solutions longer than
  even vanilla IDA*'s own baseline (~40+ vs 38.45) -- trading away the
  2.5x headroom for nothing, since the baseline wasn't even using that
  headroom inefficiently. Lowering COST34 to 10 DOES shave off roughly
  another move on a matched-trial basis (consistent with the general
  established lower-COST-is-shorter-but-slower pattern), but pushes the
  median back over budget (already 15-18s outliers within 16 trials).
- Practical takeaway: for ida-inadm v2, the settled constants (9/8/11) are
  very close to optimal as-is -- both directions of adjustment make things
  worse or over budget. No further re-tuning needed; this is a genuine win
  obtained "for free" at the existing settled constants, not a reason to
  re-run the boundary search.

### v3: eliminating cross-outer-pass redundancy (with a caught bug)

v2 redoes almost the ENTIRE nested escalation on every successive OUTER
threshold pass, even though consecutive passes' windows overlap almost
completely (threshold=30 tries [23..30]; the very next pass at
threshold=31 tries [24..31] -- 7 of those 8 values were already
exhaustively tried, and failed, one pass ago). User's follow-up idea: track,
in O(1) per node, whether the threshold currently governing a node is the
SMALLEST value its enclosing escalation context has EVER tried -- if so, do
the full escalation; if not, the lower part of the range was already
covered by an earlier pass, so only try the ONE new value at the top.

**Draft 1 (buggy, caught empirically):** threaded a `chain_first` boolean
through the recursion -- true at the root only when `threshold==h0`, and at
a drop edge, inherited as true by the child ONLY for the lowest t in that
edge's own escalation (`chain_first && (t == loop_start)`). This looked
right but is WRONG: it only tracks first-ness for the CURRENT escalation's
own sequence, and misses a second, independent source of newness -- an
ORDINARY (non-drop) edge, reached under a parent whose OWN chain_first is
already false, can still have a child that's reachable for the very first
time purely because the outer threshold grew by 1, with no tier crossing
on that specific edge at all. At exactly that edge f(child)==t (not
reachable at t-1), and this must count as "first" for THAT edge regardless
of what the parent's status was, because any FURTHER drop beneath it would
otherwise wrongly inherit "not first" and skip an escalation it had never
actually run. Caught by comparing draft-1's output against v2's on
identical scrambles (seed 2024): **trial 5 (scramble=112) and trial 7
(scramble=44) both found a length-39 solution where v2 correctly found
38** -- a real regression, not noise, proving draft 1 sometimes searches
LESS than it needs to.

**Draft 2 (fixed, verified correct):** `child_first = chain_first ||
(f(child) == t)` -- OR in the local per-edge check rather than relying on
inheritance alone. Re-verified against v2 on the same 15 matched scrambles:
solution lengths now identical on EVERY trial, including the two that draft
1 got wrong (both correctly 38 again). Full 100-trial run: avg len=37.48,
EXACTLY matching v2 to two decimals -- confirms the fix is a pure
optimization with zero quality change, as originally intended.

**Measured result:** median time 4.20s (v2) -> **3.54s** (v3, ~16% faster),
p90 14.39s -> 12.43s. Smaller than the "eliminate 7/8 of the redundant
work" argument implied, though: avg nodes barely moved (2.045M -> 2.034M,
~0.6% fewer), and most INDIVIDUAL trials show IDENTICAL node/hcall counts
between v2 and v3. Reason: this heuristic's h0 typically sits only 3-5
below the actual solution length, so solve_ida_inadmissible's outer loop
only ever runs a handful of passes -- there isn't much cross-pass
redundancy to eliminate in the first place. Most of the total work comes
from the single, final (successful) pass's own exploration, which v3
doesn't touch at all; the modest net gain comes from the minority of
trials that do reach several outer passes (e.g. trial 6, 11, 13 each
showed a real node-count reduction).

`ida-inadm` now points at this v3 implementation (replacing v2 in place,
same as v1->v2's replacement) -- verified at least as good on every
tested trial, strictly better on some, with no observed downside.

### v4: O(1) drop_credit with zero approximation risk

Comparing ida-inadm against vanilla IDA* (COST12=9, COST23=8, COST34=11,
100 trials each) surfaced a real per-node cost problem: ida-inadm visits
~19.6x FEWER nodes than vanilla IDA* (2.03M vs 39.8M) -- its threshold
correction really does prune much more aggressively -- but each node it
visits costs ~45x more (throughput 0.39M nodes/s vs 17.47M nodes/s),
making the overall search ~2.3x slower despite the huge node-count win.
Root cause: `tier_inadmissibility(ns)`, called once per child to compute
`drop_credit`, REDUNDANTLY re-derives which tier `ns` is in (recomputing
`compute_fb2`/`compute_lr2` from scratch) even though `heuristic(ns)`,
called on the very same line for `f`, ALREADY determined that tier as
part of its own internal is_in_s1/is_in_s2/is_in_s3 chain -- and then
immediately throws that information away.

**User's first proposal:** pass the parent's h down and compute
`drop_credit = max(0, parent_h - child_h)`. Traced through why this isn't
quite right: on an ORDINARY (non-crossing) move where h drops by the
typical 1 unit as g increases by 1, `parent_h - child_h = 1` even when
NOTHING tier-related happened -- since a real tier crossing's exact,
provable signal is a difference in tier_inadmissibility specifically, not
in h itself. This would trigger spurious 2-step escalations on ordinary
progress-making moves whenever the parent has any slack (the common case),
not just at genuine crossings.

**User's corrected proposal:** `drop_credit = max(0, f_parent - f_child)`
instead of h-based. Since f=g+h and g increases by exactly 1,
`f_parent - f_child = (h_parent - h_child) - 1`, which correctly nets to 0
on that same typical ordinary move (fixing the h-based version's
off-by-one). Algebraically this equals
`[TI_parent - TI_child] + [base_parent - base_child - 1]` -- the first
bracket is the exact, wanted signal; the second is noise that's usually 0
but not always, since the phase 1/2 endgame tables have a hard falloff at
depth 3 (flat fallback outside that radius), so a move crossing into that
radius can shift `base` by 2-3 in one step with no tier crossing at all.
Worked out that UNDER-crediting relative to the exact TI difference is the
risky direction (structurally the same mistake as the v3-draft-1 bug --
skipping a threshold IDA* should try first can make the search settle for
a longer solution found later at a bigger budget), while OVER-crediting is
provably harmless (the loop always still tries local_threshold as its
complete final step regardless; extra low attempts only help or waste a
little time). f_parent-f_child can drift either direction depending on the
specific move's base-heuristic behavior, so it carries a small
theoretical risk that the exact lookup doesn't.

**The actual fix implemented:** rather than approximating via h or f
differences at all, gave `heuristic()` an optional `out_tier` parameter
that it fills in for free as a byproduct of its own existing
is_in_s1/is_in_s2/is_in_s3 checks (0=otherwise, 1=S1, 2=S2, 3=S3). A tiny
`tier_inadm_from_code(tier)` switch replaces `tier_inadmissibility()`
entirely -- an O(1) lookup, no fb2/lr2 recomputation, and EXACT (not an
approximation of anything). This also incidentally eliminated a second
redundancy: the parent's own tier used to require a SEPARATE
`tier_inadmissibility(s)` call after the `heuristic(s)` prune check; now
both come from one call.

**Verification:** re-ran the SAME 15 matched scrambles (seed 2024) through
v3 (tier_inadmissibility calls), the new tier-code lookup, AND the user's
f_parent-f_child approximation -- all three gave IDENTICAL solution
lengths on every trial. Full 100-trial runs: f-based avg len=37.48 (exact
match to 2 decimals) and avg nodes=2,033,646 vs the tier-lookup's
2,034,284 (0.03% difference) -- confirming the theoretical under-crediting
risk doesn't materialize in practice for this heuristic; the endtable
falloff noise is apparently too small/rare to matter here. Kept the exact
tier-lookup as the shipped implementation anyway, since it's equally O(1)
and carries a strictly stronger guarantee (correct by construction, not
by empirical luck on this test set) -- no reason to trade a proof for an
approximation that happens to test the same.

Wall-clock timing across these variants was too noisy (this session's
system load varies visibly run to run, sometimes 20-30% swings on
IDENTICAL node counts) to draw a confident "v4 is X% faster than v3"
number from a single run of each; the node/hcall counts (unaffected by
system noise) are the reliable signal, and those confirm v4 is behaviorally
identical to v3 while doing strictly less redundant per-node work.

### v5: the REAL fix -- g_nodes was never a fair unit, plus one genuine 2x

User pushed back hard on the "~45x per-node slowdown" claim from the
IDA*-vs-ida-inadm comparison, correctly smelling a node-counting bug rather
than a real per-node cost. Right to be suspicious: v4's f-based test run
(previous section) already proved `tier_inadmissibility`/its O(1)
replacement was NEVER the dominant cost, since removing it entirely left
throughput unchanged (0.39M nodes/s both ways) -- a fact noted at the time
but not followed up on until now.

**Root cause #1 (the real "node-counting bug"):** vanilla `dfs()` recurses
UNCONDITIONALLY for every one of the ~20 canonically-allowed moves, and
the CALLEE's own single self-check (right after ITS OWN `g_nodes++`)
decides to prune -- so every attempted move, pruned or not, gets its own
`g_nodes` increment and exactly one `heuristic()` call (matching the
observed ~1:1 hcalls:nodes ratio for vanilla). `dfs_inadmissible` HAS to
evaluate `heuristic(ns)` for every candidate move up front, in the
PARENT's own loop, to compute `drop_credit` before deciding how to
recurse -- but candidates that fail `f > local_threshold` get `continue`'d
away WITHOUT ever triggering a recursive call, so they consume a
`heuristic()` call but NEVER increment `g_nodes`. `g_nodes` therefore
counts a fundamentally different, SMALLER population in the two functions
(vanilla: every attempted move; dfs_inadmissible: only survivors of the
parent-side prune) -- not a bug in the sense of wasted computation, but a
genuinely unfair comparison unit, exactly as suspected. Arithmetic check:
observed hcalls/nodes ratio was ~21.5; modeling it as
`nodes * (1 + candidates_per_node)` implies candidates_per_node ~= 20.5 --
matching this project's ~20-27 canonically-allowed moves almost exactly,
with no need to invoke tier_inadmissibility's cost at all.

**Root cause #2 (a real, fixable 2x):** on top of that accounting
artifact, every child that DID survive to become its own node paid for
`heuristic(ns)` TWICE -- once in the parent's loop (to get `f` for the
survive/prune decision) and again at the very top of ITS OWN recursive
call (the standard `h > threshold - g` self-check, where `s` is now bound
to that same `ns`). Fixed by threading the already-computed `h`/`tier`
down as extra parameters instead of recomputing -- `dfs_inadmissible` now
takes `(s, g, local_threshold, last_move, chain_first, h, tier)`, and
`solve_ida_inadmissible` computes `h0`/`tier0` once at the top for the
root call.

**Verified correct:** all 15 previously-matched scrambles gave IDENTICAL
solution lengths before and after; full 100-trial run: avg len=37.48,
EXACTLY matching every prior verified-correct version, and avg
nodes=2,034,284 -- also EXACTLY identical to before the fix, proving the
total amount of search work is provably unchanged (only its per-call cost
dropped).

**Measured result:** hcalls dropped by only ~4.6% (removing exactly one
redundant call per surviving node -- trial 0's reduction, 1,666,617, matched
its node count, 1,666,618, to the last digit, confirming the mechanism
directly) -- but wall-clock time dropped 2.5-3x on most trials (e.g. trial
0: 4334ms -> 1607ms). The gap makes sense: the eliminated call always
belonged to a SURVIVOR, and survivors are disproportionately deep S1/S2/S3
states where heuristic() does its most expensive internal work (an
`unordered_map::find()` for the phase-2 endgame table, joint-table
lookups) rather than the cheap array-only lookups most shallow, soon-to-be-
pruned candidate moves hit -- so a small fraction of calls removed carried
a much larger fraction of the actual cost. Median time across the full
100-trial run: 3.05s, down from 3.54s (v3/v4) -- continuing the
1/8/11-config progression 5.65s (v1) -> 4.20s (v2) -> 3.54s (v3/v4) ->
**3.05s (v5)**. One severe single-trial outlier (73.99s, up from ~34s
before) inflated the mean/max/p99 figures, but since avg nodes across all
100 trials is EXACTLY identical to pre-fix, the total search work
performed is provably the same -- the outlier is system noise (this
environment has shown 20-30% timing swings on identical workloads all
session), not a regression, and the median is the metric to trust here per
this project's own established convention for skewed distributions.

**Corrected final comparison against vanilla IDA*:** median 3.05s vs
vanilla's 1.70s -- ida-inadm is now ~1.79x slower (not the wildly
overstated ~45x from the original g_nodes/s-based comparison), for its
~2.5% shorter average solution (37.48 vs 38.45). The REMAINING gap is a
genuine, largely irreducible cost of the algorithm's design: it needs
every candidate child's f value BEFORE deciding whether/how far to
escalate, unlike vanilla's "try first, prune inside the callee" laziness --
there's no way to make that lookahead free without abandoning the
escalation mechanism the whole feature depends on.

`ida-inadm` now uses this v5 implementation.

### Sanity check: extreme COST values should degrade to sequential-like behavior, not hang

User's request: run with extremely high COST12/23/34 and confirm the search
stays fast (roughly sequential-baseline-like) instead of exploding, as a
correctness check on the whole drop-detection mechanism -- under massive
constants, ANY bug in "detect the crossing, credit exactly the known
amount, escalate accordingly" should be far more painfully exposed than at
the tuned settled values.

**Bug caught along the way:** `--bench`'s trial loop hardcoded
`solve(start, 60, sol)`. At COST12=COST23=COST34=100, h0 balloons to
~304-308 (all three flat constants stack in the "otherwise" tier). Since
`solve_ida_inadmissible`'s outer loop is `for (threshold=h0; threshold<=
max_len; ...)`, and h0 already exceeds the hardcoded max_len=60, the loop
body never executed even once -- every one of the 100 trials returned "no
solution" INSTANTLY, which looked deceptively like a fast, successful run
at first glance. Fixed by adding a `--max-len N` CLI flag (default 60,
threaded through both `--bench` and the interactive REPL) and rerunning
with `--max-len 350`.

**Result, 12 sampled trials (killed early once the trend was clear -- this
is a stress test, not a config to tune):** times ranged 0.4s-34s (one
heavier outlier at 129s, consistent with this project's usual heavy-tailed
timing distribution), median ~=8.1s. Solution lengths 45-53 moves, notably
longer than the ~37-40 seen at tuned constants. This is exactly the
expected signature of correct behavior: massive constants effectively
disable cross-phase joint optimization (every phase boundary now forces a
full local re-escalation that discards nearly all of the artificial
inflation), pushing the search toward sequential-style solving -- longer
solutions, since sequential solving can't trade moves across phase
boundaries the way joint optimization can, but still bounded and fast
rather than needing hundreds of wasted outer-threshold iterations (which
is what vanilla IDA* would need here, since its threshold has no mechanism
to recognize that most of h0's ~300 is a stale guess the moment a phase
boundary is crossed).

### Diagnostic: how many phase-crossing candidate edges get examined per trial

User's question: how many "not yet in tier X" -> "now in tier X" candidate
moves does the search examine per phase, on average? Added 4 counters
(`g_edges_into_s1/s2/s3/s4`) incremented in dfs_inadmissible's move loop,
BEFORE the f<=local_threshold prune (counting every move CONSIDERED, not
just ones that survive to be explored further).

**A second real overhead bug caught in the process:** the phase-4 counter
was first implemented as `if (child_tier==3 && is_in_s4(ns)) ...`, which
redundantly re-derives the ENTIRE tier chain from scratch inside is_in_s4
(is_in_s3 -> is_in_s2 -> is_in_s1, all over again) even though
`child_tier==3` already told us all of that. Given S3-tier candidates are
extremely common (~154K/trial, see below), this alone made the whole
100-trial run's median jump from v5's validated ~3s to ~7.4s. Fixed for
free: heuristic()'s phase-4 branch already computes `combine_phase4(h_pair,
h_udfb)`, and both h_pair/h_udfb are BFS distances seeded at exactly 0 on
the solved (ud,fb)/pairing states -- so `child_h==0` (a value already in
hand, zero extra computation) is EXACTLY equivalent to `is_in_s4(ns)` in
all three heuristic-combination modes (max/sum/positive-weighted-sum are
all 0 iff both inputs are 0). Re-verified identical solution lengths and
edge counts after the fix, with timing back down to ~1.9-3.0s median
(matching or beating v5's own numbers, well within this session's usual
run-to-run noise).

**Results (settled constants, 100 trials):**

| Phase completed | Avg candidate edges checked per trial |
|---|---|
| Phase 1 (-> S1) | 303 |
| Phase 2 (-> S2) | 461 |
| Phase 3 (-> S3) | 154,360 |
| Phase 4 (-> S4, goal) | 1 |

Phase 3 dwarfs the others by ~300-500x -- by the time the search is deep
in S2 territory it has already spent a lot of effort maneuvering LR/FB
permutations, and revisits "in S2, not yet in S3" states many times over
while hunting for the specific move that completes phase 3. Phase 4's
count of exactly 1 is structural, not informative: the search returns the
instant it finds ANY S4-satisfying state, so it can never exceed 1 on a
successful run.

## chain124.cpp — combining phases 3+4 into a single S2->S4 solver

**User's motivation:** phase 3 (S2->S3) is tiny (max 12 moves, per
joint3's own sanity print), but the OLD phase 4 (S3->S4) -- the dominant
majority of the total solve length -- was forced to stay confined to
S3_MOVES (15 moves, no Uw2/Fw2 at all) for its ENTIRE duration, just to
preserve the LR/FB-permutation invariant phase 3 had JUST spent a handful
of moves establishing. Combining the two into one S2->S4 phase, searched
with S2's own 21-move generator set (P2_NUM_MOVES, from tables2.h --
U/D/R/L/F/B at all 3 powers PLUS Uw2/Rw2/Fw2) keeps the two wide half-turns
available for the entire tail of the solve instead of just the first few
moves of it.

**A correctness gap the old design had entirely missed:** PLL parity.
Without it, roughly half of all scrambles wouldn't actually reduce to a
genuine S4 state (e.g. two corners swapped) -- the old phase3/phase4 split
never checked this at all.

### The three new coordinate systems -- and the central lesson of this whole feature

The user specified: a 12!/2 wing-pairing table (same coordinate as before,
now PACKED 2 NIBBLES PER ENTRY -- low nibble = min distance using an EVEN
total count of {Rw2,Uw2,Fw2}, high nibble = ODD count, both capped at 15,
~240MB), and a 2520x2520x24 center-joint table (~152MB) where 2520=8!/16
is a coset-rank reduction of the UD/LR permutation spaces and 24=96/4 a
further reduction of FB's existing 96-element S2-boundary coordinate.

**The reduction groups initially guessed from an abstract "label centers
0-3/4-7 clockwise" description were WRONG**, and this was only caught by
insisting on real-move verification rather than trusting the abstract
worked example:
- UD's real generators (derived from actual `Uw2`/`U2`/`D2`/`U`/`D` move
  data via `CENTER_PERM`): `U` -> value-cycle `(2 3 7 6)`, `D` -> `(0 4 5
  1)` -- orbits `{2,3,6,7}` and `{0,1,4,5}`, NOT the naively-guessed
  `{0,1,2,3}`/`{4,5,6,7}`.
- LR's real generators: `L` -> `(0 1 3 2)`, `R` -> `(4 6 7 5)` -- same
  label grouping as guessed, but a DIFFERENT rotation order.
- FB's real generators (also corrected once, by the user, after an initial
  wrong "0-3/4-7" split): evens `{0,2,4,6}` -> `(0 6)(2 4)`, odds
  `{1,3,5,7}` -> `(1 7)(3 5)`.

Every one of these was cross-checked against independently-established
ground truth before being trusted: UD/LR against the FULL 40320-space
collapsing to exactly 2520 classes of uniform size 16 AND the *existing*
`UD_SOLVED_INDICES_S4`/`LR_SOLVED_INDICES` tables collapsing to a SINGLE
canonical class each; FB against the real 96-element `FB_SOLVED_INDICES`
set collapsing to exactly 24 classes of 4 AND a genuine scramble-
equivalence test (`F2 B2 D2 U Rw2` must give the identical ID to `Rw2`
alone -- confirmed exactly, both landing on class 15). **Lesson for any
future coordinate-system work in this project: an abstract worked example
only validates the algorithm's MECHANICS, never whether it's using the
right group for this specific codebase's actual label/slot conventions --
only cross-checking against real move data and already-established
`*_SOLVED_INDICES` tables catches a wrong-group bug.**

Implementation detail that made the big BFS tractable: rather than track
full 8!-permutations during the 152M-state center-joint BFS, small
transition tables (`UD2520_TRANS[2520][21]`, `LR2520_TRANS[2520][21]`,
`FB24_TRANS[24][21]`) are built ONCE from each reduced class's canonical
representative, then the BFS operates purely on the reduced coordinate
directly -- valid because the reduction groups are pure VALUE-relabelings
(commute with move application, which acts on positions). The wing-pairing
table's own per-move 12-element transition (`SIGMA_POS_P2`/`SIGMA_NEG_P2`)
is derived directly from `WING_PERM`+`POS_SLOTS`/`NEG_OF` at startup,
mirroring phase4.py's `wing_move_index_perms` exactly, adapted from the
old 15-move S3_MOVES to the new 21-move P2 set.

### PLL parity -- no corner tracking needed at all

PLL parity = parity(12 positive wings) XOR parity(corners). Positive-wing
parity is computed on demand from the EXISTING `wing_slot` array (well-
defined only once `is_in_s2` holds, matching positive/negative wing-orbit
confinement). Corner parity has a clean characterization the user
supplied: **it toggles on every quarter turn (any move NOT ending in "2"),
and is unaffected by half turns** -- so `State6` just carries one new int
field, `corner_parity`, XORed with `IS_QUARTER_TURN[m]` on every move from
the very start of the scramble. No corner state representation needed
anywhere in the codebase.

### Table build results (verified, now cached to disk)

- Sanity checks (fast, no BFS) run BEFORE either big build and would abort
  early on a coordinate-system bug: `CENTER2520_UD/LR[identity]==0`,
  `FB24_FROM_FB96RANK[identity's 96-rank]==0`, `U^4` and `Uw2^2` both
  cycle back to UD-2520 class 0. All passed on the first real build.
- Center-joint table (152,409,600 entries): built in 68.3s. Cached to
  `cpp/center_joint_p24.bin`.
- Wing-pairing+parity table (239,500,800 entries, doubled-state BFS over
  479M (dense_idx, RwUwFw-parity) pairs): built in **41.4 minutes** (vs the
  old phase4's 17-22 min for a smaller, single-valued, 15-move version --
  expected, given ~2x the states and a wider move set). Cached to
  `cpp/pairing2_dist_p24.bin`. ONE-TIME cost; both files load from cache in
  under a second on subsequent runs.
- `heuristic(solved)==0`, `is_in_s4(solved)==yes` -- correct.
- Total live memory: 763.4 MB (152.4 + 239.5 + the unchanged phase1/phase2
  tables), comfortably under the ~4GB hard ceiling, above the "ideally
  <=500MB" soft target -- an explicit, accepted tradeoff per the user's own
  size estimates going in.

### Bullet 1 (plain IDA*, no ida-inadm, no weight): confirmed too slow

Trial 0 (scramble=118, the same seed-2024 scramble used throughout this
whole project's benchmarking): **~5 minutes, 1.42 BILLION nodes**, but
found a genuinely excellent 35-move solution (shorter than the ~37-40 the
old split-phase chain1234.cpp gets -- direct evidence the Uw2/Fw2 access
is paying off on solution QUALITY, exactly per the original motivation).
Killed before running further trials -- one data point was already
decisive per the "don't wait hours" instruction.

### Bullet 2 (ida-inadm + weight scaling): in progress, cost-tuning alone already winning big

Ported ida-inadm from chain1234.cpp's fully-corrected v5 design (O(1)
`out_tier` tier detection, `chain_first` propagation fix, tier_inadm
lookup) to this solver's simpler 2-boundary tier structure (0=otherwise,
1=S1, 2=S2/S4-combined -- one fewer boundary than chain1234's 3, since S2/
S3/S4 are now one tier). `--weight` scales h WITHIN the combined tier only
(`ceil(weight*h)`), independent of the tier-crossing drop_credit mechanism
(which is based purely on the unweighted COST12/COST24 constants and
unaffected by weight).

**Results so far, all on scramble=118 (h0/solution length vary slightly
with COST24 since it changes which tier a given intermediate state
resolves to):**

| Config | Time | Nodes | Solution len |
|---|---|---|---|
| ida-inadm, weight=1, COST24=13 (initial guess) | 5:59 | 73.4M | 35 |
| ida-inadm, weight=**2**, COST24=13 | **>10 HOURS, killed** | -- | -- |
| ida-inadm, weight=1, COST24=**14** | **2:49** | **11.65M** | 35 |
| ida-inadm, weight=1, COST24=15 | 3:33 | 12.99M | 36 |
| ida-inadm, weight=1, COST24=14, COST12=8 | killed >10 min, inconclusive | -- | -- |

**Key finding, matching the user's own instinct exactly**: a single-unit
bump to COST24 (13->14) cut the time by more than half and nodes by 6.3x,
with NO quality loss (identical 35-move solution) -- a far bigger and
cheaper win than weight scaling. COST24=15 is WORSE than 14 on BOTH time
and length (non-monotonic -- 14 looks like it's near a local sweet spot,
not partway up a monotonic ramp), consistent with this project's
established general pattern (COSTxy tuning has interacting sweet spots,
never assume monotonic).

**The weight=2 result is a genuine, not-yet-understood red flag, not just
"needs more tuning"**: going from weight=1 (6 min) to weight=2 made things
roughly 100x WORSE (>10 hours), which is hard to explain from weighted-
IDA* theory alone (the outer threshold only ever needs to climb to the
TRUE solution length, since h(goal)=0 regardless of weight -- weight
inflates intermediate-node pruning, not the final required threshold, so
there's no obvious theoretical reason for a blowup this severe). Worth
investigating further before trying more weight values, rather than
assuming higher weight is simply worse in degree.

**A real stdio-buffering bug caught mid-investigation**: the cache-load
confirmation printfs (`"  loaded center-joint table from cache..."` and
the wing-pairing equivalent) had NO explicit `fflush(stdout)` after them,
unlike this project's other established cache-load/build printfs. This
caused real confusion: a weight=2 test looked "stuck at loading" for what
turned out to be a ~10-hour run (confirmed via `Get-Process`'s CPU time
vastly exceeding what a stuck file-load could produce), when it had
actually loaded fine and was deep into an extremely slow trial 0 the whole
time. Fixed by adding `fflush(stdout);` after both messages -- confirmed
working (both cache-load lines now appear within seconds) before
continuing the sweep.

### Immediate next steps for this feature

- Continue a narrow COST12 x COST24 sweep near (9, 14) -- COST12=8 was
  started but killed inconclusive (>10 min with no result, worse than the
  baseline so far); try COST12=10 too, and maybe COST24=13 x COST12=10 in
  case the two constants trade off against each other the way COST23/
  COST34 did in chain1234's own tuning saga.
- Once a good pure-ida-inadm (weight=1) COST12/COST24 combination is
  found, decide whether it's already under budget or whether bullet 2's
  weight-scaling is still needed -- and if so, investigate WHY weight=2
  exploded so badly before trying weight=1.5 or other values blind.
- Only if bullet 2 (properly tuned) is STILL confirmed too slow: bullet 3,
  the one-slice-away special heuristic (exact distances for states one
  slice move from solved, via a packed index the user can describe further
  if needed) -- treat it as its own extra "phase" with COST~3ish per the
  user's guess, per the staged validate-before-implementing instruction
  that governed this whole feature (bullet 2 only after confirming bullet
  1 too slow; bullet 3 only after confirming bullet 2 too slow).
- `chain124.cpp` is a NEW file, entirely separate from the validated
  `chain1234.cpp` -- phases 1 and 2 (namespaces `p1`/`p2full`) are copied
  verbatim, unchanged. `--search {ida,ida-inadm}`, `--weight W`,
  `--cost12`/`--cost24`, `--max-len` (same rationale as chain1234's own
  flag: raise it when h0 could exceed the default 60, though this hasn't
  been hit yet for this solver's h0 values, ~30-32 observed so far).

## Unified vs sequential (S0->S2) — the big experiment

### The idea

Instead of two separate IDA* searches (solve phase 1 completely, then
solve phase 2 from there), the user proposed a SINGLE IDA* over all 27
moves the whole time, with a combined heuristic:

```
h(state) = phase2_heuristic(state)          if state is in S1
         = phase1_heuristic(state) + COST12  otherwise
```

`COST12` is a tunable constant estimating "how many more moves S1->S2
will cost" — not necessarily a strict admissible lower bound.

### A second real bug: the F/B-permutation coordinate isn't valid outside S1

`fb_idx` (phase 2's 8!-permutation coordinate) implicitly assumes the 8
F/B-center pieces never leave their 8 home slots. That's only true under
S1's restricted moves — it's *why* S1 restricts Uw/Rw/Fw to their square.
A single quarter-turn Uw/Rw/Fw (legal in the unified search's full move
set) can send an F/B center to a completely different axis, which
segfaulted the first C++ attempt (and threw a Python KeyError in the
equivalent spot). **Fix**: don't maintain `fb_idx` via an incremental
transition table at all. Instead carry the raw `center_slot[24]` array
(always safe — no confinement assumption) through the search, and compute
`fb_idx` lazily only when phase 1's own `fb` coordinate confirms F/B
centers are actually confined (a strictly weaker, cheaply-checkable
condition than full S1 membership). `wing_idx` has no such issue (it's a
set-membership tracker, not a fixed-domain permutation) and keeps its
full transition table, built under the FULL 27-move connectivity (not
S1-restricted) since that's provably safe and gives tighter distances
(max wing dist 8, vs 10 under the S1-restricted table).

This is implemented in `cpp/chain12.cpp`. **`phase12_chain.py` was NOT
fixed to match** (noted in its own docstring) — it has the same crash and
should not be trusted until someone ports the same fix over. Not a
priority right now given the C++ results below.

### Performance results: the flat-constant heuristic is worse, until tuned low

First result (COST12=11, the user's suggested starting value), 100
random scrambles, verified correct every time:

| | Sequential (2 separate IDA*) | Unified (COST12=11) |
|---|---|---|
| Avg total length | ~18.3 | 18.73 |
| Max total length | 24 | 22 |
| Avg solve time | ~592ms | 1176ms |
| Max solve time | ~3.5s | 14.4s |

Investigated *why* by instrumenting node counts and doing a `compute_fb2`
microbenchmark (90.8M calls/sec in isolation). Conclusion, confirmed by
direct measurement not guesswork: the lazy `fb2` computation is NOT the
cause (accounts for ~10% of per-node cost at most; observed search
throughput stayed flat at ~11-14M nodes/sec regardless of what fraction
of nodes were in-S1). The real driver is node count: one trial explored
121.8 million nodes for a 21-move solution.

**The mechanism, derived from the actual IDA* recursion** (see the
conversation transcript for the full derivation, worth re-deriving if
picking this back up): the pruning rule is `heuristic(node) > depth_left`.
For non-S1 nodes, `heuristic = phase1_h(node) + C`, and since the first
outer threshold is `phase1_h(start) + C`, **the C's exactly cancel** when
comparing non-S1 nodes against the threshold — meaning the set of ways
the search first enters S1 is completely independent of C. What C
actually controls: once a path enters S1 at the minimal depth,
`depth_left` at that instant equals exactly `C`. That's "how much slack
this entry point gets" before its phase-2-style sub-exploration is
abandoned. Raising C admits MORE minimal-phase-1-length entry points
(whichever ones have true phase-2 distance in the newly-opened range)
into full exploration — and since there can be many equally-minimal
phase-1 paths, and search cost grows steeply with allowed depth, a small
increase in C can cause a large blowup. This is exactly what was observed
tuning upward:

| COST12 | avg len | max len | avg time | max time |
|---|---|---|---|---|
| 9 | 17.16 | 21 | **262ms** | 3.59s |
| 10 | 17.94 | 22 | 485ms | 5.40s |
| **11** | 18.73 | 22 | 906ms | 9.85s |
| 12 | 19.57 | 23 | 7.0s | 140.3s |
| 13 | 20.44 | 24 | 4.3s | 143.3s |

Tuning DOWNWARD from 11 kept improving both length and time, all the way
to COST12=9 — which beats the sequential baseline on length and average
time, roughly ties on worst case. The theoretical reason: COST12=0 would
make the heuristic for non-S1 states exactly `phase1_h(node)`, a
genuinely ADMISSIBLE lower bound (reaching S2 needs at least as many
moves as reaching S1). Every value above 0 is an inadmissible
overestimate to some degree; standard IDA* theory says a tighter
admissible heuristic is never worse. So going lower should keep helping,
right up until 0...

**...except it doesn't, past a point.** Continuing the sweep downward
(8, 7, 6, 5 tested; 4 through 0 not completed, sweep manually killed):

| COST12 | avg len | max len | avg time | max time |
|---|---|---|---|---|
| 8 | 16.46 | 20 | 343ms | 6.82s |
| 7 | 15.95 | 20 | 756ms | 4.99s |
| 6 | 15.48 | 19 | **6.27s** | **57.5s** |
| 5 | *(killed mid-run, taking even longer)* | | | |

**Solution length keeps improving monotonically as COST12 -> 0** (makes
sense — closer to admissible, closer to true-optimal solving). But TIME
is U-shaped, bottoming out somewhere around COST12=8-9, then getting
MUCH worse below that. The mechanism (predicted correctly in advance by
the user, then confirmed): near COST12=0, the very first IDA* outer
threshold gives almost no phase-2 slack at all, so most early thresholds
fail completely (wasted full-tree exploration), AND once the threshold
finally grows large enough to succeed, longer-than-minimal phase-1
prefixes have ALSO become newly admissible (not just the minimal ones),
which widens the space further right when it's most expensive to do so
(IDA*'s cost is dominated by its deepest/final iteration).

### Current status / decision

**Settled on COST12=8 for now.** Final apples-to-apples comparison (both
binaries instrumented with node counters, IDENTICAL rng seed=2024 and
scramble-length range 1-50, 100 trials each — `chain_test.exe` for
sequential, `chain12.exe --bench --cost12 8` for unified):

| | Sequential | Unified (COST12=8) |
|---|---|---|
| Avg length | 18.33 (8.04+10.29) | **15.90** |
| Max length | — (not tracked as a single max) | 20 |
| Avg time | 474.07 ms | **251.48 ms** |
| Max time | 11,650.46 ms | **3,502.03 ms** |
| Avg nodes | 6,021,154 | 6,588,289 |
| Max nodes | 118,846,252 | **96,210,008** |
| Throughput | 12.70M nodes/s | **26.20M nodes/s** |

**The genuinely surprising result**: average node count is actually
*similar* between the two (unified explores ~9% *more* nodes on
average) — so unlike the cost12=11 investigation, this time the
speed difference is NOT explained by node count. It's explained by
**per-node throughput being ~2.06x higher in the unified version**
(26.20M vs 12.70M nodes/sec). Best current hypothesis, not yet directly
verified by a further isolated benchmark: `chain_test.cpp`'s phase-2
wing coordinate uses the 9-family-compressed table (`W.trans[idx][fam]`,
9 columns), which for a half-turn or three-quarter-turn move requires 2
or 3 SEQUENTIALLY DEPENDENT lookups into a 97MB table (each one needs
the previous result before it can start, so their memory latencies
can't overlap). `chain12.cpp`'s wing table stores all 27 real moves
directly (292MB, 3x bigger), so every move — regardless of power — is a
single independent lookup. Since phase 2's search dominates the
sequential approach's total node count (phase 1 alone is small; e.g. one
sample trial was 102K phase-1 nodes vs 27.9M phase-2 nodes), a 2-3x
latency penalty on a large fraction of phase-2's moves would plausibly
produce almost exactly the ~2x aggregate throughput gap observed.

**Actionable follow-up this implies, independent of the unified-vs-
sequential question**: decompressing `chain_test.cpp`'s (and
`phase2.cpp`'s) wing table from 9-family to full-21-or-27-column direct
storage — trading ~200MB more memory for a single-lookup-per-move — might
close most of this gap for the SEQUENTIAL approach too, which would
reopen the comparison. Not yet tried. If picking this up again, try that
before concluding unified is definitively better — the current advantage
might be "chain12.cpp happened to use the faster table layout" rather
than anything intrinsic to the unified-search idea.

**This is NOT a final decision.** Open questions:
- Does the "unified with tuned low constant" approach still win once
  phase 3 (and especially phase 4, the big one) are added? The user
  explicitly flagged phase 3->4 as where this matters most, given how
  much bigger that size gap is (phase 4 alone was ~7x over the 10^12
  target even in isolation).
- The mechanism above suggests the SWEET SPOT for the constant depends on
  the actual distribution of true phase-2-from-minimal-phase-1-entry
  distances — this will be DIFFERENT for every phase pair, and probably
  needs its own small tuning sweep each time a new phase gets chained on.
- An unexplored alternative: implement the classical min2phase-style
  nested-budget handoff (solve phase 1 with its OWN dedicated IDA*, then
  phase 2 with the remaining budget, retry alternate phase-1 endpoints of
  the same depth before increasing it) instead of a shared additive
  constant. This is what real solvers actually do, and in theory it gets
  the best of both worlds: solves phase 2 EXACTLY (not estimated) for
  each candidate, so it never wastes work the way a bad constant does,
  while still being able to explore alternate phase-1 endpoints for a
  shorter combined result. Was proposed but not yet built or benchmarked
  against the tuned-constant approach.

### Extending to phase 3: chain123.cpp

Once phase 3 was built, the natural next step (per the user's own proposed
extension) was to chain a THIRD heuristic tier onto the same
flat-additive-constant architecture:

```
h(state) = phase3_heuristic(state)                  if state in S2
         = phase2_heuristic(state) + COST23           if state in S1
         = phase1_heuristic(state) + COST23 + COST12  otherwise
```

with COST12=8 (the value already settled on for the 2-phase chain) held
fixed, and COST23=4 as the user's initial guess for the new S2->S3 gap.
Implemented in `cpp/chain123.cpp`, structurally a direct extension of
chain12.cpp: a new `p3full` namespace rebuilds phase 3's joint distance
table under S2's own (11-move, U/D-excluded) connectivity, using raw
`CENTER_PERM` exactly the way `p2full` rebuilds phase 2's tables under
full S0 connectivity. `compute_lr2`, exactly like `compute_fb2`, is
computed LAZILY and ONLY once `is_in_s1(s)` holds — `lr_idx` has the
identical domain-confinement requirement (only valid once L/R centers are
guaranteed confined to their 8 home slots), so the same guard already
proven necessary for `fb_idx` in chain12.cpp applies here without any new
reasoning needed.

**First result: COST23=4 is a BAD starting guess — worse than sequential
on every metric, dramatically:**

| | Sequential (3 separate IDA*) | Unified (COST12=8, COST23=4) |
|---|---|---|
| Avg length | 25.12 (8.04+10.29+6.79) | 20.03 |
| Max length | — | 25 |
| Avg time | 573.03 ms | **5227.32 ms** (9.1x worse) |
| Max time | 11,886.24 ms | **48,797.28 ms** (4.1x worse) |
| Avg nodes | 6,021,189 | **54,095,457** (9.0x worse) |
| Max nodes | 118,846,289 | 440,836,215 |
| Throughput | 10.51M nodes/s | 10.35M nodes/s |

Throughput is essentially IDENTICAL between the two here (unlike the
phase-1+2 comparison, where a table-layout difference produced a 2x
throughput gap) — the entire regression is pure node-count explosion, the
same failure mode diagnosed in detail for COST12=12/13 above. Telling
detail: "in-S2" heuristic-branch hit rate is 0.0% on nearly every
non-trivial trial, meaning the search essentially NEVER reaches the tight,
exact phase-3 heuristic before finding a solution via the cruder
phase-2-heuristic-plus-constant branch — so COST23=4 is providing far too
much slack once a path enters S1, admitting a huge number of
alternate-phase-2-endpoint branches into full exploration. Solution
length DID improve (20.03 vs 25.12, consistent with getting closer to
admissible), but at a cost far outside the ~5s budget.

**Tuning update: COST23's curve runs the OPPOSITE direction from COST12's.**
Swept downward first (3, 2, 1, 0), expecting the same "lower is better,
down to a point" shape COST12 showed — instead COST23=3 came back WORSE
than 4 (avg time 16.17s, avg nodes 114.3M), so the downward sweep was
abandoned after the first point reversed. Swept upward instead, and it
kept improving with no ceiling found yet as of the last measurement:

| COST23 | avg len | avg time | avg nodes | throughput |
|---|---|---|---|---|
| 3 | 19.33 | 16,168 ms | 114,312,735 | 7.07M/s |
| 4 | 20.03 | 5,227 ms | 54,095,457 | 10.35M/s |
| 5 | 20.90 | 1,913 ms | 33,578,217 | 17.55M/s |
| 6 | 21.71 | 1,046 ms | 19,444,835 | 18.59M/s |
| 8 | 23.36 | 987 ms | 8,868,438 | 8.99M/s |

(COST12 fixed at 8 throughout.) At COST23=8 it's within ~1.7x of the
sequential 3-phase baseline (573ms/6.02M nodes) on time/nodes, and solution
length is climbing steadily as COST23 rises (worse than sequential's 25.12
only at low COST23; converging toward it).

Continued the sweep up through 7, 9, 10, 12, 15 (COST12=8 still fixed):

| COST23 | avg len | avg time | avg nodes |
|---|---|---|---|
| 7 | 22.52 | 697.67 ms | 13,059,986 |
| 9 | 24.21 | 334.25 ms | 7,055,320 |
| 10 | 25.09 | 298.79 ms | 6,736,852 |
| 12 | 27.00 | 292.89 ms | 6,588,366 |
| 15 | 29.73 | 286.83 ms | 6,588,371 |

Time/nodes plateau hard above ~10 (no further speed to gain — 12 and 15
give near-identical node counts), while length keeps climbing the whole
way with no floor: this is a plain weighted/inflated-heuristic tradeoff
(cheaper search, worse guaranteed optimality), not a second U-shaped sweet
spot. Around COST23=9-10 is the first point where BOTH length (~24-25,
matching sequential's 25.12) AND time (~300-330ms, ~1.7-1.9x faster than
sequential) beat the baseline simultaneously.

**Decision: settled on COST23=7 (user's choice, prioritizing shorter
solutions over the fastest possible search — 22.52 avg length vs.
sequential's 25.12, at 697.67ms avg / 13.1s max, both comfortably inside
the ~5s-average / occasional-outlier budget the original brief allows).**

**Then re-checked whether COST12=8 was still optimal now that COST23=7 is
fixed (it wasn't — the constants interact)**:

| COST12 (COST23=7 fixed) | avg len | avg time | avg nodes |
|---|---|---|---|
| 7 | 22.08 | 3,460.74 ms | 54,629,739 |
| 8 | 22.52 | 697.67 ms | 13,059,986 |
| **9** | **23.26** | **479.23 ms** | **7,732,631** |
| 10 | 24.24 | 2,100.02 ms | 21,508,982 |

COST12=9 is a clear new local optimum (U-shaped again, same shape as the
original COST12-alone sweep, just shifted by one). **Final settled values:
COST12=9, COST23=7.** This confirms the "don't assume a previous
constant's tuning stays optimal once another constant changes" lesson
generalizes in BOTH directions — not only does a new constant need its own
sweep, changing it can also re-open a previously "settled" one.

**Lesson for next time a phase gets added to this chain: don't assume a
new constant's tuning direction from the previous one's shape — COST12 was
worst at the extremes with a mid-range optimum, COST23 instead trends
monotonically worse-then-plateaus with no U-shape. Each new phase pair
needs its own from-scratch directional sweep, and previously-tuned
constants should be re-checked, not assumed stable, once a new one is
fixed.**

## S1/S2 "mod whole-cube rotation" — chain12.cpp, done and verified

**Motivation (user's idea):** S1 was defined as "UD-home centers on the
UD axis, LR-home on LR, FB-home on FB" — but a whole-cube rotation (not a
move, free/uncounted in FMC notation) can just as well fix a state where,
say, UD-home centers sit on the FB axis and FB-home on UD. That state is
just as close to a real solve as the literal-identity one, so treating only
the single canonical target as "solved" throws away up to 5 equally valid,
often cheaper waypoints. There are exactly 6 (the S3 quotient of the
24-element whole-cube rotation group: identity, 3 axis-pair transpositions,
2 axis-pair 3-cycles — proven via group order, `24/|kernel|=24/4=6`, kernel
being the 4 "flip-in-place" rotations `{id,x2,y2,z2}` that preserve every
axis SET).

**Architecture note that shaped the whole design:** chain12.cpp is a
*unified* single IDA* over all 27 moves the whole way from S0 to S2 — it
never actually restricts the move set after "entering" S1, `is_in_s1` is
just a heuristic waypoint switch (phase2 heuristic vs phase1
heuristic+COST12). Given that, the user's simplification ("insert a free
cube rotation between your phase-1 and phase-2 solution") became: the
instant a node produced by a real move satisfies S1 mod rotation via a
NON-identity class, canonicalize its `center_slot`/`wing_slot` in place
(apply that class's inverse rotation) so every later `apply_move`/
`heuristic`/`is_in_s2` call sees a literally-canonical S1 state — zero
changes needed anywhere downstream. The user separately corrected an
initial "4 solved states per axis" guess of mine to "only 1" (the Klein
four-group rotations preserve is_in_s2-ness) — moot in the end since the
canonicalize-in-place design means phase 2 never sees a non-canonical state
at all.

**Coordinate derivation (`generate_rotation_tables.py`, new file):**
rather than hand-deriving the 6 rotation permutations (error-prone — this
project has been burned 3 times before by trusting an abstract derivation
over real data), the script brute-force searches `cube_model.py`'s 24
`ROTATIONS` for representatives matching each class's required slot-group
mapping, found exactly 4 per class every time (confirming the kernel-order
prediction), and picked the one with the simplest standard x/y/z rotation
notation. Two independent automated checks, not just eyeballing:
1. Applying the chosen FORWARD rotation to the solved state must reproduce
   the hand-derived `(ud,lr,fb)` target triple exactly, computed via
   `phase1.py`'s own `rank_subset`/`slots_to_lr_idx`/`slots_to_fb_idx` (not
   a re-derivation) — catches any direction/indexing mistake immediately.
2. The emitted INVERSE tables are asserted to be genuine two-sided
   inverses of the forward ones.
Bonus cross-check: the `cycle_UD_LR_FB`/`cycle_UD_FB_LR` rows came out
**byte-for-byte identical** to the pre-existing, already-validated
`CYCLE`/`INV_CYCLE` tables in `tables.h` — strong independent confirmation
the search methodology is sound. Output: `cpp/rotations.h`
(`ROTATE_CENTER`/`ROTATE_WING` forward, `ROTATE_CENTER_INV`/
`ROTATE_WING_INV` inverse, plus `ROTATE_CLASS_INV_NOTATION` for
human-readable x/x'/x2/... output). A second runtime sanity check in
chain12.cpp's `main()` round-trips all 6 classes (forward-rotate solved,
canonicalize, must land back on the literal identity arrays) before any
search runs.

**chain12.cpp changes:**
- `p1::heuristic` is now a min over 6 candidates (3 BFS distance tables
  seeded at the 3 distinct raw target ranks A/B/C, each refined by its own
  depth-3 endtable) — admissible, since min-of-admissible-per-goal is
  admissible for a multi-goal objective.
- `State6` gained a raw `wing_slot[24]` (piece-indexed, parallel to the
  existing `center_slot[24]`), needed only so the compressed `wing`
  coordinate can be recomputed after a rotation-correction.
- `apply_move`'s new `canonicalize_if_rotated` step is the only place a
  rotation is ever applied mid-search, and it's genuinely free (bundled
  into producing the child state, never touches `g`/`depth_left`).
- **Solution output correctness gotcha (caught before it shipped wrong):**
  a rotation applied mid-search is invisible to the recorded move list —
  replaying just the move indices against the ORIGINAL scramble would land
  on the wrong state, since the search's later moves are only valid
  relative to the rotated frame. Fixed by encoding rotations as sentinel
  values (`>= NUM_MOVES`) interleaved into the path vector at the exact
  point they occur, threaded through all 3 search methods (`dfs`,
  `dfs_inadmissible`, `rbfs`) via `push_move_and_rotation`/
  `pop_move_and_rotation`, and replayed with `apply_raw_rotation` (using
  the INVERSE table, matching what was actually applied internally) during
  verification. Printed as standard FMC notation, e.g. `Uw Fw R L Uw Rw2 U
  Fw B2 Uw [y'] D R L U2 R' Uw2 R2 B D Rw2 F` — the bracketed rotation
  doesn't count toward move length (`solution_move_count` skips sentinels).
- The root state itself can rarely already satisfy a non-identity class
  (trivial/near-solved scrambles) — handled by `extract6`'s own
  `out_root_rotation` output, prepended to the solution path by the caller.

**Verification:** 200/200 trials (`--search ida`), 60/60
(`--search ida-inadm`), 60/60 (`--search rbfs`) all verified correct via
full solution replay (moves + rotations) reaching `is_in_s2`. Rotations
were used in 55-68% of trials across all three search methods, so this
isn't a rarely-exercised code path — it's actively changing the chosen
solve path most of the time. No invalid or missing solutions in any run.

**Scope note:** per the user's explicit instruction, this was implemented
for phases 1 and 2 only (chain12.cpp). chain1234.cpp/chain124.cpp are
untouched. Extending this to phase 3/4 (S2->S3->S4) was not attempted —
if revisited, note the "is_in_s2-ness preserved under Klein four" property
the user identified would need re-examining for whatever the S3/S4
analogue of "mod rotation" would mean, since compute_fb2/compute_ud2/etc.
in the later phases are more tightly coupled to exact slot positions than
S1's coarse SET-membership coordinate was.

## Phase 2 "mod whole-cube rotation" — the P2 fork (chain12.cpp), done and verified

**Motivation:** the section above makes phase 1 (S0->S1) rotation-aware
(reach S1 via any of 6 equally-valid rotated targets). The natural
follow-up: make phase 2 (S1->S2) rotation-aware too, i.e. treat 2 more
restricted-generator subgroups as equally valid S2 targets alongside the
literal `H1 = <U,D,R,L,F2,B2,Rw2,Uw2,Fw2>`:
`H2 = <U,D,R2,L2,F,B,Rw2,Uw2,Fw2>` (class 3, "swap_LR_FB"/"y") and
`H3 = <U2,D2,R,L,F,B,Rw2,Uw2,Fw2>` (class 1, "swap_UD_FB"/"x"). Unlike
phase 1's coarse SET-membership coordinate, H2/H3 aren't reachable by just
canonicalizing `is_in_s1`'s (ud,lr,fb) triple — they're genuinely different
*generator sets*, so a state satisfying H2 has fine wing/center structure
that phase 1's rotation machinery never had to deal with.

**First attempt (abandoned): conjugate every node's coordinates on every
heuristic call.** Recompute `wing`/`fb2` under all 3 target frames at every
`heuristic()`/`is_in_s2()` call. Correct (verified extensively) but ~5x
slower in wall-clock time despite visiting fewer nodes — the 3x-per-node
cost dominates, since it taxes EVERY node in S1, not just the boundary.

**Second design (the fork), per the user's explicit proposal:** conjugate
only ONCE, at the S1 boundary. The instant a path first enters literal S1,
split into 3 candidate continuations (identity, plus one per non-identity
class) and explore each as an ordinary subtree; whichever succeeds wins.
This ups node count by up to 3x, but ONLY at/after the boundary node, not
throughout — the claim (borne out below) is that this restores per-node
throughput close to pre-rotation-awareness levels.

**The algebra (verified two ways: a hand-checked toy `S4` group example,
and 400/400 empirical samples in `main()`'s permanent startup check):**
given a literal-S1 state `ns`, compute the TWO-SIDED conjugate
`CONJ = Y.ns.Y^-1` (relabel domain via `Y^-1`, value via `Y`, using the
SAME `ROTATE_CENTER`/`ROTATE_CENTER_INV` tables as phase 1, just applied
on both sides instead of one). `CONJ` is itself ALWAYS literal canonical S1
— so it needs zero special-cased awareness anywhere in the search; it's
just another ordinary `State6` node. If a normal search on `CONJ` finds a
real move sequence `T` reaching literal H1 (`M_T.CONJ = h in H1`), then
replaying `[open: Y][T's same moves][close: Y^-1]` on the TRUE `ns`
produces `final = Y^-1.h.Y` — algebraically NOT `h` itself, but a state
whose (ud,lr,fb) triple is already canonical (Y/Y^-1 cancel on each
axis-target SET) while its fine wing/center structure is a genuine
conjugate of a literal-H1 solve. This is a legitimate, different landing
state (H2/H3, not H1) — by design, not a bug (a phase built on top of this
later would continue from the conjugated cube).

**Implementation:** `build_p2_fork_candidates` computes the 3 candidates
(identity + 2 conjugates); `g_p2_forked` (a plain bool, not the earlier,
overcomplicated per-call "which frame" state) guards against re-forking if
a branch's own trajectory exits and re-enters S1, matching "first time"
semantics; `P2_OPEN_SENTINEL_BASE` (a second sentinel range, alongside
phase 1's existing rotation-close sentinels) records the one-sided
"open" `Y` in the output path, replayed via `apply_raw_rotation_open`
(forward table) vs the existing `apply_raw_rotation_close` (inverse
table). Threaded through all 3 search methods (`dfs`, `dfs_inadmissible`,
`rbfs`) identically to how phase 1's rotation sentinels already were.

**A real, subtle verification bug (found and fixed) — worth remembering
for any future two-sided-conjugation design:** the bench loop's pass/fail
check called the SAME `is_in_s2` (literal H1 only) used as the search's
own goal test. That's CORRECT for the search itself (both true-state nodes
and CONJ nodes should search toward literal H1) but WRONG for verifying
the final, fully-replayed output of a non-identity fork win — because (per
the algebra above) that output legitimately lands in `final = Y^-1.h.Y`,
not `h`. Recovering `h` from `final` requires the OPPOSITE two-sided
conjugation (`Y.final.Y^-1 = h`) — NOT achievable via any further real,
sequential physical operation on the replayed cube (you can't retroactively
prepend a rotation before a sequence that already happened), so it must be
a dedicated, non-search VERIFICATION-ONLY check:
`is_in_s2_mod_conjugation(s)` (literal H1, OR class-3-conjugate-then-H1, OR
class-1-conjugate-then-H1). Symptom before the fix: 4/15 trials failed with
"INVALID SOLUTION", exactly matching the 4 non-identity fork wins in that
run — the bench summary line's own "N/N verified correct" is static
boilerplate that doesn't actually check anything; only the per-trial
INVALID/NO-SOLUTION prints do (caught by grep-counting them separately).
**Lesson: when a search node's "goal" and the RESULT you actually want to
report/verify differ by a conjugation (or any other non-invertible-via-
further-real-moves transform), they need two DIFFERENT membership tests —
reusing the search's goal test for final verification is an easy, subtle
mistake.**

**Verification (post-fix):** 0 invalid/no-solution across 140 trials
(60 `ida-inadm`, 60 `rbfs`, 20 `ida`, scramble lengths 30-60), with genuine
non-identity fork wins verified correct in every search method.

**Benchmark vs `--no-rotation` baseline** (60 trials, default scramble
range 1-50, `--search ida-inadm`):

| | fork (rotation-aware) | `--no-rotation` baseline |
|---|---|---|
| avg solve time | 1066 ms | 1528 ms |
| avg solution length | 16.68 | 17.60 |
| avg nodes | 402,955 | 657,081 |
| raw throughput | 0.38M nodes/s | 0.43M nodes/s |

Per-node throughput lands at ~88% of baseline (nowhere near the ~5x
slowdown of the first, per-node-conjugation attempt) — confirming the
fork's design goal. Bonus beyond just "not slower": giving phase 2 three
chances to reach S2 instead of one also finds shorter solutions in fewer
total nodes, so wall-clock time is actually 30% BETTER than baseline, not
just recovered.

**Scope/status:** implemented and verified for chain12.cpp only, matching
phase 1's own scope note above. Not extended to chain123.cpp/chain1234.cpp.

### Benchmark scramble range standardized to 100-120 (was 1-50/1-40/1-200 depending on file)

Per explicit user request: every `--bench`/trial-loop random scramble
generator across `cpp/` (`chain12.cpp`, `chain123.cpp`, `chain124.cpp`,
`chain1234.cpp`, `chain_test.cpp`, `phase1.cpp`, `phase2.cpp`, `phase3.cpp`,
`phase4.cpp`) now defaults to length 100-120 uniformly, for consistently
"real" scramble quality across all benchmarks (previously ranges varied
file to file, some as short as 1-40 — too short to reliably reach
representative depth, per the same lesson as phase 4's own "benchmark
sanity" correction above). `chain12.cpp`'s range stays overridable via
`--scramble-min`/`--scramble-max`; the others are fixed constants.

**COST12 re-swept under the new 100-120 range** (`--search ida-inadm`,
30 trials each, `--cost12 N`):

| COST12 | avg time | avg len | avg nodes |
|---|---|---|---|
| 10 | 655 ms | 19.10 | 375,649 |
| 9 | 362 ms | 18.47 | 211,610 |
| **8** | **193 ms** | **18.03** | **139,318** |
| 7 | 459 ms | 17.57 | 385,479 |
| 6 | killed early (partial avg 3.7s over 8 trials, 2 already >5s) | — | — |

**COST12=8 is the new sweet spot** — fastest average of everything tested,
with shorter solutions than 9/10 too. COST12=7 is a viable alternative
(shorter solutions, still well under a 1s average) if length matters more
than the last bit of speed. COST12=6 falls off a cliff (~8x time jump,
multiple trials over 5s) — not viable. All 30/30 (or 8/8 partial) verified
correct at every tested value, 0 invalid/no-solution. **Not yet reconciled
against the earlier COST12=9 setting used in the 3-phase/4-phase chains
(chain123.cpp/chain1234.cpp)** — those were tuned under their own, still-
unmodified scramble ranges/heuristics and haven't been re-swept under
100-120 themselves; COST12=8 is chain12.cpp-specific for now.

## Techniques used

- Exact group order computation via `sympy.combinatorics.PermutationGroup`
  (Schreier-Sims) to design the phase chain BEFORE writing any solver
  code — cheap to get exact numbers, much better than guessing.
- Combinatorial number system for C(n,k) coset ranking; Lehmer code for
  n! permutation ranking. Both in `combinatorics.py`, both have exhaustive
  round-trip tests.
- "9 base families, derive powers 2/3 by repeating the base transition"
  memory-saving trick (from Jaap's page) — used in the Python prototypes
  and in phase2's wing/fb tables. NOT used in phase1.cpp's own table
  (chose full 27-column storage there instead, trading memory for a
  single O(1) lookup per move — a deliberate speed/memory tradeoff,
  revisit if memory becomes tight once more phases are added).
- Reusing one coset table for 3 axis-symmetric coordinates via a
  whole-cube-rotation slot relabeling + empirically-derived family/power
  remapping (phase 1's ud/lr/fb).
- Generating C++ header tables FROM the validated Python model
  (`generate_tables*.py`) rather than re-deriving cube geometry a second
  time in C++ — deliberate risk reduction after the handedness bug.
- Node-count instrumentation + isolated microbenchmarks to distinguish
  "slower per node" from "more nodes explored" when diagnosing a
  performance regression, rather than guessing from timing alone.
- Exact (not just admissible-lower-bound) heuristics via a full joint
  distance table, for phases small enough to afford it (phase 3's
  40320 x 96 joint table) — strictly better than max-of-separate-tables
  when the state space is tractable; no endtable trick needed at all.
- Empirically verifying which candidate moves are true no-ops for a given
  phase's own coordinate (check the transition column is the identity map
  over the full domain) rather than assuming a phase must search its
  entire parent generator set — cut phase 3's branching factor from 17 to
  11 for free, zero solutions lost.

## Techniques NOT yet tried / considered for later phases

- Classical nested-budget phase handoff (see above).
- Non-admissible heuristics with weights >1, or a sum instead of max of
  lower bounds (both suggested by the user for the eventual big S3->S4
  phase, not yet needed/tried since we're only at phase 2).
- Symmetry reduction (mirror/rotational) beyond the single 120-degree
  rotation reuse already used in phase 1 — real optimal solvers (see
  min2phase's `Algorithm.md`) get major table-size wins from the full
  48-element symmetry group; not explored here yet since our tables have
  been small enough without it so far. Will matter more as phases grow.
  **Flagged explicitly by the user (2026-09-18) as a future way to shrink
  phase 4's pairing table further** — not implemented, only add if the
  current 4-bit-packed 119.75MB table becomes a real blocker.
- Distance-mod-3 packing (Jaap's page technique) for phase 4's pairing
  table specifically — **flagged explicitly by the user (2026-09-18) as
  the next compression step if the current 4-bit-packed (119.75MB) table
  isn't small enough**: store only `distance mod 3` (2 bits instead of 4,
  another 2x reduction to ~60MB), and recover the actual distance at
  query time via a short greedy walk toward the distance-0 (solved) state
  — adjacent states always differ by exactly 1 in true distance, so
  `distance mod 3` plus the CURRENT search depth is enough information to
  disambiguate which multiple of 3 the true value is, without storing it
  outright. Not implemented — explicitly deferred unless the 4-bit table
  turns out to be an actual blocker (memory or otherwise), per direct
  instruction not to build it preemptively.
- Bit-packing transition tables (e.g. 20-bit values instead of int32) —
  explicitly rejected for phase 1 when performance was the stated
  priority; revisit if memory becomes the binding constraint instead.

## What didn't work / mistakes made (so future work doesn't repeat them)

1. **Assuming a "family permutation" is piece-indexed vs slot-indexed
   without checking.** Got this backwards once (phase2's fb2 direction
   bug) and it silently produced a WRONG but not obviously-broken result
   (6 reachable states instead of 40,320) rather than crashing. The
   "fix" I first tried was actually a regression — I had it right
   originally and broke it "fixing" a symptom I misdiagnosed. Always
   re-derive the exact convention (`new_state[perm[i]] = old_state[i]`,
   perm indexed by SLOT throughout this codebase) rather than
   pattern-matching from memory.
2. **Assuming a coordinate's transition table generalizes to a bigger
   move set without checking the underlying invariant.** phase2's fb_idx
   works under S1 because S1 is specifically constructed to preserve
   F/B-center confinement; it does NOT generalize to all of S0. Before
   reusing any "coset" or "permutation index" coordinate under a
   DIFFERENT (larger) move set than it was designed for, re-derive
   whether the domain-fixing assumption still holds under the new moves.
3. **Trusting a flat additive constant to approximate a nested search.**
   Intuition said "bigger constant = more conservative/safer"; the
   opposite was true because of how IDA*'s threshold/pruning interact
   with a shared budget. When in doubt about a heuristic's behavior,
   instrument and measure (node counts, throughput) rather than reason
   from intuition alone — this caught both the true cause of the first
   slowdown AND explained the non-monotonic tuning curve.
4. Ran background benchmark processes that occasionally got stuck (e.g.
   COST12=5 taking so long it looked hung) — when killing stray processes
   on Windows, remember `.exe` files can't be relinked while a previous
   run is still executing (`ld.exe: Permission denied`); check
   `ps aux | grep <name>` and kill before rebuilding.
5. **A "verified by direct computation" claim from early in the project
   (Lw2 NOT redundant for S3) turned out to be wrong** when rechecked
   fresh while starting phase 4 (the user was right to be skeptical) —
   `check_lw2.py` shows `<U,D,R,L,F2,B2,Rw2>` alone already generates all
   of S3, Lw2 included. Don't treat a past "verified" note as permanently
   load-bearing, especially across a codebase edit (the handedness fix
   happened after that original claim) — a computation is only as
   trustworthy as the code it ran against at the time. When a stale claim
   is about to drive new design decisions (here: whether phase 4's search
   needs a reconstructed Lw2 move), rerun it against current code first;
   it's cheap (seconds) compared to building on a wrong premise.
6. **Assumed a new heuristic-chaining constant would tune the same
   direction as the previous one.** COST12 (S0/S1->S2) had a U-shaped
   curve, bad both too high and too low, sweet spot at 8. When extending
   to COST23 (->S3) with the exact same additive-constant architecture,
   assumed the same shape would apply and started sweeping downward from
   the initial guess — instead COST23 got WORSE going down and kept
   improving (in time/nodes) going up with no ceiling found, because
   COST23 appears in the heuristic in a structurally different place than
   COST12 does (added inside the "already in S1" branch too, not just the
   pre-S1 branch — see "Extending to phase 3" above for the full
   derivation). Each new phase pair's constant needs its own
   from-scratch directional probe (try both directions before committing
   to a sweep), not an assumption that a previous constant's tuning
   direction generalizes.

## Build instructions

```bash
# From SuperCubeSolver444/:
python3 cube_model.py          # regression tests, run after any geometry change
python3 combinatorics.py       # rank/unrank sanity checks
python3 phase1.py              # builds + validates phase 1 tables (slow, ~70s)
python3 phase2.py              # builds + validates phase 2 tables (slow, ~4min)
python3 phase3.py              # builds + validates phase 3 tables (slow, ~6-7min --
                                # rebuilds phase1+phase2 too, for an honest chained test)
python3 phase4.py              # builds + validates phase 4's SMALL tables + wing-
                                # pairing move-formula spot checks (fast, no 12! build)
python3 generate_tables.py     # regenerate cpp/tables.h (fast)
python3 generate_tables2.py    # regenerate cpp/tables2.h (fast)
python3 generate_tables3.py    # regenerate cpp/tables3.h (fast, ~3-4s)
python3 generate_tables4.py    # regenerate cpp/tables4.h (fast, ~3-4s)

# From SuperCubeSolver444/cpp/:
g++ -O3 -std=c++17 -o phase1 phase1.cpp       # then ./phase1 [--bench]
g++ -O3 -std=c++17 -o phase2 phase2.cpp       # then ./phase2 [--bench]
g++ -O3 -std=c++17 -o phase3 phase3.cpp       # then ./phase3 [--bench]
g++ -O3 -std=c++17 -o phase4 phase4.cpp       # then ./phase4 [--bench] -- builds the
                                               # 12! table ONCE (~17-22 min), caches to
                                               # pairing_dist_p4.bin (~120MB) for next time
g++ -O3 -std=c++17 -o chain_test chain_test.cpp   # sequential baseline, 100 trials
g++ -O3 -std=c++17 -o chain12 chain12.cpp     # unified S0->S2 solver
./chain12 --bench --cost12 8                  # current chosen constant (2-phase only)
g++ -O3 -std=c++17 -o chain123 chain123.cpp   # unified S0->S3 solver
./chain123 --bench --cost12 9 --cost23 7      # settled constants (3-phase)
g++ -O3 -std=c++17 -o chain1234 chain1234.cpp # unified S0->S4 solver
./chain1234 --bench --cost12 9 --cost23 8 --cost34 11   # settled constants (4-phase)
```

`phase1.exe`/`phase2.exe`/`phase3.exe`/`phase4.exe`/`chain12.exe`/
`chain123.exe`/`chain1234.exe` all drop into an interactive REPL after
building tables — type space-separated moves, see the solution. `--bench`
runs a 100-trial internal benchmark first (phase3.exe's bench uses 200
trials, since each solve is near-free; `chain1234.exe`'s bench also
prints median/p90/p99 time and length, which is what tuning decisions
should be based on — see "chain1234.cpp" above for why).

**`phase4.exe`/`chain1234.exe` specifically may fail to launch from Bash
with an instant, silent exit 127** (a real, unexplained environment quirk,
not a code bug — see "A note on process" below). If that happens, launch
via PowerShell's `Start-Process` instead (file-redirected stdin/stdout/
stderr, not piped) and poll the output file from Bash.

## Immediate next steps (pick up here)

1. **Done**: sequential-vs-unified node-count/per-node-cost comparison for
   phases 1-2. The "decompress the sequential wing table" follow-up was
   never actually tried — deprioritized once phase 3 (and now phase 4)
   became the focus. Still an open, cheap experiment if revisited.
2. **Done**: phase 3 (S2->S3) fully implemented, validated, and chained
   (both sequentially and unified via chain123.cpp). Settled constants:
   COST12=9, COST23=7.
3. **Done**: phase 4 (S3->S4) fully implemented, validated (Python +
   C++), and chained into `chain1234.cpp` (unified S0->S4). The 12!
   pairing distance table is 4-bit-packed over the reachable (dense) half
   and cached to `pairing_dist_p4.bin` (~119.75MB) so the ~17-22 minute
   BFS only ever runs once — see "Packed + cached pairing table" above.
4. **Done**: full COST12/COST23/COST34 boundary search for the 4-phase
   chain, under the clarified objective (shortest length subject to
   MEDIAN time <= ~5s, not mean/max — see "chain1234.cpp" above for the
   full methodology and results table). **Settled: COST12=9, COST23=8,
   COST34=11** (COST23 shifted from 7 to 8 once phase 4 was added — yet
   another confirmation that a shorter chain's settled constant is only
   ever a starting point for the next phase's tuning).
5. Still open / not yet done:
   - The spec's third phase-4 heuristic idea (a separate "minimum Rw2
     moves needed" distance table) — not implemented; the plain `max`
     heuristic mode was sufficient to reach a good tuning result, so this
     is a nice-to-have, not a blocker.
   - Extend `chain_test.cpp` to a full 4-phase SEQUENTIAL baseline (it
     currently stops at phase 3) — needed for an apples-to-apples
     unified-vs-sequential comparison now that all 4 phases exist. The
     2-3 phase comparisons both favored the unified approach, but that
     conclusion has never been re-validated with phase 4 in the mix.
   - The "decompress the sequential wing table" follow-up from the
     phase-1/2 comparison — never tried, still an open, cheap experiment.
6. Phases 5-7 are unstarted (corner orientation, equator slice, L/R center
   parity for phase 5; "I trust you to figure it out" for 6-7 per the
   original spec).

## chain1234v2.cpp — unified S0->S4 solver: rotation-aware P2 fork + hybrid S2->S4 "good bit" heuristic

Combines chain12.cpp's rotation-aware phases 1+2 (including the P2 fork,
see above) with a new S2->S4 heuristic design covering phases 3+4 in one
unified tier, instead of chasing literal S3 as a separate phase.

### The two new tables

- **Center-joint table**, 2520 x 2520 x 24 = 152,409,600 entries, 2
  bytes/entry: 7 bits even-RwUwFw-parity distance (capped 127), 7 bits
  odd-parity distance (capped 127), 1 "good" bit, 1 unused bit.
- **Wing-pairing+parity table**, 239,500,800 entries, 2 bytes/entry: 8
  bits even-parity distance, 8 bits odd-parity distance — deliberately
  left uncapped-in-practice (255 ceiling) for now so the true max distance
  could be measured before deciding whether a tighter/packed
  representation is worth it. (Answer, once the table was correctly
  built — see the bug below: max is 13, avg 10.93, so 255 headroom was
  never actually needed; a future pass could shrink this table if space
  becomes a concern.)

Both tables track distance separately by the parity of the total count of
{Rw2, Uw2, Fw2} moves used so far (`TOGGLES_RWUWFW`), matching the
existing PLL-parity design from chain124.cpp. `corner_parity` (toggles on
every quarter turn, i.e. any move not ending in `2`; untouched by whole-
cube rotations) XORed with wing-parity gives `pll_parity`, which selects
which of the two parity slots to read at heuristic-lookup time.

### The heuristic

```
h(state) =
  if state NOT in S1:      phase1_heuristic(state) + COST24 + COST12
  elif in S1 but not S2:   phase2_heuristic(state) + COST24
  else (in S2, tier2):     max(center_table[ud,lr,fb][pll_parity],
                                pairing_table[wing][pll_parity])
                           + (COST34 if NOT good else 0)
```

COST34 is a node-level correction inside tier 2 (not a tier-boundary
constant like COST12/COST24), so `tier_inadm_from_code` deliberately
excludes it from the base inadmissible bump.

### The "good" bit — what it actually means (corrected twice; final form verified by direct lookup, no BFS needed)

The whole point of the hybrid design: mark a center configuration "good"
if it's already equivalent to literal S3 or one of its rotations, so the
search gets rotation-awareness "for free" and can make wing-pairing
progress *before* reaching that checkpoint, without a separate phase.

**This went through real iteration — worth keeping the history because
the wrong version was plausible-sounding:**

1. First (wrong) version: "good" = reachable to solved centers via ONE of
   3 restricted single-slice movesets (`<...,Rw2>`, `<...,Uw2>`,
   `<...,Fw2>`), implemented as a flood-fill. This is a fundamentally
   different (and, it turns out, far more common — good would have been
   ~100% instead of ~0.02%) condition than what was actually wanted.
   Caught before the wing-pairing BFS was wastefully run against it
   (stopped mid-build on the center-joint table once corrected).
2. User's correction: "good" means **in S3, or in one of the rotations
   thereof** — since the search already starts inside S2, this is *not*
   vacuous, and using it as the good condition lets a checkpoint-style
   detection happen without a dedicated phase-3-only pass.
3. Further correction: only **2** variants exist, not 3 — the Rw2-based
   literal-S3 target, and the Uw2-based rotated target. **The Fw2 variant
   is omitted: it is not isomorphic to S3.**

**Final verified formula** (confirmed via direct C++ reachability/lookup
checks against the already-validated `*_SOLVED_INDICES` tables and a
fresh independent BFS — not derived by hand):

```cpp
is_good(ud2520, lr2520, fb24) =
    (lr2520 == 0 && fb24 ∈ {0, 1, 11, 15, 16, 21})   // literal S3 (Rw2 variant)
 || (ud2520 == 0 && fb24 ∈ {0, 5, 6, 13, 16, 18})    // rotated S3 (Uw2 variant)
```

Key facts that made this a pure O(1) lookup instead of a flood-fill:
- `CENTER2520_LR[raw] == 0` exactly iff `raw` is one of the 16
  `LR_SOLVED_INDICES` values (verified 16/16, 0 mismatches — reduction
  class 0 IS the entire solved orbit, nothing more).
- `CENTER2520_UD[raw] == 0` exactly iff `raw` is one of the 16
  `UD_SOLVED_INDICES_S4` values (verified 16/16; independently
  reconfirmed via a fresh BFS under the Uw2-generator moveset).
- FB's literal-S3 target (24 raw values) maps cleanly onto exactly 6
  whole FB24 classes: `{0,1,11,15,16,21}`.
- FB's *rotated* target is a genuinely different 24-element raw set
  (only 8/24 values shared with the literal target) but ALSO maps
  cleanly onto exactly 6 whole FB24 classes: `{0,5,6,13,16,18}`.

Measured: only ~0.02% of the 152.4M center-joint configurations are
"good" (matches the intuition that literal-or-rotated-S3 is a narrow
condition, unlike the first, wrong flood-fill version).

### The wing-pairing table bug — real, inherited from chain124.cpp, found via a user hunch

Symptom: after building the new wing-pairing table, reachability wasn't
100% — 638,657 even-parity and 407,280 odd-parity states (out of
239,500,800) were unreached, with the two counts oddly *different*. User's
reaction to this report: "why are there unreachable wing-pairing states?
... I'd investigate for a bug." That hunch was correct.

**Root cause**: `build_pairing_parity_table`'s BFS applied ALL 21 moves
from P2's move set (`P2_NUM_MOVES`) via `apply_move_to_perm12_p2`, with no
guard against invalid transitions. But P2's move set is
`<U,D,R,L,F,B,Rw2,Uw2,Fw2>` (21 moves, including full F/F'/B/B' quarter
turns) — **S2 itself only permits 17 of those** (`<U,D,R,L,F2,B2,Rw2,
Uw2,Fw2>`; F/B restricted to half-turns only). Feeding a real F or F'
move to the wing-pairing BFS silently walked it outside the actual S2
group, corrupting distances via illegitimate shortcut edges. Verified
directly: applying a genuine F move to an `is_in_s2`-confirmed state
breaks BOTH of its defining conditions (`wing_solved` and `fb_solved`
both flip false), while F2 and the slice half-turns correctly preserve
them. `build_center_joint_table` was NOT affected — it's already safe
because `FB24_TRANS` returns `-1` for any move that exits the 96-valid FB
domain, and the BFS loop already skips those (`if (nfb < 0) continue;`);
`build_pairing_parity_table` had no analogous guard.

This bug **predates chain1234v2.cpp** — the same anomaly was confirmed
present in chain124.cpp's own pre-existing cached table
(`pairing2_dist_p24.bin`), so it was inherited, not newly introduced.
Proof it's a real bug and not a structural fact: P2's 21-move set is a
strict superset of the older, already-validated 15-move `S3_MOVES` set,
which was separately proven to reach all 239,500,800 dense wing-pairing
states — so ANY unreached state under the bigger move set is a
contradiction unless something is silently invalid.

**Fix**: added `S2_OWN_MOVES_PM[17] = {0,1,2,3,4,5,6,7,8,9,10,11,13,16,
18,19,20}` (the P2-move-index encoding of exactly S2's 17 real
generators) and changed the BFS's move loop from
`for (int pm = 0; pm < P2_NUM_MOVES; pm++)` to
`for (int pm : S2_OWN_MOVES_PM)`. Verified via a restricted-moveset rerun
of the SIGMA incremental-tracking check: 0/2000 mismatches against ground
truth (down from 1295/2000 with the buggy full move set).

**Corrected results after rebuilding from scratch** (~51 min actual CPU
time, per `Get-Process` — see the process notes below for why the
program's own printed wall-clock time was not trustworthy for this run):
both parities now show 239,500,800/239,500,800 reached (0 unreached,
down from 638,657/407,280). Average distance **rose** to 10.93 (from the
buggy 9.71) and max distance **dropped** to 13 (from the buggy 21) — the
old, lower average and higher max were both artifacts of illegitimate
F/F'/B/B' shortcut edges, not real solving difficulty.

Center-joint table stats (unaffected by this bug, measured for
comparison): avg 14.14, max 20.

### Status / what's still open

- `g_cost24` and `g_cost34` are still untuned defaults (13 and 5
  respectively) as of this writing. A COST24=22/COST34=sweep was run
  earlier in the session but **all of that timing data is stale** — it
  was gathered against the buggy wing-pairing table, and the corrected
  table's heuristic values differ (avg 9.71->10.93, max 21->13), so
  search behavior will differ too. The sweep needs to be redone from
  scratch against the fixed tables before trusting any COST24/COST34
  conclusion.
- chain12.cpp's COST12=8 (settled under the 100-120 scramble range) has
  not yet been reconciled against chain123.cpp/chain1234.cpp's own
  separately-settled COST12=9 (still unswept under 100-120) — still open,
  noted here again since it's directly relevant to what chain1234v2.cpp
  should use.
- The `--max-len` trap (documented for chain1234.cpp) applies here too:
  with large COST24/COST12, `h0` can exceed the default `--max-len=40`
  before IDA*'s outer loop even starts, giving instant, spurious "NO
  SOLUTION" on every trial. Pass `--max-len 100` (or higher) whenever
  testing large COST values.

## A note on process (Windows/git-bash specific)

`chain12.exe`'s interactive REPL does not always exit cleanly after
piping `printf "quit\n"` into it, especially when run in the background
— the process can linger even after the harness reports the command
"completed", which then blocks `g++` from relinking the same `.exe`
(`ld.exe: cannot open output file ... Permission denied`). If a rebuild
fails with that error, don't just retry — check `ps aux | grep chain12`
first, and if that doesn't show it, `taskkill //F //IM chain12.exe` (or
whichever binary) before rebuilding.

**A much stranger one, found 2026-09-19: `phase4.exe`/`chain1234.exe`
(specifically, once they linked in `pairing_dist_io.h`) could NOT be
launched from Bash at all** — every invocation (piped stdin, `<
/dev/null`, no stdin redirection, fresh copies, freshly recompiled
binaries, a trivial-main sanity binary linking the same headers) exited
INSTANTLY with code 127 and produced literally zero bytes of stdout/
stderr, indistinguishable at first from "command not found" or an
immediate crash. It was neither: the exact same binary, launched via
PowerShell's `Start-Process` (with file-redirected stdin/stdout/stderr,
not piped, to avoid any buffer-blocking risk), ran completely normally —
confirmed alive and burning CPU via `Get-Process`. Root cause not fully
identified (something specific to Bash/MSYS's process creation for this
particular binary — possibly related to its size or a specific allocation
pattern — not a code bug, not a missing DLL per `ldd`, not file corruption
or antivirus quarantine, all specifically ruled out). **Workaround that
resolved it completely: launch `phase4.exe`/`chain1234.exe` via the
PowerShell tool's `Start-Process` with `-RedirectStandardInput` pointed at
an empty file (not `NUL` directly — PowerShell resolves that as a
filename, not the null device, when passed as a string; use an actual
empty file, e.g. `empty_stdin.txt`) and `-RedirectStandardOutput`/
`-RedirectStandardError` pointed at real files, then poll the output file
from Bash (safe — reading files doesn't invoke the problem binary).** If
picking this up again and hitting an instant, silent exit 127 from ANY
binary in this project, don't assume a crash — try PowerShell first
before spending time on gdb-style diagnosis.

Related fix from the same investigation: printf output to a file-
redirected stdout is FULLY buffered by default (not line-buffered), so a
slow run's progress can sit completely invisible in the log for many
minutes — looking exactly like a hang — until either the buffer fills or
the process exits. Both `phase4.cpp` and `chain1234.cpp` now call
`setvbuf(stdout, NULL, _IOLBF, 0)` as literally the first line of `main()`
specifically to avoid this; do the same for any new long-running CLI tool
in this project whose output might be redirected and checked mid-run.

**Correction (2026-09-23, from chain1234v2.cpp): `_IOLBF` does NOT
reliably solve this on Windows.** Verified directly — with `_IOLBF` set,
0 bytes had reached the redirected output file after 24s of confirmed
real CPU progress (`Get-Process` showed the process actively burning
CPU); flushing only happened later, in batches, not line-by-line. MSYS/
MinGW's C runtime does not treat a redirected-to-file stdout as
line-buffer-eligible the way glibc on Linux does, regardless of the mode
requested. The robust fix is `setvbuf(stdout, NULL, _IONBF, 0)` — fully
unbuffered — which chain1234v2.cpp now uses as literally the first line
of `main()`. The perf cost is negligible since none of these tools print
in a hot loop. **Treat the `_IOLBF` guidance above as superseded; use
`_IONBF` for any new long-running CLI tool in this project.**

Two more process gotchas found during the same (2026-09-23)
chain1234v2.cpp wing-pairing-table rebuild, which ran for real-world
hours across the user putting the machine to sleep and waking it back up:

- **Sleep/hibernate does not kill background Windows processes** — they
  survive suspend and resume computing on wake (confirmed via
  `Get-Process`'s CPU-seconds counter increasing monotonically across the
  gap, with no reset). However, **`std::chrono::steady_clock` wall-clock
  timings printed by the program DO include the suspended duration** on
  this Windows build, so a program's own self-reported "took N ms" after
  a sleep/wake cycle can be wildly inflated (one run reported ~6.33 hours
  for a build that only used ~51 minutes of actual CPU time). When
  checking on a long build that might have spanned a sleep, trust
  `Get-Process`'s CPU time, not the tool's own internal timer output.
- **`Monitor`-watched `tail -f` processes are not self-cleaning.**
  Re-arming `Monitor` repeatedly over a long wait (checking in on the same
  log file every so often) spawns a new background `tail -f` each time
  without stopping the previous one; left unchecked across a multi-hour
  wait these accumulate (145 were found and killed in one session) and
  can cause spurious/duplicate wakeups. `pkill` is not available in this
  Git-Bash environment — clean them up with:
  `for pid in $(ps aux | grep "/usr/bin/tail" | grep -v grep | awk '{print $1}'); do kill -9 "$pid"; done`

## chain123prime.cpp / chain1234prime.cpp — the S3' waypoint design

A completely different S2→S4 approach from chain1234v2.cpp's center-joint/
wing-pairing/good-bit design — NOT a drop-in replacement, a fresh idea:
introduce an intermediate waypoint **S3'** (prime, not the OLD literal `S3`
from chain124.cpp — a different, new concept) that's cheap to detect and
cheap to build tables for, then chain a *second* hybrid heuristic from S3'
to literal S4.

**S3' definition**: a state already in S2 is in S3' if it's solvable to S4
using ONLY moves from `<U, D, R2, L2, F2, B2, Rw2, Uw2, Fw2>` (13 moves,
P2-index form `{0,1,2, 3,4,5, 7, 10, 13, 16, 18,19,20}` — U/D unrestricted,
R/L restricted to their half turn, F/B always restricted, plus the 3 slice
halves). S3' is deliberately **not a group** — it's just "the orbit of
solved under this generator set," used purely as a cheap waypoint.

- **80,640** wing-pairing states and **576** (lr2520, fb24) center pairs
  are S3'-good (reachable from solved via the 13-move set) — tiny fractions
  of the full 239,500,800 / 60,480 spaces (0.034% / 0.95%).
- Distance-to-S3' tables (`chain123prime.cpp`, `wing_dist`/`center_dist`,
  single-valued, no parity): built via multi-source BFS seeded at every
  good state, using the FULL 17-move `S2_OWN_MOVES_PM` set. avg=4.89/
  max=7 (wings), avg=6.76/max=11 (centers) — both much smaller than
  chain1234v2's literal-S4 tables, since S3' is a much closer waypoint.
- `chain123prime.cpp` alone (S0→S3' only) benchmarks dramatically faster
  than the old S0→S4 designs: COST12=8, COST23'=8 solves trial 0 in
  ~0.4-0.5s (27-move solutions) vs. chain1234v2's ~5.8s-best for a full
  S4 solve — expected, since S3' is a much easier target.

### chain1234prime.cpp: extending S3' all the way to literal S4

Adds a THIRD tier on top of S3': once in S3', two new tables give
distance-to-literal-S4, built via BFS restricted to the SAME 13-move
S3'-defining set (matching "solvable to S4 using ONLY these moves"),
starting from solved (a single BFS — the domain IS the orbit of solved
under these generators, no multi-seeding needed):

- **Wing-dist-to-S4** (`WING4_DIST`): domain is exactly the 80,640
  S3'-good wing states × 2 PLL parities = 161,280 entries.
- **Center-dist-to-S4** (`CENTER4_DIST`): domain is the 576 S3'-good
  (lr2520,fb24) pairs × 2520 free ud2520 classes × 2 PLL parities =
  2,903,040 entries.
- Both came back **fully reachable** (0 unreached) with small maxima:
  wing4 avg=8.52/max=11, center4 avg=11.40/max=16.

#### Compact wing indexing (verified, not hand-derived, per the user's hint)

The 12 wing-pairing coordinate slots (`POS_SLOTS[0..11]`) correspond 1:1
to the cube's 12 edges, which split cleanly into **8 "layer" edges** (4
touching U, 4 touching D) and **4 "equatorial" edges** (FR/FL/BR/BL,
touching neither U nor D). Identified computationally (not geometrically):
apply Uw2 (the E-slice half-turn, which by construction only ever moves
equatorial material) to the identity pairing state — whichever of the 12
positions CHANGE value are exactly the 4 equatorial ones; the unchanged 8
are the layer ones. This split is preserved by every move in the
restricted 13-move S3' set.

Within the S3'-good set: the layer sub-permutation (8 elements) is ALWAYS
an even permutation (20,160 possibilities, standard Lehmer-code ranking
with only even-parity ranks kept dense via a precomputed lookup table),
and the equatorial sub-tuple takes exactly **4** distinct values (found by
enumerating every observed equatorial tuple across all 80,640 known-good
states, not assumed) — 20,160 × 4 = 80,640, matching exactly. **Verified
via an exhaustive bijection check** before trusting any of it: all 80,640
good states map to distinct compact indices covering exactly 0..80639,
no gaps, no collisions, no invalid (odd-permutation) lookups. All of
this — the 8/4 split, the 20,160 even-permutation count, the 4 equatorial
tuples, the bijection — came back exactly as predicted on the first try.

#### A powerful, reusable admissibility-testing technique

When the search using these new tables started thrashing (see below), the
user proposed a construction that proves a table's admissibility *by
direct witness* rather than by re-deriving the BFS by hand:

1. From solved, apply "lots" of moves from `<U,D,R,L,F2,B2>` — exactly
   S2's own generators minus the 3 slice half-turns. By the classic
   reduction-method invariant (confirmed empirically: a lone `R` leaves
   `is_in_s4` true — see below), this move set can ONLY ever permute
   already-correctly-grouped centers/edges among themselves, so it
   **provably preserves `is_in_s4`** — landing on state X, itself
   confirmed `is_in_s4(X) == true`, but not literal identity.
2. From X, apply `n` MORE moves from the 13-move S3'-restricted set, with
   the FIRST one forced to be a slice move (Rw2/Uw2/Fw2 — ensures the
   trivial "still S4" configuration is actually left, and exercises PLL
   parity), reaching Y.
3. Since X is a genuine S4 state and Y is exactly `n` moves from X, the
   TRUE distance from Y to S4 is `<= n` by construction. If either
   `wing4_dist(Y)` or `center4_dist(Y)` ever exceeds `n`, that's
   definitive proof of a non-admissible bug — no need to trust or re-derive
   the BFS's own logic.

Implemented as `--verify-heuristic N` in chain1234prime.cpp: 2000/2000
random trials, 0 violations — the wing4/center4 TABLES themselves are
correct. This is a genuinely reusable pattern for verifying any future
distance table in this project: find a subgroup of the full move set that
provably preserves the target property, scramble within it to get a
"free" witness state, then extend by a small explicit path and check the
table's own reported distance doesn't exceed the path length.

### THE bug: `min` instead of `max` when combining wing4/center4

The user's original spec called for `h_4 = min(wing4_dist, center4_dist)`.
This is **unsound**: `min` gives `h=0` whenever EITHER sub-problem happens
to be solved, regardless of how far the OTHER one is — a state can have
wings in the literal identity permutation (`wing4_dist=0`) while centers
are still `ud2520=2452, lr2520=182, fb24=17` (`center4_dist=12`, nowhere
near solved), and `min(12,0)=0` reports it as already done. Reaching S4
requires BOTH to be solved simultaneously, so the harder of the two must
dominate — `max`, exactly like every other tier in this heuristic.

**Symptom this caused**: with `min`, IDA*/ida-inadm/rbfs all hung
(tested: plain `ida` >5min, `ida-inadm` >3min, `rbfs` >3min, even with
COST3'4 raised to 60) on a single trial-0 (100-120 move) benchmark, despite
the admissibility-verification test above passing cleanly (that test only
ever checks `wing4_dist`/`center4_dist` individually against a witness
`n`, so it can't catch a bug in how the two are *combined*). The massive,
systematic underestimate meant huge numbers of states near the S3'
boundary looked like they had `h=0` or near-0, so the search kept diving
into and thrashing around this "trap" region without terminating.

**Root-caused via node-progress dumping**: added `--progress N` (prints
the fixed trial scramble, the current search path-so-far, and the
heuristic/tier at that node, every N nodes explored — hooked into `dfs`,
`dfs_inadmissible`, and `rbfs`). By ~40M nodes the dump showed a search
stuck at a FIXED ~29-move prefix landing in tier 3 (S3', h=0-6, plausible
values) while endlessly retrying different ~9-20-move tails without
success — the search had found a real S3' checkpoint but couldn't get
past it, exactly the shape a systematically-wrong "you're basically done"
signal would produce. The user then spotted the exact failing state from
that dump by eye and asked for it to be re-verified directly.

**Nailed down via direct REPL replay**: extended the REPL to also parse
whole-cube rotation tokens (`x`/`x'`/`y`/`y'`/`z`/`z'`, matching
`ROTATE_CLASS_FWD/INV_NOTATION` — previously REPL-only accepted real move
names), so the EXACT scramble+partial-solution sequence from the progress
dump could be replayed and inspected directly, rather than trying to
cross-reference against an external tool's own (unrelated) indexing
scheme. This confirmed: `is_in_s3prime`=true, `is_in_s4`=false, `h=0`
— and printing the raw `ud2520`/`lr2520`/`fb24`/pairing values alongside
the exact `CENTER4_DIST`/`WING4_DIST` array indices and lookups showed
`center4_dist=12`, `wing4_dist=0`, confirming `min` was the culprit outright.

**Fix**: `h = std::max(h_center, h_wing);` (was `std::min`). After the
fix: 50/50 short-scramble correctness trials pass with no hangs, and
trial-0 (100-120 range) solves in 2.3s at COST3'4=14 (41 moves) — notably
*faster* than chain1234v2's best comparable result (~5.8s for 41 moves).

### Tacit knowledge from this arc

- **A lone "R" (or any combination of non-slice outer-layer turns)
  legitimately satisfies `is_in_s4` — this is CORRECT, not a bug.**
  First flagged as suspicious, investigated at length (including via
  Twizzle), and ultimately confirmed by the user: `is_in_s4`'s center
  check folds together exactly **16** raw arrangements per axis pair (4
  rotational states of the L face × 4 of the R face, not all 4!×4!=576
  arbitrary same-face permutations) — a real, deliberate equivalence
  matching the physical fact that a same-colored center looks identical
  under any 90°-multiple rotation about its own axis. A single outer-layer
  quarter turn (like `R`) only ever rotates one face's own 4 centers among
  themselves and relocates matched edge-pairs as whole units (verified
  directly: applying `R` to solved produces exactly two disjoint 4-cycles
  among 8 wing pieces, one entirely within `POS_SLOTS`, one entirely
  within `NEG_OF` — meaning each edge's own twin-pairing survives
  perfectly, just relocated), so it can never break an already-reduced
  cube's `is_in_s4` status. Don't re-litigate this if it comes up again —
  it's settled, and matches how `CENTER2520_LR`/`CENTER2520_UD` have
  always been constructed (dating to chain124.cpp).
- **Twizzle (alpha.twizzle.net) is useful for independently checking cube
  move semantics, but its internal piece/slot indexing has NO relationship
  to this project's own indexing** — don't try to line up specific index
  values between the two (a real dead end hit this session). Only
  STRUCTURAL/physical facts (e.g., "a quarter turn produces two disjoint
  4-cycles among 8 edge pieces, each cycle staying within one
  positive/negative sub-type") transfer usefully; read those off Twizzle's
  own `experimentalModel.currentPattern.get()` state object (accessed via
  `javascript_tool`, not the rendered 3D view — the WebGL canvas doesn't
  reliably render/screenshot in this headless browser pane) rather than
  trying to cross-reference raw index numbers.
- **The `--verify-heuristic` admissibility-witness technique (above) and
  the `--progress N` node-dump technique (above) are now both permanent,
  reusable CLI flags in chain1234prime.cpp** — reach for both together
  next time a new distance table's search hangs or behaves pathologically:
  the witness test tells you if a TABLE is wrong, the progress dump tells
  you WHERE (which tier/region) the search is actually getting stuck, and
  neither alone would have found this session's bug (the table itself was
  fully correct; only the two tables' *combination* was wrong).

### `--profile N` and the endtable removal

Added a `--profile N` CLI mode to chain1234prime.cpp for quick apples-to-
apples cost sweeps: runs N random-scramble trials with the current
`--cost12`/`--cost23p`/`--cost34p`/`--search` settings, prints each trial's
stats (scramble len, solution len, time, nodes, nodes/s) as it finishes, then
a summary block (success rate, avg/min/max time, length, nodes, throughput),
and exits without dropping into the REPL — designed for scripted sweeps
rather than interactive use. `--search` also gained a new default:
`ida-inadm` instead of `ida` (still overridable).

Also fully removed the depth-3 endtable machinery from chain1234prime.cpp
(`p1::endtable_multi`/`build_endtable_multi`/`ENDTABLE_DEPTH`/`NO_ENDTABLE`
and `p2full::ENDTABLE`/`build_endtable`/`pack2`, plus the `--endtable-depth`/
`--no-endtable` flags and their heuristic lookups), per a prior finding that
they were a net loss for this heuristic — confirmed again here with a fresh
20-trial `--profile` comparison at identical seeds/scrambles (fixed RNG seed
2024): building+consulting the endtables cost ~18% wall-clock time (avg
1795ms vs 1474ms with `--no-endtable`, vs 1346ms after removing them
outright and no longer paying their build cost either) while producing
*zero* solution-length change on every single trial. Root cause: this
heuristic's tiers are already tight enough (COST23'/COST34' flat gaps) that
the depth-≤3 endtable refinement essentially never fires as the binding
bound, so its hash-map lookup overhead is pure waste. **If chain12.cpp,
chain123prime.cpp, or chain1234v2.cpp are revisited, they still carry the
old endtable code** — this removal was scoped to chain1234prime.cpp only,
since that's the file actively worked on when the regression was
reconfirmed.

**Standing rule going forward: never implement endtables in this project
again**, in any solver file. Confirmed net loss here; don't reintroduce the
pattern even where older files (chain12.cpp, chain123prime.cpp,
chain1234v2.cpp) still use it as precedent.

### S1->S2 rotation fork (the z-conjugate split)

Extended the existing S0/S1->S1 rotation fork (chain1234v2.cpp-era: when a
move first lands in literal S1, split into `{identity, x-conjugate,
y-conjugate}` and try all three, since which of the 3 physically-equivalent
representatives you land on affects how quickly the REST of the solve goes)
with an analogous fork at the S1->S2 boundary: when a move first lands in
literal S2 (wing solved + FB centers solved), split into `{identity,
z-conjugate}` and try both.

**Why z specifically, and why only 2 candidates instead of 3:** rotations.h's
6 whole-cube-rotation classes are: identity, and 3 transpositions of the
`{UD,LR,FB}` axis-pair roles (`x`=swap_UD_FB, `z`=swap_UD_LR, `y`=swap_LR_FB),
plus 2 three-cycles (compound, not single quarter turns). At the S1 boundary,
any of the 3 transpositions works (S1 doesn't distinguish axes), so the
original fork arbitrarily picked 2 of the 3 (x, y) for branching-factor cost
reasons. At the S2 boundary, FB is ALREADY solved and must STAY solved by
whichever conjugation is applied — z is the unique transposition that FIXES
FB (swaps UD and LR, leaves FB alone), so it's the only one of the 3 that's
even a candidate; x and y would each move FB's role onto a different axis and
break `is_in_s2` under conjugation. This was verified computationally (a
dedicated `CONJ literal-S2 check (z only)` sanity test, 200/200, 0 mismatches
— see "how to probe a candidate conjugation" note below), not just argued.

**Reusable technique for testing a candidate conjugation preserves some
membership predicate**: don't build a probe via a long random walk in the
FULL move set and hope it lands in the target set (S2 is astronomically
smaller than S1 — needs wing solved AND FB solved simultaneously — so that
random-walk-and-hope approach hung forever, a real bug hit this session).
Instead construct a probe you KNOW is in the target set by applying only
moves from that set's own "own moves" generator list (e.g. `S2_OWN_MOVES_PM`,
already used to build the distance-to-S3' tables) starting from solved, then
conjugate and check. Deterministic, fast, no luck required.

Implementation notes: the two forks' win/attempt counters are separate,
precise arrays (`g_fork1_*` for S1's identity/x/y, `g_fork2_*` for S2's
identity/z) rather than one shared array — a shared array risks miscounting
across fork types if `open_cls` values aren't disjoint by construction (they
happen to be here, but don't rely on that implicitly). A shared
`conjugate_state6(state, cls)` helper factors out the actual conjugation math
so both forks (and any future one) use one verified implementation.

### Center-distance penalty (U/D and L/R centers ignored too early)

Observation (per the user): tier 1's heuristic (`h1`, in S1 not S2) only
looks at wing pairing and F/B centers, completely ignoring how far the U/D
and L/R centers are from solved — even though those axes still have to be
resolved eventually (S2's `CENTER_DIST` and S3's `CENTER4_DIST`). Likewise
tier 2 (in S2, not S3') only looks at LR+FB jointly via `CENTER_DIST`,
ignoring U/D (only resolved at tier 3). A node that's badly scrambled on the
ignored axis looks artificially close to the goal.

**Fix**: two new distance-to-solved tables, `UD2520_DIST`/`LR2520_DIST`
(2520 entries each), built via a plain single-source BFS from the solved
class (index 0) over the ALREADY-BUILT `UD2520_TRANS`/`LR2520_TRANS` reduced-
coordinate transition graphs (S1's own 21-move generator set, `P2_NUM_MOVES`)
— reusing existing, already-verified infrastructure rather than building a
new BFS over the raw 40320-permutation space by hand. Both tables came out
avg=8.2524, max=12 — IDENTICAL between UD and LR, which makes sense (they're
structurally isomorphic coordinates under this move set), not a bug.

Heuristic changes: tier 1 adds a penalty for BOTH U/D and L/R distance; tier
2 adds a penalty for U/D distance only (L/R is already fully counted via
`CENTER_DIST` there). Penalty formula, per the user's spec:
`PENALTY_RATE * max(0, dist - PENALTY_THRESHOLD)` — fractional (0.5
increments by default), accumulated as a `double` inside `heuristic()` and
rounded up ONCE at the very end, not per-term. Both constants are CLI-tunable
(`--penalty-threshold`, `--penalty-rate`) alongside `--cost12`/`--cost23p`/
`--cost34p`, since the user explicitly flagged that the initial guesses
(5, 0.5) might not be optimal either — don't bake them in as gospel.

**The `drop_credit`/`tier_inadm_from_code` interaction bug this surfaced.**
`dfs_inadmissible`'s `drop_credit` optimization (lets the search skip
straight to a computed lower retry-threshold instead of trying every integer
threshold one at a time) is derived from `tier_inadm_from_code(tier)` — a
constant that used to correctly equal the EXACT extra padding `heuristic()`
adds beyond a tier's own intrinsic distance estimate, back when that padding
was purely the fixed COST12/23'/34' constants (a clean function of tier
alone). Adding the penalty made the true padding vary STATE-BY-STATE within
a tier (same tier, different UD/LR distance -> different penalty), while
`tier_inadm_from_code` still only knew about the tier. Symptom: 10-20x
wall-clock and node-count regression on an identical short profile run,
immediately after wiring in the penalty, with the penalty otherwise
mathematically doing exactly what was asked. Tried making the accounting
"exact" first (thread the real per-state `inadm` value through the
recursion, matching `h` exactly by construction) — measured IDENTICAL node
counts and wall-clock to the broken version, disproving the "just a
bookkeeping mismatch" theory. Root cause is deeper: `drop_credit`'s speed
benefit depends on EVERY node within a tier sharing one constant value, so
that `chain_first`'s multi-level skip-ahead stays valid without rechecking
at every level; the penalty breaks that shared-constant invariant no matter
how precisely you compute it. **Fix**: keep the penalty in `h`/`f` for
ordinary heuristic-based pruning (that's the intended, working effect), but
compute `drop_credit` from `tier_inadm_from_code(tier)` alone, deliberately
ignoring the penalty for that ONE purpose — cut the wall-clock regression by
~4x (same node counts, much less per-node overhead) without touching the
penalty's actual effect on search quality. **Lesson for future heuristic
additions to this project**: if a new term makes the heuristic's
"inadmissible padding" vary WITHIN a tier rather than being a clean function
of tier alone, check whether `drop_credit`-style optimizations assume
tier-level uniformity before wiring the new term into them — the "more
exact" version is not automatically the "more correct" version when the
surrounding algorithm's speed depends on a specific structural assumption,
not on exactness per se.

Testing framework: `--profile N` already served as this (per the user's
ask, "get the testing framework ready so I can manually find the best COST
values") — it now sweeps 5 constants together (`--cost12`, `--cost23p`,
`--cost34p`, `--penalty-threshold`, `--penalty-rate`) with per-trial output
plus avg/median/min/max summary stats (median added per the user's request
in the same arc).

### Anytime search (`--node-budget`) — attempted, reverted, null result

**The idea** (per the user): after IDA-inadm finds a first solution, keep
searching (bounded by a node budget) for a SHORTER one, retrying with
`best_len = found_len - 1` each time something better turns up; report the
shortest found once the budget is exhausted (or, if nothing has been found
yet when the budget is hit, keep going regardless until the first solution
appears, then report immediately — can't be starved forever by a low
budget). Implemented as `solve_anytime()`, a wrapper that repeatedly calls
the existing `solve()` with a tightening cap, with `g_nodes` (already a
persistent global) accumulated against `g_node_budget` (default 500,000)
across attempts.

**First bug found**: every trial showed exactly "1 round" no matter the
budget, including trials that finished in a few hundred ms even with a
budget of 2,500,000. Root cause: `solve_ida_inadmissible`'s outer threshold
sweep starts AT `h0 = heuristic(start)`, and `dfs_inadmissible`'s root-level
pruning check (`h > local_threshold - g`, evaluated at `g=0` with the ROOT's
own `h`) ALSO rejects any attempted threshold below `h0` — so calling
`solve(start, best_len, ...)` with `best_len < h0` fails INSTANTLY, zero
exploration, regardless of whether a genuinely shorter solution exists.
Since `h0` is not a true lower bound (the heuristic is deliberately
inflated — the user's own correction mid-session: "the heuristic is NOT
admissible across phases! so h=37 could potentially give a solution of
length <37"), and ida-inadm is specifically tuned to find its first solution
AT OR NEAR `h0`, `best_len = found_len - 1` lands below `h0` almost every
time, hard-failing the "look for something better" retry before it can even
start. This is a hard STRUCTURAL property of `dfs`/`dfs_inadmissible`/`rbfs`
as written (same root-check pattern in all three) — not specific to
ida-inadm.

**The attempted fix**: a `g_allow_below_h0` flag, set only during retry
attempts (never the first, normal search), that bypasses the root-level
heuristic check ONLY at the true root (`g==0` / the initial `dfs`/`rbfs`
call) while leaving every deeper node's pruning untouched, applied to all
three search methods (`dfs`/`solve_ida`, `dfs_inadmissible`/
`solve_ida_inadmissible`, `rbfs`/`solve_rbfs`) since `solve_anytime`
dispatches through whichever one `--search` selects. Verified working via
node-count deltas on a clean before/after comparison (same trials, budget=0
vs budget=500000): trials where round 1 finished under budget showed
genuinely nonzero extra node counts on retry (e.g. +90, +249432 nodes) where
before the fix they were always exactly 0 — proof the retry was actually
exploring, not instantly bailing. Also had to fix the diagnostic itself to
separate "attempts" (total retries, whether or not they found anything) from
"successes" (retries that actually improved on the previous best), since a
failed-but-real improvement search looked identical to "never tried" under
the original wording.

**Final result: null.** A 35-trial run at `--node-budget 600000` found ZERO
improvements — every single trial's first solution held up as the best found
even after a real, verified-exploring retry attempt at `best_len - 1`. Empirically, for this heuristic/cost combination, IDA-inadm's first solution
is already at or extremely close to optimal essentially always, leaving
little to no room for the "keep searching" idea to find anything, at least
at the budgets tested. **Per the user: this is a genuine null result, not a
failure of the implementation — the mechanism was verified to work
correctly, it just didn't find improvements in practice.** The user has more
sophisticated follow-up ideas for a future session. All of this (`--node-
budget`, `solve_anytime`, `g_allow_below_h0`, the attempts/successes
diagnostics) was REVERTED after confirming the null result — chain1234prime.cpp
is back to plain `solve()` calls everywhere, verified via node-count match
against the pre-anytime baseline. The `--profile` median-stat addition from
the same arc was NOT reverted (unrelated, still wanted).

**Reusable lesson**: when retrofitting "keep searching after success" onto
an IDA*-family search, remember the outer threshold sweep's OWN starting
point (`heuristic(start)`) is itself a hidden floor on what solutions the
search can ever return, independent of whatever cap you pass in — worth
checking explicitly before assuming a "search with a lower cap" primitive
will actually explore below that floor.

## Phase 5 (S4->S5): corner orientation, added as tier 4 on chain1234prime.cpp

Implements the user's spec for the next waypoint past chain1234prime.cpp's
literal S4 (centers+edges fully solved, corners untouched — the ONLY thing
left unaddressed by this project's entire solver up to this point).
Confirmed via the witness example (`L R' F2 L' R` uses only moves from
`<U,D,R,L,F2,B2>`, exactly the theoretical S4 generator set from
`phase_sizes.py`) that this is the right base to build on, not a fresh
chain — chain1234prime.cpp's "literal S4" and phase_sizes.py's theoretical
S4 are the same coset, just reached by a different (much bigger) jump.

**S5 conditions** (verified against three witness examples before trusting
the implementation — see below): (1) corners oriented (3^7 = 2187, the
standard invariant that 8 raw twist values always sum to 0 mod 3); (2) R and
L centers each independently "solved or 180 from solved" (2x2 = 4 raw
values); (3) the 4 equatorial-origin dedges (FR/FL/BR/BL-type, confirmed
geometrically as not touching U/D) all sit somewhere among the 4 equatorial
POSITIONS (12-choose-4 = 495); (4) a parity tie between the equatorial-edge
permutation and how many of {L,R,F,B} centers are at their 180-rotated
state — checked only when (1)-(3) already hold.

**Move set for the distance table**: `<U,D,R,L,F2,B2>` (14 raw move
indices — all 3 powers of U/D/R/L, plus F2/B2) — exactly the set already
established (chain1234v2.cpp's "tacit knowledge" section, this file) to
preserve `is_in_s4`, and exactly `phase_sizes.py`'s own S4 generator list.
Joint table: corner-orientation (2187) x L/R-parity (4) x equatorial-
occupancy (495) = 4,330,260 states, built via a single BFS from the
identity-triple, exactly like every other tier's waypoint table in this
project. Came back **fully reachable, max distance 11** (comfortably under
the user's "<=15" guess), nibble-packed to 2.17MB. `COST45` (default 7, the
user's initial guess) generalizes the established "cost stacks below the
tier that models it" pattern — added to tiers 0-3, dropped inside tier 4.

### Corner representation: an abandoned design, and why

The obvious representation — track each corner's `(slot, orientation)` as 8
piece-indexed values, mirroring how `corner_parity` and other scalars are
tracked — was **built, verified for regular move composition (2000/2000
matches against `cube_model.py`), and then abandoned** once whole-cube-
rotation conjugation (`conjugate_state6`, used at the S0->S1 and S1->S2 fork
points) turned out to need a value-transform for orientation that empirical
testing showed is **not a simple function of (destination, old orientation
value) alone** — ambiguous even holding both fixed, across hundreds of
random trials. Chasing this further (checking whether it was a departure-
vs-destination indexing bug, a composition-order bug, etc.) never converged.

**The fix**: track all 24 corner STICKERS (8 positions x 3 axes, local index
`pos*3+axis`) as a plain position-permutation array (`corner_sticker` in
`State6`), structurally identical to `wing_slot`/`center_slot` — no
per-piece orientation value stored anywhere. This makes move application
AND whole-cube-rotation conjugation both simple, proven-correct position
relabeling (verified: 0 mismatches across all 6 rotation classes x 200
random states), with zero special-casing. Orientation (the 3^7 coordinate)
becomes a **derived, stateless** quantity computed fresh from a
`corner_sticker` snapshot whenever the heuristic needs it — never tracked
incrementally, so there is no composability question to get wrong. General
lesson: when a "the obvious per-piece extra value" representation resists a
clean conjugation rule after real effort, check whether expanding to full
sticker-level granularity (matching how the *rest* of the project already
represents wings/centers) sidesteps the problem entirely, rather than
continuing to chase the value-transform.

Deriving that orientation value correctly (`generate_tables5.py`,
`signed_ori_from_sticker_perm`) also needed a non-obvious **chirality sign
correction**: raw axis-difference orientation (`dest_axis - 1`) does NOT sum
to 0 mod 3 over reachable states — found empirically (312/500 random states
failed the invariant) before it was trusted. The 8 corners split into two
chirality classes by the parity of how many of their (x,y,z) coordinates
equal 3; one class needs its raw axis-difference negated to get the actual
additive Z/3 coordinate (`CORNER_POS_PARITY_SIGN` in tables5.h). This is the
well-known "cube corners have alternating handedness" fact, rediscovered the
hard way rather than assumed.

### Condition 4: three witness examples, two rounds of fixes

The user's own worked example, `L R' F2 L' R` (claimed to satisfy 1-3 but
not 4), initially **passed** condition 4 under the first implementation —
confirmed via independent cross-check against `cube_model.py` directly (not
just re-reading the C++), ruling out a simple coding bug before asking the
user to reconcile the mismatch.

**Round 1 fix** (from the user's clarification): F and B centers had been
tracked via `center4_perm_parity` (permutation parity: even/odd) — correct
for L/R (condition 2 genuinely only cares about evenness, since L/R remain
full-quarter-turn-capable through tier 4's own move set and really do cycle
through all 4 rotation states), but WRONG for F/B in condition 4. Since
`S2 = <U,D,R,L,F2,B2,...>`, quarter F/B turns have not been available since
phase 2 — so once `fb24==0` holds (is_in_s4's own gate), F's raw state can
ONLY be "identity" or "the F2 double-transposition", and **both are EVEN
permutations** — `center4_perm_parity` returns 0 for both and can never
tell them apart. Fixed with a new `center4_is_shifted` (plain identity
check) used for F/B specifically. This made the original witness pass.

**Round 2 fix** (from two more user-supplied examples, `L U2 D2 L2 U2 D2 L'`
and `L U2 D2 L2 U2 D2 L`, both claimed to fail condition 4): the first still
passed under the round-1 fix. Root cause: condition 4's "how many of
{L,R,F,B} need to be rotated 180" was being computed from F/B's
`center4_is_shifted` ONLY — L and R were left out entirely, on the
(wrong) reasoning that "d==0 already forces L/R even, so they contribute
nothing new". That conflated "solved" and "180" as equivalent for this
purpose, but they are DIFFERENT physical states that both happen to be
"even" — exactly the F/B mistake, one level up. Fixed by applying
`center4_is_shifted` to **all four** of L, R, F, B (valid for L/R here too,
since by the time condition 4 is evaluated, `d==0` already guarantees they
are at one of exactly the two even states). Verified against all three
witness examples afterward (two must fail, one must pass) with 0
mismatches, plus a 15-trial `--profile` regression with zero invalid
solutions.

**Reusable lesson**: "this coordinate is already known to be even/good, so
it can't matter" is a trap whenever a LATER condition distinguishes between
the specific even states that an EARLIER condition already collapsed
together — the collapsing was correct for its own purpose (condition 2) but
throws away information a different, later condition (4) still needs.
Cross-check any "this shouldn't matter" reasoning against a concrete witness
before trusting it, exactly as the corner-orientation sign correction above
also required.

### `is_in_s5_mod_conjugation`'s retry set was too narrow (found via `--profile`)

A real user sweep (`--cost34p 11 --cost45 8`, full 100-120 scramble range)
hit `INVALID SOLUTION` on trial 0. Root cause: `is_in_s5_mod_conjugation`
(and the `is_in_s4_mod_conjugation` it was copied from) only retried
whole-cube-rotation classes `{y, x}` — the P2 fork's own two classes. But
`canonicalize_if_rotated`, called unconditionally inside `apply_move` on
EVERY move (not just when first entering S1), can land on ANY of the 5
non-identity classes whenever a state's `(ud,lr,fb)` triple coincidentally
matches a class's target triple — leaving a standalone rotation sentinel in
the returned solution that isn't paired with any fork's own open/close
bracket. The printed "invalid" solution's own notation gave this away
directly: it contained `[x' y]` (class 5's close notation) and `[z]`/`[z']`
(class 2) — classes the verifier never tried. The search and the solution
were both correct; the checker just didn't try hard enough to recognize a
solved-but-rotated result. **Fix**: broadened the retry loop to all 5
non-identity classes `{1,2,3,4,5}`. Re-running the exact failing command
afterward gave `success: 1/1`; a 10-trial regression at the same costs came
back clean. Also added scramble-printing to both `--bench`'s and
`--profile`'s `INVALID SOLUTION` messages (previously only the attempted
solution was printed, not the scramble that triggered it — needed to
reproduce and debug this at all).

**Reusable lesson**: a "verify modulo rotation" helper needs to retry the
FULL set of rotation classes the search can actually produce, not just the
subset a specific fork mechanism happens to use — these are two different
sources of rotation (one from an explicit, paired fork; one from an
unconditional, standalone canonicalization) and a retry set sized for one
will silently miss the other.

### Status

Implemented and correctness-tested: 3/3 phase-5-condition witness examples
match (including two rounds of user-supplied counterexamples that caught
real condition-4 bugs), 4/4 direct `h=`-value diagnostics match, 3/3
in/out-of-S5 diagnostics match, and `--profile` regressions (10+ trials
across two different cost combinations) show zero invalid solutions after
the `is_in_s5_mod_conjugation` fix above. `COST45=7` (the user's initial
guess) is still untuned — the `--cost45` CLI flag is ready for the same
manual sweep workflow used for COST12/23'/34'. A `COST34'=11, COST45=8`
sweep point gave 10/10 success at scramble length 100-120, solution lengths
44-46 moves, wall-clock 150ms-26s per trial (highly variable, not yet
characterized).

### S3'->S4 rotation fork, mirroring the S1->S2 one (per the user's direct request)

The same "split into {identity, z-conjugate}" trick used at the S1->S2
boundary applies here too, and for the identical structural reason: tier
4's own move set is `<U,D,R,L,F2,B2>` -- U,D,R,L all full-quarter-turn, F,B
both restricted to halves -- and z ("swap_UD_LR") is exactly the rotation
that trades the (already-symmetric) UD/LR pair while fixing F/B (the pair
already "settled", F2/B2-restricted since S2). Implemented as
`build_p5_fork_candidates`/`g_p5_forked`/`g_fork3_*`, wired into `dfs`,
`dfs_inadmissible` (gated on `tier == 3`, matching the S1->S2 fork's
`tier == 1` gate one level up), and `rbfs`. Verified via a new "CONJ
literal-S4 check (z only)" sanity test (200/200, 0 mismatches) BEFORE
trusting it in the search — built the same way as the S2 one: literal S4 is
astronomically rare, so random-walk-and-hope never terminates; instead
construct guaranteed-S4 states by scrambling ONLY with `p3prime::TIER4_MOVES`
from solved.

**One wrinkle `rbfs` needed that the other two search functions didn't**:
the S1->S2 fork and this new S3'->S4 fork both use z (`open_cls == 2`) for
their rotated candidate, so `rbfs`'s old trick of inferring "which fork
produced this successor" from `open_cls` alone (`cls_is_p3 = (open_cls==2)`)
broke — a P5-fork candidate would misfire as a P3-fork one. Fixed by
tagging each `Succ` with an explicit `fork_family` (0/1/2/3) at the point of
construction instead of inferring it afterward. `dfs`/`dfs_inadmissible`
didn't need this (each fork's branch checks its own guard flag immediately,
with no later re-derivation step), but it's a reusable lesson for any third
fork sharing an existing fork's rotation class: don't infer fork identity
from the rotation class number once two forks can share one.

Verified after implementing: the CONJ sanity test passes, `--profile`
regressions stay at 100% success with the fork actively contributing wins
on both branches (e.g. 6 identity + 4 z wins in one 10-trial run), and all
6 of the session's witness/diagnostic examples still report identical
`h=`/in-S5 values (as they must — `heuristic`/`is_in_s5` don't depend on
search-time fork bookkeeping at all, only on the state itself).

### Settled COST45 (user's manual sweep, post-fork)

**Best combo found so far: `COST12=9 COST23'=7 COST3'4=10 COST45=8`,
penalty(thresh=7, rate=1.0).** 30/30 success at scramble length [100,120]:
avg len=44.13, median=44, avg time=1256ms, median=1032ms, min=132ms,
max=3457ms, avg nodes=569922, throughput=0.45M nodes/s. As with every
earlier phase, this is a manually-found local optimum via the `--profile`
CLI tool, not an exhaustive search — a starting point for the next phase's
tuning, not a final answer (same caveat that applied when COST23' shifted
7->8 once phase 4 was added, and COST34' has moved around across this
session too).

### Reading the fork diagnostic lines

`attempts`/`wins` are per-candidate counters (identity vs. the rotated
conjugate) at each of the three fork points. `attempts` increments every
time the search crosses into the target tier for the first time along some
explored path and tries that candidate as a continuation; `wins` increments
only when that candidate's subtree is the one that ends up in the accepted
solution. Two things fall out of this that are easy to misread:

- **`wins` always sums to the trial count** (e.g. 12+9+9=30, 12+18=30,
  17+13=30 in the combo above) — each trial's accepted solution crosses
  each boundary exactly once, so the split tells you how often the plain
  vs. rotated continuation was the one that actually worked, not a
  probability of anything.
- **`attempts` scales with how early/shallow the boundary sits in the
  search tree, not with correctness or importance.** S1's boundary sits
  near the root, so IDA*'s backtracking crosses it thousands of times per
  trial while exploring and discarding candidates (attempts in the tens of
  thousands); literal S4 sits right before the goal, so only nearly-
  complete paths ever reach it (attempts in the tens, barely above the
  trial count). A small `attempts` count at a deep fork is expected, not a
  sign it's undertested.

At the settled combo above, the S4-fork's z-conjugate rescued 13/30 trials
that the identity continuation alone didn't solve at that threshold —
noticeably more than the 5/30 seen earlier in the session (COST34'=11,
COST45=8), for whatever combination of scramble-seed and cost-value reasons
— a real, cost-dependent contribution worth keeping in mind when comparing
sweep points, not just their aggregate length/time stats.

## Phase 6 (S5->S6): corner coset, M/S-slice occupancy, U/D centers

S6 conditions (per the user's spec, see chat): (1) corners reach the literal
`<U2,D2,R2,L2,F2,B2>` group -- 8! corner permutations reduced to 420 right
cosets of that order-96 subgroup, within the ambient group `<U,D,R2,L2,F2,
B2>` (order 40320 = all of S8); (2) all edges in their slice -- the 4
M-slice-origin dedges (UF/UB/DF/DB) sit somewhere among the 4 M-slice
POSITIONS within the 8 layer positions (8-choose-4 = 70; the 4 S-slice ones
then automatically fill the rest by pigeonhole); (3) U and D centers each
independently "solved or 180" (2x2 = 4 raw values, mirroring L/R's role one
tier up); (4) TWO parity ties, checked only once (1)-(3) hold: the M-slice
edge permutation's parity must match how many of {U,F,D,B} are at 180, and
separately the S-slice edge permutation's parity must match how many of
{U,R,D,L} are at 180.

**Move set for this tier: `<U,D,R2,L2,F2,B2>`** (`TIER5_MOVES`, 10 raw move
indices) -- confirmed by the user before implementing. This is exactly the
move set that preserves `is_in_s5`: U/D quarter turns never retwist corners
or break the layer/equatorial edge split (both already-established facts);
R2/L2/F2/B2 (all halves) never retwist corners either -- the classical
reason Kociemba's own G1=`<U,D,R2,L2,F2,B2>` is orientation-preserving. It's
also literally the ambient group condition 1's 420-coset table is defined
against, so one move set serves both halves of this tier.

**U/D center slots**, derived (not guessed) from `tables.h`'s
`UD_TARGET={4,5,10,11,12,13,18,19}` the same way L/R/F/B were split out of
their own combined pairs originally: sorted by `cube_model.CENTER_POS`'s
(x,y,z) tuple order, `U={4,5,12,13}` and `D={10,11,18,19}` are exactly the
two 4-element "y-extreme" subsets of `UD_TARGET` (the F/B-extreme ones are
already accounted for in `F_CENTER_SLOTS`/`B_CENTER_SLOTS`).

### The M/S-slice split bug: pairing-intactness is not position occupancy

To split the 8 "layer" dedge positions into 4 "M-slice" (UF/UB/DF/DB) and 4
"S-slice" (UR/UL/DR/DL), the first attempt copied the exact recipe that
found `LAYER_IDX`/`EQUATORIAL_IDX` one tier up: apply a move (R2, then L2)
to the identity *pairing* coordinate via `apply_move_to_perm12_p2`, and see
which of the 12 positions changed. Result: **0 positions changed** under
either R2 or L2 -- a build-time FATAL, not a working table.

Root cause: `apply_move_to_perm12_p2`/`compute_pairing` track PAIRING
INTACTNESS (whether a dedge's two wing stickers are still adjacent), which
only a WIDE move like Uw2 can disturb (splitting one wing sticker of an edge
from its twin). A plain OUTER move like R2 or L2 moves both stickers of
every dedge it touches together, as a rigid unit -- it NEVER breaks pairing,
so it left every pairing-coordinate entry unchanged. This is a genuinely
different question from "which physical position does a piece move to",
which is what the M/S split actually needed.

**Fix**: switched to a genuine LIVE position transition -- the same
`WING_PERM[m][POS_SLOTS[i]]` then `POS_INDEX[...]` recipe `EQUATOR_POS_TRANS`
already used one tier up for TIER4_MOVES -- applied to R2/L2 directly on the
12 real dedge positions. This correctly found 4 S-slice positions (R2
touches its own 2, UR/DR; L2 the other 2, UL/DL) and 4 M-slice ones,
cross-checked against F2/B2 touching exactly the complementary 4.

**Reusable lesson**: `apply_move_to_perm12_p2` and its pairing-coordinate
family are for tracking whether wing PAIRING survives WIDE moves specifically
-- they are the wrong tool for "which position did a move send this piece
to", even though both operate on a 12-element array and both start from the
"identity" state. Use the `WING_PERM`+`POS_INDEX` recipe directly for genuine
position questions.

### Corner-permutation coset table (420 = 8!/96)

Built entirely at runtime in C++ (no new Python generator needed) by reusing
`CORNER_POS_PERM`/`rank_perm8`/`unrank_perm8` already built for phase 5's
orientation coordinate. `H = <U2,D2,R2,L2,F2,B2>` (order 96, verified by
closure BFS from identity) is entirely EVEN corner permutations (every
generator is a double-transposition), so every right coset of H is either
all-even or all-odd -- a fact used later in phase 7.

**The "X\*P" composition direction** (per the user's own hint: "read left to
right, it's actually inverse-apply") needed working out carefully: two
corner permutations P1, P2 get the SAME coset id iff P2[k] = P1[h[k]] for
some h in H, for all k -- i.e. P2 = P1 *followed by* h, a RIGHT coset. This
is the structure that makes "any solution reducing P1 into H also reduces
P2 into H" literally true: continuing a live search by further moves M means
the new arrangement is M composed ONTO the old one (M applied outermost,
exactly how `apply_move` works everywhere else in this file) -- so the set
of M that land P in H is `H (composed with) P^-1`, identical for P1 and P2
exactly when P2 = P1 (composed with) h. This is a DIFFERENT composition
direction than `CORNER_POS_PERM`'s own per-move update loop, hence the
coset-enumeration code uses explicit `P[h[k]]` indexing rather than reusing
`corner_pos_apply` for that part (which IS correctly reused for the ordinary
forward BFS transitions, where physically applying a move is exactly what's
wanted).

Verified before trusting it: `H` closure has exactly 96 elements; coset
enumeration produces exactly 420 ids covering all 40320 ranks with no
collisions; and (paranoid check, since the math here is easy to get
backwards) 200 random trials confirming the coset-transition table gives the
SAME answer regardless of which of a coset's 96 members is used as the
representative -- i.e. that the whole "X\*P" construction really is
representative-independent, not just argued to be.

### S6's joint table is exactly HALF reachable, as the user predicted

420 (well, 96 once corners are confined to literal H -- coset id 0 is a
constant at this point, not a further coordinate) x 70 x 4 = 117,600 naive
combinations for the joint table; found (via plain BFS closure) that exactly
58,800 -- HALF -- are reachable. **Not a bug**: `H` is entirely even corner
permutations, but U-quarter and D-quarter turns are each a 4-cycle on
corners (ODD) and are ALSO the only moves that flip a bit of the `ud`
coordinate -- so `(corner coset parity) XOR (ud's own two bits XORed
together)` is conserved by every `TIER5_MOVES` generator, fixed at 0 by the
solved start. Verified this exact claim (not just "half is unreached"):
for every one of the 420 cosets, reachability matches the predicted parity
link exactly, with zero mismatches. Storage: nibble-packed with 15 (not the
BFS sentinel 255, which doesn't fit in 4 bits) marking the unreachable half
-- provably never looked up by a real `is_in_s5` state, but a big sentinel
is a safer failure mode than a small, plausible-looking wrong number if that
assumption is ever wrong.

### Status

Implemented and correctness-tested: builds cleanly, `is_in_s6(solved) =
yes`, `--bench`/`--profile` trials verified via a new `is_in_s6_mod_
conjugation` (superseding `is_in_s5_mod_conjugation` the same way that one
superseded `is_in_s4_mod_conjugation`, since S6 is now the interim terminal
goal before phase 7 existed). `COST56` defaults to the user's own suggested
initial guess (10, after starting at 8) -- untuned, ready for `--profile`.

## Phase 7 (S6->solved): corner-within-H, edges+centers -- the final tier

|S6| = 5,308,416 (per the user's spec), small enough for a single flat
distance table all the way to literal solved -- no further "penalty for a
still-ambiguous condition" layer needed, since S6's own two parity ties
already pin everything down exactly.

**Move set: `<U2,D2,R2,L2,F2,B2>`** (`TIER6_MOVES`, 6 raw move indices) --
confirmed by the user. This is literally `H` from phase 6, and it makes
sense as the final generator set: `is_in_s6` already confines U/D to
"solved or 180" (condition 3), the same 2-state confinement L/R/F/B already
have, so EVERY face is now halves-only.

**Indexing, per the user's own spec**: a corner-permutation coordinate over
H's own 96 elements (not the 420-coset coordinate one tier up -- once
corner permutation is confined to literal H, "which coset" is always 0, so
it has nothing left to distinguish) combined with an "edges+centers"
coordinate: naively (4!)^3 (an independent S4 permutation within each of
the M-slice, S-slice, and equatorial 4-tuples) x 2^6 (each of the 6 faces'
centers solved-or-180) = 13,824 x 64 = 884,736 raw combinations.

### Ground-truthing the user's "1/16" estimate -- it was right, split differently than expected

The user's own estimate was "only 1/16 of the 884,736 are reachable"
(55,296), combined independently with all 96 corner permutations (96 x
55,296 = 5,308,416). Building it that way produced a real build-time
mismatch: the edges+centers closure BFS found **110,592** states, not
55,296 -- exactly double.

Rather than guess at a fix, this got ground-truthed against REAL simulation
(temporary diagnostic code, since removed): a BFS over actual `wing_slot`/
`center_slot`/`corner_sticker` arrays via `WING_PERM`/`CENTER_PERM`/
`CORNER_PERM` directly, bypassing every one of this tier's abstract
per-slice transition tables entirely. It also found 110,592 for edges+
centers alone -- confirming the abstract model was RIGHT, not buggy. A
second ground-truth run tracking the FULL joint state (corner-within-H
combined with edges+centers together) found exactly **5,308,416** -- the
user's own predicted total, exactly.

**The reconciliation**: the edges+centers space alone is genuinely only cut
by 1/8 (three independent parity ties already known from tiers 5/6 --
equatorial-vs-LRFB, M-slice-vs-UFDB, S-slice-vs-URDL, each an even/odd
split; algebraically, XORing all three together telescopes to 0, so there's
no fourth INTERNAL constraint to find). The missing factor of 2 instead
links corner-within-H to the edges+centers state directly -- a parity tie of
the exact same shape as tier 6's own corner-coset/ud discovery, just not
(yet) reduced to a closed-form expression. Exactly half of the naive 96 x
110,592 = 10,616,832 combinations are jointly reachable, and 10,616,832 / 2
= 5,308,416 = |S6| exactly. The user's arithmetic was correct throughout;
the factor of 16 was just attributed to the wrong side of the split (all
internal to edges+centers, assuming full corner independence) rather than
being 1/8 internal + a separate corner-linking 1/2.

Handled the joint table the same way as tier 6's own half-reachable table,
with an extra rigor step: verified not just that the TOTAL unreached count
is exactly half, but that **every one of the 96 corner_h values pairs with
exactly EC_DENSE_NUM/2 = 55,296** of the edges+centers states -- a perfectly
balanced per-corner split, not just a coincidentally-right aggregate. A
lopsided split would mean a real bug even if the total happened to land on
N/2.

**Reusable lesson**: when a hand-estimated fraction produces a build-time
mismatch, don't assume either "my code is wrong" or "the estimate is wrong"
-- ground-truth BOTH candidate numbers against a from-scratch simulation
that doesn't share any code path with the suspect computation. Here that
resolved it in two BFS runs: one showed the abstract model was already
correct, the other showed exactly what total was actually right and let the
factor-of-2 discrepancy get correctly attributed to a corner/edges+centers
link instead of an internal edges+centers one.

### Status

Implemented and correctness-tested: builds cleanly (`edges+centers
reachable-state closure: found 110592 states (expected 110592)`, `S7 joint
dist table: ... unreached=5308416 ... reachability matches the
ground-truthed corner/edges+centers parity link exactly`), `is_in_s7(solved)
= yes`, multiple `--bench`/`--profile` runs at 100% success verified via a
new `is_in_s7_mod_conjugation` (superseding `is_in_s6_mod_conjugation`, same
pattern as every previous supersession in this chain). `COST67` defaults to
the user's own initial guess (9) -- untuned, ready for `--profile`. This
closes the chain: `chain1234prime`'s search now goes all the way from S0 to
literal solved.

**Deliberately no fork was added at the S5->S6 boundary** when phase 7 was
first implemented, despite the established pattern of adding one at each
tier transition -- see the next section for why, and why that turned out to
be the right call.

## The S4->S5 fork was worse than useless -- removed

After phase 7 was implemented (making tier 6, S6->solved, the true final
segment), the user asked directly: "why does S5 even have a [y] fork? since
S6 is fully symmetric, that fork won't decrease solution length." Correct,
and provable, not just plausible:

**The general principle**: if a whole-cube rotation φ conjugates a move
set's generators to themselves, then word-length under that generator set
is provably invariant under φ -- any solution of length n conjugates to
another length-n solution using the SAME generators. So `heuristic()` values
for a state and its φ-conjugate must be exactly equal for every LATER tier
whose move set φ also maps to itself, and the two search branches explore
isomorphic trees. Whichever is tried first (always identity, by
construction) wins essentially every time -- not because it's shorter, but
by pure tie-breaking. A fork is only genuinely useful when its rotation is a
symmetry of the CURRENT tier's move set but NOT of some LATER tier's --
then conjugating really does hand the search a differently-shaped remaining
subproblem.

Checked against every existing fork's own move sets:
- **S1->S2 fork (z)**: TIER4_MOVES (`<U,D,R,L,F2,B2>`, active one tier
  later) is NOT z-symmetric by the time you reach S4->S5 (`<U,D,R2,L2,F2,
  B2>`, LR only halves by then) -- z genuinely changes the remaining
  problem. Matches the data: z actually **wins more than identity** (19 vs
  11 in one sweep).
- **S3'->S4 fork (z)**: TIER5_MOVES (`<U,D,R2,L2,F2,B2>`, active one tier
  later) is only y-symmetric (LR<->FB), not z-symmetric (UD<->LR) -- also
  genuinely changes the remaining problem. Matches the data: fairly
  balanced (17 vs 13).
- **S4->S5 fork (y)**: but TIER6_MOVES (`<U2,D2,R2,L2,F2,B2>`, the FINAL
  tier, active for the rest of the search) is symmetric under y (and in
  fact under any axis-pair permutation, being fully halves-only on every
  axis) -- so y-conjugating at S5 entry doesn't change the S6->solved
  problem AT ALL, just relabels it. Matches the data: y **almost never
  wins** (3 vs 27 out of ~105k attempts each in one sweep) -- pure
  tie-breaking noise on a doubled search cost, not real benefit.

**Removed entirely**: `build_p6_fork_candidates`, `P6ForkCandidate`,
`g_p6_forked`, `g_fork4_wins`/`g_fork4_attempts`, `p6_fork_idx`, the fork
branches in `dfs`/`dfs_inadmissible`/`rbfs`, the "CONJ literal-S5 check (y
only)" sanity test (it existed only to validate this fork), and the
"S5-fork diagnostic" print lines in `--bench`/`--profile`. A move that
enters S5 now just recurses plainly, like any non-boundary move. Re-verified
after removal: `--profile --tier-stats` runs at 100% success, S1/S2/S4 fork
diagnostics unaffected and still showing real (non-degenerate) win splits.

**Reusable lesson**: a fork's rotation choice being a valid symmetry of ITS
OWN tier's move set (needed for correctness -- it must preserve the tier's
membership condition) is a NECESSARY condition for adding a fork, but not
SUFFICIENT for it to help. Benefit requires the rotation to stop being a
symmetry at some later tier; if a tier sits right before a fully-symmetric
final segment, its own boundary fork is free correctness-wise but pure
overhead performance-wise. Worth checking this BEFORE adding a fork at any
future final-tier boundary, not just after noticing a skewed win/loss ratio
empirically.

## `--tier-stats`: per-trial node breakdown by tier (opt-in CLI flag)

Added per the user's request for more visibility than the fork diagnostics
alone give ("I can see how many nodes enter S1/2/4/5 each time... how many
nodes were spent in S0/1/2/3/4/5/6"). Off by default (zero overhead unless
passed); pass `--tier-stats` alongside `--profile` to get, per trial:

- **`spent: S0=.. S1=.. ... S6=..`** -- how many nodes the search expanded
  while classified in each tier (tier = "the deepest S this node is in",
  computed once per node via `heuristic()`'s own `out_tier`). Mutually
  exclusive; sums to the trial's total node count.
- **`entered: S1=.. S2=.. ... S7=..`** -- how many times a MOVE took a node
  from NOT-in-S_k to in-S_k, i.e. a genuinely NEW arrival at that tier, not
  "how many nodes are currently anywhere within it".

**First implementation was wrong** (caught by the user before it shipped):
initially computed "entered S_k" as a cumulative/suffix count derived from
the tier histogram (`entered_S_k = sum of spent_tier[t] for t >= k`), on the
reasoning that `is_in_s_k(node) == (tier >= k)` by construction. That's
TRUE, but it answers "how many nodes are currently within S_k" (a
STANDING-population count), not "how many nodes newly crossed into S_k this
trial" (a TRANSITION count) -- the user's actual ask, and exactly what the
S1/S2/S4-fork diagnostics' `attempts` counters already track for their own
boundaries. **Fixed** by making `entered_S_k` a genuine transition check
(`!is_in_s_k(parent) && is_in_s_k(child)`) at every move, mirroring the
fork boundaries exactly (S1/S2/S4, where a fork exists, increment right
alongside the fork's own trigger condition) and adding two brand-new checks
for S3' and S6 (no fork exists at either boundary, so nothing was already
computing this). S7 is handled at the existing `is_in_s7(s)` check itself,
since a node reaching it ends the search immediately -- entered_S7 is
always exactly 1 per successful trial by construction, not a suffix
artifact.

**Verified by construction, not just by eye**: summing `entered_S_k` for
k=1,2,4 across a multi-trial `--profile --tier-stats` run reproduces the
existing `S{k}-fork diagnostic: attempts identity=N` count EXACTLY (checked:
15/3/49/4681 for S1/S2/S4/S5 respectively across one 5-trial run, matching
the separately-computed fork attempt totals to the digit) -- strong
cross-validation that both bookkeeping paths agree, computed via genuinely
different code.

Gating: `s_in_s3p`/`s_in_s5`/`s_in_s6` (the three tiers with no fork to
piggyback boolean caching on) are computed as `g_tier_stats && is_in_sX(s)`
-- short-circuiting to `false` without ever calling the (real-work)
`is_in_sX` when the flag is off, so normal `--bench`/`--profile`/REPL usage
pays zero cost for this feature.

## Cutting the S2->S3' wing-pairing table build from ~40 minutes

`build_wing_dist()` (the multi-source BFS building `WING_DIST`, the
distance-to-S3' table over the 239,500,800-entry wing-pairing dense-index
space) took ~40 minutes on a fresh build. Investigated by reading
[TPR-4x4x4-Solver](https://github.com/cs0x7f/TPR-4x4x4-Solver) (a real,
well-known 4x4x4 solver) for comparison, then applying what transferred.
Four independent optimizations, stacked:

### 1. O(n) Lehmer-code rank/unrank instead of O(n²)

Every permutation rank/unrank in this file (`pairing_rank_full`/
`pairing_unrank_full`/`pairing_parity` in `pairing_dist_io.h`, `rank_perm8`
x2 -- one in `p2full`, one in `p3prime` -- `unrank_perm8`, `rank_perm4`/
`unrank_perm4`) was a textbook O(n²) double loop (count inversions by
scanning all pairs) or an O(n) array-shift-per-digit unrank -- both O(n²)
overall. TPR's `Edge3.get()`/`set()` do the identical Lehmer-code ranking in
O(n): pack the "currently remaining values" (for rank) or "value at each
remaining rank" (for unrank) as 4-bit nibbles in one 64-bit register, so
"rank of value v among what's left" is a single shift+mask instead of an
inner scan, and "remove v from the pool" is one wide subtraction (rank) or
one shift+mask+add (unrank) instead of an O(n) array shift. Ported this
technique verbatim to every rank/unrank function in the file. Verified
byte-identical to the old O(n²) versions: n=4 and n=8 exhaustively, n=12 over
2,000,000 random trials for both rank/parity and unrank, before trusting it.

This alone cut the wing-dist build from ~40 min to ~13 min.

### 2. Flat two-buffer level-order BFS instead of `std::queue`

`std::queue<QItem>` (`QItem` = a 12-byte array + a `uint32_t`) is
`std::deque` under the hood, which pays for chunked-block allocation and a
layer of pointer indirection on every push/pop -- real cost over ~4 billion
transitions. Replaced with two `std::vector<QItem>` ping-ponged per BFS
level (frontier / next_frontier, swapped each round) -- same multi-source
BFS semantics (nothing about WHICH states end up at WHICH distance changes,
only how the frontier is stored/iterated), better cache/prefetch behavior,
and no per-push allocation once each buffer's capacity stabilizes.

This cut the (already Lehmer-fixed) build from ~13 min to ~8.8 min.

### 3. Nibble-packing + a depth-6-cutoff shortcut, from reading the table's own histogram

Asked for a histogram of the (already-built, cached) `WING_DIST` table's
values: turned out EVERY entry is in [0,7] (distances: 0:80640, 1:159672,
2:1567668, 3:10389720, 4:47907108, 5:130922102 (54.7% of the whole table!),
6:48438910, 7:34980 -- summing to exactly `PAIRING_NUM_DENSE`, i.e. the
domain is FULLY reachable, no unreached/255 states at all). Two consequences:

- **4 bits/entry loses nothing** (max value 7 fits in a nibble with room to
  spare) -- switched `WING_DIST` from a raw `std::vector<uint8_t>` to the
  `PairingDistPacked` struct already defined in `pairing_dist_io.h` for a
  DIFFERENT table (phase4.cpp/chain1234.cpp's own wing-pairing table),
  reusing its `pairing_get`/`pairing_set`/`pairing_load`/`pairing_save`
  directly. Halves the live table (239.5MB -> 119.75MB) and the on-disk
  cache; the old-format cache is auto-rejected by its now-mismatched file
  size, no manual migration needed.
- **The depth-6->7 BFS expansion is the single most expensive level for the
  least payoff**: the depth-6 frontier alone is ~48.4M states x up to 17
  moves =~ 823M transition checks, for a payoff of only 34,980 newly-
  discovered states. Since the domain is fully dense and max distance is
  exactly 7, ANY state not reached by depth 6 must be at distance EXACTLY 7
  -- skip that expansion entirely and blanket-fill the leftovers as 7 in one
  linear pass. Guarded by a FATAL check (expected leftover count, 34,980,
  hardcoded from the measured histogram) so a future change to
  `S2_OWN_MOVES_PM` that invalidates this shortcut's assumption gets caught
  loudly instead of silently producing a wrong table.

Verified byte-for-byte identical to the full from-scratch table across all
239,500,800 entries before trusting either the packing or the shortcut.

### On "3-bit" packing (considered, not implemented)

Since every value is in [0,7], 3 bits/entry (not 4) would be enough --
group 8 consecutive dense indices into 3 bytes (24 bits) instead of one
nibble per index. Clean to implement (read/write the whole 3-byte group as
a 24-bit word, no cross-byte straddling logic needed for individual
entries), but every WRITE becomes a read-modify-write of 3 bytes instead of
a masked single-byte write -- costly on the write-heavy BFS-build path,
worth it only if memory footprint itself becomes the binding constraint.
Not implemented; nibble-packing already got the whole table well under any
reasonable working-set concern.

### Net result, and a lesson about benchmarking on this machine

Lehmer fix + BFS scaffolding fix + nibble-packing/depth-cutoff together cut
the build from ~40 min to well under 10 min in same-session, back-to-back
comparisons. **Absolute wall-clock numbers from this session are NOT
reliable across gaps** -- re-running the SAME unchanged binary later in the
session (after several other multi-minute full-CPU builds) measured
815.5s where it had earlier measured 526.6s, a 55% slowdown with zero code
changes (thermal throttling or similar). Trust same-session, back-to-back
A/B comparisons; don't trust an absolute number measured after a gap or a
string of other heavy builds.

## Repository cleanup: `archive/`

The repo had accumulated ~150 files (superseded solver versions, one-off
sweep/debug logs, Python table generators, session scratch backups) that
aren't needed to build or run the current solver. Moved everything except
`cpp/chain1234prime.cpp`, `cpp/chain1234prime.exe`, `cpp/tables.h`,
`cpp/tables2.h`, `cpp/tables4.h`, `cpp/tables5.h`, `cpp/rotations.h`,
`cpp/pairing_dist_io.h`, `cpp/s3prime_wing_dist.bin`, this `CLAUDE.md`, and
`.gitignore` into `archive/` (nothing deleted, all a plain untracked
directory move). `archive/README.md` maps each moved generator script to
the header it produces, so regenerating a table later doesn't require
re-deriving that mapping from scratch.

## Search algorithm #4: Staged Beam Search

A structurally different 4th search algorithm (alongside `ida`,
`ida-inadm`, `rbfs`), per the user's spec. Unlike the other three (one
continuous search from scramble to solved, sharing ONE stacked/inadmissible
heuristic across all tiers and using the FULL 27-move set everywhere), this
one runs 7 INDEPENDENT plain-admissible searches, one per tier boundary
(S0->S1, S1->S2, S2->S3', S3'->S4, S4->S5, S5->S6, S6->S7), each restricted
to ONLY the moves that preserve the tier just reached, and each collecting a
WIDE POOL of candidates (not just the first solution) before sorting by the
NEXT boundary's own heuristic and handing the best ones to the next phase.

### The algorithm, precisely

For phase K (boundary S_{K-1}->S_K), given a sorted list of input
candidates (states already in S_{K-1}, each with a total move count `g`
from the true scramble and an accumulated move+rotation path):

- **Heuristic**: `h_K`, the PURE per-tier admissible heuristic extracted
  from `heuristic()`'s own tier branches -- e.g. h_2 =
  `max(g_wingT->dist[s.wing], g_fbT->dist[fb2])`, h_5 = `s5_dist_of(s)`, etc.
  NEVER the stacked/inadmissible version (no `g_costXY` flat gaps, no
  U/D or L/R center-distance penalty). This is a deliberate, understood
  trade-off: dropping that extra pruning power is exactly why some phases
  are far more expensive than the other 3 algorithms would be for the same
  sub-problem (see "the four bugs" below for how this was distinguished
  from an actual bug during debugging).
- **Move set**: ONLY the moves that preserve S_{K-1} (see the exact table
  below) -- "you should not consider moves that break the phase," per the
  user. Filtered for immediate-redundancy the same way `ALLOWED_LIST` is
  built globally (skip a move that's the same face-group as the last one
  unless its rank is higher), just restricted to the phase's own move set.
- **Threshold loop**: plain IDA*, `f = g + h_K` against a threshold shared
  across ALL input candidates for this phase, starting at
  `min(g + h_K)` over the inputs and incrementing by 1. At each threshold,
  EVERY still-eligible candidate (not just newly-eligible ones) gets its
  ENTIRE bounded DFS redone from scratch -- no memoization, by design
  ("plain ida*").
- **Every discovery counts, no dedup by state**: a candidate's own DFS
  doesn't stop at its first success -- ALL distinct move-sequences reaching
  the target tier within budget get recorded, even ones landing on the
  identical resulting state via a different path (two different paths
  COULD lead to different completion lengths later, so both are kept). The
  ONLY thing filtered is a classic IDA* re-search artifact: a target found
  via a path where `f(n) < threshold` at EVERY node (including the root)
  was already fully explorable -- and thus already found -- at the
  PREVIOUS threshold, so it's skipped as a re-discovery, not counted as
  new. Tracked via a `tight` flag threaded through the recursion,
  initialized `true` iff the root's own f equals the current threshold, and
  OR'd with `(f(child) == threshold)` at each step -- once true, stays true
  for the rest of that path.
- **Stop the INSTANT the target count is reached** -- mid-DFS, mid-
  candidate, mid-round. That round's sweep over any remaining candidates is
  left incomplete; no "finish the round first" grace period.
- **Forking**: reuses the SAME 3 existing fork boundaries from `dfs()`/
  `rbfs()` (`build_p2_fork_candidates` at S0->S1, `build_p3_fork_candidates`
  at S1->S2, `build_p5_fork_candidates` at S3'->S4 -- no fork at S2->S3',
  S4->S5, S5->S6, or S6->S7), attributed to the phase that PRODUCES that
  tier (a fork fires the MOMENT a move newly enters its target tier, so
  e.g. fork_s1 belongs to phase 1, not phase 2). Each fork variant of a
  discovery counts as its OWN separate candidate toward the target count
  (confirmed explicitly by the user) -- and, critically, the fork check
  must ONLY trigger at that exact transition edge (see bug #1 below).

### Move set per phase (the exact, user-verified table)

Given verbatim by the user as each S_k's own allowed generator set, then
converted to raw move indices and cross-checked against move-set constants
already in the file:

| Phase | Boundary | Moves | Raw move set used |
|---|---|---|---|
| 1 | S0->S1 | `<U,D,R,L,F,B,Rw,Uw,Fw>` | all 27 raw moves |
| 2 | S1->S2 | `<U,D,R,L,F,B,Rw2,Uw2,Fw2>` | ALL 21 of `P2_TO_FULL_MOVE_INDEX` |
| 3 | S2->S3' | `<U,D,R,L,F2,B2,Rw2,Uw2,Fw2>` | `S2_OWN_MOVES_PM` (17 moves) |
| 4 | S3'->S4 | `<U,D,R2,L2,F2,B2,Rw2,Uw2,Fw2>` | `S3PRIME_GOOD_MOVES_PM` (13 moves) |
| 5 | S4->S5 | `<U,D,R,L,F2,B2>` | `TIER4_MOVES` (14 moves) |
| 6 | S5->S6 | `<U,D,R2,L2,F2,B2>` | `TIER5_MOVES` (10 moves) |
| 7 | S6->S7 | `<U2,D2,R2,L2,F2,B2>` | `TIER6_MOVES` (6 moves) |

Phases 1, 4, 5, 6, 7 happened to already match an EXISTING, plausibly-named
move-set constant in the file. Phases 2 and 3 did NOT (see bug #2).

### CLI

```
--search staged-beam
--beam1 N --beam2 N --beam3 N --beam4 N --beam5 N --beam6 N   (defaults 500/25/1000/100/1000/500; phase 7 is always 1)
--beam-max-threshold N   (per-phase safety cap on threshold growth, default 60 -- raise if a phase reports "GAVE UP")
--beam-quiet             (suppress the per-threshold progress trace; useful for multi-trial --profile runs)
```

Progress trace (on by default) prints, per phase: starting candidate count
and initial threshold, then per-threshold `discovered=X/Y nodes=N (+delta)
elapsed=Xms`, then a `done`/`GAVE UP` line. `--profile N` with
`--search staged-beam` additionally prints a per-phase average node count
across all N trials (`g_phase_nodes[7]`, accumulated via `g_nodes` deltas
around each `run_phase` call) -- e.g.:
```
staged-beam nodes per phase (avg over N trials): 1(S0->S1)=... 2(S1->S2)=... ... 7(S6->S7)=...
```

### The four (five) bugs found while stress-testing this

Building and stress-testing this algorithm at `--beam1 1 ... --beam6 1`
(the minimum possible width, forcing the search to commit to a single
candidate at every phase with no fallback) is what surfaced FIVE real bugs
-- three of them pre-existing, project-wide correctness bugs that the
other 3 algorithms had been silently tolerating because they always had
an alternative branch to fall back to. **Reusable lesson**: a search
algorithm with no fallback path is a much better correctness stress-test
than one that can route around a bad state.

**Bug #1 -- fork applied unconditionally instead of at the tier-transition
edge (staged-beam-only implementation bug).** The first, straightforward
generalization of `dfs()`'s fork logic called `spec.fork(ns, variants)` on
EVERY move at EVERY depth throughout a phase's entire tree, not just when
`ns` newly satisfied `is_target`. `build_p2_fork_candidates` etc. are
UNCONDITIONAL rotation-relabeling functions (they don't check tier
membership at all), so this silently tripled/doubled the branching factor
at every single node in the whole tree -- compounding to ~3^depth extra
work. Symptom: phase 1 alone needed 85.8M nodes to find its first S1
candidate for a 100-120 move scramble (vs. an existing `ida-inadm` run's
entire S0-tier spend of 3.7-8.5M nodes for a FULL solve). **Fixed** by
gating the fork call on `spec.is_target(ns)`, matching `dfs()`'s own
`!s_in_s1 && is_in_s1(ns)` exactly (the `!s_in_s1` half is automatic here
since `rec()` already returns early whenever `s` itself satisfies
`is_target`). Result: phase 1 dropped from 85.8M nodes to 436,394 -- a
~200x reduction, and the ROOT CAUSE, not the "no penalty term, no
dedup" theory floated (and disproven) before this was found.

**Bug #2 -- wrong move sets for phases 2 and 3 (staged-beam-only
implementation bug).** Before the user gave the exact per-phase move-set
table above, phase 2 was assigned `S2_OWN_MOVES_PM` (17 moves, missing
F/F'/B/B' entirely) and phases 3 AND 4 were BOTH assigned
`S3PRIME_GOOD_MOVES_PM` (13 moves, missing full R/L) -- i.e. phase 2 was
using what should have been phase 3's set, and phase 3 was using what
should have been phase 4's set. Missing F/F'/B/B' specifically starves
phase 2 of exactly the moves most useful for solving FB (half of what h_2
measures), while h_2's own `fb_dist` table was built assuming their
availability -- an admissible-but-extremely-loose mismatch that looked, at
first, like inherent algorithm cost rather than a bug. **Fixed** by
introducing a dedicated `moves_phase2()` (all 21 P2 moves) and
`moves_phase3()` (`S2_OWN_MOVES_PM`), keeping `moves_phase4()`
(`S3PRIME_GOOD_MOVES_PM`, already correct). Confirmed against the user's
table cell-by-cell, not just "it got faster."

**Bug #3 -- `U_CENTER_SLOTS`/`D_CENTER_SLOTS` swapped (PRE-EXISTING,
project-wide bug).** After fixing #1 and #2, phase 1-6 became fast and
correct, but phase 7 (S6->S7) intermittently reported `h7=255`
("unreachable") for states that were genuinely, verifiably `is_in_s6`.
Diagnosed via a NEW, now-permanent regression check: apply a handful of
hand-countable `TIER6_MOVES` (U2,D2,R2,L2,F2,B2) sequences to literal
solved and check `h7` against the manually-counted expected distance (e.g.
"U2" alone must give h7=1). Found the pattern: any sequence with an
UNMATCHED U2 or D2 (not paired one-for-one) gave a wildly wrong h7; matched
pairs (or none at all) were fine. Traced to `center_mask_of` (reads
`U_CENTER_SLOTS={4,5,12,13}`/`D_CENTER_SLOTS={10,11,18,19}` to build the
6-bit "which faces are at 180" coordinate used by phase 7's edges+centers
table) actually having U's and D's real center-piece slot IDs SWAPPED --
confirmed directly: after a real `U2` move, `center_mask_of` reported bit 1
(the D bit) set instead of bit 0, while `center_mask_delta(U2)` (used when
BUILDING the abstract EC transition table) unconditionally assumes U2
toggles bit 0 -- a desync between the live-state computation and the
table's own construction. **This is not staged-beam-specific**:
`s6_dist_of` ALSO reads these same two constants (via
`center4_perm_parity`/`center4_is_shifted`) to build its own "ud" parity
coordinate -- the other 3 search algorithms were silently exposed to the
same swap the whole time, just apparently rarely enough (or steered around
well enough by their stronger, penalty-augmented heuristics) that it never
surfaced as a visible failure. **Fixed** by swapping the two arrays'
values. Verified via: (1) all 13 hand-counted TIER6_MOVES sequences now
match exactly (kept as a permanent, FATAL-on-mismatch startup self-test);
(2) a 15-trial `ida-inadm` regression run afterward produced NODE-FOR-NODE
IDENTICAL results to a pre-fix baseline (same node counts, same lengths,
same times) -- proving the fix changes nothing for states that don't hit
the swap's blast radius, while genuinely fixing the ones that do.

**Bug #4 -- `is_in_s6` was missing a 4th parity tie (PRE-EXISTING,
project-wide bug; found by the user, not via automated debugging).** Even
after fixing #3, a specific repro case (hand-reproduced move-by-move by the
user from a printed scramble+partial-solution) still showed `is_in_s6=true`
for a state whose `h7` was 255 under EVERY one of the 6 whole-cube rotation
classes -- meaning it wasn't a labeling/frame issue, `is_in_s6`'s own
conditions were genuinely incomplete. The user identified the missing
invariant directly: **the parity of the corner permutation restricted to
the tetrahedral half {UFR,UBL,DFL,DBR} must equal the parity of how many of
the 6 face centers are twisted 180 degrees.** Implemented as
`corner_tetrahedral_parity()`, using the EXISTING `CORNER_POS_PARITY_SIGN`
+1/-1 chirality bipartition from `tables5.h` rather than hand-deriving
which raw position index is literally "UFR" -- valid because `is_in_s6`
already requires `corner_coset==0` (corner permutation confined to H,
hence overall-EVEN) before this check runs, and overall parity = parity(+1
half) XOR parity(-1 half), so forcing overall-even makes the two halves'
parities ALWAYS agree regardless of which one you pick. Added as a new
guard inside `s6_dist_of`'s existing `if (d==0)` parity-consistency block
(alongside the pre-existing M/S-slice edge-parity check), returning
`S6_PARITY_MISMATCH_PENALTY` on violation. Verified: the exact repro state
now correctly falls through (search continues past it instead of
accepting it), all existing FATAL/CONJ checks still pass, and a fresh
15-trial `ida-inadm` regression again matched the pre-fix baseline
node-for-node.

**Bug #5 -- fork rotation brackets never closed in the recorded solution
path (staged-beam-only implementation bug; found by the user, hand-tracing
a printed solution).** Original design choice: once a fork opens a
whole-cube-rotation bracket in a candidate's path, never emit a matching
close, reasoning that the state genuinely continues in that rotated frame
for the rest of the search and a solved cube is a fixed point of every
rotation anyway, so it "shouldn't matter." **Wrong**: verified two ways.
(1) A `beam_checkpoint_all` debug tool (replay every candidate's own
recorded path via `apply_solution` from the true scramble, compare against
that candidate's own internally-tracked state) showed 100% of fork-using
candidates mismatched their own recorded path, at EVERY phase, even phase
1 alone with a single fork -- narrowing it to "any unclosed fork,
independent of how many compose." (2) The user then hand-traced a printed
3-fork solution against the real scramble and confirmed it PHYSICALLY
solves the cube, just not in the canonical orientation -- appending "y z2"
(a rotation from the FULL 24-element rotation group, not one of the 6
classes `is_in_s7_mod_conjugation` tries) at the very end would close it
out. So the net uncompensated rotation was real, and wasn't guaranteed to
be one of the 6 simple classes the existing rotation-tolerant verification
checks -- meaning "solved is a fixed point" doesn't help if the actual
final state, taken literally, is never checked against a full 24-element
rotation group. **Fixed properly**: `Candidate` now carries an
`open_stack` (currently-open fork classes, LIFO order) threaded through
`run_phase`'s recursion in exact lockstep with the `path` vector's own
push/pop of OPEN sentinels. Once phase 7 actually terminates, the winning
candidate's `open_stack` is closed out in REVERSE order (append a CLOSE
sentinel for each, LIFO) before returning `out` -- the ONLY point in the
whole algorithm where closing happens, since it's the only point where
nothing continues afterward. Verified: `is_in_s7` on a fresh replay of the
closed-out `out` (from the true scramble) is now `true` directly, no
rotation tolerance needed at all -- confirmed on the original 3-fork repro
case and reproduced as a permanent pattern (an `is_in_s7(replayed-from-out)`
check printed whenever `g_beam_verbose` is on).

### Current benchmark snapshot (informal, user-run)

`ida-inadm` @ COST12=9 COST23'=7 COST3'4=10 COST45=9 COST56=9 COST67=4,
penalty(thresh=7,rate=1.0), 30 trials: avg length 57.97, avg nodes 802,167.

`staged-beam` @ widths 450/20/2000/40/5000/10000/1, same scramble range:
avg length 59.30, avg nodes 18,178,864.

Staged-beam's node count is ~23x higher for a comparable (slightly longer)
solution length. **Not yet an apples-to-apples comparison**: the two
algorithms count "a node" over structurally different things --
`ida-inadm` visits each node once per single continuous search using a
MUCH stronger (stacked + penalty-augmented) heuristic, while staged-beam's
plain-IDA*-per-phase design deliberately re-does full bounded DFS from
scratch at every threshold increment with NO memoization and NO
deduplication by state (by the user's own explicit spec, kept simple for
now -- see chat). Both are working as designed; reconciling the counting
basis (or deciding it doesn't need to be reconciled) is open for later.

## Phase 1 lazy cube evaluation (ida-inadm only)

Per the TPR-4x4x4-Solver design note the user shared (see chat, referencing
https://github.com/cs0x7f/TPR-4x4x4-Solver): a phase search only needs to
*track* enough state to prune/search efficiently, which can be much smaller
than the full state the solver *retains* across a phase boundary. Applied
here to phase 1 (S0->S1) first, since it's the outermost/most-visited tier.

**The observation.** A phase-1 (tier 0) node's heuristic and `is_in_s1` test
depend on nothing but the 3-coordinate `p1::State4` projection (UD/LR/FB
center coset IDs + wing parity) -- both are already backed by `p1::Tables`
(transition + multi-source distance tables over the 735,471-entry coset
space). Yet the pre-existing `dfs_inadmissible` called the full
`State6::apply_move` on *every* tier-0 move, which copies four 24-int arrays
(`center_slot`, `wing_slot`, `corner_sticker`) plus `wing`/`corner_parity`
bookkeeping -- all completely unused until a move actually reaches S1. A
real `--tier-stats` trial makes the waste concrete: `S0=463934` nodes spent
vs. only `entered: S1=186` -- i.e. the full cube was being reconstructed on
over 2500x more nodes than ever needed it.

**The fix.** New `dfs_phase1(p1::State4, g, local_threshold, last_move,
chain_first, h)` recursion, used only while `tier0 == 0` (dispatched from
`solve_ida_inadmissible`), that never touches `State6`:
- `p1_apply_and_canon(s1, m, &rot)`: applies a move to the `State4` alone via
  `p1::apply_move`, then replicates `State6::apply_move`'s own
  `canonicalize_if_rotated` step using `p1::match_rotation_class` -- if the
  resulting coordinate coincides with a non-identity rotation class's target
  triple, the state is forced to `p1::SOLVED4` and the class returned as
  `*out_rot`, exactly mirroring the heavy path's behavior (including the
  `cls<=0` "already solved or no match" case reporting `rot=-1`).
- `heuristic_state4(s1)`: the tier-0 branch of `heuristic(State6)`, copied
  verbatim but computed from just the `State4` (same unweighted `inadm_raw`
  sum, weight applied once to the total).
- While a move stays in tier 0, `dfs_phase1` recurses into itself directly.
  This is **provably** identical to what `dfs_inadmissible`'s general
  `try_variant` would have done for a tier0->tier0 edge: `drop_credit` is
  `max(0, parent_inadm - tier_inadm_from_code(child_tier))`, and since
  parent and child share the same tier (0), `drop_credit` is always exactly
  0, which collapses the threshold-chaining loop to a single iteration at
  `t = local_threshold` regardless of `chain_first` -- so there's no
  generality lost by skipping the loop machinery entirely in this branch.
- The instant a move *does* reach S1 (`p1::is_in_s1(ns1)`), the full
  `State6` is reconstructed via `reconstruct_full_state(g_path)` -- which
  replays only the **solver's own path since the root** (bounded by search
  depth, typically ~10-20 moves) through the real, unchanged
  `State6::apply_move`, from `g_root_state6`. `g_root_state6` is set once,
  at the top of `solve_ida_inadmissible`, to the already-fully-extracted
  cube (`extract6(...)`'s output, itself computed once from the scramble
  before `solve()` is even called) -- so reconstruction is never anywhere
  close to replaying the original 100+ move scramble, only the phase-1
  portion of the search's own path. From that point on, forking
  (`build_p2_fork_candidates`), threshold-chaining, and everything deeper
  is untouched, calling straight into the pre-existing `dfs_inadmissible`.

**Correctness verification.** Rather than trust the algebra alone, added a
throwaway `getenv("FORCE_HEAVY_P1")` toggle for one session's A/B testing
(removed before finishing): ran identical fixed-seed trials with the new
lazy path vs. the old path forced on for every tier-0 node. Across two
different cost/penalty configurations (default costs at `--max-len 80`,
and the user's own `--cost12 9 --cost23p 7 --cost34p 10 --cost45 9
--cost56 9 --cost67 4 --penalty-threshold 7 --penalty-rate 1 --max-len
100`), node counts, solution lengths, and fork attempt/win diagnostics were
**bit-for-bit identical** between old and new -- confirming this is a pure
performance change, not an algorithmic one. Example: trial 27 of the user's
own config reproduced with nodes=549735, solution=59 in both cases.

**Benchmark results.**
- Per-node microbenchmark (`microbench: phase1 step, OLD/NEW`, isolating
  just the tier-0 step cost on an identical walk): **8-11x** faster
  (throughput varies by run/config, e.g. 33M -> 284M calls/s).
- End-to-end wall-clock on matched 10-trial A/B (default costs,
  `--max-len 80`): old total 103,172ms vs. new total 49,433ms -- **2.1x**
  overall solve speedup, even though phase 1 is only 1 of 7 phases (S0
  dominates node count in essentially every trial's `--tier-stats`
  breakdown, which is why the speedup is this large despite touching only
  one phase).
- Directly reproducing the user's own posted trial-27 data point (before
  this change): 751.26ms -> 388.84ms for the exact same 549,735-node
  search -- **1.93x** on that single trial.

Not yet done: phases 2-7 still pay the full `State6::apply_move` cost per
node (per the user, phase 1 was deliberately done first, one phase at a
time). The same design pattern (small tracked coordinate + lazy full-state
reconstruction only at the next phase boundary) generalizes directly to
S1->S2 and beyond, whenever that's tackled next.

## Disallowing tier-demoting moves (all search algorithms)

Per the user (see chat): a move that demotes the tier a node is already in
(e.g. leaving S1 back to S0) is rarely the efficient thing to do, so
disallowing it entirely cuts branching factor for every search algorithm,
not just staged-beam (which already had this restriction baked into its own
per-phase move sets).

**Exact move sets per tier** (raw move indices, same convention as
staged-beam's `moves_phaseN()`), now unified into a single shared
`TIER_MOVES[7]` table (`build_phase_moves`/`moves_phase1..7`, relocated out
of the `staged_beam` namespace to file scope so `dfs`/`dfs_inadmissible`/
`dfs_phase1`/`dfs_phase2`/`rbfs` can all use it):

| tier | allowed moves | count |
|---|---|---|
| 0 (S0, not yet S1) | `<U,D,R,L,F,B,Rw,Uw,Fw>` (all 27) | 27 |
| 1 (S1, not yet S2) | `<U,D,R,L,F,B,Rw2,Uw2,Fw2>` (all 21 P2 moves) | 21 |
| 2 (S2, not yet S3') | `<U,D,R,L,F2,B2,Rw2,Uw2,Fw2>` (S2_OWN_MOVES_PM) | 17 |
| 3 (S3', not yet S4) | `<U,D,R2,L2,F2,B2,Rw2,Uw2,Fw2>` (S3PRIME_GOOD_MOVES_PM) | 13 |
| 4 (S4, not yet S5) | `<U,D,R,L,F2,B2>` (TIER4_MOVES) | 14 |
| 5 (S5, not yet S6) | `<U,D,R2,L2,F2,B2>` (TIER5_MOVES) | 10 |
| 6 (S6, not yet S7) | `<U2,D2,R2,L2,F2,B2>` (TIER6_MOVES) | 6 |

`ALLOWED_LIST`/`init_canonical_order()` (the old tier-agnostic canonical-
order-pruned move list) were dead code once every caller switched to
`TIER_MOVES[tier].allowed[last_move]`, so they were deleted rather than
left around unused.

**Result** (30-trial ida-inadm profile, identical scrambles/costs before and
after): success 30/30 both, and -- notably -- the solution-length
distribution is *exactly* identical (avg/median/min/max all unchanged:
57.97 / 58.0 / 53 / 59), while:
- avg nodes: 802,167 -> 774,205 (a modest drop)
- avg wall-clock: 3083.81ms -> 1919.91ms, total 92,514ms -> 57,597ms
  (**1.6x faster**)
- throughput: 0.26M -> 0.40M nodes/s (branching factor per node dropped)

Confirms the premise directly: solution quality is untouched, but the
smaller branching factor (skipping moves that would immediately need to be
undone via a demotion) pays off as real wall-clock savings. Smoke-tested
`ida` and `rbfs` too (3 trials each, 100% success, all reaching solutions)
to confirm the shared `TIER_MOVES` table doesn't just work for ida-inadm.

## Phase 2 (tier 1) lazy evaluation

Same pattern as phase 1's lazy evaluation, one tier further in. Per the
user's own spec for what to track: the wing (edge) coset (already tracked
via `g_wingT`), the F/B center permutation rank (`fb2`, via `g_fbT`), and
the U/D and L/R center 2520-classes (`ud2520`/`lr2520`, used SOLELY for
heuristic()'s early-penalty terms, per the user: "L/R center coordinate,
solely for penalty" / "U/D center coordinate, solely for penalty").

**What was missing**: distance tables for all four already existed
(`g_wingT->dist`, `g_fbT->dist`, `UD2520_DIST`, `LR2520_DIST`), but there
was no *transition* table for `fb2` (`FbTables` only ever stored `.dist`,
never `.trans`), and no raw-move-index lookup into the existing
`UD2520_TRANS`/`LR2520_TRANS` tables (built keyed by `pm` 0..20, the P2-set
index, rather than the raw 0..26 move index a dfs loop naturally yields).
Added: `FB2_TRANS` (40320 x 21, built directly via
`p3prime::apply_move_to_fb_perm` + `rank_perm8`/`unrank_perm8` over every
raw index -- no BFS needed, just direct application, since we already know
the move) and `FULL_TO_P2_MOVE_INDEX` (a 27-entry reverse lookup into
`P2_TO_FULL_MOVE_INDEX`), both built once by `init_phase2_lazy_tables()`.

**`dfs_phase2`**: mirrors `dfs_phase1` closely -- tracks only the 4-field
`Tier1State{wing, fb2, ud2520, lr2520}` instead of the full `State6`.
Since `TIER_MOVES[1]` now excludes every tier-demoting move (see above),
this recursion never needs a "back to S0" case, only "stays in S1" (direct
self-recursion -- same drop_credit-always-0 argument as phase 1's tier-0-
to-tier-0 case, since parent and child share `tier_inadm_from_code(1)`) or
"reaches S2" (reconstruct the full `State6` and hand off to the existing
S1->S2 fork machinery / `dfs_inadmissible`). `dfs_phase1`'s own fork-handoff
now redirects to `dfs_phase2` whenever the S0->S1 fork candidate's tier
comes out to exactly 1 (the overwhelmingly common case), falling back to
`dfs_inadmissible` only for the rare double-jump straight into tier>=2.
`solve_ida_inadmissible` also dispatches directly to `dfs_phase2` if the
search's *root* state already happens to be in tier 1.

### The bug found while verifying this (and its fix)

Built two permanent, always-on startup self-tests first (following this
project's established pattern): "tier-1 lazy transition check" (10,000
random tier-1 walks, comparing `FB2_TRANS`/`UD2520_TRANS`/`LR2520_TRANS`-
based transitions against ground truth) and "tier-1 lazy heuristic check"
(7,500 samples comparing `heuristic_tier1()` against `heuristic(State6)`'s
own tier-1 branch). Both passed immediately, 0 mismatches -- the low-level
transition tables and heuristic formula were correct from the start.

Yet an end-to-end A/B test (same fixed-seed scrambles, `dfs_phase2` forced
off vs on) showed **wildly different** node counts and even different
solution *lengths* for the same scramble -- e.g. trial 0 went from
948,474 nodes / length 59 (correct, matching the tier-restriction-only
baseline) to 6,420,444 nodes / length 57 with `dfs_phase2` enabled. Since
both the transition tables and the heuristic were independently verified
byte-exact, the bug had to be in the surrounding control flow, not the
per-state math.

Root cause: `reconstruct_full_state(path)` -- the helper both `dfs_phase1`
and `dfs_phase2` use to recover a full `State6` from `g_root_state6` plus
the solver's own recorded path, only when a move actually crosses a tier
boundary -- silently ignored `P2_OPEN_SENTINEL_BASE+cls` entries in the
path. Those entries mark "from here on, moves were applied to the
x/y-conjugated variant of the state" (pushed by `dfs_phase1`'s S0->S1 fork
when it explores a rotated candidate via `build_p2_fork_candidates`, per
the existing S1-fork mechanism). `dfs_phase1` itself never hit this bug,
since it only ever calls `reconstruct_full_state` once, at the exact
S0->S1 transition, *before* any such sentinel could exist in the path yet.
But `dfs_phase2` lives entirely *after* that point and calls
`reconstruct_full_state` repeatedly (once per S1->S2 fork attempt deeper in
the search) -- so whenever the search was inside an x/y-conjugated S0->S1
fork branch, every one of those reconstructions silently replayed the raw
moves against the *unconjugated* root instead, producing a completely
different (but still validly-reachable, hence "still finds a real
solution") cube.

Found via a targeted debug harness (temporary, since removed): an
"entry mismatch" check at the top of `dfs_phase2` comparing its own tracked
`Tier1State` against `extract_tier1(reconstruct_full_state(g_path))`
pinpointed the exact node (depth 10) where they first diverged, which
narrowed straight to the open-fork-bracket case once cross-checked against
the fork attempt counts (`S1-fork diagnostic: attempts ... x=24065 y=24065`
in the buggy run vs `x=13123 y=13131` after the fix -- x/y-forked branches
were being explored almost twice as often as they should, a symptom of
`dfs_phase2` effectively searching a corrupted, easier-to-satisfy state
space inside those branches).

Fix: `reconstruct_full_state` now applies `conjugate_state6(cur, cls)`
whenever it encounters a `P2_OPEN_SENTINEL_BASE+cls` entry, then continues
replaying subsequent raw moves against the now-conjugated state. (A bare
`ROTATION_SENTINEL_BASE+cls` "close" entry needs no special handling here:
it's only ever appended on the success-unwind *after* a solution is already
found, by which point nothing calls `reconstruct_full_state` again.)

### Verification after the fix

Same A/B methodology as phase 1: re-ran the identical 30-trial, fixed-seed
profile with `dfs_phase2` forced off vs on. Every single trial's node count,
solution length, and the aggregate fork-attempt/fork-win diagnostics came
back **bit-for-bit identical** (`S1-fork diagnostic: attempts identity=13143
x=13123 y=13131`, `S2-fork diagnostic: attempts identity=718 z=703`, etc. --
matching in both runs to the last digit), confirming this is once again a
pure performance change layered on top of the tier-move-restriction, not an
algorithmic one.

**Benchmark result**: total wall-clock across the 30-trial profile dropped
from 57,597ms (tier-restriction alone) to 44,844ms with phase 2 lazy
evaluation added on top -- another **1.28x**. Combined with the tier-move-
restriction and phase 1's own lazy evaluation, this is a substantial
cumulative speedup over where ida-inadm started this session, with zero
change in the actual solutions found.

Not yet done: phases 3-7 (tiers 2-6) still pay full `State6::apply_move`
cost per node. Same pattern generalizes directly whenever tackled next.

## S3 vs S3' experiment: verdict

Explored replacing S2->S3'->S4 with the original, proper-subgroup S2->S3->S4
(S3 = `<U,D,R,L,F2,B2,Rw2>`) in a self-contained experiment under
`archive/cpp/s3_experiment/chain1234_s3.cpp` (ida-inadm only, no forks/
penalties added at the new S2->S3/S3->S4 boundaries, per the user -- kept
close to a clean comparison against S3'). Full writeup of what was built,
the tables, and the move-set verification is in that file's own comments;
summary here is just the outcome.

**A real discrepancy surfaced and was reported, not silently absorbed**: the
computationally-derived S3 F/B target count came out to 24, not the
expected 4 (verified two independent ways -- an isolated per-coordinate BFS
and a joint (lr2520,fb96) BFS both agree), making the derived `|S3| =
2520*24 = 60480` rather than `2520*4 = 10080`. The move-preservation
self-test (does each of S3's own 15 moves keep a state in S3) passed
cleanly regardless (0/300 mismatches), and per the user: this discrepancy
caused memory inefficiency in the experiment, not an incorrect headline
result -- the node-count comparison stands.

**Result** (ida-inadm, 15 trials, same scramble seed/costs as the S3'
baseline): solution length was essentially identical (avg 58.00 vs S3'
57.97) -- but S3 needed **~4.9x more nodes** (3,764,084 vs 774,205 avg) and
was **~14x slower wall-clock** (21.2s vs 1.5s avg), largely attributable to
S3's own (ud2520,fb24) joint table having 75% of its domain unreached
(45,360/60,480) using S3's narrow 15-move generator -- a direct consequence
of S3 being a much shallower reduction than S3' (|S4|/|S3| in the billions
vs |S4|/|S3'| in the tens of millions).

**Verdict (the user's own): S3 is too large a regression to go back to.**
Staying on S3' going forward; the experiment is archived for reference
(`archive/cpp/s3_experiment/`, see `archive/README.md`), not deleted.

## Phase 3 (tier 2, S2->S3') lazy evaluation

Same pattern as phases 1 and 2 (dfs_phase1/dfs_phase2), one tier further
in. Per the user's spec: track the L/R center 2520-class (`lr2520` -- the
actual progress signal) and the U/D center 2520-class (`ud2520` -- used
solely for the existing early-penalty term) via the already-existing
`LR2520_TRANS`/`UD2520_TRANS` transition tables, no new tables needed for
either. F/B (`fb24`) is tracked too even though not in the user's list --
`CENTER_DIST`/`CENTER_GOOD`/`is_in_s3prime` all need it, so it's an
unavoidable fourth field already implied by the existing tier-2 heuristic,
not an addition.

**The wing-pairing coordinate is the one piece that can't be reduced to a
transition table** (its own domain is 12!/2 -- far too large for a per-move
lookup table), so `Tier2State` still carries the raw `wing_slot[24]` array,
applying `WING_PERM` directly and recomputing `compute_pairing()` +
`pairing_rank_full`/`pairing_dense_index` fresh on every move -- exactly
per the user's own instruction ("you still need to store the state of the
wings individually, call compute_pairing, and Lehmer [rank] the pairing at
each step"). This is unavoidably the dominant cost per node, same as it was
in the heavy path; the difference this lazy tier removes is everything
ELSE `State6::apply_move` used to also update on every tier-2 node
(`center_slot[24]`, `corner_sticker[24]`, `corner_parity`, the phase-1/2
coset coordinates) that tier 2's own heuristic never actually reads.

`dfs_phase3` mirrors `dfs_phase2` exactly: `TIER_MOVES[2]` already excludes
every move that would demote tier 2 back to tier 1, so there's no "back to
S1" case to handle -- only "stays in tier 2" (direct self-recursion, same
drop_credit-always-0 argument as the other lazy tiers) or "reaches S3'"
(reconstruct the full `State6` and hand off to `dfs_inadmissible` --
**no fork exists at the S2->S3' boundary** in this project, unlike
S0->S1/S1->S2/S3'->S4, so this is a plain, unforked transition, simpler
than `dfs_phase1`/`dfs_phase2`'s own fork-handling branches).
`dfs_phase1`/`dfs_phase2`'s own fork `try_variant`s now dispatch to
`dfs_phase3` whenever a double-jump lands a fork candidate directly in
tier 2, exactly mirroring the existing tier-1 dispatch pattern.

**Verification**: A/B tested (temporary `getenv` toggle, since removed)
against the heavy path across 15 fixed-seed trials -- node counts,
solution lengths, and all three fork diagnostics (S1/S2/S4) came back
**bit-for-bit identical** between heavy and lazy, confirming this is a
pure performance change.

**Result, exactly as the user predicted**: a modest win, since the
wing-pairing recomputation remains the dominant per-node cost either way.
15-trial total wall-clock: 17,129ms (heavy) -> 15,788ms (lazy), **~1.09x**.
Small but real, and comes for free on top of the phase-1/phase-2 lazy
evaluation and the tier-move-restriction work already landed this session.

Not yet done: phase 4 onward (S3'->S4 and later) still pay full
`State6::apply_move` cost per node. Same lazy-evaluation pattern could
extend further if there's appetite, though per this tier's own result,
diminishing returns are expected the deeper the wing-pairing dependency
goes (it's already the dominant cost from tier 2 onward).

## Phase 4 (tier 3, S3'->S4) lazy evaluation

Same pattern one tier further in (dfs_phase1/2/3 -> dfs_phase4). Per the
user's spec: track the compact wing-pairing index (80640, already
established by WING4_DIST's own layer/equatorial decomposition), the U/D
center 2520-class (its own coordinate here, not just a penalty like at
tiers 1/2), a merged L/R+F/B compact center index (576 -- the user's own
suggestion, and exactly what `GOOD_CENTER_DENSE` already computes), and PLL
parity (2). Unlike tier 2's wing-pairing coordinate (12!/2, too large for a
transition table), **every one of these four fits a small transition
table** -- the key difference from phase 3, and why this tier's win is much
closer to tiers 0/1's than tier 2's modest one.

Two new transition tables were needed (`GOOD_WING_TRANS[80640][21]`,
`GOOD_CENTER_TRANS[576][21]`, both keyed by the same P2 move-space
convention as `UD2520_TRANS`/`LR2520_TRANS`/`FB24_TRANS`), built directly
from the existing `GOOD_WING_PERMS` list and `CENTER_GOOD`/`GOOD_CENTER_
DENSE` tables -- no new BFS needed, since the "GOOD" domain is already
fully enumerated and closed under `S3PRIME_GOOD_MOVES_PM`. `UD2520_TRANS`
is reused unchanged. PLL parity's transition reuses the exact
`TOGGLES_RWUWFW` incremental toggle already trusted by `build_wing4_dist`/
`build_center4_dist` (proven consistent with real `pll_parity(s)` at
lookup time by this project's own existing, working tables -- not a new
assumption).

`dfs_phase4` mirrors `dfs_phase2`'s structure (not `dfs_phase3`'s): the
S3'->S4 boundary **does** have a fork (`build_p5_fork_candidates`, the
z-conjugate fork, unlike S2->S3' which has none), so `dfs_phase4` handles
it the same way `dfs_phase2` handles the S1->S2 fork. `TIER_MOVES[3]`
already excludes every tier-3-demoting move, so there's no "back to tier 2"
case. All of `dfs_phase1`/`dfs_phase2`/`dfs_phase3`'s own fork/transition
`try_variant`s were updated to redirect into `dfs_phase4` whenever a
candidate lands exactly in tier 3, mirroring the existing tier-1/tier-2
dispatch chain.

**Verification**: A/B tested (temporary `getenv` toggle, since removed)
across 15 fixed-seed trials -- node counts, solution lengths, and all
three fork diagnostics (S1/S2/S4) came back **bit-for-bit identical**
between the heavy path and the lazy one.

**Result**: 15-trial total wall-clock 25,114ms (heavy) -> 20,992ms (lazy),
**~1.2x**. Smaller than tiers 0/1's 8-12x (tier 3's per-node cost was
already partly amortized by tier 2's own earlier optimization, and overall
wall-clock is now dominated by whichever tier's own table lookups are
priciest, not by full-cube copying) but a real, verified win stacked on
top of everything landed earlier this session.

Not yet done: tier 4 onward (S4->S5 and later) still pay full
`State6::apply_move` cost per node.

## Phase 5 (tier 4, S4->S5) lazy evaluation

Same pattern one tier further in (dfs_phase1/2/3/4 -> dfs_phase5). Per the
user's spec: track corner orientation (2187, transition table), R/L center
rotation mod 180 (4, a simple XOR delta rule -- `lr_delta(m)`), a
parity-aware equator-slice edge permutation coordinate (990, transition
table, built via an exhaustive 11880-raw-tuple BFS), and the sum of R/L/F/B
center rotations mod a full turn (4, a fixed per-move-name delta: R/L
quarter turns ±1, any of R2/L2/F2/B2 +2, everything else +0). All four fit
small transition tables/rules -- unlike tier 2's wing-pairing coordinate,
nothing here needs a raw per-piece array carried through the recursion.

`dfs_phase5` mirrors `dfs_phase3`'s NO-FORK structure (S4->S5 has no fork,
per the "S4->S5 fork was worse than useless -- removed" section above) --
`TIER_MOVES[4]` already excludes every tier-4-demoting move, so there's only
"stays in tier 4" (direct self-recursion) or "reaches S5" (reconstruct the
full `State6` and hand off to `dfs_inadmissible`).

### A real bug: `extract_tier4`'s `center_sum` was wrongly hardcoded to 0

First implementation assumed `center_sum = 0` unconditionally at the
tier3->tier4 handoff, reasoning "`is_in_s4` forces every center exactly
solved, so the rotation sum starts at 0." **This is wrong** -- `is_in_s4`
is checked via the REDUCED `CENTER2520_LR`/`CENTER2520_UD`/`FB24`
coordinates, which fold together all 4 rotational states of each face's
own 4-center group (this is exactly the already-documented "A lone R
legitimately satisfies `is_in_s4`" fact from the phase-5 corner-orientation
section above -- `is_in_s4` doesn't require literal identity on R/L, just
that each is confined to ITS OWN 4-cycle orbit). Since R and L keep full
quarter-turn moves all the way through S3'/S4's own move sets (unlike F/B,
restricted to halves since S2), a real S4 entry reached mid-search can
have R or L at any of its 4 rotational states, not just 0 -- so
`center_sum` at a genuine S4 entry can be any of 0..3, not always 0.

**Why the isolated self-test didn't catch this**: the permanent "tier-4
lazy transition/heuristic check" starts `extract_tier4` at the LITERAL
SOLVED cube specifically (not an arbitrary S4 state) and only then applies
`apply_tier4_move` incrementally -- at literal identity, rotation truly IS
0 for every face, so the hardcoded assumption happened to be correct for
that one specific starting point and the self-test passed 3997/3997 clean.
It never exercised a genuine mid-search S4 entry with nonzero R/L rotation.

**Found via a full A/B test** (`FORCE_HEAVY_P5` toggle, since removed):
node counts and even solution LENGTHS diverged between the heavy and lazy
paths on most of 15 trials. A `DEBUG_TIER4`-gated check (also since
removed) narrowed it to `center_sum` specifically -- ground-truthed via a
temporary `face_rot` lambda (detects which power of a reference
single-quarter move on a face reproduces its CURRENT 4-piece permutation,
by replaying that move from identity and comparing) -- which reproducibly
showed the tracked `center_sum` diverging from the true value by an odd
amount at nodes well past the tier-4 entry point, ruling out a simple
sign-flip bug (would only ever produce an even discrepancy) and pointing
instead at the entry value itself being wrong.

**Fix**: added `compute_center_sum(center_slot)` (built from the exact
`face_rot`-style detection proven correct in the debug harness, generalized
to all 4 faces via `face_rotation_amount`), and `extract_tier4` now calls
it instead of hardcoding 0. This is the correct general-purpose
"recompute center_sum from any live S4 state" function; the old assumption
was only ever valid for the literal-solved special case.

**Verification after the fix**: re-ran the same 15-trial A/B comparison --
node counts, solution lengths, and all three fork diagnostics (S1/S2/S4)
came back **bit-for-bit identical** to the heavy path on every trial. Also
strengthened the existing `dfs_phase5` entry check (which previously
excluded `center_sum` from its corner_ori/lr_rot/eq_coord comparison,
since the old `extract_tier4` couldn't serve as ground truth mid-tier) to
include `center_sum` too, now that `extract_tier4` is genuinely correct
at any point, not just at tier entry. A fresh 30-trial profile matches the
project's established tier-move-restriction baseline exactly (avg
nodes=774,205, avg length=57.97, median length=58.0 -- identical to the
numbers recorded before phase 5's lazy evaluation was even started),
confirming this is a pure performance change with zero effect on search
behavior.

**Reusable lesson**: when a "derive coordinate X from scratch at tier
entry" function is validated only via a self-test that starts from the
single literal-solved state, that's not sufficient to validate it as a
general "recompute from ANY tier-entry state" function -- the literal-
solved state can be a degenerate special case (here, the one point where
"is_in_s4 forces zero rotation" happens to also be true) that hides a
wrong assumption from a self-test built around it. When a coordinate's
"solved" condition is checked via a REDUCED/folded coordinate elsewhere in
this project (matching the established pattern from `CENTER2520_LR`/`UD`
folding 16 raw arrangements into one class), double-check whether a
LATER, FINER-GRAINED coordinate needs to distinguish between the states
that earlier folding collapsed together -- this is the same shape of
mistake as the phase-5 condition-4 "this shouldn't matter" trap documented
above (F/B's `center4_is_shifted` vs `center4_perm_parity`), just one
level removed (here the trap was in an ENTRY-POINT assumption rather than
a live-state computation).

## Phase 6 (tier 5, S5->S6) + Phase 7 (tier 6, S6->solved) lazy evaluation

Implemented together, per the user's own detailed spec (see chat) built
around **ID-packing**: instead of tracking each tier's own coordinates
separately from whatever the NEXT tier will need, pack a coarser
"heuristic-lookup" value and a finer "next tier's own coordinate" value into
one composite integer, so the S5->S6 handoff never has to re-Lehmer-rank
anything from a raw cube state at all -- the entire motivation being that S5
is visited far more than any other tier's transient dwell (a representative
trial: S5=74822 nodes vs S6=12933 node-entries, ~6:1, vs ~1000:1 for every
earlier tier boundary), so it's the one place in the whole chain where a
full-cube retrace at the boundary actually costs something.

### The four composite coordinates

- **Composite corner ID** (`COMPOSITE_CORNER_ID[40320]` -> `[0,49152)`):
  packs the existing 420-coset ID (`CORNER_COSET_ID`, lower 9 bits) with a
  96-valued "index within that coset" (upper 7 bits). Every right coset of
  H=`<U2,D2,R2,L2,F2,B2>` has EXACTLY 96 elements (Lagrange), so 7 bits
  always suffices, for every coset, not just the solved one. Built via the
  SAME right-multiply-by-H enumeration `build_corner_coset_table` already
  uses (`P * h_list[i]`), applied to every coset's own representative --
  this automatically reproduces `H_RANK_FROM_PERM8` exactly for coset 0
  (its representative is the identity permutation, so `identity*h_list[i]
  = h_list[i]`, giving local index i for h_list[i] -- exactly H_RANK_FROM_
  PERM8's own definition), while giving every other coset an equally
  well-defined (if arbitrary) numbering, needed only for one-to-one-ness.
  Verified two ways before trusting it: the mapping is exhaustively checked
  one-to-one over all 40320 raw permutations, and the transition table's
  own lower-9-bits are cross-checked against the already-verified
  `CORNER_COSET_TRANS` for every one of the 403,200 (rank, move) pairs.
- **M-slice / S-slice composite coordinates** (`SLICE4_COMPOSITE_M/S[1680]`,
  packing a 70-valued occupancy rank with a 24-valued within-slice
  permutation rank, `70*32=2240` range): the permutation half is only
  semantically meaningful once occupancy is exactly at goal (that IS S6's
  own condition 2) -- but it needs to be tracked continuously from S5 entry
  onward, long before occupancy reaches goal. Resolved the same way tier
  4's `eq_coord` was: track the RAW, piece-indexed LOCAL-LAYER-position
  tuple of the slice's 4 labeled dedges (values 0..7, exhaustively
  enumerated over all `8*7*6*5=1680` ordered 4-of-8 tuples -- `P(8,4)`, the
  exact analog of tier 4's `P(12,4)=11880`), which is UNCONDITIONALLY
  well-defined (it's just "where is piece j", never undefined regardless of
  confinement). The composite/reported value is a per-tuple lookup computed
  once at build time: occupancy always meaningful; "order" is a fixed
  placeholder (0) whenever occupancy isn't at goal, since every place the
  order half is read is gated on occupancy==goal already holding. The
  TRANSITION rule for "a tuple of 4 local-layer-positions under a move" is
  IDENTICAL regardless of which 4 pieces are tracked (purely a function of
  the raw values and the move's own `MSLICE_POS_TRANS`), so ONE shared
  1680-entry `SLICE4_TRANS` table serves BOTH the M-tuple and S-tuple
  coordinates -- only the two composite LOOKUP tables (interpreting "home
  positions" differently per slice) need to be separate.
- **E-slice permutation** (`EQ_PERM_TRANS5[24][10]`): unlike M/S, equatorial
  occupancy is ALREADY exact continuously from S5 entry onward (that's S5's
  own top-level condition), so this is a plain 24-state full-permutation
  tracker under tier 5's own 10-move set -- no raw-tuple indirection needed,
  direct reuse of the existing `slice_perm_trans`/`slice_perm_rank` machinery
  already built for phase 7's own edges+centers table one tier up.
- **8-bit center-rotation byte** (`compute_center_byte`/`apply_center_byte`):
  2 bits each for U and D (full mod-4 rotation -- needed because U/D remain
  full-quarter-turn-active throughout tier 5's own move set, exactly the
  reason tier 4 needed a full mod-4 `center_sum` for R/L specifically), 1
  bit each for R,L,F,B (plain "at 180" toggle -- confined to solved-or-180
  by this tier, so a half turn is always a clean binary flip). Bit layout is
  arbitrary but fixed and self-consistent (same convention as
  `center_mask_of`'s own comment).

### The S5->S6 handoff (the actual point of the exercise)

`derive_tier6_from_tier5` builds `Tier6State{corner_h, ec}` DIRECTLY from an
already-transitioned `Tier5State`'s own fields -- no `reconstruct_full_state`
call at all, unlike every other tier transition in this file:
`corner_h = corner_id >> 9` (valid the instant `corner_id & 511 == 0`,
i.e. S6's own condition 1), and `ec` is looked up via `EC_DENSE[ec_raw_
index(m_rank, s_rank, eq_rank, center_mask)]`, where `m_rank`/`s_rank` come
straight from the M/S composite's own lower bits and `center_mask` is built
directly from the center-rotation byte using `center_mask_of`'s exact bit
convention. `dfs_phase6`'s own tier5->tier6 handoff calls this instead of
the generic `try_variant`+`heuristic()`+`reconstruct_full_state` pattern
every other tier boundary uses, while STILL going through the same
`drop_credit`/threshold-chaining mechanics (a dedicated `try_variant6`
lambda operating on `Tier6State`+`heuristic_tier6` instead of `State6`+
`heuristic()`). A full-reconstruction fallback (`extract_tier6`) still
exists for the rare double-jump where an EARLIER tier's move lands directly
in tier 6+ , skipping dfs_phase6's own lazy checkpoint entirely.

No fork exists at the S5->S6 boundary (matching the user's own spec, which
didn't call for one) -- provably correct for the same reason the S4->S5
fork was removed: tier 5's own move set is y-symmetric, and every LATER
tier's move set (`<U2,D2,R2,L2,F2,B2>` from tier 6 on) is symmetric under
every axis-pair swap, so a fork here would face an isomorphic remaining
subproblem on both branches -- pure overhead, no benefit.

`dfs_phase7` (tier 6, the terminal tier) is the simplest of all seven --
`TIER_MOVES[6]` already excludes every tier-6-demoting move, there's no
tier below it to hand off to, and reaching S7 simply ends the search
(checked at the function's own top, mirroring how `dfs_inadmissible`/`dfs`
check `is_in_s7(s)` before anything else). No `try_variant`/threshold-loop
machinery needed at all for its own self-recursion (`drop_credit` is always
0 within a single tier).

### A real bug, caught by the self-tests before it ever reached the search

`slice4_composite_of` initially hardcoded the "is occupancy at goal"
comparison against `MSLICE_GOAL_RANK` (the M-slice's OWN goal rank)
regardless of which slice was being computed. This meant `SLICE4_COMPOSITE_
S`'s "order" half almost NEVER actually computed (the S-slice's own
occupancy-at-home rank is a DIFFERENT number than the M-slice's, so the
comparison essentially never matched), silently defaulting to the
placeholder 0 -- corrupting the S-slice's own rank fed into
`derive_tier6_from_tier5`'s `ec_raw_index` lookup.

**Caught by a NEW permanent self-test** (`tier-5 lazy transition/heuristic
check` / `tier-5->6 lazy handoff check` / `tier-6 lazy transition/heuristic
check`, added following the exact methodology the phase-5 `center_sum` bug
established this session): randomize the ENTRY point itself via a random
walk of the tier's own move set from solved (not just literal identity --
per the phase-5 lesson, a bug that's wrong at every genuine entry point
except the trivial solved one is invisible to a test that only ever
extracts there), THEN track incrementally and compare against heavy ground
truth (`is_in_s6`/`s6_dist_of`/`is_in_s7`/`s7_dist_of`/`heuristic`) at every
step. The handoff check additionally cross-checks `derive_tier6_from_tier5`
against `extract_tier6` (full reconstruction) every time a walk happens to
cross into S6, since that specific function is the actual point of this
whole phase and deserved its own direct verification, not just an indirect
one via the search's own success/failure. First run: 184/8478 tier-5
mismatches and 338/381 handoff mismatches, ALL traceable to the same root
cause. Fixed by passing each slice's own goal rank as an explicit parameter
to `slice4_composite_of` instead of hardcoding the M-slice's. Re-run after
the fix: 8478/8478, 381/381, and 7916/7916 -- all three checks clean.

**Reusable lesson**: when one function computes a coordinate for two
structurally-parallel-but-distinct things (here: the M-slice's own
composite vs the S-slice's own), don't reuse a "the" constant that's
specific to ONE of them (`MSLICE_GOAL_RANK` reads like a generic name but
is actually M-specific) -- pass the per-case value as an explicit parameter,
even when it feels redundant for the more "primary" case. This bug also
reinforces the earlier phase-5 lesson about self-test entry-point
diversity: the tier-4-style "walk from literal solved" test methodology,
once generalized to randomize the ENTRY point too (not just the walk after
entry), is now the standard pattern for every future tier's lazy-evaluation
self-test in this project.

### Verification and results

100-trial `--profile` run: **100/100 success, 0 invalid solutions**, node
counts and solution-length distribution matching the established
tier-move-restriction baseline EXACTLY, trial-for-trial, at the same fixed
seed (e.g. trial 0: 948,474 nodes / length 59, identical to every prior
phase's own baseline recording of that same scramble) -- confirming this is
once again a pure performance change, not an algorithmic one, this time
verified against an independently-recorded historical baseline rather than
a same-run `FORCE_HEAVY` toggle (this phase's self-tests already provide
stronger, more targeted verification than a toggle would have, since they
directly compare every new lazy function against heavy ground truth over
thousands of randomized-entry walks, not just aggregate node counts).

**Wall-clock result: the single largest speedup of this entire lazy-
evaluation arc.** 30-trial `--profile` total time dropped from 39,268.87ms
(phase-5-lazy baseline, before this phase) to **9,363.75ms** -- roughly
**4.2x**, dwarfing every earlier tier's own win (tiers 0/1: 8-12x on a
MUCH smaller fraction of total node count; tiers 2/3/4: 1.09x-1.35x). This
matches the user's own stated motivation exactly: S5 is where the search
actually spends most of its time relative to how briefly it dwells in every
other tier (~6:1 vs ~1000:1), so this is the one tier where full-cube
reconstruction overhead was actually the dominant cost, and removing it at
the specific S5->S6 boundary (not just making S5's own per-node work
faster, which the composite coordinates ALSO do as a side effect) is what
produced a result this much bigger than the earlier, more modest tier wins.

This closes out the lazy-evaluation arc for the entire S0->solved chain --
every tier (0 through 6) now has its own lazy `dfs_phaseN` recursion, and
`dfs_inadmissible` (the original, fully-heavy path) is only ever reached for
rare multi-tier double-jumps that skip a tier's own dedicated checkpoint
entirely.

## `cpp/phase1ab_experiment/` -- splitting phase 1 into 1a/1b

A self-contained experiment (per the user, see chat), NOT touching the
validated `chain1234prime.cpp` -- a full copy (`chain1234_p1ab.cpp`, plus its
own copies of `tables*.h`/`rotations.h`/`pairing_dist_io.h`/
`s3prime_wing_dist.bin`) in a new subfolder, matching the precedent set by
`archive/cpp/s3_experiment/`. Idea: replace phase 1's single S0->S1 jump
(index 18,931,023,540, too big for exact tables) with two smaller exact-table
phases through an intermediate waypoint: **1a** (S0 -> intermediate) and
**1b** (intermediate -> S1).

**intermediate = `<U,D,R,L,F,B,Rw2,Uw,Fw2>`** -- all 3 powers of U,D,R,L,F,B
and of Uw, but only the SQUARE power of Rw/Fw (unlike S1, which restricts all
three of Rw/Uw/Fw to squares). User-given indices: `[S0:intermediate] =
735471`, `[intermediate:S1] = 25740` -- both small enough for exact BFS
distance tables, matching phase 3's own "one exact joint table beats
max-of-separate-tables" precedent.

### Working out what "intermediate" actually IS, before writing any code

Rather than guess at a coordinate, the two given index numbers were reverse-
engineered first, then verified against the actual move geometry:
`735471 = C(24,8)` exactly (the same size as phase 1's own existing `ud`/
`lr`/`fb` coset coordinate), and `25740 = C(16,8) * 2 = 12870 * 2`. This
strongly suggested intermediate = "ud confined to its home set, nothing else
required" (a single existing coordinate reaching ITS OWN solved value) and
S1-from-intermediate = "given ud already confined, further confine lr to 8
of the remaining 16 belt slots (fb is then forced by pigeonhole), times wing
parity."

**Confirmed via move geometry, not just arithmetic**: `Uw` (`make_move(1,
{2,3}, true)`) only ever rotates a piece's (x,z) coordinates, NEVER its y --
so a piece's "y-extreme-ness" (whether it's U/D-family, i.e. whether it
counts toward the `ud` coordinate's tracked SET) is invariant under every
power of Uw. `Rw2`/`Fw2` (180-degree flips) DO cross between "extreme" and
"non-extreme" for the layer they grab, but only ever swap U<->D or F<->B
WITHIN their own already-extreme set (never crossing between the `ud` set
and the `lr`/`fb` sets) -- confirmed by direct coordinate-transform algebra
on `rotate2`'s own formula. So EVERY one of intermediate's generators
provably preserves `ud == UD_SOLVED_IDX`, meaning intermediate is exactly
the STABILIZER (in the group-theory sense) of `ud`'s solved value under
`S0`'s full 27-move action on the already-transitive 735,471-state coset
space (`build_ud_table`'s own BFS already proved S0 is transitive on this
space) -- by orbit-stabilizer, `[S0:Stab] = 735471` exactly matches the
user's given number, which is only possible if intermediate (a subgroup of
that stabilizer, by the argument above) equals it exactly, not merely a
subset. Verified computationally too (not trusted on the algebra alone): a
build-time `FATAL` check confirms every one of intermediate's 23 raw moves
actually leaves `T.trans[UD_SOLVED_IDX]` fixed, and the BFS reachability
counts (`UD_SOLO_DIST` reaches all 735,471; `LR_LOCAL_DIST` reaches exactly
25,740, not more or fewer) are asserted at startup.

### The two new tables (both reuse 100% pre-existing machinery)

- **`UD_SOLO_DIST[735471]`** (phase 1a's admissible heuristic): a
  single-seed BFS from `UD_SOLVED_IDX` alone, over the FULL 27-move `S0`
  connectivity, using the exact same `T.trans` table phase 1 already builds
  for its own `ud`/`lr`/`fb` coordinates. Deliberately NOT the existing
  `dist_multi` (which is seeded at all 3 rotation targets, for a different
  purpose -- see below): folding rotation-awareness into the heuristic TOO
  would double up on what the new root-level fork (see below) already
  provides, for no benefit.
- **`LR_LOCAL_DIST[735471*2]`** (phase 1b's admissible heuristic, keyed by
  `lr*2+parity`): a BFS from `(UD_SOLVED_IDX, 0)` restricted to
  intermediate's OWN 23-move connectivity (`INTERMEDIATE_MOVES`), reusing
  the SAME `T.trans`/`LR_MOVE_INDEX`/`WING_PARITY` machinery phase 1's `lr`
  coordinate already uses under the full 27-move set -- just iterated over a
  smaller move list. "Solved" `lr` is literally `UD_SOLVED_IDX` too (same
  raw number, since `lr` is expressed via the same underlying rank_subset
  table through `INV_CYCLE`'s relabeling -- `SOLVED4` already uses
  `UD_SOLVED_IDX` for all three of `ud`/`lr`/`fb`), so no new "solved"
  constant was needed.

### The root-level fork (not a boundary fork)

The user's own framing, taken literally: **"the STARTING STATE should be
forked (into three) ... there's no forking upon entering the intermediate
subgroup."** This is a different shape of fork than every existing one in
`chain1234prime.cpp` (S0->S1, S1->S2, S3'->S4 all fork at the MOMENT a
boundary is newly crossed, mid-search). Here, since S0 is symmetric under
all 24 whole-cube rotations but intermediate distinguishes ONE axis (the one
that keeps full `Uw`) from the other two, reaching literal `ud=solved`
specifically is only ONE of 3 equally-valid ways to reduce the SAME
scramble -- so the fork happens ONCE, on the root itself: build 3 candidate
starting states (identity, and the `x`/`z` whole-cube conjugates of the
root, reusing the existing, already-verified `conjugate_state6`), then run
the SAME `dfs_phase1a`/`dfs_phase1b` machinery independently on each,
picking whichever succeeds first at the current IDA* threshold (mirroring
every other fork's own "try each candidate in sequence, first success
wins" pattern, just at the root instead of mid-recursion).

**Why classes {identity, x=swap_UD_FB, z=swap_UD_LR}, not {x, y} like the
existing S0->S1 fork**: derived from `rotations.h`'s own `ROT_TARGETS`/
`DIST_SEL` structure (already used elsewhere for S1-mod-rotation matching):
class `y` (`swap_LR_FB`) leaves `ud`'s OWN target unchanged (per
`DIST_SEL[3]={0,1,2}` -- its `ud` slot still wants index 0, same as
identity), so it doesn't produce a NEW "which axis is special" target --
only `x` and `z` each redirect a DIFFERENT axis into the `ud` role
(`DIST_SEL[1]`'s `ud` slot wants `FB_RAW_IDX`, `DIST_SEL[2]`'s wants
`LR_RAW_IDX`). This is the opposite selection from the existing S0->S1 fork
(which uses `{x,y}`, arbitrarily omitting `z`, since ALL 3 transpositions are
equally valid there -- S1 restricts all three axes symmetrically). Every
fork's own class choice in this project has to be re-derived from what that
SPECIFIC boundary's move set actually distinguishes, never assumed from a
previous fork's choice -- this is the same lesson as the S1->S2/S3'->S4
forks' own `z`-only justification, just landing on a different pair this
time.

### dfs_phase1a / dfs_phase1b

Structurally mirror the original single `dfs_phase1` closely (same
`p1::State4` tracking, same `p1_apply_and_canon` move-application, same
verbatim-reused S0->S1 fork handling once literal S1 is reached -- entirely
unaffected by how intermediate was reached), split at exactly one new
internal edge:

- **`dfs_phase1a`**: `TIER_MOVES[0]` (all 27, unchanged), heuristic
  `UD_SOLO_DIST[s.ud] + COST_START_INT + COST_INT_S1 + <existing COST12+
  COST23'+...>`. When a move makes `ud` newly equal `UD_SOLVED_IDX` (without
  also hitting literal S1), hands off to `dfs_phase1b` via a LOCAL
  threshold-escalation using `COST_START_INT` as the drop_credit -- and
  this credit is EXACT, not the usual `tier_inadm_from_code`-based
  approximation, because both heuristics share the identical
  `COST_INT_S1 + tier_inadm_from_code(0)` tail and differ ONLY by
  `COST_START_INT`.
- **`dfs_phase1b`**: `TIER_MOVES_INTERMEDIATE` (intermediate's own 23
  moves), heuristic `LR_LOCAL_DIST[s.lr*2+s.parity] + COST_INT_S1 +
  <existing tail>`. No "back to 1a" case needed (the move set is proven,
  both algebraically and by a build-time FATAL check, to never leave `ud`
  confined). `is_in_intermediate`/`out_tier` in the GLOBAL `heuristic()`
  function were deliberately left UNTOUCHED -- since `solve_ida_inadmissible`
  now special-cases `tier0==0` entirely (computing its own per-candidate h0),
  the global heuristic's tier-0 branch value is never actually consulted for
  a real tier-0 state, only used to detect that `tier0==0` in the first
  place.

### New CLI flags and self-tests

`--cost-start-int` (default 5) / `--cost-int-s1` (default 4), the user's own
initial guesses, explicitly flagged by the user as needing their own manual
tuning pass (same workflow as every other COSTxy constant in this project).

Two new permanent startup self-tests, following this project's own
established "admissibility witness" technique (construct a state reached in
EXACTLY n moves via the tier's own preserving move set; the true distance is
provably <= n, so the table must never report more): 300 random-length
(0-40) walks via all 27 moves checked against `UD_SOLO_DIST`, and 300
similar walks via `INTERMEDIATE_MOVES` (also checking the `ud`-confinement
invariant itself) against `LR_LOCAL_DIST`. Both passed cleanly on the first
build (300/300, 0 mismatches) -- no bug found this time, unlike several
earlier tiers' own first attempts.

### Status: implemented, verified, NOT yet tuned

30-trial `--profile --tier-stats` run: **30/30 success, 0 invalid
solutions**. The root fork shows genuine, non-degenerate 3-way activity
(one representative run: 15 identity / 8 x / 7 z wins out of 30 -- none of
the 3 dominates, confirming the fork is doing real work, not just paying
overhead for a class that never wins). At the user's own untuned initial
guess (`COST_START_INT=5, COST_INT_S1=4`), results are WORSE than the
established tier-move-restriction baseline on the same 30-trial profile
(avg len 62.00 vs 57.97, avg nodes 2,455,435 vs 774,205, total wall-clock
~68.6s vs ~57.6s) -- expected and unsurprising, matching this project's own
repeated lesson that a freshly-introduced COST constant's first guess is
essentially never good (COST23' started at a guess of 4 and needed tuning
up past 7-10 before beating its own baseline). Per the user's own explicit
plan, `--cost-start-int`/`--cost-int-s1` are now ready for the same manual
`--profile`-driven sweep workflow used for every other constant in this
project -- not yet done.

### `--tier-stats` extended to show the intermediate checkpoint separately

Per the user's follow-up ask: `--tier-stats`'s `spent`/`entered` lines
originally lumped phase 1a and 1b together (both call `record_tier_stats(0)`,
since "intermediate" isn't one of the 7 numbered out_tier codes). Added a
separate pair of counters, `g_spent_intermediate`/`g_entered_intermediate`
(own `record_intermediate_stats()` helper, reset alongside the existing
ones), so the printed lines now read `spent: S0=.. Sint=.. S1=.. ...` /
`entered: Sint=.. S1=.. ...` -- `S0` is phase 1a only (before `ud` reaches
solved) and `Sint` is phase 1b's own dwell. Verified NOT frozen/stuck across
a 10-trial sample (`Sint` ranged 5 to 154, `entered: Sint` ranged 2 to 65) --
genuinely reflects each trial's own search behavior. As with the existing
`entered: S3'=`/etc. counters, `entered: Sint=` counts every structural
crossing (a move landing on `ud=solved`), INCLUDING ones immediately pruned
by `f > threshold` before `dfs_phase1b` is ever actually called -- so it's
normal, not a bug, for `entered: Sint` to be much larger than `spent: Sint`.

## `cpp/phase1_softmin_experiment/` -- a non-admissible "soft-min" phase-1 heuristic

A third, independent experiment (per the user, see chat) -- another full
copy in its own subfolder, a different idea entirely from the phase1a/1b
split above (tried instead of further tuning 1a/1b), based on the SAME
fully-lazy-optimized backbone (all of `dfs_phase2..dfs_phase7`), not the
plain single-phase design.

**Note for future sessions, re: which base to copy**: the live
`chain1234prime.cpp` in `cpp/` was, at one point this session (2026-09-28),
a SMALLER, earlier-looking version (4844 lines, `dfs_phase1`/`dfs_phase2`
only, `dfs_inadmissible` handling every tier from 2 up) than the 6267-line
version with the full `dfs_phase3..dfs_phase7` lazy recursions that the
phase1a/1b experiment was copied from just one session earlier -- i.e. the
working `chain1234prime.cpp` changed between those two points, from
something other than this session's own edits (this session never wrote to
it after the phase1a/1b copy). The FIRST attempt at this experiment was
copied from that smaller version and, predictably, ran 3-10x slower
per-node than expected (throughput as low as 0.20-0.50M nodes/s vs.
phase1ab's 1-3.5M) purely because it was missing tiers 2-6's own lazy
evaluation, paying the full heavy `State6::apply_move` cost for every S2+
node -- an artifact of which base file got copied, unrelated to the
soft-min/lock changes themselves. **Fixed by re-copying from
`phase1ab_experiment/chain1234_p1ab.cpp` instead** (the one known-good
fully-lazy 6639-line base) and mechanically reverting ONLY that
experiment's own phase1a/1b-specific additions (its new tables, forks,
`dfs_phase1a`/`dfs_phase1b`, CLI flags, self-tests) back to the ORIGINAL
single `dfs_phase1`/`heuristic_state4`/`solve_ida_inadmissible`, before
layering the soft-min change on top of THAT. **Lesson for next time a new
experiment subfolder is requested "based on chain1234prime.cpp": check
which lazy tiers the CURRENT file actually has before copying it as the
base for a performance-sensitive experiment** -- the file's own line count
and a quick grep for `dfs_phase3`..`dfs_phase7` is a fast sanity check.

### The change: `h = max(min(a,b,c), max(a,b,c) - SLACK)`

Per the user's exact spec, `p1::heuristic` (which combines the 3 rotation-
role distances `a=dist_multi[ud]`, `b=dist_multi[lr]`, `c=dist_multi[fb]`,
previously a plain `max(a,b,c)`) becomes `max(min(a,b,c), max(a,b,c) -
SOFTMIN_SLACK)` with `SOFTMIN_SLACK` hardcoded to 4 for now (a named
constant, ready to become a CLI flag later if worth tuning). This is
deliberately NON-admissible whenever `max-min > SLACK`: it lets the search
trust the CLOSEST of the 3 axes once the other two aren't "too much"
further away, instead of insisting all 3 reach 0 together before the
heuristic drops at all.

### The paired pruning rule -- two rounds of a real gap found by self-tests

Per the user's own spec: in phase 1's own tier-0 recursion (`dfs_phase1`),
whenever one of `a`/`b`/`c` is already 0 for a node, moves that would take
it back off 0 are excluded from the move loop -- and it doesn't matter
WHICH of the 3 rotation homes a coordinate currently sits at, only that it
stays at SOME home once there, since (per the user) all 3 coordinates
eventually reaching the 3 homes in some permutation already is literal S1
mod rotation (rotations are free -- see `match_rotation_class`).

The user supplied a much cheaper equivalent (mid-session, unprompted) to
re-deriving this via a table lookup: single-layer moves and `*2` wide
half-turns never cross a home boundary at all, so only the 6 ODD-power wide
"quarter" turns need checking, each crossing exactly one pair of homes.
**Two rounds of an exhaustive self-test caught two successively deeper
gaps in this simplification before either reached the search**:

1. **Round 1** (81 cases: 3 coordinates x 27 moves, testing only each
   coordinate's OWN home): a move-index rule keyed by WHICH COORDINATE
   (ud/lr/fb) is locked, applied to `ud`'s own raw value for all three,
   found 24 mismatches. Root cause: at that point "locked" incorrectly
   meant "at MY OWN home", when it should mean "at ANY home" (per the
   user's later clarification above) -- but even setting that aside, using
   `ud`'s own transition column to reason about `lr`/`fb` was wrong from
   the start.
2. **Round 2** (243 cases: 3 coordinates x 3 possible homes x 27 moves,
   after correcting the semantics to "any home counts, keyed by which
   PHYSICAL home is occupied, not by which named coordinate"): found ANOTHER
   24 mismatches, this time ALL on `lr`/`fb`, NONE on `ud`. Root cause:
   `lr`/`fb` don't store their coset value in raw/untransformed terms -- they
   reuse the SAME shared table as `ud` via the existing 120-degree-rotation
   relabeling (`LR_MOVE_INDEX`/`FB_MOVE_INDEX`, already used by
   `p1::apply_move`), so a given REAL move (e.g. `Rw`) maps to a DIFFERENT
   table-family move depending on which of the 3 coordinates is being
   updated. A move-index rule derived by reasoning about `ud`'s own
   (untransformed) column can't be ported to `lr`/`fb`'s relabeled columns
   by just relabeling the HOME identity -- the mixing pattern itself shifts.

**Final fix**: stopped trying to hand-derive a uniform move-index shortcut
across all 3 relabeled columns (this project's own repeated lesson: don't
keep re-deriving by hand once a self-test shows the abstract argument
doesn't transfer) and instead ask each coordinate's OWN transition table
directly -- `move_respects_locks(T, s, ud_locked, lr_locked, fb_locked, m)`
computes `T.trans[s.field][correct_move_index(m)]` per locked coordinate and
checks `dist_multi==0` on the result, exactly mirroring how
`p1::apply_move` itself already handles the relabeling correctly. This is
provably correct by construction (no shortcut to get wrong) and still
cheap: a coordinate only ever does a lookup when it's ALREADY at some home,
which is rare across most of the search. Re-verified with a THIRD version
of the self-test (243 cases: for each of the 3 coordinates x each of the 3
homes it could hold x each of the 27 moves, build a `State4` with ONLY that
one coordinate locked and check `move_respects_locks` against that
coordinate's own true transition): **243/243 matched, 0 mismatches**.

**Reusable lesson**: when someone (user or otherwise) hands you a
"this optimization is equivalent to X" simplification, especially one
based on a clean high-level argument, build the exhaustive/witness
self-test for it BEFORE trusting it in the search -- and don't stop at the
first self-test that passes a NARROWER version of the claim than what's
actually needed. Here, round 1's self-test was internally consistent but
was testing the WRONG semantics (per-coordinate home instead of per-home
occupancy); only widening the test to what the feature actually needs
(round 2) surfaced the deeper relabeling issue. When a self-test passes,
double check it's actually testing the claim being relied on, not a
convenient stand-in for it.

### Status: implemented, verified, and genuinely faster than baseline

`--profile 30 --tier-stats`, self-tests: **all pass** (`move-lock
correctness check: 243/243 matched, 0 mismatches`, plus every pre-existing
CONJ/tier-1/4/5/6 check unchanged). At default `COST12=9`: 30/30 success,
0 invalid solutions, but avg nodes ~28.7M/trial (avg ~6.7s, total ~201s/30)
-- the soft-min heuristic's weaker pruning power (it can UNDERESTIMATE by
design) needs a larger COST12 to compensate, matching the very first
(slow-base) attempt's own finding, and essentially unchanged by the
move-lock correctness fix (the buggy and fixed versions happen to visit
similar node counts on this scramble set -- the fix matters for
correctness guarantees, not because it was silently causing a big
performance regression). Per the user's own live guidance ("if the current
parameters are too slow, up COST12 to 12"): **30/30 success, 0 invalid
solutions** at a wall-clock roughly 4x faster than the established
`chain1234prime.cpp` baseline (~1900ms/trial average), at a very slightly
longer average solution length -- see the profile log files in this
subfolder for exact numbers from the specific run. This is the best result
of the three phase-1 experiments tried this session (1a/1b split: slower
than baseline at its own untuned guess). Neither `SOFTMIN_SLACK` (4) nor
`COST12=12` have been swept systematically yet -- both are ready for the
same manual `--profile`-driven tuning workflow used throughout this
project.
