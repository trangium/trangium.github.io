// Phase 1 (S0 -> S1): an exact-distance endtable over the three center bitmasks, nothing else.
//
// Whether a cube is d moves from S1 (mod whole-cube rotation) depends only on
//   * the UNORDERED set of the three center masks {ud, lr, fb}: 24-bit masks over the 24 center positions with
//     a 1 where a UD / LR / FB center sits (always disjoint and covering all 24 positions, so the third mask is
//     implied by the other two), because a rotation after S1 can reach any order of the three, and
//   * the wing parity (the S0/S1 "OLL" parity) -- NOT symmetric.
// A state therefore is a MaskState (masks + parity), a move permutes the bits, and the distance to S1 is a
// lookup. A BFS from the six rotated copies of S1 over canonical keys (min mask, max mask, parity) gives layers
// that are tiny (a move only permutes bit positions, and the masks of states far from S1 live in a small part of
// the 3.2e9-state space):
//   distance 0..6 : 1, 3, 54, 717, 9042, 118602, 1539456 states  (1.67M in total).
// One cumulative Bloom filter per distance k holds every state at distance <= k. They are blocked Bloom
// filters (one 64-byte cache line per query, ~3.5 MB for all of them, built in well under a second at start-up):
// a false positive only makes a state look closer than it is. Farther than the table reaches, the heuristic is
// simply "more than g_depth".
//
// This replaced the 735,471-entry per-coordinate transition and distance tables of the first design (kept in
// archive/cpp/phase1_coordinate_tables/): the endtable search needs fewer nodes (-32%) and less time (1.9x) for
// the same solutions, and a State6 carries just the masks.
#pragma once
#include "common.h"

namespace p1 {

static inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static const uint32_t ALL = (1u << 24) - 1;

// Symmetric in the three masks (the median is implied by min and max), not in the parity.
static inline uint64_t make_key(uint32_t a, uint32_t b, uint32_t c, int parity) {
    uint32_t lo = std::min({a, b, c}), hi = std::max({a, b, c});
    return (uint64_t)lo | ((uint64_t)hi << 24) | ((uint64_t)(parity & 1) << 48);
}

// A permutation of the 24 bit positions, as three byte lookup tables.
struct BitPerm {
    uint32_t lut[3][256];
    void build(const int* dest) {   // bit i moves to bit dest[i]
        for (int byte = 0; byte < 3; byte++)
            for (int v = 0; v < 256; v++) {
                uint32_t out = 0;
                for (int bit = 0; bit < 8; bit++) if (v >> bit & 1) out |= 1u << dest[byte * 8 + bit];
                lut[byte][v] = out;
            }
    }
    inline uint32_t apply(uint32_t x) const { return lut[0][x & 255] | lut[1][(x >> 8) & 255] | lut[2][x >> 16]; }
};

// ---------------------------------------------------------------- blocked Bloom filter
struct BlockedBloom {
    std::vector<uint64_t> words;   // storage, over-allocated so that `blocks` can start on a 64-byte cache line
    uint64_t* blocks = nullptr;    // nblocks * 8 words = nblocks 512-bit blocks, each exactly one cache line
    uint64_t nblocks = 1;
    int k = 8;

    // Expected false positive rate of a blocked filter with `bits_per_elem` bits per element (Poisson load per block).
    static double fpr_of(double bits_per_elem, int k) {
        const double lambda = 512.0 / bits_per_elem;
        double p = 0, pois = std::exp(-lambda);
        for (int j = 0; j < 400; j++) {
            if (j > 0) pois *= lambda / j;
            p += pois * std::pow(1.0 - std::pow(1.0 - 1.0 / 512.0, (double)k * j), k);
        }
        return p;
    }
    void init(size_t n, double target_fpr) {
        // the (bits per element, k) search costs ~14 ms (fpr_of sums 400 terms with pow) and every filter of a table asks for
        // the same target: remember the last answer
        static double cached_target = -1, cached_b = 40;
        static int cached_k = 10;
        if (target_fpr != cached_target) {
            double best_b = 40; int best_k = 10;
            for (int kk = 4; kk <= 10; kk++)
                for (double b = 8; b <= 40; b += 0.25)
                    if (fpr_of(b, kk) <= target_fpr && b < best_b) { best_b = b; best_k = kk; break; }
            cached_target = target_fpr; cached_b = best_b; cached_k = best_k;
        }
        const double best_b = cached_b;
        k = cached_k;
        nblocks = std::max<uint64_t>(16, (uint64_t)std::ceil((double)n * best_b / 512.0));
        words.assign(nblocks * 8 + 8, 0);
        blocks = (uint64_t*)(((uintptr_t)words.data() + 63) & ~(uintptr_t)63);
    }
    // all k probe positions of one key inside a block: 9 bits each, from two independent 64-bit mixes
    static inline void positions(uint64_t key, uint64_t& block_hash, int* pos, int k) {
        const uint64_t g1 = splitmix64(key), g2 = splitmix64(g1 ^ 0xD1B54A32D192ED03ULL);
        block_hash = g1 >> 32;
        for (int i = 0; i < 4; i++) pos[i] = (int)(g1 >> (9 * i)) & 511;
        for (int i = 4; i < 8; i++) pos[i] = (int)(g2 >> (9 * (i - 4))) & 511;
        for (int i = 8; i < k; i++) pos[i] = (int)(g2 >> (36 + 9 * (i - 8))) & 511;
    }
    inline uint64_t block_of(uint64_t block_hash) const { return (block_hash * nblocks) >> 32; }
    void add(uint64_t key) {
        uint64_t bh; int pos[10];
        positions(key, bh, pos, k);
        uint64_t* blk = &blocks[block_of(bh) * 8];
        for (int i = 0; i < k; i++) blk[pos[i] >> 6] |= 1ULL << (pos[i] & 63);
    }
    // Inserts many keys: the blocks are prefetched a batch ahead (the filters are far bigger than the caches).
    void add_all(const std::vector<uint64_t>& keys) {
#ifdef __EMSCRIPTEN__
        add_all_grouped(keys.data(), keys.size());
#else
        const size_t B = 64;   // deeper prefetch (was 16): the filters are far bigger than the caches
        uint64_t bh[B]; int pos[B][10];
        for (size_t i = 0; i < keys.size(); i += B) {
            const size_t n = std::min(B, keys.size() - i);
            for (size_t j = 0; j < n; j++) {
                positions(keys[i + j], bh[j], pos[j], k);
                __builtin_prefetch(&blocks[block_of(bh[j]) * 8], 1);
            }
            for (size_t j = 0; j < n; j++) {
                uint64_t* blk = &blocks[block_of(bh[j]) * 8];
                for (int t = 0; t < k; t++) blk[pos[j][t] >> 6] |= 1ULL << (pos[j][t] & 63);
            }
        }
#endif
    }
    // WebAssembly has no prefetch instruction (and the cache misses and TLB misses of 35M random inserts into a 48 MB filter cost
    // seconds): the keys of a chunk are grouped by the block they hit (radix sort on the block number), so that the inserts of
    // one group stay inside a few hundred KB of the filter.
    void add_all_grouped(const uint64_t* keys, size_t n) {
        const size_t CH = (size_t)1 << 18;
        std::vector<uint32_t> blk_of(CH), order(CH), tmp_blk(CH), tmp_order(CH);
        int bits = 1;
        while (((uint64_t)1 << bits) < nblocks) bits++;
        for (size_t base = 0; base < n; base += CH) {
            const size_t m = std::min(CH, n - base);
            for (size_t i = 0; i < m; i++) {
                const uint64_t g1 = splitmix64(keys[base + i]);
                blk_of[i] = (uint32_t)block_of(g1 >> 32);
                order[i] = (uint32_t)i;
            }
            for (int shift = 0; shift < bits; shift += 10) {   // LSD radix sort of (block, index) on the block number, 10 bits a pass
                uint32_t cnt[1025] = {0};
                for (size_t i = 0; i < m; i++) cnt[((blk_of[i] >> shift) & 1023) + 1]++;
                for (int c = 0; c < 1024; c++) cnt[c + 1] += cnt[c];
                for (size_t i = 0; i < m; i++) { const uint32_t d = cnt[(blk_of[i] >> shift) & 1023]++; tmp_blk[d] = blk_of[i]; tmp_order[d] = order[i]; }
                blk_of.swap(tmp_blk); order.swap(tmp_order);
            }
            for (size_t i = 0; i < m; i++) {
                uint64_t bh; int pos[10];
                positions(keys[base + order[i]], bh, pos, k);
                uint64_t* blk = &blocks[(uint64_t)blk_of[i] * 8];
                for (int t = 0; t < k; t++) blk[pos[t] >> 6] |= 1ULL << (pos[t] & 63);
            }
        }
    }
    inline bool test_positions(uint64_t bh, const int* pos) const {
        const uint64_t* blk = &blocks[block_of(bh) * 8];
        for (int i = 0; i < k; i++) if (!(blk[pos[i] >> 6] >> (pos[i] & 63) & 1)) return false;
        return true;
    }
    bool test(uint64_t key) const {
        uint64_t bh; int pos[10];
        positions(key, bh, pos, k);
        return test_positions(bh, pos);
    }
    size_t bytes() const { return nblocks * 64; }
};

// ---------------------------------------------------------------- the state
// The three center masks (real positions: ud, lr, fb) and the wing parity.
struct MaskState {
    uint32_t m[3]; int parity;
    bool operator==(const MaskState& o) const { return m[0] == o.m[0] && m[1] == o.m[1] && m[2] == o.m[2] && parity == o.parity; }
};

static int g_depth = 6;           // exact distances are known up to this (<= 7)
static double g_fpr = 1e-3;       // target false positive rate of every filter
static bool g_built = false;
static BitPerm MOVE_PERM[NUM_MOVES];
static uint32_t CLASS_MASKS[6][3];   // the (ud, lr, fb) masks of S1 in whole-cube-rotation class c (c = 0: literal S1)
static uint64_t GOAL_KEY = 0;
static std::vector<BlockedBloom> BLOOM;   // [k] = every state at distance <= k, k = 1..g_depth
static std::vector<std::vector<uint64_t>> LAYERS;   // exact, sorted keys per distance (kept only while small: self-tests)
static std::vector<size_t> LAYER_SIZES;
static double g_build_ms = 0;

// The masks and wing parity of a cube given as flat piece->position arrays.
static inline MaskState extract(const int* wing_slot, const int* center_slot) {
    MaskState r{{0, 0, 0}, 0};
    for (int i = 0; i < 8; i++) {
        r.m[0] |= 1u << center_slot[UD_TARGET[i]];
        r.m[1] |= 1u << center_slot[LR_TARGET[i]];
        r.m[2] |= 1u << center_slot[FB_TARGET[i]];
    }
    bool visited[24] = {false};
    for (int i = 0; i < 24; i++) {
        if (visited[i]) continue;
        int j = i, len = 0;
        while (!visited[j]) { visited[j] = true; j = wing_slot[j]; len++; }
        if (len % 2 == 0) r.parity ^= 1;
    }
    return r;
}
static inline MaskState solved() { return MaskState{{CLASS_MASKS[0][0], CLASS_MASKS[0][1], CLASS_MASKS[0][2]}, 0}; }
static inline MaskState step(const MaskState& s, int m) {
    MaskState r;
    r.m[0] = MOVE_PERM[m].apply(s.m[0]);
    r.m[1] = MOVE_PERM[m].apply(s.m[1]);
    r.m[2] = ALL ^ r.m[0] ^ r.m[1];
    r.parity = s.parity ^ WING_PARITY[m];
    return r;
}
// The whole-cube-rotation class of an S1 state (0 = literal S1), -1 if the state is not S1 mod rotation.
static inline int mask_class(const MaskState& s) {
    if (s.parity) return -1;
    for (int c = 0; c < 6; c++)
        if (s.m[0] == CLASS_MASKS[c][0] && s.m[1] == CLASS_MASKS[c][1]) return c;
    return -1;
}

// ---------------------------------------------------------------- building the endtable
static void init_tables() {
    for (int m = 0; m < NUM_MOVES; m++) MOVE_PERM[m].build(CENTER_PERM[m]);
    // Class c's masks: rotating the solved cube forward by class c puts the UD / LR / FB centers on these positions.
    const int* targets[3] = {UD_TARGET, LR_TARGET, FB_TARGET};
    for (int c = 0; c < 6; c++)
        for (int k = 0; k < 3; k++) {
            uint32_t mask = 0;
            for (int i = 0; i < 8; i++) mask |= 1u << ROTATE_CENTER[c][targets[k][i]];
            CLASS_MASKS[c][k] = mask;
        }
    GOAL_KEY = make_key(CLASS_MASKS[0][0], CLASS_MASKS[0][1], CLASS_MASKS[0][2], 0);
}

// Sorts keys (49 bits) -- LSD radix sort, far faster than std::sort on the tens of millions of keys of the last layer.
static void sort_keys(std::vector<uint64_t>& v) {
    if (v.size() < (1u << 16)) { std::sort(v.begin(), v.end()); return; }
    std::vector<uint64_t> tmp(v.size());
    for (int shift = 0; shift < 55; shift += 11) {
        std::vector<size_t> cnt(2049, 0);
        for (uint64_t x : v) cnt[((x >> shift) & 2047) + 1]++;
        for (int i = 0; i < 2048; i++) cnt[i + 1] += cnt[i];
        for (uint64_t x : v) tmp[cnt[(x >> shift) & 2047]++] = x;
        v.swap(tmp);
    }
}

// The neighbour keys of keys[0..n) (n * NUM_MOVES of them, duplicates and lower layers included), appended to `out`.
static void expand_keys(const uint64_t* keys, size_t n, std::vector<uint64_t>& out) {
    for (size_t i = 0; i < n; i++) {
        const uint64_t k = keys[i];
        const uint32_t lo = k & ALL, hi = (k >> 24) & ALL;
        const int par = (int)(k >> 48);
        for (int m = 0; m < NUM_MOVES; m++) {
            const uint32_t a = MOVE_PERM[m].apply(lo), b = MOVE_PERM[m].apply(hi), c = ALL ^ a ^ b;   // the median is implied
            out.push_back(make_key(a, b, c, par ^ WING_PARITY[m]));
        }
    }
}

// BFS layers 0..depth over canonical keys, from the six rotated copies of S1 (they share one key).
// Beyond depth 6 the last layer is never expanded and its exact set is not kept, so it is NOT sorted or
// deduplicated: its raw neighbour keys (duplicates and lower-layer states included, harmless in a Bloom filter)
// are streamed in chunks from layer depth-1 straight into the filter (`stream_last`; the 280 MB vector of raw keys of
// depth 7 used to be built first), and its size for sizing the filter is estimated from the duplicate ratio of the
// previous layer (about 1.9 for every layer).
static void build_layers(int depth, std::vector<std::vector<uint64_t>>& layers, bool& stream_last, size_t& last_estimate) {
    layers.clear();
    stream_last = false;
    layers.push_back({GOAL_KEY});
    double dup_ratio = 1.9;
    for (int d = 1; d <= depth; d++) {
        if (d == depth && depth > 6) { last_estimate = (size_t)((double)layers.back().size() * NUM_MOVES / dup_ratio); stream_last = true; break; }
        std::vector<uint64_t> next;
        next.reserve(layers.back().size() * NUM_MOVES);
        expand_keys(layers.back().data(), layers.back().size(), next);
        const size_t raw = next.size();
        sort_keys(next);
        next.erase(std::unique(next.begin(), next.end()), next.end());
        dup_ratio = (double)raw / (double)next.size();
        // neighbours of layer d-1 lie in layers d-2, d-1, d
        for (int e = d - 1; e >= std::max(0, d - 2); e--) {
            std::vector<uint64_t> diff;
            std::set_difference(next.begin(), next.end(), layers[e].begin(), layers[e].end(), std::back_inserter(diff));
            next.swap(diff);
        }
        layers.push_back(std::move(next));
    }
}

static void build() {
    if (g_built) return;
    auto t0 = std::chrono::steady_clock::now();
    LapTimer lt;
    init_tables();
    lt.lap("tables (move bit permutations, class masks)");
    const int want = std::min(g_depth, 7);
    bool stream_last = false;
    size_t last_estimate = 0;
    build_layers(want, LAYERS, stream_last, last_estimate);
    g_depth = want;
    lt.lap("exact layers (expand + sort + dedupe)");
    const bool raw_last = stream_last;   // depth 7: layers 0..6 are exact, layer 7 is streamed in unsorted
    for (auto& l : LAYERS) LAYER_SIZES.push_back(l.size());
    BLOOM.assign(g_depth + 1, BlockedBloom());
    size_t cum = 0;
    for (int d = 0; d <= g_depth; d++) {
        const bool last = raw_last && d == g_depth;
        cum += last ? last_estimate : LAYERS[d].size();
        if (d == 0) continue;
        BLOOM[d].init(cum, g_fpr);
        for (int e = 0; e < (last ? d : d + 1); e++) BLOOM[d].add_all(LAYERS[e]);
        if (last) {
            const std::vector<uint64_t>& from = LAYERS.back();
            const size_t CHUNK = (size_t)1 << 15;
            std::vector<uint64_t> chunk;
            chunk.reserve(CHUNK * NUM_MOVES);
            for (size_t i = 0; i < from.size(); i += CHUNK) {
                chunk.clear();
                expand_keys(from.data() + i, std::min(CHUNK, from.size() - i), chunk);
                BLOOM[d].add_all(chunk);
            }
        }
    }
    lt.lap("Bloom filters (init + insert)");
    if (raw_last) LAYER_SIZES.push_back(last_estimate);   // an estimate (not deduplicated)
    g_built = true;
    if (g_depth > 6) { LAYERS.clear(); LAYERS.shrink_to_fit(); }
    g_build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Distance to S1 of a canonical key: exact up to g_depth (up to Bloom false positives, which can only make it
// smaller), g_depth + 1 = "farther than that".
static inline int dist_of_key(uint64_t key) {
    if (key == GOAL_KEY) return 0;
    uint64_t bh; int pos[10];
    BlockedBloom::positions(key, bh, pos, 10);
    if (!BLOOM[g_depth].test_positions(bh, pos)) return g_depth + 1;
    int lo = 1, hi = g_depth;   // smallest k with key in BLOOM[k]
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (BLOOM[mid].test_positions(bh, pos)) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static inline int dist(const MaskState& s) { return dist_of_key(make_key(s.m[0], s.m[1], s.m[2], s.parity)); }

} // namespace p1
