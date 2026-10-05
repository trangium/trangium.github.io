// Randomized start: solve the NORMAL 4x4x4 (centers interchangeable within their face) with the supercube solver.
//
// Why it works. Let C = L S P be the cube the search would solve (L = forced suffix, P = forced prefix, S = the
// scramble; both may be empty). Solving the visible cube only needs S P Z L to be a within-face center
// permutation, so a Z that solves C "up to" a suitable permutation of centers is enough. Written on the inverse
// side the freedom sits in front of the suffix: the inverse scramble is  P^-1 S^-1 L^-1,  and
//     P^-1 S^-1 [centers permuted inside their faces] L^-1
// is solved by W exactly iff P W^-1 L solves S on the normal cube. Moving the permutation behind L^-1 it only
// permutes pieces inside the sets  U_i = L^-1(T_i)  (T_i = the 4 center positions of face i, mapped by the move
// word L^-1), so the start is X0 = P^-1 S^-1 L^-1 with arbitrary pieces swapped inside each U_i. Solving the
// state X0 f exactly with the normal solver (inverse root only, NISS may still switch later) gives
// the answer  P inverse(W) L  -- the search's solution Z is already inverse(W).
//
// Which f are legal. Every move has the same permutation sign on the centers as on the corners, so a single
// center transposition is NOT a reachable supercube state (checked with sympy: 3-cycles, double transpositions
// and one transposition in each of two faces are in the group, a lone transposition is not). Only states an even
// number of transpositions away from X0 are used as candidates; the walk still steps through the odd ones.
//
// The S1 distance (phase1.h) depends only on the three center masks, so candidates are generated cheaply as
// masks (one swap of two bits per step), deduplicated by that coset, and ranked by their exact distance to S1.
// Passes then run the usual IDA* from the best candidates with the endtable as the tier-0 heuristic:
// for d = 0, 1, 2, ...: every candidate whose S1 distance is <= d is searched at threshold  h0 + (d - D).
#pragma once
#include "search.h"
#include <unordered_set>

namespace rs {

static bool g_on = false;
struct Config {
    long long trials = 1000000;   // transposition steps of the center walk
    int max_d = 8;               // largest pass d before falling back to the ordinary search
};

static Config g_cfg;

struct Stats {
    long long steps = 0, odd_skipped = 0, dup_skipped = 0, runs = 0;
    int kept = 0, within[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // candidates with S1 distance <= k
    int best_dist = -1, win_dist = -1, win_d = -1;
    bool fallback = false;
    double gen_ms = 0, search_ms = 0;
};

static const int* FACE_SLOTS[6] = {coords::L_CENTER_SLOTS, coords::R_CENTER_SLOTS, coords::F_CENTER_SLOTS,
                                   coords::B_CENTER_SLOTS, coords::U_CENTER_SLOTS, coords::D_CENTER_SLOTS};

// The cube as a State6 without any canonicalization, and its inverse likewise.
static State6 raw_state(const Cube& c) {
    State6 s;
    for (int i = 0; i < 24; i++) { s.center_slot[i] = c.center[i]; s.wing_slot[i] = c.wing[i]; s.corner_sticker[i] = c.corner[i]; }
    s.corner_parity = c.corner_parity;
    refresh_derived(s);
    return s;
}
static State6 invert_raw(const State6& s) {
    State6 r = s;
    for (int i = 0; i < 24; i++) { r.center_slot[s.center_slot[i]] = i; r.wing_slot[s.wing_slot[i]] = i; r.corner_sticker[s.corner_sticker[i]] = i; }
    refresh_derived(r);
    return r;
}

// Solved as seen on a normal (non-super) 4x4x4: every center piece sits on a position of its own face's color
// and the rest of the cube is solved.
static bool is_visibly_solved(const Cube& c) {
    int face[24];
    for (int f = 0; f < 6; f++) for (int j = 0; j < 4; j++) face[FACE_SLOTS[f][j]] = f;
    for (int piece = 0; piece < 24; piece++) if (face[c.center[piece]] != face[piece]) return false;
    Cube n = c;
    for (int i = 0; i < 24; i++) n.center[i] = i;
    return is_in_s7(n.state());
}

// One start candidate: where every center piece sits, and its exact distance to S1.
struct Candidate { std::array<uint8_t, 24> pos; int dist; };

// Random walk over arrangements of the centers inside the six position sets (one transposition per step; the
// next transposition of a set shares one position with its previous one, which keeps a walk from undoing itself).
static void generate_candidates(const State6& x0, const int sets[6][4], long long steps, std::mt19937& rng,
                                std::vector<Candidate>& out, Stats& st) {
    int cls[24];   // 0 = UD, 1 = LR, 2 = FB center piece
    for (int i = 0; i < 8; i++) { cls[UD_TARGET[i]] = 0; cls[LR_TARGET[i]] = 1; cls[FB_TARGET[i]] = 2; }
    int at[24], pos[24];
    for (int piece = 0; piece < 24; piece++) { pos[piece] = x0.center_slot[piece]; at[pos[piece]] = piece; }
    uint32_t m[3] = {0, 0, 0};
    for (int piece = 0; piece < 24; piece++) m[cls[piece]] |= 1u << pos[piece];
    const int parity = x0.s1.parity;
    std::unordered_set<uint64_t> seen;

    auto consider = [&]() {
        const uint64_t key = p1::make_key(m[0], m[1], m[2], parity);
        if (!seen.insert(key).second) { st.dup_skipped++; return; }
        Candidate c;
        for (int piece = 0; piece < 24; piece++) c.pos[piece] = (uint8_t)pos[piece];
        c.dist = p1::dist_of_key(key);
        out.push_back(c);
    };
    auto random_pair = [&]() {
        int a = (int)(rng() % 4), b = (int)(rng() % 3);
        if (b >= a) b++;
        return std::make_pair(std::min(a, b), std::max(a, b));
    };
    std::pair<int, int> tr[6];
    for (int f = 0; f < 6; f++) tr[f] = random_pair();

    consider();   // X0 itself (no transposition)
    long long applied = 0;
    while (applied < steps) {
        int order[6] = {0, 1, 2, 3, 4, 5};
        std::shuffle(order, order + 6, rng);
        const int count = 4 + (int)(rng() % 3);
        for (int k = 0; k < count && applied < steps; k++) {
            const int f = order[k];
            const int a = sets[f][tr[f].first], b = sets[f][tr[f].second];
            const int pa = at[a], pb = at[b];
            at[a] = pb; at[b] = pa; pos[pa] = b; pos[pb] = a;
            if (cls[pa] != cls[pb]) {
                const uint32_t both = (1u << a) | (1u << b);
                m[cls[pa]] ^= both; m[cls[pb]] ^= both;
            }
            applied++;
            if (applied % 2 == 0) consider(); else st.odd_skipped++;
            // next transposition of this set: keep one of the two positions, pair it with one of the two others
            const int keep = (rng() & 1) ? tr[f].first : tr[f].second;
            int others[2], n = 0;
            for (int i = 0; i < 4; i++) if (i != tr[f].first && i != tr[f].second) others[n++] = i;
            const int other = others[rng() & 1];
            tr[f] = std::make_pair(std::min(keep, other), std::max(keep, other));
        }
    }
    st.steps = applied;
}

struct RootEntry { bool built = false; Root root; State6 n0; };

// Solves the normal cube whose scrambled-and-wrapped supercube is `c` (= L S P). On success `z` is the searched
// part; the full solution is  P z L  (assemble_forced in main.cpp), with no root rotation (root_rot_out = -1),
// unless the ordinary search had to be used (st.fallback), which may report one.
static bool solve_random_start(const Cube& c, const Config& cfg, std::mt19937& rng, int max_len, std::vector<int>& z,
                               std::string* notation, int& root_rot_out, Stats& st) {
    root_rot_out = -1;
    g_forced.root_rot = -1;
    auto t0 = std::chrono::steady_clock::now();

    const State6 x0 = invert_raw(raw_state(c));   // P^-1 S^-1 L^-1
    Cube lc;
    apply_letters(lc, inverse_alg(g_forced.suffix));
    int sets[6][4];
    {
        bool used[24] = {false};
        for (int f = 0; f < 6; f++)
            for (int j = 0; j < 4; j++) {
                sets[f][j] = lc.center[FACE_SLOTS[f][j]];
                if (used[sets[f][j]]) { printf("BUG: the position sets of the suffix images overlap\n"); abort(); }
                used[sets[f][j]] = true;
            }
    }
    std::vector<Candidate> cands;
    generate_candidates(x0, sets, cfg.trials, rng, cands, st);
    st.kept = (int)cands.size();
    std::vector<int> order;
    for (int i = 0; i < (int)cands.size(); i++) {
        const int d = cands[i].dist;
        for (int k = std::max(d, 0); k < 8; k++) st.within[k]++;
        if (d <= p1::g_depth) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cands[a].dist < cands[b].dist; });
    st.best_dist = cands.empty() ? -1 : cands[order.empty() ? 0 : order[0]].dist;
    auto t1 = std::chrono::steady_clock::now();
    st.gen_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // only the candidates that are actually tried get a root (a RootEntry is ~1.3 KB: for every candidate coset, several hundred
    // thousand of them, that was ~400 MB)
    std::unordered_map<int, RootEntry> roots;
    auto build = [&](int i) {
        RootEntry& e = roots[i];
        State6 cand0 = x0;
        for (int piece = 0; piece < 24; piece++) cand0.center_slot[piece] = cands[i].pos[piece];
        refresh_derived(cand0);
        e.n0 = invert_raw(cand0);   // the (virtual) normal-side scramble whose inverse root is the candidate
        int rot;
        e.root.s = invert_state6(e.n0, &rot);
        e.root.prefix.push_back(NISS_SWITCH_BASE);
        if (rot > 0) e.root.prefix.push_back(ROTATION_SENTINEL_BASE + rot);
        e.root.ctx = NissCtx();
        e.root.ctx.side = 1;
        e.root.h0 = heuristic(e.root.s, &e.root.tier);
        setup_root_forced(e.root);
        e.built = true;
    };

    bool found = false;
    for (int d = 0; d <= cfg.max_d && !found; d++) {
        for (int i : order) {
            if (cands[i].dist > d) break;
            if (!roots[i].built) build(i);
            RootEntry& e = roots[i];
            g_root_state6 = e.n0;
            g_forced.last_accepted.clear();
            st.runs++;
            if (try_root(e.root, 1, e.root.h0 + (d - cands[i].dist), z, notation)) {
                st.win_dist = cands[i].dist; st.win_d = d;
                found = true;
                break;
            }
        }
    }
    st.search_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    if (found) return true;

    // nothing at depth <= max_d: the ordinary (exact supercube) search is still a valid solution of the normal cube
    st.fallback = true;
    int rr;
    State6 start = c.state(&rr);
    g_forced.root_rot = rr;
    root_rot_out = rr;
    return solve(start, max_len, z, notation);
}

} // namespace rs
