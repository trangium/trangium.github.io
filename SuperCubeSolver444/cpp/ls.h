// "Leave slice" (LS): the S5 -> LS -> solved ending (opt-in, --ls).
//
// LS = < U'D, R2 U'D B2 U D', B2 U'D L2 U D', L2 U'D F2 U D', F2 U'D R2 U D' >, a group of order 384 inside S5 (S5 = <U,D,R2,L2,F2,B2>).
// U'D turns the two OUTER layers the same way and leaves the middle slice alone; the other generators move only the middle
// slice (E: the 4 equatorial dedges and the L/R/F/B centers). A state of S5 is therefore split into
//   UD part: corners, the 8 U/D-layer dedges, the U and D centers   (3.25e9 states up to U'D)
//   E part : the equatorial dedges and the L/R/F/B centers          (768 states with the E-slice turn)
// and it is in LS iff its UD part is a power of U'D away from solved (the same power for all three kinds of piece).
//
// Solving. Phase A (S5 -> LS) searches the UD part only, with the moves U, U', U2, R2, L2, F2, B2 (D is never needed: up to the
// E slice and a whole-cube rotation, D = U y'). Its heuristic is max(Bloom endtable, corner table). Phase B (LS -> solved)
// then REPLACES the found word X by an equivalent word Y: every U turn may be widened to a turn of the top three layers
// ("D y": D followed by the whole-cube rotation y, which acts on the UD part exactly like U but also turns the E slice).
// A stack holds the inverse of X and every letter of Y is pushed on it with adjacent cancellations; when the stack is empty
// the UD part is solved (up to y^k), and the cube is solved iff the E part is too. The y rotations of Y are summed and one
// closing rotation makes the total 0 (the solution then solves the cube literally).
//
// Phase A works on canonical coordinates: the pieces are relabeled by (U'D)^j (a LEFT multiplication, which commutes with the
// moves appended on the right) so that the U center is solved; distances are then identical for all four powers.
#pragma once
#include <functional>
#include "niss.h"

namespace ls {

static bool g_built = false;
static int g_entries = 1000000;      // target size of the Bloom endtable (entries): the depth whose total is closest
static int g_force_depth = -1;       // >= 0: exact layers up to this distance instead (--ls-depth)
static double g_endtable_ms = 0;
static double g_fpr = 1e-3;
static double g_build_ms = 0;
static long long g_nested_nodes = 0;   // nodes of the LS -> solved (phase B) search
static long long g_terminals = 0;       // LS -> solved searches started (one per word X that reached LS)

// letters of phase A (first NLS), then D (needed to apply U'D)
static const int NMV = 10;
static const int NLS = 7;
static const int MV[NMV] = {0, 1, 2, 10, 16, 19, 25, 6, 7, 8};   // U U2 U' R2 L2 F2 B2 D D2 D'
static const int UINC[NMV] = {1, 2, 3, 0, 0, 0, 0, 0, 0, 0};     // quarter turns of the U center
static const int DINC[NMV] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3};     // quarter turns of the D center (D itself)
static int FULL_TO_LS[NUM_MOVES];

static std::vector<uint16_t> TRC, TRE;   // [rank * NMV + letter]: corner / dedge permutation transitions
static std::vector<uint16_t> ROTC, ROTE; // [rank * 4 + j]: the pieces relabeled by (U'D)^j
static int ID_RANK = 0;
static std::vector<uint8_t> CD;          // corner distance to the nearest power of U'D (255: unreached)
static int LAYER_DEST[NMV][8];
static int UCEN[4], DCEN[4];             // slot of the first U / D center piece after r quarter turns

// ---------------------------------------------------------------- the UD part
using State = LsState;   // {corner rank, dedge rank, U center turns, D center turns}

static inline bool same(const State& a, const State& b) { return a.c == b.c && a.e == b.e && a.u == b.u && a.d == b.d; }

// The UD part of a full-cube state; false if some piece is not where an S5 state keeps it.
static bool extract_state(const State6& s, State& out) {
    out.c = (uint16_t)coords::rank_perm8(coords::corner_pos_from_sticker(s.corner_sticker));
    std::array<uint8_t,8> e;
    for (int k = 0; k < 8; k++) {
        const int piece = POS_SLOTS[coords::LAYER_IDX[k]];
        const int pi = coords::POS_INDEX[s.wing_slot[piece]];
        const int loc = pi >= 0 ? coords::LAYER_VALUE_LOCAL[pi] : -1;
        if (loc < 0) return false;
        e[k] = (uint8_t)loc;
    }
    out.e = (uint16_t)coords::rank_perm8(e);
    int u = -1, d = -1;
    for (int r = 0; r < 4; r++) { if (UCEN[r] == s.center_slot[10]) u = r; if (DCEN[r] == s.center_slot[4]) d = r; }
    if (u < 0 || d < 0) return false;
    out.u = (uint8_t)u; out.d = (uint8_t)d;
    return true;
}

static inline State apply(const State& t, int m) {
    const int mi = FULL_TO_LS[m];
    State r;
    r.c = TRC[(size_t)t.c * NMV + mi];
    r.e = TRE[(size_t)t.e * NMV + mi];
    r.u = (uint8_t)((t.u + UINC[mi]) & 3);
    r.d = (uint8_t)((t.d + DINC[mi]) & 3);
    return r;
}

// canonical key: the pieces relabeled by (U'D)^u (so the U center is solved) and the invariant u + d
static inline uint64_t key_of(const State& t) {
    const int j = t.u;
    return ((uint64_t)ROTC[(size_t)t.c * 4 + j] * 40320 + ROTE[(size_t)t.e * 4 + j]) * 4 + ((t.u + t.d) & 3);
}
static inline bool is_member(const State& t) {
    const int j = t.u;
    return ROTC[(size_t)t.c * 4 + j] == ID_RANK && ROTE[(size_t)t.e * 4 + j] == ID_RANK && ((t.u + t.d) & 3) == 0;
}

// Canonical form kept through the whole search: the relabeling by (U'D)^u is fused into the transition tables
// (TRCc / TREc: the transition of a canonical state, already relabeled), so a node costs two table reads, no
// canonicalization, and the state is {corner rank, dedge rank, 0, u + d}. The search (phase A) works on this form.
static std::vector<uint16_t> TRCc, TREc;   // [rank * NMV + letter]
static inline State canonical(const State& t) {
    const int j = t.u;
    State r;
    r.c = ROTC[(size_t)t.c * 4 + j];
    r.e = ROTE[(size_t)t.e * 4 + j];
    r.u = 0;
    r.d = (uint8_t)((t.u + t.d) & 3);
    return r;
}
static inline State apply_c(const State& t, int m) {
    const int mi = FULL_TO_LS[m];
    State r;
    r.c = TRCc[(size_t)t.c * NMV + mi];
    r.e = TREc[(size_t)t.e * NMV + mi];
    r.u = 0;
    r.d = (uint8_t)((t.d + UINC[mi] + DINC[mi]) & 3);
    return r;
}
static inline bool is_member_c(const State& t) { return t.c == ID_RANK && t.e == ID_RANK && t.d == 0; }

// ---------------------------------------------------------------- endtable (Bloom filters over canonical states)
static std::vector<p1::BlockedBloom> BLOOM;   // [k] = every canonical state at distance <= k from LS
static std::vector<size_t> LAYER_SIZES;
static int g_depth = 0;

static void expand(const std::vector<uint64_t>& from, std::vector<uint64_t>& out) {
    for (uint64_t key : from) {
        const int dl = (int)(key & 3);
        const uint64_t t = key >> 2;
        const int e = (int)(t % 40320), c = (int)(t / 40320);
        for (int mi = 0; mi < NLS; mi++) {
            const int u1 = UINC[mi];
            const int c2 = ROTC[(size_t)TRC[(size_t)c * NMV + mi] * 4 + u1];
            const int e2 = ROTE[(size_t)TRE[(size_t)e * NMV + mi] * 4 + u1];
            out.push_back(((uint64_t)c2 * 40320 + e2) * 4 + ((dl + u1) & 3));
        }
    }
}

// How the endtable is looked up (--ls-lookup):
//   0 bloom-binary : the cumulative blocked Bloom filters, the first probe in the top one decides hit / miss, a hit then
//                    binary-searches the layers (up to ~4 more cache lines, one per filter)
//   1 bloom-linear : same filters, a hit walks down from the top (most entries lie in the last layers: ~1.3 more probes)
//   3 bloom-budget : (default) the search only needs to know whether h <= r, r = the budget left for the child, so ONE probe in
//                    the cumulative filter of distance <= r decides (a hit: expand, a miss: prune); r > depth needs no probe at
//                    all (the endtable's "farther than depth" is never above r). Exact values (bloom_dist) walk down linearly.
//   2 fingerprint  : ONE bucketed table of 12-bit fingerprint + 4-bit distance entries (a 64-byte bucket = 31 entries), a
//                    single cache line answers hit and miss alike; bucket overflows spill into an exact hash map
static int g_lookup = 3;
static int g_r = 0;   // the budget the next h_budget call is for (set by the search before it steps a tier-5 node)
static bool g_build_all_lookups = false;   // tests: build the Bloom filters and the fingerprint table
struct FpTable {
    std::vector<uint16_t> words;
    uint16_t* b = nullptr;
    uint64_t nb = 1;
    std::unordered_map<uint64_t, uint8_t> spill;
    size_t entries = 0, spilled = 0;
    static inline uint64_t mix(uint64_t x) {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }
    void init(size_t n, double load) {
        nb = std::max<uint64_t>(16, (uint64_t)std::ceil((double)n / (31.0 * load)));
        words.assign(nb * 32 + 32, 0);
        b = (uint16_t*)(((uintptr_t)words.data() + 63) & ~(uintptr_t)63);
        spill.clear(); entries = spilled = 0;
    }
    inline void locate(uint64_t key, uint64_t& bi, int& fp) const {
        const uint64_t g = mix(key);
        bi = ((g >> 32) * nb) >> 32;
        fp = (int)(g & 0xFFF);
        if (fp == 0) fp = 1;
    }
    void add(uint64_t key, int d) {
        uint64_t bi; int fp; locate(key, bi, fp);
        uint16_t* bk = b + bi * 32;
        entries++;
        for (int i = 0; i < 31; i++) if (bk[i] == 0) { bk[i] = (uint16_t)((fp << 4) | d); return; }
        bk[31] = 0xFFFF;
        spill[key] = (uint8_t)d;
        spilled++;
    }
    inline int lookup(uint64_t key, int far) const {
        uint64_t bi; int fp; locate(key, bi, fp);
        const uint16_t* bk = b + bi * 32;
        int best = far;
        for (int i = 0; i < 31; i++) { const int e = bk[i]; if ((e >> 4) == fp && (e & 15) < best) best = e & 15; }
        if (bk[31] == 0xFFFF) { auto it = spill.find(key); if (it != spill.end() && it->second < best) best = it->second; }
        return best;
    }
    size_t bytes() const { return nb * 64 + spill.size() * 24; }
};
static FpTable FPT;

static inline int bloom_dist(uint64_t key) {
    if (g_lookup == 2) return FPT.lookup(key, g_depth + 1);
    uint64_t bh; int pos[10];
    p1::BlockedBloom::positions(key, bh, pos, 10);
    if (!BLOOM[g_depth].test_positions(bh, pos)) return g_depth + 1;
    if (g_lookup != 0) {
        int d = g_depth;
        while (d > 0 && BLOOM[d - 1].test_positions(bh, pos)) d--;
        return d;
    }
    int lo = 0, hi = g_depth;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (BLOOM[mid].test_positions(bh, pos)) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static inline int heuristic(const State& t) {
    const int hc = CD[t.c];
    if (hc == 255) return 100;
    return std::max(hc, bloom_dist(key_of(t)));
}
// Budgeted h of a canonical state: any value <= r means "not pruned" (it is a valid lower bound), a value > r means
// h > r. At most one filter probe, none when r exceeds the endtable depth.
static inline int h_budget(const State& t, int r) {
    const int hc = CD[t.c];
    if (hc > r) return hc;
    if (r > g_depth) return hc;
    uint64_t bh; int pos[10];
    p1::BlockedBloom::positions(((uint64_t)t.c * 40320 + t.e) * 4 + t.d, bh, pos, 10);
    return BLOOM[r].test_positions(bh, pos) ? hc : r + 1;
}
static inline int heuristic_c(const State& t) {   // of a canonical state
    const int hc = CD[t.c];
    if (hc == 255) return 100;
    return std::max(hc, bloom_dist(((uint64_t)t.c * 40320 + t.e) * 4 + t.d));
}

// ---------------------------------------------------------------- the E part
static int ESLOT_W[8], ESLOT_C[16], SITE_OF_WING[24], SITE_OF_CENTER[24];
static int LET_PERM[7][24];                   // E-site permutations of: R2 L2 F2 B2, yq, yq^2, yq^3 (yq: the rotation turning like U)
static bool Y_FWD = true;                     // yq == ROTATE[3] (else INV[3])
static std::vector<int16_t> CAYLEY;           // [id * 7 + letter]
static std::unordered_map<std::string,int> EID;
static int E_GROUP_SIZE = 0;

static bool e_arr_of(const State6& s, std::array<uint8_t,24>& arr) {
    for (int i = 0; i < 8; i++) { const int site = SITE_OF_WING[s.wing_slot[ESLOT_W[i]]]; if (site < 0) return false; arr[i] = (uint8_t)site; }
    for (int i = 0; i < 16; i++) { const int site = SITE_OF_CENTER[s.center_slot[ESLOT_C[i]]]; if (site < 0) return false; arr[8 + i] = (uint8_t)(8 + site); }
    return true;
}
static inline std::string e_key(const std::array<uint8_t,24>& a) { return std::string((const char*)a.data(), 24); }

// ---------------------------------------------------------------- build
static void fatal(const char* msg) { printf("FATAL (ls): %s\n", msg); exit(1); }

static void build() {
    auto t0 = std::chrono::steady_clock::now();
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_LS[m] = -1;
    for (int mi = 0; mi < NMV; mi++) FULL_TO_LS[MV[mi]] = mi;

    // U / D centers: one piece's slot after r quarter turns
    UCEN[0] = 10; DCEN[0] = 4;   // (in these tables move 0 = U cycles slots 10 11 19 18, move 6 = D cycles 4 5 13 12)
    for (int r = 1; r < 4; r++) { UCEN[r] = CENTER_PERM[0][UCEN[r - 1]]; DCEN[r] = CENTER_PERM[6][DCEN[r - 1]]; }
    if (CENTER_PERM[0][UCEN[3]] != 10 || UCEN[1] == 10 || UCEN[2] == 10 || CENTER_PERM[6][DCEN[3]] != 4 || DCEN[1] == 4 || DCEN[2] == 4)
        fatal("the U / D center slots do not form 4-cycles");
    for (int mi = 3; mi < 7; mi++)
        for (int s : {4, 5, 12, 13, 10, 11, 18, 19})
            if (CENTER_PERM[MV[mi]][s] != s) fatal("a face half turn moves a U/D center");

    // dedge layer-position destinations
    for (int mi = 0; mi < NMV; mi++)
        for (int i = 0; i < 8; i++) {
            const int dst = WING_PERM[MV[mi]][POS_SLOTS[coords::LAYER_IDX[i]]];
            const int pi = coords::POS_INDEX[dst];
            const int loc = pi >= 0 ? coords::LAYER_VALUE_LOCAL[pi] : -1;
            if (loc < 0) fatal("a layer dedge leaves the layer positions");
            LAYER_DEST[mi][i] = loc;
        }

    TRC.assign((size_t)40320 * NMV, 0); TRE.assign((size_t)40320 * NMV, 0);
    for (int r = 0; r < 40320; r++) {
        const auto arr = coords::unrank_perm8(r);
        for (int mi = 0; mi < NMV; mi++) {
            TRC[(size_t)r * NMV + mi] = (uint16_t)coords::rank_perm8(coords::corner_pos_apply(arr, MV[mi]));
            std::array<uint8_t,8> out;
            for (int k = 0; k < 8; k++) out[k] = (uint8_t)LAYER_DEST[mi][arr[k]];
            TRE[(size_t)r * NMV + mi] = (uint16_t)coords::rank_perm8(out);
        }
    }
    { std::array<uint8_t,8> id; for (int k = 0; k < 8; k++) id[k] = (uint8_t)k; ID_RANK = coords::rank_perm8(id); }

    // rot = U' D: its piece -> position maps, and the relabelings by rot^j
    auto step_rot = [&](const std::vector<uint16_t>& tr, int r) { return (int)tr[(size_t)tr[(size_t)r * NMV + 2] * NMV + 7]; };
    const auto rho_c = coords::unrank_perm8(step_rot(TRC, ID_RANK));
    const auto rho_e = coords::unrank_perm8(step_rot(TRE, ID_RANK));
    ROTC.assign((size_t)40320 * 4, 0); ROTE.assign((size_t)40320 * 4, 0);
    for (int r = 0; r < 40320; r++) {
        std::array<uint8_t,8> cc = coords::unrank_perm8(r), ee = cc;
        for (int j = 0; j < 4; j++) {
            ROTC[(size_t)r * 4 + j] = (uint16_t)coords::rank_perm8(cc);
            ROTE[(size_t)r * 4 + j] = (uint16_t)coords::rank_perm8(ee);
            std::array<uint8_t,8> nc, ne;
            for (int k = 0; k < 8; k++) { nc[k] = cc[rho_c[k]]; ne[k] = ee[rho_e[k]]; }
            cc = nc; ee = ne;
        }
    }

    TRCc.assign((size_t)40320 * NMV, 0); TREc.assign((size_t)40320 * NMV, 0);
    for (int r = 0; r < 40320; r++)
        for (int mi = 0; mi < NMV; mi++) {
            TRCc[(size_t)r * NMV + mi] = ROTC[(size_t)TRC[(size_t)r * NMV + mi] * 4 + UINC[mi]];
            TREc[(size_t)r * NMV + mi] = ROTE[(size_t)TRE[(size_t)r * NMV + mi] * 4 + UINC[mi]];
        }

    // corner distance to the nearest power of U'D (the four goals), over the seven letters
    CD.assign(40320, 255);
    std::vector<int> frontier;
    { int r = ID_RANK; for (int j = 0; j < 4; j++) { if (CD[r] == 255) { CD[r] = 0; frontier.push_back(r); } r = step_rot(TRC, r); } }
    for (int dist = 1; !frontier.empty(); dist++) {
        std::vector<int> next;
        for (int r : frontier)
            for (int mi = 0; mi < NLS; mi++) {
                const int n = TRC[(size_t)r * NMV + mi];
                if (CD[n] == 255) { CD[n] = (uint8_t)dist; next.push_back(n); }
            }
        frontier.swap(next);
    }

    // endtable: exact layers around the canonical solved state, cut where the total is closest to g_entries
    std::vector<std::vector<uint64_t>> layers(1);
    layers[0].push_back(((uint64_t)ID_RANK * 40320 + ID_RANK) * 4);
    size_t cum = 1;
    const auto t_end = std::chrono::steady_clock::now();
    // With a forced depth the last layer is never needed exactly: its raw neighbour keys (duplicates and lower-layer states
    // included, harmless in a Bloom filter) are streamed straight into its filter and its size is estimated from the duplicate
    // ratio of the layer before (like p2j); the other layers are sorted with the radix sort.
    const bool stream_last = g_force_depth >= 1 && g_lookup != 2 && !g_build_all_lookups;
    std::vector<uint64_t> last_raw;
    size_t last_estimate = 0;
    double dup_ratio = 4.0;
    while (true) {
        if (g_force_depth >= 0 && (int)layers.size() - 1 >= g_force_depth) break;
        std::vector<uint64_t> next;
        expand(layers.back(), next);
        if (stream_last && (int)layers.size() == g_force_depth) {
            last_estimate = (size_t)((double)next.size() / dup_ratio * 1.08);
            last_raw.swap(next);
            break;
        }
        const size_t raw = next.size();
        p1::sort_keys(next);
        next.erase(std::unique(next.begin(), next.end()), next.end());
        dup_ratio = (double)raw / (double)next.size();
        for (int e = (int)layers.size() - 1; e >= std::max(0, (int)layers.size() - 2); e--) {
            std::vector<uint64_t> diff;
            std::set_difference(next.begin(), next.end(), layers[e].begin(), layers[e].end(), std::back_inserter(diff));
            next.swap(diff);
        }
        const double with = (double)(cum + next.size()), without = (double)cum;
        if (next.empty() || (g_force_depth < 0 && std::fabs(with - g_entries) >= std::fabs(without - g_entries))) break;
        cum += next.size();
        layers.push_back(std::move(next));
    }
    const bool raw_last = !last_raw.empty();
    g_depth = (int)layers.size() - 1 + (raw_last ? 1 : 0);
    LAYER_SIZES.clear();
    for (auto& l : layers) LAYER_SIZES.push_back(l.size());
    if (raw_last) LAYER_SIZES.push_back(last_estimate);   // an estimate
    if (g_lookup != 2 || g_build_all_lookups) {
        BLOOM.assign(g_depth + 1, p1::BlockedBloom());
        size_t c2 = 0;
        for (int d = 0; d <= g_depth; d++) { c2 += LAYER_SIZES[d]; BLOOM[d].init(c2, g_fpr); }
        for (int e = 0; e < (int)layers.size(); e++)
            for (int d = e; d <= g_depth; d++) BLOOM[d].add_all(layers[e]);
        if (raw_last) BLOOM[g_depth].add_all(last_raw);
    }
    if (g_lookup == 2 || g_build_all_lookups) {
        size_t total = 0;
        for (size_t n : LAYER_SIZES) total += n;
        FPT.init(total, 0.7);
        for (int e = 0; e <= g_depth; e++) for (uint64_t k : layers[e]) FPT.add(k, e);
    }
    g_endtable_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_end).count();

    // E part: sites = the 8 equatorial wing slots + the 16 L/R/F/B center slots
    for (int i = 0; i < 24; i++) { SITE_OF_WING[i] = -1; SITE_OF_CENTER[i] = -1; }
    { int n = 0;
      for (int j = 0; j < 4; j++) {
          const int idx = coords::EQUATORIAL_IDX[j];
          ESLOT_W[n] = POS_SLOTS[idx]; SITE_OF_WING[POS_SLOTS[idx]] = n++;
          ESLOT_W[n] = NEG_OF[idx]; SITE_OF_WING[NEG_OF[idx]] = n++;
      }
      if (n != 8) fatal("expected 8 equatorial wing slots");
      bool is_ud[24] = {false};
      for (int i = 0; i < 8; i++) is_ud[UD_TARGET[i]] = true;
      n = 0;
      for (int s = 0; s < 24; s++) if (!is_ud[s]) { ESLOT_C[n] = s; SITE_OF_CENTER[s] = n++; }
      if (n != 16) fatal("expected 16 E-slice centers"); }
    // yq: the rotation that turns like U
    {
        bool fwd = true, inv = true;
        for (int s : {10, 11, 18, 19}) {
            fwd = fwd && ROTATE_CENTER[3][s] == CENTER_PERM[0][s];
            inv = inv && ROTATE_CENTER_INV[3][s] == CENTER_PERM[0][s];
        }
        if (!fwd && !inv) fatal("class 3 is not a rotation about the U/D axis turning like U");
        Y_FWD = fwd;
    }
    auto yq_w = [&](int s) { return Y_FWD ? ROTATE_WING[3][s] : ROTATE_WING_INV[3][s]; };
    auto yq_c = [&](int s) { return Y_FWD ? ROTATE_CENTER[3][s] : ROTATE_CENTER_INV[3][s]; };
    for (int l = 0; l < 7; l++) {
        for (int i = 0; i < 8; i++) {
            int dst = ESLOT_W[i];
            if (l < 4) dst = WING_PERM[MV[3 + l]][dst]; else for (int r = 0; r < l - 3; r++) dst = yq_w(dst);
            if (SITE_OF_WING[dst] < 0) fatal("an E-part letter moves an equatorial wing out of the E slice");
            LET_PERM[l][i] = SITE_OF_WING[dst];
        }
        for (int i = 0; i < 16; i++) {
            int dst = ESLOT_C[i];
            if (l < 4) dst = CENTER_PERM[MV[3 + l]][dst]; else for (int r = 0; r < l - 3; r++) dst = yq_c(dst);
            if (SITE_OF_CENTER[dst] < 0) fatal("an E-part letter moves a center out of the E slice");
            LET_PERM[l][8 + i] = 8 + SITE_OF_CENTER[dst];
        }
    }
    // class 7 (y2) must be yq^2
    for (int s = 0; s < 24; s++) {
        int d1 = yq_c(yq_c(s));
        if (ROTATE_CENTER[7][s] != d1) fatal("class 7 is not the square of the U/D-axis quarter rotation");
    }
    // the E-part group generated by the 7 letters
    {
        std::array<uint8_t,24> id; for (int i = 0; i < 24; i++) id[i] = (uint8_t)i;
        std::vector<std::array<uint8_t,24>> elems = {id};
        EID.clear(); EID[e_key(id)] = 0;
        for (size_t qi = 0; qi < elems.size(); qi++)
            for (int l = 0; l < 7; l++) {
                std::array<uint8_t,24> n;
                for (int i = 0; i < 24; i++) n[i] = (uint8_t)LET_PERM[l][elems[qi][i]];
                const std::string k = e_key(n);
                if (!EID.count(k)) { EID[k] = (int)elems.size(); elems.push_back(n); }
            }
        E_GROUP_SIZE = (int)elems.size();
        CAYLEY.assign(elems.size() * 7, 0);
        for (size_t i = 0; i < elems.size(); i++)
            for (int l = 0; l < 7; l++) {
                std::array<uint8_t,24> n;
                for (int k = 0; k < 24; k++) n[k] = (uint8_t)LET_PERM[l][elems[i][k]];
                CAYLEY[i * 7 + l] = (int16_t)EID[e_key(n)];
            }
    }
    g_built = true;
    g_build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static void print_info() {
    size_t bytes = 0;
    for (auto& b : BLOOM) bytes += b.bytes();
    if (g_lookup == 2 || g_build_all_lookups) bytes = FPT.bytes();
    printf("LS: endtable (%s) exact up to distance %d (layer sizes", g_lookup == 3 ? "Bloom filters, one budgeted probe" : g_lookup == 2 ? "fingerprint table" : g_lookup == 1 ? "Bloom filters, linear hit walk" : "Bloom filters, binary hit search", g_depth);
    size_t tot = 0;
    for (size_t n : LAYER_SIZES) { printf(" %zu", n); tot += n; }
    printf("; %zu entries, %.2f MB), corner table up to distance ", tot, bytes / 1e6);
    int mx = 0; long long unreached = 0;
    for (uint8_t v : CD) { if (v == 255) unreached++; else mx = std::max(mx, (int)v); }
    printf("%d (%lld unreached), E-part group of %d elements, built in %.0f ms (endtable %.0f ms)\n", mx, unreached, E_GROUP_SIZE, g_build_ms, g_endtable_ms);
}

// ---------------------------------------------------------------- phase B: LS -> solved
// Letters of Y: 0 R2, 1 L2, 2 F2, 3 B2, 4 U, 5 U', 6 U2, 7 (D y), 8 (D' y'), 9 (D2 y2).
struct YLetter { const char* name; int face, pw, eletter, dsum, group, rank; };
static const YLetter YL[10] = {
    {"R2", 1, 2, 0, 0, 0, 0}, {"L2", 2, 2, 1, 0, 0, 2}, {"F2", 3, 2, 2, 0, 2, 0}, {"B2", 4, 2, 3, 0, 2, 2},
    {"U", 0, 1, -1, 0, 1, 0}, {"U'", 0, 3, -1, 0, 1, 0}, {"U2", 0, 2, -1, 0, 1, 0},
    {"D y", 0, 1, 4, 1, 1, 1}, {"D' y'", 0, 3, 6, -1, 1, 1}, {"D2 y2", 0, 2, 5, 2, 1, 1},
};
// the previous-move guard (as in every other phase): same axis only with a strictly higher rank, so R2 R2, L2 R2 and
// (D' y') U are never tried; of two letters on the U axis only "plain, then widened" is allowed

static inline int stack_push(uint8_t* st, int sp, int face, int pw) {
    if (sp > 0 && (st[sp - 1] >> 2) == face) {
        const int q = ((st[sp - 1] & 3) + pw) & 3;
        sp--;
        if (q) st[sp++] = (uint8_t)((face << 2) | q);
        return sp;
    }
    st[sp++] = (uint8_t)((face << 2) | pw);
    return sp;
}

// whole-cube rotation yq^a as close sentinels (class 3 = y', class 7 = y2 in position terms): the path letters
static inline void yq_entries(int a, std::vector<int>& out) {
    a &= 3;
    const int c3 = ROTATION_SENTINEL_BASE + 3, c7 = ROTATION_SENTINEL_BASE + 7;
    if (a == 0) return;
    if (a == 2) { out.push_back(c7); return; }
    // yq^1 / yq^3 are one class-3 letter or class-7 + class-3, depending on which of them yq is
    const bool one = Y_FWD ? (a == 3) : (a == 1);
    if (one) out.push_back(c3); else { out.push_back(c7); out.push_back(c3); }
}
static void letter_entries(int l, std::vector<int>& out) {
    switch (l) {
        case 0: out.push_back(10); break;
        case 1: out.push_back(16); break;
        case 2: out.push_back(19); break;
        case 3: out.push_back(25); break;
        case 4: out.push_back(0); break;
        case 5: out.push_back(2); break;
        case 6: out.push_back(1); break;
        case 7: out.push_back(6); yq_entries(1, out); break;
        case 8: out.push_back(8); yq_entries(3, out); break;
        default: out.push_back(7); yq_entries(2, out); break;
    }
}

struct YSearch {
    int u_ls = 0, eid0 = 0, bound = 0;
    std::vector<int> word;
    std::function<bool(const std::vector<int>&)> accept;
    long long nodes = 0;

    bool goal(int eid, int ysum) const {
        if (((ysum - u_ls) & 3) != 0) return false;
        const int close = (-ysum) & 3;
        const int e2 = close == 0 ? eid : CAYLEY[(size_t)eid * 7 + 3 + close];
        return e2 == 0;
    }
    bool rec(const uint8_t* st, int sp, int eid, int ysum, int len, int lg, int lr) {
        nodes++;
        if (sp == 0 && goal(eid, ysum) && accept(word)) return true;
        if (len == bound) return false;
        for (int l = 0; l < 10; l++) {
            const YLetter& y = YL[l];
            if (lg >= 0 && y.group == lg && y.rank <= lr) continue;
            uint8_t ns[64];
            std::memcpy(ns, st, (size_t)sp);
            const int nsp = stack_push(ns, sp, y.face, y.pw);
            if (nsp > bound - len - 1) continue;
            const int neid = y.eletter >= 0 ? CAYLEY[(size_t)eid * 7 + y.eletter] : eid;
            word.push_back(l);
            if (rec(ns, nsp, neid, ysum + y.dsum, len + 1, y.group, y.rank)) return true;
            word.pop_back();
        }
        return false;
    }
};

// Phase B for the full state `cur` (in LS) that the phase-A word X (raw moves) led to (cur = the state before X, then X). Tries budgets from the length of the
// reduced stack up to d; `accept` is called with every solution Y (as letter indices) found and may refuse it. Returns true
// when one was accepted (then `y_entries` holds the path entries: raw moves and rotation letters, the closing rotation
// included). Adds the nodes to g_nested_nodes.
template <class Accept>
static bool terminal(const State6& cur, const std::vector<int>& X, int d, std::vector<int>& y_entries, Accept&& accept) {
    g_terminals++;
    State t;
    if (!extract_state(cur, t)) return false;
    // Y replaces X, so it starts from the cube BEFORE X (cur with X undone; a pending NISS flip is already inside cur and
    // commutes with the right moves); the UD part is the one after X (a power of U'D)
    State6 base = cur;
    for (int i = (int)X.size() - 1; i >= 0; i--) base = apply_move(base, inverse_move(X[i]));
    std::array<uint8_t,24> arr;
    if (!e_arr_of(base, arr)) return false;
    auto it = EID.find(e_key(arr));
    if (it == EID.end()) return false;
    uint8_t st0[64];
    int sp0 = 0;
    for (int i = (int)X.size() - 1; i >= 0; i--) {
        const int m = X[i];
        if (m <= 2) sp0 = stack_push(st0, sp0, 0, (4 - (m == 0 ? 1 : m == 1 ? 2 : 3)) & 3);
        else sp0 = stack_push(st0, sp0, m == 10 ? 1 : m == 16 ? 2 : m == 19 ? 3 : 4, 2);
    }
    YSearch ys;
    ys.u_ls = t.u; ys.eid0 = it->second;
    ys.accept = [&](const std::vector<int>& w) {
        std::vector<int> entries;
        int ysum = 0;
        for (int l : w) { letter_entries(l, entries); ysum += YL[l].dsum; }
        yq_entries(-ysum, entries);
        if (!accept(entries)) return false;
        y_entries = entries;
        return true;
    };
    bool found = false;
    for (int bound = sp0; bound <= d && !found; bound++) {
        ys.bound = bound;
        ys.word.clear();
        found = ys.rec(st0, sp0, ys.eid0, 0, 0, -1, -1);
    }
    g_nested_nodes += ys.nodes;
    return found;
}

// ---------------------------------------------------------------- standalone S5 -> solved (tests, no NISS)
struct Solver {
    long long nodes = 0;
    std::vector<int> path;
    State6 start;
    std::vector<int> result;
    int T = 0;
    bool run(const State& t, int g, int last) {
        nodes++;
        if (is_member_c(t)) {
            State6 cur = start;
            for (int m : path) cur = apply_move(cur, m);
            std::vector<int> y;
            if (terminal(cur, path, T, y, [](const std::vector<int>&) { return true; })) { result = y; return true; }
        }
        if (g >= T) return false;
        for (int mi = 0; mi < NLS; mi++) {
            const int m = MV[mi];
            if (last >= 0 && MOVE_GROUP[m] == MOVE_GROUP[last] && MOVE_RANK[m] <= MOVE_RANK[last]) continue;
            State n = apply_c(t, m);
            if (g + 1 + (g_lookup == 3 ? h_budget(n, T - g - 1) : heuristic_c(n)) > T) continue;
            path.push_back(m);
            if (run(n, g + 1, m)) return true;
            path.pop_back();
        }
        return false;
    }
};
// S5 -> solved by IDA* (the LS pipeline only). Returns false if the state is not an S5 state of the expected form.
static bool solve_s5(const State6& s, int max_len, std::vector<int>& out, long long* nodes_out = nullptr) {
    Solver sv;
    sv.start = s;
    State t;
    if (!extract_state(s, t)) return false;
    t = canonical(t);
    for (sv.T = heuristic_c(t); sv.T <= max_len; sv.T++) {
        sv.path.clear();
        if (sv.run(t, 0, -1)) { out = sv.result; if (nodes_out) *nodes_out = sv.nodes + g_nested_nodes; return true; }
    }
    if (nodes_out) *nodes_out = sv.nodes + g_nested_nodes;
    return false;
}

} // namespace ls

// used by classify_tier / heuristic in LS mode (declared in state.h)
static bool ls_member_of(const State6& s) {
    ls::State t;
    return ls::extract_state(s, t) && ls::is_member(t);
}
static int ls_h_of(const State6& s) {
    ls::State t;
    if (!ls::extract_state(s, t)) return 100;
    return ls::heuristic(t);
}
