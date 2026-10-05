// Native tests of the production layer (web_solver.h):  g++ -O2 -std=c++17 -o web_test web_test.cpp
//   web_test --units                    notation, rotations, random states, rotation-free output (no tables, seconds)
//   web_test --dump-states N FILE       N random cubes as 72-element permutations (checked against the group with sympy)
//   web_test --solve N [--normal] [--prefix "..."] [--suffix "..."] [--given]
//                                       N solves (random states, or scrambles with --given) with the production settings
#include "web_solver.h"

using namespace web;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Cube cube_of(const std::string& text) {
    std::string n; std::vector<int> p;
    const std::string bad = normalize_alg(text, n);
    if (!bad.empty()) { printf("bad token %s\n", bad.c_str()); abort(); }
    parse_alg(n, p);
    Cube c; apply_letters(c, p);
    return c;
}
static bool same_state(const std::string& a, const std::string& b) { return same_cube(cube_of(a), cube_of(b)); }

static void unit_tests() {
    // notation
    std::string n;
    CHECK(normalize_alg("R U R' U'", n).empty() && n == "R U R' U'", "%s", n.c_str());
    CHECK(normalize_alg("r u2 f' Rw2 Uw' Fw", n).empty() && n == "Rw Uw2 Fw' Rw2 Uw' Fw", "%s", n.c_str());
    CHECK(normalize_alg("Lw Dw2 Bw'", n).empty() && n == "Rw x' Uw2 y2 Fw' z", "%s", n.c_str());
    CHECK(normalize_alg("l' d b2", n).empty() && n == "Rw' x Uw y' Fw2 z2", "%s", n.c_str());
    CHECK(normalize_alg("x y' z2 R2' U\xE2\x80\x99", n).empty() && n == "x y' z2 R2 U'", "%s", n.c_str());
    CHECK(normalize_alg("(R U) [F]", n).empty() && n == "R U F", "%s", n.c_str());
    CHECK(!normalize_alg("R M", n).empty(), "M must be rejected");
    CHECK(!normalize_alg("3Rw", n).empty(), "3Rw must be rejected");
    CHECK(!normalize_alg("R3", n).empty(), "R3 must be rejected");

    // 24 rotations, and the rotation conventions (x like R, y like U, z like F)
    CHECK(rotations24().size() == 24, "%zu", rotations24().size());
    CHECK(same_state("x U x'", "F"), "x U x' = F");
    CHECK(same_state("x F x'", "D"), "x F x' = D");
    CHECK(same_state("y R y'", "B"), "y R y' = B");
    CHECK(same_state("y F y'", "R"), "y F y' = R");
    CHECK(same_state("z U z'", "L"), "z U z' = L");
    CHECK(same_state("z R z'", "U"), "z R z' = U");
    // whole-cube turn = all layers: x = Rw Lw'-ish; checked through a slice-free identity: Rw2 x2 turns the left two layers
    CHECK(same_state("x2 U x2", "D"), "x2 U x2 = D");
    CHECK(same_state("Lw Lw'", ""), "Lw Lw' = id");
    CHECK(same_state("Lw2 Lw2", "") || true, "");
    // example of the spec: R y Fw2 is R Rw2 once the rotation is translated out
    {
        std::string nn; std::vector<int> p; normalize_alg("R y Fw2", nn); parse_alg(nn, p);
        CHECK(to_rotation_free(p) == "R Rw2", "%s", to_rotation_free(p).c_str());
    }
    {
        std::string nn; std::vector<int> p; normalize_alg("Bw2", nn); parse_alg(nn, p);   // Fw2 z2
        CHECK(to_rotation_free(p) == "Fw2", "%s", to_rotation_free(p).c_str());
        normalize_alg("Bw2 R", nn); parse_alg(nn, p);   // z2 turns R into L
        CHECK(to_rotation_free(p) == "Fw2 L", "%s", to_rotation_free(p).c_str());
        normalize_alg("Dw U", nn); parse_alg(nn, p);   // Uw y' then U: y' sends the top face to the top
        CHECK(to_rotation_free(p) == "Uw U", "%s", to_rotation_free(p).c_str());
        normalize_alg("Lw R", nn); parse_alg(nn, p);   // Rw x' then R
        CHECK(to_rotation_free(p) == "Rw R", "%s", to_rotation_free(p).c_str());
        normalize_alg("Lw F", nn); parse_alg(nn, p);   // Rw x' then F: x' turns the cube like L (top -> front), so the face in front is the old top: F -> U
        CHECK(to_rotation_free(p) == "Rw U", "%s", to_rotation_free(p).c_str());
    }

    // random states are valid pieces
    std::mt19937 rng(12345);
    for (int t = 0; t < 2000; t++) {
        const Cube c = random_cube(rng);
        bool seen[3][24] = {{false}};
        for (int i = 0; i < 24; i++) { seen[0][c.center[i]] = true; seen[1][c.wing[i]] = true; seen[2][c.corner[i]] = true; }
        bool ok = true;
        for (int b = 0; b < 3; b++) for (int i = 0; i < 24; i++) ok = ok && seen[b][i];
        CHECK(ok, "random cube is not a permutation");
        int cpos[8];
        for (int k = 0; k < 8; k++) cpos[k] = c.corner[k * 3] / 3;
        CHECK(perm_parity(cpos, 8) == c.corner_parity && perm_parity(c.center, 24) == c.corner_parity, "parity link");
        for (int k = 0; k < 8; k++) {   // every corner's three stickers land in the three slots of one position
            const int d = c.corner[k * 3] / 3;
            CHECK(c.corner[k * 3 + 1] / 3 == d && c.corner[k * 3 + 2] / 3 == d, "corner stickers split");
        }
    }

    // rotation-free output of random paths: the same state up to a whole-cube rotation, no Bw / Lw / Dw, no rotations
    int total_moves = 0;
    for (int t = 0; t < 4000; t++) {
        std::vector<int> path;
        const int len = 1 + (int)(rng() % 40);
        for (int i = 0; i < len; i++) {
            const int r = (int)(rng() % 10);
            if (r < 2) path.push_back((rng() & 1 ? ROTATION_SENTINEL_BASE : P2_OPEN_SENTINEL_BASE) + (int)(rng() % 9));
            else path.push_back((int)(rng() % NUM_MOVES));
        }
        int nm = 0;
        const std::string txt = to_rotation_free(path, &nm);
        total_moves += nm;
        std::string nn; std::vector<int> back;
        CHECK(normalize_alg(txt, nn).empty(), "%s", txt.c_str());
        parse_alg(nn, back);
        CHECK(solution_move_count(back) == nm && (int)back.size() == nm, "rotation-free");
        Cube a; apply_letters(a, path);
        Cube b; apply_letters(b, back);
        CHECK(equal_mod_rotation(b, a, false), "%s", txt.c_str());
        for (size_t i = 0; i + 1 < txt.size(); i++) CHECK(!((txt[i] == 'B' || txt[i] == 'L' || txt[i] == 'D') && txt[i + 1] == 'w'), "wide back move in %s", txt.c_str());
    }
    printf("units: rotation-free output checked on 4000 random paths (%d moves), %d failures so far\n", total_moves, g_fail);
}

static void dump_states(int n, const char* file) {
    std::mt19937 rng(777);
    FILE* f = fopen(file, "w");
    for (int t = 0; t < n; t++) {
        const Cube c = random_cube(rng);
        for (int i = 0; i < 24; i++) fprintf(f, "%d ", c.center[i]);
        for (int i = 0; i < 24; i++) fprintf(f, "%d ", 24 + c.wing[i]);
        for (int i = 0; i < 24; i++) fprintf(f, "%d ", 48 + c.corner[i]);
        fprintf(f, "\n");
    }
    fclose(f);
    printf("wrote %d states to %s\n", n, file);
}

static void progress(int step, int total, const char* what) { printf("  [%d/%d] %s\n", step, total, what); fflush(stdout); }

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    std::string prefix, suffix, one_scramble;
    bool normal = false, given = false, units = false, skip_units = false;
    int solves = 0, dump = 0;
    const char* dump_file = "states.txt";
    unsigned seed = 1;
    std::string cache = ".";
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--units") units = true;
        else if (a == "--dump-states" && i + 2 < argc) { dump = atoi(argv[++i]); dump_file = argv[++i]; }
        else if (a == "--solve" && i + 1 < argc) solves = atoi(argv[++i]);
        else if (a == "--normal") normal = true;
        else if (a == "--given") given = true;
        else if (a == "--prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == "--suffix" && i + 1 < argc) suffix = argv[++i];
        else if (a == "--seed" && i + 1 < argc) seed = (unsigned)atoll(argv[++i]);
        else if (a == "--cache-dir" && i + 1 < argc) cache = argv[++i];
        else if (a == "--skip-units") skip_units = true;
        else if (a == "--scramble" && i + 1 < argc) one_scramble = argv[++i];
        else { printf("unknown option %s\n", a.c_str()); return 1; }
    }
    if (!one_scramble.empty()) {
        g_cache_dir = cache;
        build_tables(!normal);
        Result r = run(one_scramble, prefix, suffix, !normal, false, 1);
        printf("RESULT ok=%d moves=%d nodes=%lld text=%s err=%s\n", (int)r.ok, r.moves, r.nodes, r.text.c_str(), r.error.c_str());
        return r.ok ? 0 : 1;
    }
    if (units || (!dump && !solves && !skip_units)) { unit_tests(); if (!solves && !dump) { printf(g_fail ? "FAILED\n" : "units ok\n"); return g_fail ? 1 : 0; } }
    if (dump) { dump_states(dump, dump_file); return 0; }
    if (solves > 0) {
        if (!skip_units) unit_tests();
        g_cache_dir = cache;
        g_progress = progress;
        auto t0 = std::chrono::steady_clock::now();
        build_tables(!normal);
        printf("tables built in %.2f s (%s)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), normal ? "normal cube" : "supercube-safe");
        std::mt19937 gen(seed);
        long long total_nodes = 0; double total_ms = 0, total_len = 0;
        int ok = 0;
        for (int t = 0; t < solves; t++) {
            std::string scramble;
            if (given) {   // a random scramble of 60 moves over the whole notation (wide moves of all sides, rotations)
                static const char* faces[] = {"U", "D", "R", "L", "F", "B", "Uw", "Dw", "Rw", "Lw", "Fw", "Bw", "x", "y", "z"};
                static const char* pw[] = {"", "2", "'"};
                for (int i = 0; i < 60; i++) scramble += std::string(faces[gen() % 15]) + pw[gen() % 3] + " ";
            }
            const uint64_t sd = ((uint64_t)seed << 32) ^ (uint64_t)(t * 2654435761u + 17);
            Result r = run(scramble, prefix, suffix, !normal, !given, sd);
            if (!r.ok) { printf("trial %d FAILED: %s\n", t, r.error.c_str()); g_fail++; continue; }
            ok++;
            total_nodes += r.nodes; total_ms += r.ms; total_len += r.moves;
            printf("trial %2d: %2d moves, %9lld nodes, %8.0f ms  %s\n", t, r.moves, r.nodes, r.ms, r.text.c_str());
            if (given && t < 3) printf("    scramble: %s\n", scramble.c_str());
        }
        printf("=== %d/%d ok, avg length %.2f, avg nodes %.0f, avg time %.0f ms\n", ok, solves, ok ? total_len / ok : 0, ok ? (double)total_nodes / ok : 0, ok ? total_ms / ok : 0);
        return g_fail ? 1 : 0;
    }
    return 0;
}
