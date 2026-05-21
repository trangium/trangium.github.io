# Plan: Incomplete Groups

Groups that store distances only up to M moves from solved, using a hashmap.
Useful when the group is too large to enumerate completely (table would exceed memory).
States beyond M moves from solved get heuristic value M+1 (valid lower bound).

---

## Commit breakdown

### Commit A — C++: GENERAL incomplete groups
- `IncompleteGroup` struct (GENERAL fields only + `dist_map`)
- `incomplete_groups_` member on `MultiTargetSolver`
- `hashPerm64` private helper (takes first 64 bits of existing `hashPerm`)
- GENERAL BFS inside `buildTables()` (after complete-group loop)
- IDA* changes — full signature including all new parameters, even if `op_stacks` is always empty here:
  - `idaDfs` gains `ihs`, `cube_stack`, `op_stacks` parameters
  - In `solve()`: allocate `cube_stack[0] = startPerm` when any GENERAL incomplete group exists; allocate `ihs`; compute initial `ihs[0]` values; grow stacks with threshold
  - In the move loop: `if (!cube_stack.empty()) cube_stack[g+1] = compose(...)`, lookup loop for GENERAL groups only
  - h preamble includes `ihs[g]`
- Public API: `beginIncompleteGroup`, `addIncompleteGroupGenerator`, `buildIncompleteGroup(confidence, max_depth)`, `getNumIncompleteGroups`, `getIncompleteGroupTableSize`
- Emscripten bindings for the five new methods
- ORIENTPERM kind field exists in the struct but is never set; `op_stacks` is always empty in this commit

### Commit B — C++: ORIENTPERM incomplete groups
- Add ORIENTPERM fields to `IncompleteGroup`: `op_spec`, `raw_classes`, `zobrist_op`, `max_orient_mod`, `piece_to_class`
- `hashCompact` private helper (§2 ORIENTPERM Zobrist)
- In `buildTables()`: build `zobrist_op`, copy `piece_to_class`, run ORIENTPERM BFS for any ORIENTPERM incomplete group
- In `solve()`: extract compact perms from `startPerm` → populate `op_stacks[i][0]`; compute initial `ihs[0]` for ORIENTPERM groups
- In the move loop: advance `op_stacks` via `compose_piece`; dispatch to `hashCompact` for ORIENTPERM groups
- Public API: `beginIncompleteOrientPermGroup`, `addIncompleteOrientPermClass`, `buildIncompleteOrientPermGroup(max_depth)`
- Bindings update

### Commit C — JS + HTML + worker.js
- `[M]` syntax in `parseTargetGroups`: strip from line/block before parsing, attach `maxDepth` to group object
- `compute()`: pass `incompleteGroupSpecs` (array of maxDepth per group, null if complete) to worker
- `worker.js`: route each group to complete vs. incomplete API based on `maxDepth`; grow `tableKey` with `incompleteGroupSpecs`
- Group sizes will be dealt with in a future commit
- `index.html`: update the Target Subgroups hint text to mention `[M]` syntax
- Fix order of distance table specification to be order of appearance in Target Subgroups (current example is wrong)

---

## 1. New struct: `IncompleteGroup`

Add `std::vector<IncompleteGroup> incomplete_groups_` to `MultiTargetSolver`,
parallel to `groups_` and `product_tables_`.

```cpp
struct IncompleteGroup {
    enum GroupKind { GENERAL, ORIENTPERM };
    GroupKind kind = GENERAL;
    int max_depth = 0;  // M

    // GENERAL-only
    std::vector<Perm> generators;
    BSGS bsgs{0};
    std::vector<std::vector<Hash128>> zobrist;  // [pos][val], same layout as TargetGroup

    // ORIENTPERM-only
    OrientPermSpec op_spec;
    struct RawOPClass {
        std::vector<int> sticker_bases; int m; int orientation_mod; std::string type_name;
    };
    std::vector<RawOPClass> raw_classes;
    // Zobrist for ORIENTPERM: indexed [global_pos][class_id * max_orient_mod + reduced_orient]
    // global_pos = sum of earlier type counts + pos_within_type
    // max_orient_mod = max orientation_mod across all classes
    std::vector<std::vector<uint64_t>> zobrist_op;
    int max_orient_mod = 0;
    // piece_to_class[t][piece_within_type] = class index — copied from op_spec.piece_class after build
    std::vector<std::vector<int>> piece_to_class;

    // Distance hashmap: state_hash (64-bit) → actual distance (0..M).
    // States not in the map are ≥ M+1 moves from solved.
    std::unordered_map<uint64_t, uint8_t> dist_map;
    uint64_t identity_hash = 0;
};
```

---

## 2. Zobrist hashing

### GENERAL incomplete groups
Reuse the existing `hashPerm` logic from `TargetGroup`:
- `hashPerm` calls `bsgs.canonicalize(perm)`, then XORs `zobrist[i][c[base[i]]]` over base positions.
- For the 64-bit hashmap key, take `.first` of the returned `Hash128`.
- Two perms in the same coset of the group's target subgroup canonicalize to the same perm and therefore hash identically.

### ORIENTPERM incomplete groups
The existing `state_to_index_compact` cannot be used (too large to index as integer).
Need a Zobrist hash that is invariant to permuting pieces WITHIN the same class (pieces in a class are indistinguishable to the group quotient).

**Key insight:** hash by `(class_id, reduced_orientation)` per position, NOT by `(piece_id, full_orientation)`.

#### Build `zobrist_op` (done in `buildTables`, before BFS):
```
max_orient_mod = max(class.orientation_mod for all classes)
total_positions = sum(type.count for all types)
zobrist_op[pos][class_id * max_orient_mod + r] = random uint64_t
  for pos in [0, total_positions)
  for all class_id and r in [0, max_orient_mod)
```

Copy `op_spec.piece_class` into `incg.piece_to_class` after `op_spec.build()`.

#### Hash a compact perm state:
```
hash = 0
global_pos = 0
for each type t:
    for each position p in [0, types[t].count):
        val = compact[t][p]
        piece_id = (val - types[t].base) / types[t].m
        twist    = (val - types[t].base) % types[t].m
        class_id = piece_to_class[t][piece_id]
        reduced  = twist % classes[class_id].orientation_mod
        hash ^= zobrist_op[global_pos][class_id * max_orient_mod + reduced]
        global_pos++
return hash
```

This hash is invariant to swapping pieces within the same class (since class_id and reduced orientation are the only inputs, not piece_id). Pieces in the same class at the same position with the same reduced orientation contribute identically to the hash. Two pieces in the same class with the same reduced orientation are interchangeable — their swap is in the target subgroup.

---

## 3. Building the hashmap — BFS up to M

Done inside `buildTables()`, after transition tables are built for complete groups (since BFS for GENERAL groups uses `solving_moves_`).

### GENERAL incomplete group BFS:
```
Queue of Perm; start with identity(n_).
identity_hash = hashPerm64(identity, incg)  // first 64 bits of hashPerm
incg.dist_map[identity_hash] = 0
while queue not empty:
    perm = dequeue
    d = incg.dist_map[hashPerm64(perm, incg)]
    if d >= incg.max_depth: continue
    for each solving_move mv:
        next = compose(perm, mv)
        h = hashPerm64(next, incg)
        if h not in dist_map:
            dist_map[h] = d + 1
            enqueue(next)
```

The queue holds full Perm objects; at most O(|G_M|) entries where |G_M| is the number of states reachable in M moves. Memory is bounded by `max_depth` × branching factor, same order as user's table.

### ORIENTPERM incomplete group BFS:
```
Queue of vector<PiecePerm> (compact perm, one PiecePerm per type).
Start with id_compact (identity compact perm, as in buildTables for complete ORIENTPERM groups).
identity_hash = hashCompact(id_compact, incg)
incg.dist_map[identity_hash] = 0
while queue not empty:
    compact = dequeue
    d = dist_map[hashCompact(compact, incg)]
    if d >= incg.max_depth: continue
    for mi in [0, nMoves):
        next[t] = compose_piece(compact[t], compact_move[mi][t], base_t, m_t)  (per type)
        h = hashCompact(next, incg)
        if h not in dist_map:
            dist_map[h] = d + 1
            enqueue(next)
```

`compact_move[mi][t]` is extracted the same way as for complete ORIENTPERM groups in `buildTables`.

---

## 4. IDA* modification

### Tracking state for incomplete groups

If `incomplete_groups_` is non-empty, `solve()` and `idaDfs()` track additional state.

**Full cube perm stack** (for GENERAL incomplete groups):
```cpp
std::vector<Perm> cube_stack;  // cube_stack[g] = cube perm at depth g
// initialized: cube_stack[0] = startPerm (passed to solve())
// grown lazily as threshold increases
```

**Per-incomplete-ORIENTPERM-group compact perm stacks**:
```cpp
// op_stacks[i][g][t] = compact perm for type t of incomplete ORIENTPERM group i at depth g
std::vector<std::vector<std::vector<PiecePerm>>> op_stacks;
// initialized from startPerm using op_spec.state_to_index logic (extract_piece per type)
```

Only allocate these when `!incomplete_groups_.empty()`. If no incomplete groups, zero overhead.

### `idaDfs` signature extension:
```cpp
int idaDfs(
    std::vector<std::vector<int>>& ss,
    std::vector<std::vector<int>>& hs,
    std::vector<std::vector<int>>& prod_hs,
    std::vector<std::vector<int>>& ihs,       // new: [depth][incomplete_group_idx]
    std::vector<Perm>& cube_stack,             // new (empty if no GENERAL incomplete groups)
    std::vector<std::vector<std::vector<PiecePerm>>>& op_stacks,  // new (empty if none)
    int g, int threshold, MoveStreak tail)
```

### Inside the move loop:
```cpp
// 1. Advance cube perm (if any GENERAL incomplete groups)
if (!cube_stack.empty())
    cube_stack[g+1] = compose(cube_stack[g], solving_moves_[mi]);

// 2. Advance compact perms (if any ORIENTPERM incomplete groups)
for (int i = 0; i < n_iop; i++) {
    for (int t = 0; t < n_types_i[i]; t++)
        op_stacks[i][g+1][t] = compose_piece(
            op_stacks[i][g][t], compact_moves_op[i][mi][t],
            incg_types[i][t].base, incg_types[i][t].m);
}

// 3. Look up incomplete h values (NO mod-3 propagation — direct lookup each time)
for (int i = 0; i < ni; i++) {
    uint64_t h = (incomplete_groups_[i].kind == IncompleteGroup::GENERAL)
        ? hashPerm64(cube_stack[g+1], incomplete_groups_[i])
        : hashCompact(op_stacks[op_idx[i]][g+1], incomplete_groups_[i]);
    auto it = incomplete_groups_[i].dist_map.find(h);
    ihs[g+1][i] = (it != end) ? (int)it->second
                               : incomplete_groups_[i].max_depth + 1;
}
```

### h computation in `idaDfs` preamble:
```cpp
int h = *std::max_element(h_vals.begin(), h_vals.end());  // singleton
for (auto v : ph_vals) h = std::max(h, v);                // product
for (auto v : ihs[g]) h = std::max(h, v);                 // incomplete
```

No mod-3 trick for incomplete groups — values are exact distances (admissible lower bounds).

### `solve()` initialization:
For each incomplete GENERAL group:
- Compute `identity_hash` → `hashPerm64(startPerm, incg)`, lookup → initial `ihs[0][i]`
- Initialize `cube_stack[0] = startPerm`

For each incomplete ORIENTPERM group:
- Extract compact perm from `startPerm` using `extract_piece` per type → `op_stacks[i][0]`
- Compute `hashCompact(op_stacks[i][0], incg)` → lookup → initial `ihs[0][i]`

Unreachable check: if initial state not in dist_map, `ihs[0][i] = max_depth + 1`. This is a valid lower bound; no hard "unreachable" signal (unlike complete groups). IDA* will exhaust up to `max_moves` without finding a solution if truly unreachable.

Stack growth: `cube_stack` grows with `threshold + 1` perms. `op_stacks[i]` grows with `threshold + 1` slots. `ihs` grows with `threshold + 1` rows of `ni` ints.

---

## 5. Public API additions

```cpp
// ── Incomplete general groups ──────────────────────────────────────────────────
void beginIncompleteGroup();
void addIncompleteGroupGenerator(const std::vector<int>& g);
void buildIncompleteGroup(int confidence, int max_depth);
    // Runs Schreier-Sims; defers BFS to buildTables().

// ── Incomplete OrientPerm groups ──────────────────────────────────────────────
void beginIncompleteOrientPermGroup();
void addIncompleteOrientPermClass(std::vector<int> sticker_bases, int m,
                                  int orientation_mod, std::string type_name);
void buildIncompleteOrientPermGroup(int max_depth);
    // Mirrors buildOrientPermGroup; defers BFS to buildTables().
    // Must be called after buildSolvingBSGS.

// ── Query ─────────────────────────────────────────────────────────────────────
int getNumIncompleteGroups() const;
int getIncompleteGroupTableSize(int i) const;  // returns dist_map.size()
```

Emscripten bindings for all six new methods.

---

## 6. `buildTables()` changes

After the existing complete-group loop and before `buildProductDistanceTables()`:
```
for each incomplete group:
    if ORIENTPERM:
        build op_spec (same as buildOrientPermGroup, already done at spec-build time)
        extract compact_move[mi][t] for all solving moves
        build zobrist_op table (random uint64_t per position per class/orientation combo)
        copy piece_to_class from op_spec.piece_class
        BFS up to max_depth using compact perms
    if GENERAL:
        build zobrist table (same as complete GENERAL group — makeZobrist)
        BFS up to max_depth using full Perm objects
```

**Important:** incomplete groups do NOT build transition tables. Only `dist_map` and the Zobrist tables are built.

---

## 7. JS/HTML/worker.js changes

### Syntax for incomplete groups

In the "Target Subgroups" textarea, add `[M]` suffix to mark a group as incomplete with max depth M:

**GENERAL group (single-line):**
```
R, U, L, D [10]
```

**ORIENTPERM group (multi-line block):**
The `[M]` tag goes on its own line within the block (or as the last line):
```
[10]
1: {UFR UFL UBL UBR DFR DFL DBL DBR}
1: {UF UL UB UR DR FR BR DL DF DB} FL BL
```

### `parseTargetGroups` changes

When parsing a block/line, check for a `[N]` token:
- Strip it from the line/block before normal parsing
- Record `maxDepth: N` on the resulting group object (or `null` if absent)

Group object gains optional field: `{ kind: 'generator'|'orientperm', ..., maxDepth: number|null }`

### `app.js` compute()

Extract `incompleteGroupSpecs` from parsed groups:
```js
const incompleteGroupSpecs = targetGroups.map(g => g.maxDepth ?? null);
// pass to worker
```

### `worker.js` changes

After building each group:
```js
if (group.maxDepth != null) {
    // use incomplete API instead
}
```

Complete flow for each group:
- **Complete GENERAL** (maxDepth == null): existing `beginTargetGroup` / `addTargetGenerator` / `buildTargetGroup`
- **Incomplete GENERAL** (maxDepth != null, kind='generator'): `beginIncompleteGroup` / `addIncompleteGroupGenerator` / `buildIncompleteGroup(100, maxDepth)`
- **Complete ORIENTPERM** (maxDepth == null): existing `beginOrientPermGroup` / `addOrientPermClass` / `buildOrientPermGroup`
- **Incomplete ORIENTPERM** (maxDepth != null, kind='orientperm'): `beginIncompleteOrientPermGroup` / `addIncompleteOrientPermClass` / `buildIncompleteOrientPermGroup(maxDepth)`

Table sizes reported to UI: add loop over `getNumIncompleteGroups()` / `getIncompleteGroupTableSize(i)`.

The `tableKey` cache key should include `incompleteGroupSpecs`.

### UI status display

In the `preview` message, incomplete groups cannot easily predict table sizes before BFS.
Show `≤ (branching^M)` as rough upper bound, or just "partial table, M=[max_depth]".

After `tables_built`, report incomplete group sizes alongside complete group sizes.

---

## 8. Edge cases and notes

- **Hash collisions:** 64-bit Zobrist keys have collision probability ~1/2^64 per pair. For tables up to ~10^9 entries, birthday paradox probability is ~10^18 / 2^64 ≈ 0.05% — acceptable for a puzzle solver.
- **Fixed pieces in ORIENTPERM:** `op_spec` may mark some pieces as "fixed" (not contributing to the state). The Zobrist hash should skip fixed pieces. Use `op_spec.types[t].effective_count` to know how many non-fixed pieces exist. Actually, fixed pieces always stay at their home position, so they always contribute the same Zobrist value — can include or skip them consistently.
- **Orientation constraints (orient_step, parity):** The Zobrist hash does NOT need to encode these — they're structural properties that are always satisfied by reachable states. The hash just needs to distinguish different cosets, and the (class_id, reduced_orient) per position is sufficient.
- **BFS memory for GENERAL groups:** Each queue entry is a full `Perm` (n_ ints). n_ ≤ 48 for a 3×3. 1M entries ≈ 192 MB — large but feasible for M=10–12 on a Rubik's cube. For ORIENTPERM, compact perms are much smaller.
- **`max_depth = 0`:** Should behave like a complete group but with only the identity stored. Probably never useful, but gracefully handled: BFS terminates immediately, only identity in the map.
- **Interaction with product tables:** Incomplete groups are purely heuristic — they don't participate in `ProductDistanceTable` (which requires `transition_table`). If a user tries to add an incomplete group as a product table component, the user should receive an informative error message.
