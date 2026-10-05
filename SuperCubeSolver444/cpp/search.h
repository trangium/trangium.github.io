// IDA*-with-drop-credit search over the 7 tiers.
//
// One iterative-deepening search from the scramble to solved, with a combined,
// deliberately inadmissible heuristic: each tier's own lower bound plus flat
// COSTxy gaps for every tier below it (tier_inadm_from_code). Crossing into a
// higher tier makes a COST term vanish at once, so f can DROP; when a child's f
// falls below the governing threshold the search re-runs iterative deepening
// locally on that subtree, starting `drop_credit` lower (the exactly-known
// amount shed by the tier crossing) -- see dfs_tier. `chain_first` records
// whether the threshold governing a node is the smallest its enclosing
// escalation has tried, so successive outer passes don't redo the low part of
// the range.
//
// Each tier walks its own small lazy state (Tier<K>::S, defined in state.h) and
// its own tier-restricted move set (TIER_MOVES[K]: moves that demote the tier
// are never tried). The full State6 is rebuilt, by replaying the path from the
// root, only at the moment a move crosses into the next tier (the S5->S6
// crossing derives the next lazy state directly instead).
#pragma once
#include "niss.h"
#include "forced.h"
#include "ls.h"

// ---------------------------------------------------------------- move sets
static std::vector<int> moves_phase1() {
    std::vector<int> v; for (int m = 0; m < NUM_MOVES; m++) v.push_back(m); return v;
}
static std::vector<int> moves_from_pm(const int* pm_list, int n) {
    std::vector<int> v; for (int i = 0; i < n; i++) v.push_back(P2_TO_FULL_MOVE_INDEX[pm_list[i]]); return v;
}
static std::vector<int> moves_phase2() {
    std::vector<int> v; for (int pm = 0; pm < P2_NUM_MOVES; pm++) v.push_back(P2_TO_FULL_MOVE_INDEX[pm]); return v;
}
static std::vector<int> moves_phase3() { return moves_from_pm(coords::S2_OWN_MOVES_PM, 17); }
static std::vector<int> moves_phase4() { return moves_from_pm(coords::S3PRIME_GOOD_MOVES_PM, 13); }
static std::vector<int> moves_phase5()  { return std::vector<int>(coords::TIER4_MOVES, coords::TIER4_MOVES + coords::TIER4_NUM_MOVES); }
static std::vector<int> moves_phase6()  { return std::vector<int>(coords::TIER5_MOVES, coords::TIER5_MOVES + coords::TIER5_NUM_MOVES); }
static std::vector<int> moves_phase7()  { return std::vector<int>(coords::TIER6_MOVES, coords::TIER6_MOVES + coords::TIER6_NUM_MOVES); }

// Per-tier "don't immediately undo/redo the last move" filtering, using the
// same MOVE_GROUP/MOVE_RANK canonical-order convention as before, just
// restricted to each tier's own preserving move set.
struct PhaseMoves { std::vector<int> allowed[NUM_MOVES + 1]; };
static PhaseMoves build_phase_moves(const std::vector<int>& move_set) {
    PhaseMoves pm;
    for (int p = 0; p <= NUM_MOVES; p++)
        for (int m : move_set) {
            bool ok = (p == NUM_MOVES) || (MOVE_GROUP[p] != MOVE_GROUP[m]) || (MOVE_RANK[m] > MOVE_RANK[p]);
            if (ok) pm.allowed[p].push_back(m);
        }
    return pm;
}

// TIER_MOVES[tier] is the tier-restricted, canonical-order-pruned move list
// used by dfs/dfs_inadmissible/dfs_phase1/rbfs -- moves that would demote
// the tier a node is already in are excluded entirely (per the user: rarely
// useful, and disallowing them cuts branching factor). Indexed by the same
// 0..6 tier codes as heuristic()'s out_tier / tier_inadm_from_code.
// Populated once by init_tier_moves().
static PhaseMoves TIER_MOVES[7];
static void init_tier_moves() {
    TIER_MOVES[0] = build_phase_moves(moves_phase1());
    TIER_MOVES[1] = build_phase_moves(moves_phase2());
    TIER_MOVES[2] = build_phase_moves(moves_phase3());
    TIER_MOVES[3] = build_phase_moves(moves_phase4());
    TIER_MOVES[4] = build_phase_moves(moves_phase5());
    TIER_MOVES[5] = build_phase_moves(moves_phase6());
    TIER_MOVES[6] = build_phase_moves(moves_phase7());
}

// --ls: S5 -> LS -> solved instead of S5 -> S6 -> S7 (ls.h). Tier 5 then searches the UD part with U, U', U2, R2, L2, F2, B2 only.
static void enable_ls() {
    g_ls = true;
    g_cost56 = g_cost_ls;   // the flat cost stacked on tier 4: S5 -> LS -> solved
    g_cost67 = 0;
    std::vector<int> mv;
    for (int i = 0; i < ls::NLS; i++) mv.push_back(ls::MV[i]);
    TIER_MOVES[5] = build_phase_moves(mv);
}

// ---------------------------------------------------------------- path & stats
static inline void push_move_and_rotation(std::vector<int>& path, int m, int rot) {
    path.push_back(m);
    if (rot > 0) path.push_back(ROTATION_SENTINEL_BASE + rot);
}
static inline void pop_move_and_rotation(std::vector<int>& path, int rot) {
    if (rot > 0) path.pop_back();
    path.pop_back();
}

static std::vector<int> g_path;
static long long g_nodes = 0;
// Root full State6 of the current solve(): the anchor reconstruct_full_state
// replays the path from.
static State6 g_root_state6;

// Replays `path` from g_root_state6 to recover the full cube at its end. An
// OPEN sentinel (a fork branch exploring the conjugated frame) means every
// later move was applied to the CONJUGATED state, so it must conjugate too.
// A CLOSE sentinel is only appended on the success-unwind (or is a
// canonicalization letter apply_move re-derives itself), so it is skipped.
// NISS tokens: SWITCH inverts the state (and starts a pending free move if it
// carries one), FLIP left-multiplies by the pending free move.
static void replay_tokens(State6& cur, Pending& pend, const std::vector<int>& path, size_t from) {
    for (size_t i = from; i < path.size(); i++) {
        const int e = path[i];
        if (e < NUM_MOVES) cur = apply_move(cur, e);
        else if (is_niss_token(e)) {
            if (e == NISS_FLIP_TOKEN) { cur = leftmul_state6(cur, pend); pend.active = false; }
            else { cur = invert_state6(cur); int code = e - NISS_SWITCH_BASE; pend = code > 0 ? make_pending(code - 1) : Pending(); }
        }
        else if (e >= P2_OPEN_SENTINEL_BASE) {
            int cls = e - P2_OPEN_SENTINEL_BASE;
            cur = conjugate_state6(cur, cls);
            if (pend.active) pend = conj_pending(pend, cls);
        }
        else if (e - ROTATION_SENTINEL_BASE >= 6) rotate_close_state6(cur, e - ROTATION_SENTINEL_BASE);   // S4 half turn
        // (the closing letters of classes 1..5 are skipped: apply_move canonicalized the S1 state itself)
    }
}
static State6 reconstruct_full_state(const std::vector<int>& path, Pending* out_pend = nullptr) {
    State6 cur = g_root_state6;
    Pending pend;
    replay_tokens(cur, pend, path, 0);
    if (out_pend) *out_pend = pend;
    return cur;
}

// Checkpoints: every tier crossing knows the full state its path leads to (the child it is about to search), so it pushes
// (path length, state, live pending free move) here for the duration of that subtree; reconstruct_g_path then replays only
// the tokens since the innermost checkpoint instead of the whole path from the root (the replay was ~17% of all time).
// Stack discipline keeps every checkpoint a prefix of g_path (try_variant pops it before it restores the path).
struct StateCheckpoint { size_t len; State6 st; Pending pend; };
static std::vector<StateCheckpoint> g_ck;

// The solution Z as it will read once the search succeeds at the current path: fork rotations still open are
// closed again innermost first (the success-unwind of try_variant does exactly that), or, with --niss, the N
// word followed by the inverse of the I word. Without --forced this is only used for the equality check in solve().
static std::vector<int> finished_path(const std::vector<int>& path) {
    if (g_niss) return assemble_niss(path);
    std::vector<int> out = path;
    for (int i = (int)path.size() - 1; i >= 0; i--)
        if (is_open_rotation_entry(path[i])) out.push_back(ROTATION_SENTINEL_BASE + rotation_class_of(path[i]));
    return out;
}
// Called when the search stands on the solved cube: with a forced prefix/suffix, a solution that would cancel
// with it is not a solution (see forced.h) and the search just goes on.
static bool goal_accepted() {
    if (!g_forced.active()) return true;
    g_forced.checked++;
    std::vector<int> z = finished_path(g_path);
    std::vector<int> zr = z;
    if (g_forced.root_rot > 0) zr.insert(zr.begin(), ROTATION_SENTINEL_BASE + g_forced.root_rot);
    if (forced_cancels(g_forced.prefix, zr, g_forced.suffix)) { g_forced.rejected++; return false; }
    g_forced.last_accepted = z;
    return true;
}

// Search-time NISS context. `side` is 0 on the normal scramble, 1 on the inverse;
// wl[side] is the last move of that side's word (for canonical-order pruning
// after a switch back); pend is the free move waiting on the other side.
struct NissCtx { int side = 0; int wl[2] = {NUM_MOVES, NUM_MOVES}; Pending pend; };
static NissCtx g_ctx;
// --niss stats: variants tried / winning, indexed [flip][switch].
static long long g_niss_attempts[2][2] = {{0, 0}, {0, 0}};
static long long g_niss_wins[2][2] = {{0, 0}, {0, 0}};
static long long g_niss_root_wins[2] = {0, 0};
// --check-replay: every explored NISS branch is compared against a from-scratch replay of its path.
static bool g_check_replay = false;
static long long g_replay_checked = 0, g_replay_bad = 0, g_replay_pend_bad = 0;
static bool same_pending(const Pending& a, const Pending& b) {
    return a.active == b.active && (!a.active || (a.wide == b.wide && a.face_half == b.face_half && a.c == b.c && a.w == b.w && a.k == b.k));
}
// The full state at the end of g_path, replayed from the innermost checkpoint. With --check-replay it is compared with the
// from-the-root replay every time.
static long long g_ck_checked = 0, g_ck_bad = 0;
static bool g_use_ck = true;   // --no-checkpoints: replay from the root every time (A/B timing)
static State6 reconstruct_g_path() {
    State6 cur;
    Pending pend;
    size_t from = 0;
    if (g_ck.empty() || !g_use_ck) cur = g_root_state6;
    else { const StateCheckpoint& c = g_ck.back(); cur = c.st; pend = c.pend; from = c.len; }
    replay_tokens(cur, pend, g_path, from);
    if (g_check_replay) {
        g_ck_checked++;
        if (!same_state(cur, reconstruct_full_state(g_path))) { if (++g_ck_bad <= 5) printf("CHECKPOINT MISMATCH (path length %zu, checkpoint at %zu)\n", g_path.size(), g_ck.empty() ? (size_t)0 : g_ck.back().len); }
    }
    return cur;
}
// switch-eligible crossings that create a free move / of those, how many had a commuting quarter turn right before m
static long long g_niss_sw_cross = 0, g_niss_cluster_cross = 0;

// --tier-stats: nodes spent per tier (mutually exclusive; sums to total nodes)
// and how many times a move newly crossed into S_k.
static bool g_tier_stats = false;
static long long g_spent_tier[7] = {0};
static long long g_entered_s[8] = {0};  // index 0 unused
static inline void record_tier_stats(int tier) { if (g_tier_stats) g_spent_tier[tier]++; }
static inline void reset_tier_stats() {
    for (int i = 0; i < 7; i++) g_spent_tier[i] = 0;
    for (int i = 0; i < 8; i++) g_entered_s[i] = 0;
}

// --ls: where tier 5 (S5, not yet LS) began in g_path (the first letter of the word X) and at which g; g_ls_in5 is set
// while a tier-5 subtree is being searched (so a crossing from it into LS keeps these, a jump from tier 4 sets them).
static size_t g_ls_x_start = 0;
static int g_ls_g5 = 0;
static bool g_ls_in5 = false;

// LS -> solved at the LS state the path leads to. The word X searched since S5 began is REPLACED by an equivalent word Y of
// at most threshold - g_ls_g5 letters (the depth the S5 -> LS search ran at) that also solves the E slice; a pending NISS
// FLIP token stays after it. With forced ends a Y whose solution would cancel is refused and the search goes on.
static bool ls_terminal(int g, int threshold) {
    (void)g;
    const State6 cur = reconstruct_g_path();
    std::vector<int> X, tail;
    for (size_t i = g_ls_x_start; i < g_path.size(); i++) (g_path[i] < NUM_MOVES ? X : tail).push_back(g_path[i]);
    const std::vector<int> saved = g_path;
    const size_t start = g_ls_x_start;
    const long long n0 = ls::g_nested_nodes;
    std::vector<int> y;
    const bool ok = ls::terminal(cur, X, threshold - g_ls_g5, y, [&](const std::vector<int>& entries) {
        g_path.resize(start);
        g_path.insert(g_path.end(), entries.begin(), entries.end());
        g_path.insert(g_path.end(), tail.begin(), tail.end());
        if (goal_accepted()) return true;
        g_path = saved;
        return false;
    });
    g_nodes += ls::g_nested_nodes - n0;
    if (ok && g_tier_stats) g_entered_s[7]++;
    return ok;
}

// ---------------------------------------------------------------- forks
// When a move first crosses a fork boundary the search splits into branches
// {identity, conjugates by whole-cube rotations} and tries each; which one
// wins changes the remaining subproblem only where the later tiers' move sets
// are not symmetric under that rotation (see CLAUDE.md, "fork" sections).
//   fork 0: S0->S1  {id, y (cls 3), x (cls 1)}   -- stats slots id/x/y
//   fork 1: S1->S2  {id, z (cls 2)}
//   fork 2: S3'->S4 {id, z (cls 2)}
struct Fork { bool guard = false; long long attempts[3] = {0, 0, 0}; long long wins[3] = {0, 0, 0}; };
static Fork g_fork[3];
static constexpr int fork_of_tier(int tier) { return tier == 0 ? 0 : tier == 1 ? 1 : tier == 3 ? 2 : -1; }
static inline int fork_slot(int fork, int cls) { return cls == 0 ? 0 : (fork == 0 ? (cls == 1 ? 1 : 2) : 1); }

struct ForkCandidate { State6 state; int open_cls; };
static void build_fork_candidates(int fork, const State6& ns, std::vector<ForkCandidate>& out) {
    out.push_back({ns, 0});
    if (fork == 0) { for (int cls : {3, 1}) out.push_back({conjugate_state6(ns, cls), cls}); }
    else out.push_back({conjugate_state6(ns, 2), 2});
}

// ---------------------------------------------------------------- forced ends under NISS
// Pretended previous move (Forced::virtual_last) of each word that has no move yet, given the rotation letters
// already in it: an OPEN letter goes into both words, a canonicalization letter only into the word the search
// was in. Such a letter at the boundary of Z changes which layer the boundary move turns.
static void refresh_virtual_wl(const std::vector<int>& path, int wl[2]) {
    std::vector<int> lead[2];
    bool moved[2] = {false, false};
    int cur = 0;
    for (int e : path) {
        if (e == NISS_FLIP_TOKEN) continue;
        if (e >= NISS_SWITCH_BASE) { cur ^= 1; continue; }
        if (e < NUM_MOVES) { moved[cur] = true; continue; }
        if (is_open_rotation_entry(e)) { for (int s = 0; s < 2; s++) if (!moved[s]) lead[s].push_back(e); }
        else if (!moved[cur]) lead[cur].push_back(e);
    }
    if (!moved[0]) wl[0] = lead[0].empty() ? g_forced.base[0] : g_forced.virtual_last(0, lead[0]);
    if (!moved[1]) {
        if (lead[1].empty()) wl[1] = g_forced.base[1];
        else {
            std::vector<int> ex;   // inverse(I) ends with the inverses of its leading letters, last first
            for (int i = (int)lead[1].size() - 1; i >= 0; i--) ex.push_back(inverse_letter(lead[1][i]));
            wl[1] = g_forced.virtual_last(1, ex);
        }
    }
}

// ---------------------------------------------------------------- NISS variants
// One branch of a tier crossing (move m from tier K into a deeper tier, `ns` the
// resulting cube). Path tokens are pushed in the order  m [rot]  FLIP?  SWITCH?  OPEN?.
struct NissVar { State6 state; int open_cls; int flip; int sw; int lm; NissCtx ctx; };

// All branches of a crossing: {apply the pending free move or not} x {switch or
// not} x {rotation-fork candidates}. A FLIP is only meaningful when the pending
// free move must be resolved anyway (a switch back, or a tier whose target is
// not invariant under it); otherwise it just rides along in ctx.
// `force_sw` (the S2 -> S3' switch route, tier 2): ns satisfies the conditions under which its INVERSE is in S3', so
// only the switching branches exist.
static void build_niss_variants(int fk, const State6& ns, int m, int rot, int prev, std::vector<NissVar>& out, bool force_sw = false) {
    const bool guarded = (fk >= 0) && g_fork[fk].guard;
    const int ntier = classify_tier(ns);
    const NissCtx& c0 = g_ctx;
    const bool P = c0.pend.active;
    const bool trigger = P && ((c0.pend.wide && ntier >= 2) || (!c0.pend.wide && ntier >= 6));
    const bool new_pend_ok = rot <= 0 && (m % 3) != 1 && (is_wide_family(m) ? ntier == 1 : ntier <= 5);
    // The path that enters with m^-1 is reached by flipping m, so when a free move is created only ONE
    // orientation of the entering quarter turn may switch (else both nodes would explore the same class).
    const bool sw_ok = !guarded && (force_sw || ntier == 1 || ntier == 2 || ntier == 4 || (ntier == 5 && !(g_ls && g_ls_no_s5_switch))) && !(new_pend_ok && (m % 3) == 2);
    if (sw_ok && new_pend_ok && !force_sw) {
        g_niss_sw_cross++;
        if (prev < NUM_MOVES && MOVE_GROUP[prev] == MOVE_GROUP[m] && prev % 3 != 1 && !is_wide_family(prev)) g_niss_cluster_cross++;
    }
    for (int flip = 0; flip < 2; flip++) {
        for (int sw = 0; sw < 2; sw++) {
            if (flip && !(P && (trigger || sw))) continue;
            if (sw && !sw_ok) continue;
            if (force_sw && !sw) continue;
            State6 s = flip ? leftmul_state6(ns, c0.pend) : ns;
            NissCtx c = c0;
            if (sw) {
                s = invert_state6(s);
                c.wl[c.side] = m;
                c.side ^= 1;
                c.pend = new_pend_ok ? make_pending(m) : Pending();
            } else if (flip || trigger) c.pend.active = false;
            std::vector<ForkCandidate> cands;
            if (fk >= 0 && !guarded) build_fork_candidates(fk, s, cands);
            else cands.push_back({s, 0});
            for (auto& fc : cands) {
                NissCtx cc = c;
                if (fc.open_cls > 0 && cc.pend.active) cc.pend = conj_pending(cc.pend, fc.open_cls);
                if (g_forced.active() && (fc.open_cls > 0 || sw)) {
                    // the path this variant will have (the tokens try_variant pushes), for the words' boundary rotations
                    std::vector<int> tmp = g_path;
                    push_move_and_rotation(tmp, m, rot);
                    if (flip) tmp.push_back(NISS_FLIP_TOKEN);
                    if (sw) tmp.push_back(NISS_SWITCH_BASE);
                    if (fc.open_cls > 0) tmp.push_back(P2_OPEN_SENTINEL_BASE + fc.open_cls);
                    refresh_virtual_wl(tmp, cc.wl);
                }
                out.push_back({fc.state, fc.open_cls, flip, sw, sw ? cc.wl[cc.side] : m, cc});
            }
        }
    }
}

// ---------------------------------------------------------------- tier traits
// Tier<K>::S is tier K's lazy state; extract() builds it from a full State6.
// step() applies move m: returns true iff the child enters tier K+1 (then
// child_h is unset), else sets child_h to the child's tier-K heuristic. `rot`
// is the whole-cube rotation the move forced (tier 0 only; 0 elsewhere).
template <int K> struct Tier;
template <> struct Tier<0> {
    using S = p1::MaskState;
    static S extract(const State6& s) { return s.s1; }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        nt = p1::step(t, m);
        const int cls = p1::mask_class(nt);   // S1 mod whole-cube rotation: crossing, with the rotation class (-1: none)
        if (cls >= 0) { rot = cls > 0 ? cls : -1; return true; }
        rot = -1;
        child_h = heuristic_tier0(nt);
        return false;
    }
};
template <> struct Tier<1> {
    using S = Tier1State;
    static S extract(const State6& s) { return extract_tier1(s); }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        rot = 0;
        nt = apply_tier1_move(t, m);
        if (tier1_is_in_s2(nt)) return true;
        child_h = heuristic_tier1(nt);
        return false;
    }
};
template <> struct Tier<3> {
    using S = Tier3State;
    static S extract(const State6& s) { return extract_tier3(s); }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        rot = 0;
        nt = apply_tier3_move(t, m);
        const int k = coords::tier3_s4_class(nt);   // S4 up to a half turn: crossing, with the rotation class (-1: none)
        if (k >= 0) { rot = k > 0 ? 5 + k : -1; return true; }
        child_h = heuristic_tier3(nt);
        return false;
    }
};
template <> struct Tier<4> {
    using S = Tier4State;
    static S extract(const State6& s) { return extract_tier4(s); }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        rot = 0;
        nt = apply_tier4_move(t, m);
        if (tier4_is_in_s5(nt)) return true;
        child_h = heuristic_tier4(nt);
        return false;
    }
};
template <> struct Tier<5> {
    using S = Tier5State;
    static S extract(const State6& s) {
        if (g_ls) { S t{}; ls::State raw; if (ls::extract_state(s, raw)) t.ls = ls::canonical(raw); return t; }
        return extract_tier5(s);
    }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        rot = 0;
        if (g_ls) {   // the UD part; crossing = in LS
            nt = t;
            nt.ls = ls::apply_c(t.ls, m);   // canonical states (ls.h)
            if (ls::is_member_c(nt.ls)) return true;
            child_h = ls::g_lookup == 3 ? ls::h_budget(nt.ls, ls::g_r) : ls::heuristic_c(nt.ls);
            return false;
        }
        nt = apply_tier5_move(t, m);
        if (tier5_is_in_s6(nt)) return true;
        child_h = heuristic_tier5(nt);
        return false;
    }
};
template <> struct Tier<6> {
    using S = Tier6State;
    static S extract(const State6& s) { return extract_tier6(s); }
};
// Tier 2 (S2, not yet S3'): the S2 -> S3' SWITCH route (s2switch.h). The crossing is the state whose INVERSE is in
// S3', and it is only taken together with a switch (build_niss_variants, force_sw), so this tier needs NISS.
template <> struct Tier<2> {
    using S = s2sw::Tier2AltState;
    static S extract(const State6& s) { return s2sw::extract(s); }
    static bool step(const S& t, int m, S& nt, int& rot, int& child_h) {
        rot = 0;
        nt = s2sw::apply_move(t, m);
        return s2sw::at_goal_else_h_budget(nt, &child_h);
    }
};

// Runs the search from a full child state that landed in `tier`.
static bool dispatch(int tier, const State6& child, int g, int threshold, int last_move, bool chain_first, int h);

// One node of the tier-K search; g = moves so far, `threshold` = the f-limit
// currently governing this subtree.
template <int K>
static bool dfs_tier(const typename Tier<K>::S& t, int g, int threshold, int last_move, bool chain_first, int h) {
    g_nodes++;
    if constexpr (K == 6) {
        if (g_ls) return ls_terminal(g, threshold);
        if (tier6_is_in_s7(t)) {
            if (!goal_accepted()) return false;
            if (g_tier_stats) g_entered_s[7]++;
            return true;
        }
    }
    record_tier_stats(K);
    if (g >= threshold) return false;
    if (h > threshold - g) return false;
    const int parent_inadm = tier_inadm_from_code(K);

    // Explores child `child` (a full State6 that just crossed into tier K+1 via
    // move m), escalating the threshold locally from the tier-crossing drop.
    auto try_variant = [&](const State6& child, int open_cls, int m, int rot, const NissVar* nv = nullptr) -> bool {
        int child_tier;
        int child_h = heuristic(child, &child_tier);
        int f = g + 1 + child_h;
        if (f > threshold) return false;
        int drop_credit = std::max(0, parent_inadm - tier_inadm_from_code(child_tier));
        int start_t = std::max(f, threshold - drop_credit);
        int loop_start = chain_first ? start_t : threshold;
        const int lm = nv ? nv->lm : m;
        push_move_and_rotation(g_path, m, rot);
        if (nv && nv->flip) g_path.push_back(NISS_FLIP_TOKEN);
        if (nv && nv->sw) g_path.push_back(NISS_SWITCH_BASE + (nv->ctx.pend.active ? 1 + m : 0));
        if (open_cls > 0) g_path.push_back(P2_OPEN_SENTINEL_BASE + open_cls);
        NissCtx saved_ctx;
        if (nv) { saved_ctx = g_ctx; g_ctx = nv->ctx; }
        if (g_check_replay && nv) {
            Pending rp;
            State6 replayed = reconstruct_full_state(g_path, &rp);
            g_replay_checked++;
            // the replay keeps a wide free move that reached S2 unflipped (only FLIP/SWITCH clear it there);
            // that stale copy is never used, so only a live search-side pending is compared
            const bool state_bad = !same_state(replayed, child);
            const bool pend_bad = nv->ctx.pend.active && !same_pending(rp, nv->ctx.pend);
            if (state_bad || pend_bad) {
                if (state_bad) g_replay_bad++; else g_replay_pend_bad++;
                if (g_replay_bad + g_replay_pend_bad <= 5) printf("REPLAY MISMATCH %s (crossing tier %d, flip=%d sw=%d open=%d)\n", state_bad ? "state" : "pending", K + 1, nv->flip, nv->sw, open_cls);
            }
        }
        bool found = false;
        g_ck.push_back({g_path.size(), child, nv ? nv->ctx.pend : Pending()});
        for (int t2 = loop_start; t2 <= threshold && !found; t2++) {
            bool child_first = chain_first || (f == t2);
            found = dispatch(child_tier, child, g + 1, t2, lm, child_first, child_h);
        }
        g_ck.pop_back();
        if (nv) g_ctx = saved_ctx;
        if (found) {
            // NISS closes its rotation forks through the inverse of the I word instead.
            if (open_cls > 0 && !g_niss) g_path.push_back(ROTATION_SENTINEL_BASE + open_cls);
        } else {
            if (open_cls > 0) g_path.pop_back();
            if (nv && nv->sw) g_path.pop_back();
            if (nv && nv->flip) g_path.pop_back();
            pop_move_and_rotation(g_path, rot);
        }
        return found;
    };

    for (int m : TIER_MOVES[K].allowed[last_move]) {
        if constexpr (K == 6) {
            Tier6State nt = apply_tier6_move(t, m);
            int child_h = heuristic_tier6(nt);
            int f = g + 1 + child_h;
            if (f <= threshold) {
                g_path.push_back(m);
                if (dfs_tier<6>(nt, g + 1, threshold, m, chain_first || (f == threshold), child_h)) return true;
                g_path.pop_back();
            }
        } else {
            typename Tier<K>::S nt;
            int rot = 0, child_h = 0;
            if constexpr (K == 2) s2sw::g_r = threshold - g - 1;   // the child's budget (s2sw::at_goal_else_h_budget)
            if constexpr (K == 5) ls::g_r = threshold - g - 1;   // the child's budget (ls::h_budget): set per child, deeper nodes overwrite it
            if (Tier<K>::step(t, m, nt, rot, child_h)) {
                if constexpr (K == 5) { if (!g_ls) {
                    // S5->S6: the next lazy state is derived algebraically, no full-cube rebuild.
                    if (g_tier_stats) g_entered_s[6]++;
                    // Explores the S6 child; `flip` = the pending free move was applied to it.
                    auto try6 = [&](const Tier6State& child, bool flip) -> bool {
                        int ch = heuristic_tier6(child);
                        int f = g + 1 + ch;
                        if (f > threshold) return false;
                        int drop_credit = std::max(0, parent_inadm - tier_inadm_from_code(6));
                        int start_t = std::max(f, threshold - drop_credit);
                        int loop_start = chain_first ? start_t : threshold;
                        g_path.push_back(m);
                        if (flip) g_path.push_back(NISS_FLIP_TOKEN);
                        NissCtx saved_ctx;
                        if (g_niss) { saved_ctx = g_ctx; g_ctx.pend.active = false; }
                        if (g_check_replay && g_niss) {
                            g_replay_checked++;
                            Tier6State rt = extract_tier6(reconstruct_g_path());
                            if (rt.corner_h != child.corner_h || rt.ec != child.ec) {
                                if (++g_replay_bad <= 5) printf("REPLAY MISMATCH (tier-6 lazy branch, flip=%d)\n", (int)flip);
                            }
                        }
                        bool found = false;
                        for (int t2 = loop_start; t2 <= threshold && !found; t2++)
                            found = dfs_tier<6>(child, g + 1, t2, m, chain_first || (f == t2), ch);
                        if (g_niss) g_ctx = saved_ctx;
                        if (found) return true;
                        if (flip) g_path.pop_back();
                        g_path.pop_back();
                        return false;
                    };
                    Tier6State child = derive_tier6_from_tier5(nt);
                    if (g_niss && g_ctx.pend.active) {
                        // a non-wide free move must be resolved on entering the last tier: both branches
                        g_niss_attempts[0][0]++;
                        if (try6(child, false)) { g_niss_wins[0][0]++; return true; }
                        g_niss_attempts[1][0]++;
                        if (try6(t6_leftmul(child, g_ctx.pend.face_half), true)) { g_niss_wins[1][0]++; return true; }
                    } else if (try6(child, false)) return true;
                    continue;
                } }
                {   // (with --ls the S5 -> LS crossing is an ordinary full-state crossing)
                    State6 ns = apply_move(reconstruct_g_path(), m);
                    if (rot >= 6) rotate_close_state6(ns, rot);   // S4 up to a half turn: turn the cube back (the letter goes into the path)
                    constexpr int fk = fork_of_tier(K);
                    constexpr int next_tier = K + 1;
                    if constexpr (K == 2) { if (!g_niss) continue; }   // S3' is only reached through a switch
                    if (g_niss) {
                        // NISS: {flip?} x {switch?} x {rotation fork} branches, all under this boundary's fork guard
                        if (g_tier_stats && (fk < 0 || !g_fork[fk].guard)) g_entered_s[next_tier]++;
                        std::vector<NissVar> vars;
                        build_niss_variants(fk, ns, m, rot, last_move, vars, K == 2);
                        const bool own_guard = fk >= 0 && !g_fork[fk].guard;
                        if (own_guard) g_fork[fk].guard = true;
                        bool found_any = false;
                        for (auto& v : vars) {
                            int slot = 0;
                            if (fk >= 0) { slot = fork_slot(fk, v.open_cls); if (own_guard) g_fork[fk].attempts[slot]++; }
                            g_niss_attempts[v.flip][v.sw]++;
                            if (try_variant(v.state, v.open_cls, m, rot, &v)) {
                                if (fk >= 0 && own_guard) g_fork[fk].wins[slot]++;
                                g_niss_wins[v.flip][v.sw]++;
                                found_any = true;
                                break;
                            }
                        }
                        if (own_guard) g_fork[fk].guard = false;
                        if (found_any) return true;
                        continue;
                    }
                    if constexpr (fk >= 0) {
                        Fork& fs = g_fork[fk];
                        if (!fs.guard) {
                            if (g_tier_stats) g_entered_s[next_tier]++;
                            std::vector<ForkCandidate> candidates;
                            build_fork_candidates(fk, ns, candidates);
                            bool found_any = false;
                            fs.guard = true;
                            for (auto& c : candidates) {
                                int slot = fork_slot(fk, c.open_cls);
                                fs.attempts[slot]++;
                                if (try_variant(c.state, c.open_cls, m, rot)) { fs.wins[slot]++; found_any = true; break; }
                            }
                            fs.guard = false;
                            if (found_any) return true;
                            continue;
                        }
                        // Already inside this boundary's fork branch: cross plainly.
                        if (try_variant(ns, 0, m, rot)) return true;
                    } else {
                        if (g_tier_stats) g_entered_s[next_tier]++;
                        if (try_variant(ns, 0, m, rot)) return true;
                    }
                }
                continue;
            }
            int f = g + 1 + child_h;
            if (f <= threshold) {
                g_path.push_back(m);
                if (dfs_tier<K>(nt, g + 1, threshold, m, chain_first || (f == threshold), child_h)) return true;
                g_path.pop_back();
            }
        }
    }
    return false;
}

static bool dispatch(int tier, const State6& c, int g, int threshold, int last_move, bool chain_first, int h) {
    switch (tier) {
        case 1: return dfs_tier<1>(Tier<1>::extract(c), g, threshold, last_move, chain_first, h);
        case 2: return dfs_tier<2>(Tier<2>::extract(c), g, threshold, last_move, chain_first, h);
        case 3: return dfs_tier<3>(Tier<3>::extract(c), g, threshold, last_move, chain_first, h);
        case 4: return dfs_tier<4>(Tier<4>::extract(c), g, threshold, last_move, chain_first, h);
        case 5: {
            if (!g_ls) return dfs_tier<5>(Tier<5>::extract(c), g, threshold, last_move, chain_first, h);
            const size_t sx = g_ls_x_start; const int sg = g_ls_g5; const bool sin = g_ls_in5;
            g_ls_x_start = g_path.size(); g_ls_g5 = g; g_ls_in5 = true;
            const bool r = dfs_tier<5>(Tier<5>::extract(c), g, threshold, last_move, chain_first, h);
            g_ls_in5 = sin;
            if (!r) { g_ls_x_start = sx; g_ls_g5 = sg; }
            return r;
        }
        case 6:
            if (g_ls && !g_ls_in5) { g_ls_x_start = g_path.size(); g_ls_g5 = g; }   // not reached from tier 5: no word X
            if (g_ls) return dfs_tier<6>(Tier6State{}, g, threshold, last_move, chain_first, h);
            return dfs_tier<6>(Tier<6>::extract(c), g, threshold, last_move, chain_first, h);
        default: return dfs_tier<0>(Tier<0>::extract(c), g, threshold, last_move, chain_first, h);
    }
}

// One starting point of the search: a full cube state, the path tokens that lead to it (a root switch, and
// the rotation that canonicalized it), its tier / heuristic and the NISS context.
struct Root { State6 s; std::vector<int> prefix; int tier; int h0; NissCtx ctx; };

// Pretended previous move of a root's words (forced prefix/suffix, see forced.h), once per root.
static void setup_root_forced(Root& r) {
    g_forced.base[0] = g_forced.virtual_last(0);
    g_forced.base[1] = g_forced.virtual_last(1);
    r.ctx.wl[0] = g_forced.base[0]; r.ctx.wl[1] = g_forced.base[1];
    if (g_niss && g_forced.active()) refresh_virtual_wl(r.prefix, r.ctx.wl);
}

// One IDA* iteration from one root at one threshold. `ri` is only the root's index in the NISS statistics.
// On success `out` is the solution (the assembled N word + inverse I word with --niss).
static bool try_root(Root& r, size_t ri, int threshold, std::vector<int>& out, std::string* niss_notation) {
    g_path = r.prefix;
    g_ck.clear();
    g_ctx = r.ctx;
    if (!dispatch(r.tier, r.s, 0, threshold, r.ctx.wl[r.ctx.side], threshold == r.h0, r.h0)) return false;
    if (g_niss) { g_niss_root_wins[ri]++; out = assemble_niss(g_path, niss_notation); }
    else out = g_path;
    if (g_forced.active() && out != g_forced.last_accepted) { printf("BUG: the solution differs from the one the forced-ends check accepted\n"); abort(); }
    return true;
}

// Finds a solution (as a path with rotation sentinels) of at most max_len moves.
// With --niss the root also has an inverse-scramble candidate, and the returned
// path is the assembled N-word + inverse(I-word) (assemble_niss).
static bool solve(const State6& start, int max_len, std::vector<int>& out, std::string* niss_notation = nullptr) {
    g_path.clear();
    g_ctx = NissCtx();
    g_forced.last_accepted.clear();
    if (is_in_s7(start)) { out.clear(); return true; }
    g_root_state6 = start;
    std::vector<Root> roots;
    { Root r; r.s = start; r.h0 = heuristic(start, &r.tier); roots.push_back(r); }
    if (g_niss) {
        int rot;
        Root r; r.s = invert_state6(start, &rot);
        r.prefix.push_back(NISS_SWITCH_BASE);
        if (rot > 0) r.prefix.push_back(ROTATION_SENTINEL_BASE + rot);
        r.ctx.side = 1;
        r.h0 = heuristic(r.s, &r.tier);
        roots.push_back(r);
    }
    for (auto& r : roots) setup_root_forced(r);
    int h_min = roots[0].h0;
    for (auto& r : roots) h_min = std::min(h_min, r.h0);
    for (int threshold = h_min; threshold <= max_len; threshold++)
        for (size_t ri = 0; ri < roots.size(); ri++)
            if (try_root(roots[ri], ri, threshold, out, niss_notation)) return true;
    return false;
}
