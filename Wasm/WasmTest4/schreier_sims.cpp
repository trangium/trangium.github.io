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
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#endif

// ─── Types ───────────────────────────────────────────────────────────────────

using Perm = std::vector<int>;

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
    std::mt19937& rng;
    int n;

    ProductReplacer(int n, const std::vector<Perm>& gens, std::mt19937& rng)
        : rng(rng), n(n) {
        for (const Perm& g : gens) {
            bag.push_back(g);
            bag.push_back(inv(g));
        }
        while ((int)bag.size() < 10) bag.push_back(identity(n));
        for (int k = 0; k < 50; ++k) step();   // warm-up
    }

    Perm next() {
        step();
        std::uniform_int_distribution<int> d(0, (int)bag.size() - 1);
        return bag[d(rng)];
    }

private:
    void step() {
        std::uniform_int_distribution<int> d(0, (int)bag.size() - 1);
        int i = d(rng), j;
        do { j = d(rng); } while (j == i);
        bag[i] = (rng() & 1) ? compose(bag[i], bag[j])
                              : compose(bag[i], inv(bag[j]));
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
public:
    void reset(int n) { n_ = n; generators_.clear(); }

    void addGenerator(const std::vector<int>& g) {
        generators_.emplace_back(g.begin(), g.end());
    }

    // Returns JSON: {"base":[…],"orbitSizes":[…],"order":N}
    std::string run(int confidence) {
        BSGS bsgs = randomized_schreier_sims(n_, generators_, confidence);
        auto B = bsgs.base();
        std::string s = "{\"base\":[";
        for (int i = 0; i < (int)B.size(); i++) { if (i) s += ","; s += std::to_string(B[i]); }
        s += "],\"orbitSizes\":[";
        for (int i = 0; i < (int)bsgs.chain.size(); i++) {
            if (i) s += ",";
            s += std::to_string(bsgs.chain[i].transversal.size());
        }
        s += "],\"order\":" + std::to_string(bsgs.order()) + "}";
        return s;
    }
};

EMSCRIPTEN_BINDINGS(module) {
    emscripten::register_vector<int>("VectorInt");
    emscripten::class_<SchreierSimsRunner>("SchreierSimsRunner")
        .constructor<>()
        .function("reset",        &SchreierSimsRunner::reset)
        .function("addGenerator", &SchreierSimsRunner::addGenerator)
        .function("run",          &SchreierSimsRunner::run);
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