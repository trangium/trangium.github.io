#pragma once
#include "common.h"

namespace p2full {

// O(n) packed-nibble Lehmer rank (see pairing_dist_io.h's pairing_rank_full
// for the technique writeup) instead of the O(n^2) double loop.
static inline int rank_perm8(const std::array<uint8_t,8>& p) {
    uint64_t val = 0x76543210ULL;
    int r = 0;
    for (int i = 0; i < 8; i++) {
        int shift = ((int)p[i]) << 2;
        r = r * (8 - i) + (int)((val >> shift) & 0xF);
        val -= 0x11111110ULL << shift;
    }
    return r;
}

// The wing coset (phase 2) is a 24-bit mask: bit s is 1 iff slot s holds one of the 12 "positive" wings
// (WING_TARGET). A move permutes the bits, done with one lookup per byte of the mask, so there is no transition
// table. The distance table is indexed by the low 23 bits: the mask has exactly 12 ones, so the popcount of the
// low 23 bits says what the missing top bit was and the index is unique (2^23 entries for C(24,12) = 2,704,156
// states, ~3.1x sparse; the unused entries stay 255).
static uint32_t WING_STEP[NUM_MOVES][3][256];
static uint32_t WING_SOLVED_MASK;
static void init_wing_step() {
    for (int m = 0; m < NUM_MOVES; m++)
        for (int byte = 0; byte < 3; byte++)
            for (int v = 0; v < 256; v++) {
                uint32_t out = 0;
                for (int bit = 0; bit < 8; bit++)
                    if (v >> bit & 1) out |= 1u << WING_PERM[m][byte * 8 + bit];
                WING_STEP[m][byte][v] = out;
            }
    WING_SOLVED_MASK = 0;
    for (int i = 0; i < 12; i++) WING_SOLVED_MASK |= 1u << WING_TARGET[i];
}
static inline uint32_t wing_step(uint32_t mask, int m) {
    return WING_STEP[m][0][mask & 255] | WING_STEP[m][1][(mask >> 8) & 255] | WING_STEP[m][2][mask >> 16];
}
// the mask from the slot of every wing piece (works for std::array<int,24>, int[24], ...)
template <class A>
static inline uint32_t wing_mask_of(const A& wing_slot) {
    uint32_t mask = 0;
    for (int i = 0; i < 12; i++) mask |= 1u << wing_slot[WING_TARGET[i]];
    return mask;
}
static inline uint32_t wing_index(uint32_t mask) { return mask & 0x7FFFFF; }

struct WingTables { std::vector<uint8_t> dist; long long hist[256] = {0}; };   // [wing_index(mask)] = moves to the solved coset

// The coset space (C(24,12) = 2,704,156 states) is walked from the solved coset over all 27 moves. Distances are 0..8 with
// layer sizes 1 5 89 1441 22955 310270 1799686 569705 4: expanding the huge layer 6 (1.8M x 27 transitions) is what made
// this slow, so the table is built in three steps: top-down BFS over layers 0..5 (finds layer 6), then BOTTOM-UP for layer 7
// (each still-unlabeled coset looks for a neighbour in layer 6: ~570k x <= 27 instead of 1.8M x 27; the 27 moves are closed
// under inverses so neighbour = predecessor), and what is left after that is layer 8 (the maximum is fixed). The histogram
// is counted on the way, so the statistics printout needs no scan. test_wing_mask checks the table against the
// distance-field characterization.
static const int WING_TOP_DOWN_LAYERS = 5;      // layers 0..5 are expanded top-down
static const int WING_MAX_DIST = 8;
static void build_wing_distance_table(WingTables& W) {
    init_wing_step();
    W.dist.assign((size_t)1 << 23, 255);
    for (long long& h : W.hist) h = 0;
    W.dist[wing_index(WING_SOLVED_MASK)] = 0;
    W.hist[0] = 1;
    std::vector<uint32_t> frontier = {WING_SOLVED_MASK}, next;
    for (int d = 0; d <= WING_TOP_DOWN_LAYERS; d++) {
        next.clear();
        for (uint32_t mask : frontier)
            for (int m = 0; m < NUM_MOVES; m++) {
                const uint32_t nm = wing_step(mask, m);
                uint8_t& e = W.dist[wing_index(nm)];
                if (e == 255) { e = (uint8_t)(d + 1); next.push_back(nm); }
            }
        W.hist[d + 1] = (long long)next.size();
        frontier.swap(next);
    }
    // bottom-up: every unlabeled coset with a neighbour in layer 6 is in layer 7, the rest is layer 8
    const int L = WING_TOP_DOWN_LAYERS + 1;   // 6
    for (uint32_t i = 0; i < ((uint32_t)1 << 23); i++) {
        const int ones = __builtin_popcount(i);
        if ((ones != 11 && ones != 12) || W.dist[i] != 255) continue;
        const uint32_t mask = ones == 11 ? (i | (1u << 23)) : i;
        int d = WING_MAX_DIST;
        for (int m = 0; m < NUM_MOVES; m++)
            if (W.dist[wing_index(wing_step(mask, m))] == L) { d = L + 1; break; }
        W.dist[i] = (uint8_t)d;
        W.hist[d]++;
    }
}
static void print_wing_dist_stats(const WingTables& W) {
    long long n = 0, sum = 0;
    int mx = 0;
    for (int d = 0; d < 256; d++) if (W.hist[d]) { n += W.hist[d]; sum += (long long)d * W.hist[d]; mx = d; }
    printf("  wing coset dist table: %lld entries (index space 2^23 = %.1fx), unreached=%lld, avg=%.4f max=%d\n",
           n, (double)((size_t)1 << 23) / (double)n, 2704156LL - n, n ? (double)sum / (double)n : 0.0, mx);
    printf("    distance:");
    for (int d = 0; d <= mx; d++) printf(" %d:%lld", d, W.hist[d]);
    printf("\n");
}

struct FbTables { std::vector<uint8_t> dist; };

static int FB_LOCAL[24];

static std::array<uint8_t,8> apply_move_to_fb_perm(const std::array<uint8_t,8>& state, int m) {
    std::array<uint8_t,8> out;
    for (int i=0;i<8;i++) {
        int new_slot = CENTER_PERM[m][FB_TARGET[i]];
        out[FB_LOCAL[new_slot]] = state[i];
    }
    return out;
}
static void build_fb_distance_table(FbTables& F) {
    for (int i=0;i<24;i++) FB_LOCAL[i] = -1;
    for (int i=0;i<8;i++) FB_LOCAL[FB_TARGET[i]] = i;

    F.dist.assign(NUM_FB_PERMS, 255);
    std::queue<int> q;
    for (int i=0;i<96;i++) { F.dist[FB_SOLVED_INDICES[i]]=0; q.push(FB_SOLVED_INDICES[i]); }
    std::vector<std::array<uint8_t,8>> perm_by_idx(NUM_FB_PERMS);
    for (int i=0;i<96;i++) {
        int idx = FB_SOLVED_INDICES[i];
        // O(n) packed-nibble unrank (see pairing_dist_io.h's
        // pairing_unrank_full for the technique writeup), replacing the
        // O(n^2) array-shift version this used to be inline here.
        uint64_t val = 0x76543210ULL;
        std::array<uint8_t,8> perm;
        int r = idx;
        static const int FACT[8] = {5040,720,120,24,6,2,1,1};
        for (int k=0;k<8;k++) {
            int f = FACT[k];
            int sel = r / f;
            r %= f;
            int shift = sel << 2;
            perm[k] = (uint8_t)((val >> shift) & 0xF);
            uint64_t m = (shift == 0) ? 0ULL : ((1ULL << shift) - 1);
            val = (val & m) + ((val >> 4) & ~m);
        }
        perm_by_idx[idx] = perm;
    }
    while (!q.empty()) {
        int idx=q.front(); q.pop();
        const auto& state = perm_by_idx[idx];
        for (int pm=0; pm<P2_NUM_MOVES; pm++) {
            int m = P2_TO_FULL_MOVE_INDEX[pm];
            auto ns = apply_move_to_fb_perm(state, m);
            int nidx = rank_perm8(ns);
            if (F.dist[nidx]==255) { F.dist[nidx]=F.dist[idx]+1; perm_by_idx[nidx]=ns; q.push(nidx); }
        }
    }
}

static bool FB_IS_SOLVED[NUM_FB_PERMS];

} // namespace p2full
