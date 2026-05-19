# Plan: Product Distance Tables

## Goal

Decouple distance tables from individual target groups. Instead of one distance table per
group, support *product distance tables* that track the simultaneous distance of 2+ groups.
For example, a product of EdgeOrientation (2048 states) × CornerOrientation (2187 states)
produces a 4 478 976-entry table tracking how far both are from being solved at once —
with no transition table stored for the product.

## Architecture change

**Before:**
```
TargetGroup[i]:
  transition_table[id * nMoves + mi]   ← keep
  distance_table (Mod3Table)           ← remove
  identity_id                          ← move to ProductDistanceTable

IDA*: h_i = distance_table[i][state_i],  h = max(h_i over all i)
```

**After:**
```
TargetGroup[i]:
  transition_table[id * nMoves + mi]   ← unchanged

ProductDistanceTable[p]:
  component_ids[]   ← indices into groups_
  strides[]         ← stride[j] = product of sizes[j+1..]
  total_states      ← product of sizes (long long, up to 16 B)
  distance_table    ← Mod3Table (2 bits/state)
  identity_id       ← long long

IDA*: pd_state_p = Σ_j state[comp_j] * stride_j
      h_p = distance_table[p][pd_state_p],  h = max(h_p over all p)
```

A singleton product table (one component) is identical to the old per-group distance table.

---

## Files to modify

- `schreier_sims.cpp` — all C++ logic
- `worker.js` — bridge product specs to C++
- `app.js` — parse new UI field
- `index.html` — add "Distance Tables" textarea

---

## Step-by-step C++ changes

### 1. Update `Mod3Table` for 64-bit indexing

Change `assign`, `get`, and `set` to use `long long` (or `size_t`) instead of `int`.
`std::vector<uint8_t>` already supports up to `SIZE_MAX` elements on 64-bit platforms, so
this is a signature-only change.

```cpp
void assign(long long n, int fill_val) { sz = n; ... data.assign((n + 3) / 4, byte); }
int  get(long long i) const { return (data[i >> 2] >> ((i & 3) << 1)) & 3; }
void set(long long i, int val) { int sh = (i & 3) << 1; uint8_t& b = data[i >> 2]; ... }
```

Add a `long long sz` field (rename from `int sz`).

### 2. Add `ProductDistanceTable` struct inside `MultiTargetSolver`

```cpp
struct ProductDistanceTable {
    std::vector<int>       component_ids;  // indices into groups_
    std::vector<long long> strides;        // stride[j] = ∏ sizes[j+1..]
    long long              total_states = 0;
    Mod3Table              distance_table;
    long long              identity_id = -1;
};
std::vector<ProductDistanceTable> product_tables_;
```

### 3. Remove `distance_table` and `identity_id` from `TargetGroup`

`TargetGroup` keeps only `transition_table` (and its group-theory fields). Every
reference to `grp.distance_table` or `grp.identity_id` inside the old BFS is deleted.

### 4. Add public API for specifying product tables

```cpp
void beginProductDistanceTable() {
    product_tables_.emplace_back();
}
void addProductTableComponent(int grp_idx) {
    product_tables_.back().component_ids.push_back(grp_idx);
}
int  getNumProductTables() const { return (int)product_tables_.size(); }
long long getProductTableSize(int p) const { return product_tables_[p].total_states; }
```

These are called (from JS) before `buildTables()`.

### 5. Modify `buildTables()`

New flow inside `buildTables()`:

```
1. Build transition tables for every group exactly as before
   (identical code for both ORIENTPERM and GENERAL kinds).
   BFS distance block is removed.

2. If product_tables_ is empty, create one singleton product table per group
   (backward-compatible default).

3. For each ProductDistanceTable p:
   a. Compute sizes[j] = table size of groups_[component_ids[j]]
   b. Compute strides (right to left: stride[nc-1]=1, stride[j]=stride[j+1]*sizes[j+1])
   c. Compute total_states = stride[0] * sizes[0]
   d. Compute identity_id = Σ_j identity_of_group[j] * strides[j]
      (identity_of_group[j] is the id of the identity state in group component_ids[j];
       stored temporarily in a helper variable during transition table construction)
   e. Call buildProductDistanceTable(p) — see step 6
```

For step (d), we need to know each group's identity id. Store it in a local parallel array
during transition table construction (or add a helper field `int identity_id` back to
`TargetGroup`, used only internally during `buildTables()`).

### 6. Sweep algorithm: `buildProductDistanceTable(int p)`

```cpp
void buildProductDistanceTable(int p) {
    auto& pdt = product_tables_[p];
    const long long N = pdt.total_states;
    const int nMoves = (int)solving_moves_.size();
    const int nc = (int)pdt.component_ids.size();

    // Per-component sizes (for odometer)
    std::vector<int> sizes(nc);
    for (int j = 0; j < nc; j++) {
        int gid = pdt.component_ids[j];
        sizes[j] = (int)(pdt.strides[j] == 1
            ? N / (j > 0 ? pdt.strides[j-1] : 1)  // last component
            : pdt.strides[j-1] / pdt.strides[j]);  // careful: use sizes[] directly
    }
    // Better: compute sizes[] from strides[] as sizes[j] = strides[j-1] / strides[j]
    // with strides[-1] = N.

    pdt.distance_table.assign(N, 3);           // 3 = unvisited
    pdt.distance_table.set(pdt.identity_id, 0);

    long long visited_count = 1;
    int depth_mod3 = 0;

    std::vector<int> comp(nc, 0);  // odometer: comp[j] = current state of component j

    while (visited_count < N) {
        std::fill(comp.begin(), comp.end(), 0);
        for (long long s = 0; s < N; s++) {
            if (pdt.distance_table.get(s) == depth_mod3) {
                for (int mi = 0; mi < nMoves; mi++) {
                    long long ns = 0;
                    for (int j = 0; j < nc; j++) {
                        int gid = pdt.component_ids[j];
                        int new_c = groups_[gid].transition_table[comp[j] * nMoves + mi];
                        ns += (long long)new_c * pdt.strides[j];
                    }
                    if (pdt.distance_table.get(ns) == 3) {
                        pdt.distance_table.set(ns, (depth_mod3 + 1) % 3);
                        ++visited_count;
                    }
                }
            }
            // Increment odometer (amortized O(1))
            for (int j = nc - 1; j >= 0; j--) {
                if (++comp[j] < sizes[j]) break;
                comp[j] = 0;
            }
        }
        depth_mod3 = (depth_mod3 + 1) % 3;
    }
}
```

**Why odometer instead of division?** Avoids `long long` division in the hot inner loop.
The odometer is incremented unconditionally for every `s`, costing ~O(1) amortized.

**Why sweep instead of BFS?** BFS stores an `(id, depth)` pair per frontier entry (8 bytes),
which can exceed the 0.25 bytes/state theoretical minimum of the distance table itself.
The sweep uses zero extra memory beyond the table.

**Complexity:** O(max_depth × N × nMoves × nc). For a 4 GB table (16 B entries) with
max_depth ≈ 20 and nMoves × nc ≈ 20, ≈ 6400 B total ops — this is the memory-bandwidth
limit, not a CPU limit.

### 7. Modify `idaDfs()` signature and body

New signature:
```cpp
int idaDfs(
    std::vector<std::vector<int>>&       ss,     // [depth][group_idx]
    std::vector<std::vector<long long>>& pd_ss,  // [depth][prod_table_idx]
    std::vector<std::vector<int>>&       hs,     // [depth][prod_table_idx]
    int g, int threshold, MoveStreak tail)
```

Inner loop changes (replace the old group-level update):
```cpp
const int np = (int)product_tables_.size();
for (int mi = 0; mi < nMoves; mi++) {
    if (pruner_.prune(tail.mi, tail.count, mi)) continue;

    // 1. Update individual group states
    for (int i = 0; i < t; i++)
        ss[g+1][i] = groups_[i].transition_table[state[i] * nMoves + mi];

    // 2. Update product states + h values
    for (int p = 0; p < np; p++) {
        long long new_pd = 0;
        for (int j = 0; j < (int)product_tables_[p].component_ids.size(); j++) {
            int ci = product_tables_[p].component_ids[j];
            new_pd += (long long)ss[g+1][ci] * product_tables_[p].strides[j];
        }
        pd_ss[g+1][p] = new_pd;
        hs[g+1][p] = hs[g][p]
            + kMod3Diff[hs[g][p] % 3][product_tables_[p].distance_table.get(new_pd)];
    }
    ...
}
```

h = `*std::max_element(hs[g].begin(), hs[g].end())`.

### 8. Modify `computeExactH()` for product tables

Rename to `computeExactH_product(int pd_idx, long long cur_pd_state)`. Decode component
states from the product state using strides (division), then greedy-walk the product
distance table toward identity.

```cpp
int computeExactH_product(int p, long long s) const {
    const auto& pdt = product_tables_[p];
    const int nMoves = (int)solving_moves_.size();
    const int nc = (int)pdt.component_ids.size();
    int distance = 0;
    while (s != pdt.identity_id) {
        int d1m3 = pdt.distance_table.get(s);
        // Decode component states
        long long rem = s;
        std::vector<int> comp(nc);
        for (int j = 0; j < nc; j++) {
            comp[j] = (int)(rem / pdt.strides[j]);
            rem    %= pdt.strides[j];
        }
        for (int mi = 0; mi < nMoves; mi++) {
            long long ns = 0;
            for (int j = 0; j < nc; j++) {
                int new_c = groups_[pdt.component_ids[j]].transition_table[comp[j] * nMoves + mi];
                ns += (long long)new_c * pdt.strides[j];
            }
            if (kMod3Diff[d1m3][pdt.distance_table.get(ns)] == -1) {
                s = ns; distance++; break;
            }
        }
    }
    return distance;
}
```

### 9. Modify `solve()`

Replace per-group state + h initialization with per-product-table initialization:

```cpp
const int np = (int)product_tables_.size();
std::vector<long long> pd_state(np);
std::vector<int> h_vals(np);
for (int p = 0; p < np; p++) {
    long long idx = 0;
    for (int j = 0; j < (int)product_tables_[p].component_ids.size(); j++) {
        int ci = product_tables_[p].component_ids[j];
        idx += (long long)state[ci] * product_tables_[p].strides[j];
    }
    pd_state[p] = idx;
    h_vals[p] = computeExactH_product(p, idx);
}
int h = *std::max_element(h_vals.begin(), h_vals.end());
```

Resize `pd_ss` and `hs` analogously to how `ss` and `hs` were resized before.

### 10. Update Emscripten bindings

```cpp
.function("beginProductDistanceTable", &MultiTargetSolver::beginProductDistanceTable)
.function("addProductTableComponent",  &MultiTargetSolver::addProductTableComponent)
.function("getNumProductTables",       &MultiTargetSolver::getNumProductTables)
.function("getProductTableSize",       &MultiTargetSolver::getProductTableSize)
```

---

## JS changes: `worker.js`

After setting up all individual groups (before `buildTables()`), iterate `productTableSpecs`
from the message payload:

```js
for (const spec of productTableSpecs) {      // spec = [0, 2] means T1*T3
    solver.beginProductDistanceTable();
    for (const idx of spec)
        solver.addProductTableComponent(idx);
}
solver.buildTables();
```

Update the `preview` and `tables_built` messages to report product table sizes instead of
(or in addition to) individual group sizes:

```js
const tableSizes = [];
for (let p = 0; p < solver.getNumProductTables(); p++)
    tableSizes.push(Number(solver.getProductTableSize(p)));
```

(Note: `getProductTableSize` returns `long long`; in JS this arrives as a `number`, which is
fine for sizes up to 2^53.)

---

## JS changes: `app.js`

Add `parseDistanceTables(text, numGroups)`:

```js
function parseDistanceTables(text, numGroups) {
    const trimmed = text.trim();
    if (!trimmed)
        return Array.from({ length: numGroups }, (_, i) => [i]);   // default singletons
    const specs = [];
    for (const line of trimmed.split('\n')) {
        for (const part of line.split(',')) {
            const p = part.trim(); if (!p) continue;
            const indices = p.split('*').map(s => {
                const m = s.trim().match(/^T?(\d+)$/i);
                if (!m) throw new Error(`Invalid group reference: "${s.trim()}"`);
                const idx = parseInt(m[1]) - 1;
                if (idx < 0 || idx >= numGroups) throw new Error(`T${idx+1} out of range (only ${numGroups} groups)`);
                return idx;
            });
            specs.push(indices);
        }
    }
    return specs.length ? specs : Array.from({ length: numGroups }, (_, i) => [i]);
}
```

Call this in `compute()` after parsing target groups, then include `productTableSpecs` in
the worker message payload.

---

## HTML changes: `index.html`

Add a new labeled textarea between "Target Subgroups" and "Solving Subgroup":

```html
<label for="dist-tables">Distance Tables</label>
<textarea id="dist-tables" rows="3"
  placeholder="T1 * T3&#10;T2&#10;(blank = one table per group)"></textarea>
```

---

## Commit plan

| Commit | Scope | Key changes |
|--------|-------|-------------|
| A | C++ core | `Mod3Table` 64-bit; `ProductDistanceTable` struct; remove `distance_table` from `TargetGroup`; `beginProductDistanceTable` / `addProductTableComponent` API; `buildProductDistanceTable` sweep; `buildTables` updated; `idaDfs` / `solve` / `computeExactH` updated; bindings updated |
| B | JS + HTML | `index.html` textarea; `app.js` `parseDistanceTables`; `worker.js` product table setup + size reporting |

These two commits are the natural split: A compiles and passes the existing test cases
(singleton default maintains identical behavior); B wires up the new UI.

---

## Edge cases and invariants

- **Empty product spec → default to singletons**: identical behavior to current code.
- **Single-component product**: same as old per-group distance table.
- **Group appearing in multiple product tables**: fine — each product table is independent.
- **Group appearing in no product table**: its transition table is still built (for groups
  that appear as components of other tables). If it truly appears in no table, the IDA*
  never consults its distance, so it's effectively unconstrained — caller's responsibility.
- **Product state index overflow**: strides are `long long`; product is bounded at 16 B
  (4 GB at 2 bits/state). Assert `total_states <= (long long)16e9` in
  `buildProductDistanceTables`.
- **`getProductTableSize` return type**: `long long` on the C++ side; JS receives it as a
  `number` (fine up to 2^53 ≈ 9 × 10^15, well above the 16 B limit).
- **BFS vs sweep**: sweep is used for ALL product tables (including singletons), so the
  old BFS queue code is deleted entirely.
