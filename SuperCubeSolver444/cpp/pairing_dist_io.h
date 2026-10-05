// Packed 4-bit-per-entry storage for phase 4's wing-pairing distance
// table, shared between phase4.cpp and chain1234.cpp (both build the
// IDENTICAL table -- same SIGMA_POS/SIGMA_NEG move basis from tables4.h --
// so whichever program builds it first can save it, and the other loads
// the cached file instead of repeating the ~17-22 minute BFS).
//
// Two independent memory-halvings stacked, per the user's request:
//
// 1. DENSE INDEXING (479,001,600 raw indices -> 239,500,800 used ones).
//    Only half of the 12! index space is ever reachable from a genuine S3
//    element (see CLAUDE.md's phase 4 section -- verified against sympy's
//    own group membership test, not assumed): S3's moves always preserve
//    the wing-pairing permutation's parity, so only the even (alternating-
//    group) half is ever produced. Rather than store a separate lookup
//    table to compress out the unreachable half, this exploits a fact
//    about the Lehmer-code ranking itself: swapping the LAST TWO elements
//    of a 12-permutation always flips its parity, and (because the
//    factorial place-value for that swap is exactly 1! = 1, the only odd
//    factorial weight among the 12 digits -- 0! also has weight 1 but its
//    digit is always 0) it changes the raw rank by EXACTLY 1, so raw ranks
//    pair up as (even, even+1) with one member of each pair even-parity
//    and the other odd-parity. `raw_rank >> 1` (integer division) is
//    therefore a valid, collision-free DENSE index in [0, 12!/2) for any
//    permutation -- reachable or not -- and needs no separate mapping
//    table at all, just this one bit shift.
//
// 2. 4-BIT PACKING, capping distances at 15 (spec's own suggestion). The
//    true max distance is 18 (~30% of reachable states are >=16 --
//    checked directly, NOT assumed to be rare); capping to 15 for storage
//    keeps the heuristic admissible (a lower bound can only be an
//    UNDERestimate of a larger true value, never invalid) at the cost of
//    slightly less pruning power for that minority of states.
//
// Total size: 239,500,800 / 2 = 119,750,400 bytes (~119.75 MB, matching
// the "around 120MB" target).

#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

static const int64_t PAIRING_FACT12[13] = {
    1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800, 39916800, 479001600};

// O(n) Lehmer-code rank/unrank/parity for a 12-element permutation, via a
// 4-bit-per-value packed register updated with O(1) bit ops per digit
// instead of an O(n) inner scan (rank_full) or O(n) array shift (unrank_full)
// -- the same technique as TPR-4x4x4-Solver's Edge3.get()/set() (see chat).
// `val`'s nibble at bit-position p*4 holds, for rank_full, the current RANK
// of value p among not-yet-consumed values (== the old "smaller" count,
// since positions 0..i-1 have already been removed and only p[i..11] remain
// -- identical semantics, just read via shift+mask instead of a rescan);
// for unrank_full it holds the inverse: the VALUE currently sitting at a
// given remaining-rank. Subtracting/compacting via the wide constant removes
// one entry and re-indexes every larger entry down by one, in one op.
static inline uint32_t pairing_rank_full(const std::array<uint8_t, 12>& p) {
    uint64_t val = 0xba9876543210ULL;
    uint32_t r = 0;
    for (int i = 0; i < 12; i++) {
        int shift = ((int)p[i]) << 2;
        r = r * (uint32_t)(12 - i) + (uint32_t)((val >> shift) & 0xF);
        val -= 0x111111111110ULL << shift;
    }
    return r;
}

static inline std::array<uint8_t, 12> pairing_unrank_full(uint32_t r) {
    uint64_t val = 0xba9876543210ULL;
    std::array<uint8_t, 12> perm;
    for (int i = 0; i < 12; i++) {
        uint32_t f = (uint32_t)PAIRING_FACT12[11 - i];
        uint32_t sel = r / f;
        r %= f;
        int shift = (int)sel << 2;
        perm[i] = (uint8_t)((val >> shift) & 0xF);
        uint64_t m = (shift == 0) ? 0ULL : ((1ULL << shift) - 1);
        val = (val & m) + ((val >> 4) & ~m);
    }
    return perm;
}

static inline int pairing_parity(const std::array<uint8_t, 12>& p) {
    // Permutation parity == XOR of the Lehmer digits' own parities (each
    // digit IS an inversion count for its position, and total inversions
    // == sum of digits) -- so this reuses rank_full's digit extraction
    // without needing the O(n^2) rescan.
    uint64_t val = 0xba9876543210ULL;
    int parity = 0;
    for (int i = 0; i < 12; i++) {
        int shift = ((int)p[i]) << 2;
        parity ^= (int)((val >> shift) & 0xF) & 1;
        val -= 0x111111111110ULL << shift;
    }
    return parity;
}

static inline uint32_t pairing_dense_index(uint32_t full_rank) { return full_rank >> 1; }

static const uint32_t PAIRING_NUM_DENSE = 239500800;  // 12!/2
static const int PAIRING_TRUE_MAX_KNOWN = 18;         // measured, not assumed -- see CLAUDE.md

struct PairingDistPacked {
    std::vector<uint8_t> packed;  // PAIRING_NUM_DENSE/2 bytes, 2 nibbles each
};

static inline int pairing_get(const PairingDistPacked& P, uint32_t dense_idx) {
    uint8_t b = P.packed[dense_idx >> 1];
    return (dense_idx & 1) ? (b >> 4) : (b & 0x0F);
}

static inline void pairing_set(PairingDistPacked& P, uint32_t dense_idx, int d) {
    uint8_t& b = P.packed[dense_idx >> 1];
    if (dense_idx & 1) b = (uint8_t)((b & 0x0F) | ((d & 0x0F) << 4));
    else                b = (uint8_t)((b & 0xF0) | (d & 0x0F));
}

// Reconstruct THE (unique) reachable permutation for a given dense index --
// only for diagnostics (e.g. materializing a "worst case" state pulled
// from the table); never needed on the search hot path.
static inline std::array<uint8_t, 12> pairing_unrank_dense(uint32_t dense_idx) {
    auto a = pairing_unrank_full(2 * dense_idx);
    if (pairing_parity(a) == 0) return a;
    return pairing_unrank_full(2 * dense_idx + 1);
}

static inline bool pairing_load(const std::string& path, PairingDistPacked& P) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff sz = f.tellg();
    size_t expected = PAIRING_NUM_DENSE / 2;
    if (sz != (std::streamoff)expected) return false;
    f.seekg(0);
    P.packed.resize(expected);
    f.read((char*)P.packed.data(), (std::streamsize)expected);
    return (bool)f;
}

static inline bool pairing_save(const std::string& path, const PairingDistPacked& P) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)P.packed.data(), (std::streamsize)P.packed.size());
    return (bool)f;
}
