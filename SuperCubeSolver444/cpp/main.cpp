// S0 -> solved 4x4x4 supercube solver (single translation unit).
//
// Module map (each header includes the one before it):
//   common.h      includes + generated data headers
//   phase1.h      S0->S1  the three center bitmasks + wing parity, exact distance endtable (Bloom filters)
//   phase2.h      S1->S2  wing coset + F/B permutation
//   coords.h      center 2520-classes, fb24, reduced transitions, penalties
//   s3p.h         S3' sets, distance-to-S3' tables, wing layer/equatorial split
//   s4_coords.h   S3'->S4 coordinates + generic two-factor distance tables
//   s4_tables.h   S3'->S4 heuristic tables (the variant-specific part)
//   s5.h s6.h s7.h  phase 5/6/7 tables;  lazy67.h  ID-packed composites for 6+7
//   state.h       State6, is_in_s* predicates, per-tier lazy states, heuristic
//   raw.h         path sentinels, raw-cube replay, conjugation
//   niss.h        NISS state algebra (inversion, left multiplication), solution assembly
//   ls.h          --ls: S5 -> LS (leave slice) -> solved: UD-part tier, Bloom endtable, stack search that replaces the word
//   forced.h      forced prefix/suffix: algorithm parsing, exact cancellation check
//   random_start.h  randomized start: solve the normal (non-super) 4x4x4 by trying many center permutations
//   search.h      tiered IDA* with drop credit and rotation forks (and NISS switching)
//   selftest.h    startup self-tests
//
// Build: g++ -O3 -std=c++17 -o solver main.cpp
// Run from a folder holding s3prime_wing_dist.bin (built on first run, ~10 min).
#include "selftest.h"

// ------------------------------------------------------------------ config
struct Config {
    int max_len = 100;
    int scramble_min = 100, scramble_max = 120;
    int profile_trials = 0;
    unsigned seed = 2024;
    bool skip_selftest = false;
    bool print_solutions = false;
    bool ls = false;   // --ls: S5 -> LS -> solved
    std::string cache_dir = ".";
};

static bool g_check_s4_sym = false;

static double ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// P + (the root state's rotation) + Z + L: the finished solution of the original scramble.
static std::vector<int> assemble_forced(const std::vector<int>& z, int root_rot) {
    std::vector<int> full = g_forced.prefix;
    if (root_rot > 0) full.push_back(ROTATION_SENTINEL_BASE + root_rot);
    full.insert(full.end(), z.begin(), z.end());
    full.insert(full.end(), g_forced.suffix.begin(), g_forced.suffix.end());
    return full;
}

static void usage() {
    printf("options:\n"
           "  --cost12 N --cost23p N --cost34p N --cost45 N --cost56 N --cost67 N   flat tier gaps (defaults 9 7 10 9 9 4)\n"
           "  --penalty-threshold N --penalty-rate X   early U/D, L/R center-distance penalty (defaults 7, 1.0)\n"
           "  --max-len N                  give up above this many moves (default 100; raise it with large COSTs)\n"
           "  --profile N                  solve N random scrambles and print summary stats, then exit\n"
           "  --tier-stats                 with --profile: per-trial nodes spent in / entries into each tier\n"
           "  (NISS switching is always on: S3' is only reached through a switch; the plain search is in archive/cpp/s3_route_normal_and_both)\n"
           "  --check-replay               debug: verify every NISS branch against a replay of its path (slow)\n"
           "  --print-solutions            with --profile: print each trial's scramble and solution (with NISS shorthand)\n"
           "  --random-start               solve the NORMAL 4x4x4 (centers interchangeable within a face): try many within-face\n"
           "                               center permutations of the inverse scramble, ranked by their exact distance to S1\n"
           "  --rs-trials N --rs-max-d N   random start: transposition steps of the center walk (1000000); largest pass d (8)\n"
           "  --s1-depth N --s1-fpr X      exact distances to S1 are known up to N <= 7 (default 6); Bloom false positive rate (0.001)\n"
           "  --p2-depth N --p2-fpr X      phase 2 joint (wing, F/B) endtable: exact distances to S2 up to N <= 6 (default 5, 0 = off); false positive rate (0.001)\n"
           "  --p2-tables LIST             phase 2 distance tables used next to the endtable: wing,fb (default), wing, fb or none (to measure the endtable alone)\n"
           "  --prefix \"R U F\"            force the solution to start with these moves (x/y/z rotations allowed too)\n"
           "  --suffix \"R2 U'\"            force the solution to end with these moves\n"
           "  --scramble-min N --scramble-max N   random scramble length range (default 100 120)\n"
           "  --seed N                     RNG seed for --profile (default 2024)\n"
           "  --cache-dir DIR              where the cached distance tables live (default .)\n"
           "  --check-s4-sym               compare every entry of the symmetry-reduced S3'->S4 tables with the full BFS tables, then exit\n"
           "  --ls                         end with S5 -> LS (leave slice) -> solved instead of S5 -> S6 -> S7 (ls.h)\n"
           "  --cost-ls N                  with --ls: flat cost of S5 -> LS -> solved stacked on tier 4 (default 11; replaces COST56 + COST67)\n"
           "  --ls-niss-all                with --ls: also allow a NISS switch on the move that enters S5 (default with --ls: no switch there, earlier boundaries keep theirs)\n"
           "  --ls-entries N               with --ls: LS Bloom endtable size: the exact depth whose total is closest to N entries (default 1000000 = depth 8)\n"
           "  --ls-lookup M                with --ls: endtable lookup: bloom-budget (default: one probe, at the budget left), fingerprint, bloom-linear, bloom-binary\n"
           "  --ls-depth N                 with --ls: exact LS endtable distances up to depth N instead (9 = 2.1M entries)\n"
           "  --no-s2-fast                 reference S2 wing index and no budget shortcut in the S2 heuristic (slower; for A/B timing)\n"
           "  --startup-times              print the time of every table-building step at start-up\n"
           "  --no-checkpoints             rebuild the full cube at every tier crossing by replaying the whole path (slower; for A/B timing)\n"
           "  --skip-selftest              skip the startup self-tests\n"
           "without --profile, reads scrambles from stdin (moves, plus whole-cube x/y/z) and prints solutions.\n");
}

static bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        const char* v;
        if (a == "--tier-stats") g_tier_stats = true;
        else if (a == "--random-start") rs::g_on = true;
        else if (a == "--niss") g_niss = true;   // the default; kept so old command lines still work
        else if (a == "--no-niss") { printf("--no-niss is gone: S3' is reached through a switch (see archive/cpp/s3_route_normal_and_both for the plain search)\n"); return false; }
        else if (a == "--print-solutions") cfg.print_solutions = true;
        else if (a == "--check-replay") g_check_replay = true;
        else if (a == "--check-s4-sym") g_check_s4_sym = true;
        else if (a == "--skip-selftest") cfg.skip_selftest = true;
        else if (a == "--ls") cfg.ls = true;
        else if (a == "--startup-times") g_startup_times = true;
        else if (a == "--no-checkpoints") g_use_ck = false;
        else if (a == "--no-s2-fast") s2sw::g_fast = false;
        else if (a == "--ls-niss-all") g_ls_no_s5_switch = false;
        else if (a == "--help" || a == "-h") { usage(); return false; }
        else if (!(v = next())) { printf("missing value for %s\n", a.c_str()); return false; }
        else if (a == "--prefix" || a == "--suffix") {
            std::vector<int> alg;
            std::string bad = parse_alg(v, alg);
            if (!bad.empty()) { printf("%s: unknown move '%s'\n", a.c_str(), bad.c_str()); return false; }
            (a == "--prefix" ? g_forced.prefix : g_forced.suffix) = alg;
        }
        else if (a == "--rs-trials") rs::g_cfg.trials = atoll(v);
        else if (a == "--rs-max-d") rs::g_cfg.max_d = atoi(v);
        else if (a == "--s1-depth") p1::g_depth = atoi(v);
        else if (a == "--s1-fpr") p1::g_fpr = atof(v);
        else if (a == "--p2-depth") p2j::g_depth = atoi(v);
        else if (a == "--p2-fpr") p2j::g_fpr = atof(v);
        else if (a == "--p2-tables") {
            const std::string list = v;
            g_p2_use_wing = list.find("wing") != std::string::npos;
            g_p2_use_fb = list.find("fb") != std::string::npos;
        }
        else if (a == "--cost12") g_cost12 = atoi(v);
        else if (a == "--cost23p") g_cost23p = atoi(v);
        else if (a == "--cost34p") g_cost34p = atoi(v);
        else if (a == "--cost45") g_cost45 = atoi(v);
        else if (a == "--cost56") g_cost56 = atoi(v);
        else if (a == "--cost67") g_cost67 = atoi(v);
        else if (a == "--cost-ls") g_cost_ls = atoi(v);
        else if (a == "--ls-entries") ls::g_entries = atoi(v);
        else if (a == "--ls-depth") ls::g_force_depth = atoi(v);
        else if (a == "--ls-lookup") {
            const std::string m = v;
            if (m == "bloom-budget") ls::g_lookup = 3; else if (m == "bloom-binary") ls::g_lookup = 0; else if (m == "bloom-linear") ls::g_lookup = 1; else if (m == "fingerprint") ls::g_lookup = 2;
            else { printf("--ls-lookup: bloom-budget, bloom-binary, bloom-linear or fingerprint\n"); return false; }
        }
        else if (a == "--penalty-threshold") coords::PENALTY_THRESHOLD = atoi(v);
        else if (a == "--penalty-rate") {
            const double r = atof(v);
            if (r != std::floor(r) || r < 0 || r > 20) { printf("--penalty-rate must be a whole number (1 is the tuned value): the penalty is integer arithmetic now\n"); return false; }
            coords::PENALTY_RATE = (int)r;
        }
        else if (a == "--max-len") cfg.max_len = atoi(v);
        else if (a == "--profile") cfg.profile_trials = atoi(v);
        else if (a == "--scramble-min") cfg.scramble_min = atoi(v);
        else if (a == "--scramble-max") cfg.scramble_max = atoi(v);
        else if (a == "--seed") cfg.seed = (unsigned)atoll(v);
        else if (a == "--cache-dir") cfg.cache_dir = v;
        else { printf("unknown option %s\n", a.c_str()); usage(); return false; }
    }
    return true;
}

// ------------------------------------------------------------------ table setup
#define TIMED(name, expr) do { auto _t0 = std::chrono::steady_clock::now(); expr; if (g_startup_times) { double _ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _t0).count(); printf("    [startup] %-58s %8.1f ms\n", name, _ms); } } while (0)

static void build_all_tables(const Config& cfg) {
    auto t0 = std::chrono::steady_clock::now();
    init_tier_moves();
    for (int i = 0; i < NUM_FB_PERMS; i++) p2full::FB_IS_SOLVED[i] = false;
    for (int i = 0; i < 96; i++) p2full::FB_IS_SOLVED[FB_SOLVED_INDICES[i]] = true;

    TIMED("p1::build()", p1::build());
    TIMED("p2full::build_wing_distance_table(g_wing)", p2full::build_wing_distance_table(g_wing));
    TIMED("p2full::build_fb_distance_table(g_fb)", p2full::build_fb_distance_table(g_fb));
    auto t1 = std::chrono::steady_clock::now();
    printf("phase 1/2 tables: %.0f ms\n", ms_between(t0, t1));
    TIMED("p2full::print_wing_dist_stats(g_wing)", p2full::print_wing_dist_stats(g_wing));

    TIMED("coords::init_move_flags()", coords::init_move_flags());
    TIMED("coords::init_center_locals()", coords::init_center_locals());
    TIMED("coords::build_center2520_ud()", coords::build_center2520_ud());
    TIMED("coords::build_center2520_lr()", coords::build_center2520_lr());
    TIMED("coords::build_fb24()", coords::build_fb24());
    TIMED("coords::build_reduced_transitions()", coords::build_reduced_transitions());
    TIMED("coords::build_sigma_p2()", coords::build_sigma_p2());
    TIMED("coords::build_ud2520_dist()", coords::build_ud2520_dist());
    TIMED("coords::build_lr2520_dist()", coords::build_lr2520_dist());
    TIMED("coords::build_wing_good()", coords::build_wing_good());
    TIMED("coords::build_center_good()", coords::build_center_good());
    TIMED("coords::init_layer_equatorial_split()", coords::init_layer_equatorial_split());
    TIMED("coords::init_layer_value_local()", coords::init_layer_value_local());
    fflush(stdout);

    TIMED("coords::build_s4_coords()", coords::build_s4_coords());
    TIMED("init_s4_goals()", init_s4_goals());   // the four S4 goals (literal and x2/y2/z2 images) in the S3'->S4 coordinates
    printf("S3'->S4 tables, variant %s\n", coords::S4_VARIANT_NAME);
    TIMED("coords::build_rest_coord()", coords::build_rest_coord());
    TIMED("s4sym::build(cfg.cache_dir)", s4sym::build(cfg.cache_dir));
    TIMED("s4sym::print_stats()", s4sym::print_stats());
    fflush(stdout);


    TIMED("coords::init_corner_pos_twist()", coords::init_corner_pos_twist());
    TIMED("coords::build_equator_rank_tables()", coords::build_equator_rank_tables());
    TIMED("coords::build_equator_trans()", coords::build_equator_trans());
    TIMED("coords::build_s5_dist()", coords::build_s5_dist());
    TIMED("coords::build_full_to_tier4_index()", coords::build_full_to_tier4_index());
    TIMED("coords::build_corner_ori_trans()", coords::build_corner_ori_trans());
    TIMED("coords::build_center_sum_delta()", coords::build_center_sum_delta());
    TIMED("coords::build_eq_parity_trans()", coords::build_eq_parity_trans());

    g_legacy_tail = !cfg.ls;   // --ls: S5 -> LS -> solved needs none of the S6 / S7 tables, composites and left multiplications
    if (g_legacy_tail) {
        TIMED("coords::init_mslice_split()", coords::init_mslice_split());
        TIMED("coords::build_mslice_rank_tables()", coords::build_mslice_rank_tables());
        TIMED("coords::build_mslice_trans()", coords::build_mslice_trans());
        TIMED("coords::build_corner_coset_table()", coords::build_corner_coset_table());
        TIMED("coords::build_corner_coset_trans()", coords::build_corner_coset_trans());
        TIMED("coords::build_s6_dist()", coords::build_s6_dist());

        TIMED("coords::build_composite_corner_id()", coords::build_composite_corner_id());
        TIMED("coords::build_composite_corner_trans()", coords::build_composite_corner_trans());
        TIMED("coords::build_tuple4of8_tables()", coords::build_tuple4of8_tables());
        TIMED("coords::build_slice4_composite_tables()", coords::build_slice4_composite_tables());
        TIMED("coords::build_perm4_parity()", coords::build_perm4_parity());
        TIMED("coords::build_eq_perm_trans5()", coords::build_eq_perm_trans5());
        TIMED("coords::build_center_byte_delta()", coords::build_center_byte_delta());
        TIMED("coords::build_full_to_tier5_index()", coords::build_full_to_tier5_index());
        TIMED("coords::build_full_to_tier6_index()", coords::build_full_to_tier6_index());

        TIMED("coords::build_slice_pos_trans()", coords::build_slice_pos_trans());
        TIMED("coords::build_slice_perm_trans()", coords::build_slice_perm_trans());
        TIMED("coords::build_ec_reachable()", coords::build_ec_reachable());
        TIMED("coords::build_h_trans()", coords::build_h_trans());
        TIMED("coords::build_s7_dist()", coords::build_s7_dist());
    }
    TIMED("init_lazy_tables()", init_lazy_tables());
    TIMED("init_niss_tables()", init_niss_tables());
    if (cfg.ls) TIMED("ls::build()", ls::build());
    TIMED("p2j::build()", p2j::build());   // needs the F/B transitions made by init_lazy_tables
    TIMED("s2sw::build()", s2sw::build());  // S2 -> S3' switch route tables (needs the equatorial split, sigma tables and the P2 move index)

    size_t p1_bytes = 0;
    for (auto& b : p1::BLOOM) p1_bytes += b.bytes();
    for (auto& b : p2j::BLOOM) p1_bytes += b.bytes();
    p1_bytes += s2sw::LR8_TRANS.size() * 2 + s2sw::LR8_DIST.size() + s2sw::WING_DIST.size();
    size_t bytes = p1_bytes + g_wing.dist.size() + g_fb.dist.size()
                 + s4sym::bytes()
                 + coords::S5_DIST_PACKED.size() + coords::S6_DIST_PACKED.size()
                 + coords::S7_DIST_PACKED.size() + coords::EC_DENSE.size() * sizeof(int);
    printf("all tables ready: %.0f ms total, ~%.0f MB of tables (S3'->S4: %.0f MB)\n",
           ms_between(t0, std::chrono::steady_clock::now()), bytes / 1e6, s4sym::bytes() / 1e6);
    fflush(stdout);
}

// ------------------------------------------------------------------ profile
static double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static void print_fork_stats() {
    const Fork* f = g_fork;
    printf("S1-fork diagnostic: attempts identity=%lld x=%lld y=%lld | wins identity=%lld x=%lld y=%lld\n",
           f[0].attempts[0], f[0].attempts[1], f[0].attempts[2], f[0].wins[0], f[0].wins[1], f[0].wins[2]);
    printf("S2-fork diagnostic: attempts identity=%lld z=%lld | wins identity=%lld z=%lld\n",
           f[1].attempts[0], f[1].attempts[1], f[1].wins[0], f[1].wins[1]);
    printf("S4-fork diagnostic: attempts identity=%lld z=%lld | wins identity=%lld z=%lld\n",
           f[2].attempts[0], f[2].attempts[1], f[2].wins[0], f[2].wins[1]);
}

static int run_profile(const Config& cfg) {
    std::mt19937 rng(cfg.seed);
    std::mt19937 rng_rs(cfg.seed ^ 0x9E3779B9u);   // the randomized start's own stream: scrambles stay the same with / without it
    long long rs_fallbacks = 0, rs_gen_runs = 0, rs_runs = 0, rs_best_sum = 0, rs_win_sum = 0, rs_wins = 0, rs_visible = 0;
    double rs_gen_ms = 0, rs_search_ms = 0;
    int rs_win_d_hist[16] = {0}, rs_best_hist[16] = {0};
    std::uniform_int_distribution<int> move_dist(0, NUM_MOVES - 1);
    std::uniform_int_distribution<int> len_dist(cfg.scramble_min, cfg.scramble_max);
    const int trials = cfg.profile_trials;
    int successes = 0, literal_failures = 0;
    double total_ms = 0;
    long long total_len = 0, total_nodes = 0;
    std::vector<double> all_ms, all_len, all_nodes;
    for (int trial = 0; trial < trials; trial++) {
        Cube cube;   // what the search sees: L S P (just S without --prefix/--suffix)
        std::vector<int> scramble;
        int n = len_dist(rng);
        apply_letters(cube, g_forced.suffix);
        for (int i = 0; i < n; i++) { int mv = move_dist(rng); scramble.push_back(mv); cube.apply(mv); }
        apply_letters(cube, g_forced.prefix);
        int root_rot = -1;
        std::vector<int> sol;
        std::string niss_notation;
        rs::Stats rst;
        g_nodes = 0;
        reset_tier_stats();
        auto ta = std::chrono::steady_clock::now();
        bool found;
        if (rs::g_on) found = rs::solve_random_start(cube, rs::g_cfg, rng_rs, cfg.max_len, sol, &niss_notation, root_rot, rst);
        else {
            State6 start = cube.state(&root_rot);
            g_forced.root_rot = root_rot;
            found = solve(start, cfg.max_len, sol, &niss_notation);
        }
        double dt = ms_between(ta, std::chrono::steady_clock::now());
        if (!found) { printf("PROFILE trial %d: NO SOLUTION (scramble len %d)\n", trial, n); fflush(stdout); continue; }
        const std::vector<int> z = sol;
        sol = assemble_forced(z, root_rot);   // P Z L
        Cube check;
        for (int mv : scramble) check.apply(mv);
        apply_letters(check, sol);
        const bool literal = is_in_s7(rs::raw_state(check));   // the replayed cube itself, not canonicalized by a rotation
        const bool visible = rs::is_visibly_solved(check);
        if (!literal) literal_failures++;
        if (visible) rs_visible++;
        // with a forced suffix only the literal solved cube is right (L S P Z = id says nothing about L^-1 r L);
        // the randomized start solves the normal cube only: centers may sit permuted inside their faces
        const bool ok = rs::g_on ? visible : (literal || (!g_forced.active() && is_in_s7_mod_conjugation(check.state())));
        if (!ok) {
            printf("PROFILE trial %d: INVALID SOLUTION: scramble=%s solution=%s\n", trial,
                   format_solution(scramble).c_str(), format_solution(sol).c_str());
            fflush(stdout);
            continue;
        }
        int len = solution_move_count(sol);
        successes++;
        total_ms += dt; total_len += len; total_nodes += g_nodes;
        all_ms.push_back(dt); all_len.push_back(len); all_nodes.push_back((double)g_nodes);
        printf("  trial %2d: scramble=%2d  solution=%2d  (%8.2f ms)  nodes=%10lld  %.2fM nodes/s\n",
               trial, n, len, dt, g_nodes, g_nodes / (dt / 1000.0) / 1e6);
        if (rs::g_on) {
            rs_gen_ms += rst.gen_ms; rs_search_ms += rst.search_ms; rs_runs += rst.runs; rs_gen_runs++;
            if (rst.fallback) rs_fallbacks++;
            else { rs_wins++; rs_win_sum += rst.win_dist; rs_win_d_hist[std::min(rst.win_d, 15)]++; }
            if (rst.best_dist >= 0) { rs_best_sum += rst.best_dist; rs_best_hist[std::min(rst.best_dist, 15)]++; }
            printf("    random start: %d candidate cosets (S1 distance <=4: %d, <=5: %d, <=6: %d), closest %d; %s; %lld IDA* runs; walk %.0f ms, search %.0f ms\n",
                   rst.kept, rst.within[4], rst.within[5], rst.within[6], rst.best_dist,
                   rst.fallback ? "NO candidate solved: fell back to the ordinary search" : ("solved from a candidate at distance " + std::to_string(rst.win_dist) + " in pass d=" + std::to_string(rst.win_d)).c_str(),
                   rst.runs, rst.gen_ms, rst.search_ms);
        }
        if (cfg.print_solutions) {
            printf("    scramble: %s\n    solution: %s\n", format_solution(scramble).c_str(), format_solution(sol).c_str());
            if (g_forced.active()) printf("    searched: %s\n", format_solution(z).c_str());
            if (g_niss) printf("    niss:     %s\n", niss_notation.c_str());
        }
        if (g_tier_stats) {
            printf("    spent:   S0=%lld S1=%lld S2=%lld S3'=%lld S4=%lld S5=%lld S6=%lld\n", g_spent_tier[0], g_spent_tier[1],
                   g_spent_tier[2], g_spent_tier[3], g_spent_tier[4], g_spent_tier[5], g_spent_tier[6]);
            printf("    entered: S1=%lld S2=%lld S3'=%lld S4=%lld S5=%lld S6=%lld S7=%lld\n", g_entered_s[1], g_entered_s[2],
                   g_entered_s[3], g_entered_s[4], g_entered_s[5], g_entered_s[6], g_entered_s[7]);
        }
        fflush(stdout);
    }
    printf("\n=== PROFILE: %d trials, COST12=%d COST23'=%d COST3'4=%d COST45=%d COST56=%d COST67=%d, penalty(thresh=%d,rate=%d), scramble len [%d,%d], S4 variant %s ===\n",
           trials, g_cost12, g_cost23p, g_cost34p, g_cost45, g_cost56, g_cost67, coords::PENALTY_THRESHOLD,
           coords::PENALTY_RATE, cfg.scramble_min, cfg.scramble_max, coords::S4_VARIANT_NAME);
    printf("success: %d/%d (%.1f%%)\n", successes, trials, 100.0 * successes / trials);
    if (successes > 0) {
        printf("time (ms):  avg=%.2f  median=%.2f  min=%.2f  max=%.2f  total=%.2f\n", total_ms / successes, median_of(all_ms),
               *std::min_element(all_ms.begin(), all_ms.end()), *std::max_element(all_ms.begin(), all_ms.end()), total_ms);
        printf("length:     avg=%.2f  median=%.1f  min=%.0f  max=%.0f\n", (double)total_len / successes, median_of(all_len),
               *std::min_element(all_len.begin(), all_len.end()), *std::max_element(all_len.begin(), all_len.end()));
        printf("nodes:      avg=%.0f  median=%.0f  min=%.0f  max=%.0f  throughput=%.2fM nodes/s\n", (double)total_nodes / successes,
               median_of(all_nodes), *std::min_element(all_nodes.begin(), all_nodes.end()),
               *std::max_element(all_nodes.begin(), all_nodes.end()), total_nodes / (total_ms / 1000.0) / 1e6);
    }
    print_fork_stats();
    if (g_ls) printf("LS: %lld LS -> solved searches (one per word that reached LS), %lld nested nodes (%.1f per search)\n",
                     ls::g_terminals, ls::g_nested_nodes, ls::g_terminals ? (double)ls::g_nested_nodes / ls::g_terminals : 0.0);
    if (g_niss) {
        printf("NISS: root wins normal=%lld inverse=%lld | variants tried/won [flip][switch]: 00=%lld/%lld 10=%lld/%lld 01=%lld/%lld 11=%lld/%lld\n",
               g_niss_root_wins[0], g_niss_root_wins[1], g_niss_attempts[0][0], g_niss_wins[0][0], g_niss_attempts[1][0], g_niss_wins[1][0],
               g_niss_attempts[0][1], g_niss_wins[0][1], g_niss_attempts[1][1], g_niss_wins[1][1]);
    }
    if (g_niss) printf("NISS: switch crossings creating a free move=%lld, of which preceded by a commuting quarter turn=%lld\n", g_niss_sw_cross, g_niss_cluster_cross);
    if (g_check_replay) printf("replay check: %lld NISS branches compared, %lld state mismatches, %lld pending mismatches\n", g_replay_checked, g_replay_bad, g_replay_pend_bad);
    if (g_check_replay) printf("checkpoint check: %lld reconstructions compared with the from-the-root replay, %lld mismatches\n", g_ck_checked, g_ck_bad);
    if (g_forced.active())
        printf("forced ends: prefix [%s] suffix [%s]; %lld solved cubes reached, %lld refused (their solution would cancel with the prefix/suffix)\n",
               format_solution(g_forced.prefix).c_str(), format_solution(g_forced.suffix).c_str(), g_forced.checked, g_forced.rejected);
    if (rs::g_on) {
        printf("random start: %lld/%lld solved from a candidate (%lld fell back to the ordinary search); avg closest candidate distance to S1 %.2f, avg winning candidate %.2f\n",
               rs_wins, rs_gen_runs, rs_fallbacks, rs_gen_runs ? (double)rs_best_sum / rs_gen_runs : 0.0, rs_wins ? (double)rs_win_sum / rs_wins : 0.0);
        printf("  closest candidate distance histogram:");
        for (int d = 0; d < 16; d++) if (rs_best_hist[d]) printf(" %d:%d", d, rs_best_hist[d]);
        printf("\n  winning pass d histogram:");
        for (int d = 0; d < 16; d++) if (rs_win_d_hist[d]) printf(" %d:%d", d, rs_win_d_hist[d]);
        printf("\n  per trial: walk %.1f ms, search %.1f ms, %.1f IDA* runs\n",
               rs_gen_runs ? rs_gen_ms / rs_gen_runs : 0.0, rs_gen_runs ? rs_search_ms / rs_gen_runs : 0.0, rs_gen_runs ? (double)rs_runs / rs_gen_runs : 0.0);
        printf("verification: %lld/%d solutions solve the normal (non-super) cube; %d of them reach the literal supercube solved state\n", rs_visible, successes, successes - literal_failures);
    } else
        printf("verification: %d/%d solutions reach the literal solved cube (the rest only up to a whole-cube rotation)\n", successes - literal_failures, successes);
    fflush(stdout);
    return 0;
}

// ------------------------------------------------------------------ REPL
static std::string alg_text(const std::vector<int>& w) { return w.empty() ? "(none)" : format_solution(w); }

static int run_repl(const Config& cfg) {
    std::mt19937 rng_rs(cfg.seed ^ 0x9E3779B9u);
    printf("\nType a scramble (any of the 27 moves, plus x/y/z rotations), or 'quit'.\n"
           "  prefix <moves> / suffix <moves>: force the solution to start / end with those moves (no moves: clear)\n");
    if (g_forced.active()) printf("  prefix: %s   suffix: %s\n", alg_text(g_forced.prefix).c_str(), alg_text(g_forced.suffix).c_str());
    std::string line;
    while (true) {
        printf("\nsolver> ");
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line == "quit" || line == "exit") break;
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        {
            std::istringstream first(line);
            std::string word, rest;
            first >> word;
            if (word == "prefix" || word == "suffix") {
                std::getline(first, rest);
                std::vector<int> alg;
                std::string bad = parse_alg(rest, alg);
                if (!bad.empty()) { printf("  Unknown move '%s'\n", bad.c_str()); continue; }
                (word == "prefix" ? g_forced.prefix : g_forced.suffix) = alg;
                printf("  prefix: %s   suffix: %s\n", alg_text(g_forced.prefix).c_str(), alg_text(g_forced.suffix).c_str());
                continue;
            }
        }
        std::vector<int> scramble;
        std::string bad = parse_alg(line, scramble);
        if (!bad.empty()) { printf("  Unknown move '%s'\n", bad.c_str()); continue; }
        Cube cube;   // what the search sees: L S P
        apply_letters(cube, g_forced.suffix);
        apply_letters(cube, scramble);
        apply_letters(cube, g_forced.prefix);
        int root_rot = -1;
        std::vector<int> z;
        std::string niss_notation;
        rs::Stats rst;
        bool found;
        auto ta = std::chrono::steady_clock::now();
        if (rs::g_on) found = rs::solve_random_start(cube, rs::g_cfg, rng_rs, cfg.max_len, z, &niss_notation, root_rot, rst);
        else {
            State6 s = cube.state(&root_rot);
            g_forced.root_rot = root_rot;
            int tier;
            int h = heuristic(s, &tier);
            printf("  tier %d (in S%d)   h=%d\n", tier, tier, h);
            found = solve(s, cfg.max_len, z, &niss_notation);
        }
        auto tb = std::chrono::steady_clock::now();
        if (!found) { printf("  No solution found within %d moves.\n", cfg.max_len); continue; }
        const std::vector<int> sol = assemble_forced(z, root_rot);   // P Z L
        printf("  Solution (%d moves, %.3f ms): %s\n", solution_move_count(sol), ms_between(ta, tb), format_solution(sol).c_str());
        if (g_forced.active()) {
            std::vector<int> zr = z;
            if (root_rot > 0) zr.insert(zr.begin(), ROTATION_SENTINEL_BASE + root_rot);
            printf("  = prefix + %d searched moves + suffix; searched part: %s\n", solution_move_count(z), format_solution(zr).c_str());
        }
        if (g_niss) printf("  NISS shorthand (searched part): %s\n", niss_notation.c_str());
        if (rs::g_on)
            printf("  random start: %d candidate cosets (S1 distance <=4: %d, <=5: %d, <=6: %d), closest %d; %s; %lld IDA* runs (walk %.0f ms, search %.0f ms)\n",
                   rst.kept, rst.within[4], rst.within[5], rst.within[6], rst.best_dist,
                   rst.fallback ? "no candidate solved, ordinary search used" : ("solved from a candidate at distance " + std::to_string(rst.win_dist) + " in pass d=" + std::to_string(rst.win_d)).c_str(),
                   rst.runs, rst.gen_ms, rst.search_ms);
        Cube check;
        apply_letters(check, scramble);
        apply_letters(check, sol);
        if (rs::g_on) printf("  verified: %s\n", rs::is_visibly_solved(check) ? "solves the normal cube (centers may be permuted inside their faces)" : "DOES NOT SOLVE THE CUBE (bug!)");
        else if (g_forced.active()) printf("  verified: %s\n", is_in_s7(check.state()) ? "solves the scramble (prefix and suffix included)" : "DOES NOT SOLVE THE SCRAMBLE (bug!)");
        else printf("  verified: %s\n", is_in_s7_mod_conjugation(check.state()) ? "reaches S7 (mod conjugation)" : "DOES NOT REACH S7 (bug!)");
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 1;
    printf("COST12=%d COST23'=%d COST3'4=%d COST45=%d COST56=%d COST67=%d  penalty(threshold=%d, rate=%d)\n",
           g_cost12, g_cost23p, g_cost34p, g_cost45, g_cost56, g_cost67, coords::PENALTY_THRESHOLD, coords::PENALTY_RATE);
    if (rs::g_on && !g_niss) { printf("--random-start needs NISS (it starts on the inverse side); drop --no-niss\n"); return 1; }
    build_all_tables(cfg);
    printf("S1 endtable: exact distances to S1 up to %d (layer sizes", p1::g_depth);
    for (size_t n : p1::LAYER_SIZES) printf(" %zu", n);
    size_t bloom_bytes = 0; for (auto& b : p1::BLOOM) bloom_bytes += b.bytes();
    printf("), cumulative Bloom filters %.1f MB, built in %.0f ms\n", bloom_bytes / 1e6, p1::g_build_ms);
    if (p2j::g_depth > 0) {
        printf("S2 endtable: exact distances to S2 up to %d over (wing mask, F/B permutation) (layer sizes", p2j::g_depth);
        for (size_t i = 0; i < p2j::LAYER_SIZES.size(); i++) printf(" %zu%s", p2j::LAYER_SIZES[i], i + 1 == p2j::LAYER_SIZES.size() ? "~" : "");
        size_t p2_bytes = 0; for (auto& b : p2j::BLOOM) p2_bytes += b.bytes();
        printf("), cumulative Bloom filters %.1f MB, built in %.0f ms; heuristic = max(endtable%s%s)\n", p2_bytes / 1e6, p2j::g_build_ms,
               g_p2_use_wing ? ", wing table" : "", g_p2_use_fb ? ", F/B table" : "");
    }
    printf("S2 -> S3' switch route: L/R centers 40320 states, wings 1470150 classes, built in %.0f ms\n", s2sw::g_build_ms);
    s2sw::print_hist("L/R centers", s2sw::LR8_HIST, 40320);
    s2sw::print_hist("wings", s2sw::WING_HIST, s2sw::NWING);
    if (g_check_s4_sym) return s4sym::check_against_full() ? 0 : 1;
    if (cfg.ls) ls::print_info();
    if (!cfg.skip_selftest && !run_selftests()) { printf("Aborting: fix the failing self-test(s) before trusting any search result.\n"); return 1; }
    if (cfg.ls) { enable_ls(); printf("LS mode: S5 -> LS -> solved, COST(S5->LS->solved)=%d (COST56=%d COST67=%d)\n", g_cost_ls, g_cost56, g_cost67); }
    if (cfg.profile_trials > 0) return run_profile(cfg);
    return run_repl(cfg);
}
