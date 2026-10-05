#pragma once
#include "s3p.h"

namespace coords {

// =============================== Phase 5 (S4->S5) ============================
// S5 conditions (per the user's spec, see chat): (1) corners oriented (3^7 =
// 2187, the standard corner-twist coordinate -- one of 8 raw twist values is
// determined by the invariant that all 8 sum to 0 mod 3); (2) R and L
// centers each independently "solved or 180 from solved" (2x2 = 4 raw
// values); (3) the 4 equatorial-origin dedges all sit somewhere among the 4
// equatorial POSITIONS (12-choose-4 = 495, not caring which specific one is
// where); (4) a parity tie between the equatorial-edge permutation and how
// many of {L,R,F,B} centers are at an odd (90/270) rotation -- checked ONLY
// when (1)-(3) already hold, since it's otherwise not well-defined.
//
// Move set for this tier's distance table: <U,D,R,L,F2,B2> (14 raw move
// indices below -- all 3 powers of U/D/R/L plus the 2 half turns of F/B).
// This is exactly the move set already established (see "tacit knowledge"
// in CLAUDE.md) to preserve is_in_s4 -- and exactly phase_sizes.py's own S4
// generator set. Distance-to-S5 under these generators is precisely "how
// many more S4-preserving moves are needed", matching the established
// "waypoint + joint distance table" pattern already used one tier up
// (WING4_DIST/CENTER4_DIST, S3'->literal S4).

static const int L_CENTER_SLOTS[4] = {0, 1, 2, 3};
static const int R_CENTER_SLOTS[4] = {20, 21, 22, 23};
static const int F_CENTER_SLOTS[4] = {7, 9, 15, 17};
static const int B_CENTER_SLOTS[4] = {6, 8, 14, 16};
// U and D's own center-piece ids (== solved slots), derived from tables.h's
// UD_TARGET={4,5,10,11,12,13,18,19} the same way L/R/F/B were split out of
// their own combined pairs: sorted by cube_model.CENTER_POS's (x,y,z) tuple
// order, U/D are the two 4-element "y-extreme" subsets of UD_TARGET (the
// F/B-extreme ones, {6,7,8,9,14,15,16,17}, are already accounted for above)
// -- needed starting phase 6 (S5->S6), where U/D become the tier's own
// full-quarter-turn axis (see TIER5_MOVES below), mirroring L/R's role in
// tier 4.
//
// The original derivation assigned {4,5,12,13} to U and {10,11,18,19} to D
// by assumption; that assignment was BACKWARDS (found via chat's phase-7
// h7 self-test: a single real U2 from solved set center_mask bit 1 -- the
// D bit -- instead of bit 0, so center_mask_delta(U2)=1 (see below) landed
// on the wrong bit and desynced the EC BFS built with build_ec_reachable
// from what a live ec_dense_of computes). Fixed by swapping the two arrays'
// values below -- confirmed correct against multiple hand-verified
// TIER6_MOVES sequences (see chat).
static const int U_CENTER_SLOTS[4] = {10, 11, 18, 19};
static const int D_CENTER_SLOTS[4] = {4, 5, 12, 13};

// Permutation parity of a single face's own 4 center pieces (piece ids ==
// the 4 slots above, by this project's "piece k solved at slot k"
// convention). A center piece has exactly 1 extreme coordinate, so it is
// NEVER touched by any move except turns of its own face -- meaning a
// single quarter turn is exactly a 4-cycle (an ODD permutation) applied to
// just these 4 pieces, so this permutation's parity == (quarter-turn count
// applied to this face) mod 2. That makes "solved or 180" exactly "this
// parity is even" -- a plain function of the CURRENT center_slot snapshot,
// needing no separate incremental tracking, exactly like ud2520/lr2520/
// fb24 elsewhere in this file.
static inline int center4_perm_parity(const std::array<int,24>& center_slot, const int slots[4]) {
    int idx_of[24];
    for (int i = 0; i < 4; i++) idx_of[slots[i]] = i;
    int p[4];
    for (int i = 0; i < 4; i++) p[i] = idx_of[center_slot[slots[i]]];
    bool visited[4] = {false, false, false, false};
    int parity = 0;
    for (int i = 0; i < 4; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) { visited[j] = true; j = p[j]; len++; }
        if (len % 2 == 0) parity ^= 1;
    }
    return parity;
}

// Whether a face's own 4 center pieces sit at anything OTHER than the
// identity arrangement. For F and B specifically (used in condition 4, NOT
// condition 2), this is the right question -- NOT center4_perm_parity above.
// Reason (found from a witness example, see chat): F and B centers have
// been restricted to F2/B2 only since S2 (S2 = <U,D,R,L,F2,B2,...> --
// quarter F/B turns are never available again), so once fb24==0 holds
// (is_in_s4's own gate), F's raw state can ONLY be "identity" or "the F2
// double-transposition" -- TWO states, both of which are EVEN permutations,
// so center4_perm_parity (which only sees evenness) returns 0 for BOTH and
// can never tell them apart. L and R, in contrast, remain full-quarter-turn
// capable all the way through tier 4's own move set, so they genuinely
// cycle through all 4 rotation states and DO need the parity question
// (center4_perm_parity is correct there, and is what condition 2 / the
// joint table's own "lr" dimension use).
static inline bool center4_is_shifted(const std::array<int,24>& center_slot, const int slots[4]) {
    for (int i = 0; i < 4; i++) if (center_slot[slots[i]] != slots[i]) return true;
    return false;
}

// ---- Corner orientation (3^7) ----------------------------------------------
// Derived once from tables5.h's CORNER_PERM (24-element sticker permutation)
// + CORNER_POS_PARITY_SIGN: an 8-element POSITION-only permutation and a
// signed twist DELTA per departure position, matching the standard
// Kociemba-style "twist coordinate" -- the delta added to whatever value is
// currently at a departure slot is a fixed function of (move, that slot)
// alone, independent of history or of which physical piece is there
// (verified empirically before trusting this, see chat / generate_tables5.py's
// history-independence check). This is intentionally SEPARATE from the
// State6-tracked corner_sticker array (which exists to make whole-cube-
// rotation conjugation trivial, see tables5.h) -- this position+delta form
// exists only to run the reduced (2187-state) BFS below cheaply.
static int CORNER_POS_PERM[27][8];
static int CORNER_TWIST_DELTA[27][8];
static void init_corner_pos_twist() {
    for (int m = 0; m < 27; m++) {
        for (int s = 0; s < 8; s++) {
            int li = CORNER_PERM[m][s * 3 + 1];
            int dest_pos = li / 3, dest_axis = li % 3;
            CORNER_POS_PERM[m][s] = dest_pos;
            int raw = (dest_axis - 1 + 3) % 3;
            CORNER_TWIST_DELTA[m][s] = ((CORNER_POS_PARITY_SIGN[dest_pos] * raw) % 3 + 3) % 3;
        }
    }
}

static const int CORNER_ORI_NUM = 2187;  // 3^7
static inline int ori_array_to_rank(const int ori[8]) {
    int rank = 0;
    for (int i = 0; i < 7; i++) rank = rank * 3 + ori[i];
    return rank;
}
static inline void ori_rank_to_array(int rank, int ori[8]) {
    int sum = 0;
    for (int i = 6; i >= 0; i--) { ori[i] = rank % 3; rank /= 3; sum += ori[i]; }
    ori[7] = (3 - sum % 3) % 3;
}
// Transition used ONLY to build the reduced (2187-state) BFS -- applies the
// permute+add rule to a full 8-element orientation-by-position snapshot.
static inline int corner_ori_trans(int rank, int m) {
    int ori[8], new_ori[8];
    ori_rank_to_array(rank, ori);
    for (int s = 0; s < 8; s++) new_ori[CORNER_POS_PERM[m][s]] = (ori[s] + CORNER_TWIST_DELTA[m][s]) % 3;
    return ori_array_to_rank(new_ori);
}
// Query from a LIVE corner_sticker snapshot (State6's own tracked array):
// piece k's home axis-1 (Y) sticker has local index k*3+1 by construction,
// so corner_sticker[k*3+1] directly gives where it ends up (mirrors
// signed_ori_from_sticker_perm in generate_tables5.py exactly).
static inline int corner_ori_rank_from_sticker(const std::array<int,24>& corner_sticker) {
    int ori[8];
    for (int k = 0; k < 8; k++) {
        int li = corner_sticker[k * 3 + 1];
        int dest_pos = li / 3, dest_axis = li % 3;
        int raw = (dest_axis - 1 + 3) % 3;
        ori[dest_pos] = ((CORNER_POS_PARITY_SIGN[dest_pos] * raw) % 3 + 3) % 3;
    }
    return ori_array_to_rank(ori);
}

// ---- Equatorial-dedge occupancy (12 choose 4 = 495) ------------------------
// Which 4-subset of the 12 dedge POSITIONS currently holds the 4
// equatorial-ORIGIN dedges (identified via the fixed POS_INDEX/NEG_INDEX
// piece->dedge-id lookup, NOT via compute_pairing -- compute_pairing tracks
// pairing-INTACTNESS, which stays at the identity permutation throughout
// tier 4 by construction, so it can't distinguish which dedge is where; see
// chat for the full derivation).
static const int TIER4_MOVES[14] = {
    0, 1, 2,       // U, U2, U'
    6, 7, 8,       // D, D2, D'
    9, 10, 11,     // R, R2, R'
    15, 16, 17,    // L, L2, L'
    19,            // F2
    25,            // B2
};
static const int TIER4_NUM_MOVES = 14;

static int EQUATOR_MASK_TO_RANK[4096];
static int EQUATOR_RANK_TO_MASK[495];
static int EQUATOR_GOAL_RANK = -1;
static inline int popcount12(int mask) {
    int c = 0;
    for (int i = 0; i < 12; i++) if (mask & (1 << i)) c++;
    return c;
}
static void build_equator_rank_tables() {
    for (int i = 0; i < 4096; i++) EQUATOR_MASK_TO_RANK[i] = -1;
    int next = 0;
    for (int mask = 0; mask < 4096; mask++) {
        if (popcount12(mask) != 4) continue;
        EQUATOR_MASK_TO_RANK[mask] = next;
        EQUATOR_RANK_TO_MASK[next] = mask;
        next++;
    }
    if (next != 495) { printf("FATAL: expected 495 4-of-12 masks, got %d\n", next); exit(1); }
    int goal_mask = 0;
    for (int j = 0; j < 4; j++) goal_mask |= (1 << EQUATORIAL_IDX[j]);
    EQUATOR_GOAL_RANK = EQUATOR_MASK_TO_RANK[goal_mask];
}

static int EQUATOR_POS_TRANS[14][12];  // [t][i] = new position index for content at position i
static int EQUATOR_TRANS[495][14];     // [rank][t] = new rank
static void build_equator_trans() {
    for (int t = 0; t < TIER4_NUM_MOVES; t++) {
        int m = TIER4_MOVES[t];
        for (int i = 0; i < 12; i++) {
            int dest_slot = WING_PERM[m][POS_SLOTS[i]];
            int j = POS_INDEX[dest_slot];
            if (j < 0) {
                printf("FATAL: tier-4 move %d does not preserve the POS/NEG orbit split at position %d\n", m, i);
                exit(1);
            }
            EQUATOR_POS_TRANS[t][i] = j;
        }
    }
    for (int rank = 0; rank < 495; rank++) {
        int mask = EQUATOR_RANK_TO_MASK[rank];
        for (int t = 0; t < TIER4_NUM_MOVES; t++) {
            int new_mask = 0;
            for (int i = 0; i < 12; i++) if (mask & (1 << i)) new_mask |= (1 << EQUATOR_POS_TRANS[t][i]);
            EQUATOR_TRANS[rank][t] = EQUATOR_MASK_TO_RANK[new_mask];
        }
    }
}

static inline int dedge_id_of_piece(int piece) {
    return (POS_INDEX[piece] >= 0) ? POS_INDEX[piece] : NEG_INDEX[piece];
}
static inline int equator_occupancy_rank(const int* wing_slot) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    int mask = 0;
    for (int i = 0; i < 12; i++) {
        int dedge = dedge_id_of_piece(occupant_of_slot[POS_SLOTS[i]]);
        for (int j = 0; j < 4; j++) if (EQUATORIAL_IDX[j] == dedge) { mask |= (1 << i); break; }
    }
    return EQUATOR_MASK_TO_RANK[mask];
}

// Parity of the permutation mapping the 4 equatorial POSITIONS to the 4
// equatorial DEDGES currently occupying them (treated as 4 whole edges, per
// the user's spec) -- well-defined ONLY when equator_occupancy_rank already
// equals EQUATOR_GOAL_RANK (all 4 are colocated with the equatorial slots).
static inline int equator_edge_perm_parity(const int* wing_slot) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    int p[4];
    for (int i = 0; i < 4; i++) {
        int dedge = dedge_id_of_piece(occupant_of_slot[POS_SLOTS[EQUATORIAL_IDX[i]]]);
        int local = -1;
        for (int j = 0; j < 4; j++) if (EQUATORIAL_IDX[j] == dedge) { local = j; break; }
        p[i] = local;
    }
    bool visited[4] = {false, false, false, false};
    int parity = 0;
    for (int i = 0; i < 4; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) { visited[j] = true; j = p[j]; len++; }
        if (len % 2 == 0) parity ^= 1;
    }
    return parity;
}

// ---- Joint distance-to-S5 table (2187 x 4 x 495 = 4,330,260) ---------------
static const int S5_LR_NUM = 4;
static const int S5_EQ_NUM = 495;
static inline int s5_joint_index(int corner_r, int lr, int eq) {
    return (corner_r * S5_LR_NUM + lr) * S5_EQ_NUM + eq;
}
// L-quarter (L or L', NOT L2) flips bit 0; R-quarter flips bit 1; everything
// else (including L2/R2, an EVEN number of quarter turns) leaves both alone.
static inline int lr_delta(int m) {
    bool is_l_quarter = (MOVE_GROUP[m] == 1 && MOVE_RANK[m] == 2 && IS_QUARTER_TURN[m]);
    bool is_r_quarter = (MOVE_GROUP[m] == 1 && MOVE_RANK[m] == 0 && IS_QUARTER_TURN[m]);
    return (is_l_quarter ? 1 : 0) | (is_r_quarter ? 2 : 0);
}

static std::vector<uint8_t> S5_DIST_PACKED;  // nibble-packed: 2 entries/byte
static inline int s5_dist_get(int idx) {
    uint8_t b = S5_DIST_PACKED[idx / 2];
    return (idx % 2 == 0) ? (b & 0xF) : (b >> 4);
}
static int16_t CORNER_ORI_TRANS[CORNER_ORI_NUM][TIER4_NUM_MOVES];   // [rank][t] (filled by build_corner_ori_trans below)
static void build_corner_ori_trans();
static void build_s5_dist() {
    // A level-synchronous BFS over table lookups (the corner twist transition is a precomputed table; the old per-state
    // decode of the twist rank and a std::queue made this 1.2 s, now ~0.1 s).
    build_corner_ori_trans();
    const int N = CORNER_ORI_NUM * S5_LR_NUM * S5_EQ_NUM;
    int lrd[TIER4_NUM_MOVES];
    for (int t = 0; t < TIER4_NUM_MOVES; t++) lrd[t] = lr_delta(TIER4_MOVES[t]);
    std::vector<uint8_t> dist(N, 255);
    const uint32_t start = (uint32_t)s5_joint_index(0, 0, EQUATOR_GOAL_RANK);
    dist[start] = 0;
    // The layers are 1 2 17 142 1169 9230 67465 421488 1694704 1957400 178304 338 (distances 0..11): the two huge ones are
    // not expanded top-down. Layers 0..7 are expanded (this labels layer 8), then layers 9 and 10 are labeled BOTTOM-UP (an
    // unlabeled state with a neighbour in layer L is in layer L+1: the 14 moves are closed under inverses, so neighbour =
    // predecessor), and the 338 states left over are layer 11 (the maximum is fixed). ~2.5x fewer transitions;
    // test_s5_dist checks the whole table against the distance-field characterization.
    const int TOP_DOWN = 7, MAX_DIST = 11;
    std::vector<uint32_t> cur = {start}, next;
    for (int d = 0; !cur.empty() && d <= TOP_DOWN; d++) {
        next.clear();
        for (uint32_t idx : cur) {
            const int eq = (int)(idx % S5_EQ_NUM);
            const int rem = (int)(idx / S5_EQ_NUM);
            const int lr = rem % S5_LR_NUM;
            const int corner_r = rem / S5_LR_NUM;
            for (int t = 0; t < TIER4_NUM_MOVES; t++) {
                const uint32_t nidx = (uint32_t)s5_joint_index(CORNER_ORI_TRANS[corner_r][t], lr ^ lrd[t], EQUATOR_TRANS[eq][t]);
                if (dist[nidx] == 255) { dist[nidx] = (uint8_t)(d + 1); next.push_back(nidx); }
            }
        }
        cur.swap(next);
    }
    for (int L = TOP_DOWN + 1; L < MAX_DIST - 1; L++) {   // L = 8, 9: label L + 1 from the unlabeled states next to layer L
        for (uint32_t idx = 0; idx < (uint32_t)N; idx++) {
            if (dist[idx] != 255) continue;
            const int eq = (int)(idx % S5_EQ_NUM);
            const int rem = (int)(idx / S5_EQ_NUM);
            const int lr = rem % S5_LR_NUM;
            const int corner_r = rem / S5_LR_NUM;
            for (int t = 0; t < TIER4_NUM_MOVES; t++)
                if (dist[s5_joint_index(CORNER_ORI_TRANS[corner_r][t], lr ^ lrd[t], EQUATOR_TRANS[eq][t])] == L) { dist[idx] = (uint8_t)(L + 1); break; }
        }
    }
    for (uint32_t idx = 0; idx < (uint32_t)N; idx++) if (dist[idx] == 255) dist[idx] = (uint8_t)MAX_DIST;
    int unreached = 0, maxd = 0;
    long long sum = 0;
    for (int v : dist) { if (v == 255) unreached++; else { maxd = std::max(maxd, (int)v); sum += v; } }
    printf("  S5 joint dist table: %d entries, unreached=%d, avg=%.4f max=%d\n",
           N, unreached, (double)sum / std::max(1, N - unreached), maxd);
    if (unreached > 0) {
        printf("FATAL: S5 joint dist table has %d unreached states -- move set or table bug\n", unreached);
        exit(1);
    }
    if (maxd > 15) {
        printf("FATAL: max S5 dist %d exceeds 15 -- nibble packing would truncate (not implemented for this case)\n", maxd);
        exit(1);
    }
    S5_DIST_PACKED.assign((N + 1) / 2, 0);
    for (int i = 0; i < N; i++) {
        uint8_t v = dist[i];
        if (i % 2 == 0) S5_DIST_PACKED[i / 2] = (uint8_t)((S5_DIST_PACKED[i / 2] & 0xF0) | (v & 0xF));
        else            S5_DIST_PACKED[i / 2] = (uint8_t)((S5_DIST_PACKED[i / 2] & 0x0F) | ((v & 0xF) << 4));
    }
}

// Per the user's spec: when the joint distance is 0, conditions (1)-(3) hold
// but (4) might still fail -- in which case literal S5 has NOT been
// reached, and the shortest known fix is 5 moves (witnessed: L R' F2 L' R
// from solved satisfies (1)-(3) but not (4)). Only meaningful once (1)-(3)
// already hold.
static const int S5_PARITY_MISMATCH_PENALTY = 5;

// ---------------- Tier-4 (S4->S5) lazy-evaluation transition tables --------
// Per the user, see chat: phase 5's own heuristic/is_in_s5 test depend on 4
// small coordinates, ALL small enough for direct transition tables (unlike
// tier 2's wing pairing) -- corner orientation (2187, already have
// corner_ori_trans as a per-call function; precomputed into a table here for
// O(1) lookup), R/L rotation parity (4, already just an XOR via lr_delta --
// no table needed), a PARITY-AWARE equator-edge coordinate (990 = 495*2,
// extending the existing occupancy-only EQUATOR_TRANS with a permutation-
// parity bit, per the user's exact construction: filter the 12 edge
// positions down to the 4 currently holding an equatorial-origin edge, in
// position order, and take that 4-element sequence's permutation parity),
// and a new sum-of-center-rotations-mod-4 accumulator (4, a fixed per-move
// delta, no table needed either).
static int FULL_TO_TIER4_MOVE_INDEX[NUM_MOVES];
static void build_full_to_tier4_index() {
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_TIER4_MOVE_INDEX[m] = -1;
    for (int t = 0; t < TIER4_NUM_MOVES; t++) FULL_TO_TIER4_MOVE_INDEX[TIER4_MOVES[t]] = t;
}

static void build_corner_ori_trans() {
    for (int r = 0; r < CORNER_ORI_NUM; r++)
        for (int t = 0; t < TIER4_NUM_MOVES; t++)
            CORNER_ORI_TRANS[r][t] = (int16_t)corner_ori_trans(r, TIER4_MOVES[t]);
}

// Per the user: "Any R or L increases by 1 mod 4, any R' or L' decreases by
// 1 mod 4, any R2, L2, F2, B2 increases by 2 mod 4" -- everything else (U/D
// of any power) leaves the sum alone. A fixed per-move constant, no
// transition table needed (per the user's own "if you can implement it more
// simply that's fine too").
static int CENTER_SUM_DELTA[NUM_MOVES];
static void build_center_sum_delta() {
    for (int m = 0; m < NUM_MOVES; m++) {
        std::string name = MOVE_NAMES[m];
        if (name == "R" || name == "L") CENTER_SUM_DELTA[m] = 1;
        else if (name == "R'" || name == "L'") CENTER_SUM_DELTA[m] = 3;
        else if (name == "R2" || name == "L2" || name == "F2" || name == "B2") CENTER_SUM_DELTA[m] = 2;
        else CENTER_SUM_DELTA[m] = 0;
    }
}

// Ground-truth (non-incremental) computation of a single face's own net
// rotation (0..3), by detecting which power of a reference single-quarter
// move on that face reproduces the CURRENT permutation of its 4-piece group,
// starting the reference walk from the identity cube. Needed because
// is_in_s4 does NOT force literal identity on R/L/F/B -- it folds together
// all 4 rotational states of each face (see "A lone R legitimately satisfies
// is_in_s4" in CLAUDE.md), so a real S4 entry can have any of the 4 states
// on R or L (F/B are confined to {0,2} once quarter F/B turns are gone from
// S2 onward, but this function doesn't need to assume that). This mirrors
// the per-move CENTER_PERM update State6::apply_move already uses.
static inline int face_rotation_amount(const std::array<int,24>& center_slot, const int slots[4], int quarter_move) {
    int idx_of[24];
    for (int i = 0; i < 4; i++) idx_of[slots[i]] = i;
    int cur[4];
    for (int i = 0; i < 4; i++) cur[i] = idx_of[center_slot[slots[i]]];
    std::array<int,24> ref;
    for (int i = 0; i < 24; i++) ref[i] = i;
    for (int k = 0; k < 4; k++) {
        int refp[4];
        for (int i = 0; i < 4; i++) refp[i] = idx_of[ref[slots[i]]];
        bool match = true;
        for (int i = 0; i < 4; i++) if (refp[i] != cur[i]) { match = false; break; }
        if (match) return k;
        std::array<int,24> nref;
        for (int i = 0; i < 24; i++) nref[i] = CENTER_PERM[quarter_move][ref[i]];
        ref = nref;
    }
    return 0;  // unreachable for a genuinely S4-confined face group
}

static inline int compute_center_sum(const std::array<int,24>& center_slot) {
    int rR = face_rotation_amount(center_slot, R_CENTER_SLOTS, 9);   // "R"
    int rL = face_rotation_amount(center_slot, L_CENTER_SLOTS, 15);  // "L"
    int rF = face_rotation_amount(center_slot, F_CENTER_SLOTS, 18);  // "F"
    int rB = face_rotation_amount(center_slot, B_CENTER_SLOTS, 24);  // "B"
    return (rR + rL + rF + rB) & 3;
}

// Parity-aware equator-edge coordinate: 495 (occupancy, via the existing
// EQUATOR_MASK_TO_RANK) x 2 (permutation parity of the 4 equator-origin
// edges' relative position order, per the user's exact construction --
// see the worked example in chat: edge-permutation-array [4,6,9,0,11,10,7,
// 2,8,1,3,5] filters to [0,2,1,3], an odd permutation). Well-defined at ANY
// state (not just once occupancy reaches the goal), computed here from a
// raw 4-tuple of positions (pos[k] = current position of the edge
// originally labeled k, i.e. originally at EQUATORIAL_IDX[k]).
static const int EQ_COORD_SIZE = 990;  // 495 * 2
static inline int eq_coord_from_pos(const int pos[4]) {
    int mask = 0;
    for (int k = 0; k < 4; k++) mask |= (1 << pos[k]);
    int occ = EQUATOR_MASK_TO_RANK[mask];
    int label_at[12];
    for (int i = 0; i < 12; i++) label_at[i] = -1;
    for (int k = 0; k < 4; k++) label_at[pos[k]] = k;
    int seq[4], si = 0;
    for (int i = 0; i < 12; i++) if (label_at[i] >= 0) seq[si++] = label_at[i];
    bool visited[4] = {false, false, false, false};
    int parity = 0;
    for (int i = 0; i < 4; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) { visited[j] = true; j = seq[j]; len++; }
        if (len % 2 == 0) parity ^= 1;
    }
    return occ + (parity ? 495 : 0);
}
// Live extraction from a wing_slot[24] array -- same filtering, driven off
// dedge_id_of_piece/EQUATORIAL_IDX directly instead of a raw pos[4] tuple
// (used only once per solve, at the S3'->S4 boundary handoff; the search
// itself only ever uses EQ_PARITY_TRANS thereafter).
static inline int compute_eq_parity_coord(const int* wing_slot) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    int pos[4];
    for (int k = 0; k < 4; k++) {
        int target_dedge = EQUATORIAL_IDX[k];
        // find which of the 12 dedge-positions currently holds dedge k --
        // scan POS_SLOTS since that's the canonical position-index basis
        // used everywhere else in this file (equator_occupancy_rank etc).
        for (int i = 0; i < 12; i++) {
            if (dedge_id_of_piece(occupant_of_slot[POS_SLOTS[i]]) == target_dedge) { pos[k] = i; break; }
        }
    }
    return eq_coord_from_pos(pos);
}

static int16_t EQ_PARITY_TRANS[EQ_COORD_SIZE][TIER4_NUM_MOVES];
static void build_eq_parity_trans() {
    for (int i = 0; i < EQ_COORD_SIZE; i++)
        for (int t = 0; t < TIER4_NUM_MOVES; t++) EQ_PARITY_TRANS[i][t] = -1;
    // Exhaustive BFS over RAW 4-position-tuples (not just one representative
    // per coordinate value) -- self-verifying: if two different raw tuples
    // sharing a coordinate value ever disagreed about where a move sends
    // them, that would mean the (occupancy,parity) coordinate isn't actually
    // well-defined under tier-4 moves, and this catches it directly rather
    // than assuming it.
    std::vector<bool> visited_raw(12 * 12 * 12 * 12, false);
    auto raw_key = [](const std::array<int,4>& p) { return ((p[0] * 12 + p[1]) * 12 + p[2]) * 12 + p[3]; };
    std::array<int,4> start;
    for (int k = 0; k < 4; k++) start[k] = EQUATORIAL_IDX[k];
    visited_raw[raw_key(start)] = true;
    std::queue<std::array<int,4>> q;
    q.push(start);
    int conflicts = 0, raw_count = 0;
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        raw_count++;
        int cur_coord = eq_coord_from_pos(cur.data());
        for (int t = 0; t < TIER4_NUM_MOVES; t++) {
            std::array<int,4> nxt;
            for (int k = 0; k < 4; k++) nxt[k] = EQUATOR_POS_TRANS[t][cur[k]];
            int ncoord = eq_coord_from_pos(nxt.data());
            if (EQ_PARITY_TRANS[cur_coord][t] == -1) EQ_PARITY_TRANS[cur_coord][t] = (int16_t)ncoord;
            else if (EQ_PARITY_TRANS[cur_coord][t] != ncoord) conflicts++;
            int nk = raw_key(nxt);
            if (!visited_raw[nk]) { visited_raw[nk] = true; q.push(nxt); }
        }
    }
    printf("  eq-parity coordinate: BFS visited %d/11880 raw arrangements, %d transition conflicts\n", raw_count, conflicts);
    if (conflicts > 0) {
        printf("FATAL: EQ_PARITY_TRANS has %d inconsistent transitions -- the (occupancy,parity) coordinate is not well-defined under tier-4 moves as computed\n", conflicts);
        exit(1);
    }
}

} // namespace coords
