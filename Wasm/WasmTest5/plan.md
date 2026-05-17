# WasmTest5 Feature Plan (Detailed)

## Approach: Extend in place

Same folder, same files. The C++ and JS changes are self-contained enough to be additive. No structural rename is needed.

---

## 1. Piece-based puzzle input (JS — `app.js`)

### What changes

`parseDefs` currently treats every token in a cycle as a raw integer index. The new format introduces **required** header lines declaring piece types and a number of orientations:

```
EDGES 2 UF UL UB UR DF DL DB DR FR FL BL BR
CORNERS 3 UFR UBR UBL UFL DFR DFL DBL DBR
```

These headers are required whenever piece names are used — without them there is no way to know how many sticker slots to assign each piece. If any piece name is used in a subsequent line without having been defined in a header line, the user should be notified of an error in the definition.

After headers are declared, cycle notation uses piece names with optional orientation offsets.

### How to expand

Each declared piece name (e.g. `UF`) with `m` orientations gets `m` consecutive sticker integer indices: `UF` occupies slots `base, base+1, ..., base+m-1`.

**Plain piece cycle** `(UF UL UB UR)`: sticker index `s` of `UF` maps to sticker index `s` of `UL`, and so on. Expanded: `(UF_0 UL_0 UB_0 UR_0)(UF_1 UL_1 UB_1 UR_1)`.

**Oriented piece cycle** `(P1+k1 P2+k2 ... Pt+kt)`:

The orientation offsets define which sticker index is the "anchor" in each position. The rule for sticker `s` of piece `Pi`:

> **`Pi_s` maps to `P(i+1 mod t)` at sticker index `(s − ki + k(i+1)) mod m`**

This means the offset difference `k(i+1) − ki` is the shift applied to the sticker index as the piece moves from position `Pi` to position `P(i+1)`. Pieces without an explicit offset default to `+0`.

**Verification with `(UFR+1 DFR DFL-1 UFL+2)` (m=3, offsets k=[1,0,2,2]):**
- `UFR_s → DFR_{(s−1+0) mod 3}` → shift of −1
- `DFR_s → DFL_{(s−0+2) mod 3}` → shift of +2
- `DFL_s → UFL_{(s−2+2) mod 3}` → shift of 0
- `UFL_s → UFR_{(s−2+1) mod 3}` → shift of −1

This produces exactly the three sticker cycles:
- `(UFR_1 DFR_0 DFL_2 UFL_2)`
- `(UFR_2 DFR_1 DFL_0 UFL_0)`
- `(UFR_0 DFR_2 DFL_1 UFL_1)`

Note: for an unoriented cycle (all offsets 0), the formula reduces to `Pi_s → P(i+1)_s` — each sticker index maps straight across, matching the plain piece cycle rule.

### Skew-oriented piece cycles

A cycle is **skew** if its last token names the same piece as its first token. The last token is not an additional position — it is only there to carry an alternate closing offset `kfinal`. The cycle has `t = len − 1` real positions.

> **Detection:** after tokenising a cycle, if `last_piece == first_piece`, strip the last token and record its offset as `kfinal`. Otherwise `kfinal = k1` (standard closure).

The only difference from a normal cycle is the final transition `Pt → P1`:

> **`Pt_s` maps to `P1` at sticker index `(s − kt + kfinal) mod m`**

All other transitions `Pi → P(i+1)` (for i = 1 … t−1) use the same formula as before.

**What this controls:** the net sticker-index shift per lap around the cycle is `kfinal − k1`. Starting from sticker `s` of `P1`, after one full lap through all t positions the sticker index has shifted by `(kfinal − k1) mod m`. The sticker cycle length is therefore `t × m / gcd(m, kfinal − k1)`.

| Example | t | m | kfinal−k1 | Cycle structure |
|---|---|---|---|---|
| `(UF UB+1 UF+1)` | 2 | 2 | 1 | one 4-cycle |
| `(UF UB+1 UF)` | 2 | 2 | 0 | two 2-cycles (same as normal) |
| `(A A+2)` | 1 | 4 | 2 | two 2-cycles (in-place twist) |

The `(A A+2)` case (`t = 1`) is a single-piece self-cycle: the only "transition" is the one from P1 back to itself, using `kfinal = 2`, giving `A_s → A_{(s+2) mod 4}`.

When `kfinal = k1` (or no closing element), the skew formula reduces exactly to the normal cycle formula — no special case is needed.

### Implementation in `parseDefs`

1. Before the main line-parsing loop, scan for `EDGES N name1 name2 ...` and `CORNERS N name1 name2 ...` lines. (Their names could be something other than EDGES or CORNERS). Build a map: `pieceName → {baseIndex: int, numOrientations: int}`. Register all `m` sticker indices for each piece into the existing token map.
2. In the cycle parser, tokenise each cycle. If the last token names the same piece as the first, record its offset as `kfinal` and drop it from the position list. Otherwise set `kfinal = k1`.
3. Apply the transition formula to fill the permutation array: use `kfinal` for the `Pt → P1` transition, `k(i+1)` for all others.
4. Tokens that don't match any declared piece name should give an error message. Move definitions that don't use pieces (e.g. derived moves like `U2: U U`) are unaffected.

C++ sees the same flat sticker permutations as today — no C++ changes for this part.

---

## 2. OrientPerm target group type (JS + C++)

### JS: parsing a new syntax

A target group can now be specified as an "OrientPerm group" using lines like:

```
{UFR UFL UBL UBR}
1: UF UL UB UR
{DFR DFL} {DBR DBL} {DR DF DL DB}
```

Detection rule: if any token in the target group text begins with `{` or a line begins with `N:`, treat the whole block as an OrientPerm specification. Otherwise, use the existing generator-list parser.

Parsing rules:
- `{P1 P2 ...}` — these piece names form one equivalence class; they are interchangeable.
- `N:` prefix on a line — all `{...}` groups on that line have their orientations knocked down to `N`. `1:` means ignore orientation entirely. `2:` on a 4-orientation piece means reduce to 2 orientations.
- Pieces not inside `{...}` are singleton equivalence classes (distinct from all others).
- No piece appears twice. All pieces in one `{...}` must have the same piece type (same number of orientations).

The parsed result is a structure sent to C++ via `worker.postMessage`:

```js
{
  type: 'orientperm',
  classes: [
    { pieces: ['UFR','UFL','UBL','UBR'], orientation_mod: 1 },
    { pieces: ['UF','UL','UB','UR'],     orientation_mod: 1 },
    ...
  ]
}
```

Each entry in `classes` gives the sticker-index base of each piece (looked up from the token map built during puzzle parsing), the number of orientations for that piece type, and the knockdown factor.

### C++: new `OrientPermSpec` struct

`MultiTargetSolver::TargetGroup` gains a `table_kind` field (enum: `GENERAL`, `ORIENTPERM`) and, for the OrientPerm case, an `OrientPermSpec` object.

`OrientPermSpec` stores:
- The equivalence class partition: for each class, which piece indices (not sticker indices — logical piece positions 0..n-1) it contains and their orientation count.
- The knockdown factor per class.
- A precomputed **orientation constraint basis**: which orientations are "free" and which are determined by the others.
- Precomputed **binomial coefficient table** (Pascal's triangle up to `n` for combinatorial numbering).

#### Orientation constraint analysis

For each piece type with `m` orientations, project the solving group generators onto orientation-only space: each generator becomes a vector in `Z_m^n` giving the orientation delta it applies to each piece (accounting for permutation: piece at position `i` ends up at position `g(i)` with orientation delta `delta_i`). The image of these vectors spans the reachable orientation combinations. Using Gaussian elimination over `Z_m`, find a basis for the kernel (the constraint) — this tells us which orientations are determined by the others. Typically for standard puzzles this is just the "sum ≡ 0 mod m" constraint (last orientation forced), but the code handles the general case.

The result: a list of "free" orientation indices (0..n-2 for the standard case) and a function to recover the forced orientations from the free ones.

#### State indexing: `state_to_index(perm)` and `index_to_state(idx)`

Given a full puzzle permutation (on stickers), compute a dense integer index in `[0, N)`:

**Orientation part** (for each piece type with knockdown `d > 1`):  
For each piece in canonical position order, read its orientation from the sticker permutation and reduce mod `d`. Then index the free orientations as a mixed-radix number: `o_0 + d*o_1 + d²*o_2 + ...` (only free positions contribute). The forced orientations are omitted.

**Permutation part** (for pieces with knockdown `d = 1`, or after separating orientation out):  
For each piece type, canonical state is determined only by which equivalence class occupies each position. Number these using the **combinatorial number system**:  
- Represent the assignment as a sequence `L_0, L_1, ..., L_{n-1}` where `L_i ∈ {class index}`, and the count of each class label equals its class size.  
- Lexicographic rank using precomputed binomial/multinomial coefficients.  
- For singleton classes (distinct pieces), this degenerates to a Lehmer code / factoradic.

The full index is: `orientation_index + orientation_count * permutation_index`.

`index_to_state` is not strictly required for solving (only needed for debugging or the Scramble-walk distance recovery trick), so it can be implemented later.

#### Canonicalization (O(n) instead of O(n²))

Given a permutation, canonical form:
1. For each equivalence class of pieces, sort the class members into their canonical (lowest-label) positions.
2. Apply orientation knockdown.

This is O(n) — just reorder pieces within each class in sorted order. Replaces BSGS.canonicalize entirely for this group type.

---

## 3. Compact and incomplete distance tables (C++)

Each `TargetGroup` now has a `TableMode` enum:

```cpp
enum TableMode {
    FULL,            // transition table + distance table (current)
    NO_TRANSITION,   // distance table only, canonicalize on the fly
    INCOMPLETE,      // partial BFS up to max_depth, stored as hash map
};
```

`OrientPermSpec` groups also have a `compact_dist` bool: when true, store the distance table as a packed 2-bit array (the mod-3 trick) instead of `uint8_t[]`.

### Full OrientPerm table

Replace the hash map `unordered_map<Hash128, int>` with a flat `std::vector<uint8_t> distance_table` of size `N` (the theoretical state space size). Phase 1 (enumerate canonical IDs) is replaced with the math indexing: no DFS needed to build the ID mapping, since every index in `[0, N)` corresponds to a valid state. Phase 2 (transition table) is built by iterating over all reachable states.

### Compact (mod-3) distance table

Store `dist[state] mod 3` in a packed array: 4 entries per byte, 2 bits each. Size is `N / 4` bytes.

During IDA*, the DFS traverses the tree one move at a time. The key invariant: at each node, the parent's exact distance is known. Since applying one move changes distance by at most 1, and we know the new state's `dist mod 3` (from the table), we can recover the exact distance:
- `d_new ∈ {d_parent - 1, d_parent, d_parent + 1}`
- `d_new ≡ table[new_state]` (mod 3)
- Exactly one of the three candidates satisfies both — that's `d_new`.

For the root of the DFS (the scrambled state), the exact distance is unknown. We use IDA*'s iterative deepening: the outer loop over `threshold` means we never need a better-than-linear heuristic for the root.

### Incomplete table

BFS from the set of solved states, up to depth `max_depth` (configurable). For general groups: stored as `unordered_map<Hash128, uint8_t>`. For OrientPerm groups: stored as the flat `uint8_t[]` with a sentinel value (e.g., `0xFF`) meaning "not reached within max_depth".

IDA* heuristic for a state not in the incomplete table: if the state is absent, we know its exact distance is `> max_depth`, so `h = max_depth + 1` is a valid (but loose) lower bound. This means the solver can still prune aggressively as long as the remaining budget is ≤ `max_depth`.

No transition table is built for incomplete mode.

---

## 4. General group without transition table (C++)

New `TableMode::NO_TRANSITION`: only the distance table is stored. During IDA*, instead of `state[i] = transition_table[parent_state[i]][mi]`, we maintain the full cube permutation alongside the DFS stack (apply/undo moves explicitly). Canonicalization is recomputed at each node: `hash = hashPerm(canonicalize(cube), base)`, then `distance_table.find(hash)`.

**Optimization**: canonicalization is O(n²) (BSGS) and expensive. Only invoke it when it can affect pruning:
- If `remaining_depth >= 2`: always canonicalize (heuristic can save work).
- If `remaining_depth == 1`: skip (we'll try all moves directly; there are at most `|moves|` of them).
- If `remaining_depth == 0`: this node is a solution — we've already checked it.

This avoids the O(|moves| × n²) cost at the leaves of the DFS tree where canonicalization provides no benefit.

---

## 5. Multiple solved positions (JS + C++)

### JS: Scramble field parsing

The existing "Starting Algorithm" field is renamed "Scramble" and gains a richer syntax. Parsing proceeds left to right through a sequence of tokens / bracket groups:

**Plain token** (`R`, `U`, `F'`, etc.): look up in the move map and compose onto the current state.

**Square brackets** `[A, B, ...]`: the solver branches. Each branch is evaluated independently from the current prefix state. Results in multiple solved positions. Implemented recursively: parse each comma-separated sub-sequence, apply it to the current permutation accumulator, collect permutation results.

**Angle brackets** `<g1, g2, ...>`: treat `g1, g2, ...` as generators. BFS/DFS from the current permutation using those generators (and their inverses) to collect all reachable permutations. Each one is a solved position.

Full example: `R U R' U' [R', F] <B, U>` expands to:
1. Apply `R U R' U'` → state S
2. Branch: S + `R'` → S1; S + `F` → S2
3. For each branch, expand `<B, U>`: BFS from S1 (resp. S2) using B and U, collecting all reachable states.

The result is a list of `Perm[]` (solved positions) sent to the worker.

### C++: multi-source BFS

`TargetGroup` gains a `vector<Perm> solved_states` (default: `{identity}`).

Phases 1 (enumerate canonical IDs) and 2 (build transition table) are unchanged — they don't depend on which states are "solved".

Phase 3 (BFS for distances) becomes multi-source: seed the queue with all solved state IDs simultaneously, each at distance 0. The rest of the BFS is identical.

For incomplete tables, BFS also starts from all solved states simultaneously.

---

## 6. IDA* changes for new table types

The current `idaDfs` is a tight loop indexing `transition_table[state[i]][mi]`. We need to dispatch on `table_kind` and `TableMode` per group. Two approaches:

**Option A (simpler)**: duplicate the IDA* inner loop for each mode combination and template/dispatch at the top of `solve()`. Avoids branch prediction misses inside the tight loop.

**Option B (cleaner for now)**: a virtual-dispatch or function-pointer approach on `TargetGroup`, where each group provides a `lookup(cube) → distance` function. The DFS calls this for each group at each node.

Plan: start with Option B for clarity, profile later.

The DFS also needs to carry the current cube permutation when any group uses `NO_TRANSITION` or `ORIENTPERM` (since OrientPerm lookup computes the index from the cube directly rather than chaining through a transition table). This means the DFS stack always maintains the cube permutation (apply move on descent, undo on ascent) when at least one group needs it.

For groups using `FULL` mode + transition table, the current `state[i] → transition_table[state[i]][mi]` path is preserved as an O(1) fast path.

---

## 7. UI changes (`index.html`)

New fields added:

- **Target Groups**: the existing textarea is kept as-is. Each line is either a generator list (current behavior) or an OrientPerm block (new). The parser auto-detects which syntax is being used per line/block.
- **Table mode selector**: a small dropdown or radio group per target group — "Full", "No transition table", "Incomplete (depth:)". Rendered dynamically as the user edits the target groups textarea (can start as a global setting below the textarea).
- **Incomplete depth**: a number input (only relevant for incomplete mode).
- **Scramble** (renamed from "Starting Algorithm"): same `<input>` element, new label.

---

## 8. Worker and message protocol (`worker.js`)

The worker receives new fields in the `compute` message:

```js
{
  type: 'compute',
  k,
  targetGroups: [          // one entry per target group
    {
      kind: 'generator',   // existing
      perms: [...],
      tableMode: 'full' | 'no_transition' | 'incomplete',
      maxDepth: 5,         // only for incomplete
      solvedPerms: [...],  // list of solved permutations (default: [identity])
    },
    {
      kind: 'orientperm',  // new
      classes: [...],      // parsed OrientPerm class structure
      tableMode: '...',
      maxDepth: 5,
      solvedPerms: [...],
    }
  ],
  solvingPerms, solvingAlgos,
  startingPerms: [...],    // list of starting permutations (may be 1 for single scramble)
}
```

The worker calls the corresponding new C++ API per group type. The solve call is adapted to handle a list of starting permutations (solve from each and union the results, or find solutions that work from any given start).

---

## 9. New C++ API surface

New methods on `MultiTargetSolver`:

```cpp
// Per target group, before buildTargetGroup:
void setGroupTableMode(TableMode mode, int max_depth);
void setGroupKind(GroupKind kind);
void addOrientPermClass(vector<int> piece_sticker_bases, int num_orientations, int knockdown);
void addSolvedState(vector<int> perm);  // call multiple times for multi-source BFS

// After buildTables, for solve:
vector<int> solve(vector<int> startPerm, int min_moves, int max_moves, int slack);
// (unchanged signature, but now handles all modes internally)
```

The existing `buildTargetGroup`, `buildTables`, and `solve` methods are extended internally; their signatures remain compatible with the current `worker.js` where no new features are used.

---

## 10. Build

`schreier_sims.cpp` is the only C++ file. Compile command is unchanged (just add no new files). The new code is conditionally compiled under `#ifdef __EMSCRIPTEN__` as before.

The new `EMSCRIPTEN_BINDINGS` block adds the new methods. The existing bindings remain for backward compatibility.

---

## Implementation order (suggested)

1. Piece-based puzzle expansion in JS (pure JS, no C++ needed, low risk)
2. Multi-source BFS in C++ (small change, high value, needed for #7)
3. OrientPerm state indexing math in C++ (`OrientPermSpec`, `state_to_index`)
4. OrientPerm target group: full table mode (flat array distance + transition)
5. Compact (mod-3) distance table
6. Incomplete table mode for both general and OrientPerm groups
7. No-transition IDA* for general groups
8. OrientPerm JS parsing + worker wiring
9. Scramble field: `[...]` and `<...>` syntax in JS
10. UI: table mode controls, rename field
