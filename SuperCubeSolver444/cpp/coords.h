#pragma once
#include "phase2.h"

namespace coords {

// ---------------- move classification (needed again -- PLL parity is back)
static bool IS_QUARTER_TURN[NUM_MOVES];
static bool TOGGLES_RWUWFW[NUM_MOVES];  // Uw2(4), Rw2(13), Fw2(22) in FULL 27-move indexing
static void init_move_flags() {
    for (int m = 0; m < NUM_MOVES; m++) {
        std::string name = MOVE_NAMES[m];
        IS_QUARTER_TURN[m] = (name.back() != '2');
        TOGGLES_RWUWFW[m] = (m == 4 || m == 13 || m == 22);
    }
}

// ---------------- generic perm8 rank/unrank (same convention as p2full) ----
// O(n) packed-nibble Lehmer rank/unrank (see pairing_dist_io.h's
// pairing_rank_full/pairing_unrank_full for the technique writeup) instead
// of the O(n^2) double-loop rank / O(n^2) array-shift unrank this used to be.
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
static std::array<uint8_t,8> unrank_perm8(int r) {
    uint64_t val = 0x76543210ULL;
    std::array<uint8_t,8> perm;
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
    return perm;
}

// ---------------- UD 2520-coordinate (verbatim from chain1234v2.cpp -- needed
// again now that we go all the way to literal S4) --------------------------
static int16_t CENTER2520_UD[40320];
static int UD2520_REP_RAW[2520];

static void build_center2520_ud() {
    auto u_next = [](int v)->int {
        switch (v) { case 2: return 3; case 3: return 7; case 7: return 6; case 6: return 2; default: return v; }
    };
    auto d_next = [](int v)->int {
        switch (v) { case 0: return 4; case 4: return 5; case 5: return 1; case 1: return 0; default: return v; }
    };
    auto relabel = [&](const std::array<uint8_t,8>& p, int ku, int kd) {
        std::array<uint8_t,8> out;
        for (int i = 0; i < 8; i++) {
            int v = p[i];
            bool is_u = (v==2||v==3||v==6||v==7);
            if (is_u) { for (int t=0;t<ku;t++) v=u_next(v); }
            else      { for (int t=0;t<kd;t++) v=d_next(v); }
            out[i] = (uint8_t)v;
        }
        return out;
    };
    for (int raw = 0; raw < 40320; raw++) CENTER2520_UD[raw] = -1;
    int next_id = 0;
    for (int raw = 0; raw < 40320; raw++) {
        auto p = unrank_perm8(raw);
        std::array<uint8_t,8> best; bool have_best=false;
        for (int ku=0; ku<4; ku++) for (int kd=0; kd<4; kd++) {
            auto cand = relabel(p, ku, kd);
            if (!have_best || cand < best) { best = cand; have_best = true; }
        }
        int canon_raw = rank_perm8(best);
        if (CENTER2520_UD[canon_raw] == -1) {
            CENTER2520_UD[canon_raw] = (int16_t)next_id;
            UD2520_REP_RAW[next_id] = canon_raw;
            next_id++;
        }
        CENTER2520_UD[raw] = CENTER2520_UD[canon_raw];
    }
}

// ---------------- LR 2520-coordinate (verbatim from chain1234v2.cpp) -------
static int16_t CENTER2520_LR[40320];
static int LR2520_REP_RAW[2520];

static void build_center2520_lr() {
    auto l_next = [](int v)->int {
        switch (v) { case 0: return 1; case 1: return 3; case 3: return 2; case 2: return 0; default: return v; }
    };
    auto r_next = [](int v)->int {
        switch (v) { case 4: return 6; case 6: return 7; case 7: return 5; case 5: return 4; default: return v; }
    };
    auto relabel = [&](const std::array<uint8_t,8>& p, int kl, int kr) {
        std::array<uint8_t,8> out;
        for (int i = 0; i < 8; i++) {
            int v = p[i];
            bool is_l = (v==0||v==1||v==2||v==3);
            if (is_l) { for (int t=0;t<kl;t++) v=l_next(v); }
            else      { for (int t=0;t<kr;t++) v=r_next(v); }
            out[i] = (uint8_t)v;
        }
        return out;
    };
    for (int raw = 0; raw < 40320; raw++) CENTER2520_LR[raw] = -1;
    int next_id = 0;
    for (int raw = 0; raw < 40320; raw++) {
        auto p = unrank_perm8(raw);
        std::array<uint8_t,8> best; bool have_best=false;
        for (int kl=0; kl<4; kl++) for (int kr=0; kr<4; kr++) {
            auto cand = relabel(p, kl, kr);
            if (!have_best || cand < best) { best = cand; have_best = true; }
        }
        int canon_raw = rank_perm8(best);
        if (CENTER2520_LR[canon_raw] == -1) {
            CENTER2520_LR[canon_raw] = (int16_t)next_id;
            LR2520_REP_RAW[next_id] = canon_raw;
            next_id++;
        }
        CENTER2520_LR[raw] = CENTER2520_LR[canon_raw];
    }
}

// ---------------- FB 24-coordinate (verbatim from chain1234v2.cpp) ---------
static int8_t FB24_FROM_FB96RANK[96];
static int FB24_REP_RAW[24];
static int FB96_RANK_OF[NUM_FB_PERMS];

static void build_fb24() {
    for (int i = 0; i < NUM_FB_PERMS; i++) FB96_RANK_OF[i] = -1;
    for (int i = 0; i < 96; i++) FB96_RANK_OF[FB_SOLVED_INDICES[i]] = i;

    auto even_next = [](int v)->int {
        switch (v) { case 0: return 6; case 6: return 0; case 2: return 4; case 4: return 2; default: return v; }
    };
    auto odd_next = [](int v)->int {
        switch (v) { case 1: return 7; case 7: return 1; case 3: return 5; case 5: return 3; default: return v; }
    };
    auto relabel = [&](const std::array<uint8_t,8>& p, int ke, int ko) {
        std::array<uint8_t,8> out;
        for (int i = 0; i < 8; i++) {
            int v = p[i];
            if (v % 2 == 0) { for (int t=0;t<ke;t++) v=even_next(v); }
            else            { for (int t=0;t<ko;t++) v=odd_next(v); }
            out[i] = (uint8_t)v;
        }
        return out;
    };
    for (int i = 0; i < 96; i++) FB24_FROM_FB96RANK[i] = -1;
    int next_id = 0;
    for (int i = 0; i < 96; i++) {
        int raw = FB_SOLVED_INDICES[i];
        auto p = unrank_perm8(raw);
        std::array<uint8_t,8> best; bool have_best=false;
        for (int ke=0; ke<2; ke++) for (int ko=0; ko<2; ko++) {
            auto cand = relabel(p, ke, ko);
            if (!have_best || cand < best) { best = cand; have_best = true; }
        }
        int canon_raw = rank_perm8(best);
        int canon_96rank = FB96_RANK_OF[canon_raw];
        if (FB24_FROM_FB96RANK[canon_96rank] == -1) {
            FB24_FROM_FB96RANK[canon_96rank] = (int8_t)next_id;
            FB24_REP_RAW[next_id] = canon_raw;
            next_id++;
        }
        FB24_FROM_FB96RANK[i] = FB24_FROM_FB96RANK[canon_96rank];
    }
}

// ---------------- center perm application -----------------------------------
static int UD_LOCAL[24], LR_LOCAL[24], FB_LOCAL2[24];
static void init_center_locals() {
    for (int i=0;i<24;i++) { UD_LOCAL[i]=-1; LR_LOCAL[i]=-1; FB_LOCAL2[i]=-1; }
    for (int i=0;i<8;i++) { UD_LOCAL[UD_TARGET[i]]=i; LR_LOCAL[LR_TARGET[i]]=i; FB_LOCAL2[FB_TARGET[i]]=i; }
}
static std::array<uint8_t,8> apply_move_to_ud_perm(const std::array<uint8_t,8>& s, int m) {
    std::array<uint8_t,8> out;
    for (int i=0;i<8;i++) { int ns=CENTER_PERM[m][UD_TARGET[i]]; out[UD_LOCAL[ns]]=s[i]; }
    return out;
}
static std::array<uint8_t,8> apply_move_to_lr_perm(const std::array<uint8_t,8>& s, int m) {
    std::array<uint8_t,8> out;
    for (int i=0;i<8;i++) { int ns=CENTER_PERM[m][LR_TARGET[i]]; out[LR_LOCAL[ns]]=s[i]; }
    return out;
}
static std::array<uint8_t,8> apply_move_to_fb_perm(const std::array<uint8_t,8>& s, int m) {
    std::array<uint8_t,8> out;
    for (int i=0;i<8;i++) { int ns=CENTER_PERM[m][FB_TARGET[i]]; out[FB_LOCAL2[ns]]=s[i]; }
    return out;
}

// ---------------- small reduced-coordinate transition tables ---------------
static int16_t UD2520_TRANS[2520][P2_NUM_MOVES];
static int16_t LR2520_TRANS[2520][P2_NUM_MOVES];
static int8_t  FB24_TRANS[24][P2_NUM_MOVES];

static void build_reduced_transitions() {
    for (int c = 0; c < 2520; c++) {
        auto p = unrank_perm8(UD2520_REP_RAW[c]);
        for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
            int m = P2_TO_FULL_MOVE_INDEX[pm];
            auto np = apply_move_to_ud_perm(p, m);
            UD2520_TRANS[c][pm] = CENTER2520_UD[rank_perm8(np)];
        }
    }
    for (int c = 0; c < 2520; c++) {
        auto p = unrank_perm8(LR2520_REP_RAW[c]);
        for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
            int m = P2_TO_FULL_MOVE_INDEX[pm];
            auto np = apply_move_to_lr_perm(p, m);
            LR2520_TRANS[c][pm] = CENTER2520_LR[rank_perm8(np)];
        }
    }
    for (int c = 0; c < 24; c++) {
        auto p = unrank_perm8(FB24_REP_RAW[c]);
        for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
            int m = P2_TO_FULL_MOVE_INDEX[pm];
            auto np = apply_move_to_fb_perm(p, m);
            int raw96 = FB96_RANK_OF[rank_perm8(np)];
            FB24_TRANS[c][pm] = (raw96 < 0) ? -1 : FB24_FROM_FB96RANK[raw96];
        }
    }
}

// Distance-to-solved for the U/D and L/R centers alone (per the user, see
// chat: "penalizing high-distance center configurations early" -- tier 1's
// h1 only looks at wing+FB, tier 2's h_center only looks at LR+FB jointly,
// so in both cases some axis's center distance is invisible to the
// heuristic even though it still has to be solved eventually). Plain
// single-source BFS from the solved class (0) over the already-built
// UD2520_TRANS/LR2520_TRANS graphs, using S1's own 21-move generator set --
// same move set and "solved means class 0" convention as everywhere else
// these 2520-reduced coordinates are used (respects the 16-fold same-face
// rotation symmetry, so a merely-rotated-but-physically-solved arrangement
// correctly reads back as distance 0, not spuriously penalized).
static std::vector<uint8_t> UD2520_DIST;
static std::vector<uint8_t> LR2520_DIST;
static void build_center2520_dist(const int16_t trans[2520][P2_NUM_MOVES], std::vector<uint8_t>& dist) {
    dist.assign(2520, 255);
    dist[0] = 0;
    std::queue<int> q;
    q.push(0);
    while (!q.empty()) {
        int c = q.front(); q.pop();
        uint8_t d = dist[c];
        for (int pm = 0; pm < P2_NUM_MOVES; pm++) {
            int nc = trans[c][pm];
            if (dist[nc] == 255) { dist[nc] = d + 1; q.push(nc); }
        }
    }
}
static void build_ud2520_dist() { build_center2520_dist(UD2520_TRANS, UD2520_DIST); }
// Early center penalty: PENALTY_RATE per unit of U/D (L/R) center distance above PENALTY_THRESHOLD (0 at or below it).
// The rate is an INTEGER (1 was found to be the best value; a fractional rate used to be summed as a double and rounded up
// once, which cost ~6 ns per node for nothing: h = h1 + integer). The penalty of a center class is a table lookup,
// PEN_UD[ud2520] / PEN_LR[lr2520] (built after the distance tables, with the final threshold and rate).
static int PENALTY_THRESHOLD = 7;
static int PENALTY_RATE = 1;
static std::vector<int8_t> PEN_UD, PEN_LR;
static inline int center_dist_penalty(int dist) { return PENALTY_RATE * std::max(0, dist - PENALTY_THRESHOLD); }
static void build_penalty_tables() {
    PEN_UD.assign(UD2520_DIST.size(), 0);
    PEN_LR.assign(LR2520_DIST.size(), 0);
    for (size_t i = 0; i < UD2520_DIST.size(); i++) PEN_UD[i] = (int8_t)std::min(127, center_dist_penalty(UD2520_DIST[i]));
    for (size_t i = 0; i < LR2520_DIST.size(); i++) PEN_LR[i] = (int8_t)std::min(127, center_dist_penalty(LR2520_DIST[i]));
}
static void build_lr2520_dist() {
    build_center2520_dist(LR2520_TRANS, LR2520_DIST);
    build_penalty_tables();   // needs both distance tables (main builds the U/D one first)
}

} // namespace coords
