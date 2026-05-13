#include <emscripten/bind.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <limits>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace std;
using namespace emscripten;

// ── Min-plus evaluator ────────────────────────────────────────────────────────

static const float INF = numeric_limits<float>::infinity();

struct SparseMatrix {
    int n;
    vector<int> row_ptr, cols;
    vector<float> vals;
};

static void minPlusMV(const SparseMatrix& A, const vector<float>& v, vector<float>& out) {
    for (int i = 0; i < A.n; i++) {
        float ri = INF;
        for (int k = A.row_ptr[i], e = A.row_ptr[i + 1]; k < e; k++) {
            float vj = v[A.cols[k]];
            if (vj < INF) {
                float x = A.vals[k] + vj;
                if (x < ri) ri = x;
            }
        }
        out[i] = ri;
    }
}

class AlgorithmEvaluator {
    int n = 0;
    unordered_map<string, SparseMatrix> moves;
    vector<float> initVec;

    SparseMatrix toSparse(const vector<float>& flat) const {
        SparseMatrix sm;
        sm.n = n;
        sm.row_ptr.resize(n + 1, 0);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (isfinite(flat[i * n + j])) sm.row_ptr[i + 1]++;
        for (int i = 0; i < n; i++) sm.row_ptr[i + 1] += sm.row_ptr[i];
        int nnz = sm.row_ptr[n];
        sm.cols.resize(nnz);
        sm.vals.resize(nnz);
        for (int i = 0; i < n; i++) {
            int k = sm.row_ptr[i];
            for (int j = 0; j < n; j++) {
                float v = flat[i * n + j];
                if (isfinite(v)) { sm.cols[k] = j; sm.vals[k++] = v; }
            }
        }
        return sm;
    }

public:
    void setInitialVector(const vector<float>& v) {
        n = (int)v.size();
        initVec = v;
    }

    void addMove(const string& name, const vector<float>& flat) {
        moves.insert_or_assign(name, toSparse(flat));
    }

    float evaluate(const vector<string>& seq) const {
        vector<float> buf[2] = {initVec, vector<float>(n, INF)};
        int cur = 0;
        for (const string& mv : seq) {
            auto it = moves.find(mv);
            if (it != moves.end()) {
                minPlusMV(it->second, buf[cur], buf[cur ^ 1]);
                cur ^= 1;
            }
        }
        float res = INF;
        for (float x : buf[cur]) if (x < res) res = x;
        return res;
    }
};

// ── Cube permutation evaluator ────────────────────────────────────────────────

class CubeEvaluator {
    int k = 0;
    unordered_map<string, vector<uint32_t>> moves;

    vector<uint32_t> makeIdentity() const {
        vector<uint32_t> p(k);
        iota(p.begin(), p.end(), 0u);
        return p;
    }

public:
    void setK(int dim) {
        k = dim;
        moves.clear();
    }

    void addMove(const string& name, const vector<int>& flat) {
        moves.insert_or_assign(name, vector<uint32_t>(flat.begin(), flat.end()));
    }

    void composeMove(const string& name, const vector<string>& seq) {
        vector<uint32_t> perm = makeIdentity();
        for (const string& mv : seq) {
            auto it = moves.find(mv);
            if (it == moves.end()) throw runtime_error("Unknown move: " + mv);
            const auto& p = it->second;
            for (int i = 0; i < k; i++) perm[i] = p[perm[i]];
        }
        moves.insert_or_assign(name, std::move(perm));
    }

    // Evaluate sequence applied to the given base points only.
    vector<int> evaluate(const vector<int>& base, const vector<string>& seq) const {
        vector<int> pos(base);
        for (const string& mv : seq) {
            auto it = moves.find(mv);
            if (it == moves.end()) continue;
            const auto& p = it->second;
            for (int& x : pos) x = p[x];
        }
        return pos;
    }

    // Evaluate sequence starting from identity — returns the full k-length permutation.
    vector<int> evaluateFull(const vector<string>& seq) const {
        vector<int> pos(k);
        iota(pos.begin(), pos.end(), 0);
        for (const string& mv : seq) {
            auto it = moves.find(mv);
            if (it == moves.end()) continue;
            const auto& p = it->second;
            vector<int> next(k);
            for (int i = 0; i < k; i++) next[i] = (int)p[pos[i]];
            pos = std::move(next);
        }
        return pos;
    }
};

// ── Schreier-Sims (incremental with sifting) ──────────────────────────────────
//
// Naive Schreier-Sims passes all Schreier generators to the next level, causing
// exponential blowup: orbit_size^depth generators, each of size k.  For the 3×3×3
// (k=48, depth≈16) this becomes gigabytes within a few levels.
//
// Fix: sift each Schreier generator before acting on it.  If it reaches identity
// it is already in the group — discard it.  This bounds generators per level by
// orbit_size−1, giving O(k²) total SGS elements and O(k⁵) build time.

class SchreierSimsTable {
    int k = 0;
    vector<vector<uint32_t>> pendingGens;
    vector<int> baseVec;
    // transversals[i][y] = permutation u with u(base[i]) = y
    vector<unordered_map<int, vector<uint32_t>>> transversals;
    // chainGens[i] = generators added at stabilizer level i
    vector<vector<vector<uint32_t>>> chainGens;

    static vector<uint32_t> invertPerm(const vector<uint32_t>& p) {
        vector<uint32_t> r(p.size());
        for (size_t i = 0; i < p.size(); i++) r[p[i]] = (uint32_t)i;
        return r;
    }

    static bool isIdentity(const vector<uint32_t>& p) {
        for (size_t i = 0; i < p.size(); i++) if (p[i] != (uint32_t)i) return false;
        return true;
    }

    // Sift g through the chain.  Return (level_stopped, residual).
    // level_stopped == baseVec.size() means g is in the group.
    pair<int, vector<uint32_t>> sift(vector<uint32_t> g) const {
        for (int i = 0; i < (int)baseVec.size(); i++) {
            int x = (int)g[baseVec[i]];
            auto it = transversals[i].find(x);
            if (it == transversals[i].end()) return {i, std::move(g)};
            auto u_inv = invertPerm(it->second);
            vector<uint32_t> ng(k);
            for (int j = 0; j < k; j++) ng[j] = u_inv[g[j]];
            g = std::move(ng);
        }
        return {(int)baseVec.size(), std::move(g)};
    }

    // Insert g into the BSGS.  Sift first; discard if already explained.
    // This is the core of incremental Schreier-Sims.
    void doInsert(vector<uint32_t> g) {
        if (isIdentity(g)) return;

        auto [L, residual] = sift(std::move(g));
        if (isIdentity(residual)) return;      // already in group — discard

        // residual fails at level L; if L is new, extend the chain.
        if (L == (int)baseVec.size()) {
            for (int i = 0; i < k; i++) {
                if (residual[i] != (uint32_t)i) {
                    baseVec.push_back(i);
                    vector<uint32_t> id(k); iota(id.begin(), id.end(), 0u);
                    transversals.push_back({{i, id}});
                    chainGens.push_back({});
                    break;
                }
            }
        }

        chainGens[L].push_back(residual);

        // BFS to extend the orbit at level L using all generators in chainGens[L].
        // Generators added by recursive doInsert calls below only go to levels > L
        // (Schreier gens fix base[L]), so chainGens[L] is stable during recursion.
        vector<int> orbit;
        for (const auto& [x, _] : transversals[L]) orbit.push_back(x);

        for (size_t qi = 0; qi < orbit.size(); qi++) {
            int x = orbit[qi];
            for (const auto& gen : chainGens[L]) {
                int y = (int)gen[x];
                if (!transversals[L].count(y)) {
                    vector<uint32_t> ty(k);
                    for (int j = 0; j < k; j++) ty[j] = gen[transversals[L][x][j]];
                    transversals[L].emplace(y, std::move(ty));
                    orbit.push_back(y);
                }
            }
        }

        // Snapshot orbit data and generators locally before recursive calls,
        // since push_back on the outer transversals/chainGens vectors can relocate
        // their heap buffers, invalidating any stored references.
        vector<pair<int,vector<uint32_t>>> orbitSnap;
        orbitSnap.reserve(orbit.size());
        for (int x : orbit) orbitSnap.emplace_back(x, transversals[L][x]);

        unordered_map<int, vector<uint32_t>> T_inv;
        T_inv.reserve(orbit.size());
        for (auto& [y, ty] : orbitSnap) T_inv[y] = invertPerm(ty);

        vector<vector<uint32_t>> gensSnap = chainGens[L]; // stable: won't grow in recursion

        // Schreier's lemma: sg = T[x] ∘ gen ∘ T[gen(x)]⁻¹ stabilises base[L].
        // Sifting discards generators already explained by deeper levels.
        for (const auto& [x, tx] : orbitSnap) {
            for (const auto& gen : gensSnap) {
                int y = (int)gen[x];
                const auto& ty_inv = T_inv.at(y);
                vector<uint32_t> sg(k);
                for (int j = 0; j < k; j++) sg[j] = ty_inv[gen[tx[j]]];
                doInsert(std::move(sg));
            }
        }
    }

public:
    void reset(int dim) {
        k = dim;
        pendingGens.clear();
        baseVec.clear();
        transversals.clear();
        chainGens.clear();
    }

    void addGenerator(const vector<int>& perm) {
        pendingGens.push_back(vector<uint32_t>(perm.begin(), perm.end()));
    }

    void build() {
        baseVec.clear();
        transversals.clear();
        chainGens.clear();
        if (k == 0) return;
        for (const auto& g : pendingGens) doInsert(g);
    }

    bool isMember(const vector<int>& perm) const {
        auto [level, residual] = sift(vector<uint32_t>(perm.begin(), perm.end()));
        (void)level;
        return isIdentity(residual);
    }

    vector<int> getBase() const { return baseVec; }

    vector<int> getOrbitSizes() const {
        vector<int> s;
        s.reserve(transversals.size());
        for (const auto& T : transversals) s.push_back((int)T.size());
        return s;
    }
};

// ── Bindings ──────────────────────────────────────────────────────────────────

EMSCRIPTEN_BINDINGS(module) {
    register_vector<string>("VectorString");
    register_vector<float>("VectorFloat");
    register_vector<int>("VectorInt");

    class_<AlgorithmEvaluator>("AlgorithmEvaluator")
        .constructor<>()
        .function("setInitialVector", &AlgorithmEvaluator::setInitialVector)
        .function("addMove",          &AlgorithmEvaluator::addMove)
        .function("evaluate",         &AlgorithmEvaluator::evaluate);

    class_<CubeEvaluator>("CubeEvaluator")
        .constructor<>()
        .function("setK",          &CubeEvaluator::setK)
        .function("addMove",       &CubeEvaluator::addMove)
        .function("composeMove",   &CubeEvaluator::composeMove)
        .function("evaluate",      &CubeEvaluator::evaluate)
        .function("evaluateFull",  &CubeEvaluator::evaluateFull);

    class_<SchreierSimsTable>("SchreierSimsTable")
        .constructor<>()
        .function("reset",        &SchreierSimsTable::reset)
        .function("addGenerator", &SchreierSimsTable::addGenerator)
        .function("build",        &SchreierSimsTable::build)
        .function("isMember",     &SchreierSimsTable::isMember)
        .function("getBase",      &SchreierSimsTable::getBase)
        .function("getOrbitSizes",&SchreierSimsTable::getOrbitSizes);
}
