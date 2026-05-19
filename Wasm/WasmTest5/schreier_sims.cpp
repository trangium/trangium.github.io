/**
 * Fast Randomized (Monte Carlo) Schreier-Sims Algorithm
 * ======================================================
 * Computes a Base and Strong Generating Set (BSGS) for a permutation group.
 *
 * ── Overview ──────────────────────────────────────────────────────────────
 *
 * A BSGS for G ≤ Sym(n) consists of:
 *   • Base B = (β₀, β₁, …, β_{k-1}): a sequence of points such that the
 *     pointwise stabilizer chain  G = G⁽⁰⁾ ≥ G⁽¹⁾ ≥ … ≥ G⁽ᵏ⁾ = {1}
 *     is non-trivially refined at each step.  (G⁽ⁱ⁾ = stab of β₀…β_{i-1}.)
 *   • SGS S: a set of generators such that S ∩ G⁽ⁱ⁾ generates G⁽ⁱ⁾ ∀i.
 *
 * Each level stores a Schreier tree (BFS orbit + coset representatives):
 *   transversal[pt] = u_pt  with  β_i ^ u_pt = pt.
 *
 * Sifting: given g, strip via transversal representatives until the residue
 * is identity (g ∈ G) or an orbit miss proves the chain is incomplete.
 *
 * Randomized phase (Monte Carlo):
 *   Product replacement (Shake/Rattle) draws pseudo-random group elements.
 *   Non-trivial sift residues augment the chain.  Stop after `confidence`
 *   consecutive trivial sifts → error probability ≤ 2^{-confidence}.
 *
 * Complexity (Monte Carlo): O(n log n · log⁴|G|  +  t·n·log|G|)
 *   where t = |generators|.
 *
 * ── Build ──────────────────────────────────────────────────────────────────
 *   g++ -O2 -std=c++17 -o schreier_sims schreier_sims.cpp
 *
 * ── Run ────────────────────────────────────────────────────────────────────
 *   ./schreier_sims             # built-in examples
 *   ./schreier_sims --stdin     # read from stdin: n t \n g1 \n g2 …
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

// ─── Types ───────────────────────────────────────────────────────────────────

using Perm = std::vector<int>;
using Hash128 = std::pair<uint64_t, uint64_t>;

// One integer per piece: base + m * source_piece + orientation_twist.
// full_perm[base + m*pos] for each position pos.
using PiecePerm = std::vector<int>;

struct Hash128Hasher {
    size_t operator()(const Hash128& h) const {
        size_t s = h.first;
        s ^= h.second + 0x9e3779b97f4a7c15ULL + (s << 6) + (s >> 2);
        return s;
    }
};

// Zobrist table: zobrist[pos][val] are independent random 128-bit values.
// Collision probability for any fixed pair of distinct inputs is exactly 2^-128.
static std::vector<std::vector<Hash128>> makeZobrist(int base_size, int n) {
    std::mt19937_64 rng(0xdeadbeefcafe1234ULL);
    std::vector<std::vector<Hash128>> z(base_size, std::vector<Hash128>(n));
    for (auto& row : z)
        for (auto& h : row)
            h = {rng(), rng()};
    return z;
}

// ─── Permutation primitives ──────────────────────────────────────────────────

static Perm identity(int n) {
    Perm p(n);
    std::iota(p.begin(), p.end(), 0);
    return p;
}

static bool is_identity(const Perm& p) {
    for (int i = 0; i < (int)p.size(); ++i)
        if (p[i] != i) return false;
    return true;
}

// Apply a first, then b:  result[i] = b[a[i]]
static Perm compose(const Perm& a, const Perm& b) {
    int n = (int)a.size();
    Perm r(n);
    for (int i = 0; i < n; ++i) r[i] = b[a[i]];
    return r;
}

static Perm inv(const Perm& a) {
    int n = (int)a.size();
    Perm r(n);
    for (int i = 0; i < n; ++i) r[a[i]] = i;
    return r;
}

// ─── PiecePerm operations ────────────────────────────────────────────────────

// Compose A then B for one piece type (base B_base, m stickers per piece).
static PiecePerm compose_piece(const PiecePerm& A, const PiecePerm& B, int base, int m) {
    PiecePerm R(A.size());
    for (int src = 0; src < (int)A.size(); src++) {
        int a = A[src] - base, mid = a / m, tA = a % m;
        int b = B[mid] - base;
        R[src] = base + (b / m) * m + (tA + b % m) % m;
    }
    return R;
}

// Invert: piece at src goes to dest with twist t  →  dest now holds src with -t.
static PiecePerm invert_piece(const PiecePerm& A, int base, int m) {
    PiecePerm R(A.size());
    for (int src = 0; src < (int)A.size(); src++) {
        int a = A[src] - base;
        R[a / m] = base + m * src + (m - a % m) % m;
    }
    return R;
}

// Extract compact representation from full sticker permutation.
static PiecePerm extract_piece(const Perm& full, int base, int m, int n_t) {
    PiecePerm cp(n_t);
    for (int pos = 0; pos < n_t; pos++)
        cp[pos] = full[base + m * pos];
    return cp;
}

// ─── Compact mod-3 distance table ────────────────────────────────────────────
//
// Packs 4 entries per byte (2 bits each). Values 0–2 are dist mod 3.
// Value 3 (binary 11) is the "unvisited" sentinel.

struct Mod3Table {
    std::vector<uint8_t> data;
    long long sz = 0;

    void assign(long long n, int fill_val) {
        sz = n;
        uint8_t byte = 0;
        int v = fill_val & 3;
        for (int i = 0; i < 4; i++) byte |= v << (2 * i);
        data.assign((n + 3) / 4, byte);
    }

    int get(long long i) const {
        return (data[i >> 2] >> ((i & 3) << 1)) & 3;
    }

    void set(long long i, int val) {
        int sh = (int)((i & 3) << 1);
        uint8_t& b = data[i >> 2];
        b = (b & ~(3 << sh)) | ((val & 3) << sh);
    }
};

// ─── Stabilizer level ────────────────────────────────────────────────────────

struct Level {
    int base_point = -1;

    // transversal[pt] = coset rep u  such that  base_point ^ u = pt
    std::unordered_map<int, Perm> transversal;

    // Generators of G^(i)  (together across all levels = SGS)
    std::vector<Perm> generators;

    Level() = default;
    explicit Level(int bp) : base_point(bp) {}

    bool in_orbit(int pt) const { return transversal.count(pt) > 0; }

    // Add gen as a generator and BFS-extend the orbit.
    // Seeds transversal with base_point → id if it is still empty.
    // Returns true if the orbit actually grew.
    bool add_generator_extend(const Perm& gen, int n) {
        // Skip exact duplicates (cheap check on small generators lists)
        for (const Perm& g : generators)
            if (g == gen) return false;
        generators.push_back(gen);

        if (transversal.empty())
            transversal[base_point] = identity(n);

        bool grew = false;
        // Re-run BFS from all known points with the full generator set.
        std::vector<int> frontier;
        for (auto& [pt, _] : transversal) frontier.push_back(pt);

        while (!frontier.empty()) {
            std::vector<int> next_front;
            for (int pt : frontier)
                for (const Perm& g : generators) {
                    int img = g[pt];
                    if (!transversal.count(img)) {
                        transversal[img] = compose(transversal.at(pt), g);
                        next_front.push_back(img);
                        grew = true;
                    }
                }
            frontier = std::move(next_front);
        }
        return grew;
    }
};

// ─── BSGS ────────────────────────────────────────────────────────────────────

struct BSGS {
    int n;
    std::vector<Level> chain;

    explicit BSGS(int n) : n(n) {}

    // Sift (strip) g through the stabilizer chain.
    // Returns the residue.  Identity iff g ∈ G and chain is complete.
    Perm sift(Perm g) const {
        for (const Level& lev : chain) {
            int img = g[lev.base_point];
            auto it = lev.transversal.find(img);
            if (it == lev.transversal.end()) return g;  // orbit miss → residue
            g = compose(g, inv(it->second));             // strip coset rep
            if (is_identity(g)) return g;
        }
        return g;
    }

    Perm sift_from(Perm g, int start) const {
        for (int i = start; i < (int)chain.size(); ++i) {
            int img = g[chain[i].base_point];
            auto it = chain[i].transversal.find(img);
            if (it == chain[i].transversal.end()) return g;
            g = compose(g, inv(it->second));
            if (is_identity(g)) return g;
        }
        return g;
    }

    // Canonical representative of the equivalence class of s under left G-action.
    //   canonicalize(compose(g, s)) == canonicalize(s)  for every g in G.
    // Equivalently: canonicalize(s) == canonicalize(t)  iff  compose(t, inv(s)) in G.
    //
    // Algorithm: at each level i, left-multiply s by the transversal element that
    // maps base_point to the orbit point minimizing s[pt].  This greedy choice is
    // invariant to any prior left-G multiplication (since G permutes each orbit).
    Perm canonicalize(Perm s) const {
        for (const Level& lev : chain) {
            int best_pt = -1, best_val = n;
            for (auto& [pt, u] : lev.transversal)
                if (s[pt] < best_val) { best_val = s[pt]; best_pt = pt; }
            s = compose(lev.transversal.at(best_pt), s);
        }
        return s;
    }

    // Augment the chain with element g (must be non-identity).
    // Returns true if the chain was modified.
    bool augment(Perm g) {
        for (int i = 0; i < (int)chain.size(); ++i) {
            Level& lev = chain[i];
            int img = g[lev.base_point];

            if (!lev.in_orbit(img)) {
                // g expands the orbit at level i → register as new generator
                lev.add_generator_extend(g, n);
                propagate_schreier(i);   // push new Schreier gens downward
                return true;
            }
            g = compose(g, inv(lev.transversal.at(img)));
            if (is_identity(g)) return false;
        }

        // g survived all levels: we need a new base point
        int new_base = -1;
        for (int j = 0; j < n; ++j)
            if (g[j] != j) { new_base = j; break; }
        if (new_base == -1) return false;   // identity slipped through

        Level lev(new_base);
        lev.transversal[new_base] = identity(n);
        lev.add_generator_extend(g, n);
        chain.push_back(std::move(lev));
        return true;
    }

    // Compute Schreier generators for level i+1 from level i and augment them.
    //   Schreier gen:  u_x · s · u_{x^s}^{-1}
    //   for x ∈ Δ^(i), s ∈ generators^(i)
    void propagate_schreier(int i) {
        if (i + 1 >= (int)chain.size()) return;
        const Level& lev = chain[i];
        std::vector<Perm> schreier;
        for (auto& [x, u_x] : lev.transversal)
            for (const Perm& s : lev.generators) {
                int xs = s[x];
                auto it = lev.transversal.find(xs);
                if (it == lev.transversal.end()) continue;
                Perm sg = compose(compose(u_x, s), inv(it->second));
                if (!is_identity(sg)) schreier.push_back(std::move(sg));
            }
        for (Perm& sg : schreier) augment(std::move(sg));
    }

    // Deterministic verification + correction via Schreier generators.
    // After Monte Carlo, iterates every (orbit-point, generator) pair at each
    // level, sifts the Schreier generator through the deeper levels, and
    // augments on any non-trivial residue.  Repeats until fully closed.
    void verify_and_complete() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < (int)chain.size(); ++i) {
                std::unordered_map<int, Perm> trans = chain[i].transversal;
                std::vector<Perm> gens;
                for (int j = i; j < (int)chain.size(); ++j)
                    gens.insert(gens.end(),
                                chain[j].generators.begin(),
                                chain[j].generators.end());
                for (auto& [omega, u_omega] : trans) {
                    for (const Perm& s : gens) {
                        int img = s[omega];
                        auto it = trans.find(img);
                        if (it == trans.end()) {
                            // img not in snapshot — check the live orbit.
                            auto live_it = chain[i].transversal.find(img);
                            if (live_it == chain[i].transversal.end()) {
                                // Genuinely missing: compose(u_omega, s) maps
                                // base_point → omega → img ∉ orbit, so augmenting
                                // it will extend the level-i orbit correctly.
                                augment(compose(u_omega, s));
                                changed = true;
                                continue;
                            }
                            // img was added to the live orbit earlier this pass.
                            // Fall through using the live representative so the
                            // Schreier generator is not silently dropped.
                            Perm sch = compose(compose(u_omega, s), inv(live_it->second));
                            if (is_identity(sch)) continue;
                            Perm residue = sift_from(sch, i + 1);
                            if (!is_identity(residue)) {
                                augment(residue);
                                changed = true;
                            }
                            continue;
                        }
                        Perm sch = compose(compose(u_omega, s), inv(it->second));
                        if (is_identity(sch)) continue;
                        Perm residue = sift_from(sch, i + 1);
                        if (!is_identity(residue)) {
                            augment(residue);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    // Remove exact-duplicate generators within each level
    void deduplicate() {
        for (Level& lev : chain) {
            std::vector<Perm> unique_gens;
            for (const Perm& g : lev.generators) {
                bool dup = false;
                for (const Perm& u : unique_gens) if (u == g) { dup = true; break; }
                if (!dup) unique_gens.push_back(g);
            }
            lev.generators = std::move(unique_gens);
        }
    }

    std::vector<int> base() const {
        std::vector<int> B;
        for (const Level& lev : chain) B.push_back(lev.base_point);
        return B;
    }

    // SGS grouped by stabilizer level
    std::vector<std::vector<Perm>> sgs_per_level() const {
        std::vector<std::vector<Perm>> res;
        for (const Level& lev : chain) res.push_back(lev.generators);
        return res;
    }

    long long order() const {
        long long ord = 1;
        for (const Level& lev : chain)
            ord *= (long long)lev.transversal.size();
        return ord;
    }
};

// ─── Product-replacement random element generator ────────────────────────────
//
// Maintains a bag of pseudo-random group elements.
// Each call to next() randomly replaces bag[i] ← bag[i] · bag[j]^{±1}.

struct ProductReplacer {
    std::vector<Perm> bag;
    Perm acc;

    std::mt19937& rng;
    int n;

    std::uniform_int_distribution<int> bit{0, 1};

    ProductReplacer(int n,
                     const std::vector<Perm>& gens,
                     std::mt19937& rng)
        : rng(rng), n(n), acc(identity(n))
    {
        // Build initial bag
        for (const Perm& g : gens) {
            bag.push_back(g);
            bag.push_back(inv(g));
        }

        // Pad bag to decent size
        while ((int)bag.size() < 32)
            bag.push_back(identity(n));

        // Long burn-in
        for (int k = 0; k < 2000; ++k)
            step();
    }

    Perm next() {
        // Multiple mutations between samples
        for (int k = 0; k < 4; ++k)
            step();

        return acc;
    }

private:
    void step() {
        std::uniform_int_distribution<int> d(0, (int)bag.size() - 1);

        int i = d(rng);
        int j;
        do {
            j = d(rng);
        } while (j == i);

        const Perm& g = bag[j];

        Perm h = bit(rng) ? g : inv(g);

        // Randomize left/right multiplication
        if (bit(rng))
            bag[i] = compose(bag[i], h);
        else
            bag[i] = compose(h, bag[i]);

        // Update accumulator
        if (bit(rng))
            acc = compose(acc, bag[i]);
        else
            acc = compose(bag[i], acc);
    }
};

// ─── Randomized Schreier-Sims ─────────────────────────────────────────────────

/**
 * Compute a BSGS using the Monte Carlo Schreier-Sims algorithm.
 *
 * @param n          Degree of the group (points are {0, …, n−1})
 * @param generators Generating set (each a Perm of length n)
 * @param confidence Stop after this many consecutive trivial sifts.
 *                   Error probability ≤ 2^{-confidence}.  Default 40 ≈ 10^{-12}.
 * @param seed       RNG seed for reproducibility
 */
BSGS randomized_schreier_sims(int n,
                               const std::vector<Perm>& generators,
                               int confidence = 40,
                               uint32_t seed = 42) {
    BSGS bsgs(n);

    // Phase 1 – deterministic seed: insert all generators
    for (const Perm& g : generators)
        if (!is_identity(g)) bsgs.augment(g);

    if (generators.empty()) return bsgs;

    // Phase 2 – Monte Carlo augmentation
    std::mt19937 rng(seed);
    ProductReplacer pr(n, generators, rng);

    int hits = 0;
    while (hits < confidence) {
        Perm r        = pr.next();
        Perm residue  = bsgs.sift(r);
        if (!is_identity(residue)) {
            bsgs.augment(residue);
            hits = 0;
        } else {
            ++hits;
        }
    }

    bsgs.verify_and_complete();
    bsgs.deduplicate();
    return bsgs;
}

// Utility functions
static bool membership_test(const BSGS& bsgs, const Perm& g) {
    return is_identity(bsgs.sift(g));
}

// True iff H ≤ G
// Checks whether every SGS generator of H belongs to G.
// Since the SGS generates H, this is sufficient.
static bool subgroup_test(const BSGS& H, const BSGS& G) {
    for (const Level& lev : H.chain)
        for (const Perm& gen : lev.generators)
            if (!membership_test(G, gen))
                return false;
    return true;
}

// ─── Pretty printing ─────────────────────────────────────────────────────────

static void print_perm(const Perm& p) {
    std::cout << "[";
    for (int i = 0; i < (int)p.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << p[i];
    }
    std::cout << "]";
}

static void print_bsgs(const BSGS& bsgs) {
    auto B   = bsgs.base();
    auto sgs = bsgs.sgs_per_level();

    std::cout << "Base: [";
    for (int i = 0; i < (int)B.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << B[i];
    }
    std::cout << "]\n";

    std::cout << "SGS per stabilizer level:\n";
    for (int i = 0; i < (int)sgs.size(); ++i) {
        if (i > 0) std::cout << "[fixes b[0.." << i-1 << "]]";
        std::cout << "]]  orbit-size=" << bsgs.chain[i].transversal.size() << "\n";
        for (const Perm& g : sgs[i]) {
            std::cout << "    "; print_perm(g); std::cout << "\n";
        }
    }
    std::cout << "Orbit sizes: ";
    for (const Level& lev : bsgs.chain) std::cout << lev.transversal.size() << " ";
    std::cout << "\nGroup order: " << bsgs.order() << "\n";
}

static void print_transversal(const BSGS& bsgs) {
    std::cout << "Transversals:\n";
    for (int i = 0; i < (int)bsgs.chain.size(); ++i) {
        const Level& lev = bsgs.chain[i];
        std::cout << "  Level " << i << "  (base point=" << lev.base_point
                  << ", orbit-size=" << lev.transversal.size() << "):\n";

        // Sort by orbit point for deterministic output
        std::vector<std::pair<int, const Perm*>> sorted;
        for (auto& [pt, u] : lev.transversal)
            sorted.push_back({pt, &u});
        std::sort(sorted.begin(), sorted.end());

        for (auto& [pt, u] : sorted) {
            std::cout << "    " << lev.base_point << " ^^ ";
            print_perm(*u);
            std::cout << " = " << pt << "\n";
        }
    }
}

// ─── Emscripten wrapper ───────────────────────────────────────────────────────

#ifdef __EMSCRIPTEN__

class SchreierSimsRunner {
    int n_ = 0;
    std::vector<Perm> generators_;
    BSGS bsgs_{0};

    // Solving moves (loaded separately for DFS)
    std::vector<Perm> solving_moves_;

    // Canonical ID table (populated by buildTable)
    std::unordered_map<std::string, int> canon_id_table_;
    std::vector<int> canon_id_base_;

    // Transition table (populated by buildTransitionTable)
    // transition_table_[id][mi] = canonical ID after applying solving_moves_[mi]
    std::vector<std::vector<int>> transition_table_;

    // distance_table_[id] = min solving moves to reach identity (populated by buildDistanceTable)
    std::vector<int> distance_table_;

    std::string canonKey(const Perm& perm, const std::vector<int>& base) const {
        Perm c = bsgs_.canonicalize(perm);
        std::string key;
        for (int i = 0; i < (int)base.size(); i++) {
            if (i) key += ',';
            key += std::to_string(c[base[i]]);
        }
        return key;
    }

public:
    void reset(int n) { n_ = n; generators_.clear(); bsgs_ = BSGS(n); }

    void addGenerator(const std::vector<int>& g) {
        generators_.emplace_back(g.begin(), g.end());
    }

    // Build BSGS and return JSON: {"base":[…],"orbitSizes":[…],"order":N}
    std::string run(int confidence) {
        build(confidence);
        auto B = bsgs_.base();
        std::string s = "{\"base\":[";
        for (int i = 0; i < (int)B.size(); i++) { if (i) s += ","; s += std::to_string(B[i]); }
        s += "],\"orbitSizes\":[";
        for (int i = 0; i < (int)bsgs_.chain.size(); i++) {
            if (i) s += ",";
            s += std::to_string(bsgs_.chain[i].transversal.size());
        }
        s += "],\"order\":" + std::to_string(bsgs_.order()) + "}";
        return s;
    }

    void build(int confidence) {
        bsgs_ = randomized_schreier_sims(n_, generators_, confidence);
    }

    std::vector<int> getBase() const { return bsgs_.base(); }

    std::vector<int> canonicalizePerm(const std::vector<int>& perm) const {
        Perm p(perm.begin(), perm.end());
        Perm c = bsgs_.canonicalize(p);
        return std::vector<int>(c.begin(), c.end());
    }

    // ── Solving moves ─────────────────────────────────────────────────────────

    void clearSolvingMoves() { solving_moves_.clear(); }

    // Adds m and inv(m) to solving_moves_, skipping each if already present.
    void addSolvingMove(const std::vector<int>& m) {
        Perm p(m.begin(), m.end());
        auto already = [&](const Perm& q) {
            for (const Perm& e : solving_moves_) if (e == q) return true;
            return false;
        };
        if (!already(p)) solving_moves_.push_back(p);
        Perm p_inv = inv(p);
        if (p_inv != p && !already(p_inv)) solving_moves_.push_back(p_inv);
    }

    // DFS over the solving moves, canonicalizing each state with the target
    // bsgs_.  The canonical key is the image of the solving BSGS base points
    // under canonicalize(cube).  Returns the total number of table entries.
    int buildTable(const std::vector<int>& base) {
        canon_id_table_.clear();
        canon_id_base_ = base;
        const int nMoves = (int)solving_moves_.size();
        if (nMoves == 0) return 0;

        std::vector<Perm> invs;
        invs.reserve(nMoves);
        for (const Perm& m : solving_moves_) invs.push_back(inv(m));

        Perm cube = identity(n_);
        std::vector<int> stack = {0};
        canon_id_table_[canonKey(cube, base)] = 0;

        while (!stack.empty()) {
            if (stack.back() == nMoves) {
                stack.pop_back();
                if (!stack.empty())
                    cube = compose(cube, invs[stack.back() - 1]);
                continue;
            }

            int mi = stack.back();
            stack.back()++;

            cube = compose(cube, solving_moves_[mi]);
            std::string h = canonKey(cube, base);

            if (!canon_id_table_.count(h)) {
                canon_id_table_[h] = (int)canon_id_table_.size();
                stack.push_back(0);
            } else {
                cube = compose(cube, invs[mi]);
            }
        }

        return (int)canon_id_table_.size();
    }

    // Must be called after buildTable. Fills transition_table_[id][mi] with the
    // canonical ID reached by applying solving_moves_[mi] to any state whose
    // canonical ID is id. Uses the same DFS structure as buildTable.
    void buildTransitionTable() {
        const int nMoves = (int)solving_moves_.size();
        const int tableSize = (int)canon_id_table_.size();
        transition_table_.assign(tableSize, std::vector<int>(nMoves, -1));

        if (nMoves == 0 || tableSize == 0) return;

        std::vector<Perm> invs;
        invs.reserve(nMoves);
        for (const Perm& m : solving_moves_) invs.push_back(inv(m));

        std::vector<bool> visited(tableSize, false);

        auto fillTransitions = [&](const Perm& cube, int id) {
            for (int tj = 0; tj < nMoves; tj++) {
                Perm next = compose(cube, solving_moves_[tj]);
                transition_table_[id][tj] = canon_id_table_.at(canonKey(next, canon_id_base_));
            }
        };

        Perm cube = identity(n_);
        int rootId = canon_id_table_.at(canonKey(cube, canon_id_base_));
        visited[rootId] = true;
        fillTransitions(cube, rootId);

        std::vector<int> stack = {0};

        while (!stack.empty()) {
            if (stack.back() == nMoves) {
                stack.pop_back();
                if (!stack.empty())
                    cube = compose(cube, invs[stack.back() - 1]);
                continue;
            }

            int mi = stack.back();
            stack.back()++;

            cube = compose(cube, solving_moves_[mi]);
            int id = canon_id_table_.at(canonKey(cube, canon_id_base_));

            if (!visited[id]) {
                visited[id] = true;
                fillTransitions(cube, id);
                stack.push_back(0);
            } else {
                cube = compose(cube, invs[mi]);
            }
        }
    }

    std::vector<int> getTransitionRow(int id) const {
        if (id < 0 || id >= (int)transition_table_.size()) return {};
        return transition_table_[id];
    }

    // BFS from the identity state outward using all solving_moves_ (which
    // include inverses). distance_table_[id] is the minimum number of moves
    // to reach identity from any state with that canonical ID.
    // Must be called after buildTransitionTable.
    void buildDistanceTable() {
        const int tableSize = (int)canon_id_table_.size();
        const int nMoves = (int)solving_moves_.size();
        distance_table_.assign(tableSize, -1);

        if (nMoves == 0 || tableSize == 0) return;

        int identity_id = canon_id_table_.at(canonKey(identity(n_), canon_id_base_));
        distance_table_[identity_id] = 0;

        std::queue<int> q;
        q.push(identity_id);

        while (!q.empty()) {
            int id = q.front(); q.pop();
            int d = distance_table_[id];
            for (int mi = 0; mi < nMoves; mi++) {
                int next_id = transition_table_[id][mi];
                if (distance_table_[next_id] == -1) {
                    distance_table_[next_id] = d + 1;
                    q.push(next_id);
                }
            }
        }
    }

    int getDistance(int id) const {
        if (id < 0 || id >= (int)distance_table_.size()) return -1;
        return distance_table_[id];
    }

    // Returns the sequence of solving_moves_ indices that takes state id to
    // identity, following the steepest descent in distance_table_.
    // Must be called after buildDistanceTable. Returns empty if id == identity
    // or id is out of range.
    std::vector<int> solve(int id) const {
        std::vector<int> path;
        if (id < 0 || id >= (int)distance_table_.size()) return path;
        int d = distance_table_[id];
        int currentId = id;
        while (d > 0) {
            const auto& row = transition_table_[currentId];
            for (int mi = 0; mi < (int)row.size(); mi++) {
                if (distance_table_[row[mi]] == d - 1) {
                    path.push_back(mi);
                    currentId = row[mi];
                    d--;
                    break;
                }
            }
        }
        return path;
    }

    // Returns the canonical ID of perm, or -1 if not in the table.
    int lookupCanonId(const std::vector<int>& perm) const {
        Perm p(perm.begin(), perm.end());
        auto it = canon_id_table_.find(canonKey(p, canon_id_base_));
        return (it != canon_id_table_.end()) ? it->second : -1;
    }

    int getTableSize() const { return (int)canon_id_table_.size(); }
};

// ─── OrientPermSpec ───────────────────────────────────────────────────────────
//
// Describes the combinatorial structure of an OrientPerm target group:
// pieces partitioned into equivalence classes, with per-class orientation knockdown.
// All heavy computation (orientation analysis, multinomials) is in build() — commit 3.
// Indexing (state_to_index) is in commit 4.

struct OrientPermSpec {
    int n = 0;  // total sticker domain size

    struct PieceTypeMeta {
        int base;               // sticker index of piece 0's sticker 0
        int m;                  // stickers per piece
        int count;              // pieces of this type (including fixed pieces)
        int effective_count;    // count minus fixed pieces; used for multinomial and ranking
        long long initial_multinomial;  // effective_count! / (class_sizes[0]! * ...) — seed for rank loop
    };
    std::vector<PieceTypeMeta> types;

    struct Class {
        int type_idx;            // index into types[]
        int orientation_mod;     // knockdown factor d
        std::vector<int> pieces; // piece indices within the type, sorted
    };
    std::vector<Class> classes;

    std::vector<std::vector<int>> piece_class;  // [type_idx][piece_within_type] → class idx

    // orient_step[t] = m_t / orbit_size_t, computed by joint BSGS on virtual permutations in build().
    // orient_step[t] == 1  → no constraint; last tracked piece is fully free.
    // orient_step[t] == m_t → sum is always 0; last tracked piece is forced (contributes nothing to index).
    std::vector<int> orient_step;

    // is_parity_forced[t]: true iff the permutation parity of type t is forced,
    // given that all previous types' parities are already determined (commit 3c).
    std::vector<bool> is_parity_forced;

    long long perm_space   = 0;
    long long orient_space = 0;
    long long total_states = 0;

    // Precomputed in build() for state_to_index hot path.
    // type_class_global[t][lci] = global class index of the lci-th class of type t.
    std::vector<std::vector<int>> type_class_global;
    // ci_local[global_ci] = local class index of that class within its type.
    std::vector<int> ci_local;

    // Commit 3a: fixed piece detection (§2k).
    // Commit 3b: orientation constraint analysis (§2f) + orient_space.
    // Commit 3c: parity constraint analysis (§2l) + initial_multinomial, perm_space, total_states.
    void build(const std::vector<Class>& classes_in,
               const std::vector<PieceTypeMeta>& types_in,
               int n_in,
               const std::vector<Perm>& generators) {
        n = n_in;
        types = types_in;
        classes = classes_in;

        int n_types = (int)types.size();

        // Build initial piece_class from classes_in
        piece_class.assign(n_types, {});
        for (int t = 0; t < n_types; t++)
            piece_class[t].assign(types[t].count, -1);
        for (int ci = 0; ci < (int)classes.size(); ci++)
            for (int p : classes[ci].pieces)
                piece_class[classes[ci].type_idx][p] = ci;

        // ── §2k: Fixed piece detection ──────────────────────────────────────

        // Extract compact PiecePerms for all solving generators (one per type per generator)
        std::vector<std::vector<PiecePerm>> compact_gens(
            generators.size(), std::vector<PiecePerm>(n_types));
        for (int gi = 0; gi < (int)generators.size(); gi++)
            for (int t = 0; t < n_types; t++)
                compact_gens[gi][t] = extract_piece(
                    generators[gi], types[t].base, types[t].m, types[t].count);

        // Position p in type t is fixed iff every generator leaves it in place with zero twist.
        std::vector<std::vector<bool>> is_fixed(n_types);
        for (int t = 0; t < n_types; t++) {
            int n_t = types[t].count, base = types[t].base, m = types[t].m;
            is_fixed[t].assign(n_t, true);
            for (int p = 0; p < n_t && !generators.empty(); p++)
                for (int gi = 0; gi < (int)generators.size(); gi++)
                    if (compact_gens[gi][t][p] != base + m * p) {
                        is_fixed[t][p] = false;
                        break;
                    }
        }

        // Filter each class: remove fixed pieces; drop the class entirely if it becomes empty.
        {
            std::vector<Class> filtered;
            for (auto& cls : classes) {
                std::vector<int> kept;
                for (int p : cls.pieces)
                    if (!is_fixed[cls.type_idx][p])
                        kept.push_back(p);
                if (!kept.empty()) {
                    cls.pieces = std::move(kept);
                    filtered.push_back(std::move(cls));
                }
            }
            classes = std::move(filtered);
        }

        // Rebuild piece_class with updated class indices after filtering
        for (int t = 0; t < n_types; t++)
            std::fill(piece_class[t].begin(), piece_class[t].end(), -1);
        for (int ci = 0; ci < (int)classes.size(); ci++)
            for (int p : classes[ci].pieces)
                piece_class[classes[ci].type_idx][p] = ci;

        // Set effective_count = non-fixed piece count for each type
        for (int t = 0; t < n_types; t++) {
            int eff = 0;
            for (int p = 0; p < types[t].count; p++)
                if (!is_fixed[t][p]) eff++;
            types[t].effective_count = eff;
        }

        // ── §2f: Orientation constraint analysis ──────────────────────────────

        // Virtual domain: type t occupies [type_offset[t], type_offset[t+1])
        std::vector<int> type_offset(n_types + 1, 0);
        for (int t = 0; t < n_types; t++)
            type_offset[t + 1] = type_offset[t] + types[t].m;
        const int total_virtual = type_offset[n_types];

        std::vector<Perm> virtual_perms;

        // One virtual perm per solving generator (§2f Step 1)
        for (int gi = 0; gi < (int)generators.size(); gi++) {
            std::vector<int> deltas(n_types, 0);
            for (int t = 0; t < n_types; t++) {
                int base = types[t].base, m = types[t].m, n_t = types[t].count;
                int d = 0;
                for (int p = 0; p < n_t; p++)
                    d += (compact_gens[gi][t][p] - base) % m;
                deltas[t] = ((d % m) + m) % m;
            }
            Perm vp(total_virtual);
            std::iota(vp.begin(), vp.end(), 0);
            bool nontrivial = false;
            for (int t = 0; t < n_types; t++) {
                if (deltas[t] == 0) continue;
                nontrivial = true;
                int off = type_offset[t], m = types[t].m, d = deltas[t];
                for (int i = 0; i < m; i++)
                    vp[off + i] = off + (i + d) % m;
            }
            if (nontrivial) virtual_perms.push_back(std::move(vp));
        }

        // One virtual perm per knockdown delta from each class (§2f Step 2)
        for (int ci = 0; ci < (int)classes.size(); ci++) {
            int t = classes[ci].type_idx, d = classes[ci].orientation_mod;
            int m = types[t].m;
            if (d <= 0 || d % m == 0) continue;
            int off = type_offset[t];
            Perm vp(total_virtual);
            std::iota(vp.begin(), vp.end(), 0);
            for (int i = 0; i < m; i++)
                vp[off + i] = off + (i + d) % m;
            virtual_perms.push_back(std::move(vp));
        }

        // §2f Steps 3–4: joint BSGS on virtual permutation group
        orient_step.assign(n_types, 0);
        if (total_virtual > 0 && !virtual_perms.empty()) {
            BSGS vbsgs = randomized_schreier_sims(total_virtual, virtual_perms, 100);
            // §2f Step 5: read orbit sizes → orient_step
            for (const auto& lev : vbsgs.chain) {
                int bp = lev.base_point;
                for (int t = 0; t < n_types; t++) {
                    if (bp >= type_offset[t] && bp < type_offset[t + 1]) {
                        int orbit_size = (int)lev.transversal.size();
                        orient_step[t] = types[t].m / orbit_size;
                        break;
                    }
                }
            }
        }
        // Types not in any chain level: orbit_size = 1 → sum is fixed → orient_step = m
        for (int t = 0; t < n_types; t++)
            if (orient_step[t] == 0)
                orient_step[t] = types[t].m;

        // §2f Step 6: compute orient_space
        orient_space = 1;
        for (int t = 0; t < n_types; t++) {
            int n_t = types[t].count;
            // Carrier: last non-fixed position with d_p > 1
            int carrier = -1;
            for (int p = n_t - 1; p >= 0; p--) {
                if (is_fixed[t][p]) continue;
                int ci = piece_class[t][p];
                if (ci >= 0 && classes[ci].orientation_mod > 1) { carrier = p; break; }
            }
            if (carrier == -1) continue;
            int orbit_size_t = types[t].m / orient_step[t];
            long long type_contrib = orbit_size_t;
            for (int p = 0; p < n_t; p++) {
                if (p == carrier || is_fixed[t][p]) continue;
                int ci = piece_class[t][p];
                if (ci >= 0 && classes[ci].orientation_mod > 1)
                    type_contrib *= classes[ci].orientation_mod;
            }
            orient_space *= type_contrib;
        }

        // ── §2l: Parity constraint analysis ───────────────────────────────────

        // A type is restricted if any of its classes (after fixed-piece filtering) has > 1 piece.
        std::vector<bool> restricted(n_types, false);
        for (const auto& cls : classes)
            if ((int)cls.pieces.size() > 1)
                restricted[cls.type_idx] = true;

        // Build non-fixed position list and re-index map per type.
        std::vector<std::vector<int>> non_fixed_pos(n_types);
        std::vector<std::vector<int>> reindex(n_types);
        for (int t = 0; t < n_types; t++) {
            reindex[t].assign(types[t].count, -1);
            for (int p = 0; p < types[t].count; p++) {
                if (!is_fixed[t][p]) {
                    reindex[t][p] = (int)non_fixed_pos[t].size();
                    non_fixed_pos[t].push_back(p);
                }
            }
        }

        // Virtual parity domain: 2 pieces per type (Z/2Z).
        const int parity_domain = 2 * n_types;
        std::vector<Perm> parity_perms;

        for (int gi = 0; gi < (int)generators.size(); gi++) {
            std::vector<int> parity_delta(n_types, 0);
            for (int t = 0; t < n_types; t++) {
                if (restricted[t]) continue;
                int base = types[t].base, m = types[t].m;
                int eff = types[t].effective_count;
                if (eff == 0) continue;
                // Build re-indexed position permutation
                std::vector<int> pos_perm(eff);
                for (int i = 0; i < eff; i++) {
                    int p = non_fixed_pos[t][i];
                    int dest_orig = (compact_gens[gi][t][p] - base) / m;
                    pos_perm[i] = reindex[t][dest_orig];
                }
                // Parity from cycle structure: parity = (eff - num_cycles) % 2
                std::vector<bool> visited(eff, false);
                int num_cycles = 0;
                for (int i = 0; i < eff; i++) {
                    if (!visited[i]) {
                        num_cycles++;
                        for (int j = i; !visited[j]; j = pos_perm[j])
                            visited[j] = true;
                    }
                }
                parity_delta[t] = (eff - num_cycles) % 2;
            }
            Perm vp(parity_domain);
            std::iota(vp.begin(), vp.end(), 0);
            bool nontrivial = false;
            for (int t = 0; t < n_types; t++) {
                if (parity_delta[t] == 0) continue;
                nontrivial = true;
                vp[2 * t]     = 2 * t + 1;
                vp[2 * t + 1] = 2 * t;
            }
            if (nontrivial) parity_perms.push_back(std::move(vp));
        }

        // Start assuming all unrestricted types are parity-forced (orbit_size = 1).
        // A chain level with orbit_size == 2 for type t means parity is free for t.
        is_parity_forced.assign(n_types, false);
        for (int t = 0; t < n_types; t++)
            is_parity_forced[t] = !restricted[t];
        if (parity_domain > 0 && !parity_perms.empty()) {
            BSGS pbsgs = randomized_schreier_sims(parity_domain, parity_perms, 100);
            for (const auto& lev : pbsgs.chain) {
                int t = lev.base_point / 2;
                if (t < n_types && (int)lev.transversal.size() == 2)
                    is_parity_forced[t] = false;
            }
        }

        // ── §2g: initial_multinomial, perm_space, total_states ────────────────

        auto factorial = [](int n) -> long long {
            long long r = 1;
            for (int i = 2; i <= n; i++) r *= i;
            return r;
        };

        perm_space = 1;
        for (int t = 0; t < n_types; t++) {
            int eff = types[t].effective_count;
            long long multi = factorial(eff);
            // Divide by each class's size factorial
            for (const auto& cls : classes) {
                if (cls.type_idx != t) continue;
                multi /= factorial((int)cls.pieces.size());
            }
            types[t].initial_multinomial = multi;
            perm_space *= is_parity_forced[t] ? multi / 2 : multi;
        }

        total_states = perm_space * orient_space;

        // Precompute per-type class index tables
        type_class_global.assign(n_types, {});
        for (int ci = 0; ci < (int)classes.size(); ci++)
            type_class_global[classes[ci].type_idx].push_back(ci);
        ci_local.resize(classes.size());
        for (int t = 0; t < n_types; t++)
            for (int lci = 0; lci < (int)type_class_global[t].size(); lci++)
                ci_local[type_class_global[t][lci]] = lci;
    }

private:
    // §2e shared implementation; get_val(t, src) returns the PiecePerm entry for type t, source pos src.
    template<typename GetVal>
    long long state_to_index_impl(GetVal get_val) const {
        const int n_types = (int)types.size();

        // Step 1: scatter — for each non-fixed source, record its destination and orientation.
        std::vector<std::vector<int>> piece_at_p(n_types), orient_at_p(n_types);
        for (int t = 0; t < n_types; t++) {
            int n_t = types[t].count, base = types[t].base, m = types[t].m;
            piece_at_p[t].assign(n_t, -1);
            orient_at_p[t].assign(n_t, 0);
            for (int src = 0; src < n_t; src++) {
                if (piece_class[t][src] == -1) continue;  // fixed
                int val = get_val(t, src) - base;
                int dest = val / m, twist = val % m;
                piece_at_p[t][dest] = src;
                orient_at_p[t][dest] = (m - twist) % m;
            }
        }

        // Step 2: permutation rank (incremental ratio method)
        long long perm_idx = 0, perm_stride = 1;
        for (int t = 0; t < n_types; t++) {
            int n_t = types[t].count, eff = types[t].effective_count;
            if (eff == 0) continue;
            const auto& tcg = type_class_global[t];
            int n_lc = (int)tcg.size();

            std::vector<long long> remaining(n_lc);
            for (int lci = 0; lci < n_lc; lci++)
                remaining[lci] = (long long)classes[tcg[lci]].pieces.size();

            long long rank = 0, current = types[t].initial_multinomial;
            int pos = 0;
            for (int p = 0; p < n_t && pos < eff; p++) {
                if (piece_class[t][p] == -1) continue;  // fixed destination, skip
                int piece = piece_at_p[t][p];
                int label = ci_local[piece_class[t][piece]];
                long long n_rem = eff - pos;
                for (int lci = 0; lci < label; lci++)
                    if (remaining[lci] > 0)
                        rank += current * remaining[lci] / n_rem;
                current = current * remaining[label] / n_rem;
                remaining[label]--;
                pos++;
            }

            long long index_t = is_parity_forced[t] ? rank / 2 : rank;
            perm_idx += perm_stride * index_t;
            perm_stride *= is_parity_forced[t] ? types[t].initial_multinomial / 2
                                                : types[t].initial_multinomial;
        }

        // Step 3: orientation index (mixed-radix, carrier = last position with d_p > 1)
        long long orient_idx = 0, orient_stride = 1;
        for (int t = 0; t < n_types; t++) {
            int n_t = types[t].count;
            int orbit_size_t = types[t].m / orient_step[t];

            bool need_parity = true;
            long long orient_idx_t = 0, stride = 1;
            for (int p = 0; p < n_t; p++) {
                int piece = piece_at_p[t][p];
                if (piece == -1) continue;
                int ci = piece_class[t][piece];
                int d_p = classes[ci].orientation_mod;
                if (d_p == 1) continue;
                if (need_parity) {
                    orient_idx_t += (orient_at_p[t][p] / orient_step[t]) * stride;
                    stride *= orbit_size_t;
                    need_parity = false;
                } else {
                    orient_idx_t += orient_at_p[t][p] * stride;
                    stride *= d_p;
                }
            }
            orient_idx += orient_stride * orient_idx_t;
            orient_stride *= stride;
        }

        return perm_idx + perm_space * orient_idx;
    }

public:
    long long state_to_index(const Perm& S) const {
        return state_to_index_impl([&](int t, int src) {
            return S[types[t].base + types[t].m * src];
        });
    }
    long long state_to_index_compact(const std::vector<PiecePerm>& compact) const {
        return state_to_index_impl([&](int t, int src) {
            return compact[t][src];
        });
    }
};

// ─── Move-sequence pruning ────────────────────────────────────────────────────

// Tracks the trailing move streak in the current IDA* path.
struct MoveStreak {
    int mi    =  0;  // index of last move
    int count =  0;  // consecutive times mi appears at the path tail
};

// Precomputes, for every (prev_move, next_move) pair, the maximum consecutive
// count of prev_move that still permits next_move to follow.
// INT_MAX = always allowed; 0 = never allowed.
struct MovePruner {
    std::vector<std::vector<int>> can_follow;
    int nMoves = 0;

    static int perm_order(const Perm& p, const int limit) {
        int n = (int)p.size();
        std::vector<bool> vis(n, false);
        int ord = 1;
        for (int i = 0; i < n; ++i) {
            if (vis[i]) continue;
            int cyc = 0, j = i;
            while (!vis[j]) { vis[j] = true; j = p[j]; ++cyc; }
            ord = std::lcm(ord, cyc);
            if (ord > limit) return INT_MAX;
        }
        return ord;
    }

    void build(const std::vector<Perm>& moves) {
        nMoves = (int)moves.size();
        can_follow.assign(nMoves, std::vector<int>(nMoves, INT_MAX));

        // Inverse index: inv_idx[i] = j where compose(moves[i], moves[j]) == id
        std::vector<int> inv_idx(nMoves, -1);
        for (int i = 0; i < nMoves; ++i) {
            if (inv_idx[i] >= 0) continue;
            for (int j = i; j < nMoves; ++j) {
                if (is_identity(compose(moves[i], moves[j]))) {
                    inv_idx[i] = j; inv_idx[j] = i; break;
                }
            }
        }

        // Perm → index lookup for 2-move collapse detection
        auto perm_key = [](const Perm& p) {
            std::string k; k.reserve(p.size() * 4);
            for (int x : p) { k += std::to_string(x); k += ','; }
            return k;
        };
        std::unordered_map<std::string, int> perm_index;
        for (int i = 0; i < nMoves; ++i) perm_index[perm_key(moves[i])] = i;

        for (int i = 0; i < nMoves; ++i) {
            for (int j = 0; j < nMoves; ++j) {
                Perm ij = compose(moves[i], moves[j]);

                // Rule 1b: 2-move combo equals identity or another move in the set
                if (is_identity(ij) || perm_index.count(perm_key(ij))) {
                    can_follow[i][j] = 0; continue;
                }

                if (i != j) {
                    // Rule 1a: commuting pair — enforce lower-index-first ordering
                    if (ij == compose(moves[j], moves[i]) && j < i) {
                        can_follow[i][j] = 0; continue;
                    }
                } else {
                    // Rule 2: cap consecutive uses of low-order moves.
                    // For move M with order n and inverse at inv_idx[i]:
                    //   forward  (inv_i > i): max streak = floor(n/2)
                    //   inverse  (inv_i < i): max streak = floor((n-1)/2)
                    //   self-inv (inv_i == i): max streak = 1
                    int ord = perm_order(moves[i], 16);
                    if (ord < INT_MAX) {
                        const int inv_i = inv_idx[i];
                        int cap = (inv_i < 0 || inv_i == i) ? 0 // dead code, reached by (1b)
                                : (inv_i > i)               ? ord / 2 - 1
                                :                             (ord - 1) / 2 - 1;
                        can_follow[i][i] = cap;
                    }
                }
            }
        }
    }

    // Returns true if next_mi should be pruned given the current path tail.
    bool prune(int prev_mi, int prev_count, int next_mi) const {
        return prev_count > can_follow[prev_mi][next_mi];
    }
};

// ─── MultiTargetSolver ────────────────────────────────────────────────────────
//
// Manages t target groups (each with its own BSGS + tables) and one set of
// solving moves.  buildTables() runs the full pipeline for every group.
// solve() runs IDA* with h = max distance across all groups.

class MultiTargetSolver {
    int n_ = 0;

    struct TargetGroup {
        enum GroupKind { GENERAL, ORIENTPERM };
        GroupKind kind = GENERAL;
        OrientPermSpec op_spec;

        // Temporary accumulator for addOrientPermClass calls; consumed by buildOrientPermGroup().
        struct RawOPClass { std::vector<int> sticker_bases; int m; int orientation_mod; std::string type_name; };
        std::vector<RawOPClass> raw_classes;

        std::vector<Perm> generators;
        BSGS bsgs{0};
        std::unordered_map<Hash128, int, Hash128Hasher> canon_id_table;
        std::vector<int> transition_table; // flat: [id * nMoves + mi]
        int identity_id = -1;             // set during buildTables(), used to seed product tables
        std::vector<std::vector<Hash128>> zobrist; // [pos][val]

        Hash128 hashPerm(const Perm& perm, const std::vector<int>& base) const {
            Perm c = bsgs.canonicalize(perm);
            uint64_t lo = 0, hi = 0;
            for (int i = 0; i < (int)base.size(); ++i) {
                lo ^= zobrist[i][c[base[i]]].first;
                hi ^= zobrist[i][c[base[i]]].second;
            }
            return {lo, hi};
        }
    };

    std::vector<TargetGroup> groups_;

    struct ProductDistanceTable {
        std::vector<int>       component_ids;  // indices into groups_
        std::vector<long long> strides;        // stride[j] = ∏ sizes[j+1..]
        long long              total_states = 0;
        Mod3Table              distance_table;
        long long              identity_id = -1;
    };
    std::vector<ProductDistanceTable> product_tables_;

    std::vector<Perm> solving_generators_;
    BSGS solving_bsgs_{0};
    std::vector<Perm> solving_moves_;
    std::vector<int> base_;
    std::vector<int> solution_;
    emscripten::val js_callback_ = emscripten::val::undefined();
    emscripten::val depth_callback_ = emscripten::val::undefined();
    bool found_any_solution_ = false;
    MovePruner pruner_;

    // kMod3Diff[p][c]: exact change in distance when parent mod-3 is p and child mod-3 is c.
    // One move changes distance by at most 1, so exactly one of {-1, 0, +1} matches.
    static constexpr int kMod3Diff[3][3] = {
        {  0, +1, -1 },
        { -1,  0, +1 },
        { +1, -1,  0 },
    };

    // Returns the minimum f that exceeded threshold, or INT_MAX if no branch did.
    // ss[g]    = per-group state indices at depth g (for transition lookup)
    // pd_ss[g] = per-product-table product state indices at depth g
    // hs[g]    = per-product-table h values at depth g
    int idaDfs(std::vector<std::vector<int>>&       ss,
               std::vector<std::vector<long long>>& pd_ss,
               std::vector<std::vector<int>>&       hs,
               int g, int threshold, MoveStreak tail) {
        const std::vector<int>& state  = ss[g];
        const std::vector<int>& h_vals = hs[g];

        int h = *std::max_element(h_vals.begin(), h_vals.end());
        if (h == 0) {
            if (g == threshold) {
                found_any_solution_ = true;
                if (!js_callback_.isUndefined() && !js_callback_.isNull()) {
                    emscripten::val arr = emscripten::val::array();
                    for (int mi : solution_) arr.call<void>("push", mi);
                    js_callback_(arr);
                }
            }
            return INT_MAX;
        }
        int f = g + h;
        if (f > threshold) return f;

        const int nMoves = (int)solving_moves_.size();
        const int t  = (int)groups_.size();
        const int np = (int)product_tables_.size();
        int minExceeded = INT_MAX;

        for (int mi = 0; mi < nMoves; mi++) {
            if (pruner_.prune(tail.mi, tail.count, mi)) continue;
            for (int i = 0; i < t; i++)
                ss[g+1][i] = groups_[i].transition_table[state[i] * nMoves + mi];
            for (int p = 0; p < np; p++) {
                long long new_pd = 0;
                for (int j = 0; j < (int)product_tables_[p].component_ids.size(); j++) {
                    int ci = product_tables_[p].component_ids[j];
                    new_pd += (long long)ss[g+1][ci] * product_tables_[p].strides[j];
                }
                pd_ss[g+1][p] = new_pd;
                hs[g+1][p] = h_vals[p] + kMod3Diff[h_vals[p] % 3][product_tables_[p].distance_table.get(new_pd)];
            }
            solution_.push_back(mi);
            MoveStreak new_tail{mi, mi == tail.mi ? tail.count + 1 : 1};
            int result = idaDfs(ss, pd_ss, hs, g + 1, threshold, new_tail);
            solution_.pop_back();
            if (result < minExceeded) minExceeded = result;
        }
        return minExceeded;
    }

    // Greedy walk on a product distance table, decoding component states via strides.
    int computeExactH(int pd_idx, long long s) const {
        const auto& pdt = product_tables_[pd_idx];
        const int nMoves = (int)solving_moves_.size();
        const int nc = (int)pdt.component_ids.size();
        int distance = 0;
        while (s != pdt.identity_id) {
            int d1m3 = pdt.distance_table.get(s);
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

    // Sweep algorithm: fills product distance tables using O(max_depth * N) passes,
    // zero extra memory beyond the distance table itself.
    void buildProductDistanceTables() {
        const int nMoves = (int)solving_moves_.size();
        for (auto& pdt : product_tables_) {
            const long long N = pdt.total_states;
            if (N <= 0) continue;
            const int nc = (int)pdt.component_ids.size();

            // Component sizes: sizes[0] = N / strides[0], sizes[j] = strides[j-1] / strides[j]
            std::vector<int> comp_sizes(nc);
            comp_sizes[0] = (int)(N / pdt.strides[0]);
            for (int j = 1; j < nc; j++)
                comp_sizes[j] = (int)(pdt.strides[j-1] / pdt.strides[j]);

            pdt.distance_table.assign(N, 3);
            pdt.distance_table.set(pdt.identity_id, 0);

            int depth_mod3 = 0;
            std::vector<int> comp(nc, 0);  // odometer: comp[j] = current state of component j

            bool any_new = true;
            while (any_new) {
                any_new = false;
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
                                any_new = true;
                            }
                        }
                    }
                    // Increment odometer (amortized O(1))
                    for (int j = nc - 1; j >= 0; j--) {
                        if (++comp[j] < comp_sizes[j]) break;
                        comp[j] = 0;
                    }
                }
                depth_mod3 = (depth_mod3 + 1) % 3;
            }
        }
    }

public:
    void reset(int n) {
        n_ = n;
        groups_.clear();
        product_tables_.clear();
        solving_generators_.clear();
        solving_bsgs_ = BSGS(n);
        solving_moves_.clear();
        base_.clear();
    }

    void addBasePoint(int pt) { base_.push_back(pt); }

    // ── Target groups ─────────────────────────────────────────────────────────

    void beginTargetGroup() {
        groups_.emplace_back();
        groups_.back().bsgs = BSGS(n_);
    }

    void addTargetGenerator(const std::vector<int>& g) {
        groups_.back().generators.emplace_back(g.begin(), g.end());
    }

    void buildTargetGroup(int confidence) {
        auto& grp = groups_.back();
        grp.bsgs = randomized_schreier_sims(n_, grp.generators, confidence);
    }

    // ── OrientPerm groups ─────────────────────────────────────────────────────

    void beginOrientPermGroup() {
        groups_.emplace_back();
        groups_.back().kind = TargetGroup::ORIENTPERM;
    }

    void addOrientPermClass(std::vector<int> sticker_bases, int m, int orientation_mod,
                            std::string type_name) {
        groups_.back().raw_classes.push_back(
            {std::move(sticker_bases), m, orientation_mod, std::move(type_name)});
    }

    // Must be called after buildSolvingBSGS (i.e., after all addSolvingGenerator calls),
    // because build() uses solving_generators_ for fixed-piece detection and orientation analysis.
    void buildOrientPermGroup() {
        auto& grp = groups_.back();

        // ── Step 1: determine types, grouped by (type_name, m) ───────────────
        // Using (type_name + "|" + m) as hash key correctly distinguishes two piece
        // types that share the same m value even if their sticker bases are adjacent.
        struct TypeInfo { std::string type_name; int m, base, count; };
        std::vector<TypeInfo> type_infos;
        std::unordered_map<std::string, int> type_key_idx;

        for (const auto& rc : grp.raw_classes) {
            if (rc.sticker_bases.empty()) continue;
            const std::string key = rc.type_name + "|" + std::to_string(rc.m);
            if (!type_key_idx.count(key)) {
                type_key_idx[key] = (int)type_infos.size();
                type_infos.push_back({rc.type_name, rc.m, INT_MAX, 0});
            }
            TypeInfo& ti = type_infos[type_key_idx[key]];
            for (int sb : rc.sticker_bases) {
                ti.base = std::min(ti.base, sb);
                ti.count++;
            }
        }
        // Sort by base for consistent puzzle-layout order
        std::sort(type_infos.begin(), type_infos.end(),
                  [](const TypeInfo& a, const TypeInfo& b) { return a.base < b.base; });
        // Rebuild key→index after sort
        type_key_idx.clear();
        for (int t = 0; t < (int)type_infos.size(); t++)
            type_key_idx[type_infos[t].type_name + "|" + std::to_string(type_infos[t].m)] = t;

        std::vector<OrientPermSpec::PieceTypeMeta> types;
        for (const auto& ti : type_infos)
            types.push_back({ti.base, ti.m, ti.count, 0, 0LL});

        // ── Step 2: build OrientPermSpec::Class list ──────────────────────────
        std::vector<OrientPermSpec::Class> classes;
        for (const auto& rc : grp.raw_classes) {
            if (rc.sticker_bases.empty()) continue;
            const std::string key = rc.type_name + "|" + std::to_string(rc.m);
            int t = type_key_idx.at(key);
            OrientPermSpec::Class cls;
            cls.type_idx = t;
            cls.orientation_mod = rc.orientation_mod;
            for (int sb : rc.sticker_bases)
                cls.pieces.push_back((sb - types[t].base) / rc.m);
            std::sort(cls.pieces.begin(), cls.pieces.end());
            classes.push_back(std::move(cls));
        }

        grp.raw_classes.clear();
        grp.op_spec.build(classes, types, n_, solving_generators_);
    }

    // ── Product distance table specification ──────────────────────────────────
    // Call beginProductDistanceTable() + addProductTableComponent() for each
    // desired product table BEFORE buildTables().  If none are added, buildTables()
    // defaults to one singleton product per group (backward-compatible).

    void beginProductDistanceTable() {
        product_tables_.emplace_back();
    }

    void addProductTableComponent(int grp_idx) {
        product_tables_.back().component_ids.push_back(grp_idx);
    }

    // ── Solving moves ─────────────────────────────────────────────────────────

    void addSolvingGenerator(const std::vector<int>& g) {
        solving_generators_.emplace_back(g.begin(), g.end());
    }

    void buildSolvingBSGS(int confidence) {
        solving_bsgs_ = randomized_schreier_sims(n_, solving_generators_, confidence);
    }

    void clearSolvingMoves() { solving_moves_.clear(); }

    void addSolvingMove(const std::vector<int>& m) {
        Perm p(m.begin(), m.end());
        auto already = [&](const Perm& q) {
            for (const Perm& e : solving_moves_) if (e == q) return true;
            return false;
        };
        if (!already(p)) solving_moves_.push_back(p);
        Perm p_inv = inv(p);
        if (p_inv != p && !already(p_inv)) solving_moves_.push_back(p_inv);
    }

    // ── Table building ────────────────────────────────────────────────────────

    // Runs the full 3-phase pipeline (canon IDs → transitions → BFS distances)
    // for every target group. Must be called after buildSolvingBSGS.
    void buildTables() {
        const int nMoves = (int)solving_moves_.size();
        if (nMoves == 0 || groups_.empty()) return;

        pruner_.build(solving_moves_);

        const std::vector<int>& base = base_;
        const auto zobrist = makeZobrist((int)base.size(), n_);

        std::vector<Perm> invs;
        invs.reserve(nMoves);
        for (const Perm& mv : solving_moves_) invs.push_back(inv(mv));

        for (auto& grp : groups_) {
            if (grp.kind == TargetGroup::ORIENTPERM) {
                const auto& spec = grp.op_spec;
                const long long total = spec.total_states;
                if (total <= 0) continue;
                const int n_types = (int)spec.types.size();

                // Pre-extract compact reps of all solving moves
                std::vector<std::vector<PiecePerm>> compact_move(
                    nMoves, std::vector<PiecePerm>(n_types));
                for (int mi = 0; mi < nMoves; mi++)
                    for (int t = 0; t < n_types; t++)
                        compact_move[mi][t] = extract_piece(
                            solving_moves_[mi],
                            spec.types[t].base, spec.types[t].m, spec.types[t].count);

                // Identity compact perm (piece p at position p, zero twist)
                std::vector<PiecePerm> id_compact(n_types);
                for (int t = 0; t < n_types; t++) {
                    int n_t = spec.types[t].count, base = spec.types[t].base, m = spec.types[t].m;
                    id_compact[t].resize(n_t);
                    for (int p = 0; p < n_t; p++) id_compact[t][p] = base + m * p;
                }

                long long id0 = spec.state_to_index_compact(id_compact);
                grp.identity_id = (int)id0;
                grp.transition_table.assign((int)total * nMoves, -1);

                // DFS: enumerate reachable states and fill transition_table
                std::vector<bool> visited((int)total, false);
                visited[(int)id0] = true;

                struct Frame { std::vector<PiecePerm> perm; long long id; int mi; };
                std::vector<Frame> dfs;
                dfs.push_back({id_compact, id0, 0});

                while (!dfs.empty()) {
                    Frame& fr = dfs.back();
                    if (fr.mi == nMoves) { dfs.pop_back(); continue; }
                    int cur_mi = fr.mi++;

                    std::vector<PiecePerm> next(n_types);
                    for (int t = 0; t < n_types; t++)
                        next[t] = compose_piece(fr.perm[t], compact_move[cur_mi][t],
                                                spec.types[t].base, spec.types[t].m);
                    long long nid = spec.state_to_index_compact(next);
                    grp.transition_table[(int)fr.id * nMoves + cur_mi] = (int)nid;

                    if (!visited[(int)nid]) {
                        visited[(int)nid] = true;
                        dfs.push_back({std::move(next), nid, 0});
                    }
                }
                continue;
            }
            grp.canon_id_table.clear();
            grp.zobrist = zobrist;

            // Phase 1: build canon_id_table
            Perm cube = identity(n_);
            std::vector<int> stack = {0};
            grp.canon_id_table[grp.hashPerm(cube, base)] = 0;

            while (!stack.empty()) {
                if (stack.back() == nMoves) {
                    stack.pop_back();
                    if (!stack.empty()) cube = compose(cube, invs[stack.back() - 1]);
                    continue;
                }
                int mi = stack.back(); stack.back()++;
                cube = compose(cube, solving_moves_[mi]);
                Hash128 h = grp.hashPerm(cube, base);
                if (!grp.canon_id_table.count(h)) {
                    grp.canon_id_table[h] = (int)grp.canon_id_table.size();
                    stack.push_back(0);
                } else {
                    cube = compose(cube, invs[mi]);
                }
            }

            const int tableSize = (int)grp.canon_id_table.size();
            grp.identity_id = grp.canon_id_table.at(grp.hashPerm(identity(n_), base));
            grp.transition_table.assign(tableSize * nMoves, -1);

            // Phase 2: build transition_table
            std::vector<bool> visited(tableSize, false);
            cube = identity(n_);
            visited[grp.identity_id] = true;

            auto fillTransitions = [&](const Perm& c, int id) {
                for (int mi = 0; mi < nMoves; mi++) {
                    Perm next = compose(c, solving_moves_[mi]);
                    grp.transition_table[id * nMoves + mi] = grp.canon_id_table.at(grp.hashPerm(next, base));
                }
            };
            fillTransitions(cube, grp.identity_id);

            stack = {0};
            while (!stack.empty()) {
                if (stack.back() == nMoves) {
                    stack.pop_back();
                    if (!stack.empty()) cube = compose(cube, invs[stack.back() - 1]);
                    continue;
                }
                int mi = stack.back(); stack.back()++;
                cube = compose(cube, solving_moves_[mi]);
                int id = grp.canon_id_table.at(grp.hashPerm(cube, base));
                if (!visited[id]) {
                    visited[id] = true;
                    fillTransitions(cube, id);
                    stack.push_back(0);
                } else {
                    cube = compose(cube, invs[mi]);
                }
            }

        }

        // ── Build product distance tables ─────────────────────────────────────

        // Default: one singleton product table per group (backward-compatible)
        if (product_tables_.empty()) {
            for (int i = 0; i < (int)groups_.size(); i++) {
                ProductDistanceTable pdt;
                pdt.component_ids = {i};
                pdt.strides       = {1LL};
                long long sz = (groups_[i].kind == TargetGroup::ORIENTPERM)
                             ? groups_[i].op_spec.total_states
                             : (long long)groups_[i].canon_id_table.size();
                pdt.total_states  = sz;
                pdt.identity_id   = (long long)groups_[i].identity_id;
                product_tables_.push_back(std::move(pdt));
            }
        } else {
            for (auto& pdt : product_tables_) {
                const int nc = (int)pdt.component_ids.size();
                std::vector<long long> sizes(nc);
                for (int j = 0; j < nc; j++) {
                    int gid = pdt.component_ids[j];
                    sizes[j] = (groups_[gid].kind == TargetGroup::ORIENTPERM)
                             ? groups_[gid].op_spec.total_states
                             : (long long)groups_[gid].canon_id_table.size();
                }
                pdt.strides.resize(nc);
                pdt.strides[nc - 1] = 1LL;
                for (int j = nc - 2; j >= 0; j--)
                    pdt.strides[j] = pdt.strides[j + 1] * sizes[j + 1];
                pdt.total_states = pdt.strides[0] * sizes[0];
                pdt.identity_id  = 0;
                for (int j = 0; j < nc; j++)
                    pdt.identity_id += (long long)groups_[pdt.component_ids[j]].identity_id * pdt.strides[j];
            }
        }

        buildProductDistanceTables();
    }

    // ── Query ─────────────────────────────────────────────────────────────────

    int getNumGroups() const { return (int)groups_.size(); }

    int getNumProductTables() const { return (int)product_tables_.size(); }

    long long getProductTableSize(int p) const {
        if (p < 0 || p >= (int)product_tables_.size()) return 0;
        return product_tables_[p].total_states;
    }

    int getGroupTableSize(int g) const {
        if (g < 0 || g >= (int)groups_.size()) return 0;
        const auto& grp = groups_[g];
        return grp.kind == TargetGroup::ORIENTPERM
            ? (int)grp.op_spec.total_states
            : (int)grp.canon_id_table.size();
    }

    std::vector<int> getSolvingOrbitSizes() const {
        std::vector<int> sizes;
        for (const Level& lev : solving_bsgs_.chain)
            sizes.push_back((int)lev.transversal.size());
        return sizes;
    }

    std::vector<int> getTargetGroupOrbitSizes(int g) const {
        if (g < 0 || g >= (int)groups_.size()) return {};
        std::vector<int> sizes;
        for (const Level& lev : groups_[g].bsgs.chain)
            sizes.push_back((int)lev.transversal.size());
        return sizes;
    }

    void setCallback(emscripten::val cb) { js_callback_ = cb; }
    void setDepthCallback(emscripten::val cb) { depth_callback_ = cb; }

    // Runs IDA* from startPerm to the simultaneous identity of all groups.
    // Solutions are streamed to js_callback_ as JS arrays of move indices.
    // Reports solutions of length in [min_moves, max_moves] that are within
    // slack moves of the shortest found solution (>= min_moves).
    // Returns {} on success (including already-solved), {-1} if unreachable.
    std::vector<int> solve(const std::vector<int>& startPerm,
                           int min_moves, int max_moves, int slack) {
        solution_.clear();
        const Perm p(startPerm.begin(), startPerm.end());
        const std::vector<int>& base = base_;
        const int t  = (int)groups_.size();
        const int np = (int)product_tables_.size();

        // Compute initial per-group state indices
        std::vector<int> state(t);
        for (int i = 0; i < t; i++) {
            if (groups_[i].kind == TargetGroup::ORIENTPERM) {
                state[i] = (int)groups_[i].op_spec.state_to_index(p);
            } else {
                auto it = groups_[i].canon_id_table.find(groups_[i].hashPerm(p, base));
                if (it == groups_[i].canon_id_table.end()) return {-1};
                state[i] = it->second;
            }
        }

        // Compute initial product state indices and check reachability
        std::vector<long long> pd_state(np);
        for (int pi = 0; pi < np; pi++) {
            long long idx = 0;
            for (int j = 0; j < (int)product_tables_[pi].component_ids.size(); j++) {
                int ci = product_tables_[pi].component_ids[j];
                idx += (long long)state[ci] * product_tables_[pi].strides[j];
            }
            pd_state[pi] = idx;
            if (product_tables_[pi].distance_table.get(idx) == 3) return {-1};
        }

        std::vector<int> h_vals(np);
        for (int pi = 0; pi < np; pi++)
            h_vals[pi] = computeExactH(pi, pd_state[pi]);
        int h = *std::max_element(h_vals.begin(), h_vals.end());
        if (h == 0) {
            if (min_moves == 0 && !js_callback_.isUndefined() && !js_callback_.isNull())
                js_callback_(emscripten::val::array());
            return {};
        }

        int threshold = std::max(h, min_moves);
        if (threshold > max_moves) return {};
        int effective_max = max_moves;
        bool first_solution_found = false;

        // Stacks: one slot per depth level, grown as needed.
        std::vector<std::vector<int>>       ss(1, state);
        std::vector<std::vector<long long>> pd_ss(1, pd_state);
        std::vector<std::vector<int>>       hs(1, h_vals);

        while (threshold <= effective_max) {
            if ((int)ss.size() <= threshold) {
                ss.resize(threshold + 1, std::vector<int>(t));
                pd_ss.resize(threshold + 1, std::vector<long long>(np));
                hs.resize(threshold + 1, std::vector<int>(np));
            }
            if (!depth_callback_.isUndefined() && !depth_callback_.isNull())
                depth_callback_(threshold);
            found_any_solution_ = false;
            int next = idaDfs(ss, pd_ss, hs, 0, threshold, {});
            if (found_any_solution_ && !first_solution_found) {
                first_solution_found = true;
                effective_max = std::min(threshold + slack, max_moves);
            }
            if (next == INT_MAX || next > effective_max) break;
            threshold = next;
        }
        return {};
    }
};

EMSCRIPTEN_BINDINGS(module) {
    emscripten::register_vector<int>("VectorInt");
    emscripten::class_<SchreierSimsRunner>("SchreierSimsRunner")
        .constructor<>()
        .function("reset",             &SchreierSimsRunner::reset)
        .function("addGenerator",      &SchreierSimsRunner::addGenerator)
        .function("run",               &SchreierSimsRunner::run)
        .function("build",             &SchreierSimsRunner::build)
        .function("getBase",           &SchreierSimsRunner::getBase)
        .function("canonicalizePerm",  &SchreierSimsRunner::canonicalizePerm)
        .function("clearSolvingMoves", &SchreierSimsRunner::clearSolvingMoves)
        .function("addSolvingMove",    &SchreierSimsRunner::addSolvingMove)
        .function("buildTable",             &SchreierSimsRunner::buildTable)
        .function("lookupCanonId",          &SchreierSimsRunner::lookupCanonId)
        .function("getTableSize",           &SchreierSimsRunner::getTableSize)
        .function("buildTransitionTable",   &SchreierSimsRunner::buildTransitionTable)
        .function("getTransitionRow",       &SchreierSimsRunner::getTransitionRow)
        .function("buildDistanceTable",     &SchreierSimsRunner::buildDistanceTable)
        .function("getDistance",            &SchreierSimsRunner::getDistance)
        .function("solve",                  &SchreierSimsRunner::solve);
    emscripten::class_<MultiTargetSolver>("MultiTargetSolver")
        .constructor<>()
        .function("reset",               &MultiTargetSolver::reset)
        .function("addBasePoint",        &MultiTargetSolver::addBasePoint)
        .function("beginTargetGroup",    &MultiTargetSolver::beginTargetGroup)
        .function("addTargetGenerator",  &MultiTargetSolver::addTargetGenerator)
        .function("buildTargetGroup",    &MultiTargetSolver::buildTargetGroup)
        .function("beginOrientPermGroup",  &MultiTargetSolver::beginOrientPermGroup)
        .function("addOrientPermClass",    &MultiTargetSolver::addOrientPermClass)
        .function("buildOrientPermGroup",  &MultiTargetSolver::buildOrientPermGroup)
        .function("addSolvingGenerator", &MultiTargetSolver::addSolvingGenerator)
        .function("buildSolvingBSGS",    &MultiTargetSolver::buildSolvingBSGS)
        .function("clearSolvingMoves",   &MultiTargetSolver::clearSolvingMoves)
        .function("addSolvingMove",      &MultiTargetSolver::addSolvingMove)
        .function("buildTables",               &MultiTargetSolver::buildTables)
        .function("beginProductDistanceTable", &MultiTargetSolver::beginProductDistanceTable)
        .function("addProductTableComponent",  &MultiTargetSolver::addProductTableComponent)
        .function("getNumGroups",              &MultiTargetSolver::getNumGroups)
        .function("getNumProductTables",       &MultiTargetSolver::getNumProductTables)
        .function("getProductTableSize",       &MultiTargetSolver::getProductTableSize)
        .function("getGroupTableSize",         &MultiTargetSolver::getGroupTableSize)
        .function("getSolvingOrbitSizes",      &MultiTargetSolver::getSolvingOrbitSizes)
        .function("getTargetGroupOrbitSizes",  &MultiTargetSolver::getTargetGroupOrbitSizes)
        .function("setCallback",               &MultiTargetSolver::setCallback)
        .function("setDepthCallback",          &MultiTargetSolver::setDepthCallback)
        .function("solve",                     &MultiTargetSolver::solve);
}

#else

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    // Problem-statement example: V₄ (Klein 4-group), order 4
    {
        std::cout << "=== Problem example: n=4, gens=[[0,1,3,2],[1,0,2,3]] ===\n";
        std::cout << "Gens are (2 3) and (0 1) -> Klein 4-group, |G|=4\n";
        BSGS b = randomized_schreier_sims(4, {{0,1,3,2},{1,0,2,3}});
        print_transversal(b);
        assert(b.order() == 4);
        std::cout << "\n";
    }

    // S₄
    {
        std::cout << "=== S_4: gens=[(0 1 2 3),(0 1)] ===\n";
        BSGS b = randomized_schreier_sims(4, {{1,2,3,0},{1,0,2,3}});
        print_transversal(b);
        assert(b.order() == 24);
        std::cout << "\n";
    }

    // S₅
    {
        std::cout << "=== S_5: gens=[(0 1 2 3 4),(0 1)] ===\n";
        BSGS b = randomized_schreier_sims(5, {{1,2,3,4,0},{1,0,2,3,4}});
        print_transversal(b);
        assert(b.order() == 120);
        std::cout << "\n";
    }

    // A₅  (order 60)
    {
        std::cout << "=== A_5: gens=[(0 1 2),(0 1 2 3 4)] ===\n";
        BSGS b = randomized_schreier_sims(5, {{1,2,0,3,4},{1,2,3,4,0}});
        print_transversal(b);
        assert(b.order() == 60);
        std::cout << "\n";
    }

    // S₈  (order 40320)
    {
        std::cout << "=== S_8: gens=[(01234567),(01)] ===\n";
        BSGS b = randomized_schreier_sims(8,
            {{1,2,3,4,5,6,7,0},{1,0,2,3,4,5,6,7}});
        print_transversal(b);
        assert(b.order() == 40320);
        std::cout << "\n";
    }

    // Interactive stdin mode
    if (argc > 1 && std::string(argv[1]) == "--stdin") {
        std::cout << "=== stdin mode ===\n";
        std::cout << "n  t          (degree, number of generators)\n";
        std::cout << "g_1[0] ...    (t lines, each n ints)\n";
        int n, t;
        if (!(std::cin >> n >> t)) return 1;
        std::vector<Perm> gens(t, Perm(n));
        for (int i = 0; i < t; ++i)
            for (int j = 0; j < n; ++j) std::cin >> gens[i][j];
        BSGS b = randomized_schreier_sims(n, gens);
        print_transversal(b);
        std::cout << "Order: " << b.order();
    }

    return 0;
}

#endif