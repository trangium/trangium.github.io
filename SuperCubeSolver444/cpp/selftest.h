// Startup self-tests. Everything the search trusts is checked here against an
// independent ground truth (the full-cube arrays and the is_in_s*/s*_dist_of
// predicates), so a bug in a table, a lazy transition or a tier boundary aborts
// before any search runs:
//   * coordinate / rotation / conjugation sanity
//   * per tier K=1..6: lazy transitions == re-extraction from the real cube,
//     next-tier membership, and (tiers 4-6) lazy distances == full-cube distances,
//     starting from RANDOMIZED tier entries (not just literal solved, which is a
//     degenerate special case that has hidden bugs before)
//   * S3'->S4 tables: admissibility by witness (see test_s4_witness)
// A failing test prints the first mismatches and returns false.
#pragma once
#include "random_start.h"


static State6 solved_state() { return Cube().state(); }

struct TestReport {
    int failures = 0;
    void done(const char* name, long long checked, long long bad) {
        printf("  %-46s %lld/%lld ok%s\n", name, checked - bad, checked, bad ? "   *** FAILED ***" : "");
        failures += (bad != 0);
    }
};

// ---- lazy-state equality (one overload per tier) ------------------------------
static bool same(const p1::MaskState& a, const p1::MaskState& b) { return a == b; }
static bool same(const Tier1State& a, const Tier1State& b) { return a.wing == b.wing && a.fb2 == b.fb2 && a.ud2520 == b.ud2520 && a.lr2520 == b.lr2520; }
static bool same(const Tier3State& a, const Tier3State& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
static bool same(const Tier4State& a, const Tier4State& b) { return a.corner_ori == b.corner_ori && a.lr_rot == b.lr_rot && a.eq_coord == b.eq_coord && a.center_sum == b.center_sum; }
static bool same(const Tier5State& a, const Tier5State& b) { return a.corner_id == b.corner_id && a.slice_m_id == b.slice_m_id && a.slice_s_id == b.slice_s_id && a.eq_perm == b.eq_perm && a.center_byte == b.center_byte; }
static bool same(const Tier6State& a, const Tier6State& b) { return a.corner_h == b.corner_h && a.ec == b.ec; }

// ---- per-tier test hooks: does the full-cube state enter tier K+1, and (tiers
// 4-6) what does the independent full-cube distance say? -------------------------
template <int K> static bool heavy_enters_next(const State6& s) {
    if constexpr (K == 0) return is_in_s1(s);
    if constexpr (K == 1) return is_in_s2(s);
    if constexpr (K == 2) return is_in_s3prime(s);
    if constexpr (K == 3) return is_in_s4(s);
    if constexpr (K == 4) return is_in_s5(s);
    if constexpr (K == 5) return is_in_s6(s);
    return is_in_s7(s);
}
template <int K> static bool lazy_enters_next(const typename Tier<K>::S& t) {
    if constexpr (K == 0) return p1::mask_class(t) >= 0;   // S1 mod rotation (the State6 is canonicalized, the lazy state is not)
    if constexpr (K == 1) return tier1_is_in_s2(t);
    if constexpr (K == 3) return tier3_is_in_s4(t);
    if constexpr (K == 4) return tier4_is_in_s5(t);
    if constexpr (K == 5) return tier5_is_in_s6(t);
    if constexpr (K == 6) return tier6_is_in_s7(t);
    return false;
}
// Lazy tier-K heuristic minus its flat COST terms == the full-cube distance (K = 4,5,6).
template <int K> static int lazy_raw_dist(const typename Tier<K>::S& t) {
    if constexpr (K == 4) return heuristic_tier4(t) - g_cost56 - g_cost67;
    if constexpr (K == 5) return heuristic_tier5(t) - g_cost67;
    if constexpr (K == 6) return heuristic_tier6(t);
    return -1;
}
template <int K> static int heavy_raw_dist(const State6& s) {
    if constexpr (K == 4) return s5_dist_of(s);
    if constexpr (K == 5) return s6_dist_of(s);
    if constexpr (K == 6) return s7_dist_of(s);
    return -1;
}

// Walks each tier's own move set from solved (which keeps a state in S_K) to a
// randomized entry, extracts the lazy state, then walks on step by step and
// checks lazy vs full cube at every step.
template <int K>
static void test_tier(TestReport& rep, std::mt19937& rng, int trials) {
    const auto& moves = TIER_MOVES[K].allowed[NUM_MOVES];
    std::uniform_int_distribution<int> pick(0, (int)moves.size() - 1);
    std::uniform_int_distribution<int> entry_steps(1, 40), steps(1, 15);
    long long checked = 0, bad = 0;
    auto note = [&](const char* what, int trial, int step) {
        if (++bad <= 5) printf("    tier %d %s mismatch (trial %d step %d)\n", K, what, trial, step);
    };
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int e = entry_steps(rng); e > 0; e--) c.apply(moves[pick(rng)]);
        State6 s = c.state();
        if (classify_tier(s) < K) { note("entry-not-in-tier (tier move set does not preserve S_K)", trial, -1); continue; }
        if (classify_tier(s) != K) continue;
        typename Tier<K>::S t = Tier<K>::extract(s);
        for (int step = steps(rng); step > 0; step--) {
            int m = moves[pick(rng)];
            c.apply(m);
            s = c.state();
            if constexpr (K == 0) { t = p1::step(t, m); if (p1::mask_class(t) > 0) t = p1::solved(); }   // canonical like the State6
            else if constexpr (K == 1) t = apply_tier1_move(t, m);
            else if constexpr (K == 3) t = apply_tier3_move(t, m);
            else if constexpr (K == 4) t = apply_tier4_move(t, m);
            else if constexpr (K == 5) t = apply_tier5_move(t, m);
            else t = apply_tier6_move(t, m);
            checked++;
            int tier = classify_tier(s);
            if (tier < K) { note("move left the tier", trial, step); break; }
            // an S4 image (S4 after a half turn): the State6 was canonicalized by the half turn, the lazy state is not
            const bool s4_image = (K == 3) && coords::tier3_s4_class(*(const Tier3State*)&t) > 0;
            if (s4_image) { if (!is_in_s4(s)) note("S4 image was not canonicalized into S4", trial, step); break; }
            if (!same(Tier<K>::extract(s), t)) { note("lazy transition != re-extraction", trial, step); break; }
            bool heavy_next = heavy_enters_next<K>(s), lazy_next = lazy_enters_next<K>(t);
            if (heavy_next != lazy_next) { note("next-tier membership", trial, step); break; }
            if constexpr (K >= 4) {
                if (!heavy_next && lazy_raw_dist<K>(t) != heavy_raw_dist<K>(s)) { note("distance", trial, step); break; }
            }
            if (tier != K) break;  // entered the next tier: continue with a fresh trial
        }
    }
    char name[64];
    snprintf(name, sizeof(name), "tier-%d lazy transitions/membership%s", K, K >= 4 ? "/distance" : "");
    rep.done(name, checked, bad);
}

// The S5->S6 handoff (derive_tier6_from_tier5) against full reconstruction.
static void test_handoff56(TestReport& rep, std::mt19937& rng, int trials) {
    const auto& moves = TIER_MOVES[5].allowed[NUM_MOVES];
    std::uniform_int_distribution<int> pick(0, (int)moves.size() - 1), entry(1, 30), steps(1, 15);
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int e = entry(rng); e > 0; e--) c.apply(moves[pick(rng)]);
        State6 s = c.state();
        if (classify_tier(s) != 5) continue;
        Tier5State t = extract_tier5(s);
        for (int step = steps(rng); step > 0; step--) {
            int m = moves[pick(rng)];
            c.apply(m);
            s = c.state();
            t = apply_tier5_move(t, m);
            if (!is_in_s6(s)) continue;
            checked++;
            if (!same(derive_tier6_from_tier5(t), extract_tier6(s))) bad++;
            break;
        }
    }
    rep.done("tier 5->6 handoff (derive vs reconstruct)", checked, bad);
}

// Rotation classes: forward-rotating solved must match its class and canonicalize back.
static void test_rotation_classes(TestReport& rep) {
    State6 solved = solved_state();
    long long bad = 0;
    for (int c = 0; c < 6; c++) {
        State6 r = solved;
        for (int i = 0; i < 24; i++) r.center_slot[i] = ROTATE_CENTER[c][i];
        for (int i = 0; i < 24; i++) r.wing_slot[i] = ROTATE_WING[c][i];
        for (int i = 0; i < 24; i++) r.corner_sticker[i] = ROTATE_CORNER[c][i];
        r.s1 = p1::extract(r.wing_slot.data(), r.center_slot.data());
        if (p1::mask_class(r.s1) != c) { bad++; continue; }
        int applied = canonicalize_if_rotated(r);
        if (applied != (c == 0 ? -1 : c) || r.center_slot != solved.center_slot || r.wing_slot != solved.wing_slot ||
            r.corner_sticker != solved.corner_sticker || !is_in_s1(r)) bad++;
    }
    rep.done("rotation classes round-trip to canonical S1", 6, bad);
}

// A fork's conjugation must keep the state inside the boundary's group. Probes
// are built by walking the boundary group's OWN move set from solved (random
// walks in a bigger group essentially never land in S2/S4).
static void test_conjugation(TestReport& rep, std::mt19937& rng) {
    struct Case { const char* name; int tier; std::vector<int> classes; };
    // S1 group moves = tier-1 moves; S2 = tier-2; S4 = tier-4.
    std::vector<Case> cases = {{"conjugation keeps S1 (classes y,x)", 1, {3, 1}},
                               {"conjugation keeps S2 (class z)", 2, {2}},
                               {"conjugation keeps S4 (class z)", 4, {2}}};
    for (auto& cs : cases) {
        const auto& moves = TIER_MOVES[cs.tier].allowed[NUM_MOVES];
        std::uniform_int_distribution<int> pick(0, (int)moves.size() - 1);
        long long checked = 0, bad = 0;
        for (int trial = 0; trial < 200; trial++) {
            Cube c;
            for (int i = 0; i < 60; i++) c.apply(moves[pick(rng)]);
            State6 probe = c.state();
            auto in_group = [&](const State6& s) { return cs.tier == 1 ? is_in_s1(s) : cs.tier == 2 ? is_in_s2(s) : is_in_s4(s); };
            if (!in_group(probe)) { bad++; continue; }
            for (int cls : cs.classes) { checked++; if (!in_group(conjugate_state6(probe, cls))) bad++; }
        }
        rep.done(cs.name, checked, bad);
    }
}

// h7 spot checks: hand-counted sequences of TIER6 moves from solved
// (U2 D2 R2 L2 F2 B2 are raw moves 1,7,10,16,19,25) with known distances.
static void test_h7_cases(TestReport& rep) {
    struct H7Case { std::vector<int> moves; int expect; };
    std::vector<H7Case> cases = {
        {{1}, 1}, {{7}, 1}, {{1, 7}, 2}, {{1, 1}, 0}, {{16}, 1}, {{10}, 1}, {{19}, 1}, {{25}, 1},
        {{1, 10, 19}, 3}, {{16, 7, 16, 25, 16, 7, 16, 25}, 8}, {{10, 1, 19, 1, 10, 19, 1}, 7},
        {{10, 19, 10, 1, 7, 16, 25, 16, 1, 7}, 10}, {{10, 1, 10, 1, 10, 1, 10}, 5}};
    long long bad = 0;
    for (auto& c : cases) {
        Cube cube;
        for (int m : c.moves) cube.apply(m);
        if (s7_dist_of(cube.state()) != c.expect) bad++;
    }
    rep.done("h7 hand-verified sequences", (long long)cases.size(), bad);
}

// Admissibility witness for the S3'->S4 tables (and a check of extract_tier3 and
// the coordinate transitions): from solved, scramble with <U,D,R,L,F2,B2> (which
// provably preserves is_in_s4) to X, then apply n further S3'-set moves, the
// first a slice half turn (Rw2/Uw2/Fw2). Y is exactly n moves from S4, so no
// table may report more than n.
static void test_s4_witness(TestReport& rep, std::mt19937& rng, int trials) {
    const int* step1 = coords::TIER4_MOVES;
    std::uniform_int_distribution<int> p1d(0, coords::TIER4_NUM_MOVES - 1), nd(1, 8);
    const auto& moves3 = TIER_MOVES[3].allowed[NUM_MOVES];
    std::uniform_int_distribution<int> p3d(0, (int)moves3.size() - 1);
    static const int SLICE[3] = {4, 13, 22};  // Uw2, Rw2, Fw2
    std::uniform_int_distribution<int> sd(0, 2);
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int i = 0; i < 200; i++) c.apply(step1[p1d(rng)]);
        if (!is_in_s4(c.state())) { bad++; continue; }
        int n = nd(rng);
        c.apply(SLICE[sd(rng)]);
        for (int i = 1; i < n; i++) c.apply(moves3[p3d(rng)]);
        State6 y = c.state();
        if (classify_tier(y) < 3) { bad++; continue; }
        if (classify_tier(y) != 3) continue;
        checked++;
        const Tier3State t3 = extract_tier3(y);
        if (s4sym::dist1(t3) > n || s4sym::dist2(t3) > n) bad++;
    }
    rep.done("S3'->S4 tables admissible by witness", checked, bad);
}

// S4 up to a half turn: for a random literal S4 state t and each half turn k (x2, y2, z2), the state (k . t) is recognized as an
// S4 image of class 5 + k by the full-cube test and by the lazy coordinates (distance 0 in both tables), extract6 turns it
// back to exactly t and reports the rotation, and the literal state is not an image. The three images and the identity are the
// only S4 images: the other 20 whole-cube rotations of a solved cube are not even in S1.
static void test_s4_images(TestReport& rep, std::mt19937& rng, int trials) {
    const int* step1 = coords::TIER4_MOVES;
    std::uniform_int_distribution<int> p1d(0, coords::TIER4_NUM_MOVES - 1);
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int i = 0; i < 200; i++) c.apply(step1[p1d(rng)]);
        const State6 t = c.state();
        if (!is_in_s4(t) || s4_k_class(t) != 0) { bad++; continue; }
        for (int k = 1; k <= 3; k++) {
            Cube r = c;
            apply_raw_rotation_open(r.wing, r.center, r.corner, 5 + k);   // the forward half turn, applied after t
            const State6 raw = rs::raw_state(r);
            checked++;
            if (s4_k_class(raw) != k || is_in_s4(raw)) { bad++; continue; }
            const Tier3State lazy = extract_tier3(raw);
            if (coords::tier3_s4_class(lazy) != k || s4sym::dist1(lazy) != 0 || s4sym::dist2(Tier3State{lazy.layer, lazy.ud, (int16_t)(lazy.rest % 576)}) != 0) { bad++; continue; }
            int rot = -1;
            const State6 back = r.state(&rot);
            if (rot != 5 + k || !same_state(back, t)) bad++;
        }
    }
    rep.done("S4 up to a half turn: images recognized, canonicalized back to the literal state", checked, bad);
}

// The symmetry-reduced S3'->S4 tables (s4_sym.h). A distance field is exactly the BFS distance to the goals iff (a) it is 0 on
// the goals and nowhere else, (b) neighbouring states differ by at most 1, (c) every non-goal has a neighbour one closer; so this
// checks the reduced tables on random states of the full product space without any full table: (a) goal <=> 0, (b) and (c) on
// all 13 moves, and (d) invariance under a random element of the symmetry group (the class / element canonicalization is right
// and every duplicate of an orbit was filled). `--check-s4-sym` compares every entry with the plain full BFS.
static void test_s4_sym(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> lay(0, s4sym::N_LAYER - 1), ud(0, s4sym::N_UD - 1), rest(0, s4sym::N_REST - 1);
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Tier3State t{lay(rng), (int16_t)ud(rng), (int16_t)rest(rng)};
        if (trial % 4 == 0) t.layer = 0;   // exercise the neighbourhood of the goals too
        if (trial % 4 == 1) t.rest = (int16_t)(rest(rng) % 1152);
        const int d1 = s4sym::dist1(t), d2 = s4sym::dist2(t);
        checked++;
        if (d1 > s4sym::DIST_CAP || d2 > s4sym::DIST_CAP) { bad++; continue; }
        bool goal1 = false, goal2 = false;
        for (int k = 0; k < 4; k++) {
            goal1 = goal1 || (t.layer == 0 && t.ud == coords::S4_GOAL_UD[k]);
            goal2 = goal2 || (t.rest == coords::S4_GOAL_REST[k] && t.ud == coords::S4_GOAL_UD[k]);
        }
        if ((d1 == 0) != goal1 || (d2 == 0) != goal2) bad++;
        bool down1 = d1 == 0 || d1 == s4sym::DIST_CAP, down2 = d2 == 0 || d2 == s4sym::DIST_CAP;   // no descent claim on the plateau of the cap
        for (int k = 0; k < coords::S4_NM; k++) {
            const Tier3State c = coords::tier3_apply(t, k);
            const int c1 = s4sym::dist1(c), c2 = s4sym::dist2(c);
            if (std::abs(c1 - d1) > 1 || std::abs(c2 - d2) > 1) { bad++; break; }
            down1 = down1 || c1 == d1 - 1;
            down2 = down2 || c2 == d2 - 1;
        }
        if (!down1 || !down2) bad++;
        const size_t e1 = rng() % s4sym::R1.elems(), e2 = rng() % s4sym::R2.elems();
        if (s4sym::R1.dist(s4sym::R1.b[e1][t.layer], s4sym::R1.ud[e1][t.ud]) != d1) bad++;
        if (s4sym::R2.dist(s4sym::R2.b[e2][t.rest], s4sym::R2.ud[e2][t.ud]) != d2) bad++;
    }
    rep.done("S3'->S4 reduced tables: goals, 1-Lipschitz, descent, group invariance", checked, bad);
}

static void test_basics(TestReport& rep) {
    State6 solved = solved_state();
    long long bad = 0;
    if (coords::CENTER2520_UD[0] != 0 || coords::CENTER2520_LR[0] != 0 || coords::FB24_FROM_FB96RANK[0] != 0) bad++;
    if (classify_tier(solved) != 6 || heuristic(solved) != 0) bad++;
    if (!(is_in_s4(solved) && is_in_s5(solved) && (!g_legacy_tail || is_in_s6(solved)) && is_in_s7(solved))) bad++;
    if (!(solved.s1 == p1::solved())) bad++;
    rep.done("solved state is tier 6 with h=0", 1, bad);
}


// ---- NISS algebra ---------------------------------------------------------------

// Inversion and left multiplication against raw move-by-move simulation:
//   invert(state(W))       == state(W^-1)
//   leftmul(state(W), m^2) == state(m^2 then W)
//   conjugation commutes with both (the pending free move is conjugated with the state)
static void test_niss_algebra(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1), len(3, 40), rot(1, 5);
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        std::vector<int> w;
        for (int i = len(rng); i > 0; i--) w.push_back(mv(rng));
        Cube a, b;
        for (int m : w) a.apply(m);
        for (int i = (int)w.size() - 1; i >= 0; i--) b.apply(inverse_move(w[i]));
        State6 sa = a.state();
        checked++;
        if (!same_state(invert_state6(sa), b.state())) bad++;
        int q;
        do q = mv(rng); while (q % 3 == 1);
        Pending g = make_pending(q);
        Cube c;
        c.apply((q / 3) * 3 + 1);
        for (int m : w) c.apply(m);
        State6 lm = leftmul_state6(sa, g);
        checked++;
        if (!same_state(lm, c.state())) bad++;
        int cls = rot(rng);
        checked++;
        if (!same_state(conjugate_state6(lm, cls), leftmul_state6(conjugate_state6(sa, cls), conj_pending(g, cls)))) bad++;
        checked++;
        if (!same_state(conjugate_state6(invert_state6(sa), cls), invert_state6(conjugate_state6(sa, cls)))) bad++;
    }
    rep.done("NISS inversion / left multiplication / conjugation", checked, bad);
}

// The lazy tier-6 left-multiplication tables against full-cube left multiplication,
// and conjugated face half turns staying face half turns.
static void test_t6_leftmul(TestReport& rep, std::mt19937& rng, int trials) {
    static const int FACE_HALVES[6] = {1, 7, 10, 16, 19, 25};
    const auto& moves = TIER_MOVES[6].allowed[NUM_MOVES];
    std::uniform_int_distribution<int> pick(0, (int)moves.size() - 1), n(1, 60), fh(0, 5), rot(1, 5);
    long long checked = 0, bad = 0;
    for (int cls = 1; cls < 6; cls++) for (int h : FACE_HALVES) { checked++; if (CONJ_FACE_HALF[cls][h] < 0) bad++; }
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int i = n(rng); i > 0; i--) c.apply(moves[pick(rng)]);
        State6 s = c.state();
        if (classify_tier(s) != 6) { bad++; continue; }
        int h = FACE_HALVES[fh(rng)];
        Pending g = make_pending(h - 1);
        if (rng() & 1) { int cls = rot(rng); g = conj_pending(g, cls); h = g.face_half; }
        checked++;
        Tier6State lazy = t6_leftmul(extract_tier6(s), h);
        Tier6State full = extract_tier6(leftmul_state6(s, g));
        if (!same(lazy, full)) bad++;
    }
    rep.done("tier-6 left multiplication (lazy tables vs full cube)", checked, bad);
}

// A free move (face half turn, or a wide half turn while still in S1/S2) must not
// change the tier of a state, and for tiers 3-6 (targets that contain it) must not
// change the distance either -- that is what makes carrying it along "free".
template <int K> static void test_free_move_invariance(TestReport& rep, std::mt19937& rng, int trials, bool wide) {
    const auto& moves = TIER_MOVES[K].allowed[NUM_MOVES];
    std::uniform_int_distribution<int> pick(0, (int)moves.size() - 1), n(1, 60), fh(0, 5), fw(0, 2);
    static const int FACE_HALVES[6] = {1, 7, 10, 16, 19, 25};
    static const int WIDE_QUARTERS[3] = {3, 12, 21};
    long long checked = 0, bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        for (int i = n(rng); i > 0; i--) c.apply(moves[pick(rng)]);
        State6 s = c.state();
        if (classify_tier(s) != K) continue;
        Pending g = wide ? make_pending(WIDE_QUARTERS[fw(rng)]) : make_pending(FACE_HALVES[fh(rng)] - 1);
        State6 gs = leftmul_state6(s, g);
        checked++;
        int t2;
        int h1 = heuristic(s), h2 = heuristic(gs, &t2);
        // A wide free move can carry an S2 state into S3' (that set is not invariant under it -- the
        // reason wide free moves fork on reaching S2); everything else keeps the tier. Distances are
        // invariant exactly when the tier's target contains the free move (tiers 3-5), not at tier 6.
        if (wide && K == 2 ? t2 < 2 : t2 != K) { bad++; continue; }
        if (K >= 3 && K <= 5 && h1 != h2) bad++;
    }
    char name[80];
    snprintf(name, sizeof(name), "free move keeps tier %d%s", K, (K >= 3 && K <= 5) ? " and heuristic" : "");
    rep.done(wide ? (std::string(name) + " (wide)").c_str() : name, checked, bad);
}

// assemble_niss on the worked examples: "A (B C D E) F G" -> A F G E' D' C' B',
// "(A) B (C) D E (F G)" -> B D E G' F' C' A', a flipped free move, and a rotation letter.
static void test_assembly(TestReport& rep) {
    const int A = 0, B = 6, C = 9, D = 15, E = 18, F = 24, G = 3, X = 21, RW = 12;
    auto inv = [](int m) { return inverse_move(m); };
    const int SW = NISS_SWITCH_BASE;
    struct Case { std::vector<int> path, expect; };
    std::vector<Case> cases = {
        {{A, SW, B, C, D, E, SW, F, G}, {A, F, G, inv(E), inv(D), inv(C), inv(B)}},
        {{SW, A, SW, B, SW, C, SW, D, E, SW, F, G}, {B, D, E, inv(G), inv(F), inv(C), inv(A)}},
        {{X, RW, SW + 1 + RW, B, NISS_FLIP_TOKEN, SW, C}, {X, RW + 2, C, inv(B)}},
        {{A, P2_OPEN_SENTINEL_BASE + 2, SW, B}, {A, P2_OPEN_SENTINEL_BASE + 2, inv(B), ROTATION_SENTINEL_BASE + 2}},
    };
    long long bad = 0;
    for (auto& c : cases) if (assemble_niss(c.path) != c.expect) bad++;
    rep.done("NISS solution assembly (worked examples)", (long long)cases.size(), bad);
}

// ---- forced prefix / suffix (forced.h) -------------------------------------------------
static std::vector<int> alg_of(const char* text) {
    std::vector<int> w;
    std::string bad = parse_alg(text, w);
    if (!bad.empty()) { printf("BUG: test algorithm has unknown token %s\n", bad.c_str()); abort(); }
    return w;
}

// L S P Z = id  =>  S P Z L = id, for any Z that solves L S P (here Z = the plain inverse word of L S P).
static void test_forced_algebra(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1), len(0, 5), slen(1, 40);
    long long bad = 0;
    for (int t = 0; t < trials; t++) {
        std::vector<int> S, P, L;
        for (int i = slen(rng); i > 0; i--) S.push_back(mv(rng));
        for (int i = len(rng); i > 0; i--) P.push_back(mv(rng));
        for (int i = len(rng); i > 0; i--) L.push_back(mv(rng));
        std::vector<int> scrambled = L; scrambled.insert(scrambled.end(), S.begin(), S.end()); scrambled.insert(scrambled.end(), P.begin(), P.end());
        std::vector<int> Z = inverse_alg(scrambled);
        std::vector<int> full = P; full.insert(full.end(), Z.begin(), Z.end()); full.insert(full.end(), L.begin(), L.end());
        Cube c;
        for (int m : S) c.apply(m);
        apply_solution(c.wing, c.center, &c.corner_parity, c.corner, full);
        Cube id;
        bool ok = true;
        for (int i = 0; i < 24; i++) ok = ok && c.wing[i] == i && c.center[i] == i && c.corner[i] == i;
        if (!ok) bad++;
    }
    rep.done("forced ends: L S P Z = id => S (P Z L) = id", trials, bad);
}

static void test_forced_cancels(TestReport& rep, std::mt19937& rng, int trials) {
    long long checked = 0, bad = 0;
    auto expect = [&](const char* name, const char* p, const char* z, const char* l, bool want) {
        checked++;
        if (forced_cancels(alg_of(p), alg_of(z), alg_of(l)) != want) { bad++; printf("    forced_cancels case failed: P=[%s] Z=[%s] L=[%s] expected %d (%s)\n", p, z, l, (int)want, name); }
    };
    // the worked example: P = "U F" -- Z may not start with F, F', F2, nor with "B F" (B commutes with F)
    expect("F", "U F", "F", "", true);
    expect("F'", "U F", "F' R", "", true);
    expect("F2", "U F", "F2", "", true);
    expect("B F", "U F", "B F R", "", true);
    expect("B' F2 (any powers, through a commuting move)", "U F", "B' F2 R", "", true);
    expect("B2 F'", "U F", "B2 F'", "", true);
    expect("B' F2 with F2 in P", "U F2", "B' F'", "", true);
    expect("B' F2 with P ending Fw", "U Fw", "B' F2", "", false);
    expect("B", "U F", "B R", "", false);
    expect("Fw is another layer set", "U F", "Fw", "", false);
    expect("B R F: R blocks", "U F", "B R F", "", false);
    expect("U does not commute with F", "U F", "U", "", false);
    expect("B then F in P", "U B", "F", "", false);
    expect("B then F in P, B in Z", "U B", "F B", "", true);
    // suffix mirror
    expect("suffix R R", "", "U R", "R F", true);
    expect("suffix through L", "", "R L", "R", true);
    expect("suffix blocked by U", "", "R U", "R U", false);
    expect("suffix R' R", "", "F R'", "R2", true);
    // whole-cube rotations inside the word: exactly one of U/D re-turns the layer F turned before x
    {   // exactly one of U / D re-turns the layer F turned before the rotation x
        checked++;
        if (forced_cancels(alg_of("F"), alg_of("x U"), {}) == forced_cancels(alg_of("F"), alg_of("x D"), {})) { bad++; printf("    forced_cancels: F [x] U/D should split\n"); }
    }
    expect("F [x] F does not", "F", "x F", "", false);
    expect("F [z] F (z turns about F's axis)", "F", "z F", "", true);
    expect("rotation in the prefix", "F x", "U", "", forced_cancels(alg_of("F"), alg_of("x U"), {}));
    expect("rotation closing Z before the suffix", "", "F x", "U", forced_cancels(alg_of("F"), alg_of("x U"), {}));
    // random rotation-free words against the plain letter rule: a move of Z merges into P iff it is in the
    // trailing same-axis block of P with a family that block contains, as long as Z is still in its first block
    auto letter_rule = [](const std::vector<int>& p, const std::vector<int>& z, const std::vector<int>& l) {
        auto merges = [](const std::vector<int>& before, const std::vector<int>& after) {
            if (before.empty()) return false;
            const int axis = MOVE_GROUP[before.back()];
            std::vector<int> fam;
            for (int i = (int)before.size() - 1; i >= 0 && MOVE_GROUP[before[i]] == axis; i--) fam.push_back(before[i] / 3);
            for (int m : after) {
                if (MOVE_GROUP[m] != axis) break;
                if (std::find(fam.begin(), fam.end(), m / 3) != fam.end()) return true;
            }
            return false;
        };
        return merges(p, z) || merges(z, l);
    };
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1), len(0, 4), zlen(1, 6);
    int hits = 0;
    for (int t = 0; t < trials; t++) {
        std::vector<int> p, z, l;
        for (int i = len(rng); i > 0; i--) p.push_back(mv(rng));
        for (int i = zlen(rng); i > 0; i--) z.push_back(mv(rng));
        for (int i = len(rng); i > 0; i--) l.push_back(mv(rng));
        bool a = forced_cancels(p, z, l), b = letter_rule(p, z, l);
        hits += a;
        checked++;
        if (a != b) {
            if (++bad <= 4) printf("    forced_cancels mismatch: P=[%s] Z=[%s] L=[%s] exact=%d letters=%d\n", format_solution(p).c_str(), format_solution(z).c_str(), format_solution(l).c_str(), (int)a, (int)b);
        }
    }
    if (hits == 0 || hits == trials) bad++;   // the random test must see both outcomes
    rep.done("forced ends: cancellation check (cases, letter rule)", checked, bad);
}

// The pretended previous move (Forced::virtual_last) must forbid every single first move of Z that cancels with
// P, and (inverse side) every single first move of the inverse scramble whose inverse would cancel with L.
// P and L may contain rotations; it may forbid more than that, never less.
static void test_forced_virtual_last(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1), len(0, 4), rotk(0, 4);
    const Forced saved = g_forced;
    long long checked = 0, bad = 0, forbidding = 0;
    auto random_alg = [&](int n) {
        std::vector<int> w;
        for (int i = 0; i < n; i++) {
            int r = rotk(rng);
            if (r == 0) w.push_back(P2_OPEN_SENTINEL_BASE + 1 + (int)(rng() % 3));
            else if (r == 1) w.push_back(ROTATION_SENTINEL_BASE + 1 + (int)(rng() % 3));
            else w.push_back(mv(rng));
        }
        return w;
    };
    for (int t = 0; t < trials; t++) {
        g_forced = Forced();
        g_forced.prefix = random_alg(len(rng));
        g_forced.suffix = random_alg(len(rng));
        // rotation letters already in the word the search starts (fork letters land there under --niss)
        std::vector<int> extra;
        for (int i = (int)(rng() % 3); i > 0; i--) extra.push_back(rng() % 2 ? P2_OPEN_SENTINEL_BASE + 1 + (int)(rng() % 3) : ROTATION_SENTINEL_BASE + 1 + (int)(rng() % 3));
        for (int side = 0; side < 2; side++) {
            const int vl = g_forced.virtual_last(side, extra);
            std::vector<int> pre = g_forced.prefix, suf = extra;
            pre.insert(pre.end(), extra.begin(), extra.end());
            suf.insert(suf.end(), g_forced.suffix.begin(), g_forced.suffix.end());
            for (int z = 0; z < NUM_MOVES; z++) {
                const bool allowed = std::find(TIER_MOVES[0].allowed[vl].begin(), TIER_MOVES[0].allowed[vl].end(), z) != TIER_MOVES[0].allowed[vl].end();
                const bool cancels = side == 0 ? forced_cancels(pre, {z}, {}) : forced_cancels({}, {inverse_move(z)}, suf);
                checked++;
                forbidding += cancels;
                if (cancels && allowed) {
                    if (++bad <= 4) printf("    virtual last miss: side %d P=[%s] L=[%s] z=%s vl=%s\n", side, format_solution(g_forced.prefix).c_str(), format_solution(g_forced.suffix).c_str(), MOVE_NAMES[z], vl < NUM_MOVES ? MOVE_NAMES[vl] : "none");
                }
            }
        }
    }
    g_forced = saved;
    if (forbidding == 0) bad++;
    rep.done("forced ends: pretended last move forbids what cancels", checked, bad);
}

// ---- randomized start (random_start.h, s1table.h); run only with --random-start --------------------------
static int perm_sign24(const int* a) {   // +1 even, -1 odd
    bool seen[24] = {false};
    int sign = 1;
    for (int i = 0; i < 24; i++) {
        if (seen[i]) continue;
        int len = 0;
        for (int j = i; !seen[j]; j = a[j]) { seen[j] = true; len++; }
        if (len % 2 == 0) sign = -sign;
    }
    return sign;
}

// ---- phase 1: the mask state and its endtable (phase1.h) ---------------------------------
// p1::step along a random walk == p1::extract of the real cube's arrays (masks and wing parity)
static void test_p1_step(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1);
    long long checked = 0, bad = 0;
    for (int t = 0; t < trials; t++) {
        Cube c;
        p1::MaskState ms = p1::extract(c.wing, c.center);
        for (int i = 0; i < 40; i++) {
            const int m = mv(rng);
            c.apply(m);
            ms = p1::step(ms, m);
            checked++;
            if (!(ms == p1::extract(c.wing, c.center))) { bad++; break; }
        }
    }
    rep.done("phase 1: mask step == masks of the real cube", checked, bad);
}

// ---- phase 2: the wing coset mask and its distance table (phase2.h) ----------------------------
// (a) wing_step along a random walk == wing_mask_of the real cube's wing slots; (b) the low 23 bits identify
// the mask (every one of the 2,704,156 masks with 12 ones gets its own index, and the top bit is recoverable);
// (c) neighbouring cosets have distances within 1 of each other and the solved coset is at 0.
static void test_wing_mask(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1);
    long long checked = 0, bad = 0;
    for (int t = 0; t < trials; t++) {
        Cube c;
        uint32_t mask = p2full::wing_mask_of(c.wing);
        if (mask != p2full::WING_SOLVED_MASK) bad++;
        for (int i = 0; i < 40; i++) {
            const int m = mv(rng);
            c.apply(m);
            const uint32_t nm = p2full::wing_step(mask, m);
            checked++;
            if (nm != p2full::wing_mask_of(c.wing)) { bad++; break; }
            const int d0 = g_wing.dist[p2full::wing_index(mask)], d1 = g_wing.dist[p2full::wing_index(nm)];
            if (d0 == 255 || d1 == 255 || std::abs(d0 - d1) > 1) { bad++; break; }
            mask = nm;
        }
    }
    rep.done("phase 2: wing mask step == mask of the real cube, distances 1-Lipschitz", checked, bad);

    std::vector<bool> used((size_t)1 << 23, false);
    long long masks = 0, clash = 0;
    for (uint32_t m = 0; m < (1u << 24); m++) {
        if (__builtin_popcount(m) != 12) continue;
        masks++;
        const uint32_t idx = p2full::wing_index(m);
        const uint32_t top = 12 - __builtin_popcount(idx);   // the missing bit 23
        if (used[idx] || (top << 23 | idx) != m) clash++;
        used[idx] = true;
    }
    rep.done("phase 2: low 23 bits index every wing mask uniquely", masks, clash + (masks != NUM_WING_COSETS ? 1 : 0));
    rep.done("phase 2: solved coset at distance 0", 1, g_wing.dist[p2full::wing_index(p2full::WING_SOLVED_MASK)] != 0);
}

// ---- phase 2: the joint (wing mask, F/B permutation) endtable (phase2_endtable.h) ---------------
// Moves left to S2 (wing solved, F/B centers solved) by an independent DFS over the 21 phase-2 moves.
static bool p2_dfs(uint32_t wing, int fb2, int left) {
    if (wing == p2full::WING_SOLVED_MASK && p2full::FB_IS_SOLVED[fb2]) return true;
    if (left == 0) return false;
    for (int pm = 0; pm < P2_NUM_MOVES; pm++)
        if (p2_dfs(p2full::wing_step(wing, P2_TO_FULL_MOVE_INDEX[pm]), fb2_trans(fb2, pm), left - 1)) return true;
    return false;
}
// (a) a state k <= depth moves from S2 is reported at distance <= k (no false negatives); (b) up to distance 4 the
// answer is exact (no shorter path exists); (c) the exact layer sizes; (d) false positive rate on random pairs.
static void test_p2_endtable(TestReport& rep, std::mt19937& rng, int trials, long long probes) {
    const int depth = p2j::g_depth;
    std::uniform_int_distribution<int> pmv(0, P2_NUM_MOVES - 1);
    long long checked = 0, bad = 0, exact = 0;
    for (int t = 0; t < trials; t++) {
        uint32_t wing = p2full::WING_SOLVED_MASK;
        int fb2 = FB_SOLVED_INDICES[rng() % 96];
        const int k = 1 + (int)(rng() % depth);
        for (int i = 0; i < k; i++) {
            const int pm = pmv(rng);
            wing = p2full::wing_step(wing, P2_TO_FULL_MOVE_INDEX[pm]);
            fb2 = fb2_trans(fb2, pm);
        }
        const int d = p2j::dist(wing, fb2);
        checked++;
        if (d > k) { bad++; continue; }
        if (d >= 1 && d <= 4) { exact++; if (p2_dfs(wing, fb2, d - 1)) bad++; }
    }
    if (exact < trials / 10) bad++;   // the exactness half must actually run
    rep.done("phase 2 endtable: distance <= walk, no shorter path", checked, bad);

    static const size_t EXACT[6] = {96, 192, 2784, 28032, 234816, 2060160};
    long long layer_bad = 0;
    for (int d = 0; d < depth && d < 6; d++) if (p2j::LAYER_SIZES[d] != EXACT[d]) layer_bad++;
    rep.done("phase 2 endtable: layer sizes", depth, layer_bad);

    long long fp = 0;
    for (long long i = 0; i < probes; i++) {   // a random pair is within the table's reach with probability ~1e-5
        int pos[24];
        for (int j = 0; j < 24; j++) pos[j] = j;
        std::shuffle(pos, pos + 24, rng);
        uint32_t mask = 0;
        for (int j = 0; j < 12; j++) mask |= 1u << pos[j];
        if (p2j::dist(mask, (int)(rng() % NUM_FB_PERMS)) <= depth) fp++;
    }
    const double rate = (double)fp / (double)probes;
    printf("    (phase 2 endtable false positive rate measured on %lld random pairs: %.5f, target %.5f)\n", probes, rate, p2j::g_fpr);
    rep.done("phase 2 endtable: false positive rate <= 3x target", probes, rate > 3 * p2j::g_fpr ? 1 : 0);
}

// ---- S2 -> S3' switch route (s2switch.h) ----------------------------------------------------
// the independent ground truth: is the INVERSE of the cube in S3' (or deeper)?
static bool inverse_in_s3prime(const State6& s) { return classify_tier(invert_state6(s)) >= 3; }

// shortest path to the goal of both coordinates by DFS (raw wing arrays; the L/R part through its table)
static bool sw_dfs(const s2sw::Tier2AltState& t, int left) {
    if (s2sw::is_goal(t)) return true;
    if (left == 0) return false;
    for (int m : TIER_MOVES[2].allowed[NUM_MOVES])
        if (sw_dfs(s2sw::apply_move(t, m), left - 1)) return true;
    return false;
}

// (a) the two conditions (L/R arrangement, wing classes) are exactly "the inverse is in S3'" on S2 states, both on
// random S2 states and on states built as the inverse of an S3' word (so many are positive); (b) the lazy state steps
// like a re-extraction; (c) the wing index is invariant under the S4 x V4 relabelings and commutes with the moves,
// decodes back to itself; (d) both distance tables never exceed the walk length from a goal and are exact up to 3;
// (e) the goal sets have the expected sizes.
// The table-based wing index == the comparison-based reference, and the budgeted S2 heuristic == the plain one up to the
// budget: same goal verdict, same h when not pruned, and a value above the budget exactly when the plain h is above it.
static void test_s2_fast(TestReport& rep, std::mt19937& rng, int trials) {
    long long bad = 0, checked = 0;
    std::uniform_int_distribution<int> move(0, s2sw::NCOL - 1);
    for (int t = 0; t < trials; t++) {
        Cube c;
        for (int i = 0; i < 60; i++) c.apply(P2_TO_FULL_MOVE_INDEX[coords::S2_OWN_MOVES_PM[move(rng)]]);
        State6 s = c.state();
        if (!is_in_s2(s)) { bad++; continue; }
        s2sw::Tier2AltState st = s2sw::extract(s);
        for (int step = 0; step < 20; step++) {
            st = s2sw::apply_move(st, P2_TO_FULL_MOVE_INDEX[coords::S2_OWN_MOVES_PM[move(rng)]]);
            checked++;
            if (s2sw::wing_index(st.a, st.b) != s2sw::wing_index_ref(st.a, st.b)) { bad++; break; }
            int h_plain = 0;
            const bool goal_plain = s2sw::at_goal_else_h(st, &h_plain);
            for (int r : {0, 3, 7, 12, 18, 25, 40}) {
                s2sw::g_r = r;
                int h = 0;
                const bool goal = s2sw::at_goal_else_h_budget(st, &h);
                if (goal != goal_plain || (!goal && (h > r) != (h_plain > r)) || (!goal && h <= r && h != h_plain)) { bad++; break; }
            }
        }
    }
    rep.done("S2: wing index table == reference, budgeted h == plain h", checked, bad);
}

// The S5 joint distance table is exactly the BFS distance field: zero only at the goal, neighbours differ by at most 1,
// and every other state has a neighbour one closer (checked on all 4,330,260 states x 14 moves, ~0.1 s).
static void test_s5_dist(TestReport& rep) {
    const int N = coords::CORNER_ORI_NUM * coords::S5_LR_NUM * coords::S5_EQ_NUM;
    const int start = coords::s5_joint_index(0, 0, coords::EQUATOR_GOAL_RANK);
    long long bad = 0;
    for (int idx = 0; idx < N; idx++) {
        const int d = coords::s5_dist_get(idx);
        if ((d == 0) != (idx == start)) bad++;
        const int eq = idx % coords::S5_EQ_NUM, rem = idx / coords::S5_EQ_NUM;
        const int lr = rem % coords::S5_LR_NUM, corner = rem / coords::S5_LR_NUM;
        bool down = idx == start;
        for (int t = 0; t < coords::TIER4_NUM_MOVES; t++) {
            const int n = coords::s5_dist_get(coords::s5_joint_index(coords::CORNER_ORI_TRANS[corner][t], lr ^ coords::lr_delta(coords::TIER4_MOVES[t]), coords::EQUATOR_TRANS[eq][t]));
            if (std::abs(n - d) > 1) bad++;
            if (n == d - 1) down = true;
        }
        if (!down) bad++;
    }
    rep.done("S5 joint table is the exact distance field", N, bad);
}

// The S2-switch wing class table (1,470,150 classes) is exactly the BFS distance field: zero only on the goal classes, neighbours
// differ by at most 1, every other class has a neighbour one closer (all classes x 17 moves, ~0.3 s). This also proves the
// shortcut in build_wing (last layer filled in without expanding it).
static void test_s2_wing_dist(TestReport& rep) {
    long long bad = 0;
    for (uint32_t idx = 0; idx < s2sw::NWING; idx++) {
        const int d = s2sw::WING_DIST[idx];
        const uint32_t pair = idx / 6u;
        const bool goal = pair / 495u == pair % 495u && idx % 6u == (uint32_t)s2sw::COSET_IDENTITY;
        if ((d == 0) != goal) bad++;
        uint8_t a[4], b[4], na[4], nb[4];
        s2sw::wing_decode(idx, a, b);
        bool down = goal;
        for (int c = 0; c < s2sw::NCOL; c++) {
            for (int j = 0; j < 4; j++) { na[j] = s2sw::STEP_POS[c][a[j]]; nb[j] = s2sw::STEP_NEG[c][b[j]]; }
            const int n = s2sw::WING_DIST[s2sw::wing_index(na, nb)];
            if (std::abs(n - d) > 1) bad++;
            if (n == d - 1) down = true;
        }
        if (!down) bad++;
    }
    rep.done("S2 switch wing classes: exact distance field", s2sw::NWING, bad);
}

// The wing coset distance table is exactly the BFS distance field (zero only at the solved coset, neighbours differ by at
// most 1, every other coset has a neighbour one closer): all 2,704,156 cosets x 27 moves. This also proves the bottom-up
// last layers of build_wing_distance_table.
static void test_wing_dist_field(TestReport& rep) {
    long long bad = 0, checked = 0;
    const uint32_t solved = p2full::WING_SOLVED_MASK;
    for (uint32_t i = 0; i < ((uint32_t)1 << 23); i++) {
        const int ones = __builtin_popcount(i);
        if (ones != 11 && ones != 12) continue;
        const uint32_t mask = ones == 11 ? (i | (1u << 23)) : i;
        const int d = g_wing.dist[i];
        checked++;
        if (d == 255 || (d == 0) != (mask == solved)) { bad++; continue; }
        bool down = mask == solved;
        for (int m = 0; m < NUM_MOVES; m++) {
            const int n = g_wing.dist[p2full::wing_index(p2full::wing_step(mask, m))];
            if (std::abs(n - d) > 1) bad++;
            if (n == d - 1) down = true;
        }
        if (!down) bad++;
    }
    rep.done("wing coset table is the exact distance field", checked, bad);
}

static void test_s2switch(TestReport& rep, std::mt19937& rng, int trials) {
    const auto& s2moves = TIER_MOVES[2].allowed[NUM_MOVES];
    const auto& s3moves = TIER_MOVES[3].allowed[NUM_MOVES];
    long long checked = 0, bad = 0, pos = 0, neg = 0, lazy_checked = 0, lazy_bad = 0;
    for (int trial = 0; trial < trials; trial++) {
        Cube c;
        if (trial % 2 == 0) {
            for (int i = 1 + (int)(rng() % 40); i > 0; i--) c.apply(s2moves[rng() % s2moves.size()]);
        } else {   // inverse of a word over the S3' moves, then 0..3 more S2 moves
            std::vector<int> g;
            for (int i = 1 + (int)(rng() % 30); i > 0; i--) g.push_back(s3moves[rng() % s3moves.size()]);
            for (int i = (int)g.size() - 1; i >= 0; i--) c.apply(inverse_move(g[i]));
            for (int i = (int)(rng() % 4); i > 0; i--) c.apply(s2moves[rng() % s2moves.size()]);
        }
        State6 s = rs::raw_state(c);   // not c.state(): that would turn an S4 image back
        if (classify_tier(s) < 2) { bad++; continue; }
        const bool truth = inverse_in_s3prime(s);
        const s2sw::Tier2AltState t = s2sw::extract(s);
        checked++;
        (truth ? pos : neg)++;
        if (truth != s2sw::is_goal(t)) bad++;
        // (b) a few lazy steps
        s2sw::Tier2AltState lt = t;
        for (int k = 0; k < 6; k++) {
            const int m = s2moves[rng() % s2moves.size()];
            c.apply(m);
            lt = s2sw::apply_move(lt, m);
            const s2sw::Tier2AltState re = s2sw::extract(rs::raw_state(c));   // not c.state(): that would turn an S4 image back
            lazy_checked++;
            if (re.lr8 != lt.lr8 || re.ud2520 != lt.ud2520 || memcmp(re.a, lt.a, 4) || memcmp(re.b, lt.b, 4)) { lazy_bad++; break; }
        }
    }
    if (pos < trials / 20 || neg < trials / 20) bad++;   // both outcomes must be exercised
    rep.done("S2 switch route: conditions == inverse is in S3'", checked, bad);
    rep.done("S2 switch route: lazy state steps like re-extraction", lazy_checked, lazy_bad);

    // (c) relabeling invariance of the wing index
    static const int V4[4][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {2, 3, 0, 1}, {3, 2, 1, 0}};
    long long ichecked = 0, ibad = 0;
    for (int trial = 0; trial < 20000; trial++) {
        uint8_t a[4], b[4];
        int pa[12], pb[12];
        for (int i = 0; i < 12; i++) pa[i] = pb[i] = i;
        std::shuffle(pa, pa + 12, rng);
        std::shuffle(pb, pb + 12, rng);
        for (int j = 0; j < 4; j++) { a[j] = (uint8_t)pa[j]; b[j] = (uint8_t)pb[j]; }
        int sg[4] = {0, 1, 2, 3};
        std::shuffle(sg, sg + 4, rng);
        const int v = (int)(rng() % 4);
        uint8_t a2[4], b2[4];
        for (int k = 0; k < 4; k++) { a2[sg[k]] = a[k]; b2[sg[k]] = b[k]; }   // relabel the four edges
        uint8_t b3[4];
        for (int k = 0; k < 4; k++) b3[k] = b2[V4[v][k]];                      // relabel only the negative wings by a V4 element
        const uint32_t i0 = s2sw::wing_index(a, b);
        ichecked++;
        if (i0 >= s2sw::NWING || i0 != s2sw::wing_index(a2, b3)) ibad++;
        uint8_t da[4], db[4];
        s2sw::wing_decode(i0, da, db);
        if (s2sw::wing_index(da, db) != i0) ibad++;
        const int col = (int)(rng() % s2sw::NCOL);   // a move gives the same class either way
        uint8_t na[4], nb[4], na3[4], nb3[4];
        for (int j = 0; j < 4; j++) { na[j] = s2sw::STEP_POS[col][a[j]]; nb[j] = s2sw::STEP_NEG[col][b[j]]; na3[j] = s2sw::STEP_POS[col][a2[j]]; nb3[j] = s2sw::STEP_NEG[col][b3[j]]; }
        if (s2sw::wing_index(na, nb) != s2sw::wing_index(na3, nb3)) ibad++;
    }
    rep.done("S2 switch route: wing index invariant under S4 x V4 relabeling, commutes with moves", ichecked, ibad);

    // (d) from a goal state k moves away: the distances are <= k; exact up to 5 against a DFS
    long long dchecked = 0, dbad = 0, exact = 0;
    for (int trial = 0; trial < 3000; trial++) {
        Cube c;   // an S2 state whose inverse is in S3': the inverse of an S3' word
        std::vector<int> g;
        for (int i = 1 + (int)(rng() % 25); i > 0; i--) g.push_back(s3moves[rng() % s3moves.size()]);
        for (int i = (int)g.size() - 1; i >= 0; i--) c.apply(inverse_move(g[i]));
        const int k = 1 + (int)(rng() % 4);
        // most S2 moves are S3' moves and keep the goal; the R/L quarter turns are the ones that leave it
        static const int QUARTERS[4] = {P2_TO_FULL_MOVE_INDEX[6], P2_TO_FULL_MOVE_INDEX[8], P2_TO_FULL_MOVE_INDEX[9], P2_TO_FULL_MOVE_INDEX[11]};
        for (int i = 0; i < k; i++) c.apply((rng() & 1) ? QUARTERS[rng() % 4] : s2moves[rng() % s2moves.size()]);
        const s2sw::Tier2AltState t = s2sw::extract(c.state());
        const int d = s2sw::bound(t);
        dchecked++;
        if (d > k) { dbad++; printf("    switch dist %d > walk %d (lr %d wing %d)\n", d, k, (int)s2sw::LR8_DIST[t.lr8], (int)s2sw::WING_DIST[s2sw::wing_index(t.a, t.b)]); }
        if (d >= 1 && d <= 5 && exact < 120) {
            exact++;
            // no state within d-1 moves has both conditions: the max of the two tables is a lower bound on the moves
            if (sw_dfs(t, d - 1)) { dbad++; printf("    switch dist %d but a path of %d exists\n", d, d - 1); }
        }
    }
    if (exact < 60) { dbad++; printf("    only %lld of %lld samples were exactly checkable\n", exact, dchecked); }
    rep.done("S2 switch route: distances <= walk, no shorter path (d <= 5)", dchecked, dbad);

    long long zl = 0, zw = 0, unreached = 0;
    for (int i = 0; i < 40320; i++) { zl += s2sw::LR8_DIST[i] == 0; unreached += s2sw::LR8_DIST[i] == 255; }
    for (uint32_t i = 0; i < s2sw::NWING; i++) { zw += s2sw::WING_DIST[i] == 0; unreached += s2sw::WING_DIST[i] == 255; }
    rep.done("S2 switch route: goal sets (384 L/R arrangements, 495 wing classes), nothing unreached", 3, (zl != 384) + (zw != 495) + (unreached != 0));
}

// shortest path to S1 mod rotation, by an independent DFS over the real cube arrays
static bool p1_dfs(const Cube& c, int g, int limit, int last) {
    if (p1::mask_class(p1::extract(c.wing, c.center)) >= 0) return true;
    if (g == limit) return false;
    for (int m : TIER_MOVES[0].allowed[last]) {
        Cube n = c;
        n.apply(m);
        if (p1_dfs(n, g + 1, limit, m)) return true;
    }
    return false;
}
// The endtable distance of a state k moves from S1 is <= k (no false negatives, every state in the table),
// and no shorter path exists (checked by DFS while that is cheap).
static void test_p1_distance(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1), len(1, 6);
    long long checked = 0, bad = 0, exact = 0;
    for (int t = 0; t < trials; t++) {
        Cube c;
        const int k = len(rng);
        for (int i = 0; i < k; i++) c.apply(mv(rng));
        const int d = p1::dist(p1::extract(c.wing, c.center));
        checked++;
        if (d > k) { bad++; continue; }
        if (d >= 1 && d <= 4) { exact++; if (p1_dfs(c, 0, d - 1, NUM_MOVES)) bad++; }
    }
    if (exact < trials / 10) bad++;   // the exactness half must actually run
    rep.done("phase 1: endtable distance <= walk, no shorter path", checked, bad);
}
static void test_p1_fpr(TestReport& rep, std::mt19937& rng, long long probes) {
    long long members = 0, fp = 0;
    for (long long i = 0; i < probes; i++) {
        int pos[24];
        for (int j = 0; j < 24; j++) pos[j] = j;
        std::shuffle(pos, pos + 24, rng);
        uint32_t m[3] = {0, 0, 0};
        for (int j = 0; j < 24; j++) m[j / 8] |= 1u << pos[j];
        const uint64_t key = p1::make_key(m[0], m[1], m[2], (int)(rng() & 1));
        bool member = false;
        for (auto& layer : p1::LAYERS) if (std::binary_search(layer.begin(), layer.end(), key)) { member = true; break; }
        if (member) { members++; continue; }
        if (p1::BLOOM[p1::g_depth].test(key)) fp++;
    }
    const double rate = (double)fp / (double)(probes - members);
    printf("    (Bloom false positive rate measured on %lld non-members: %.5f, target %.5f)\n", probes - members, rate, p1::g_fpr);
    rep.done("phase 1: Bloom false positive rate <= 3x target", probes, rate > 3 * p1::g_fpr ? 1 : 0);
}

static void test_rs_candidates(TestReport& rep, std::mt19937& rng, int trials) {
    std::uniform_int_distribution<int> mv(0, NUM_MOVES - 1);
    const Forced saved = g_forced;
    long long checked = 0, bad = 0;
    for (int t = 0; t < trials; t++) {
        g_forced = Forced();
        for (int i = (int)(rng() % 5); i > 0; i--) g_forced.suffix.push_back(mv(rng));
        Cube c;
        apply_letters(c, g_forced.suffix);
        for (int i = 30 + (int)(rng() % 20); i > 0; i--) c.apply(mv(rng));
        const State6 x0 = rs::invert_raw(rs::raw_state(c));
        Cube lc;
        apply_letters(lc, inverse_alg(g_forced.suffix));
        int sets[6][4];
        for (int f = 0; f < 6; f++) for (int j = 0; j < 4; j++) sets[f][j] = lc.center[rs::FACE_SLOTS[f][j]];
        std::vector<rs::Candidate> cands;
        rs::Stats st;
        rs::generate_candidates(x0, sets, 4000, rng, cands, st);
        std::unordered_set<uint64_t> cosets;
        int x0pos[24];
        for (int i = 0; i < 24; i++) x0pos[i] = x0.center_slot[i];
        bool this_bad = cands.empty();
        for (auto& cd : cands) {
            int pos[24];
            for (int i = 0; i < 24; i++) pos[i] = cd.pos[i];
            // a permutation, with the same sign as X0's (an even number of transpositions: a reachable state)
            bool seen[24] = {false}; bool perm = true;
            for (int i = 0; i < 24; i++) { if (pos[i] > 23 || seen[pos[i]]) perm = false; else seen[pos[i]] = true; }
            if (!perm || perm_sign24(pos) != perm_sign24(x0pos)) this_bad = true;
            // pieces only moved inside their own set
            int set_of_pos[24];
            for (int f = 0; f < 6; f++) for (int j = 0; j < 4; j++) set_of_pos[sets[f][j]] = f;
            for (int i = 0; i < 24; i++) if (set_of_pos[pos[i]] != set_of_pos[x0pos[i]]) this_bad = true;
            // stored distance == distance of the recomputed masks; cosets distinct
            uint32_t m[3] = {0, 0, 0};
            for (int i = 0; i < 8; i++) { m[0] |= 1u << pos[UD_TARGET[i]]; m[1] |= 1u << pos[LR_TARGET[i]]; m[2] |= 1u << pos[FB_TARGET[i]]; }
            const uint64_t key = p1::make_key(m[0], m[1], m[2], x0.s1.parity);
            if (p1::dist_of_key(key) != cd.dist || !cosets.insert(key).second) this_bad = true;
        }
        checked++;
        bad += this_bad;
    }
    g_forced = saved;
    rep.done("random start: candidates are even, within their sets, distinct", checked, bad);
}

static void test_visible_solved(TestReport& rep) {
    long long checked = 0, bad = 0;
    auto expect = [&](bool got, bool want) { checked++; if (got != want) bad++; };
    Cube c;
    expect(rs::is_visibly_solved(c), true);
    Cube d = c;   // two centers of one face swapped: invisible
    std::swap(d.center[0], d.center[1]);
    expect(rs::is_visibly_solved(d), true);
    Cube e = c;   // two centers of different faces swapped: visible
    std::swap(e.center[0], e.center[20]);
    expect(rs::is_visibly_solved(e), false);
    Cube f = c;
    f.apply(coords::TIER6_MOVES[0]);
    expect(rs::is_visibly_solved(f), false);
    Cube g = c;   // a whole face's centers cycled among themselves: invisible
    for (int i = 0; i < 4; i++) g.center[coords::U_CENTER_SLOTS[i]] = coords::U_CENTER_SLOTS[(i + 1) % 4];
    expect(rs::is_visibly_solved(g), true);
    rep.done("visible-solved check (normal cube)", checked, bad);
}

// ---- leave slice (ls.h) ----------------------------------------------------------------------------------------------
static const int LS_S5_LETTERS[10] = {0, 1, 2, 6, 7, 8, 10, 16, 19, 25};   // U U2 U' D D2 D' R2 L2 F2 B2
// LS generators as raw move words: U'D and the four "R2 U'D B2 U D'" family
static const std::vector<std::vector<int>> LS_GENS = {
    {2, 6}, {10, 2, 6, 25, 0, 8}, {25, 2, 6, 16, 0, 8}, {16, 2, 6, 19, 0, 8}, {19, 2, 6, 10, 0, 8}};
static bool cube_is_solved_literal(const Cube& c) {
    for (int i = 0; i < 24; i++) if (c.wing[i] != i || c.center[i] != i || c.corner[i] != i) return false;
    return true;
}
static void ls_random_s5(Cube& c, std::mt19937& rng, int len) {
    for (int i = 0; i < len; i++) c.apply(LS_S5_LETTERS[rng() % 10]);
}
static void ls_random_member(Cube& c, std::mt19937& rng, int len) {
    for (int i = 0; i < len; i++) for (int m : LS_GENS[rng() % LS_GENS.size()]) c.apply(m);
}
// shortest literal solution of the cube by the 10 S5 letters (no rotations), at most maxd moves; -1 if none
static int ls_iddfs_opt(const State6& s, int maxd) {
    std::function<bool(const State6&, int, int)> rec = [&](const State6& t, int left, int last) -> bool {
        bool id = true;
        for (int i = 0; i < 24 && id; i++) id = t.wing_slot[i] == i && t.center_slot[i] == i && t.corner_sticker[i] == i;
        if (id) return true;
        if (left == 0) return false;
        for (int m : LS_S5_LETTERS) {
            if (last >= 0 && MOVE_GROUP[m] == MOVE_GROUP[last] && MOVE_RANK[m] <= MOVE_RANK[last]) continue;
            if (rec(apply_move(t, m), left - 1, m)) return true;
        }
        return false;
    };
    for (int d = 0; d <= maxd; d++) if (rec(s, d, -1)) return d;
    return -1;
}

static void test_ls_basic(TestReport& rep, std::mt19937& rng, int trials) {
    long long bad = 0, checked = 0;
    // members: words in the generators are in LS; U, R2 alone and random S5 words are not
    for (int t = 0; t < trials; t++) {
        Cube c; ls_random_member(c, rng, 1 + (int)(rng() % 12));
        State6 s = c.state();
        ls::State st;
        checked++;
        if (!ls::extract_state(s, st) || !ls::is_member(st) || !ls_member_of(s) || ls_h_of(s) != 0) bad++;
    }
    for (int m : {0, 1, 2, 10, 16, 19, 25}) {
        Cube c; c.apply(m);
        ls::State st; checked++;
        if (!ls::extract_state(c.state(), st) || ls::is_member(st)) bad++;
    }
    for (int t = 0; t < trials; t++) {
        Cube c; ls_random_s5(c, rng, 25);
        ls::State st; checked++;
        if (!ls::extract_state(c.state(), st)) { bad++; continue; }
        if (ls::is_member(st)) bad++;   // probability ~ 1e-9 per state
    }
    rep.done("LS: generator words are members, others not", checked, bad);
}

static void test_ls_lazy(TestReport& rep, std::mt19937& rng, int walks) {
    long long bad = 0, checked = 0;
    for (int w = 0; w < walks; w++) {
        Cube c; ls_random_s5(c, rng, 10);
        State6 s = c.state();
        ls::State st;
        if (!ls::extract_state(s, st)) { bad++; continue; }
        ls::State sc = ls::canonical(st);
        for (int step = 0; step < 40; step++) {
            const int m = ls::MV[rng() % ls::NMV];
            s = apply_move(s, m);
            st = ls::apply(st, m);
            sc = ls::apply_c(sc, m);
            ls::State ex;
            checked++;
            if (!ls::extract_state(s, ex) || !ls::same(ex, st)) { bad++; break; }
            if (ls::is_member(st) != ls_member_of(s)) { bad++; break; }
            if (!ls::same(ls::canonical(ex), sc) || ls::is_member_c(sc) != ls::is_member(st) || ls::heuristic_c(sc) != ls::heuristic(st)) { bad++; break; }
        }
    }
    rep.done("LS: lazy transitions == re-extraction", checked, bad);
}

// witness: from a member, n letters (no D) lead to a state whose distance to LS is <= n
static void test_ls_admissible(TestReport& rep, std::mt19937& rng, int trials) {
    long long bad = 0, checked = 0;
    for (int t = 0; t < trials; t++) {
        Cube c; ls_random_member(c, rng, 1 + (int)(rng() % 8));
        State6 s = c.state();
        int n = 1 + (int)(rng() % 12);
        int last = -1;
        for (int i = 0; i < n; i++) {
            int m;
            do { m = ls::MV[rng() % ls::NLS]; } while (last >= 0 && MOVE_GROUP[m] == MOVE_GROUP[last] && MOVE_RANK[m] <= MOVE_RANK[last]);
            s = apply_move(s, m); last = m;
        }
        checked++;
        if (ls_h_of(s) > n) bad++;
    }
    rep.done("LS: heuristic admissible (witness)", checked, bad);
}

// the LS pipeline against a plain search on short S5 scrambles, with literal replay of the solution
static bool ls_check_solution(const std::vector<int>& scr, const std::vector<int>& sol) {
    Cube c;
    for (int m : scr) c.apply(m);
    apply_letters(c, sol);
    return cube_is_solved_literal(c);
}
static void test_ls_short(TestReport& rep, std::mt19937& rng, int randoms) {
    long long bad = 0, checked = 0;
    std::vector<std::vector<int>> cases;
    for (const char* a : {"U D", "U' D", "R2 U R2", "R2 U' D R2", "R2 U' D B2", "U R2 U2 F2 U", "U2 D2", "D", "U", "R2", "D R2 D'", "R2 U R2 U'"}) {
        std::vector<int> w; parse_alg(a, w); cases.push_back(w);
    }
    for (int i = 0; i < randoms; i++) {
        std::vector<int> w; int n = 3 + (int)(rng() % 4);
        for (int k = 0; k < n; k++) w.push_back(LS_S5_LETTERS[rng() % 10]);
        cases.push_back(w);
    }
    for (auto& scr : cases) {
        Cube c; for (int m : scr) c.apply(m);
        State6 s = c.state();
        std::vector<int> sol;
        checked++;
        if (!ls::solve_s5(s, 12, sol)) { bad++; printf("    LS pipeline found nothing for %s\n", format_solution(scr).c_str()); continue; }
        if (!ls_check_solution(scr, sol)) { bad++; printf("    LS solution does not solve %s: %s\n", format_solution(scr).c_str(), format_solution(sol).c_str()); continue; }
        const int opt = ls_iddfs_opt(s, 6);
        if (opt >= 0 && solution_move_count(sol) > opt) { bad++; printf("    LS solution longer than the optimum (%d > %d) for %s\n", solution_move_count(sol), opt, format_solution(scr).c_str()); }
    }
    rep.done("LS: short S5 cases (literal, optimal)", checked, bad);
}

static bool run_selftests() {
    auto t0 = std::chrono::steady_clock::now();
    std::mt19937 rng(9001);
    TestReport rep;
    printf("self-tests:\n");
    test_basics(rep);
    test_rotation_classes(rep);
    test_conjugation(rep, rng);
    if (g_legacy_tail) test_h7_cases(rep);
    test_tier<0>(rep, rng, 600);
    test_tier<1>(rep, rng, 600);
    test_tier<3>(rep, rng, 600);
    test_tier<4>(rep, rng, 600);
    if (g_legacy_tail) {
        test_tier<5>(rep, rng, 1000);
        test_handoff56(rep, rng, 4000);
        test_tier<6>(rep, rng, 1000);
    }
    test_s4_witness(rep, rng, 2000);
    test_s4_images(rep, rng, 600);
    test_s4_sym(rep, rng, 300000);
    test_assembly(rep);
    test_forced_algebra(rep, rng, 300);
    test_forced_cancels(rep, rng, 4000);
    test_forced_virtual_last(rep, rng, 1500);
    test_niss_algebra(rep, rng, 3000);
    if (g_legacy_tail) test_t6_leftmul(rep, rng, 3000);
    test_free_move_invariance<1>(rep, rng, 1500, true);
    test_free_move_invariance<2>(rep, rng, 1500, true);
    test_free_move_invariance<2>(rep, rng, 1500, false);
    test_free_move_invariance<3>(rep, rng, 1500, false);
    test_free_move_invariance<4>(rep, rng, 1500, false);
    if (g_legacy_tail) {
        test_free_move_invariance<5>(rep, rng, 1500, false);
        test_free_move_invariance<6>(rep, rng, 1500, false);
    }
    test_p1_step(rep, rng, 1000);
    test_wing_mask(rep, rng, 1000);
    if (ls::g_built) {
        test_ls_basic(rep, rng, 400);
        test_ls_lazy(rep, rng, 300);
        test_ls_admissible(rep, rng, 600);
        test_ls_short(rep, rng, 40);
    }
    if (p2j::g_depth > 0) test_p2_endtable(rep, rng, 400, 1000000);
    test_s2switch(rep, rng, 30000);
    test_s2_fast(rep, rng, 3000);
    test_s5_dist(rep);
    test_s2_wing_dist(rep);
    test_wing_dist_field(rep);
    test_p1_distance(rep, rng, 400);
    if (!p1::LAYERS.empty()) test_p1_fpr(rep, rng, 300000);
    if (rs::g_on) {
        test_rs_candidates(rep, rng, 40);
        test_visible_solved(rep);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("self-tests %s (%.1fs)\n", rep.failures ? "FAILED" : "passed", secs);
    fflush(stdout);
    return rep.failures == 0;
}
