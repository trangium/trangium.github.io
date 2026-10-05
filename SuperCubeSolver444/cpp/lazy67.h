#pragma once
#include "s7.h"

namespace coords {

// =========== Phase 6+7 lazy evaluation: ID-packed composite coordinates =====
// Per the user's own spec (see chat/CLAUDE.md "Phase 6+7 lazy evaluation"):
// pack each tier-5-tracked coordinate so it carries BOTH the information
// tier 5's own S6 heuristic needs AND the information tier 6/7 need after
// the S5->S6 handoff -- so that handoff never has to re-Lehmer-rank
// anything from a raw cube state (the whole point: S5 is visited far more
// than any other tier's transient dwell, ~6:1 vs ~1000:1 elsewhere, so a
// full-cube retrace right at that boundary is the one place worth avoiding
// entirely, not just speeding up).

// ---- Composite corner ID: coset (lower 9 bits) + within-coset index -------
// (upper 7 bits) -- every right coset of H has EXACTLY 96 elements
// (Lagrange), so "which of the 96" always fits 7 bits, for every coset, not
// just the solved one. Built via the SAME right-multiply-by-H enumeration
// build_corner_coset_table already uses (P * h_list[i]) -- applied to EVERY
// coset's own representative, not just coset 0's -- which automatically
// reproduces H_RANK_FROM_PERM8 exactly for coset 0 (its representative is
// the identity permutation, so identity*h_list[i] = h_list[i], giving local
// index i for h_list[i] -- exactly H_RANK_FROM_PERM8's own definition) while
// giving every OTHER coset an equally well-defined (if arbitrary) internal
// numbering, needed only to keep the composite ID one-to-one.
static const int COMPOSITE_CORNER_RANGE = 49152;  // 512 (9-bit coset slot) * 96
static int32_t COMPOSITE_CORNER_ID[40320];
static std::array<uint8_t,8> COMPOSITE_CORNER_PERM[COMPOSITE_CORNER_RANGE];

static void build_composite_corner_id() {
    for (int c = 0; c < CORNER_COSET_NUM; c++) {
        const auto& P = CORNER_COSET_REP[c];
        for (int i = 0; i < 96; i++) {
            std::array<uint8_t,8> result;
            for (int k = 0; k < 8; k++) result[k] = P[H_ELEMENTS[i][k]];
            int r2 = rank_perm8(result);
            int id = c + i * 512;
            COMPOSITE_CORNER_ID[r2] = id;
            COMPOSITE_CORNER_PERM[id] = result;
        }
    }
    // Verify one-to-one (every one of the 40320 raw ranks got a DISTINCT
    // id) and that coset 0's upper bits exactly match H_RANK_FROM_PERM8, per
    // the user's own condition 3 -- both checked directly, not just argued.
    std::vector<int> seen(COMPOSITE_CORNER_RANGE, -1);
    int distinct = 0;
    for (int r = 0; r < 40320; r++) {
        int id = COMPOSITE_CORNER_ID[r];
        if (seen[id] >= 0) {
            printf("FATAL: compositeCornerID collision: ranks %d and %d both map to id %d\n", seen[id], r, id);
            exit(1);
        }
        seen[id] = r;
        distinct++;
    }
    if (distinct != 40320) { printf("FATAL: compositeCornerID is not one-to-one (%d distinct ids, expected 40320)\n", distinct); exit(1); }
    int h_mismatches = 0;
    for (int i = 0; i < 96; i++) {
        int r = rank_perm8(H_ELEMENTS[i]);
        int id = COMPOSITE_CORNER_ID[r];
        if ((id & 511) != 0 || (id >> 9) != H_RANK_FROM_PERM8[r] || H_RANK_FROM_PERM8[r] != i) h_mismatches++;
    }
    if (h_mismatches > 0) {
        printf("FATAL: compositeCornerID's upper bits don't match H_RANK_FROM_PERM8 on the solved coset (%d mismatches)\n", h_mismatches);
        exit(1);
    }
    printf("  compositeCornerID: 40320/40320 raw permutations packed 1:1 into [0,%d), solved-coset upper bits verified against H_RANK_FROM_PERM8\n", COMPOSITE_CORNER_RANGE);
}

static int32_t COMPOSITE_CORNER_TRANS[COMPOSITE_CORNER_RANGE][TIER5_NUM_MOVES];
static void build_composite_corner_trans() {
    for (int r = 0; r < 40320; r++) {
        int id = COMPOSITE_CORNER_ID[r];
        const auto& P = COMPOSITE_CORNER_PERM[id];
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            int m = TIER5_MOVES[t];
            auto np = corner_pos_apply(P, m);
            COMPOSITE_CORNER_TRANS[id][t] = COMPOSITE_CORNER_ID[rank_perm8(np)];
        }
    }
    // Cross-check against the already-verified CORNER_COSET_TRANS: the lower
    // 9 bits of a composite transition must always match the coset-only
    // transition, for every valid id and every tier-5 move.
    int mismatches = 0;
    for (int r = 0; r < 40320; r++) {
        int id = COMPOSITE_CORNER_ID[r];
        int coset = id & 511;
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            if ((COMPOSITE_CORNER_TRANS[id][t] & 511) != CORNER_COSET_TRANS[coset][t]) mismatches++;
        }
    }
    if (mismatches > 0) {
        printf("FATAL: compositeCornerID transition's coset bits disagree with CORNER_COSET_TRANS (%d mismatches)\n", mismatches);
        exit(1);
    }
    printf("  compositeCornerID transition: 403200/403200 checked, coset bits match CORNER_COSET_TRANS exactly\n");
}

// ---- M-slice / S-slice composite edge coordinates --------------------------
// Packs each slice's own 8-choose-4 occupancy rank (0-69, MSLICE_MASK_TO_
// RANK's own domain) with a "phase-6-ID" (0-23, permutation of the slice's 4
// labeled dedges relative to the slice's own 4 home positions) -- per the
// user's spec. The permutation half is well-defined ONLY once occupancy is
// exactly at goal (that IS S6's own condition 2) -- but per the user's own
// design goal, it must be tracked continuously from S5 entry onward, long
// before occupancy reaches goal.
//
// Resolved the same way tier 4's eq_coord was: track the RAW, piece-indexed
// LOCAL-LAYER-position tuple of the slice's 4 labeled dedges (values 0..7,
// exhaustively enumerated over all 8*7*6*5=1680 ordered 4-of-8 tuples --
// P(8,4), the exact analog of tier 4's P(12,4)=11880), which is
// UNCONDITIONALLY well-defined (it's just "where is piece j", never
// undefined, regardless of confinement). The reported/packed composite
// value (occupancy + order) is a per-tuple LOOKUP, computed once at
// table-build time -- occupancy always meaningful; "order" is a fixed
// placeholder (0) whenever occupancy isn't yet at goal, since it's provably
// never consulted off-goal (every place the order half is read is gated on
// occupancy==goal already holding).
//
// The TRANSITION rule for "a tuple of 4 local-layer-positions under a move"
// is IDENTICAL regardless of WHICH 4 pieces are being tracked (it's purely a
// function of the raw tuple values and the move's own MSLICE_POS_TRANS), so
// ONE shared 1680-entry transition table serves BOTH the M-tuple and the
// S-tuple coordinates -- only the two COMPOSITE lookup tables (interpreting
// "home positions" differently) need to be separate.
static const int SLICE4_TUPLE_NUM = 8 * 7 * 6 * 5;  // 1680
static int TUPLE4OF8_RANK_OF[8 * 8 * 8 * 8];         // base-8-digit encoding -> rank, -1 if invalid
static std::array<int,4> TUPLE4OF8_LIST[SLICE4_TUPLE_NUM];
static void build_tuple4of8_tables() {
    for (int i = 0; i < 8 * 8 * 8 * 8; i++) TUPLE4OF8_RANK_OF[i] = -1;
    int next = 0;
    for (int a = 0; a < 8; a++)
        for (int b = 0; b < 8; b++) { if (b == a) continue;
            for (int c = 0; c < 8; c++) { if (c == a || c == b) continue;
                for (int d = 0; d < 8; d++) { if (d == a || d == b || d == c) continue;
                    int code = ((a * 8 + b) * 8 + c) * 8 + d;
                    TUPLE4OF8_RANK_OF[code] = next;
                    TUPLE4OF8_LIST[next] = {a, b, c, d};
                    next++;
                }
            }
        }
    if (next != SLICE4_TUPLE_NUM) { printf("FATAL: expected %d 4-of-8 ordered tuples, got %d\n", SLICE4_TUPLE_NUM, next); exit(1); }
}
static inline int tuple4of8_rank(const std::array<int,4>& t) {
    int code = ((t[0] * 8 + t[1]) * 8 + t[2]) * 8 + t[3];
    return TUPLE4OF8_RANK_OF[code];
}

static int M_SLICE_LOCAL[4], S_SLICE_LOCAL[4];
static int S_SLICE_GOAL_RANK = -1;
// `goal_rank` is THIS slice's own occupancy-at-home rank (MSLICE_GOAL_RANK
// for M, S_SLICE_GOAL_RANK for S) -- NOT hardcoded to MSLICE_GOAL_RANK
// unconditionally, which was a real bug (see chat): the S-slice's own home
// mask maps to a DIFFERENT rank than the M-slice's, so comparing against
// the wrong constant meant SLICE4_COMPOSITE_S's "order" half almost never
// actually computed (silently defaulting to the placeholder 0), corrupting
// the S-slice's own rank fed into the S6->S7 handoff.
static inline int slice4_composite_of(const std::array<int,4>& pos, const int slice_local[4], int goal_rank) {
    int mask = 0;
    for (int j = 0; j < 4; j++) mask |= (1 << pos[j]);
    int occ = MSLICE_MASK_TO_RANK[mask];
    int order = 0;
    if (occ == goal_rank) {
        std::array<uint8_t,4> p;
        for (int i = 0; i < 4; i++) {
            int home = slice_local[i];
            int j = -1;
            for (int k = 0; k < 4; k++) if (pos[k] == home) { j = k; break; }
            p[i] = (uint8_t)j;
        }
        order = rank_perm4(p);
    }
    return order | (occ << 5);
}

static int SLICE4_TRANS[SLICE4_TUPLE_NUM][TIER5_NUM_MOVES];
static int SLICE4_COMPOSITE_M[SLICE4_TUPLE_NUM];
static int SLICE4_COMPOSITE_S[SLICE4_TUPLE_NUM];
static void build_slice4_composite_tables() {
    for (int i = 0; i < 4; i++) {
        M_SLICE_LOCAL[i] = LAYER_VALUE_LOCAL[M_SLICE_IDX[i]];
        S_SLICE_LOCAL[i] = LAYER_VALUE_LOCAL[S_SLICE_IDX[i]];
    }
    int s_home_mask = 0;
    for (int i = 0; i < 4; i++) s_home_mask |= (1 << S_SLICE_LOCAL[i]);
    S_SLICE_GOAL_RANK = MSLICE_MASK_TO_RANK[s_home_mask];
    for (int r = 0; r < SLICE4_TUPLE_NUM; r++) {
        const auto& tup = TUPLE4OF8_LIST[r];
        SLICE4_COMPOSITE_M[r] = slice4_composite_of(tup, M_SLICE_LOCAL, MSLICE_GOAL_RANK);
        SLICE4_COMPOSITE_S[r] = slice4_composite_of(tup, S_SLICE_LOCAL, S_SLICE_GOAL_RANK);
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            std::array<int,4> ntup;
            for (int j = 0; j < 4; j++) ntup[j] = MSLICE_POS_TRANS[t][tup[j]];
            SLICE4_TRANS[r][t] = tuple4of8_rank(ntup);
        }
    }
    // Sanity: the M-slice's own home tuple must report occupancy==GOAL
    // (M_SLICE_IDX's own mask, by construction) with order==0 (identity);
    // the S-slice's own home tuple must report order==0 too, at WHATEVER
    // occupancy rank corresponds to S_SLICE_IDX's own (different) mask --
    // NOT MSLICE_GOAL_RANK, which is specifically the M-slice's rank. Catches
    // an index/convention mismatch immediately rather than only showing up
    // as a hard-to-diagnose search-time bug later.
    std::array<int,4> m_home, s_home;
    for (int i = 0; i < 4; i++) { m_home[i] = M_SLICE_LOCAL[i]; s_home[i] = S_SLICE_LOCAL[i]; }
    int s_goal_rank = S_SLICE_GOAL_RANK;
    int m_home_composite = SLICE4_COMPOSITE_M[tuple4of8_rank(m_home)];
    int s_home_composite = SLICE4_COMPOSITE_S[tuple4of8_rank(s_home)];
    if (m_home_composite != MSLICE_GOAL_RANK * 32 || s_home_composite != s_goal_rank * 32) {
        printf("FATAL: M/S-slice composite home-tuple sanity check failed (m=%d expected %d; s=%d expected %d)\n",
               m_home_composite, MSLICE_GOAL_RANK * 32, s_home_composite, s_goal_rank * 32);
        exit(1);
    }
    printf("  M/S-slice composite tables: %d raw tuples, home-tuple sanity check passed\n", SLICE4_TUPLE_NUM);
}

static int PERM4_PARITY_OF_RANK[24];
static void build_perm4_parity() {
    for (int r = 0; r < 24; r++) {
        auto p = unrank_perm4(r);
        bool visited[4] = {false, false, false, false};
        int parity = 0;
        for (int i = 0; i < 4; i++) {
            if (visited[i]) continue;
            int j = i, len = 0;
            while (!visited[j]) { visited[j] = true; j = p[j]; len++; }
            if (len % 2 == 0) parity ^= 1;
        }
        PERM4_PARITY_OF_RANK[r] = parity;
    }
}

// ---- E-slice (equatorial) full permutation under tier 5's own move set ----
// Unlike M/S, the equatorial occupancy is ALREADY exact continuously from S5
// entry onward (that's S5's own top-level condition), so this is a
// straightforward 24-state full-permutation tracker -- no raw-tuple
// indirection needed. TIER5_MOVES has 10 entries (vs TIER6_MOVES's existing
// 6-move EQ_PERM_TRANS), so this needs its own transition table; U/U'/D/D'
// turn out to be no-ops on it (equatorial pieces never touch the U/D layer),
// but are still given real (identity) entries for uniformity.
static int EQ_SLICE_POS_TRANS5[TIER5_NUM_MOVES][4];
static int EQ_PERM_TRANS5[24][TIER5_NUM_MOVES];
static void build_eq_perm_trans5() {
    for (int t = 0; t < TIER5_NUM_MOVES; t++) {
        int m = TIER5_MOVES[t];
        for (int i = 0; i < 4; i++) {
            int dest_slot = WING_PERM[m][POS_SLOTS[EQUATORIAL_IDX[i]]];
            int j = POS_INDEX[dest_slot];
            int local = -1;
            for (int k = 0; k < 4; k++) if (EQUATORIAL_IDX[k] == j) { local = k; break; }
            if (local < 0) { printf("FATAL: tier-5 move %d does not preserve the equatorial slice\n", m); exit(1); }
            EQ_SLICE_POS_TRANS5[t][i] = local;
        }
    }
    for (int r = 0; r < 24; r++)
        for (int t = 0; t < TIER5_NUM_MOVES; t++)
            EQ_PERM_TRANS5[r][t] = slice_perm_trans(r, EQ_SLICE_POS_TRANS5[t]);
}

// ---- 8-bit "center rotation" byte for tier 5 -------------------------------
// Per the user's spec: 2 bits each for U and D (full mod-4 rotation --
// needed because U/D remain full-quarter-turn-active throughout tier 5's own
// move set, exactly the reason tier 4 needed a full mod-4 center_sum for
// R/L specifically), 1 bit each for R,L,F,B (plain "is this face at 180"
// toggle -- they're all confined to solved-or-180 by this tier, so a half
// turn of a given face is always a clean toggle between those two states).
// Bit layout (arbitrary, never observed externally, just fixed and
// self-consistent between extraction and the delta tables): bits 0-1 = U
// mod-4, bits 2-3 = D mod-4, bit 4 = R shifted, bit 5 = L shifted, bit 6 = F
// shifted, bit 7 = B shifted.
static int U_ROT_DELTA[NUM_MOVES], D_ROT_DELTA[NUM_MOVES];
static int CENTER_BYTE_TOGGLE[NUM_MOVES];  // XOR mask, already shifted into bits 4-7
static void build_center_byte_delta() {
    for (int m = 0; m < NUM_MOVES; m++) {
        std::string name = MOVE_NAMES[m];
        U_ROT_DELTA[m] = (name == "U") ? 1 : (name == "U'") ? 3 : (name == "U2") ? 2 : 0;
        D_ROT_DELTA[m] = (name == "D") ? 1 : (name == "D'") ? 3 : (name == "D2") ? 2 : 0;
        CENTER_BYTE_TOGGLE[m] = (name == "R2") ? (1 << 4) : (name == "L2") ? (1 << 5) :
                                  (name == "F2") ? (1 << 6) : (name == "B2") ? (1 << 7) : 0;
    }
}
static inline uint8_t apply_center_byte(uint8_t b, int m) {
    int u = ((b & 3) + U_ROT_DELTA[m]) & 3;
    int d = (((b >> 2) & 3) + D_ROT_DELTA[m]) & 3;
    int rlfb = ((b >> 4) & 0xF) ^ (CENTER_BYTE_TOGGLE[m] >> 4);
    return (uint8_t)(u | (d << 2) | (rlfb << 4));
}
static inline uint8_t compute_center_byte(const std::array<int,24>& center_slot) {
    int u = face_rotation_amount(center_slot, U_CENTER_SLOTS, 0);   // "U"
    int d = face_rotation_amount(center_slot, D_CENTER_SLOTS, 6);   // "D"
    bool rs = center4_is_shifted(center_slot, R_CENTER_SLOTS);
    bool ls = center4_is_shifted(center_slot, L_CENTER_SLOTS);
    bool fs = center4_is_shifted(center_slot, F_CENTER_SLOTS);
    bool bs = center4_is_shifted(center_slot, B_CENTER_SLOTS);
    return (uint8_t)(u | (d << 2) | ((rs ? 1 : 0) << 4) | ((ls ? 1 : 0) << 5) | ((fs ? 1 : 0) << 6) | ((bs ? 1 : 0) << 7));
}

// ---- Reverse move-index lookups for tiers 5 and 6 (see FULL_TO_TIER4_MOVE_
// INDEX one tier up for the same pattern) -----------------------------------
static int FULL_TO_TIER5_MOVE_INDEX[NUM_MOVES];
static void build_full_to_tier5_index() {
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_TIER5_MOVE_INDEX[m] = -1;
    for (int t = 0; t < TIER5_NUM_MOVES; t++) FULL_TO_TIER5_MOVE_INDEX[TIER5_MOVES[t]] = t;
}
static int FULL_TO_TIER6_MOVE_INDEX[NUM_MOVES];
static void build_full_to_tier6_index() {
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_TIER6_MOVE_INDEX[m] = -1;
    for (int t = 0; t < TIER6_NUM_MOVES; t++) FULL_TO_TIER6_MOVE_INDEX[TIER6_MOVES[t]] = t;
}


} // namespace coords
