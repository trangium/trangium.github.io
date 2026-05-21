# WasmTest5 Feature Plan (Detailed)

## Approach: Extend in place

Same folder, same files. The C++ and JS changes are self-contained enough to be additive.

---

## 1. Piece-based puzzle input — **IMPLEMENTED** (JS — `app.js`)

### What was built

`parseDefs` now requires piece-type header lines. Puzzle input without headers gives an error immediately. Header format (any ALL-CAPS keyword):

```
EDGES 2 UF UL UB UR DF DL DB DR FR FL BL BR
CORNERS 3 UFR UBR UBL UFL DFR DFL DBL DBR
```

The regex `/^([A-Z][A-Z0-9_-]*)\s+(\d+)\s+(.+)$/` matches them. Any type name is accepted (EDGES, CORNERS, X-CENTERS, PLUS-CENTERS, etc.). Each piece declaration stores `{base: int, m: int, type: string}` in `pieceInfo`.

Sticker tokens are `PieceName_0`, `PieceName_1`, … and are added to `tokenMap` for use by `composeAlgo` (inline cycle notation in generators/algorithms). These sticker names are the only valid identifiers for target generators that use explicit cycle notation.

Two new standalone functions handle piece-mode cycles:
- `parsePieceCycle(content, perm, pieceInfo)` — one cycle (text between parentheses)
- `parsePieceCycles(str, k, pieceInfo)` — full move definition string

**Expansion formula** for `(P1+k1 P2+k2 ... Pt+kt)`:
> `Pi_s  →  P(i+1 mod t)` at sticker `(s − ki + k(i+1)) mod m`

For the last transition, `k(i+1)` uses `kfinal` (from the skew-closing element if present, else `k1`).

**Skew cycle detection:** if the last token names the same piece as the first, it is a skew closer — its offset becomes `kfinal` and it is removed from the position list. Implements in-place twists (`(A A+2)`) and merged-cycle moves (`(UF UB+1 UF+1)`).

**Cycle type validation:** all pieces in a cycle must share the same `type` string (e.g. all EDGES). An error is thrown if EDGES and CORNERS are mixed, even if they happen to have the same `m`.

The old sticker-mode fallback is gone. `parseCycles` is retained only for `composeAlgo`.

Default puzzle updated to 3×3 piece notation (48 sticker slots: 12 edges × 2 + 8 corners × 3).

---

## 2. OrientPerm target group type (JS + C++)

### 2a. JS: target group textarea — new delimiter and detection — **IMPLEMENTED**

**Group delimiter change.** The textarea is split by blank lines into *blocks*. Within a block:
- If any line contains `{` OR matches `/^\s*\d+:/`, the entire block is one **OrientPerm group** (all its lines together define one group).
- Otherwise each line in the block is a separate **generator group** (existing behavior — one line, comma-separated generators).

This means the defaults for generator groups must be separated by blank lines. Update `$('target-gens').value` accordingly.

### 2b. JS: parsing an OrientPerm block — **IMPLEMENTED**

Each line within an OrientPerm block has the form:
```
[d:] item item item ...
```
where `d` is an integer knockdown factor (default: full orientations = `m` for each piece type encountered) and each item is either:
- `{P1 P2 ...}` — an equivalence class of interchangeable pieces
- `Pname` — a singleton equivalence class (piece is distinct, its position matters)

All pieces on a `d:`-prefixed line get knockdown factor `d`. Pieces on a line with no prefix keep their full `m`.

After parsing the explicit lines, iterate over all pieces in `pieceInfo`. Any piece not yet assigned to a class is added as a singleton class with full orientation (`orientation_mod = m`).

The parsed result for the block is a flat ordered list of classes:
```js
// Sent in the worker message as targetGroups[i].classes
[
  { bases: [24, 27, 30, 33], m: 3, typeName: 'CORNERS', orientation_mod: 1 },
  { bases: [0, 2, 4, 6],     m: 2, typeName: 'EDGES',   orientation_mod: 1 },
  { bases: [36, 39],         m: 3, typeName: 'CORNERS', orientation_mod: 3 },
  // ...
]
```

`bases` is the list of `pieceInfo.get(name).base` for each piece in the class. `m` and `typeName` come from the first piece's `pieceInfo` entry; validate that all pieces in one `{...}` share the same `typeName`.

### 2c. Worker message format — **IMPLEMENTED**

The `compute` message gains a new field replacing `targetPermsArray`:

```js
targetGroups: [
  {
    kind: 'generator',
    perms: Perm[],   // same as current targetPermsArray[i]
  },
  {
    kind: 'orientperm',
    classes: [{ bases: int[], m: int, typeName: string, orientation_mod: int }, ...],
  },
]
```

The worker builds `targetPermsArray` from generator groups (for backward compatibility with the C++ `addTargetGenerator` path) and calls new C++ API for orientperm groups.

### 2d. C++ structs — **IMPLEMENTED** (scaffolding; `build()` is a stub)

Add to `MultiTargetSolver::TargetGroup`:

```cpp
enum GroupKind { GENERAL, ORIENTPERM };
GroupKind kind = GENERAL;
OrientPermSpec op_spec;   // only valid when kind == ORIENTPERM
```

A **compact piece permutation** (`PiecePerm`) for a single piece type stores one integer per piece rather than one per sticker:

```cpp
// PiecePerm[pos] = base + m * source_piece + orientation_twist
// This is exactly full_perm[base + m * pos] — sticker 0 of each position.
// The remaining stickers are recoverable: full_perm[base + m*pos + s] = base + m*dest + (s + twist) % m
using PiecePerm = std::vector<int>;  // length n_t
```

Composition and inversion are O(pieces), not O(stickers):

```cpp
// Compose A then B for one piece type (base B, m stickers per piece):
PiecePerm compose_piece(const PiecePerm& A, const PiecePerm& B, int base, int m) {
    PiecePerm R(A.size());
    for (int src = 0; src < (int)A.size(); src++) {
        int a = A[src] - base, mid = a / m, tA = a % m;
        int b = B[mid] - base;
        R[src] = base + (b / m) * m + (tA + b % m) % m;  // combined dest + twist
    }
    return R;
}

// Invert: piece at src_pos goes to dest_pos with twist t  →  dest_pos now holds src_pos with -t
PiecePerm invert_piece(const PiecePerm& A, int base, int m) {
    PiecePerm R(A.size());
    for (int src = 0; src < (int)A.size(); src++) {
        int a = A[src] - base;
        R[a / m] = base + m * src + (m - a % m) % m;
    }
    return R;
}
```

**Where PiecePerm is used:** in `buildTables()` DFS (apply/undo moves in O(pieces) instead of O(stickers)), and inside `state_to_index` (scatter replaces O(n) inversion — see §2e). The IDA\* DFS does **not** use PiecePerm: FULL OrientPerm groups go through the pre-built transition table (no perm maintained at all), and NO_TRANSITION GENERAL groups need the full Perm for Zobrist hashing anyway.

New `OrientPermSpec` struct (all fields computed in `build()`):

```cpp
struct OrientPermSpec {
    int n;          // total sticker domain size

    struct PieceTypeMeta {
        int base;              // sticker index of piece 0's sticker 0
        int m;                 // stickers per piece
        int count;             // number of pieces of this type (including fixed pieces)
        int effective_count;   // count minus fixed pieces; used for multinomial and ranking
        long long initial_multinomial; // effective_count! / (class_sizes[0]! * class_sizes[1]! * ...)
                               // = perm_space contribution BEFORE parity halving; seed for incremental ratio
    };
    std::vector<PieceTypeMeta> types;  // one entry per unique piece type

    struct Class {
        int type_idx;              // index into types[]
        int orientation_mod;       // knockdown d (1 = don't track, m = full)
        std::vector<int> pieces;   // sorted piece indices within the type
    };
    std::vector<Class> classes;   // in order of first appearance

    // piece_class[type_idx][piece_within_type] = class index
    std::vector<std::vector<int>> piece_class;

    // orient_step[t] = m_t / orbit_size_t
    //   = gcd of all achievable sum-deltas for type t (in the stabilizer of all previous types),
    //     taking m_t into account.
    // orient_step[t] == 1  → no constraint; last tracked piece is fully free.
    // orient_step[t] == m_t → sum is always 0; last tracked piece is completely forced (contributes
    //                         only 1 value to the index = effectively not tracked).
    // Computed by build() via joint BSGS on virtual permutations. See §2f.
    std::vector<int> orient_step;

    // is_parity_forced[t]: true iff the permutation parity of type t is forced,
    // given that all previous types' parities are already determined.
    // Always false for restricted types (any class with > 1 piece after fixed-piece filtering).
    // Computed in build() via joint BSGS on parity deltas. See §2l.
    std::vector<bool> is_parity_forced;

    long long perm_space;    // product of perm_space contributions across all types
                             // contribution = initial_multinomial / 2 for parity-forced types,
                             //               initial_multinomial otherwise
    long long orient_space;  // see §2e Step 3 for formula
    long long total_states;  // perm_space * orient_space

    void build(const std::vector<Class>& classes_in,
               const std::vector<PieceTypeMeta>& types_in,
               int n_in,
               const std::vector<Perm>& generators);

    // External call: extracts compact representation internally from full sticker perm.
    long long state_to_index(const Perm& S) const;
    // Internal DFS call: accepts pre-maintained compact perms (one per type), O(pieces) total.
    long long state_to_index_compact(const std::vector<PiecePerm>& compact) const;
};
```

### 2e. `state_to_index` algorithm

Given state permutation S where `S[i] = j` means solved sticker `i` is now at position `j`.

**Step 1: Extract piece and orientation at each position.**

Instead of computing `R = inv(S)` over all `n` stickers (O(n)), scatter over pieces only (O(n_t) per type). Skip fixed positions (see §2k) — their piece and orientation are always trivial and are not tracked.

```
for each piece type t (base B, m stickers, n_t pieces):
    for src_pos in 0..n_t-1:
        if src_pos is fixed for type t: continue
        val   = S[B + m * src_pos] - B   // = m * dest_pos + twist
        dest  = val / m
        twist = val % m
        piece_at_p[t][dest]  = src_pos
        orient_at_p[t][dest] = (m - twist) % m   // orientation at destination
```

When called as `state_to_index_compact(compact)`, the same scatter loop reads `compact[t][src_pos]` directly instead of `S[B + m * src_pos]`.

**Step 2: Compute permutation index.**

For each piece type `t`:
  Build label sequence `L` over the `effective_count_t` non-fixed positions (in position order),
  where `L[p] = class_of(piece_at_p[t][p])`.

  Compute lex rank using the **incremental ratio method** (no lookup table needed):

  ```
  rank = 0
  remaining_class_counts = copy of class sizes (after fixed-piece filtering, §2k)
  n_eff = types[t].effective_count
  current = types[t].initial_multinomial   // = n_eff! / (class_sizes[0]! * class_sizes[1]! * ...)
  for p = 0 to n_eff - 1:
      label   = L[p]
      n_rem   = n_eff - p
      for each class c' with index < label and remaining_class_counts[c'] > 0:
          rank += current * remaining_class_counts[c'] / n_rem
      current = current * remaining_class_counts[label] / n_rem
      remaining_class_counts[label]--
  ```

  Each `rank +=` and the `current` update are O(1) integer operations (the division is always exact because `current * count[c'] / n_rem` equals `M(n_rem-1, counts-with-c'-decremented)`, a valid multinomial coefficient). No precomputed table is required.

  **Parity halving.** For types where `is_parity_forced[t]` is true (see §2l), the reachable permutations form exactly half the lex-rank space. Since consecutive lex ranks differ by a swap of the last two elements — and every such swap changes parity — even-ranked permutations are all one parity and odd-ranked permutations are all the other. Therefore `floor(rank / 2)` bijects the reachable permutations onto `{0, ..., n_eff!/2 - 1}`. Apply it:
  ```
  index_t = is_parity_forced[t] ? rank / 2 : rank
  ```
  The perm_space contribution of a parity-forced type is `initial_multinomial / 2`; all other types contribute `initial_multinomial`.

  Combine across types: `perm_idx = index_type0 + perm_size_type0 * index_type1 + ...`

**Step 3: Compute orientation index.**

For each piece type `t`:

  Let `d_p = classes[piece_class[t][p]].orientation_mod` for each piece position `p`.

  Let `carrier` = the last piece position `p` in type `t` with `d_p > 1`. If no such position exists, skip this type (no orientation tracking).

  Let `orbit_size_t = types[t].m / orient_step[t]`.

  Iterate positions `p = 0..n_t-1` in order, accumulating into a mixed-radix index:
  ```
  stride = 1
  orient_idx_t = 0
  for p = 0..n_t-1:
      if d_p == 1: skip
      elif p == carrier:
          if orbit_size_t > 1:
              orient_idx_t += floor(orient_at_p[t][p] / orient_step[t]) * stride
              stride *= orbit_size_t
          // else orbit_size_t == 1: last piece is forced, contributes nothing
      else:
          orient_idx_t += orient_at_p[t][p] * stride
          stride *= d_p
  ```

  Why `floor(o / orient_step[t])` for the carrier: the carrier's valid orientation values are a coset
  of the orbit, spaced exactly `orient_step[t]` apart. Dividing by `orient_step[t]` maps them
  bijectively to `{0, 1, ..., orbit_size_t - 1}` regardless of the coset offset. This works as long
  as `orient_step[t]` divides `d_carrier` (the carrier's knockdown). See §2f for the known limitation.

  Combine across types: `orient_idx = orient_idx_t0 + stride_t0 * orient_idx_t1 + ...`

  **orient_space** = product of `stride` values after processing each type.

**Final:** `index = perm_idx + perm_space * orient_idx`

### 2f. Orientation constraint analysis in `build()`

The goal is to compute `orient_step[t]` for each piece type `t`. This captures how constrained the
orientation sum is for each type, **given that all previous types' sums are already fixed**.

**Why per-type gcd is wrong (critical pitfall):**

A naive approach would compute `gcd(all Δ_t(G) for all generators G, m_t)` for each type
independently. This is incorrect. Example: generators U:[0,0,0], R:[0,2,2], F:[1,0,3] acting on
types EDGES (m=2), CORNERS (m=3), BLOBS (m=6). Blob deltas alone: {0, 2, 3}. gcd(0,2,3,6) = 1,
suggesting orbit_size=6 (no constraint). But this is wrong.

To fix EDGES, F must appear in even counts → each such pair contributes 2×3=6≡0 (mod 6) to blobs.
To fix CORNERS, R must appear in multiples of 3 → each such triple contributes 3×2=6≡0 (mod 6)
to blobs. So in the stabilizer of (EDGES, CORNERS), every generator sequence leaves blobs unchanged.
Blob orbit in that stabilizer = {0}, orbit_size = 1. The blob sum is completely fixed — even though
gcd of blob deltas alone is 1.

The cross-type coupling through shared generators means the analysis must be **joint**.

**Algorithm:**

**Step 1 — Compute sum-delta per type per generator.**

For each solving generator G and each piece type t, extract compact PiecePerm and compute:
```
Δ_t(G) = (sum over all src positions p: (compact[t][p] - base_t) % m_t) % m_t
```
This is the total orientation sum change that G applies to all type-t pieces.

**Step 2 — Add knockdown deltas.**

For each class `c` in the target group:
- Let `d = classes[c].orientation_mod`, `t = classes[c].type_idx`.
- Add `d` to the delta set for type `t`.

Interpretation: having knockdown `d` on a class means changing any of those pieces' orientations by
`d` is "free" for indexing. This is equivalent to adding a virtual move with sum-delta `d` for type
`t` (and 0 for all other types).

If multiple classes of the same type contribute knockdowns `d_1, d_2, ...`, add each as a separate
delta. (Equivalently, their gcd is the only new information, but adding each individually is fine.)

**Step 3 — Build virtual permutation group.**

Allocate `total_virtual = sum(m_t)` virtual pieces, with type `t` occupying indices
`[offset_t, offset_t + m_t)` where `offset_t = sum(m_s for s < t)`.

A sum-delta of `d` for type `t` = the cyclic-shift-by-d permutation on type `t`'s virtual pieces:
```
virtual_perm[offset_t + i] = offset_t + (i + d) % m_t  for i = 0..m_t-1
virtual_perm[offset_s + i] = offset_s + i               for all other types s
```

Build one virtual permutation per solving generator (encoding all types' deltas simultaneously) and
one virtual permutation per distinct knockdown delta per type.

**Step 4 — Run joint BSGS on the virtual permutation group.**

Run `randomized_schreier_sims` on the joint virtual permutation group (using the existing machinery).
No base forcing is required. Any base ordering the BSGS naturally picks is valid: different orderings
partition the |G| degrees of freedom across types differently, but orient_space = (product of
non-carrier d_p) × |G| is invariant, and state_to_index remains injective on the reachable set.
This holds because: (a) the product of orbit sizes always equals |G|; (b) the "colliding" states in
any ordering are unreachable by the same group structure the BSGS is computing; (c) the
`floor(o / orient_step)` formula works for any ordering since the virtual group is cyclic-shift-based,
making valid carrier values always a coset of ⟨orient_step⟩.

**Step 5 — Read orbit sizes.**

After the BSGS completes, match each chain level to its type by inspecting the base point:
```
for each level i in bsgs.chain:
    bp = bsgs.chain[i].base_point
    t = type index such that offset_t <= bp < offset_t + m_t
    orbit_size[t] = bsgs.chain[i].transversal.size()
    orient_step[t] = m_t / orbit_size[t]
```
Types whose representative never appears as a base point (all deltas were 0, never moved)
get orbit_size = 1, orient_step = m_t (sum is fixed at 0, last piece is forced).

**Step 6 — Compute orient_space.**

```
orient_space = 1
for each type t:
    carrier_d = d_p for the carrier piece (last piece with d_p > 1); 0 if no such piece
    if carrier_d > 1:
        n_tracked_non_carrier = count of pieces with d_p > 1, excluding carrier
        orient_space *= (product of d_p for non-carrier tracked pieces) * orbit_size_t
```

### 2g. Precomputing per-type initial multinomials

For each piece type `t` in `build()`, compute once using the filtered class sizes (after fixed-piece removal, §2k):

```
effective_count_t = sum of class sizes of type t (= n_t - num_fixed_t)
initial_multinomial[t] = effective_count_t! / (class_sizes[0]! * class_sizes[1]! * ...)
```

using a single pass: compute `effective_count_t!` then divide by each `class_size!`. Store in `PieceTypeMeta::initial_multinomial`. This is the seed for the incremental ratio method and equals the per-type permutation count before parity halving.

`perm_space` is the product of per-type contributions:
```
perm_space = product over t of:
    initial_multinomial[t] / 2   if is_parity_forced[t]
    initial_multinomial[t]       otherwise
```

The ratio identity used during `state_to_index`:
```
M(n_rem - 1, counts_with_c'_decremented) = current * counts[c'] / n_rem
```
holds because `(n_rem - 1)! / (... (counts[c']-1)! ...) = n_rem! / (... counts[c']! ...) * counts[c'] / n_rem`. The division is always exact.

### 2h. Table building for OrientPerm groups (in `buildTables()`)

When `kind == ORIENTPERM`:
- **No phase 1 (hash map ID enumeration)**. Instead, allocate `distance_table` as `std::vector<uint8_t>(op_spec.total_states, 0xFF)` and `transition_table` as `std::vector<std::vector<int>>(op_spec.total_states, std::vector<int>(nMoves, -1))` — indexed directly by `state_to_index`.
- **Phase 1-like DFS** still runs to find all reachable states, but instead of building a hash map, it marks entries in the flat arrays. The DFS maintains a stack of `std::vector<PiecePerm>` (one `PiecePerm` per type) rather than a full `Perm`. Applying move `i` composes each type's compact perm with `compact_move[i][t]` using `compose_piece` — O(pieces) per move instead of O(stickers). The DFS calls `state_to_index_compact(compact)` to compute the index. Undo is O(pieces) (restore previous compact perms from the stack).
- Pre-extract compact representations of all solving moves once before the DFS: `compact_move[i][t] = extract(solving_perm[i], types[t].base, types[t].m)`.
- **Phase 3 (BFS for distances)** is the same BFS over `distance_table`, seeded from `state_to_index_compact(identity_compact)`.

For `FULL` mode: build both transition and distance tables.
For `INCOMPLETE` mode: BFS only (up to `max_depth`); skip the DFS; `distance_table` remains `0xFF` for all unreached states.

### 2i. IDA* lookup for OrientPerm groups

In `idaDfs`, the current state tracking is `std::vector<int> state` (one integer per group). This still works for OrientPerm groups: `state[i] = op_spec.state_to_index(cube)`. The lookup is `distance_table[state[i]]`, which is O(1) and a direct array access.

The transition table `transition_table[state[i]][mi]` gives the next state index directly. This is the same as for GENERAL groups, so the IDA* inner loop is unchanged. The difference is in how `state[i]` is initialized (using `state_to_index` instead of `hashPerm`) and how `state[i]` handles the `0xFF` sentinel (unreachable state → pretend distance 0 to avoid false pruning).

Since the cube permutation is not needed for the FULL OrientPerm path (we go through the transition table), the DFS does not need to maintain the full cube when all groups are FULL-mode.

### 2k. Fixed piece detection (in `build()`)

Before orientation analysis (§2f) and parity analysis (§2l), determine which piece positions never
move under any solving generator. Fixed positions do not contribute to permutation ranking,
orientation tracking, or parity analysis.

**Definition.** Position `p` in type `t` is **fixed** if for every solving generator `G`:
```
compact_G[t][p] == types[t].base + types[t].m * p
```
(the piece at position `p` remains at position `p` with zero twist after `G`).

**Algorithm:**
1. For each type `t` and position `p = 0..n_t-1`, check all compact generator perms. A position that fails the test for any one generator is not fixed.
2. For each class `c`, remove any fixed positions from `classes[c].pieces`. If the class becomes empty, drop it entirely.
3. Set `types[t].effective_count` = number of non-fixed positions in type `t` (= sum of remaining class sizes for classes of type `t`).

Fixed positions are skipped in `state_to_index` Steps 1, 2, and 3.

**Example.** On a 3×3×3 with solving subgroup ⟨R, U⟩, the bottom-left corners DFL and DBL never
appear in any R or U move. They are fixed. A class `{DFL DFR DBR}` becomes `{DFR DBR}`, and the
effective corner count drops from 8 to 6 (giving 6! permutations instead of 8!).

### 2l. Parity constraint analysis (in `build()`)

Runs after §2k. For unrestricted piece types, checks whether the permutation parity of each type
is forced given that all previous types' parities are already fixed. Uses the same joint virtual
BSGS approach as orientation constraint analysis (§2f).

**Restricted vs. unrestricted types.** Type `t` is **restricted** if any class of type `t` has
`|pieces| > 1` after fixed-piece filtering. Restricted types always have
`is_parity_forced[t] = false` (we make no parity reduction for them).

**Parity delta.** For each solving generator `G` and each type `t`:
- If type `t` is restricted: `parity_delta[t] = 0`.
- Otherwise: extract the position permutation of non-fixed pieces induced by `G`:
  ```
  pos_perm[i] = (compact_G[t][non_fixed_pos[i]] - types[t].base) / types[t].m
  ```
  where `non_fixed_pos[0..effective_count-1]` lists non-fixed positions in order, and
  `pos_perm[i]` is the destination piece index (re-indexed to 0..effective_count-1 by skipping
  fixed positions). `parity_delta[t] = parity of pos_perm` (0 = even, 1 = odd, computed from
  cycle structure or inversion count).

**Virtual parity permutations.** Build virtual permutations over domain size `2 * nTypes` (two
virtual pieces per type, representing Z/2Z):
```
offset_t = 2 * t
for each generator G with deltas [delta_0, ..., delta_{nTypes-1}]:
    virtual_perm[offset_t + i] = offset_t + (i + delta_t) % 2   for i in {0, 1}, all t
```
One virtual permutation per solving generator. No base-forcing technique needed, see §2f.

**Joint BSGS and orbit sizes.** Run `randomized_schreier_sims` on these virtual permutations. No
base forcing is required (same argument as §2f Step 4: any ordering is valid). After the BSGS
completes, match each level to its type by base point (`offset_t = 2*t`, so `t = base_point / 2`):
```
for each level i in bsgs.chain:
    t = bsgs.chain[i].base_point / 2
    orbit_size_t = bsgs.chain[i].transversal.size()   // 1 or 2
    is_parity_forced[t] = (orbit_size_t == 1) && !restricted[t]
```
Types with no level in the chain (all parity deltas were 0) get `orbit_size_t = 1`.

**Example.** Unrestricted 3×3×3 with solving moves R, R2, U, U2:
```
R:  corners=odd, edges=odd   → parity delta [1, 1]
R2: corners=even, edges=even → parity delta [0, 0]
U:  corners=odd, edges=odd   → parity delta [1, 1]
U2: corners=even, edges=even → parity delta [0, 0]
```
Integer span of {[1,1], [0,0]} over Z/2Z: corner parity is free (orbit size 2), but given
corner parity is fixed, edge parity is also fixed (orbit size 1 in the stabilizer).
`is_parity_forced[corners] = false`, `is_parity_forced[edges] = true`.

**Effect on indexing.** For types where `is_parity_forced[t]` is true: apply `floor(rank / 2)` in
Step 2 of §2e, and use `initial_multinomial / 2` as the perm_space contribution (see §2g).

### 2j. New C++ API — **IMPLEMENTED**

```cpp
// On MultiTargetSolver:
void beginOrientPermGroup();
// Call once per equivalence class:
void addOrientPermClass(std::vector<int> sticker_bases, int m, int orientation_mod);
// After all classes are added, finalise (runs orientation constraint analysis):
void buildOrientPermGroup();  // uses solving_generators_ for orientation analysis
```

`buildOrientPermGroup` populates `groups_.back().op_spec` by calling `OrientPermSpec::build(...)`.

### Commit Plan:

#### Commit 1 — JS: OrientPerm parsing + worker wiring (2a, 2b, 2c) — **DONE**
Pure JS, self-contained. Parses the OrientPerm textarea syntax, detects it via the `{` / `d:` heuristic, and emits the structured classes array in the worker message.

#### Commit 2 — C++ scaffolding: structs + API bindings (2d, 2j) — **DONE**
Defines PiecePerm, OrientPermSpec (all fields, `build()` is a stub), compose_piece, invert_piece, extract_piece, and the EMSCRIPTEN_BINDINGS for beginOrientPermGroup / addOrientPermClass / buildOrientPermGroup.

#### Commit 3a — Fixed piece detection (2k) — **DONE**
Inside `build()`, extract compact PiecePerms for all solving generators, then run §2k:
- For each type `t` and position `p`, check all generators; mark `p` fixed if every generator leaves it in place with zero twist.
- Filter each class's `pieces` list; drop empty classes.
- Set `types[t].effective_count`.

No BSGS work yet; purely array inspection. Prerequisite for 3b and 3c.

#### Commit 3b — Orientation constraint analysis (2f, orient_space from 2g)  — **DONE**
Inside `build()`, after §2k:
- Compute orientation sum-delta per type per generator (§2f Step 1).
- Add knockdown deltas from classes (§2f Step 2).
- Construct virtual cyclic-shift permutations and run joint BSGS with forced base (§2f Steps 3–4).
- Store `orient_step[t]` from orbit sizes (§2f Step 5).
- Compute and store `orient_space` (§2f Step 6).

**Do NOT** compute per-type gcd of deltas in isolation — that misses cross-type dependencies through
shared generators (see §2f pitfall section for the canonical counter-example).

#### Commit 3c — Parity constraint analysis + multinomials (2l, 2g)  — **DONE**
Inside `build()`, after §2k:
- For each unrestricted type, compute parity delta per generator (§2l).
- Construct virtual Z/2Z permutations and run joint BSGS with forced base (§2l).
- Store `is_parity_forced[t]` from orbit sizes (§2l).
- Compute `initial_multinomial` per type from `effective_count` and filtered class sizes (§2g).
- Compute and store `perm_space` (using `initial_multinomial / 2` for parity-forced types) and `total_states = perm_space * orient_space` (§2g).

**Do NOT** compute parity independence per type in isolation — same cross-type coupling pitfall as orientation.

#### Commit 4 — state_to_index with PiecePerm scatter + incremental ratio (2e) — **DONE**
Both overloads. Step 1: scatter to get piece_at_p and orient_at_p. Step 2: incremental ratio rank loop (current * count[c'] / n_rem). Step 3: mixed-radix orientation index using orient_step and carrier position.

#### Commit 5 — buildTables() DFS/BFS + IDA* integration (2h, 2i) — **DONE**
Flat-array allocation, DFS with compact perm stack, BFS seeding from identity, and wiring state[i] initialization and lookup into idaDfs. First commit where end-to-end solving works for OrientPerm groups.

---

## 3. Compact and incomplete distance tables (C++)

Each `TargetGroup` has a `TableMode` enum:

```cpp
enum TableMode { FULL, NO_TRANSITION, INCOMPLETE };
```

All target groups (whether orientPerm or general) will now store 2 bits per state (dist mod 3) for its instance table in a packed array instead of `int[]`.

### Full OrientPerm Groups

`distance_table` is a flat `std::vector<int>` of size `total_states`. Direct array access replaces hash map lookup. This is the current implementation.

### No-Transition Groups

See section 4. Distances for all states still stored, but not transition tables.

### Incomplete table

BFS from solved states up to `max_depth`. For GENERAL and ORIENTPERM groups: `unordered_map<Hash128, uint8_t>`. In IDA*: absent state → `h = max_depth + 1` (valid lower bound since distance > max_depth).

### Compact (mod-3) distance table

Store `dist[state] mod 3` packed 4 entries per byte (2 bits each). During IDA*: at each node, the parent's exact distance is known. Since one move changes distance by at most 1, and we know `d_new mod 3`, exactly one of `{d_parent-1, d_parent, d_parent+1}` matches — that's `d_new`. To find the root's exact distance, search greedily from the root until you get to the identity coset. Pseudocode:

Initialize: state = root
Initialize: distance = 0
while (state != identity coset) {
    d1 = dist_mod_3(state)
    for (move in moves) {
        d2 = dist_mod_3(state.apply(move))
        if ((d1-d2)%3 == 1) {
            state = state.apply(move)
            distance = distance + 1
            break
        }
    }
}
return distance

This is used for all groups.

The BFS for distances (mod 3) will also have to be changed to actually have the extremely small memory footprint we're looking for, once we implement the general group without transition tables.

---

## 4. General group without transition table (C++)

`TableMode::NO_TRANSITION`: skip phase 2. During IDA*, maintain the full cube permutation on the DFS stack (apply/undo). At each node, compute `canonicalize(cube)` + hash + distance lookup. Skip canonicalization when `remaining_depth == 1` (no pruning benefit).

---

## 5. Multiple solved positions (JS + C++)

### JS: Scramble field parsing

Parse left-to-right:
- Plain token: compose onto current state.
- `[A, B, ...]`: evaluate each branch independently from current prefix, collect permutations.
- `<g1, g2, ...>`: BFS from current state using generators and inverses, collect all reachable states.

Result: `startingPerms: Perm[]` in the worker message.

### C++: multi-source BFS

`TargetGroup` gains `std::vector<Perm> solved_states`. Phase 3 BFS seeds the queue with all solved state indices at distance 0. Phases 1 and 2 are unchanged.

---

## 6. IDA* changes for new table types

The DFS carries `state[i]` per group. For FULL-mode groups (both GENERAL and ORIENTPERM), `state[i]` chains through the transition table — unchanged. For NO_TRANSITION groups, the DFS also carries the cube permutation and recomputes lookup on the fly.

Dispatch per group in `idaDfs`: check `groups_[i].kind` and `groups_[i].table_mode` before the inner loop, not inside it (to avoid branch overhead in the hot path).

---

## 7. UI changes (`index.html`)

- **Target Groups**: same textarea; new blank-line delimiter; OrientPerm syntax auto-detected.
- **Table mode**: global dropdown — "Full", "No transition table", "Incomplete (depth N)".
- **Incomplete depth**: number input (only active for incomplete mode).
- **Scramble** (renamed from "Starting Algorithm"): same element, new label.

---

## 8. Worker and message protocol (`worker.js`)

```js
{
  type: 'compute',
  k,
  targetGroups: [
    { kind: 'generator',  perms: Perm[] },
    { kind: 'orientperm', classes: [{ bases: int[], m: int, typeName: string, orientation_mod: int }] },
  ],
  tableMode: 'full' | 'no_transition' | 'incomplete',
  maxDepth: int,
  solvingPerms, solvingAlgos,
  startingPerms: Perm[],  // one or more starting permutations
}
```

---

## 9. New C++ API (`EMSCRIPTEN_BINDINGS`)

```cpp
// New methods on MultiTargetSolver:
void beginOrientPermGroup();
void addOrientPermClass(vector<int> sticker_bases, int m, int orientation_mod);
void buildOrientPermGroup();
void setTableMode(TableMode mode, int max_depth);   // call after beginTargetGroup or beginOrientPermGroup
void addSolvedState(vector<int> perm);              // call before buildTables; may call multiple times
```

Existing bindings unchanged.

---

## 10. Build

`schreier_sims.cpp` only. Compile command unchanged. New code under `#ifdef __EMSCRIPTEN__` as before.

---

## Implementation order (suggested)

1. **DONE** — Piece-based puzzle expansion in JS
2. **DONE** — OrientPerm JS parsing + worker message wiring (section 2a–2c)
3. **DONE** — C++ scaffolding: PiecePerm, OrientPermSpec struct, API bindings (section 2d, 2j)
4a. **DONE** — `OrientPermSpec::build()` — fixed piece detection (§2k)
4b. **DONE** — `OrientPermSpec::build()` — orientation constraint analysis, orient_step, orient_space (§2f, §2g)
4c. **DONE** — `OrientPermSpec::build()` — parity constraint analysis, initial_multinomial, perm_space, total_states (§2l, §2g)
5. **DONE** — `OrientPermSpec::state_to_index()` — PiecePerm scatter + incremental ratio + orient carrier (section 2e)
6. **DONE** — OrientPerm table building in `buildTables()` — flat array, DFS, BFS (section 2h)
7. **DONE** — OrientPerm IDA* lookup in `idaDfs` (section 2i)
8. Multi-source BFS in C++ (section 5) — small change, high value
9. Compact (mod-3) distance table (section 3)
10. Incomplete table mode (section 3)
11. No-transition IDA* for general groups (section 4)
12. Scramble field `[...]` and `<...>` syntax in JS (section 5)
13. UI: table mode controls, rename field (section 7)
