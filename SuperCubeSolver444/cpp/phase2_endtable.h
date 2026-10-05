// Phase 2 (S1 -> S2): a JOINT endtable over the wing coset mask and the F/B center permutation.
//
// The separate wing and F/B distance tables each see only half of the coordinate, so max(wing, fb) is exact only up
// to distance 3 and then underestimates (46% of the states at distance 6 are off by one or more). The joint table
// is the exact distance to S2 over the 21 phase-2 moves of the PAIR (wing mask, F/B permutation):
//   distance 0..6 : 96, 192, 2784, 28032, 234816, 2060160, 19384800 states (22M in total; layer 7 would be ~180M).
// Like phase 1 it is a set of cumulative blocked Bloom filters, one per distance k holding every state at distance
// <= k (a false positive only makes a state look closer than it is, so the heuristic stays a lower bound). A key
// is the wing mask (24 bits) + the F/B permutation rank (16 bits). Farther than the table reaches the heuristic is
// "more than g_depth". The default depth is 5 (5.7 MB, ~0.2 s to build); depth 6 is 52 MB and ~2.5 s.
// The F/B and wing distance tables still run next to it: the heuristic takes the maximum.
#pragma once

namespace p2j {

static int g_depth = 5;           // exact distances known up to this (0 = off, <= 6)
static double g_fpr = 1e-3;       // target false positive rate of every filter
static double g_build_ms = 0;
static std::vector<p1::BlockedBloom> BLOOM;   // [k] = every state at distance <= k, k = 0..g_depth
static std::vector<size_t> LAYER_SIZES;       // exact for layers 0..g_depth-1; the last one is an estimate

static inline uint64_t make_key(uint32_t wing_mask, int fb2) { return ((uint64_t)fb2 << 24) | wing_mask; }

static void expand(const uint64_t* from, size_t n, std::vector<uint64_t>& out) {
    for (size_t i = 0; i < n; i++) {
        const uint32_t mask = (uint32_t)(from[i] & 0xFFFFFF);
        const int fb2 = (int)(from[i] >> 24);
        for (int pm = 0; pm < P2_NUM_MOVES; pm++)
            out.push_back(make_key(p2full::wing_step(mask, P2_TO_FULL_MOVE_INDEX[pm]), fb2_trans(fb2, pm)));
    }
}

// Needs the wing mask tables, FB2_TRANS and the F/B distance bookkeeping (FB_SOLVED_INDICES) to be ready.
// Layers 0..g_depth-1 are expanded exactly (sorted, deduplicated against the two layers below); the last layer is
// never expanded, so its raw neighbour keys (duplicates and lower-layer states included, harmless in a Bloom
// filter) are streamed straight into its filter, whose size is estimated from the previous layer's duplicate ratio.
static void build() {
    g_depth = std::max(0, std::min(g_depth, 6));
    if (g_depth == 0) return;
    auto t0 = std::chrono::steady_clock::now();
    LapTimer lt;
    std::vector<std::vector<uint64_t>> layers(1);
    for (int i = 0; i < 96; i++) layers[0].push_back(make_key(p2full::WING_SOLVED_MASK, FB_SOLVED_INDICES[i]));
    std::sort(layers[0].begin(), layers[0].end());
    double dup_ratio = 2.3;
    for (int d = 1; d < g_depth; d++) {
        std::vector<uint64_t> next;
        next.reserve(layers.back().size() * P2_NUM_MOVES);
        expand(layers.back().data(), layers.back().size(), next);
        const size_t raw = next.size();
        p1::sort_keys(next);
        next.erase(std::unique(next.begin(), next.end()), next.end());
        dup_ratio = (double)raw / (double)next.size();
        for (int e = d - 1; e >= std::max(0, d - 2); e--) {   // neighbours of layer d-1 lie in layers d-2, d-1, d
            std::vector<uint64_t> diff;
            std::set_difference(next.begin(), next.end(), layers[e].begin(), layers[e].end(), std::back_inserter(diff));
            next.swap(diff);
        }
        layers.push_back(std::move(next));
    }
    lt.lap("exact layers (expand + sort + dedupe)");
    LAYER_SIZES.clear();
    for (auto& l : layers) LAYER_SIZES.push_back(l.size());
    LAYER_SIZES.push_back((size_t)((double)layers.back().size() * P2_NUM_MOVES / dup_ratio * 1.08));   // estimate, a bit generous

    BLOOM.assign(g_depth + 1, p1::BlockedBloom());
    size_t cum = 0;
    for (int d = 0; d <= g_depth; d++) {
        cum += LAYER_SIZES[d];
        BLOOM[d].init(cum, g_fpr);
    }
    lt.lap("Bloom filters allocated");
    for (int e = 0; e < g_depth; e++)
        for (int d = e; d <= g_depth; d++) BLOOM[d].add_all(layers[e]);
    lt.lap("exact layers inserted");
    const std::vector<uint64_t>& from = layers.back();
    const size_t CHUNK = (size_t)1 << 17;
    std::vector<uint64_t> chunk;
    chunk.reserve(CHUNK * P2_NUM_MOVES);
    for (size_t i = 0; i < from.size(); i += CHUNK) {
        chunk.clear();
        expand(from.data() + i, std::min(CHUNK, from.size() - i), chunk);
        BLOOM[g_depth].add_all(chunk);
    }
    lt.lap("last layer streamed into its filter");
    g_build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Distance to S2 of (wing mask, F/B permutation): exact up to g_depth (up to Bloom false positives, which can only
// make it smaller), g_depth + 1 = "farther than that", 0 when the table is off.
static inline int dist(uint32_t wing_mask, int fb2) {
    if (g_depth == 0) return 0;
    uint64_t bh; int pos[10];
    p1::BlockedBloom::positions(make_key(wing_mask, fb2), bh, pos, 10);
    if (!BLOOM[g_depth].test_positions(bh, pos)) return g_depth + 1;
    int lo = 0, hi = g_depth;   // smallest k with the key in BLOOM[k]
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (BLOOM[mid].test_positions(bh, pos)) hi = mid; else lo = mid + 1;
    }
    return lo;
}

} // namespace p2j
