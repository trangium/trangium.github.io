// Build: g++ -O2 -std=c++17 -o web_fmc_test web_fmc_test.cpp   Run from this folder: web_fmc_test --minutes 20 [--gen-seed N] [--safe --pad ""] [--n N] [--log FILE]
// Long-running test that is as faithful as possible to pressing the website's "FMC scramble" button:
//   web::run("", "Rw' Uw' F", "Rw' Uw' F", safe=false, random_state=true, seed=<fresh random 64-bit seed>)   (index.html / worker.js)
// and the printed (rotation free) text is what the button shows. Every scramble is checked for redundancy:
//   * the text starts and ends with the padding,
//   * every maximal run of consecutive printed moves on one axis (U/D, R/L, F/B; outer or wide) is rewritten as the fewest moves possible.
//     Layers of an axis commute; a run is a vector in Z4^4 (layer 1..4 turn counts, seen from the U/R/F side), whole-cube rotations are free
//     (mod (1,1,1,1)), and the available moves are X (layer 1), Xw (layers 1+2) and Y (layer 4) in the quarter-turn powers 1,2,3
//     (Dw = Xw mod rotation). The fewest moves = number of non-zero coordinates in the basis (X, Xw, Y). A run with more moves than that
//     is redundant (e.g. R Rw R2, U D Uw U2, L' L2, Rw2 Rw').
//   * each redundant run is confirmed by replay: the shorter rewrite must give the same position as the printed scramble (the site's
//     own equality: up to a whole-cube rotation and the order of centers inside a face).
#include "web_solver.h"
#include <fstream>
#include <array>
#include <set>
using namespace web;

struct Tok { int axis; int kind; int p; std::string text; };   // kind 0 X, 1 Xw, 2 Y, 3 Yw
static bool parse_tok(const std::string& t, Tok& o) {
    if (t.empty()) return false;
    const char c = t[0];
    int axis, neg;
    switch (c) {
        case 'U': axis = 0; neg = 0; break; case 'D': axis = 0; neg = 1; break;
        case 'R': axis = 1; neg = 0; break; case 'L': axis = 1; neg = 1; break;
        case 'F': axis = 2; neg = 0; break; case 'B': axis = 2; neg = 1; break;
        default: return false;
    }
    size_t i = 1;
    bool wide = false;
    if (i < t.size() && t[i] == 'w') { wide = true; i++; }
    int p = 1;
    if (i < t.size()) { if (t[i] == '2') p = 2; else if (t[i] == '\'') p = 3; else return false; i++; }
    if (i != t.size()) return false;
    o.axis = axis; o.kind = (neg ? 2 : 0) + (wide ? 1 : 0); o.p = p; o.text = t;
    return true;
}
static void add_vec(int v[4], const Tok& t) {
    switch (t.kind) {
        case 0: v[0] += t.p; break;
        case 1: v[0] += t.p; v[1] += t.p; break;
        case 2: v[3] -= t.p; break;
        case 3: v[2] -= t.p; v[3] -= t.p; break;
    }
}
static int md4(int a) { return ((a % 4) + 4) % 4; }
// the fewest moves for the run, and the rewrite
static int fewest(const std::vector<Tok>& run, std::vector<std::string>* out) {
    int v[4] = {0, 0, 0, 0};
    for (const Tok& t : run) add_vec(v, t);
    const int v1 = md4(v[0] - v[3]), v2 = md4(v[1] - v[3]), v3 = md4(v[2] - v[3]);
    const int x = md4(v1 - v2), w = md4(v2 - v3), y = v3;
    static const char* FACE[3][2] = {{"U", "D"}, {"R", "L"}, {"F", "B"}};
    const int axis = run[0].axis;
    auto pw = [](int p) { return p == 1 ? "" : (p == 2 ? "2" : "'"); };
    int n = 0;
    if (x) { n++; if (out) out->push_back(std::string(FACE[axis][0]) + pw(x)); }
    if (w) { n++; if (out) out->push_back(std::string(FACE[axis][0]) + "w" + pw(w)); }
    if (y) { n++; if (out) out->push_back(std::string(FACE[axis][1]) + pw(y)); }
    return n;
}
static std::vector<std::string> split(const std::string& s) { std::vector<std::string> v; std::istringstream is(s); std::string w; while (is >> w) v.push_back(w); return v; }

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    double minutes = 20; uint64_t gen_seed = 0; std::string pad = "Rw' Uw' F"; std::string logfile = "fmc_long_log.txt";
    int max_n = 1 << 30; bool selftest_only = false; bool safe = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--minutes" && i + 1 < argc) minutes = atof(argv[++i]);
        else if (a == "--gen-seed" && i + 1 < argc) gen_seed = (uint64_t)atoll(argv[++i]);
        else if (a == "--log" && i + 1 < argc) logfile = argv[++i];
        else if (a == "--n" && i + 1 < argc) max_n = atoi(argv[++i]);
        else if (a == "--checker-selftest") selftest_only = true;
        else if (a == "--safe") safe = true;
        else if (a == "--pad" && i + 1 < argc) pad = argv[++i];
    }
    // checker self-test on the examples
    {
        const char* red[] = {"R Rw R2", "U D Uw U2", "L' L2", "Rw2 Rw'", "U D U", "F F'", "Uw U Uw'"};
        const char* ok[] = {"R Rw", "U D", "Uw D", "U Uw", "R2 L2", "Rw L", "F B2 Fw'", "U R U'"};
        int bad = 0;
        for (const char* s : red) { std::vector<Tok> run; for (auto& t : split(s)) { Tok k; parse_tok(t, k); if (run.empty() || run[0].axis == k.axis) run.push_back(k); } if ((int)run.size() <= fewest(run, nullptr)) { bad++; printf("checker miss: %s\n", s); } }
        for (const char* s : ok) { std::vector<Tok> run; for (auto& t : split(s)) { Tok k; parse_tok(t, k); if (run.empty() || run[0].axis == k.axis) run.push_back(k); } if ((int)run.size() != 0 && (int)run.size() > fewest(run, nullptr) && run.size() == split(s).size()) { bad++; printf("checker false alarm: %s\n", s); } }
        printf("checker self-test: %s\n", bad ? "FAILED" : "ok");
        if (bad || selftest_only) return bad ? 1 : 0;
    }
    auto t0 = std::chrono::steady_clock::now();
    build_tables(safe);
    printf("tables built in %.1f s (normal cube, as the button)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    std::mt19937_64 gen(gen_seed ? gen_seed : (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::ofstream log(logfile, std::ios::app);
    const std::vector<std::string> padtok = split(pad);
    // every way the end padding can read in the rotation-free text: the padding moves after up to three whole-cube rotations, printed by
    // the site's own to_rotation_free (Dw / Lw / Bw become the opposite wide move and turn the frame again, so it is not one conjugation)
    std::set<std::string> end_forms;
    std::string literal_end;
    {
        std::string nn; std::vector<int> pp;
        normalize_alg(pad, nn); parse_alg(nn, pp);
        std::vector<int> rot_tok = {-1};
        for (int cls = 1; cls < NUM_ROTATE_CLASSES; cls++) { rot_tok.push_back(ROTATION_SENTINEL_BASE + cls); rot_tok.push_back(P2_OPEN_SENTINEL_BASE + cls); }
        for (int a : rot_tok) for (int b : rot_tok) for (int c : rot_tok) {
            std::vector<int> path;
            for (int e : {a, b, c}) if (e >= 0) path.push_back(e);
            path.insert(path.end(), pp.begin(), pp.end());
            end_forms.insert(to_rotation_free(path));
        }
        literal_end = to_rotation_free(pp);
        printf("end padding can read %zu different ways in the rotation-free text (literal: %s)\n", end_forms.size(), literal_end.c_str());
    }
    long long back_literal = 0, merged_scrambles = 0, merged_moves = 0;
    long long n = 0, fail = 0, flagged = 0, bad_pad = 0, bad_replay = 0, runs_checked = 0;
    long long total_moves = 0;
    long long by_len[8] = {0};   // flagged runs by run length (capped)
    long long flagged_wide = 0, flagged_face_only = 0, flagged_adjacent_same_family = 0;
    double total_ms = 0; long long total_nodes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (n < max_n && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < minutes * 60.0) {
        const uint64_t seed = gen();
        Result r = run("", pad, pad, safe, true, seed);
        n++;
        if (!r.ok) { fail++; log << "FAIL seed=" << seed << " : " << r.error << "\n"; log.flush(); printf("run %lld failed: %s\n", n, r.error.c_str()); continue; }
        total_moves += r.moves; total_ms += r.ms; total_nodes += r.nodes;
        if (r.merged > 0) { merged_scrambles++; merged_moves += r.merged; log << "MERGED seed=" << seed << " removed=" << r.merged << " : " << r.text << "\n"; log.flush(); printf("MERGED (run %lld): the join reduction removed %d move(s)\n", n, r.merged); }
        const std::vector<std::string> tok = split(r.text);
        // the padding: the start is literal; the end is the padding in the frame of the hidden rotations the rotation-free text translates out
        // (every move after a rotation is renamed), so it is checked as the padding under some whole-cube rotation
        bool front_ok = tok.size() >= 2 * padtok.size();
        for (size_t i = 0; front_ok && i < padtok.size(); i++) if (tok[i] != padtok[i]) front_ok = false;
        int back_kind = 0;   // 0 invalid, 1 literal, 2 under a rotation
        if (tok.size() >= 2 * padtok.size()) {
            std::string tail;
            for (size_t i = tok.size() - padtok.size(); i < tok.size(); i++) { if (!tail.empty()) tail += " "; tail += tok[i]; }
            if (tail == literal_end) back_kind = 1;
            else if (end_forms.count(tail)) back_kind = 2;
        }
        const bool pad_ok = front_ok && back_kind != 0;
        if (back_kind == 1) back_literal++;
        if (!pad_ok) { bad_pad++; log << "BADPAD seed=" << seed << " front_ok=" << front_ok << " back_kind=" << back_kind << " : " << r.text << "\n"; log.flush(); }
        // runs
        std::vector<Tok> toks(tok.size());
        bool parsed = true;
        for (size_t i = 0; i < tok.size(); i++) if (!parse_tok(tok[i], toks[i])) { parsed = false; log << "UNPARSED seed=" << seed << " token " << tok[i] << "\n"; }
        if (!parsed) { flagged++; continue; }
        bool any = false;
        for (size_t i = 0; i < toks.size();) {
            size_t j = i;
            while (j < toks.size() && toks[j].axis == toks[i].axis) j++;
            std::vector<Tok> run_(toks.begin() + i, toks.begin() + j);
            runs_checked++;
            std::vector<std::string> rewrite;
            const int fw = fewest(run_, &rewrite);
            if ((int)run_.size() > fw) {
                any = true;
                // replay: printed scramble vs the scramble with the run rewritten
                std::string t2;
                for (size_t k = 0; k < tok.size(); k++) {
                    if (k == i) { for (auto& s : rewrite) { if (!t2.empty()) t2 += " "; t2 += s; } }
                    if (k >= i && k < j) continue;
                    if (!t2.empty()) t2 += " ";
                    t2 += tok[k];
                }
                std::string na, nb; normalize_alg(r.text, na); normalize_alg(t2, nb);
                std::vector<int> pa, pb; parse_alg(na, pa); parse_alg(nb, pb);
                const bool same = equal_mod_rotation(apply_text(Cube(), pa), apply_text(Cube(), pb), true);
                if (!same) bad_replay++;
                std::string runtext;
                for (size_t k = i; k < j; k++) { if (k > i) runtext += " "; runtext += tok[k]; }
                bool wide = false; for (auto& t : run_) if (t.kind == 1 || t.kind == 3) wide = true;
                if (wide) flagged_wide++; else flagged_face_only++;
                by_len[std::min<size_t>(run_.size(), 7)]++;
                log << "REDUNDANT seed=" << seed << " moves=" << r.moves << " at " << (i + 1) << ".." << j << " of " << tok.size() << " : [" << runtext << "] -> ["
                    << (rewrite.empty() ? std::string("nothing") : [&] { std::string s; for (size_t q = 0; q < rewrite.size(); q++) { if (q) s += " "; s += rewrite[q]; } return s; }())
                    << "] replay_equal=" << (same ? "yes" : "NO") << "\n   " << r.text << "\n";
                log.flush();
                printf("REDUNDANT (run %lld): [%s] at %zu..%zu of %zu, replay %s\n", n, runtext.c_str(), i + 1, j, tok.size(), same ? "equal" : "NOT equal");
            }
            i = j;
        }
        if (any) flagged++;
        if (n % 100 == 0) {
            printf("[%.1f min] %lld scrambles, avg %.2f moves, avg %.0f ms (solver), flagged %lld, failed %lld\n",
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 60.0, n, (double)total_moves / (n - fail), total_ms / (n - fail), flagged, fail);
        }
    }
    const double mins = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 60.0;
    printf("RESULT: %lld scrambles in %.1f min (%lld failed); avg %.3f moves, avg solver time %.0f ms, avg nodes %.0f; runs checked %lld\n",
           n, mins, fail, (double)total_moves / std::max<long long>(1, n - fail), total_ms / std::max<long long>(1, n - fail), (double)total_nodes / std::max<long long>(1, n - fail), runs_checked);
    printf("        scrambles with a redundant same-axis run: %lld (wide involved %lld, face moves only %lld); bad padding %lld (end padding printed literally in %lld, in the rest only under a whole-cube rotation); rewrite NOT equal by replay %lld\n",
           flagged, flagged_wide, flagged_face_only, bad_pad, back_literal, bad_replay);
    printf("        flagged runs by length: 2:%lld 3:%lld 4:%lld 5+:%lld\n", by_len[2], by_len[3], by_len[4], by_len[5] + by_len[6] + by_len[7]);
    printf("        scrambles the join reduction shortened: %lld (%lld moves removed)\n", merged_scrambles, merged_moves);
    log << "SUMMARY scrambles=" << n << " failed=" << fail << " flagged=" << flagged << " badpad=" << bad_pad << " badreplay=" << bad_replay << "\n";
    return 0;
}
