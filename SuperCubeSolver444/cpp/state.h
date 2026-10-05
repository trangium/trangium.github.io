// Full-cube state (State6), tier-membership predicates, and the per-tier
// LAZY states the search actually walks.
//
// The search is a chain of 7 tiers (S0 ... S6 = "in S_k, not yet S_{k+1}"); the
// tier a node is in is the deepest S it belongs to (classify_tier). Each tier
// tracks only the small coordinates its heuristic and next-tier test need
// (TierKState), never the full cube; State6 is rebuilt (by replaying the
// search path) only when a move crosses into the next tier.
//
// Ground truth vs lazy: is_in_s1..s7 and s5/s6/s7_dist_of work from the raw
// full-cube arrays and are the reference the self-tests compare every lazy
// transition/heuristic against.
#pragma once
#include "phase1.h"
#include "s4_tables.h"
#include "lazy67.h"

// ---------------------------------------------------------------- State6
struct State6 {
    p1::MaskState s1;   // the three center masks + wing parity (phase 1); literal S1 once canonicalized
    int32_t wing;
    std::array<int,24> center_slot;
    std::array<int,24> wing_slot;
    // PLL-parity tracking, back again for the S3'->S4 tier's tables (toggles
    // on every quarter turn, from move 0; PLL parity = wing_parity ^ this).
    int corner_parity;
    // Corner STICKER permutation (24 = 8 positions x 3 axes, local index
    // pos_idx*3+axis -- see tables5.h), for phase 5 (S4->S5, corner
    // orientation). Structurally identical to center_slot/wing_slot (pure
    // position relabeling under both moves AND whole-cube-rotation
    // conjugation) -- deliberately NOT an 8-piece slot+orientation pair; see
    // tables5.h's header comment for why that representation was tried and
    // abandoned (its rotation-conjugation transform isn't a simple function
    // of destination+old value). Corner ORIENTATION is a derived quantity,
    // computed fresh from this array only when the phase-5 heuristic needs
    // it -- see corner_ori_piece_indexed().
    std::array<int,24> corner_sticker;
};

// Distance tables and cost constants (COSTxy are flat additive gaps stacked on
// tiers below the one that models them; see tier_inadm_from_code).
static p2full::WingTables g_wing;   // distance table only; the wing coset itself is a 24-bit mask (phase2.h)
static p2full::FbTables g_fb;
static int g_cost12 = 9;
static int g_cost23p = 7;   // COST23'
static int g_cost34p = 10;  // COST3'4
static int g_cost45 = 9;
static int g_cost56 = 9;
static int g_cost67 = 4;

static inline bool is_in_s1(const State6& s) { return p1::mask_class(s.s1) == 0; }

static inline int compute_fb2(const State6& s) {
    int occupant_of_slot[24];
    for (int i = 0; i < 8; i++) occupant_of_slot[s.center_slot[FB_TARGET[i]]] = i;
    std::array<uint8_t, 8> perm;
    for (int i = 0; i < 8; i++) perm[i] = (uint8_t)occupant_of_slot[FB_TARGET[i]];
    return p2full::rank_perm8(perm);
}
static inline int compute_ud2(const State6& s) {
    int occupant_of_slot[24];
    for (int i = 0; i < 8; i++) occupant_of_slot[s.center_slot[UD_TARGET[i]]] = i;
    std::array<uint8_t, 8> perm;
    for (int i = 0; i < 8; i++) perm[i] = (uint8_t)occupant_of_slot[UD_TARGET[i]];
    return p2full::rank_perm8(perm);
}
static inline int compute_lr2(const State6& s) {
    int occupant_of_slot[24];
    for (int i = 0; i < 8; i++) occupant_of_slot[s.center_slot[LR_TARGET[i]]] = i;
    std::array<uint8_t, 8> perm;
    for (int i = 0; i < 8; i++) perm[i] = (uint8_t)occupant_of_slot[LR_TARGET[i]];
    return p2full::rank_perm8(perm);
}

static inline bool is_in_s2(const State6& s) {
    if (!is_in_s1(s)) return false;
    return s.wing == (int32_t)p2full::WING_SOLVED_MASK && p2full::FB_IS_SOLVED[compute_fb2(s)];
}

static inline bool is_in_s3prime(const State6& s) {
    if (!is_in_s2(s)) return false;
    int lr2520 = coords::CENTER2520_LR[compute_lr2(s)];
    int fb96 = coords::FB96_RANK_OF[compute_fb2(s)];
    int fb24 = (fb96 < 0) ? -1 : coords::FB24_FROM_FB96RANK[fb96];
    if (fb24 < 0) return false;
    if (!coords::CENTER_GOOD[coords::center_idx(lr2520, fb24)]) return false;
    auto pairing = coords::compute_pairing(s.wing_slot.data());
    uint32_t di = pairing_dense_index(pairing_rank_full(pairing));
    return coords::WING_GOOD[di];
}

// Literal S4: is_in_s2, plus centers fully resolved (ud2520==lr2520==
// fb24==0) and wing-pairing solved. Deliberately does NOT go through
// is_in_s3prime as a gate -- it's a much more specific condition (identity
// is just ONE of the ~80,640 S3'-good states).
static const std::array<uint8_t,12> PAIRING_SOLVED12 = {0,1,2,3,4,5,6,7,8,9,10,11};

static inline bool is_in_s4(const State6& s) {
    if (!is_in_s2(s)) return false;
    int ud2520 = coords::CENTER2520_UD[compute_ud2(s)];
    int lr2520 = coords::CENTER2520_LR[compute_lr2(s)];
    int fb96 = coords::FB96_RANK_OF[compute_fb2(s)];
    int fb24 = (fb96 < 0) ? -1 : coords::FB24_FROM_FB96RANK[fb96];
    if (ud2520 != 0 || lr2520 != 0 || fb24 != 0) return false;
    auto pairing = coords::compute_pairing(s.wing_slot.data());
    return pairing == PAIRING_SOLVED12;
}

// ---- S4 up to a whole-cube half turn ------------------------------------------------------------
// The search treats S4 like S1: a state that is S4 after x2, y2 or z2 is as good as a literal S4 one. (Example:
// Rw2 U2 D2 F2 B2 Rw2 followed by x2 is in S4, S5 and S6.) These three rotations are the only ones that fix S1
// literally, so they are the whole freedom left after S1. The state is canonicalized by the half turn (a free
// rotation letter in the path, class 5 + k) at the S3'->S4 crossing, and tier 4 onwards only sees literal S4.
// A state is such an "S4 image" iff its centers' ud/lr/fb classes and its wing pairing match those of
// (half turn) . solved: the signatures are computed from the rotation tables once (init_s4_goals).
struct S4Sig { int ud2520, lr2520, fb24; };
static S4Sig S4_SIG[4];   // [0] = literal S4 (0, 0, 0); [k] = the signature of the half turn of class 5 + k, applied to solved
static inline S4Sig s4_sig_of(const State6& s) {
    const int fb96 = coords::FB96_RANK_OF[compute_fb2(s)];
    return S4Sig{coords::CENTER2520_UD[compute_ud2(s)], coords::CENTER2520_LR[compute_lr2(s)],
                 (fb96 < 0) ? -1 : coords::FB24_FROM_FB96RANK[fb96]};
}
static void init_s4_goals() {
    for (int k = 0; k < 4; k++) {
        State6 s;
        const int cls = k == 0 ? 0 : 5 + k;
        for (int i = 0; i < 24; i++) { s.center_slot[i] = ROTATE_CENTER[cls][i]; s.wing_slot[i] = ROTATE_WING[cls][i]; }
        S4_SIG[k] = s4_sig_of(s);
        auto pairing = coords::compute_pairing(s.wing_slot.data());
        const int ud = coords::C_UD.dense_of_raw[S4_SIG[k].ud2520], lr = coords::C_LR.dense_of_raw[S4_SIG[k].lr2520];
        const int fb = S4_SIG[k].fb24 < 0 ? -1 : coords::C_FB.dense_of_raw[S4_SIG[k].fb24];
        if (!(pairing == PAIRING_SOLVED12) || ud < 0 || lr < 0 || fb < 0) {
            printf("FATAL: the half turn of class %d is not an S4 state in the S3'->S4 coordinates\n", cls);
            exit(1);
        }
        coords::S4_GOAL_UD[k] = ud;
        coords::S4_GOAL_REST[k] = lr * 24 + fb;
        for (int j = 0; j < k; j++)
            if (S4_SIG[j].ud2520 == S4_SIG[k].ud2520 && S4_SIG[j].lr2520 == S4_SIG[k].lr2520 && S4_SIG[j].fb24 == S4_SIG[k].fb24) {
                printf("FATAL: two S4 images have the same signature\n");
                exit(1);
            }
    }
}
// -1: not S4 (even up to a half turn); 0: literal S4; k = 1, 2, 3: S4 after the half turn of class 5 + k.
static inline int s4_k_class(const State6& s) {
    if (!is_in_s2(s)) return -1;
    const S4Sig g = s4_sig_of(s);
    if (g.fb24 < 0) return -1;
    int k = -1;
    for (int j = 0; j < 4; j++)
        if (g.ud2520 == S4_SIG[j].ud2520 && g.lr2520 == S4_SIG[j].lr2520 && g.fb24 == S4_SIG[j].fb24) k = j;
    if (k < 0) return -1;
    return coords::compute_pairing(s.wing_slot.data()) == PAIRING_SOLVED12 ? k : -1;
}

// Distance to S5 (phase 5), per the joint table built in coords. Precondition:
// is_in_s4(s) already true (S5's own conditions are only meaningful there).
// When the table reports 0 but the condition-4 parity tie fails, literal S5
// has NOT been reached -- see coords::S5_PARITY_MISMATCH_PENALTY.
static inline int s5_dist_of(const State6& s) {
    int corner_r = coords::corner_ori_rank_from_sticker(s.corner_sticker);
    int l_par = coords::center4_perm_parity(s.center_slot, coords::L_CENTER_SLOTS);
    int r_par = coords::center4_perm_parity(s.center_slot, coords::R_CENTER_SLOTS);
    int lr = l_par | (r_par << 1);
    int eq = coords::equator_occupancy_rank(s.wing_slot.data());
    int d = coords::s5_dist_get(coords::s5_joint_index(corner_r, lr, eq));
    if (d == 0) {
        // ALL FOUR of L,R,F,B use center4_is_shifted here, NOT
        // center4_perm_parity -- "solved" and "180" are different states
        // that both count as EVEN (center4_perm_parity can't tell them
        // apart), but condition 4 cares specifically about how many are
        // AT the 180/shifted state, not just whether that count is
        // reachable-as-good. For F/B this is forced (quarter F/B turns
        // haven't been available since S2, so once fb24==0 holds, F's raw
        // state can only be "identity" or "the F2 double-transposition").
        // For L/R, d==0 already guarantees "even" (identity or 180) via
        // the joint table's own "lr" dimension, so is_shifted correctly
        // resolves which of those two it is. Found and fixed from THREE
        // witness examples (see chat): L R' F2 L' R, and L U2 D2 L2 U2 D2
        // L' (both must fail condition 4) vs L U2 D2 L2 U2 D2 L (must also
        // fail, and already did under the old F/B-only formula -- but the
        // first two didn't until L/R were included here too).
        bool l_shifted = coords::center4_is_shifted(s.center_slot, coords::L_CENTER_SLOTS);
        bool r_shifted = coords::center4_is_shifted(s.center_slot, coords::R_CENTER_SLOTS);
        bool f_shifted = coords::center4_is_shifted(s.center_slot, coords::F_CENTER_SLOTS);
        bool b_shifted = coords::center4_is_shifted(s.center_slot, coords::B_CENTER_SLOTS);
        int centers_parity = (int)l_shifted ^ (int)r_shifted ^ (int)f_shifted ^ (int)b_shifted;
        int edge_par = coords::equator_edge_perm_parity(s.wing_slot.data());
        if (edge_par != centers_parity) return coords::S5_PARITY_MISMATCH_PENALTY;
    }
    return d;
}

static inline bool is_in_s5(const State6& s) {
    return is_in_s4(s) && s5_dist_of(s) == 0;
}

// Distance to S6 (phase 6), per the joint table built in coords. Precondition:
// is_in_s5(s) already true (S6's own conditions are only meaningful there --
// in particular the M/S-slice occupancy split is only well-defined once the
// layer/equatorial split is exact, which is exactly what is_in_s5 gives).
static inline int s6_dist_of(const State6& s) {
    auto corner_arr = coords::corner_pos_from_sticker(s.corner_sticker);
    int corner_coset = coords::CORNER_COSET_ID[coords::rank_perm8(corner_arr)];
    int u_par = coords::center4_perm_parity(s.center_slot, coords::U_CENTER_SLOTS);
    int d_par = coords::center4_perm_parity(s.center_slot, coords::D_CENTER_SLOTS);
    int ud = u_par | (d_par << 1);
    int mslice = coords::mslice_occupancy_rank(s.wing_slot.data());
    int d = coords::s6_dist_get(coords::s6_joint_index(corner_coset, ud, mslice));
    if (d == 0) {
        // All SIX faces use center4_is_shifted here (not center4_perm_parity)
        // -- same reasoning as s5_dist_of one tier up, applied proactively
        // this time (see chat: that one was found only after 3 witness
        // examples). L/R and F/B are both halves-only by this point (L/R as
        // of this very tier, F/B since S2), so they're ALWAYS confined to
        // {solved,180} -- is_shifted is unconditionally meaningful for them.
        // U/D are this tier's own full-quarter axis, but d==0 already forces
        // ud==0 (both even, i.e. AT solved-or-180) via the joint table's own
        // "ud" dimension, exactly like L/R did for s5_dist_of.
        bool u_shifted = coords::center4_is_shifted(s.center_slot, coords::U_CENTER_SLOTS);
        bool d_shifted = coords::center4_is_shifted(s.center_slot, coords::D_CENTER_SLOTS);
        bool l_shifted = coords::center4_is_shifted(s.center_slot, coords::L_CENTER_SLOTS);
        bool r_shifted = coords::center4_is_shifted(s.center_slot, coords::R_CENTER_SLOTS);
        bool f_shifted = coords::center4_is_shifted(s.center_slot, coords::F_CENTER_SLOTS);
        bool b_shifted = coords::center4_is_shifted(s.center_slot, coords::B_CENTER_SLOTS);
        int m_centers_parity = (int)u_shifted ^ (int)f_shifted ^ (int)d_shifted ^ (int)b_shifted;
        int s_centers_parity = (int)u_shifted ^ (int)r_shifted ^ (int)d_shifted ^ (int)l_shifted;
        int m_edge_par = coords::slice_edge_perm_parity(s.wing_slot.data(), coords::M_SLICE_IDX);
        int s_edge_par = coords::slice_edge_perm_parity(s.wing_slot.data(), coords::S_SLICE_IDX);
        if (m_edge_par != m_centers_parity || s_edge_par != s_centers_parity) return coords::S6_PARITY_MISMATCH_PENALTY;
        // 4th parity tie (per the user, see chat): the parity of the corner
        // permutation restricted to the tetrahedral half {UFR,UBL,DFL,DBR}
        // must equal the parity of how many of the 6 face centers are
        // twisted 180 degrees. Missing this let states through is_in_s6
        // that satisfied every OTHER check but weren't actually
        // phase-7-reachable in the frame the S7 table assumes (found via a
        // real repro case: is_in_s6 was true but h7 was 255 -- unreachable
        // -- under every whole-cube rotation).
        int corner_tet_par = coords::corner_tetrahedral_parity(corner_arr);
        int total_centers_par = (int)u_shifted ^ (int)d_shifted ^ (int)l_shifted ^ (int)r_shifted ^ (int)f_shifted ^ (int)b_shifted;
        if (corner_tet_par != total_centers_par) return coords::S6_PARITY_MISMATCH_PENALTY;
    }
    return d;
}

// With --ls the S6 / S7 tables (and everything built on them) are not generated at all (g_legacy_tail == false): "solved" is then
// the literal identity of the arrays, which is exactly what is_in_s7 means (S7 is the trivial group).
static bool g_legacy_tail = true;
static inline bool is_solved_literal(const State6& s) {
    for (int i = 0; i < 24; i++) if (s.wing_slot[i] != i || s.center_slot[i] != i || s.corner_sticker[i] != i) return false;
    return true;
}
static inline bool is_in_s6(const State6& s) {
    if (!g_legacy_tail) { printf("BUG: is_in_s6 needs the S6 tables, which --ls does not build\n"); abort(); }
    return is_in_s5(s) && s6_dist_of(s) == 0;
}

// Distance to solved (phase 7), per the joint table built in coords.
// Precondition: is_in_s6(s) already true. No penalty layer here, unlike
// s5_dist_of/s6_dist_of -- S6's own two parity ties already pin the state
// down exactly, so (per the user's own spec) this joint table is fully
// reachable with no leftover ambiguity to special-case.
static inline int s7_dist_of(const State6& s) {
    int corner_h = coords::h_rank_of(s.corner_sticker);
    int ec = coords::ec_dense_of(s.wing_slot.data(), s.center_slot);
    return coords::s7_dist_get(coords::s7_joint_index(corner_h, ec));
}

static inline bool is_in_s7(const State6& s) {
    if (!g_legacy_tail) return is_solved_literal(s);
    return is_in_s6(s) && s7_dist_of(s) == 0;
}

// parity of the permutation of just the 12 POSITIVE-orbit wing pieces
// (well-defined only once is_in_s2 holds). PLL parity = wing_parity ^
// corner_parity -- verbatim from chain1234v2.cpp.
static inline int positive_wing_parity(const State6& s) {
    bool visited[12] = {false};
    int occupant_of_pos_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_pos_slot[s.wing_slot[piece]] = piece;
    int parity = 0;
    for (int i = 0; i < 12; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) {
            visited[j] = true;
            int occupant_piece = occupant_of_pos_slot[POS_SLOTS[j]];
            j = coords::POS_INDEX[occupant_piece];
            len++;
        }
        if (len % 2 == 0) parity ^= 1;
    }
    return parity;
}
static inline int pll_parity(const State6& s) {
    return positive_wing_parity(s) ^ s.corner_parity;
}

// Verification-only, rotation-aware S7 (literal solved) membership test --
// supersedes this file's own now-removed is_in_s6_mod_conjugation the same
// way that one superseded is_in_s5_mod_conjugation, since S7 is now the
// search's terminal goal. ALL 5 non-identity classes, not just a fork's own
// subset -- canonicalize_if_rotated (called unconditionally on every move
// inside apply_move) can land on ANY of classes 1-5 whenever a state's
// (ud,lr,fb) triple coincidentally matches a non-identity class's target
// triple (not only when first entering S1), leaving a standalone rotation
// sentinel in the solution that is NOT paired with a fork's own open/close
// bracket. Found from a real --profile "INVALID SOLUTION" report (see chat)
// whose printed solution contained bracket notation for classes this retry
// loop, at the time, never tried.
static bool is_in_s7_mod_conjugation(const State6& s) {
    if (is_in_s7(s)) return true;
    for (int cls : {1, 2, 3, 4, 5}) {
        State6 conj = s;
        for (int i = 0; i < 24; i++) conj.center_slot[i] = ROTATE_CENTER[cls][s.center_slot[ROTATE_CENTER_INV[cls][i]]];
        for (int i = 0; i < 24; i++) conj.wing_slot[i] = ROTATE_WING[cls][s.wing_slot[ROTATE_WING_INV[cls][i]]];
        for (int i = 0; i < 24; i++) conj.corner_sticker[i] = ROTATE_CORNER[cls][s.corner_sticker[ROTATE_CORNER_INV[cls][i]]];
        conj.s1 = p1::extract(conj.wing_slot.data(), conj.center_slot.data());
        conj.wing = p2full::wing_mask_of(conj.wing_slot);
        conj.corner_parity = s.corner_parity;
        if (is_in_s7(conj)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- moves on State6
// r := (rotation class cls, inverted) . r : the whole cube is turned back, only the slots are relabeled.
static inline void rotate_close_state6(State6& r, int cls) {
    std::array<int,24> nc, nw, ncorner;
    for (int i = 0; i < 24; i++) nc[i] = ROTATE_CENTER_INV[cls][r.center_slot[i]];
    for (int i = 0; i < 24; i++) nw[i] = ROTATE_WING_INV[cls][r.wing_slot[i]];
    for (int i = 0; i < 24; i++) ncorner[i] = ROTATE_CORNER_INV[cls][r.corner_sticker[i]];
    r.center_slot = nc;
    r.wing_slot = nw;
    r.corner_sticker = ncorner;
    r.s1 = p1::extract(nw.data(), nc.data());
    r.wing = p2full::wing_mask_of(nw);
}

// Canonicalizes a state that is S1 up to a whole-cube rotation (always), and with `with_k` also one that is S4 only up
// to a half turn x2/y2/z2 (class 6..8). Returns the class of the rotation applied, or -1. apply_move leaves `with_k`
// off: the search canonicalizes the S4 images itself at the S3'->S4 crossing, where the rotation letter is recorded in
// the path (and a replay of the path applies it again); root states (extract6, invert_state6) use `with_k`.
static inline int canonicalize_if_rotated(State6& r, bool with_k = false) {
    int cls = p1::mask_class(r.s1);
    if (cls < 0) return -1;
    if (cls == 0) {
        if (!with_k) return -1;
        const int k = s4_k_class(r);
        if (k <= 0) return -1;
        rotate_close_state6(r, 5 + k);
        return 5 + k;
    }
    std::array<int,24> nc, nw, ncorner;
    for (int i = 0; i < 24; i++) nc[i] = ROTATE_CENTER_INV[cls][r.center_slot[i]];
    for (int i = 0; i < 24; i++) nw[i] = ROTATE_WING_INV[cls][r.wing_slot[i]];
    for (int i = 0; i < 24; i++) ncorner[i] = ROTATE_CORNER_INV[cls][r.corner_sticker[i]];
    r.center_slot = nc;
    r.wing_slot = nw;
    r.corner_sticker = ncorner;
    r.s1 = p1::solved();
    r.wing = p2full::wing_mask_of(nw);
    return cls;
}

static inline State6 apply_move(const State6& s, int m, int* out_rotation = nullptr) {
    State6 r;
    r.s1 = p1::step(s.s1, m);
    r.wing = (int32_t)p2full::wing_step((uint32_t)s.wing, m);
    for (int i = 0; i < 24; i++) r.center_slot[i] = CENTER_PERM[m][s.center_slot[i]];
    for (int i = 0; i < 24; i++) r.wing_slot[i] = WING_PERM[m][s.wing_slot[i]];
    r.corner_parity = s.corner_parity ^ (coords::IS_QUARTER_TURN[m] ? 1 : 0);
    for (int i = 0; i < 24; i++) r.corner_sticker[i] = CORNER_PERM[m][s.corner_sticker[i]];
    int cls = canonicalize_if_rotated(r);
    if (out_rotation) *out_rotation = cls;
    return r;
}

// ================================================================ lazy tiers
// Tier 0 (S0, not yet S1): p1::MaskState (the three center masks + wing parity). The heuristic is the exact
// distance to S1 from the endtable (g_depth + 1 = "farther") plus the flat COST gaps of every tier below.
static inline int heuristic_tier0(const p1::MaskState& s1) {
    return p1::dist(s1) + g_cost12 + g_cost23p + g_cost34p + g_cost45 + g_cost56 + g_cost67;
}

// Tier 1 (S1, not yet S2): wing coset, F/B center permutation rank, and the
// U/D and L/R center classes (the latter two only feed the early penalty).
struct Tier1State { int32_t wing; int32_t fb2; int16_t ud2520; int16_t lr2520; };

static int FULL_TO_P2_MOVE_INDEX[NUM_MOVES];
static std::vector<int32_t> FB2_TRANS;  // [fb2_raw * P2_NUM_MOVES + pm]
static void init_phase2_lazy_tables() {
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_P2_MOVE_INDEX[m] = -1;
    for (int pm = 0; pm < P2_NUM_MOVES; pm++) FULL_TO_P2_MOVE_INDEX[P2_TO_FULL_MOVE_INDEX[pm]] = pm;

    FB2_TRANS.assign((size_t)NUM_FB_PERMS * P2_NUM_MOVES, -1);
    for (int raw = 0; raw < NUM_FB_PERMS; raw++) {
        auto p = coords::unrank_perm8(raw);
        for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
            int m = P2_TO_FULL_MOVE_INDEX[pm];
            auto np = coords::apply_move_to_fb_perm(p, m);
            FB2_TRANS[(size_t)raw * P2_NUM_MOVES + pm] = coords::rank_perm8(np);
        }
    }
}
static inline int32_t fb2_trans(int32_t fb2, int pm) { return FB2_TRANS[(size_t)fb2 * P2_NUM_MOVES + pm]; }

static inline Tier1State extract_tier1(const State6& s) {
    Tier1State t;
    t.wing = s.wing;
    t.fb2 = compute_fb2(s);
    t.ud2520 = coords::CENTER2520_UD[compute_ud2(s)];
    t.lr2520 = coords::CENTER2520_LR[compute_lr2(s)];
    return t;
}

static inline bool tier1_is_in_s2(const Tier1State& t) {
    return t.wing == (int32_t)p2full::WING_SOLVED_MASK && p2full::FB_IS_SOLVED[t.fb2];
}

// Tier-1 branch of heuristic(State6), copied verbatim but computed from the
// 4 reduced coordinates alone (same h1 formula, same unweighted inadm_raw
// sum including both penalty terms, weight applied once to the total).
#include "phase2_endtable.h"
// h1 = the largest of the wing distance table, the F/B distance table and the joint endtable (any of the first
// two can be switched off with --p2-tables, to measure what the endtable alone achieves).
static bool g_p2_use_wing = true, g_p2_use_fb = true;
static inline int heuristic_tier1(const Tier1State& t) {
    int h1 = 0;
    if (g_p2_use_wing) h1 = (int)g_wing.dist[p2full::wing_index((uint32_t)t.wing)];
    if (g_p2_use_fb) h1 = std::max(h1, (int)g_fb.dist[t.fb2]);
    if (h1 <= p2j::g_depth) h1 = std::max(h1, p2j::dist((uint32_t)t.wing, t.fb2));   // the endtable can only raise h1 up to g_depth + 1
    return h1 + g_cost23p + g_cost34p + g_cost45 + g_cost56 + g_cost67 + coords::PEN_UD[t.ud2520] + coords::PEN_LR[t.lr2520];
}

static inline Tier1State apply_tier1_move(const Tier1State& t, int m) {
    int pm = FULL_TO_P2_MOVE_INDEX[m];
    Tier1State r;
    r.wing = (int32_t)p2full::wing_step((uint32_t)t.wing, m);
    r.fb2 = fb2_trans(t.fb2, pm);
    r.ud2520 = coords::UD2520_TRANS[t.ud2520][pm];
    r.lr2520 = coords::LR2520_TRANS[t.lr2520][pm];
    return r;
}

// Tier 2 (S2, not yet S3'): searched through the S2 -> S3' SWITCH route (s2switch.h): the goal is a state whose
// inverse is in S3', reached together with a switch. (The earlier "normal" route to S3' by moves alone needed a
// 120 MB wing-pairing distance table; see archive/cpp/s3_route_normal_and_both.)
#include "s2switch.h"

// Tier 3 (S3', not yet S4): the variant-specific Tier3State from s4_tables.h
// (compact wing/center coordinates with direct transition tables).
static int FULL_TO_S4_COL[NUM_MOVES];
static void init_tier3_moves() {
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_S4_COL[m] = -1;
    for (int k = 0; k < coords::S4_NM; k++) FULL_TO_S4_COL[P2_TO_FULL_MOVE_INDEX[coords::S3PRIME_GOOD_MOVES_PM[k]]] = k;
}
using coords::Tier3State;
static inline Tier3State extract_tier3(const State6& s) {
    int ud2520 = coords::CENTER2520_UD[compute_ud2(s)];
    int lr2520 = coords::CENTER2520_LR[compute_lr2(s)];
    int fb96 = coords::FB96_RANK_OF[compute_fb2(s)];
    int fb24 = (fb96 < 0) ? 0 : coords::FB24_FROM_FB96RANK[fb96];
    auto pairing = coords::compute_pairing(s.wing_slot.data());
    return coords::tier3_from_ids(coords::s4_ids_of(pairing, ud2520, lr2520, fb24, pll_parity(s)));
}
static inline Tier3State apply_tier3_move(const Tier3State& t, int m) {
    return coords::tier3_apply(t, FULL_TO_S4_COL[m]);
}
static inline bool tier3_is_in_s4(const Tier3State& t) { return coords::tier3_is_s4(t); }   // S4 up to a half turn
#include "s4_sym.h"   // the symmetry-reduced T1 / T2 and their lookup
static inline int heuristic_tier3(const Tier3State& t) {
    return s4sym::tier3_dist(t) + g_cost45 + g_cost56 + g_cost67;
}

// Tier 4 (S4, not yet S5): corner orientation, L/R rotation parity, equator-edge
// coordinate, center-rotation sum.
struct Tier4State { int16_t corner_ori; int8_t lr_rot; int16_t eq_coord; int8_t center_sum; };

static inline Tier4State extract_tier4(const State6& s) {
    Tier4State t;
    t.corner_ori = (int16_t)coords::corner_ori_rank_from_sticker(s.corner_sticker);
    int l_par = coords::center4_perm_parity(s.center_slot, coords::L_CENTER_SLOTS);
    int r_par = coords::center4_perm_parity(s.center_slot, coords::R_CENTER_SLOTS);
    t.lr_rot = (int8_t)(l_par | (r_par << 1));
    t.eq_coord = (int16_t)coords::compute_eq_parity_coord(s.wing_slot.data());
    // NOT always 0: is_in_s4 (the precondition for ever calling this) only
    // forces ud2520==lr2520==fb24==0, and those reduced coordinates fold
    // together all 4 rotational states of each face (see "A lone R
    // legitimately satisfies is_in_s4" in CLAUDE.md) -- R/L in particular
    // keep full quarter-turn moves all the way through S3'/S4's own move
    // sets, so a real S4 entry can land with R or L at any of 0..3, not
    // just literal identity. Must be genuinely computed from the state.
    t.center_sum = (int8_t)coords::compute_center_sum(s.center_slot);
    return t;
}

static inline Tier4State apply_tier4_move(const Tier4State& t, int m) {
    int ti = coords::FULL_TO_TIER4_MOVE_INDEX[m];
    Tier4State r;
    r.corner_ori = (int16_t)coords::CORNER_ORI_TRANS[t.corner_ori][ti];
    r.lr_rot = (int8_t)(t.lr_rot ^ coords::lr_delta(m));
    r.eq_coord = (int16_t)coords::EQ_PARITY_TRANS[t.eq_coord][ti];
    r.center_sum = (int8_t)((t.center_sum + coords::CENTER_SUM_DELTA[m]) & 3);
    return r;
}

// Condition 4 (per the user): the equator-edge permutation's parity (the
// eq_coord's own high/low half) must match whether the center-rotation sum
// is 0 or 2. Both conditions 1-3 (corner_ori==0, lr_rot==0, eq occupancy at
// goal) AND condition 4 are required for literal S5.
static inline bool tier4_is_in_s5(const Tier4State& t) {
    if (t.corner_ori != 0 || t.lr_rot != 0) return false;
    bool eq_odd = (t.eq_coord >= 495);
    int eq_occ = eq_odd ? (t.eq_coord - 495) : t.eq_coord;
    if (eq_occ != coords::EQUATOR_GOAL_RANK) return false;
    bool center_high = (t.center_sum >> 1) & 1;  // true iff center_sum==2 (vs 0)
    return eq_odd == center_high;
}

// Tier-4 branch of heuristic(State6), copied verbatim but computed from the
// lazy coordinates alone -- including the condition-4 check inline (was
// s5_dist_of's own job), since the joint table's own domain (corner_ori x
// lr_rot x eq_occupancy) doesn't encode condition 4 at all.
static inline int heuristic_tier4(const Tier4State& t) {
    bool eq_odd = (t.eq_coord >= 495);
    int eq_occ = eq_odd ? (t.eq_coord - 495) : t.eq_coord;
    int d = coords::s5_dist_get(coords::s5_joint_index(t.corner_ori, t.lr_rot, eq_occ));
    if (d == 0) {
        bool center_high = (t.center_sum >> 1) & 1;
        if (eq_odd != center_high) d = coords::S5_PARITY_MISMATCH_PENALTY;
    }
    double inadm_raw = g_cost56 + g_cost67;
    double h = d + inadm_raw;
    int h_int = (int)std::ceil(h);
    return h_int;
}

// tier 6 (in S6): 0 extra. tier 5 (in S5, not S6): COST67. tier 4 (in S4,
// not S5): COST56+COST67. tier 3 (in S3', not S4): COST45+COST56+COST67.
// tier 2 (S2, not S3'): COST3'4+COST45+COST56+COST67. tier 1 (S1, not S2):
// COST23'+COST3'4+COST45+COST56+COST67. tier 0 (not S1):
// COST12+COST23'+COST3'4+COST45+COST56+COST67. Generalizes the established
// "cost stacks below the tier that models it, drops away once inside it"
// pattern from COST23'/COST24/COST34 elsewhere in this project. Deliberately
// does NOT include the U/D-and-LR-center penalty (see heuristic() and
// dfs_inadmissible's comment on drop_credit) -- this is used ONLY for the
// ida-inadm drop_credit optimization, which needs a value that is constant
// across all states within a tier, not heuristic()'s true h.
static inline int tier_inadm_from_code(int tier) {
    int base = (tier == 6) ? 0 : (tier == 5) ? g_cost67 : (tier == 4) ? g_cost56 + g_cost67 :
               (tier == 3) ? g_cost45 + g_cost56 + g_cost67 :
               (tier == 2) ? g_cost34p + g_cost45 + g_cost56 + g_cost67 :
               (tier == 1) ? g_cost23p + g_cost34p + g_cost45 + g_cost56 + g_cost67 :
               g_cost12 + g_cost23p + g_cost34p + g_cost45 + g_cost56 + g_cost67;
    return base;
}

// Tiers 5 (S5, not yet S6) and 6 (S6, not yet solved): ID-packed composite
// coordinates (lazy67.h); the S5->S6 handoff never rebuilds a State6.
// Leave-slice mode (--ls, ls.h): tier 5 = S5 not yet LS (the UD part below), tier 6 = LS (the terminal LS -> solved search).
struct LsState { uint16_t c = 0, e = 0; uint8_t u = 0, d = 0; };   // corner rank, U/D-layer dedge rank, U center turns, D center turns
static bool g_ls = false;
static bool g_ls_no_s5_switch = true;   // --ls: no NISS switch on the move that enters S5 (earlier boundaries keep theirs); --ls-niss-all: switch there too
static int g_cost_ls = 11;   // flat cost of S5 -> LS -> solved, stacked on tier 4 (replaces COST56 + COST67)
static bool ls_member_of(const State6& s);   // ls.h
static int ls_h_of(const State6& s);

struct Tier5State {
    LsState ls;           // --ls only
    int32_t corner_id;    // composite corner ID (coset = &511, H-rank once coset==0 = >>9)
    int16_t slice_m_id;   // raw M-tuple id (0..1679)
    int16_t slice_s_id;   // raw S-tuple id (0..1679)
    int8_t  eq_perm;      // E-slice full permutation rank (0..23)
    uint8_t center_byte;  // U(2b)/D(2b)/R,L,F,B(1b each) -- see compute_center_byte
};

static inline Tier5State extract_tier5(const State6& s) {
    Tier5State t;
    t.corner_id = coords::COMPOSITE_CORNER_ID[coords::rank_perm8(coords::corner_pos_from_sticker(s.corner_sticker))];
    int occupant_of_slot[24];
    for (int piece = 0; piece < 24; piece++) occupant_of_slot[s.wing_slot[piece]] = piece;
    std::array<int,4> pos_m, pos_s;
    for (int j = 0; j < 4; j++) {
        int target = coords::M_SLICE_IDX[j];
        for (int i = 0; i < 8; i++) {
            int li = coords::LAYER_IDX[i];
            if (coords::dedge_id_of_piece(occupant_of_slot[POS_SLOTS[li]]) == target) { pos_m[j] = i; break; }
        }
    }
    for (int j = 0; j < 4; j++) {
        int target = coords::S_SLICE_IDX[j];
        for (int i = 0; i < 8; i++) {
            int li = coords::LAYER_IDX[i];
            if (coords::dedge_id_of_piece(occupant_of_slot[POS_SLOTS[li]]) == target) { pos_s[j] = i; break; }
        }
    }
    t.slice_m_id = (int16_t)coords::tuple4of8_rank(pos_m);
    t.slice_s_id = (int16_t)coords::tuple4of8_rank(pos_s);
    t.eq_perm = (int8_t)coords::slice_perm_rank(s.wing_slot.data(), coords::EQUATORIAL_IDX);
    t.center_byte = coords::compute_center_byte(s.center_slot);
    return t;
}

static inline Tier5State apply_tier5_move(const Tier5State& t, int m) {
    int ti = coords::FULL_TO_TIER5_MOVE_INDEX[m];
    Tier5State r;
    r.corner_id = coords::COMPOSITE_CORNER_TRANS[t.corner_id][ti];
    r.slice_m_id = (int16_t)coords::SLICE4_TRANS[t.slice_m_id][ti];
    r.slice_s_id = (int16_t)coords::SLICE4_TRANS[t.slice_s_id][ti];
    r.eq_perm = (int8_t)coords::EQ_PERM_TRANS5[t.eq_perm][ti];
    r.center_byte = coords::apply_center_byte(t.center_byte, m);
    return r;
}

// Condition 4's two parity ties (M-slice/S-slice) plus the tetrahedral-
// corner tie, computed straight from the tracked fields -- mirrors
// s6_dist_of's own inline check exactly, just from lazy coordinates.
static inline bool tier5_condition4_ok(const Tier5State& t) {
    int u_rot = t.center_byte & 3, d_rot = (t.center_byte >> 2) & 3;
    bool u_shifted = (u_rot == 2), d_shifted = (d_rot == 2);
    bool r_shifted = (t.center_byte >> 4) & 1, l_shifted = (t.center_byte >> 5) & 1;
    bool f_shifted = (t.center_byte >> 6) & 1, b_shifted = (t.center_byte >> 7) & 1;
    int m_centers_par = (int)u_shifted ^ (int)f_shifted ^ (int)d_shifted ^ (int)b_shifted;
    int s_centers_par = (int)u_shifted ^ (int)r_shifted ^ (int)d_shifted ^ (int)l_shifted;
    int m_edge_par = coords::PERM4_PARITY_OF_RANK[coords::SLICE4_COMPOSITE_M[t.slice_m_id] & 31];
    int s_edge_par = coords::PERM4_PARITY_OF_RANK[coords::SLICE4_COMPOSITE_S[t.slice_s_id] & 31];
    if (m_edge_par != m_centers_par || s_edge_par != s_centers_par) return false;
    int h_rank = t.corner_id >> 9;
    int corner_tet_par = coords::corner_tetrahedral_parity(coords::H_ELEMENTS[h_rank]);
    int total_centers_par = (int)u_shifted ^ (int)d_shifted ^ (int)l_shifted ^ (int)r_shifted ^ (int)f_shifted ^ (int)b_shifted;
    return corner_tet_par == total_centers_par;
}

static inline bool tier5_is_in_s6(const Tier5State& t) {
    int u_rot = t.center_byte & 3, d_rot = (t.center_byte >> 2) & 3;
    if ((u_rot & 1) != 0 || (d_rot & 1) != 0) return false;
    if ((t.corner_id & 511) != 0) return false;
    int m_occ = coords::SLICE4_COMPOSITE_M[t.slice_m_id] >> 5;
    if (m_occ != coords::MSLICE_GOAL_RANK) return false;
    return tier5_condition4_ok(t);
}

static inline int heuristic_tier5(const Tier5State& t) {
    int corner_coset = t.corner_id & 511;
    int u_par = t.center_byte & 1;
    int d_par = (t.center_byte >> 2) & 1;
    int ud = u_par | (d_par << 1);
    int mslice = coords::SLICE4_COMPOSITE_M[t.slice_m_id] >> 5;
    int d = coords::s6_dist_get(coords::s6_joint_index(corner_coset, ud, mslice));
    if (d == 0 && !tier5_condition4_ok(t)) d = coords::S6_PARITY_MISMATCH_PENALTY;
    double inadm_raw = g_cost67;
    double h = d + inadm_raw;
    int h_int = (int)std::ceil(h);
    return h_int;
}

// Tier6State: the terminal tier's own tracked coordinates (corner's rank
// within H, 0..95; and the "edges+centers" dense index, 0..110591).
struct Tier6State { int8_t corner_h; int32_t ec; };

// The S5->S6 handoff, DELIBERATELY special (see dfs_phase6 below): derives
// Tier6State directly and algebraically from an ALREADY-transitioned
// Tier5State that just satisfied tier5_is_in_s6 -- no full-cube
// reconstruction at all, per the user's own point 10.2.
static inline Tier6State derive_tier6_from_tier5(const Tier5State& t) {
    Tier6State r;
    r.corner_h = (int8_t)(t.corner_id >> 9);
    int m_rank = coords::SLICE4_COMPOSITE_M[t.slice_m_id] & 31;
    int s_rank = coords::SLICE4_COMPOSITE_S[t.slice_s_id] & 31;
    int eq_rank = t.eq_perm;
    int mask = 0;
    int u_rot = t.center_byte & 3, d_rot = (t.center_byte >> 2) & 3;
    if (u_rot == 2) mask |= 1;
    if (d_rot == 2) mask |= 2;
    if ((t.center_byte >> 5) & 1) mask |= 4;   // L
    if ((t.center_byte >> 4) & 1) mask |= 8;   // R
    if ((t.center_byte >> 6) & 1) mask |= 16;  // F
    if ((t.center_byte >> 7) & 1) mask |= 32;  // B
    r.ec = coords::EC_DENSE[coords::ec_raw_index(m_rank, s_rank, eq_rank, mask)];
    return r;
}

// Full-reconstruction fallback for Tier6State -- used only for the rare
// double-jump where an EARLIER tier's move lands directly in tier 6 (or
// beyond), skipping the S5->S6 checkpoint dfs_phase6 itself handles lazily.
static inline Tier6State extract_tier6(const State6& s) {
    Tier6State r;
    r.corner_h = (int8_t)coords::h_rank_of(s.corner_sticker);
    r.ec = coords::ec_dense_of(s.wing_slot.data(), s.center_slot);
    return r;
}

static inline Tier6State apply_tier6_move(const Tier6State& t, int m) {
    int ti = coords::FULL_TO_TIER6_MOVE_INDEX[m];
    Tier6State r;
    r.corner_h = (int8_t)coords::H_TRANS[t.corner_h][ti];
    r.ec = coords::EC_TRANS[t.ec][ti];
    return r;
}

static inline bool tier6_is_in_s7(const Tier6State& t) {
    return coords::s7_dist_get(coords::s7_joint_index(t.corner_h, t.ec)) == 0;
}
static inline int heuristic_tier6(const Tier6State& t) {
    int d = coords::s7_dist_get(coords::s7_joint_index(t.corner_h, t.ec));
    return d;
}

// ================================================================ classification
static inline void init_lazy_tables() {
    init_phase2_lazy_tables();
    init_tier3_moves();
}

// The deepest S_k the state belongs to (0..6).
static inline int classify_tier(const State6& s) {
    if (!is_in_s1(s)) return 0;
    int fb2 = compute_fb2(s);
    if (!(s.wing == (int32_t)p2full::WING_SOLVED_MASK && p2full::FB_IS_SOLVED[fb2])) return 1;
    int lr2520 = coords::CENTER2520_LR[compute_lr2(s)];
    int fb96 = coords::FB96_RANK_OF[fb2];
    int fb24 = (fb96 < 0) ? 0 : coords::FB24_FROM_FB96RANK[fb96];
    auto pairing = coords::compute_pairing(s.wing_slot.data());
    uint32_t di = pairing_dense_index(pairing_rank_full(pairing));
    if (!(coords::CENTER_GOOD[coords::center_idx(lr2520, fb24)] && coords::WING_GOOD[di])) return 2;
    int ud2520 = coords::CENTER2520_UD[compute_ud2(s)];
    bool at_s4 = (ud2520 == 0) && (lr2520 == 0) && (fb24 == 0) && (pairing == PAIRING_SOLVED12);
    if (!at_s4) return 3;
    if (s5_dist_of(s) != 0) return 4;
    if (g_ls || !g_legacy_tail) return ls_member_of(s) ? 6 : 5;
    return (s6_dist_of(s) == 0) ? 6 : 5;
}

// Full-state heuristic: classify, then evaluate the tier's lazy heuristic on the
// extracted lazy state. Used at tier handoffs (the search itself walks lazy
// states); the self-tests check it against the independent full-cube distances.
static inline int heuristic(const State6& s, int* out_tier = nullptr) {
    int tier = classify_tier(s);
    if (out_tier) *out_tier = tier;
    switch (tier) {
        case 0: return heuristic_tier0(s.s1);
        case 1: return heuristic_tier1(extract_tier1(s));
        case 2: return s2sw::heuristic(s2sw::extract(s));
        case 3: return heuristic_tier3(extract_tier3(s));
        case 4: return heuristic_tier4(extract_tier4(s));
        case 5: return (g_ls || !g_legacy_tail) ? ls_h_of(s) : heuristic_tier5(extract_tier5(s));
        default: return (g_ls || !g_legacy_tail) ? 0 : heuristic_tier6(extract_tier6(s));
    }
}
