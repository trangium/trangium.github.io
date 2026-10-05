#pragma once
#include "coords.h"

namespace coords {

// ---------------- wing-pairing SIGMA_POS/NEG for the 21 P2 moves -----------
static int POS_INDEX[24], NEG_INDEX[24];
static int16_t SIGMA_POS_P2[P2_NUM_MOVES][12];
static int16_t SIGMA_NEG_P2[P2_NUM_MOVES][12];

static const int S2_OWN_MOVES_PM[17] = {0,1,2, 3,4,5, 6,7,8, 9,10,11, 13, 16, 18,19,20};
static const int S3PRIME_GOOD_MOVES_PM[13] = {0,1,2, 3,4,5, 7, 10, 13, 16, 18,19,20};

static void build_sigma_p2() {
    for (int i = 0; i < 24; i++) { POS_INDEX[i] = -1; NEG_INDEX[i] = -1; }
    for (int i = 0; i < 12; i++) { POS_INDEX[POS_SLOTS[i]] = i; NEG_INDEX[NEG_OF[i]] = i; }
    for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
        int m = P2_TO_FULL_MOVE_INDEX[pm];
        for (int i = 0; i < 12; i++) {
            SIGMA_POS_P2[pm][i] = (int16_t)POS_INDEX[WING_PERM[m][POS_SLOTS[i]]];
            SIGMA_NEG_P2[pm][i] = (int16_t)NEG_INDEX[WING_PERM[m][NEG_OF[i]]];
        }
    }
}
static inline std::array<uint8_t,12> apply_move_to_perm12_p2(const std::array<uint8_t,12>& perm, int pm) {
    std::array<uint8_t,12> out;
    for (int i=0;i<12;i++) out[SIGMA_POS_P2[pm][i]] = (uint8_t)SIGMA_NEG_P2[pm][perm[i]];
    return out;
}

static inline std::array<uint8_t,12> compute_pairing(const int* wing_slot) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    std::array<uint8_t,12> perm;
    for (int i = 0; i < 12; i++) {
        int occupant = occupant_of_slot[POS_SLOTS[i]];
        int twin_piece = WING_ADJ[occupant];
        int twin_slot = wing_slot[twin_piece];
        perm[i] = (uint8_t)NEG_INDEX[twin_slot];
    }
    return perm;
}

static const uint32_t PAIRING_NUM_DENSE = 239500800;  // 12!/2
static const int CENTER_LRFB_SIZE = 2520 * 24;
static inline int center_idx(int lr, int fb) { return lr * 24 + fb; }

// ---------------- Step 1: "S3'-good" reachability (restricted move set) ----
static std::vector<bool> WING_GOOD;
static std::vector<std::array<uint8_t,12>> GOOD_WING_PERMS;

static void build_wing_good() {
    WING_GOOD.assign((size_t)PAIRING_NUM_DENSE, false);
    GOOD_WING_PERMS.clear();
    std::array<uint8_t,12> solved = {0,1,2,3,4,5,6,7,8,9,10,11};
    uint32_t solved_di = pairing_dense_index(pairing_rank_full(solved));
    WING_GOOD[solved_di] = true;
    GOOD_WING_PERMS.push_back(solved);
    std::queue<std::array<uint8_t,12>> q;
    q.push(solved);
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (int pm : S3PRIME_GOOD_MOVES_PM) {
            auto nxt = apply_move_to_perm12_p2(cur, pm);
            uint32_t ndi = pairing_dense_index(pairing_rank_full(nxt));
            if (!WING_GOOD[ndi]) { WING_GOOD[ndi] = true; GOOD_WING_PERMS.push_back(nxt); q.push(nxt); }
        }
    }
}

static std::vector<bool> CENTER_GOOD;

static void build_center_good() {
    CENTER_GOOD.assign(CENTER_LRFB_SIZE, false);
    int start = center_idx(0, 0);
    CENTER_GOOD[start] = true;
    std::queue<int> q; q.push(start);
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int lr = cur / 24, fb = cur % 24;
        for (int pm : S3PRIME_GOOD_MOVES_PM) {
            int nlr = LR2520_TRANS[lr][pm];
            int nfb = FB24_TRANS[fb][pm];
            if (nfb < 0) continue;
            int nidx = center_idx(nlr, nfb);
            if (!CENTER_GOOD[nidx]) { CENTER_GOOD[nidx] = true; q.push(nidx); }
        }
    }
}

// (The distance-to-S3' tables of the earlier normal route -- a 120 MB wing-pairing table and a center table -- are
// gone: S3' is reached through the switch route of s2switch.h. See archive/cpp/s3_route_normal_and_both.)

// ---------------- wing layer / equatorial split ------------------------------
//
// The 12 wing-pairing coordinate slots (POS_SLOTS[0..11]) correspond 1:1 to
// the cube's 12 edges. 8 of them are "layer" edges (4 touching U, 4 touching
// D); the other 4 are "equatorial" edges (FR/FL/BR/BL, touching neither U
// nor D). Identified COMPUTATIONALLY (not by hand): apply Uw2 (the E-slice
// half-turn, which by construction only ever moves equatorial-layer
// material) to the identity pairing state -- whichever of the 12 positions
// changed value are exactly the 4 equatorial ones; the other 8 (unchanged)
// are the layer ones. This split is preserved by every move in the
// restricted 13-move S3' set (checked by the S3' coordinate builders in s4_coords.h, which would
// discover a mixed-up split as a wrong-sized coordinate and abort).
static int LAYER_IDX[8], EQUATORIAL_IDX[4];

static void init_layer_equatorial_split() {
    std::array<uint8_t,12> id = {0,1,2,3,4,5,6,7,8,9,10,11};
    auto after = apply_move_to_perm12_p2(id, 18);  // pm=18 -> Uw2
    int nl = 0, ne = 0;
    for (int i = 0; i < 12; i++) {
        if (after[i] == id[i]) { LAYER_IDX[nl++] = i; }
        else { EQUATORIAL_IDX[ne++] = i; }
    }
    if (nl != 8 || ne != 4) {
        printf("FATAL: layer/equatorial split under Uw2 gave %d layer / %d equatorial (expected 8/4)\n", nl, ne);
        exit(1);
    }
}

// Layer VALUES are exactly the same 8 indices as LAYER_IDX (a layer position
// always holds a layer piece, since the split is move-set-invariant and
// trivially true at solved) -- local-relabeled to 0..7 for standard
// permutation ranking via coords::rank_perm8/unrank_perm8.
static int LAYER_VALUE_LOCAL[12];
static void init_layer_value_local() {
    for (int i = 0; i < 12; i++) LAYER_VALUE_LOCAL[i] = -1;
    for (int i = 0; i < 8; i++) LAYER_VALUE_LOCAL[LAYER_IDX[i]] = i;
}
static inline int layer_rank_of(const std::array<uint8_t,12>& perm) {
    std::array<uint8_t,8> t;
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)LAYER_VALUE_LOCAL[perm[LAYER_IDX[i]]];
    return rank_perm8(t);
}

} // namespace coords
