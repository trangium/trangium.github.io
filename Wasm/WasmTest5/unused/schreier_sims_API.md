# schreier_sims.cpp — API Reference (current)

Monte Carlo Schreier-Sims for permutation groups, compiled to WebAssembly via
Emscripten.  The frontend in this directory (`index.html` / `app.js` /
`worker.js`) implements a coset canonicalizer + canonical-ID table builder.

---

## Compile (Powershell)

```bat
REM From repo root (Windows).  emsdk must be installed next to the repo.
emsdk\upstream\emscripten\emcc.bat Wasm\WasmTest5\schreier_sims.cpp ^
    -o Wasm\WasmTest5\a.out.js ^
    -lembind --no-entry -O2 -std=c++17 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1
```

Produces `a.out.js` + `a.out.wasm` in the same directory.

---

## Native build (testing / debugging)

```sh
g++ -O2 -std=c++17 -o schreier_sims schreier_sims.cpp
./schreier_sims          # runs built-in test cases (V4, S4, S5, A5, S8)
./schreier_sims --stdin  # interactive: read n t on first line, then t permutations
```

---

## Core C++ types (not directly exposed to JS)

```cpp
using Perm = std::vector<int>;   // zero-indexed: p[i] = image of i
```

Key free functions:
```cpp
Perm identity(int n);
Perm compose(const Perm& a, const Perm& b);   // result[i] = b[a[i]]  (a first, then b)
Perm inv(const Perm& a);

BSGS randomized_schreier_sims(int n, const std::vector<Perm>& generators,
                               int confidence = 40, uint32_t seed = 42);
```

`BSGS::canonicalize(s)` returns the canonical (lex-min) left-coset representative
of `s` under the group:
```
canonicalize(g ∘ s) == canonicalize(s)   for every g in G
```
Two permutations are in the same left coset iff their canonical forms are equal.

---

## Emscripten / JS API

### Registered types

| JS name      | C++ type            |
|-------------|---------------------|
| `VectorInt` | `std::vector<int>`  |

`VectorInt` supports: `push_back(x)`, `get(i)`, `size()`, `delete()`.

---

### `SchreierSimsRunner`

Single class that manages a **target BSGS** (for canonicalization) and
**solving moves** (for the DFS table).  Typically two instances are used:
one as the target runner, one as the solving runner.

```js
const runner = new Module.SchreierSimsRunner();
```

#### Target-group API

| Method | Description |
|--------|-------------|
| `reset(n)` | Set domain size to `n`, clear all generators and cached state. |
| `addGenerator(v: VectorInt)` | Add one generator permutation of length `n`. |
| `build(confidence: int)` | Run Monte Carlo Schreier-Sims; store BSGS internally.  `confidence` = consecutive trivial sifts before stopping; error ≤ 2^−confidence.  Use **80** in practice. |
| `run(confidence: int) → string` | `build` + return JSON `{"base":[…],"orbitSizes":[…],"order":N}`. |
| `getBase() → VectorInt` | Base points of the stored BSGS.  **Must call `build` first.** |
| `canonicalizePerm(v: VectorInt) → VectorInt` | Canonicalize a permutation using the stored BSGS. Returns a new `VectorInt` — **call `.delete()` on it**. |

#### Solving-move / DFS-table API

| Method | Description |
|--------|-------------|
| `clearSolvingMoves()` | Clear the solving move list. |
| `addSolvingMove(v: VectorInt)` | Append one solving move (permutation of length `n`). |
| `buildTable(base: VectorInt) → int` | Run DFS over the solving moves.  For each reachable state, computes `canonicalize(cube)` using the target BSGS, then keys the table on the images of `base` points under that canonical perm.  Stores the table internally.  Returns total table size. |
| `lookupCanonId(v: VectorInt) → int` | Look up a permutation's canonical ID in the stored table.  Returns `-1` if not found. |
| `getTableSize() → int` | Number of entries in the stored table. |

---

## Full pipeline (as implemented in worker.js)

```
Inputs
  k              — domain size (inferred from puzzle cycle notation)
  targetPerms    — generator permutations for the target group
  solvingPerms   — generator permutations for the solving group
  startingPerm   — the algorithm whose canonical ID we want

Step 1 — build target BSGS
  targetRunner.reset(k)
  for each p in targetPerms: targetRunner.addGenerator(p)
  targetRunner.build(40)

Step 2 — build solving BSGS, get base
  solvingRunner.reset(k)
  for each p in solvingPerms: solvingRunner.addGenerator(p)
  solvingRunner.build(40)
  base ← solvingRunner.getBase()          // VectorInt → JS array

Step 3 — load solving moves into targetRunner and run C++ DFS
  targetRunner.clearSolvingMoves()
  for each p in solvingPerms: targetRunner.addSolvingMove(p)
  tableSize ← targetRunner.buildTable(base)   // DFS entirely in C++

Step 4 — look up starting algorithm
  id ← targetRunner.lookupCanonId(startingPerm)   // -1 if not reachable
```

**Canonical key** used inside `buildTable` / `lookupCanonId`:
```
key = canonicalize(cube)[base[0]], canonicalize(cube)[base[1]], …
```
i.e. the images of the solving-BSGS base points under the target-canonicalized permutation, joined as a comma-separated string.

---

## Puzzle input format (parsed in JS, not C++)

```
# Atomic move: cycle notation
U: (0 1 2 3) (4 16 12 8) (5 17 13 9)

# Derived move: sequence of already-defined move names
U2: U U
U': U U U
```

`(a b c)` means a→b, b→c, c→a.  Atomic moves are parsed first; derived moves
are resolved in a fixed-point loop so ordering within derived moves doesn't matter
as long as there are no cycles.

Generators (target or solving) are entered as comma- or newline-separated
sequences of move names, e.g.:
```
F R U R' U' F', R U R' U R U2 R'
```

---

## 2×2×2 example defaults

**Puzzle**
```
U: (0 1 2 3) (4 16 12 8) (5 17 13 9)
R: (12 13 14 15) (1 19 21 9) (2 16 22 10)
F: (8 9 10 11) (3 12 21 6) (2 15 20 5)
U2: U U    U': U U U
R2: R R    R': R R R
F2: F F    F': F F F
```

**Target generators** `F R U R' U' F', R U R' U R U2 R'`

**Solving generators** `R, U, F`

**Starting algorithm** `R' F R U R U' R' F' R U' R'`
→ canonical ID (example output): some integer in [0, tableSize)
