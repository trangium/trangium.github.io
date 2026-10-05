// Production layer of the browser build (see web_main.cpp for the WASM exports, web_test.cpp for the native tests).
//
// Two modes (the checkbox "Supercube-safe?"):
//   supercube-safe   S5 -> LS ending, exact supercube search, S1 endtable depth 7:
//                    --cost12 7 --cost23p 7 --cost34p 11 --cost45 8 --ls --cost-ls 9 --ls-depth 9
//                    (NISS always on, penalty threshold / rate at the solver's defaults 7 / 1)
//   not safe         --cost12 8 --cost23p 6 --cost34p 10 --cost45 9 --ls --cost-ls 9 --ls-depth 9 --niss --random-start --rs-max-d 10
//                    --s1-depth 7 --penalty-threshold 8 --penalty-rate 1: solves the NORMAL 4x4x4 (centers may end up permuted inside their faces)
//
// Two sources of the position (the toggle): a given scramble, or a uniformly random state (see random_cube).
// For a random state the search solves it with the user's suffix inverted as the forced PREFIX and the user's
// prefix inverted as the forced SUFFIX, and the printed scramble is the inverse of that solution, so it starts with
// the user's prefix and ends with the user's suffix.
// The printed moves are rotation free (see to_rotation_free): whole-cube rotations are translated out and no wide
// move of the B / L / D side is printed (Bw / Lw / Dw become Fw / Rw / Uw plus a rotation that is translated out).
#pragma once
#include "random_start.h"

namespace web {

// progress hook (step 1..total, what is being built); nullptr = silent
typedef void (*ProgressFn)(int step, int total, const char* what);
static ProgressFn g_progress = nullptr;
static std::string g_cache_dir = ".";

// ------------------------------------------------------------------------------------------ tables
static bool g_built = false;
static bool g_supercube_safe = true;

static void set_flags(bool supercube_safe) {
    g_cost_ls = 9;
    ls::g_force_depth = 9;
    g_niss = true;
    coords::PENALTY_RATE = 1;
    if (supercube_safe) {
        g_cost12 = 7; g_cost23p = 7; g_cost34p = 11; g_cost45 = 8;
        coords::PENALTY_THRESHOLD = 7;   // the solver's default (the command line has no --penalty-threshold)
    } else {
        g_cost12 = 8; g_cost23p = 6; g_cost34p = 10; g_cost45 = 9;
        coords::PENALTY_THRESHOLD = 8;
        rs::g_cfg.max_d = 10;
    }
}

// Both modes' tables are built once, with the same depth 7 S1 endtable (for the supercube mode a deeper table than the depth 6 its costs
// were tuned with: same average length, ~40% fewer nodes). A mode switch sets the costs and rebuilds the 2520-entry penalty tables.
static void build_tables(bool supercube_safe) {
    set_flags(supercube_safe);
    const int want_depth = 7;   // both modes use the depth 7 S1 endtable
    if (g_built) {
        coords::build_penalty_tables();
        p1::g_depth = want_depth;
        g_supercube_safe = supercube_safe;
        rs::g_on = !supercube_safe;
        return;
    }
    const int TOTAL = 9;
    int step = 0;
    auto stage = [&](const char* what) { step++; if (g_progress) g_progress(step, TOTAL, what); };

    init_tier_moves();
    for (int i = 0; i < NUM_FB_PERMS; i++) p2full::FB_IS_SOLVED[i] = false;
    for (int i = 0; i < 96; i++) p2full::FB_IS_SOLVED[FB_SOLVED_INDICES[i]] = true;

    stage("First-phase table");
    p1::g_depth = 7;   // built to depth 7 for both modes
    p1::build();
    p1::g_depth = want_depth;

    stage("Wing and center tables");
    p2full::build_wing_distance_table(g_wing);
    p2full::build_fb_distance_table(g_fb);
    coords::init_move_flags();
    coords::init_center_locals();
    coords::build_center2520_ud();
    coords::build_center2520_lr();
    coords::build_fb24();
    coords::build_reduced_transitions();
    coords::build_sigma_p2();
    coords::build_ud2520_dist();
    coords::build_lr2520_dist();
    coords::build_wing_good();
    coords::build_center_good();
    coords::init_layer_equatorial_split();
    coords::init_layer_value_local();

    stage("Edge-pairing tables");
    coords::build_s4_coords();
    init_s4_goals();
    coords::build_rest_coord();
    s4sym::build(g_cache_dir);

    stage("Corner tables");
    coords::init_corner_pos_twist();
    coords::build_equator_rank_tables();
    coords::build_equator_trans();
    coords::build_s5_dist();
    coords::build_full_to_tier4_index();
    coords::build_corner_ori_trans();
    coords::build_center_sum_delta();
    coords::build_eq_parity_trans();

    g_legacy_tail = false;   // the leave-slice ending needs none of the S6 / S7 tables
    stage("Transition tables");
    init_lazy_tables();
    init_niss_tables();

    stage("Leave-slice table");
    ls::build();

    stage("Second-phase table");
    p2j::build();

    stage("Switch tables");
    s2sw::build();

    enable_ls();
    g_built = true;
    g_supercube_safe = supercube_safe;
    rs::g_on = !supercube_safe;
}

// ------------------------------------------------------------------------------------------ notation
// 0 U, 1 D, 2 R, 3 L, 4 F, 5 B
static const char FACE_CH[6] = {'U', 'D', 'R', 'L', 'F', 'B'};
static int face_index(char c) { for (int f = 0; f < 6; f++) if (FACE_CH[f] == c) return f; return -1; }
// the family of each move (MOVE_NAMES order: U Uw D R Rw L F Fw B, three powers each)
struct MoveDesc { int face; bool wide; int k; };   // k = quarter turns 1..3
static MoveDesc move_desc(int m) {
    static const int FACE_OF_FAMILY[9] = {0, 0, 1, 2, 2, 3, 4, 4, 5};
    static const bool WIDE_OF_FAMILY[9] = {false, true, false, false, true, false, false, true, false};
    const int fam = m / 3, p = m % 3;
    return {FACE_OF_FAMILY[fam], WIDE_OF_FAMILY[fam], p == 0 ? 1 : (p == 1 ? 2 : 3)};
}
static std::string power_suffix(int k) { return k == 1 ? "" : (k == 2 ? "2" : "'"); }

// Free-form scramble text -> text the solver's parse_alg understands (moves of the 27 + x y z rotations).
// Accepts U D R L F B, wide moves as Rw or r, rotations x y z, the suffixes ' 2 2' and typographic primes.
// Lw / Dw / Bw (not among the 27) are rewritten as Rw x', Uw y', Fw z' (and their powers): the left two layers turn
// like R' does, i.e. the whole cube turns like x' and the right two layers are turned back. Returns "" or an error.
static std::string normalize_alg(const std::string& in, std::string& out) {
    std::string t;
    for (size_t i = 0; i < in.size(); i++) {
        const unsigned char c = (unsigned char)in[i];
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80 &&
            ((unsigned char)in[i + 2] == 0x99 || (unsigned char)in[i + 2] == 0x98 || (unsigned char)in[i + 2] == 0xB2)) { t += '\''; i += 2; }
        else if (c == '(' || c == ')' || c == '[' || c == ']' || c == ',' || c == ';' || c == '\n' || c == '\r' || c == '\t' || c == 0xC2) t += ' ';
        else t += (char)c;
    }
    out.clear();
    std::istringstream iss(t);
    std::string tok;
    while (iss >> tok) {
        char c = tok[0];
        size_t i = 1;
        bool rot = false, wide = false;
        int face = -1;
        if (std::string("UDRLFB").find(c) != std::string::npos) {
            face = face_index(c);
            if (i < tok.size() && tok[i] == 'w') { wide = true; i++; }
        } else if (std::string("udrlfb").find(c) != std::string::npos) { face = face_index((char)toupper(c)); wide = true; }
        else if (c == 'x' || c == 'y' || c == 'z') rot = true;
        else return tok;
        const std::string rest = tok.substr(i);
        int k;
        if (rest.empty()) k = 1;
        else if (rest == "'") k = 3;
        else if (rest == "2" || rest == "2'" || rest == "'2") k = 2;
        else return tok;
        if (!out.empty()) out += " ";
        if (rot) out += std::string(1, c) + power_suffix(k);
        else if (!wide) out += std::string(1, FACE_CH[face]) + power_suffix(k);
        else if (face == 0 || face == 2 || face == 4) out += std::string(1, FACE_CH[face]) + "w" + power_suffix(k);
        else {
            // Dw = Uw y', Lw = Rw x', Bw = Fw z'
            const int opp = face == 1 ? 0 : (face == 3 ? 2 : 4);
            const char axis = face == 1 ? 'y' : (face == 3 ? 'x' : 'z');
            out += std::string(1, FACE_CH[opp]) + "w" + power_suffix(k) + " " + axis + power_suffix(4 - k);
        }
    }
    return "";
}

// ------------------------------------------------------------------------------------------ cube algebra
// "a then e": the slot arrays of e are applied after those of a.
static Cube cube_then(const Cube& a, const Cube& e) {
    Cube r;
    for (int i = 0; i < 24; i++) { r.center[i] = e.center[a.center[i]]; r.wing[i] = e.wing[a.wing[i]]; r.corner[i] = e.corner[a.corner[i]]; }
    r.corner_parity = a.corner_parity ^ e.corner_parity;
    return r;
}
static Cube cube_inverse(const Cube& c) {
    Cube r;
    for (int i = 0; i < 24; i++) { r.center[c.center[i]] = i; r.wing[c.wing[i]] = i; r.corner[c.corner[i]] = i; }
    r.corner_parity = c.corner_parity;
    return r;
}
static bool same_cube(const Cube& a, const Cube& b) {
    for (int i = 0; i < 24; i++) if (a.center[i] != b.center[i] || a.wing[i] != b.wing[i] || a.corner[i] != b.corner[i]) return false;
    return true;
}

// The 24 whole-cube rotations as cubes (generated by x, y, z).
static const std::vector<Cube>& rotations24() {
    static std::vector<Cube> rots;
    if (!rots.empty()) return rots;
    const int gen[3] = {P2_OPEN_SENTINEL_BASE + 1, P2_OPEN_SENTINEL_BASE + 3, P2_OPEN_SENTINEL_BASE + 2};   // x, y, z
    rots.push_back(Cube());
    for (size_t i = 0; i < rots.size(); i++)
        for (int g = 0; g < 3; g++) {
            Cube c = rots[i];
            apply_letters(c, {gen[g]});
            bool have = false;
            for (const Cube& r : rots) if (same_cube(r, c)) { have = true; break; }
            if (!have) rots.push_back(c);
        }
    return rots;
}

// t is s turned as a whole (T = S then a whole-cube rotation); with `normal` the centers of a face may also be permuted
// inside their face (the two cubes look the same). The solved cube is s = Cube().
static bool equal_mod_rotation(const Cube& t, const Cube& s, bool normal) {
    const Cube sinv = cube_inverse(s);
    for (const Cube& r : rotations24()) {
        const Cube v = cube_then(cube_then(t, cube_inverse(r)), sinv);
        if (normal ? rs::is_visibly_solved(v) : same_cube(v, Cube())) return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------ random state
static int perm_parity(const int* p, int n) {
    bool seen[24] = {false};
    int cycles = 0;
    for (int i = 0; i < n; i++) {
        if (seen[i]) continue;
        cycles++;
        for (int j = i; !seen[j]; j = p[j]) seen[j] = true;
    }
    return (n - cycles) & 1;
}
static void shuffle_ints(int* a, int n, std::mt19937& rng) {   // Fisher-Yates
    for (int i = n - 1; i > 0; i--) std::swap(a[i], a[std::uniform_int_distribution<int>(0, i)(rng)]);
}

// A uniformly random element of the group the solver works in (all 8! corner permutations with any twists summing to 0 mod 3,
// all 24! wing permutations, all 24! center permutations whose sign equals the sign of the corner permutation: the group has
// order 8! 3^7 24! 24!/2, checked with sympy). Wings and centers are distinct pieces (centers have no orientation),
// twin wings are not interchangeable here; the visible state of the normal cube is then uniform too.
static Cube random_cube(std::mt19937& rng) {
    Cube c;
    // corners: piece k goes to position cp[k] with signed twist ori[position] (sum 0 mod 3), as in s5.h
    int cp[8];
    for (int k = 0; k < 8; k++) cp[k] = k;
    shuffle_ints(cp, 8, rng);
    int ori[8], sum = 0;
    for (int d = 0; d < 7; d++) { ori[d] = std::uniform_int_distribution<int>(0, 2)(rng); sum += ori[d]; }
    ori[7] = (3 - sum % 3) % 3;
    for (int k = 0; k < 8; k++) {
        const int d = cp[k];
        const int raw = ((CORNER_POS_PARITY_SIGN[d] * ori[d]) % 3 + 3) % 3;
        const int y = (raw + 1) % 3;                                  // slot axis taken by the piece's sticker 1
        const int delta = CORNER_POS_PARITY_SIGN[k] * CORNER_POS_PARITY_SIGN[d];   // the cyclic order of the three stickers
        for (int a = 0; a < 3; a++) c.corner[k * 3 + a] = d * 3 + (((y + delta * (a - 1)) % 3) + 3) % 3;
    }
    c.corner_parity = perm_parity(cp, 8);
    for (int i = 0; i < 24; i++) c.wing[i] = i;
    shuffle_ints(c.wing, 24, rng);
    for (int i = 0; i < 24; i++) c.center[i] = i;
    shuffle_ints(c.center, 24, rng);
    if (perm_parity(c.center, 24) != c.corner_parity) std::swap(c.center[0], c.center[1]);
    return c;
}

// ------------------------------------------------------------------------------------------ rotation-free output
static void rotation_map(int axis, int to[6]) {
    // to[f] = the face the layers of face f move to under one quarter turn of the whole cube about the axis
    // (x: F->U->B->D->F, y: F->L->B->R->F, z: U->R->D->L->U)
    for (int f = 0; f < 6; f++) to[f] = f;
    auto cyc = [&](std::initializer_list<char> fs) {
        std::vector<int> v;
        for (char ch : fs) v.push_back(face_index(ch));
        for (size_t i = 0; i < v.size(); i++) to[v[i]] = v[(i + 1) % v.size()];
    };
    if (axis == 0) cyc({'F', 'U', 'B', 'D'});
    else if (axis == 1) cyc({'F', 'L', 'B', 'R'});
    else cyc({'U', 'R', 'D', 'L'});
}

// Moves and rotations of a path -> moves only, as text. rho[f] = the face of the output cube (never rotated) that is
// the face f of the cube the path is applied to. A rotation of that cube re-labels rho; a wide move of the B / L / D side
// is replaced by the opposite wide move, which leaves the output cube turned by a whole-cube rotation relative to the
// wanted one, and that is absorbed into rho as well. The result takes the solved cube to the same state up to a whole-cube rotation.
static std::string to_rotation_free(const std::vector<int>& path, int* nmoves = nullptr) {
    int rho[6];
    for (int f = 0; f < 6; f++) rho[f] = f;
    std::string out;
    int count = 0;
    auto emit = [&](int face, bool wide, int k) {
        if (!out.empty()) out += " ";
        out += std::string(1, FACE_CH[face]) + (wide ? "w" : "") + power_suffix(k);
        count++;
    };
    auto turn_rho = [&](int axis, int quarter_turns) {   // rho := (that rotation) o rho
        int to[6];
        rotation_map(axis, to);
        for (int q = 0; q < quarter_turns; q++) for (int f = 0; f < 6; f++) rho[f] = to[rho[f]];
    };
    auto rotate_input = [&](int axis, int quarter_turns) {   // the path's cube is rotated: the layers at face f now were at to^-1(f)
        int to[6];
        rotation_map(axis, to);
        for (int q = 0; q < quarter_turns; q++) {
            int nr[6];
            for (int f = 0; f < 6; f++) nr[to[f]] = rho[f];
            for (int f = 0; f < 6; f++) rho[f] = nr[f];
        }
    };
    for (int e : path) {
        if (e < NUM_MOVES) {
            const MoveDesc d = move_desc(e);
            const int face = rho[d.face];
            if (!d.wide || face == 0 || face == 2 || face == 4) { emit(face, d.wide, d.k); continue; }
            // Dw / Lw / Bw -> Uw / Rw / Fw, the output cube ends up turned by that axis' rotation, k times
            const int opp = face == 1 ? 0 : (face == 3 ? 2 : 4);
            const int axis = face == 1 ? 1 : (face == 3 ? 0 : 2);
            emit(opp, true, d.k);
            turn_rho(axis, d.k);
        } else if (e < NISS_FLIP_TOKEN) {
            const std::string rots = is_open_rotation_entry(e) ? ROTATE_CLASS_FWD_NOTATION[rotation_class_of(e)] : ROTATE_CLASS_INV_NOTATION[rotation_class_of(e)];
            std::istringstream is(rots);
            std::string tk;
            while (is >> tk) {
                const int axis = tk[0] == 'x' ? 0 : (tk[0] == 'y' ? 1 : 2);
                const int k = tk.size() == 1 ? 1 : (tk[1] == '2' ? 2 : 3);
                rotate_input(axis, k);
            }
        }
    }
    if (nmoves) *nmoves = count;
    return out;
}

// ------------------------------------------------------------------------------------------ solving
struct Result {
    bool ok = false;
    std::string error;
    std::string text;          // the scramble (random state) or the solution (given position), rotation free
    int moves = 0;
    long long nodes = 0;
    double ms = 0;
    bool random_state = false;
};

static Cube apply_text(const Cube& start, const std::vector<int>& path) {
    Cube c = start;
    apply_letters(c, path);
    return c;
}

// scramble_text empty or random_state: a uniformly random state; otherwise the position after the given scramble.
static Result run(const std::string& scramble_text, const std::string& prefix_text, const std::string& suffix_text,
                  bool supercube_safe, bool random_state, uint64_t seed) {
    Result res;
    res.random_state = random_state;
    if (!g_built) { res.error = "tables are not built"; return res; }
    std::mt19937 rng((uint32_t)(seed ^ (seed >> 32)));
    std::mt19937 rng_rs((uint32_t)((seed >> 7) ^ 0x9E3779B9u));

    std::vector<int> pre, suf, scr;
    auto read_alg = [&](const std::string& text, const char* what, std::vector<int>& path) {
        std::string norm, bad = normalize_alg(text, norm);
        if (bad.empty()) bad = parse_alg(norm, path);
        if (!bad.empty()) res.error = std::string("Unknown move in the ") + what + ": " + bad;
        return bad.empty();
    };
    if (!read_alg(prefix_text, "forced prefix", pre) || !read_alg(suffix_text, "forced suffix", suf)) return res;

    Cube target;   // the state that is solved (a random one, or the given scramble applied to the solved cube)
    if (random_state) target = random_cube(rng);
    else {
        if (!read_alg(scramble_text, "scramble", scr)) return res;
        apply_letters(target, scr);
    }
    // forced ends of the search: for a random state the scramble is the inverse of the solution
    g_forced = Forced();
    g_forced.prefix = random_state ? inverse_alg(suf) : pre;
    g_forced.suffix = random_state ? inverse_alg(pre) : suf;

    // the cube the search solves: L S P
    Cube c;
    apply_letters(c, g_forced.suffix);
    c = cube_then(c, target);
    apply_letters(c, g_forced.prefix);

    rs::g_on = !supercube_safe;
    g_nodes = 0;
    std::vector<int> z;
    std::string niss;
    int root_rot = -1;
    const int max_len = 100;
    auto t0 = std::chrono::steady_clock::now();
    bool found;
    if (!supercube_safe) {
        rs::Stats st;
        found = rs::solve_random_start(c, rs::g_cfg, rng_rs, max_len, z, &niss, root_rot, st);
    } else {
        State6 start = c.state(&root_rot);
        g_forced.root_rot = root_rot;
        found = solve(start, max_len, z, &niss);
    }
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    res.nodes = g_nodes;
    if (!found) { res.error = "No solution found within " + std::to_string(max_len) + " moves."; return res; }

    std::vector<int> sol = g_forced.prefix;   // P (+ the root rotation) Z L
    if (root_rot > 0) sol.push_back(ROTATION_SENTINEL_BASE + root_rot);
    sol.insert(sol.end(), z.begin(), z.end());
    sol.insert(sol.end(), g_forced.suffix.begin(), g_forced.suffix.end());

    // check: the position followed by the solution is solved (up to a whole-cube rotation; for the normal cube also up to
    // the order of centers inside a face)
    // (the search reaches the literal solved cube, rotations closed: a scramble that is the inverse of it is then exact)
    {
        const Cube end = apply_text(target, sol);
        if (!(supercube_safe ? same_cube(end, Cube()) : rs::is_visibly_solved(end))) { res.error = "internal error: the solution does not solve the position"; return res; }
    }

    const std::vector<int> shown = random_state ? inverse_alg(sol) : sol;
    res.text = to_rotation_free(shown, &res.moves);
    // check the printed moves too: a scramble must give the random state (from the solved cube), a solution must solve the position
    {
        std::string n2;
        std::vector<int> back;
        normalize_alg(res.text, n2);
        parse_alg(n2, back);
        const bool good = random_state ? equal_mod_rotation(apply_text(Cube(), back), target, !supercube_safe)
                                       : equal_mod_rotation(apply_text(target, back), Cube(), !supercube_safe);
        if (!good) { res.error = "internal error: the printed moves do not match the solution"; return res; }
    }
    res.ok = true;
    return res;
}

} // namespace web
