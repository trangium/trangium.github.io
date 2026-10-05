#pragma once
#include "s6.h"

namespace coords {

// =============================== Phase 7 (S6->solved) =========================
// The final tier: |S6| = 5,308,416 (per the user's spec, see chat), so this
// is a single flat distance table all the way to literal solved -- no
// further "penalty for a still-ambiguous condition" layer needed, unlike
// tiers 5 and 6, since S6's own two parity ties already pinned everything
// down exactly.
//
// Move set: <U2,D2,R2,L2,F2,B2> (6 raw move indices below) -- confirmed by
// the user. This is EXACTLY H, the same order-96 group condition 1 (one
// tier up) already confines corner permutation to -- U/D drop from full-
// quarter to halves-only here (matching L/R's own drop at this tier and F/B's
// at S2), since is_in_s6 already requires U/D "solved or 180" (condition 3),
// exactly the same 2-state confinement L/R/F/B already have.
//
// Indexing (per the user's own spec, refined by an empirical correction --
// see chat): a corner-permutation coordinate over H's own 96 elements (NOT
// the 420-coset coordinate one tier up -- once corner permutation is
// confined to literal H, "which of the 420 cosets" is always coset 0, so it
// has nothing left to distinguish; need WHICH of H's 96 elements instead),
// combined with an "edges+centers" coordinate: naively (4!)^3 (an
// independent S4 permutation within each of the M-slice, S-slice, and
// equatorial 4-tuples) x 2^6 (each of the 6 faces' centers solved-or-180)
// = 13824 x 64 = 884,736 raw combinations.
//
// The user's own initial estimate was "1/16 of these are reachable"
// (55,296), combined independently with all 96 corner permutations. Ground-
// truthed against real WING_PERM/CENTER_PERM/CORNER_PERM simulation (see
// chat) before trusting either number: the edges+centers space ALONE
// (ignoring corners) is actually only cut by 1/8 (three independent parity
// ties already known from tiers 5/6 -- equatorial-vs-LRFB, M-slice-vs-UFDB,
// S-slice-vs-URDL -- each an even/odd split; no fourth internal constraint),
// giving 110,592 reachable states, not 55,296. The missing factor of 2
// instead links corner-within-H to the edges+centers state directly (a
// parity tie of the SAME shape as tier 6's own corner-coset/ud discovery,
// just not yet reduced to a closed-form expression) -- exactly HALF of the
// naive 96 x 110,592 = 10,616,832 combinations are jointly reachable, and
// 10,616,832 / 2 = 5,308,416 = |S6| exactly, matching the user's own number
// (their arithmetic was right; the factor of 16 was just split differently
// between "internal to edges+centers" and "linking corners to them" than
// their initial guess). Handled the same way as tier 6's own half-reachable
// joint table: build over the full naive product, verify EXACTLY half
// reachable with a perfectly balanced per-corner_h split (not just "some
// unreached count"), and store a safe sentinel for the other half.

static const int TIER6_MOVES[6] = { 1, 7, 10, 16, 19, 25 };  // U2,D2,R2,L2,F2,B2
static const int TIER6_NUM_MOVES = 6;

// ---- Generic rank/unrank for a 4-element permutation -----------------------
// O(n) packed-nibble Lehmer rank/unrank (see pairing_dist_io.h's
// pairing_rank_full/pairing_unrank_full for the technique writeup) instead
// of the O(n^2) double-loop rank / O(n^2) array-shift unrank this used to be
// -- n=4 is tiny either way, but kept consistent with the rest.
static inline int rank_perm4(const std::array<uint8_t,4>& p) {
    uint32_t val = 0x3210u;
    int r = 0;
    for (int i = 0; i < 4; i++) {
        int shift = ((int)p[i]) << 2;
        r = r * (4 - i) + (int)((val >> shift) & 0xF);
        val -= 0x1110u << shift;
    }
    return r;
}
static inline std::array<uint8_t,4> unrank_perm4(int r) {
    uint32_t val = 0x3210u;
    static const int FACT[4] = {6, 2, 1, 1};
    std::array<uint8_t,4> out;
    for (int i = 0; i < 4; i++) {
        int f = FACT[i];
        int sel = r / f;
        r %= f;
        int shift = sel << 2;
        out[i] = (uint8_t)((val >> shift) & 0xF);
        uint32_t m = (shift == 0) ? 0u : ((1u << shift) - 1);
        val = (val & m) + ((val >> 4) & ~m);
    }
    return out;
}

// ---- Per-generator position transitions for each of the 3 edge slices ------
// Same WING_PERM+POS_INDEX recipe as MSLICE_POS_TRANS one tier up (a genuine
// LIVE position transition, not the pairing-intactness coordinate -- see the
// M/S-slice-split comment above for why that distinction matters).
static int M_SLICE_POS_TRANS[6][4], S_SLICE_POS_TRANS[6][4], EQ_SLICE_POS_TRANS[6][4];
static void build_slice_pos_trans() {
    for (int t = 0; t < TIER6_NUM_MOVES; t++) {
        int m = TIER6_MOVES[t];
        for (int i = 0; i < 4; i++) {
            auto local_of = [&](const int idx[4], int global_pos) -> int {
                for (int j = 0; j < 4; j++) if (idx[j] == global_pos) return j;
                return -1;
            };
            int dest_m = POS_INDEX[WING_PERM[m][POS_SLOTS[M_SLICE_IDX[i]]]];
            int dest_s = POS_INDEX[WING_PERM[m][POS_SLOTS[S_SLICE_IDX[i]]]];
            int dest_eq = POS_INDEX[WING_PERM[m][POS_SLOTS[EQUATORIAL_IDX[i]]]];
            int lm = local_of(M_SLICE_IDX, dest_m);
            int ls = local_of(S_SLICE_IDX, dest_s);
            int leq = local_of(EQUATORIAL_IDX, dest_eq);
            if (lm < 0 || ls < 0 || leq < 0) {
                printf("FATAL: tier-6 move %d does not preserve the M/S/equatorial slice split\n", m);
                exit(1);
            }
            M_SLICE_POS_TRANS[t][i] = lm;
            S_SLICE_POS_TRANS[t][i] = ls;
            EQ_SLICE_POS_TRANS[t][i] = leq;
        }
    }
}

// Full (not just occupancy) permutation of a slice's 4 dedges among its 4
// positions -- generalizes slice_edge_perm_parity (one tier up, which only
// wanted the PARITY of this same array) to expose the whole array/rank, now
// that phase 7 needs to distinguish all 24 arrangements, not just even/odd.
static inline std::array<uint8_t,4> slice_perm_array(const int* wing_slot, const int slice_idx[4]) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    std::array<uint8_t,4> p;
    for (int i = 0; i < 4; i++) {
        int dedge = dedge_id_of_piece(occupant_of_slot[POS_SLOTS[slice_idx[i]]]);
        int local = -1;
        for (int j = 0; j < 4; j++) if (slice_idx[j] == dedge) { local = j; break; }
        p[i] = (uint8_t)local;
    }
    return p;
}
static inline int slice_perm_rank(const int* wing_slot, const int slice_idx[4]) {
    return rank_perm4(slice_perm_array(wing_slot, slice_idx));
}
static inline int slice_perm_trans(int rank, const int pos_trans[4]) {
    auto p = unrank_perm4(rank);
    std::array<uint8_t,4> np;
    for (int i = 0; i < 4; i++) np[pos_trans[i]] = p[i];
    return rank_perm4(np);
}
static int M_PERM_TRANS[24][6], S_PERM_TRANS[24][6], EQ_PERM_TRANS[24][6];
static void build_slice_perm_trans() {
    for (int r = 0; r < 24; r++) {
        for (int t = 0; t < TIER6_NUM_MOVES; t++) {
            M_PERM_TRANS[r][t] = slice_perm_trans(r, M_SLICE_POS_TRANS[t]);
            S_PERM_TRANS[r][t] = slice_perm_trans(r, S_SLICE_POS_TRANS[t]);
            EQ_PERM_TRANS[r][t] = slice_perm_trans(r, EQ_SLICE_POS_TRANS[t]);
        }
    }
}

// ---- 6-bit "which faces are at 180" center coordinate -----------------------
// Bit assignment is arbitrary (never observed externally), just fixed and
// self-consistent between center_mask_of and center_mask_delta.
static inline int center_mask_of(const std::array<int,24>& center_slot) {
    int mask = 0;
    if (center4_is_shifted(center_slot, U_CENTER_SLOTS)) mask |= 1;
    if (center4_is_shifted(center_slot, D_CENTER_SLOTS)) mask |= 2;
    if (center4_is_shifted(center_slot, L_CENTER_SLOTS)) mask |= 4;
    if (center4_is_shifted(center_slot, R_CENTER_SLOTS)) mask |= 8;
    if (center4_is_shifted(center_slot, F_CENTER_SLOTS)) mask |= 16;
    if (center4_is_shifted(center_slot, B_CENTER_SLOTS)) mask |= 32;
    return mask;
}
// A half turn of face X only ever touches face X's own 4 centers (never any
// other face's), so it simply toggles exactly that one bit.
static inline int center_mask_delta(int m) {
    switch (m) {
        case 1:  return 1;   // U2
        case 7:  return 2;   // D2
        case 16: return 4;   // L2
        case 10: return 8;   // R2
        case 19: return 16;  // F2
        case 25: return 32;  // B2
        default: return 0;
    }
}

// ---- "Edges+centers" reachable-state closure (884,736 raw -> 110,592 dense)
// 110,592 (not the user's initial 55,296 guess), i.e. only 1/8 of the naive
// space -- see the phase-7 header comment above for the ground-truthed
// derivation (three known parity ties, not four; the missing factor of 2
// links to corners instead, handled at the joint-table level below).
static const int EC_M_NUM = 24, EC_S_NUM = 24, EC_EQ_NUM = 24, EC_CENTER_NUM = 64;
static const int EC_RAW_NUM = EC_M_NUM * EC_S_NUM * EC_EQ_NUM * EC_CENTER_NUM;  // 884,736
static const int EC_DENSE_NUM = 110592;
static inline int ec_raw_index(int m_rank, int s_rank, int eq_rank, int center_mask) {
    return ((m_rank * EC_S_NUM + s_rank) * EC_EQ_NUM + eq_rank) * EC_CENTER_NUM + center_mask;
}
static std::vector<int> EC_DENSE;  // [884736] -> dense id, or -1 (unreachable)
// A vector, not a fixed-size EC_DENSE_NUM array: lets the BFS run to
// completion and report its ACTUAL size even if that size is wrong, instead
// of crashing (a fixed array overflowing silently corrupts memory) -- found
// the hard way, from a real overflow crash during development (see chat).
static std::vector<std::array<int,6>> EC_TRANS;
static void build_ec_reachable() {
    EC_DENSE.assign(EC_RAW_NUM, -1);
    EC_TRANS.clear();
    int start_raw = ec_raw_index(0, 0, 0, 0);
    EC_DENSE[start_raw] = 0;
    EC_TRANS.push_back({});
    int num_found = 1;
    std::queue<int> q;
    q.push(start_raw);
    while (!q.empty()) {
        int cur_raw = q.front(); q.pop();
        int cur_dense = EC_DENSE[cur_raw];
        int center_mask = cur_raw % EC_CENTER_NUM;
        int rem = cur_raw / EC_CENTER_NUM;
        int eq_rank = rem % EC_EQ_NUM; rem /= EC_EQ_NUM;
        int s_rank = rem % EC_S_NUM;
        int m_rank = rem / EC_S_NUM;
        for (int t = 0; t < TIER6_NUM_MOVES; t++) {
            int mv = TIER6_MOVES[t];
            int nm = M_PERM_TRANS[m_rank][t];
            int ns = S_PERM_TRANS[s_rank][t];
            int neq = EQ_PERM_TRANS[eq_rank][t];
            int nmask = center_mask ^ center_mask_delta(mv);
            int nraw = ec_raw_index(nm, ns, neq, nmask);
            if (EC_DENSE[nraw] < 0) {
                EC_DENSE[nraw] = num_found++;
                EC_TRANS.push_back({});
                q.push(nraw);
            }
            EC_TRANS[cur_dense][t] = EC_DENSE[nraw];
        }
    }
    printf("  edges+centers reachable-state closure: found %d states (expected %d)\n", num_found, EC_DENSE_NUM);
    if (num_found != EC_DENSE_NUM) {
        printf("FATAL: edges+centers reachable-state closure found %d states (expected %d)\n", num_found, EC_DENSE_NUM);
        exit(1);
    }
}
static inline int ec_dense_of(const int* wing_slot, const std::array<int,24>& center_slot) {
    int m_rank = slice_perm_rank(wing_slot, M_SLICE_IDX);
    int s_rank = slice_perm_rank(wing_slot, S_SLICE_IDX);
    int eq_rank = slice_perm_rank(wing_slot, EQUATORIAL_IDX);
    int mask = center_mask_of(center_slot);
    return EC_DENSE[ec_raw_index(m_rank, s_rank, eq_rank, mask)];
}

// ---- Corner permutation's rank WITHIN H (0..95) -----------------------------
static int H_TRANS[96][6];
static void build_h_trans() {
    for (int i = 0; i < 96; i++) {
        for (int t = 0; t < TIER6_NUM_MOVES; t++) {
            int m = TIER6_MOVES[t];
            auto np = corner_pos_apply(H_ELEMENTS[i], m);
            int j = H_RANK_FROM_PERM8[rank_perm8(np)];
            if (j < 0) {
                printf("FATAL: H is not closed under its own generator %d -- corner-coset math is wrong\n", m);
                exit(1);
            }
            H_TRANS[i][t] = j;
        }
    }
}
static inline int h_rank_of(const std::array<int,24>& corner_sticker) {
    return H_RANK_FROM_PERM8[rank_perm8(corner_pos_from_sticker(corner_sticker))];
}

// ---- Joint distance-to-solved table (96 x 110,592 = 10,616,832 naive; ------
// ---- exactly half, 5,308,416, is genuinely reachable -- see the header ----
// ---- comment above for the ground-truthed derivation) ----------------------
static const int S7_CORNER_NUM = 96;
static const int S7_EC_NUM = EC_DENSE_NUM;
static inline int s7_joint_index(int corner_h, int ec) { return corner_h * S7_EC_NUM + ec; }

static bool S7_DIST_NIBBLE = true;
static std::vector<uint8_t> S7_DIST_PACKED;
static inline int s7_dist_get(int idx) {
    if (S7_DIST_NIBBLE) {
        uint8_t b = S7_DIST_PACKED[idx / 2];
        return (idx % 2 == 0) ? (b & 0xF) : (b >> 4);
    }
    return S7_DIST_PACKED[idx];
}
static void build_s7_dist() {
    const int N = S7_CORNER_NUM * S7_EC_NUM;
    std::vector<uint8_t> dist(N, 255);
    int start = s7_joint_index(0, 0);
    dist[start] = 0;
    std::queue<int> q;
    q.push(start);
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int ec = cur % S7_EC_NUM;
        int corner = cur / S7_EC_NUM;
        uint8_t d = dist[cur];
        for (int t = 0; t < TIER6_NUM_MOVES; t++) {
            int ncorner = H_TRANS[corner][t];
            int nec = EC_TRANS[ec][t];
            int nidx = s7_joint_index(ncorner, nec);
            if (dist[nidx] == 255) { dist[nidx] = (uint8_t)(d + 1); q.push(nidx); }
        }
    }
    // Per the ground-truthed derivation above: exactly HALF of this naive
    // product is reachable, linked corner-to-edges+centers the same way
    // tier 6's own corner-coset/ud parity was linked. Verified two ways
    // before trusting it: (1) the total unreached count is exactly N/2, and
    // (2) EVERY one of the 96 corner_h values pairs with EXACTLY EC_DENSE_
    // NUM/2 of the edges+centers states (a perfectly balanced split, not
    // just an aggregate half) -- a lopsided per-corner_h split would mean a
    // real bug even if the TOTAL happened to come out to N/2 by coincidence.
    int unreached = 0, maxd = 0;
    long long sum = 0;
    std::vector<int> reached_per_corner(S7_CORNER_NUM, 0);
    for (int corner = 0; corner < S7_CORNER_NUM; corner++) {
        for (int ec = 0; ec < S7_EC_NUM; ec++) {
            uint8_t v = dist[s7_joint_index(corner, ec)];
            if (v == 255) { unreached++; continue; }
            reached_per_corner[corner]++;
            maxd = std::max(maxd, (int)v);
            sum += v;
        }
    }
    printf("  S7 joint dist table: %d entries, unreached=%d, avg=%.4f max=%d\n",
           N, unreached, (double)sum / std::max(1, N - unreached), maxd);
    bool balanced = true;
    for (int corner = 0; corner < S7_CORNER_NUM; corner++) {
        if (reached_per_corner[corner] != S7_EC_NUM / 2) { balanced = false; break; }
    }
    if (unreached != N / 2 || !balanced) {
        printf("FATAL: S7 joint dist table reachability isn't the expected perfectly-balanced half (unreached=%d, expected %d; balanced=%s) -- a real bug, not the ground-truthed parity link\n",
               unreached, N / 2, balanced ? "yes" : "no");
        exit(1);
    }
    printf("  reachability matches the ground-truthed corner/edges+centers parity link exactly (%d unreached, evenly split across all %d corner_h values)\n",
           unreached, S7_CORNER_NUM);
    S7_DIST_NIBBLE = (maxd <= 14);
    if (S7_DIST_NIBBLE) {
        S7_DIST_PACKED.assign((N + 1) / 2, 0);
        for (int i = 0; i < N; i++) {
            uint8_t v = (dist[i] == 255) ? 15 : dist[i];
            if (i % 2 == 0) S7_DIST_PACKED[i / 2] = (uint8_t)((S7_DIST_PACKED[i / 2] & 0xF0) | (v & 0xF));
            else            S7_DIST_PACKED[i / 2] = (uint8_t)((S7_DIST_PACKED[i / 2] & 0x0F) | ((v & 0xF) << 4));
        }
        printf("  packed 2 entries/byte (max reachable dist %d <= 14)\n", maxd);
    } else {
        S7_DIST_PACKED = dist;
        printf("  stored 1 entry/byte (max reachable dist %d)\n", maxd);
    }
}

} // namespace coords
