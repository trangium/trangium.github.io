// S3' -> S4 coordinates and generic distance-table machinery.
//
// The S3'->S4 coset space (234,101,145,600 states) is a direct product of 6
// coordinates that each evolve independently under the 13-move S3'-defining
// generator set <U,D,R2,L2,F2,B2,Rw2,Uw2,Fw2> (S3PRIME_GOOD_MOVES_PM):
//
//   layer  20,160  even permutations of the 8 U/D-layer wing pairs
//   eq          4  the equatorial (FR/FL/BR/BL) wing-pair tuple
//   pll         2  PLL parity (toggled by Uw2/Rw2/Fw2)
//   ud      2,520  U/D center class (free at the S3' checkpoint)
//   lr         24  L/R center class -- 24 of the 2520 lr2520 classes occur
//   fb         24  F/B center class (fb24)
//
// Every coordinate is derived the same way, with no hand-derived "good" subsets:
// start from a RAW representation with a big-enough native domain, BFS the
// orbit of solved under the 13 moves (build_coord), and keep whatever it finds
// as a dense, compact coordinate with a [size][13] transition table. The
// discovered sizes are checked against the expected ones at startup. Solved is
// dense id 0 in every coordinate (it is the BFS start).
//
// Composite coordinates (compose) and two-factor distance tables
// (build_pair_table) are then plain products of these, so a heuristic variant
// is just a choice of which composites to tabulate -- see s4_tables.h.
#pragma once
#include "s3p.h"

namespace coords {

static const int S4_NM = 13;  // moves in S3PRIME_GOOD_MOVES_PM; column k <-> pm S3PRIME_GOOD_MOVES_PM[k]

struct Coord {
    int size = 0;
    std::vector<int32_t> trans;         // [size * S4_NM + k] -> new dense id
    std::vector<int32_t> dense_of_raw;  // raw domain -> dense id (-1 if never reached); empty for composites
    inline int step(int id, int k) const { return trans[(size_t)id * S4_NM + k]; }
};

// `apply(raw, k)` returns the raw state after move column k.
template <typename ApplyFn>
static Coord build_coord(int raw_domain, int raw_start, ApplyFn apply) {
    Coord C;
    C.dense_of_raw.assign(raw_domain, -1);
    std::vector<int> rep;
    C.dense_of_raw[raw_start] = 0;
    rep.push_back(raw_start);
    for (size_t head = 0; head < rep.size(); head++) {
        int cur = rep[head];
        for (int k = 0; k < S4_NM; k++) {
            int nxt = apply(cur, k);
            if (C.dense_of_raw[nxt] == -1) { C.dense_of_raw[nxt] = (int)rep.size(); rep.push_back(nxt); }
        }
    }
    C.size = (int)rep.size();
    C.trans.assign((size_t)C.size * S4_NM, 0);
    for (int d = 0; d < C.size; d++)
        for (int k = 0; k < S4_NM; k++) C.trans[(size_t)d * S4_NM + k] = C.dense_of_raw[apply(rep[d], k)];
    return C;
}

// Product coordinate: id = a * b.size + b'.
static Coord compose(const Coord& a, const Coord& b) {
    Coord C;
    C.size = a.size * b.size;
    C.trans.resize((size_t)C.size * S4_NM);
    for (int i = 0; i < a.size; i++)
        for (int j = 0; j < b.size; j++)
            for (int k = 0; k < S4_NM; k++)
                C.trans[((size_t)i * b.size + j) * S4_NM + k] = a.step(i, k) * b.size + b.step(j, k);
    return C;
}

// Raw P(12,4) rank/unrank: an ORDERED tuple of 4 distinct values from 0..11
// (11,880 raw states) -- the raw form of the equatorial coordinate.
static inline int rank_p12_4(const std::array<int,4>& t) {
    bool used[12] = {};
    int rank = 0, avail = 12;
    for (int i = 0; i < 4; i++) {
        int less = 0;
        for (int u = 0; u < t[i]; u++) if (!used[u]) less++;
        rank = rank * avail + less;
        avail--;
        used[t[i]] = true;
    }
    return rank;
}
static inline std::array<int,4> unrank_p12_4(int r) {
    bool used[12] = {};
    std::array<int,4> t;
    static const int PLACE[4] = {11 * 10 * 9, 10 * 9, 9, 1};
    for (int i = 0; i < 4; i++) {
        int less = r / PLACE[i];
        r %= PLACE[i];
        int v = -1, cnt = -1;
        for (int u = 0; u < 12; u++) if (!used[u]) { cnt++; if (cnt == less) { v = u; break; } }
        t[i] = v;
        used[v] = true;
    }
    return t;
}

static Coord C_LAYER, C_EQ, C_PLL, C_UD, C_LR, C_FB;

static inline int equatorial_raw_of(const std::array<uint8_t,12>& pairing) {
    std::array<int,4> t;
    for (int j = 0; j < 4; j++) t[j] = pairing[EQUATORIAL_IDX[j]];
    return rank_p12_4(t);
}

// Requires init_layer_equatorial_split/init_layer_value_local, the reduced
// center transitions and build_sigma_p2 to have run.
static void build_s4_coords() {
    // layer: an 8-permutation embedded in a 12-tuple (equatorial slots = identity).
    C_LAYER = build_coord(40320, 0, [&](int raw, int k) {
        auto local = unrank_perm8(raw);
        std::array<uint8_t,12> full;
        for (int i = 0; i < 8; i++) full[LAYER_IDX[i]] = (uint8_t)LAYER_IDX[local[i]];
        for (int j = 0; j < 4; j++) full[EQUATORIAL_IDX[j]] = (uint8_t)EQUATORIAL_IDX[j];
        return layer_rank_of(apply_move_to_perm12_p2(full, S3PRIME_GOOD_MOVES_PM[k]));
    });
    // equatorial: a 4-tuple embedded in a 12-tuple (layer slots = identity).
    {
        std::array<int,4> id_tuple;
        for (int j = 0; j < 4; j++) id_tuple[j] = EQUATORIAL_IDX[j];
        C_EQ = build_coord(11880, rank_p12_4(id_tuple), [&](int raw, int k) {
            auto tup = unrank_p12_4(raw);
            std::array<uint8_t,12> full;
            for (int i = 0; i < 8; i++) full[LAYER_IDX[i]] = (uint8_t)LAYER_IDX[i];
            for (int j = 0; j < 4; j++) full[EQUATORIAL_IDX[j]] = (uint8_t)tup[j];
            return equatorial_raw_of(apply_move_to_perm12_p2(full, S3PRIME_GOOD_MOVES_PM[k]));
        });
    }
    C_PLL = build_coord(2, 0, [&](int raw, int k) {
        return raw ^ (TOGGLES_RWUWFW[P2_TO_FULL_MOVE_INDEX[S3PRIME_GOOD_MOVES_PM[k]]] ? 1 : 0);
    });
    C_UD = build_coord(2520, 0, [&](int raw, int k) { return (int)UD2520_TRANS[raw][S3PRIME_GOOD_MOVES_PM[k]]; });
    C_LR = build_coord(2520, 0, [&](int raw, int k) { return (int)LR2520_TRANS[raw][S3PRIME_GOOD_MOVES_PM[k]]; });
    C_FB = build_coord(24, 0, [&](int raw, int k) {
        int v = (int)FB24_TRANS[raw][S3PRIME_GOOD_MOVES_PM[k]];
        if (v < 0) { printf("FATAL: FB24_TRANS left the fb domain under an S3'-good move\n"); exit(1); }
        return v;
    });

    struct Expect { const char* name; const Coord* c; int size; } expect[6] = {
        {"layer", &C_LAYER, 20160}, {"eq", &C_EQ, 4}, {"pll", &C_PLL, 2},
        {"ud", &C_UD, 2520}, {"lr", &C_LR, 24}, {"fb", &C_FB, 24}};
    for (auto& e : expect) {
        if (e.c->size != e.size) {
            printf("FATAL: S3'->S4 coordinate '%s' has %d states (expected %d)\n", e.name, e.c->size, e.size);
            exit(1);
        }
    }
}

// Dense S3'->S4 coordinate ids of a state in S3', from its raw pieces.
struct S4Ids { int layer, eq, pll, ud, lr, fb; };
static inline S4Ids s4_ids_of(const std::array<uint8_t,12>& pairing, int ud2520, int lr2520, int fb24, int pll) {
    return S4Ids{C_LAYER.dense_of_raw[layer_rank_of(pairing)], C_EQ.dense_of_raw[equatorial_raw_of(pairing)],
                 pll, C_UD.dense_of_raw[ud2520], C_LR.dense_of_raw[lr2520], C_FB.dense_of_raw[fb24]};
}

// ---------------- two-factor distance tables (reference BFS) -----------------
// dist[a * B.size + b] = distance from (a,b) to the NEAREST GOAL under the 13 moves, with A and B advancing in
// lockstep. The goals are the coordinates of the literal S4 and of its images under the whole-cube half turns
// x2, y2, z2 (S4 is only defined up to them, see state.h): `Goals` lists one (a, b) pair per goal. Byte per entry. Level-synchronous scan
// (no frontier storage); consecutive b share `a`, so a level's writes for a
// fixed (a,k) stay inside one B.size-byte row and the BFS is cache-friendly.
using Goals = std::vector<std::pair<int, int>>;   // (a, b) pairs: the goal states of a pair table

static void build_pair_table_bfs(const Coord& A, const Coord& B, const Goals& goals, std::vector<uint8_t>& dist) {
    const size_t nb = (size_t)B.size, total = (size_t)A.size * nb;
    dist.assign(total, 255);
    for (auto& g : goals) dist[(size_t)g.first * nb + (size_t)g.second] = 0;
    for (int d = 0; d < 254; d++) {
        size_t found = 0;
        for (size_t idx = 0; idx < total; idx++) {
            if (dist[idx] != d) continue;
            const size_t a = idx / nb, b = idx % nb;
            const int32_t* ta = &A.trans[a * S4_NM];
            const int32_t* tb = &B.trans[b * S4_NM];
            for (int k = 0; k < S4_NM; k++) {
                size_t nidx = (size_t)ta[k] * nb + (size_t)tb[k];
                if (dist[nidx] == 255) { dist[nidx] = (uint8_t)(d + 1); found++; }
            }
        }
        if (!found) break;
    }
}

// The unreduced tables are not used by the search any more (s4_sym.h builds the symmetry-reduced ones directly); this plain BFS
// is only the independent reference that `--check-s4-sym` and the checks in selftest.h compare the reduced tables against.
} // namespace coords
