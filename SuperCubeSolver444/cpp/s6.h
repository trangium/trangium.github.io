#pragma once
#include "s5.h"

namespace coords {

// =============================== Phase 6 (S5->S6) ============================
// S6 conditions (per the user's spec, see chat): (1) corners reach the
// literal <U2,D2,R2,L2,F2,B2> group -- 8! permutations reduced to 420
// cosets of that order-96 subgroup, within the AMBIENT group <U,D,R2,L2,
// F2,B2> (order 40320 = all of S8, i.e. this tier's own move set below);
// (2) all edges in their slice -- equivalently the 4 M-slice-origin dedges
// (UF/UB/DF/DB) sit somewhere among the 4 M-slice POSITIONS within the 8
// layer positions (8-choose-4 = 70; the 4 S-slice ones then automatically
// fill the other 4 by pigeonhole); (3) U and R centers -- no, U and D
// centers each independently "solved or 180" (2x2 = 4 raw values, mirroring
// L/R's role one tier up); (4) TWO parity ties, checked only once (1)-(3)
// hold: the M-slice edge permutation's parity must match how many of
// {U,F,D,B} are at 180, and separately the S-slice edge permutation's
// parity must match how many of {U,R,D,L} are at 180.
//
// Move set for this tier's distance table: <U,D,R2,L2,F2,B2> (10 raw move
// indices below -- all 3 powers of U/D, plus the 2 halves of R/L/F/B). This
// is exactly the move set that preserves is_in_s5 (U/D quarters never
// retwist corners or break the layer/equatorial edge split; R2/L2/F2/B2 --
// all halves -- never retwist corners either, the classical reason
// Kociemba's own G1=<U,D,R2,L2,F2,B2> is corner-orientation-preserving) --
// and it is ALSO literally the ambient group the corner-coset table (condition
// 1) is defined against, so both pieces of this tier reuse one move set.
static const int TIER5_MOVES[10] = {
    0, 1, 2,     // U, U2, U'
    6, 7, 8,     // D, D2, D'
    10,          // R2
    16,          // L2
    19,          // F2
    25,          // B2
};
static const int TIER5_NUM_MOVES = 10;

// U-quarter (U or U', NOT U2) flips bit 0; D-quarter flips bit 1; everything
// else (including U2/D2, an EVEN number of quarter turns) leaves both alone
// -- exact mirror of lr_delta above, needed now that U/D are this tier's own
// full-quarter-turn axis (L/R and F/B are all halves-only by this point, so
// they need center4_is_shifted instead, not a parity coordinate -- see
// s6_dist_of).
static inline int ud_delta(int m) {
    bool is_u_quarter = (MOVE_GROUP[m] == 0 && MOVE_RANK[m] == 0 && IS_QUARTER_TURN[m]);
    bool is_d_quarter = (MOVE_GROUP[m] == 0 && MOVE_RANK[m] == 2 && IS_QUARTER_TURN[m]);
    return (is_u_quarter ? 1 : 0) | (is_d_quarter ? 2 : 0);
}

// ---- M-slice / S-slice split of the 8 layer edges --------------------------
// Among the 8 "layer" dedge positions (LAYER_IDX, established one tier up),
// 4 are "M-slice" (UF/UB/DF/DB -- untouched by R/L, only ever reachable via
// U/D/F/B) and 4 are "S-slice" (UR/UL/DR/DL -- untouched by F/B). Identified
// COMPUTATIONALLY via a genuine LIVE position transition (mirroring
// EQUATOR_POS_TRANS's own recipe below: WING_PERM[m][POS_SLOTS[i]] then
// POS_INDEX to get the destination position index) applied to R2 and L2 --
// whichever LAYER positions either one displaces are exactly the 4 S-slice
// ones (R2 touches its own 2, UR/DR; L2 the other 2, UL/DL); the remaining 4
// are M-slice. Cross-checked against F2/B2 (which should displace exactly
// the complementary 4) before trusting it.
//
// NOT the apply_move_to_perm12_p2/compute_pairing machinery used for LAYER_
// IDX/EQUATORIAL_IDX one tier up -- that tracks PAIRING INTACTNESS (whether
// a dedge's two wing stickers are still adjacent), which only a WIDE move
// like Uw2 can disturb; a plain OUTER move like R2 or L2 never breaks
// pairing at all (both stickers of every dedge it touches move together as
// a rigid unit), so it left every pairing-coordinate entry unchanged --
// found the hard way, from a build-time FATAL reporting 0 S-slice positions
// under that approach (see chat).
static int M_SLICE_IDX[4], S_SLICE_IDX[4];

static void init_mslice_split() {
    bool is_s[12] = {false};
    for (int i = 0; i < 12; i++) {
        int dest_r2 = POS_INDEX[WING_PERM[10][POS_SLOTS[i]]];  // full move idx 10 = R2
        int dest_l2 = POS_INDEX[WING_PERM[16][POS_SLOTS[i]]];  // full move idx 16 = L2
        if (dest_r2 != i || dest_l2 != i) is_s[i] = true;
    }
    int nm = 0, ns = 0;
    for (int i = 0; i < 8; i++) {
        int li = LAYER_IDX[i];
        if (is_s[li]) S_SLICE_IDX[ns++] = li; else M_SLICE_IDX[nm++] = li;
    }
    if (nm != 4 || ns != 4) {
        printf("FATAL: M/S-slice split under R2/L2 gave %d M-slice / %d S-slice (expected 4/4)\n", nm, ns);
        exit(1);
    }
    for (int i = 0; i < 4; i++) {
        int li = M_SLICE_IDX[i];
        int dest_f2 = POS_INDEX[WING_PERM[19][POS_SLOTS[li]]];  // full move idx 19 = F2
        int dest_b2 = POS_INDEX[WING_PERM[25][POS_SLOTS[li]]];  // full move idx 25 = B2
        if (dest_f2 == li && dest_b2 == li) {
            printf("FATAL: M-slice position %d untouched by BOTH F2 and B2 -- M/S split assumption is wrong\n", li);
            exit(1);
        }
    }
}

// Which 4-subset of the 8 LAYER positions (reindexed 0..7 via
// LAYER_VALUE_LOCAL) currently holds the 4 M-slice-ORIGIN dedges (8 choose
// 4 = 70) -- exact analog of EQUATOR_MASK_TO_RANK/EQUATOR_TRANS one tier up.
static int MSLICE_MASK_TO_RANK[256];
static int MSLICE_RANK_TO_MASK[70];
static int MSLICE_GOAL_RANK = -1;
static inline int popcount8(int mask) {
    int c = 0;
    for (int i = 0; i < 8; i++) if (mask & (1 << i)) c++;
    return c;
}
static void build_mslice_rank_tables() {
    for (int i = 0; i < 256; i++) MSLICE_MASK_TO_RANK[i] = -1;
    int next = 0;
    for (int mask = 0; mask < 256; mask++) {
        if (popcount8(mask) != 4) continue;
        MSLICE_MASK_TO_RANK[mask] = next;
        MSLICE_RANK_TO_MASK[next] = mask;
        next++;
    }
    if (next != 70) { printf("FATAL: expected 70 4-of-8 masks, got %d\n", next); exit(1); }
    int goal_mask = 0;
    for (int j = 0; j < 4; j++) goal_mask |= (1 << LAYER_VALUE_LOCAL[M_SLICE_IDX[j]]);
    MSLICE_GOAL_RANK = MSLICE_MASK_TO_RANK[goal_mask];
}

static int MSLICE_POS_TRANS[10][8];  // [t][i] = new LOCAL layer index for content at local layer position i, under TIER5_MOVES[t]
static int MSLICE_TRANS[70][10];
static void build_mslice_trans() {
    for (int t = 0; t < TIER5_NUM_MOVES; t++) {
        int m = TIER5_MOVES[t];
        for (int i = 0; i < 8; i++) {
            int li = LAYER_IDX[i];
            int dest_slot = WING_PERM[m][POS_SLOTS[li]];
            int j = POS_INDEX[dest_slot];
            if (j < 0 || LAYER_VALUE_LOCAL[j] < 0) {
                printf("FATAL: tier-5 move %d does not preserve the layer/equatorial split at layer position %d\n", m, li);
                exit(1);
            }
            MSLICE_POS_TRANS[t][i] = LAYER_VALUE_LOCAL[j];
        }
    }
    for (int rank = 0; rank < 70; rank++) {
        int mask = MSLICE_RANK_TO_MASK[rank];
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            int new_mask = 0;
            for (int i = 0; i < 8; i++) if (mask & (1 << i)) new_mask |= (1 << MSLICE_POS_TRANS[t][i]);
            MSLICE_TRANS[rank][t] = MSLICE_MASK_TO_RANK[new_mask];
        }
    }
}

static inline int mslice_occupancy_rank(const int* wing_slot) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    int mask = 0;
    for (int i = 0; i < 8; i++) {
        int li = LAYER_IDX[i];
        int dedge = dedge_id_of_piece(occupant_of_slot[POS_SLOTS[li]]);
        for (int j = 0; j < 4; j++) if (M_SLICE_IDX[j] == dedge) { mask |= (1 << i); break; }
    }
    return MSLICE_MASK_TO_RANK[mask];
}

// Parity of the permutation mapping a 4-element slice's POSITIONS to the
// dedges currently occupying them -- generalizes equator_edge_perm_parity
// (one tier up) to an arbitrary 4-index group, since condition 4 here needs
// this TWICE (M-slice and S-slice separately). Well-defined only once that
// group's own occupancy is already fully correct.
static inline int slice_edge_perm_parity(const int* wing_slot, const int slice_idx[4]) {
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[wing_slot[piece]] = piece;
    int p[4];
    for (int i = 0; i < 4; i++) {
        int dedge = dedge_id_of_piece(occupant_of_slot[POS_SLOTS[slice_idx[i]]]);
        int local = -1;
        for (int j = 0; j < 4; j++) if (slice_idx[j] == dedge) { local = j; break; }
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

// ---- Corner-permutation coset table (8!/96 = 420) --------------------------
// Partitions all 8! corner PERMUTATIONS (position-only -- orientation is
// already forced to 0 throughout S5, see corner_ori_rank_from_sticker) into
// 420 RIGHT cosets of H=<U2,D2,R2,L2,F2,B2> (order 96) within the ambient
// group <U,D,R2,L2,F2,B2> (order 40320 -- i.e. literally all of S8, verified
// below). Two permutations P1,P2 get the same ID iff P2[k]=P1[h[k]] for all
// k, for some h in H -- i.e. P2 = P1 (following) h, a RIGHT coset. This is
// the structure that makes "any solution reducing P1 into H also reduces P2
// into H" true: physically continuing a solve from a live state by further
// moves M means the NEW arrangement is M composed ONTO the old one (M(.)
// applied outermost, exactly how apply_move works elsewhere in this file)
// -- so the set of M that land P in H is H (following) P^-1, which is
// IDENTICAL for P1 and P2 exactly when P2=P1(following)h (see chat for the
// full derivation, worked out from the user's own "X*P, read left-to-right,
// inverse-apply" hint). This is DELIBERATELY not the same composition
// direction as CORNER_POS_PERM's own per-move update loop -- hence the
// explicit P[h[k]] indexing below rather than reusing corner_pos_apply for
// the enumeration itself (corner_pos_apply IS reused, correctly, for the
// forward BFS transitions in build_corner_coset_trans, where physically
// applying a move is exactly what's wanted).
static inline std::array<uint8_t,8> corner_pos_apply(const std::array<uint8_t,8>& arr, int m) {
    std::array<uint8_t,8> out;
    for (int k = 0; k < 8; k++) out[k] = (uint8_t)CORNER_POS_PERM[m][arr[k]];
    return out;
}
static inline std::array<uint8_t,8> corner_pos_from_sticker(const std::array<int,24>& corner_sticker) {
    std::array<uint8_t,8> arr;
    for (int k = 0; k < 8; k++) arr[k] = (uint8_t)(corner_sticker[k * 3 + 1] / 3);
    return arr;
}

static const int CORNER_COSET_NUM = 420;
static int16_t CORNER_COSET_ID[40320];
static std::array<uint8_t,8> CORNER_COSET_REP[420];  // one representative array per coset id

// H's own 96 elements and a perm8-rank -> H-index lookup, exposed globally
// (not just used transiently inside build_corner_coset_table) because phase
// 7 needs to track WHICH element of H the live corner permutation currently
// is (0..95), not just which of the 420 cosets it's in -- once corner
// permutation is confined to literal H (is_in_s6), "which coset" is always
// exactly 0, so the coset id alone has nothing left to say.
static std::array<uint8_t,8> H_ELEMENTS[96];
static int H_RANK_FROM_PERM8[40320];

static void build_corner_coset_table() {
    static const int H_GENS[6] = {1, 7, 10, 16, 19, 25};  // U2,D2,R2,L2,F2,B2
    std::array<uint8_t,8> ident = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<std::array<uint8_t,8>> h_list;
    h_list.push_back(ident);
    std::queue<std::array<uint8_t,8>> q;
    q.push(ident);
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (int g : H_GENS) {
            auto nxt = corner_pos_apply(cur, g);
            if (std::find(h_list.begin(), h_list.end(), nxt) == h_list.end()) {
                h_list.push_back(nxt);
                q.push(nxt);
            }
        }
    }
    if ((int)h_list.size() != 96) {
        printf("FATAL: <U2,D2,R2,L2,F2,B2> closure on corners has %zu elements (expected 96)\n", h_list.size());
        exit(1);
    }
    for (int i = 0; i < 40320; i++) H_RANK_FROM_PERM8[i] = -1;
    for (int i = 0; i < 96; i++) {
        H_ELEMENTS[i] = h_list[i];
        H_RANK_FROM_PERM8[rank_perm8(h_list[i])] = i;
    }

    for (int i = 0; i < 40320; i++) CORNER_COSET_ID[i] = -1;
    int next_id = 0;
    for (int rank = 0; rank < 40320; rank++) {
        if (CORNER_COSET_ID[rank] >= 0) continue;
        auto P = unrank_perm8(rank);
        CORNER_COSET_REP[next_id] = P;
        for (auto& h : h_list) {
            std::array<uint8_t,8> result;
            for (int k = 0; k < 8; k++) result[k] = P[h[k]];
            int r2 = rank_perm8(result);
            if (CORNER_COSET_ID[r2] >= 0 && CORNER_COSET_ID[r2] != next_id) {
                printf("FATAL: corner coset %d and %d collided at rank %d -- H isn't a group, or composition is wrong\n",
                       CORNER_COSET_ID[r2], next_id, r2);
                exit(1);
            }
            CORNER_COSET_ID[r2] = (int16_t)next_id;
        }
        next_id++;
    }
    if (next_id != CORNER_COSET_NUM) {
        printf("FATAL: corner coset enumeration produced %d cosets (expected 420)\n", next_id);
        exit(1);
    }
}

static int CORNER_COSET_TRANS[420][10];
static void build_corner_coset_trans() {
    for (int c = 0; c < CORNER_COSET_NUM; c++) {
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            int m = TIER5_MOVES[t];
            auto np = corner_pos_apply(CORNER_COSET_REP[c], m);
            CORNER_COSET_TRANS[c][t] = CORNER_COSET_ID[rank_perm8(np)];
        }
    }
    // Self-check the "transition is representative-independent" claim the
    // header comment above rests on: for random cosets, apply the SAME move
    // to a SECOND, different member of the coset (found by scanning) and
    // confirm it lands in the same place as the table says -- empirical
    // proof, not just an argument, before trusting this in the BFS below.
    std::mt19937 rng(20260925);
    std::uniform_int_distribution<int> coset_dist(0, CORNER_COSET_NUM - 1);
    std::uniform_int_distribution<int> move_dist(0, TIER5_NUM_MOVES - 1);
    int checked = 0, mismatches = 0;
    for (int trial = 0; trial < 200; trial++) {
        int c = coset_dist(rng);
        int r1 = rank_perm8(CORNER_COSET_REP[c]);
        int r2 = -1;
        for (int r = 0; r < 40320; r++) { if (r != r1 && CORNER_COSET_ID[r] == c) { r2 = r; break; } }
        if (r2 < 0) continue;
        int t = move_dist(rng);
        int m = TIER5_MOVES[t];
        auto p2 = unrank_perm8(r2);
        int c2_via_trans = CORNER_COSET_TRANS[c][t];
        int c2_via_direct = CORNER_COSET_ID[rank_perm8(corner_pos_apply(p2, m))];
        checked++;
        if (c2_via_trans != c2_via_direct) mismatches++;
    }
    if (mismatches > 0) {
        printf("FATAL: corner-coset transition is NOT representative-independent (%d/%d mismatches) -- coset math is wrong\n", mismatches, checked);
        exit(1);
    }
    printf("  corner-coset transition representative-independence: %d/%d checked, 0 mismatches\n", checked, checked);
}

// ---- Joint distance-to-S6 table (420 x 4 x 70 = 117,600) -------------------
static const int S6_UD_NUM = 4;
static const int S6_MSLICE_NUM = 70;
static inline int s6_joint_index(int corner_coset, int ud, int mslice) {
    return (corner_coset * S6_UD_NUM + ud) * S6_MSLICE_NUM + mslice;
}

static bool S6_DIST_NIBBLE = true;
static std::vector<uint8_t> S6_DIST_PACKED;  // nibble-packed if S6_DIST_NIBBLE, else 1 byte/entry
static inline int s6_dist_get(int idx) {
    if (S6_DIST_NIBBLE) {
        uint8_t b = S6_DIST_PACKED[idx / 2];
        return (idx % 2 == 0) ? (b & 0xF) : (b >> 4);
    }
    return S6_DIST_PACKED[idx];
}
// Parity of an 8-element permutation array (piece-indexed, same convention
// as rank_perm8/CORNER_COSET_REP) -- needed to verify the parity-link
// invariant below.
static inline int corner_perm_parity(const std::array<uint8_t,8>& arr) {
    bool visited[8] = {false, false, false, false, false, false, false, false};
    int parity = 0;
    for (int i = 0; i < 8; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) { visited[j] = true; j = arr[j]; len++; }
        if (len % 2 == 0) parity ^= 1;
    }
    return parity;
}

// Parity of the corner permutation RESTRICTED to one tetrahedral half
// {UFR,UBL,DFL,DBR} (per the user, see chat -- the 4th parity tie missing
// from is_in_s6, found by hand after the h7 self-test + repro sequence
// exposed a real false-positive). H=<U2,D2,R2,L2,F2,B2> maps this specific
// 4-corner set to itself (each generator either fixes it setwise or swaps
// two of its members with each other -- checked for all 6 generators), so
// its restriction is itself a well-defined 4-element permutation.
// CORNER_POS_PARITY_SIGN's +1/-1 split (tables5.h) is exactly this same
// chirality bipartition, so it's reused here rather than re-deriving which
// of the 8 raw position indices is literally "UFR" by hand. Using the +1
// class specifically vs the -1 class doesn't matter: this is only ever
// called after corner_coset==0 already holds (corner permutation confined
// to H, hence overall EVEN), and overall parity = parity(+1 half) XOR
// parity(-1 half), so forcing overall-even means the two halves' parities
// are always equal to each other.
static inline int corner_tetrahedral_parity(const std::array<uint8_t,8>& arr) {
    int idx[4], n = 0;
    for (int i = 0; i < 8; i++) if (CORNER_POS_PARITY_SIGN[i] > 0) idx[n++] = i;
    bool visited[4] = {false, false, false, false};
    int parity = 0;
    for (int i = 0; i < 4; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) {
            visited[j] = true;
            int piece = arr[idx[j]];
            int nj = 0;
            for (int k = 0; k < 4; k++) if (idx[k] == piece) { nj = k; break; }
            j = nj;
            len++;
        }
        if (len % 2 == 0) parity ^= 1;
    }
    return parity;
}

static void build_s6_dist() {
    const int N = CORNER_COSET_NUM * S6_UD_NUM * S6_MSLICE_NUM;
    std::vector<uint8_t> dist(N, 255);
    if (CORNER_COSET_ID[0] != 0) { printf("FATAL: identity corner permutation's coset id is %d, expected 0\n", CORNER_COSET_ID[0]); exit(1); }
    int start = s6_joint_index(0, 0, MSLICE_GOAL_RANK);
    dist[start] = 0;
    std::queue<int> q;
    q.push(start);
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int mslice = cur % S6_MSLICE_NUM;
        int rem = cur / S6_MSLICE_NUM;
        int ud = rem % S6_UD_NUM;
        int corner = rem / S6_UD_NUM;
        uint8_t d = dist[cur];
        for (int t = 0; t < TIER5_NUM_MOVES; t++) {
            int m = TIER5_MOVES[t];
            int ncorner = CORNER_COSET_TRANS[corner][t];
            int nud = ud ^ ud_delta(m);
            int nmslice = MSLICE_TRANS[mslice][t];
            int nidx = s6_joint_index(ncorner, nud, nmslice);
            if (dist[nidx] == 255) { dist[nidx] = (uint8_t)(d + 1); q.push(nidx); }
        }
    }
    // Per the user's own spec ("I suspect half of these states are not
    // reachable"): H=<U2,D2,R2,L2,F2,B2> consists entirely of EVEN corner
    // permutations (every generator is a double-transposition), so every
    // right coset is either all-even or all-odd -- a well-defined per-coset
    // parity. Every U/D QUARTER turn is a 4-cycle on corners (ODD) and ALSO
    // the only kind of move that flips a bit of ud (via ud_delta); every
    // other generator (U2,D2,R2,L2,F2,B2) is corner-parity-EVEN and leaves
    // ud alone. So (corner coset parity) XOR (ud's own two bits XORed
    // together) is conserved by every TIER5_MOVES generator, fixed at 0 by
    // the solved start -- exactly half of (corner,ud) pairs satisfy it,
    // hence exactly half the table is genuinely unreachable, not a bug.
    // Verified explicitly below (not just argued) before trusting it.
    int unreached = 0, maxd = 0, parity_mismatches = 0;
    long long sum = 0;
    for (int corner = 0; corner < CORNER_COSET_NUM; corner++) {
        int cp = corner_perm_parity(CORNER_COSET_REP[corner]);
        for (int ud = 0; ud < S6_UD_NUM; ud++) {
            bool expect_reachable = (cp == ((ud & 1) ^ ((ud >> 1) & 1)));
            for (int mslice = 0; mslice < S6_MSLICE_NUM; mslice++) {
                uint8_t v = dist[s6_joint_index(corner, ud, mslice)];
                bool reached = (v != 255);
                if (reached != expect_reachable) parity_mismatches++;
                if (!reached) unreached++; else { maxd = std::max(maxd, (int)v); sum += v; }
            }
        }
    }
    printf("  S6 joint dist table: %d entries, unreached=%d, avg=%.4f max=%d\n",
           N, unreached, (double)sum / std::max(1, N - unreached), maxd);
    if (parity_mismatches > 0) {
        printf("FATAL: S6 reachability doesn't match the predicted corner/ud parity link (%d mismatches) -- a real table bug, not the expected half-unreachable split\n", parity_mismatches);
        exit(1);
    }
    printf("  reachability matches the predicted corner/ud parity link exactly (%d unreached as expected, not a bug)\n", unreached);
    // Unreached entries are stored as an in-range SENTINEL (15 in nibble
    // mode, 255 in byte mode) rather than a real distance -- provably never
    // looked up in practice (s6_dist_of's precondition is a real is_in_s5
    // state, which by the invariant above can only ever land on a reachable
    // (corner,ud) pair), but a big sentinel is a safer failure mode than a
    // small, plausible-looking wrong number if that precondition is ever
    // violated by a future bug.
    S6_DIST_NIBBLE = (unreached == 0) ? (maxd <= 15) : (maxd <= 14);
    if (S6_DIST_NIBBLE) {
        S6_DIST_PACKED.assign((N + 1) / 2, 0);
        for (int i = 0; i < N; i++) {
            uint8_t v = (dist[i] == 255) ? 15 : dist[i];
            if (i % 2 == 0) S6_DIST_PACKED[i / 2] = (uint8_t)((S6_DIST_PACKED[i / 2] & 0xF0) | (v & 0xF));
            else            S6_DIST_PACKED[i / 2] = (uint8_t)((S6_DIST_PACKED[i / 2] & 0x0F) | ((v & 0xF) << 4));
        }
        printf("  packed 2 entries/byte (max reachable dist %d <= %d)\n", maxd, unreached == 0 ? 15 : 14);
    } else {
        S6_DIST_PACKED = dist;
        printf("  stored 1 entry/byte (max reachable dist %d)\n", maxd);
    }
}

// Per the user's spec: when the joint distance is 0, conditions (1)-(3) hold
// but either half of (4) might still fail -- in which case literal S6 has
// NOT been reached, and the shortest known fix is 5 moves (same value as
// S5_PARITY_MISMATCH_PENALTY one tier up; not re-derived from a witness
// example yet, just carried over as the initial guess -- see chat/CLAUDE.md
// if this needs revisiting).
static const int S6_PARITY_MISMATCH_PENALTY = 5;

} // namespace coords
