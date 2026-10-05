// S2 -> S3' "on the other side": reach S3' by SWITCHING (inverting the cube) instead of by moves alone.
//
// S3' is not closed under inverses, so a normal search cannot switch when it lands in S3'. But from a state X in S2
// the INVERSE of X is in S3' iff two small conditions hold on X itself (checked against the real test, see the
// self-tests):
//   (centers) the L/R centers of X can be solved with [R or nothing][L or nothing][180-degree turns]; equivalently
//             X's L/R arrangement is (half turns) then (R^a L^b), 96 * 4 = 384 of the 8! = 40320 arrangements. (The
//             F/B part is automatic: every S2 state has an F/B arrangement whose inverse is good.)
//   (wings)   the 4 wing pairs (edges) that belong in the equator slice are "paired amongst each other" and either
//             solved or two 2-cycles: write A_e = the edge position holding the POSITIVE wing of equator edge e and
//             B_e = the edge position holding its NEGATIVE wing; the condition is A_e = B_v(e) for all four e, with v
//             the identity or a double transposition (the Klein four-group; a single 2-cycle does NOT qualify).
// When both hold the search must switch, and the inverted cube is in S3'. This route needs two small tables:
//   LR8_DIST   40320 entries: the L/R arrangement itself (no 2520-reduction), BFS from the 384 goal arrangements
//              under the 17 S2 moves, with a plain transition table LR8_TRANS (40320 x 17).
//   WING_DIST  1,470,150 entries over the positions of the 8 relevant wings. A raw state is (A_e, B_e) for the 4
//              equator edges (12*11*10*9)^2 = 141M. The conditions are invariant under relabeling the four edges
//              (S4, same relabel for the positive and negative wing) and under relabeling only the negative wings
//              by a Klein-four element (V4): 141M / (24 * 4) = C(12,4)^2 * 6 = 1,470,150 classes. The index of a raw
//              state: relabel so the A_e are increasing, then the position sets of A and of B (495 each), and the
//              relative order of the B's modulo V4 (6 cosets). No transition table: a move maps the 8 positions
//              through 12-entry tables and the index is recomputed.
// The search state (Tier2AltState, tier code 7 in search.h) holds both coordinates plus the U/D class that feeds the
// early center penalty; its heuristic is max of the two distances plus the same flat costs as tier 2's.
#pragma once

namespace s2sw {

static const int NCOL = 17;                  // the S2 moves (coords::S2_OWN_MOVES_PM)
static const uint32_t NWING = 495u * 495u * 6u;   // 1,470,150
static int FULL_TO_COL[NUM_MOVES];
static int COL_PM[NCOL];
static double g_build_ms = 0;

// ------------------------------------------------------------------ L/R centers
static std::vector<uint16_t> LR8_TRANS;      // [raw * NCOL + col], raw = rank of the occupant permutation (compute_lr2)
static std::vector<uint8_t> LR8_DIST;
static long long LR8_HIST[32];

static void build_lr() {
    LR8_TRANS.assign((size_t)40320 * NCOL, 0);
    for (int raw = 0; raw < 40320; raw++) {
        const auto p = coords::unrank_perm8(raw);
        for (int c = 0; c < NCOL; c++)
            LR8_TRANS[(size_t)raw * NCOL + c] = (uint16_t)coords::rank_perm8(coords::apply_move_to_lr_perm(p, P2_TO_FULL_MOVE_INDEX[COL_PM[c]]));
    }
    // goal: (half turns) then R^a L^b, from the solved arrangement
    const int solved = coords::rank_perm8({0, 1, 2, 3, 4, 5, 6, 7});
    std::vector<char> in_h(40320, 0);
    std::vector<int> q = {solved};
    in_h[solved] = 1;
    for (size_t qi = 0; qi < q.size(); qi++)
        for (int c = 0; c < NCOL; c++) {
            const int pm = COL_PM[c];
            const int m = P2_TO_FULL_MOVE_INDEX[pm];
            if (m % 3 != 1) continue;   // half turns only (the move index is face*3 + {quarter, half, quarter'})
            const int nr = LR8_TRANS[(size_t)q[qi] * NCOL + c];
            if (!in_h[nr]) { in_h[nr] = 1; q.push_back(nr); }
        }
    const int cR = FULL_TO_COL[P2_TO_FULL_MOVE_INDEX[6]], cL = FULL_TO_COL[P2_TO_FULL_MOVE_INDEX[9]];   // R and L quarter turns
    LR8_DIST.assign(40320, 255);
    std::vector<int> frontier;
    for (int r = 0; r < 40320; r++) if (in_h[r])
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
            int x = r;
            if (a) x = LR8_TRANS[(size_t)x * NCOL + cR];
            if (b) x = LR8_TRANS[(size_t)x * NCOL + cL];
            if (LR8_DIST[x] == 255) { LR8_DIST[x] = 0; frontier.push_back(x); }
        }
    // multi-source BFS (the move set is closed under inverses, so this is the distance TO the goal set)
    std::vector<int> next;
    for (int d = 0; !frontier.empty(); d++) {
        next.clear();
        for (int x : frontier)
            for (int c = 0; c < NCOL; c++) {
                const int nx = LR8_TRANS[(size_t)x * NCOL + c];
                if (LR8_DIST[nx] == 255) { LR8_DIST[nx] = (uint8_t)(d + 1); next.push_back(nx); }
            }
        frontier.swap(next);
    }
    for (int r = 0; r < 40320; r++) LR8_HIST[std::min<int>(LR8_DIST[r], 31)]++;
}

// ------------------------------------------------------------------ wings
static uint8_t STEP_POS[NCOL][12], STEP_NEG[NCOL][12];   // where a positive / negative wing of edge position i goes
static uint16_t RANK495[4096];                            // 12-bit mask with 4 ones -> 0..494
static uint16_t MASK_OF_RANK[495];
static uint8_t SET_BITS[495][4];                          // the 4 positions of a set, ascending
static uint8_t BELOW495[495][12];                         // BELOW495[set][p] = how many positions of the set lie below p
static uint8_t COSET[256];                                // code of the relative order (tau) -> V4 coset 0..5 (255: not a permutation)
static uint8_t COSET_REP[6][4];                           // a representative tau of each coset
static int COSET_IDENTITY;                                // the coset of the identity order
static std::vector<uint8_t> WING_DIST;
static long long WING_HIST[32];

// 8 positions: a[j] = edge position holding the positive wing of equator edge j, b[j] = the negative wing's.
// Index of the class of (a, b): see the header comment.
// Reference implementation (the original: ranks by comparisons); tests check wing_index against it, --no-s2-fast uses it.
static inline uint32_t wing_index_ref(const uint8_t* a, const uint8_t* b) {
    const uint32_t lm = (1u << a[0]) | (1u << a[1]) | (1u << a[2]) | (1u << a[3]);
    const uint32_t hm = (1u << b[0]) | (1u << b[1]) | (1u << b[2]) | (1u << b[3]);
    uint32_t code = 0;
    for (int j = 0; j < 4; j++) {
        const uint32_t ra = (a[0] < a[j]) + (a[1] < a[j]) + (a[2] < a[j]) + (a[3] < a[j]);   // rank of a[j] among the a's
        const uint32_t rb = (b[0] < b[j]) + (b[1] < b[j]) + (b[2] < b[j]) + (b[3] < b[j]);
        code |= rb << (2 * ra);   // tau(ra) = rb
    }
    return ((uint32_t)RANK495[lm] * 495u + RANK495[hm]) * 6u + COSET[code];
}
static bool g_fast = true;   // --no-s2-fast: reference index and no budget shortcut (A/B timing)
static inline uint32_t wing_index(const uint8_t* a, const uint8_t* b) {
    if (!g_fast) return wing_index_ref(a, b);
    const uint32_t lm = (1u << a[0]) | (1u << a[1]) | (1u << a[2]) | (1u << a[3]);
    const uint32_t hm = (1u << b[0]) | (1u << b[1]) | (1u << b[2]) | (1u << b[3]);
    const uint32_t ka = RANK495[lm], kb = RANK495[hm];
    const uint8_t* ba = BELOW495[ka];   // rank of a position inside its set: a table read (was 4 compares each)
    const uint8_t* bb = BELOW495[kb];
    const uint32_t code = ((uint32_t)bb[b[0]] << (2 * ba[a[0]])) | ((uint32_t)bb[b[1]] << (2 * ba[a[1]]))
                        | ((uint32_t)bb[b[2]] << (2 * ba[a[2]])) | ((uint32_t)bb[b[3]] << (2 * ba[a[3]]));   // tau(rank in A) = rank in B
    return (ka * 495u + kb) * 6u + COSET[code];
}

static void init_wing_tables() {
    for (int c = 0; c < NCOL; c++)
        for (int i = 0; i < 12; i++) {
            STEP_POS[c][i] = (uint8_t)coords::SIGMA_POS_P2[COL_PM[c]][i];
            STEP_NEG[c][i] = (uint8_t)coords::SIGMA_NEG_P2[COL_PM[c]][i];
        }
    int n = 0;
    for (uint32_t m = 0; m < 4096; m++) {
        RANK495[m] = 0;
        if (__builtin_popcount(m) != 4) continue;
        MASK_OF_RANK[n] = (uint16_t)m;
        RANK495[m] = (uint16_t)n;
        int k = 0;
        for (int bit = 0; bit < 12; bit++) if (m >> bit & 1) SET_BITS[n][k++] = (uint8_t)bit;
        { int below = 0; for (int p = 0; p < 12; p++) { BELOW495[n][p] = (uint8_t)below; if (m >> p & 1) below++; } }
        n++;
    }
    // V4 cosets of the relative order tau (tau(i) = rank among the B's of the negative wing of the edge with A-rank i);
    // relabeling only the negative wings by v gives tau o v
    static const int V4[4][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {2, 3, 0, 1}, {3, 2, 1, 0}};
    for (int i = 0; i < 256; i++) COSET[i] = 255;
    int p[4] = {0, 1, 2, 3}, ncoset = 0;
    do {
        uint32_t code = 0;
        for (int i = 0; i < 4; i++) code |= (uint32_t)p[i] << (2 * i);
        if (COSET[code] != 255) continue;
        for (int v = 0; v < 4; v++) {
            uint32_t c2 = 0;
            for (int i = 0; i < 4; i++) c2 |= (uint32_t)p[V4[v][i]] << (2 * i);
            COSET[c2] = (uint8_t)ncoset;
        }
        for (int i = 0; i < 4; i++) COSET_REP[ncoset][i] = (uint8_t)p[i];
        if (p[0] == 0 && p[1] == 1 && p[2] == 2 && p[3] == 3) COSET_IDENTITY = ncoset;
        ncoset++;
    } while (std::next_permutation(p, p + 4));
    if (ncoset != 6 || n != 495) { printf("FATAL: S2 switch wing classes: %d cosets, %d sets\n", ncoset, n); abort(); }
}

// a representative raw state of a class index
static inline void wing_decode(uint32_t idx, uint8_t* a, uint8_t* b) {
    const uint32_t coset = idx % 6u, pair = idx / 6u;
    const uint8_t* la = SET_BITS[pair / 495u];
    const uint8_t* hb = SET_BITS[pair % 495u];
    for (int j = 0; j < 4; j++) { a[j] = la[j]; b[j] = hb[COSET_REP[coset][j]]; }
}

static const int WING_EXPAND_DEPTH = 4;   // layers 0..4 are expanded top-down (this labels layer 5)
static const int WING_MAX_DIST = 7;
static void build_wing() {
    WING_DIST.assign(NWING, 255);
    std::vector<uint32_t> frontier, next;
    for (uint32_t l = 0; l < 495; l++) {   // goal: the same position set, identity order (A_e = B_e up to V4)
        const uint32_t idx = (l * 495u + l) * 6u + (uint32_t)COSET_IDENTITY;
        WING_DIST[idx] = 0;
        frontier.push_back(idx);
    }
    // The layers are 495 1260 7700 40192 187609 607686 614924 10284 (distances 0..7); the last layers are the expensive ones and
    // the maximum is fixed: layers 0..4 are expanded top-down (this labels layer 5), layer 6 is labeled BOTTOM-UP (an unlabeled
    // class with a neighbour in layer 5 is in layer 6: the moves are closed under inverses, so neighbour = predecessor), and
    // every class left over is at distance 7. (test_s2_wing_dist checks the whole table against the distance-field characterization.)
    for (int d = 0; !frontier.empty() && d <= WING_EXPAND_DEPTH; d++) {
        next.clear();
        for (uint32_t idx : frontier) {
            uint8_t a[4], b[4], na[4], nb[4];
            wing_decode(idx, a, b);
            for (int c = 0; c < NCOL; c++) {
                for (int j = 0; j < 4; j++) { na[j] = STEP_POS[c][a[j]]; nb[j] = STEP_NEG[c][b[j]]; }
                const uint32_t ni = wing_index(na, nb);
                if (WING_DIST[ni] == 255) { WING_DIST[ni] = (uint8_t)(d + 1); next.push_back(ni); }
            }
        }
        frontier.swap(next);
    }
    for (uint32_t idx = 0; idx < NWING; idx++) {
        if (WING_DIST[idx] != 255) continue;
        uint8_t a[4], b[4], na[4], nb[4];
        wing_decode(idx, a, b);
        for (int c = 0; c < NCOL; c++) {
            for (int j = 0; j < 4; j++) { na[j] = STEP_POS[c][a[j]]; nb[j] = STEP_NEG[c][b[j]]; }
            if (WING_DIST[wing_index(na, nb)] == WING_EXPAND_DEPTH + 1) { WING_DIST[idx] = (uint8_t)(WING_EXPAND_DEPTH + 2); break; }
        }
    }
    for (uint32_t i = 0; i < NWING; i++) if (WING_DIST[i] == 255) WING_DIST[i] = (uint8_t)WING_MAX_DIST;
    for (uint32_t i = 0; i < NWING; i++) WING_HIST[std::min<int>(WING_DIST[i], 31)]++;
}

// ------------------------------------------------------------------ the tier-2 alternative state
struct Tier2AltState { uint16_t lr8; int16_t ud2520; uint8_t a[4], b[4]; };

static inline Tier2AltState extract(const State6& s) {
    Tier2AltState t;
    t.lr8 = (uint16_t)compute_lr2(s);
    t.ud2520 = coords::CENTER2520_UD[compute_ud2(s)];
    for (int j = 0; j < 4; j++) {
        const int e = coords::EQUATORIAL_IDX[j];
        t.a[j] = (uint8_t)coords::POS_INDEX[s.wing_slot[POS_SLOTS[e]]];
        t.b[j] = (uint8_t)coords::NEG_INDEX[s.wing_slot[NEG_OF[e]]];
    }
    return t;
}
static inline Tier2AltState apply_move(const Tier2AltState& t, int m) {
    const int c = FULL_TO_COL[m];
    Tier2AltState r;
    r.lr8 = LR8_TRANS[(size_t)t.lr8 * NCOL + c];
    r.ud2520 = coords::UD2520_TRANS[t.ud2520][FULL_TO_P2_MOVE_INDEX[m]];
    for (int j = 0; j < 4; j++) { r.a[j] = STEP_POS[c][t.a[j]]; r.b[j] = STEP_NEG[c][t.b[j]]; }
    return r;
}
// the tier's lower bound on the moves to the goal (without the flat costs); 0 at the goal
static inline int bound(const Tier2AltState& t) {
    return std::max((int)LR8_DIST[t.lr8], (int)WING_DIST[wing_index(t.a, t.b)]);
}
// true iff both conditions hold (the inverse of the state is in S3'); else *h = the tier's heuristic
static inline bool at_goal_else_h(const Tier2AltState& t, int* h) {
    const int b = bound(t);
    if (b == 0) return true;
    *h = b + g_cost34p + g_cost45 + g_cost56 + g_cost67 + coords::PEN_UD[t.ud2520];
    return false;
}
// Same, for a child whose h only matters up to the budget r = threshold - g - 1 (set by the search before it steps the child):
// when the L/R table alone already puts h above r the child is pruned without computing the wing index (63% of the children).
// A goal (bound 0) is never pruned. The returned h is only a lower bound > r then, which is all a pruned child needs.
static int g_r = 0;
static inline bool at_goal_else_h_budget(const Tier2AltState& t, int* h) {
    const int lr = LR8_DIST[t.lr8];
    const int add = g_cost34p + g_cost45 + g_cost56 + g_cost67 + coords::PEN_UD[t.ud2520];
    if (g_fast && lr > 0 && lr + add > g_r) { *h = lr + add; return false; }
    const int b = std::max(lr, (int)WING_DIST[wing_index(t.a, t.b)]);
    if (b == 0) return true;
    *h = b + add;
    return false;
}
static inline bool is_goal(const Tier2AltState& t) {
    return LR8_DIST[t.lr8] == 0 && WING_DIST[wing_index(t.a, t.b)] == 0;
}
static inline int heuristic(const Tier2AltState& t) {
    int h = 0;
    at_goal_else_h(t, &h);
    return h;
}

static void print_hist(const char* name, const long long* hist, long long n) {
    int mx = 0;
    double sum = 0;
    for (int d = 0; d < 31; d++) if (hist[d]) { mx = d; sum += (double)d * hist[d]; }
    printf("  %s: %lld states, avg=%.4f max=%d, unreached=%lld; distance:", name, n, sum / (double)(n - hist[31]), mx, hist[31]);
    for (int d = 0; d <= mx; d++) printf(" %d:%lld", d, hist[d]);
    printf("\n");
}

// needs: coords centers/wings (center locals, equatorial split, sigma tables), FULL_TO_P2_MOVE_INDEX
static void build() {
    auto t0 = std::chrono::steady_clock::now();
    for (int m = 0; m < NUM_MOVES; m++) FULL_TO_COL[m] = -1;
    for (int c = 0; c < NCOL; c++) { COL_PM[c] = coords::S2_OWN_MOVES_PM[c]; FULL_TO_COL[P2_TO_FULL_MOVE_INDEX[COL_PM[c]]] = c; }
    LapTimer lt;
    build_lr();
    lt.lap("L/R centers: transitions + BFS (40320 states)");
    init_wing_tables();
    lt.lap("wing class tables");
    build_wing();
    lt.lap("wing classes: BFS over 1,470,150 states");
    g_build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace s2sw
