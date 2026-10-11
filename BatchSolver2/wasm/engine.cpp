// Batch Solver 2 engine (freestanding C++ -> wasm32).
//
// The JS side prepares a "masked" puzzle (see src/mask.js): n locations, each holding
// a 16-bit value  classRep | (orientation << 8),  and nm moves given as
//     new[p] = old[src[p]] + tw[p]
// where tw is the full twist (mod ori[p]) and the new orientation is reduced modulo k[class of the
// piece that arrives at p] (the number of unique orientations of that piece, see src/mask.js).
//
// This file owns every hot loop:
//   * exact breadth-first enumeration of the states near the target set,
//   * counting (HyperLogLog) and filling of the blocked Bloom filter layers,
//   * IDA* with a single Bloom lookup per node.
//
// No libc, no STL. Memory is a bump allocator (nothing is ever freed; the JS side
// creates a fresh instance for every table build).

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef short i16;
typedef int i32;
typedef double f64;

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

extern "C" {
void* memcpy(void* d, const void* s, unsigned long n) { return __builtin_memcpy(d, s, n); }
void* memset(void* d, int c, unsigned long n) { return __builtin_memset(d, c, n); }
int memcmp(const void* a, const void* b, unsigned long n) {
    const u8 *x = (const u8*)a, *y = (const u8*)b;
    for (; n; n--, x++, y++) if (*x != *y) return (int)*x - (int)*y;
    return 0;
}
}

// ------------------------------------------------------------------ memory ---

extern u8 __heap_base;
static u64 heapPtr = 0, heapEnd = 0;

static void* balloc(u64 bytes) {
    if (heapPtr == 0) {
        heapPtr = (u64)(u32)&__heap_base;
        heapEnd = (u64)__builtin_wasm_memory_size(0) * 65536ull;
    }
    u64 start = (heapPtr + 63) & ~63ull;
    u64 end = start + bytes;
    if (end > 0xFFFF0000ull) return 0;
    if (end > heapEnd) {
        u64 need = (end - heapEnd + 65535) >> 16;
        if (need < 16) need = 16;
        if ((u64)__builtin_wasm_memory_size(0) + need > 65535) need = 65535 - (u64)__builtin_wasm_memory_size(0);
        if (heapEnd + need * 65536ull < end) return 0;
        if (__builtin_wasm_memory_grow(0, (u32)need) == (unsigned long)-1) return 0;
        heapEnd += need * 65536ull;
    }
    heapPtr = end;
    return (void*)(u32)start;
}

template <class T> static T* newArr(u64 count) { return (T*)balloc(count * sizeof(T)); }

EXPORT(wasm_alloc) void* wasm_alloc(u32 bytes) { return balloc(bytes); }
EXPORT(bytes_used) f64 bytes_used() { return (f64)heapPtr; }

// ------------------------------------------------------------------ hashing ---

static inline u64 mix64(u64 x) {
    x ^= x >> 32; x *= 0xD6E8FEB86659FD93ULL;
    x ^= x >> 32; x *= 0xD6E8FEB86659FD93ULL;
    x ^= x >> 32;
    return x;
}

static u64 rngState = 0x243F6A8885A308D3ULL;
static u64 nextRand() {
    u64 z = (rngState += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// ------------------------------------------------------------------ puzzle ---

static const u32 MAXL = 256;   // maximum Bloom layers (= maximum prune cost)

static u32 N = 0, NM = 0, OMAX = 1, ZS = 0;
static u8 *K, *ORI, *SRC, *TW;     // K: per piece class; ORI: per location (full orientation count)
static u8 MODT[16 * 9];             // MODT[o * 9 + k] = o % k
static bool UNIFORM = true;         // fast path: orientation can be reduced modulo K[location]
static const u32 BIG = 1u << 28;    // cost of a move that may not be played in a given direction
static u32 *COST, *BCOST, *INV;
static u32 *chgOff;
static u8 *chg;
static u64 *Z;
static u32 minCost, minBCost, maxBCost;
static u32 W;                      // 64-bit words per move bitset
static u64 *commute;               // NM x W: commute[a] has bit b if a b == b a
static i16 *prodMove;              // NM x NM: -2 identity, -1 none, else index of the equal single move

static u8 *tmpA, *tmpB, *tmpC, *tmpD;

EXPORT(init) u32 init(u32 n, u32 nm, u32 omax) {
    N = n; NM = nm; OMAX = omax; ZS = n * omax;
    for (u32 o = 0; o < 16; o++) for (u32 k = 1; k < 9; k++) MODT[o * 9 + k] = (u8)(o % k);
    K = newArr<u8>(n); ORI = newArr<u8>(n); SRC = newArr<u8>((u64)nm * n); TW = newArr<u8>((u64)nm * n);
    COST = newArr<u32>(nm); BCOST = newArr<u32>(nm); INV = newArr<u32>(nm);
    tmpA = newArr<u8>(n); tmpB = newArr<u8>(n); tmpC = newArr<u8>(n); tmpD = newArr<u8>(n);
    return (K && ORI && SRC && TW && COST && BCOST && INV && tmpA && tmpD) ? 1 : 0;
}
EXPORT(p_k) u8* p_k() { return K; }
EXPORT(p_ori) u8* p_ori() { return ORI; }
EXPORT(uniform_k) u32 uniform_k() { return UNIFORM ? 1 : 0; }
EXPORT(p_src) u8* p_src() { return SRC; }
EXPORT(p_tw) u8* p_tw() { return TW; }
EXPORT(p_cost) u32* p_cost() { return COST; }
EXPORT(p_bcost) u32* p_bcost() { return BCOST; }
EXPORT(p_inv) u32* p_inv() { return INV; }

static inline const u8* mSrc(u32 m) { return SRC + (u64)m * N; }
static inline const u8* mTw(u32 m) { return TW + (u64)m * N; }

// product "a then b" into (os, ot)
static void productOf(u32 a, u32 b, u8* os, u8* ot) {
    const u8 *as = mSrc(a), *at = mTw(a), *bs = mSrc(b), *bt = mTw(b);
    for (u32 p = 0; p < N; p++) {
        u32 q = bs[p];
        os[p] = as[q];
        u32 t = at[q] + bt[p];
        if (t >= ORI[p]) t -= ORI[p];
        ot[p] = (u8)t;
    }
}

static bool isIdentityArr(const u8* s, const u8* t) {
    for (u32 p = 0; p < N; p++) if (s[p] != p || t[p] != 0) return false;
    return true;
}

// ---- move lookup by action
static u32 *mvTab; static u32 mvTabMask;
static u64 actHash(const u8* s, const u8* t) {
    u64 h = 0xcbf29ce484222325ULL;
    for (u32 p = 0; p < N; p++) { h = (h ^ s[p]) * 0x100000001b3ULL; h = (h ^ t[p]) * 0x100000001b3ULL; }
    return mix64(h);
}
static i32 findMove(const u8* s, const u8* t) {
    u32 slot = (u32)actHash(s, t) & mvTabMask;
    for (;;) {
        u32 e = mvTab[slot];
        if (e == 0) return -1;
        u32 m = e - 1;
        const u8 *ms = mSrc(m), *mt = mTw(m);
        bool eq = true;
        for (u32 p = 0; p < N; p++) if (ms[p] != s[p] || mt[p] != t[p]) { eq = false; break; }
        if (eq) return (i32)m;
        slot = (slot + 1) & mvTabMask;
    }
}

// ---- redundancy automaton -------------------------------------------------

struct Auto {
    u32 nctx;      // contexts 0..nctx-1 = (move, run length); nctx = START
    u32 *base;     // per move: first context
    u32 *maxRun;
    u32 *off;      // nctx+2 offsets into mv/nx
    u16 *mv;
    u32 *nx;
};
static Auto AF, AB;      // forward (costs) and backward (costs of inverses)
static u32 *orderOf;
static const u32 RUNCAP = 64;

static bool pairAllowed(u32 a, u32 b, const u32* cost) {
    i16 pm = prodMove[(u64)a * NM + b];
    if (pm == -2) return false;
    if (pm >= 0 && cost[pm] <= cost[a] + cost[b]) return false;
    if ((commute[(u64)a * W + (b >> 6)] >> (b & 63)) & 1) { if (a > b) return false; }
    return true;
}

static bool buildAuto(Auto& A, const u32* cost) {
    A.base = newArr<u32>(NM); A.maxRun = newArr<u32>(NM);
    if (!A.base || !A.maxRun) return false;
    u32 total = 0;
    for (u32 m = 0; m < NM; m++) {
        if (cost[m] >= BIG) { A.maxRun[m] = 0; A.base[m] = total; continue; }
        u32 o = orderOf[m];
        u32 mr = 1;
        // E = m^k for k = 2..
        for (u32 j = 0; j < N; j++) { tmpA[j] = SRC[(u64)m * N + j]; tmpB[j] = TW[(u64)m * N + j]; }
        for (u32 k = 2; k < o && k <= RUNCAP; k++) {
            // tmpA/B = m^(k-1); multiply by m
            const u8 *ms = mSrc(m), *mt = mTw(m);
            for (u32 p = 0; p < N; p++) {
                u32 q = ms[p];
                tmpC[p] = tmpA[q];
                u32 t = tmpB[q] + mt[p]; if (t >= ORI[p]) t -= ORI[p];
                tmpD[p] = (u8)t;
            }
            for (u32 p = 0; p < N; p++) { tmpA[p] = tmpC[p]; tmpB[p] = tmpD[p]; }
            bool ok = true;
            i32 c = findMove(tmpA, tmpB);
            if (c >= 0 && cost[c] <= k * cost[m]) ok = false;
            if (ok) {
                u32 j2 = o - k;                 // m^k == inv^(o-k)
                if (j2 >= 2) {
                    u32 im = INV[m];
                    u64 altCost = (u64)j2 * cost[im], myCost = (u64)k * cost[m];
                    if (altCost < myCost || (altCost == myCost && im < m)) ok = false;
                }
            }
            if (!ok) break;
            mr = k;
        }
        A.maxRun[m] = mr;
        A.base[m] = total;
        total += mr;
    }
    A.nctx = total;
    A.off = newArr<u32>(total + 2);
    if (!A.off) return false;
    // count, then fill
    u32 count = 0;
    for (u32 pass = 0; pass < 2; pass++) {
        count = 0;
        for (u32 ctx = 0; ctx <= total; ctx++) {
            if (pass == 1) A.off[ctx] = count;
            if (ctx == total) { // START
                for (u32 b = 0; b < NM; b++) { if (cost[b] >= BIG) continue; if (pass == 1) { A.mv[count] = (u16)b; A.nx[count] = A.base[b]; } count++; }
                continue;
            }
            // find (m, r) for ctx
            u32 m = 0;
            while (m + 1 < NM && A.base[m + 1] <= ctx) m++;
            u32 r = ctx - A.base[m] + 1;
            for (u32 b = 0; b < NM; b++) {
                if (cost[b] >= BIG) continue;
                if (b == m) {
                    if (r < A.maxRun[m]) { if (pass == 1) { A.mv[count] = (u16)b; A.nx[count] = A.base[m] + r; } count++; }
                } else if (pairAllowed(m, b, cost)) {
                    if (pass == 1) { A.mv[count] = (u16)b; A.nx[count] = A.base[b]; }
                    count++;
                }
            }
        }
        if (pass == 0) { A.mv = newArr<u16>(count + 1); A.nx = newArr<u32>(count + 1); if (!A.mv || !A.nx) return false; }
    }
    A.off[total + 1] = count;
    return true;
}

// ---------------------------------------------------------------- prepare ---

EXPORT(prepare) u32 prepare() {
    W = (NM + 63) / 64;
    // changed locations per move
    chgOff = newArr<u32>(NM + 1);
    u32 tot = 0;
    for (u32 m = 0; m < NM; m++) {
        const u8 *s = mSrc(m), *t = mTw(m);
        for (u32 p = 0; p < N; p++) if (s[p] != p || t[p] != 0) tot++;
    }
    chg = newArr<u8>(tot + 1);
    u32 pos = 0;
    for (u32 m = 0; m < NM; m++) {
        chgOff[m] = pos;
        const u8 *s = mSrc(m), *t = mTw(m);
        for (u32 p = 0; p < N; p++) if (s[p] != p || t[p] != 0) chg[pos++] = (u8)p;
    }
    chgOff[NM] = pos;
    // Fast orientation path if every orbit of locations (under the moves) has one common k.
    {
        u8* orb = newArr<u8>(N);
        if (!orb) return 0;
        for (u32 p = 0; p < N; p++) orb[p] = (u8)p;
        for (bool changed = true; changed; ) {
            changed = false;
            for (u32 m = 0; m < NM; m++) for (u32 p = 0; p < N; p++) {
                u32 q = mSrc(m)[p];
                if (orb[p] != orb[q]) { u8 lo = orb[p] < orb[q] ? orb[p] : orb[q]; orb[p] = orb[q] = lo; changed = true; }
            }
        }
        UNIFORM = true;
        for (u32 p = 0; p < N; p++) if (K[p] != K[orb[p]]) UNIFORM = false;
    }
    // Zobrist
    Z = newArr<u64>((u64)N * ZS);
    if (!Z) return 0;
    for (u64 i = 0; i < (u64)N * ZS; i++) Z[i] = nextRand();
    minCost = 0xFFFFFFFF; minBCost = 0xFFFFFFFF; maxBCost = 0;
    for (u32 m = 0; m < NM; m++) {
        if (COST[m] < BIG && COST[m] < minCost) minCost = COST[m];
        if (BCOST[m] < BIG && BCOST[m] < minBCost) minBCost = BCOST[m];
        if (BCOST[m] < BIG && BCOST[m] > maxBCost) maxBCost = BCOST[m];
    }
    // move table
    u32 cap = 16; while (cap < NM * 4) cap <<= 1;
    mvTab = newArr<u32>(cap); mvTabMask = cap - 1;
    if (!mvTab) return 0;
    for (u32 m = 0; m < NM; m++) {
        u32 slot = (u32)actHash(mSrc(m), mTw(m)) & mvTabMask;
        while (mvTab[slot]) slot = (slot + 1) & mvTabMask;
        mvTab[slot] = m + 1;
    }
    // products and commutation
    commute = newArr<u64>((u64)NM * W);
    prodMove = newArr<i16>((u64)NM * NM);
    if (!commute || !prodMove) return 0;
    for (u32 a = 0; a < NM; a++) {
        for (u32 b = 0; b < NM; b++) {
            productOf(a, b, tmpA, tmpB);
            if (isIdentityArr(tmpA, tmpB)) prodMove[(u64)a * NM + b] = -2;
            else prodMove[(u64)a * NM + b] = (i16)findMove(tmpA, tmpB);
            if (a != b) {
                productOf(b, a, tmpC, tmpD);
                bool eq = true;
                for (u32 p = 0; p < N; p++) if (tmpA[p] != tmpC[p] || tmpB[p] != tmpD[p]) { eq = false; break; }
                if (eq) commute[(u64)a * W + (b >> 6)] |= 1ull << (b & 63);
            }
        }
    }
    // orders
    orderOf = newArr<u32>(NM);
    for (u32 m = 0; m < NM; m++) {
        for (u32 j = 0; j < N; j++) { tmpA[j] = mSrc(m)[j]; tmpB[j] = mTw(m)[j]; }
        u32 o = 1;
        while (!isIdentityArr(tmpA, tmpB) && o < 100000) {
            const u8 *ms = mSrc(m), *mt = mTw(m);
            for (u32 p = 0; p < N; p++) {
                u32 q = ms[p];
                tmpC[p] = tmpA[q];
                u32 t = tmpB[q] + mt[p]; if (t >= ORI[p]) t -= ORI[p];
                tmpD[p] = (u8)t;
            }
            for (u32 p = 0; p < N; p++) { tmpA[p] = tmpC[p]; tmpB[p] = tmpD[p]; }
            o++;
        }
        orderOf[m] = o;
    }
    if (!buildAuto(AF, COST)) return 0;
    if (!buildAuto(AB, BCOST)) return 0;
    return 1;
}
EXPORT(auto_contexts) u32 auto_contexts() { return AF.nctx; }
EXPORT(auto_branching) f64 auto_branching() {      // average successors per context (diagnostics)
    f64 s = 0; for (u32 c = 0; c < AF.nctx; c++) s += AF.off[c + 1] - AF.off[c];
    return AF.nctx ? s / AF.nctx : 0;
}

// ------------------------------------------------------------- state ops ---

static inline u64 hashState(const u16* s) {
    u64 h = 0;
    for (u32 p = 0; p < N; p++) h ^= Z[(u64)p * ZS + (u32)(s[p] & 255) * OMAX + (s[p] >> 8)];
    return h;
}

// Computes the values of the changed locations of `par` after move m and returns the child's hash.
static inline u64 childHash(const u16* par, u64 H, u32 m, u16* nv) {
    const u8 *s = mSrc(m), *t = mTw(m);
    u32 a = chgOff[m], b = chgOff[m + 1];
    for (u32 i = a; i < b; i++) {
        u32 p = chg[i];
        u32 v = par[s[p]] + ((u32)t[p] << 8);
        if (UNIFORM) {
            if ((v >> 8) >= K[p]) v -= (u32)K[p] << 8;         // k is the same for every piece that can reach p
        } else {
            u32 cl = v & 255;
            v = cl | ((u32)MODT[(v >> 8) * 9 + K[cl]] << 8);
        }
        nv[i - a] = (u16)v;
        H ^= Z[(u64)p * ZS + (v & 255) * OMAX + (v >> 8)] ^ Z[(u64)p * ZS + (u32)(par[p] & 255) * OMAX + (par[p] >> 8)];
    }
    return H;
}
static inline void writeChild(u16* dst, const u16* par, u32 m, const u16* nv) {
    __builtin_memcpy(dst, par, N * 2);
    u32 a = chgOff[m], b = chgOff[m + 1];
    for (u32 i = a; i < b; i++) dst[chg[i]] = nv[i - a];
}


// ------------------------------------------------------------ Bloom layers ---

struct Bloom { u64* data; u32 nBlocks; u32 k; };
static Bloom BF[MAXL + 1];
static u32 DEPTH = 0;          // number of Bloom layers (the prune cost)

static inline u32 blockOf(u64 x, u32 nBlocks) { return (u32)(((x >> 32) * (u64)nBlocks) >> 32); }
static inline u32 bitPos(u64 x, u64 h2, u32 j) {
    return j < 3 ? (u32)((x >> (9 * j)) & 511) : (u32)((h2 >> (55 - 9 * (j - 3))) & 511);
}
static inline bool bloomTest(const Bloom& b, u64 x) {
    const u64* w = b.data + (u64)blockOf(x, b.nBlocks) * 8;
    u64 h2 = x * 0x9E3779B97F4A7C15ULL;
    for (u32 j = 0; j < b.k; j++) {
        u32 pos = bitPos(x, h2, j);
        if (!((w[pos >> 6] >> (pos & 63)) & 1)) return false;
    }
    return true;
}
static inline void bloomInsert(const Bloom& b, u64 x) {
    u64* w = b.data + (u64)blockOf(x, b.nBlocks) * 8;
    u64 h2 = x * 0x9E3779B97F4A7C15ULL;
    for (u32 j = 0; j < b.k; j++) {
        u32 pos = bitPos(x, h2, j);
        w[pos >> 6] |= 1ull << (pos & 63);
    }
}

EXPORT(bloom_test) u32 bloom_test(u32 layer, const u16* st) { return bloomTest(BF[layer], mix64(hashState(st))) ? 1 : 0; }
EXPORT(bloom_alloc) u32 bloom_alloc(u32 layer, u32 nBlocks, u32 k) {
    if (layer < 1 || layer > MAXL || k < 1 || k > 10 || nBlocks < 1) return 0;
    BF[layer].data = newArr<u64>((u64)nBlocks * 8);
    BF[layer].nBlocks = nBlocks; BF[layer].k = k;
    if (layer > DEPTH) DEPTH = layer;
    return BF[layer].data ? 1 : 0;
}
EXPORT(bloom_bytes) f64 bloom_bytes(u32 layer) { return (f64)BF[layer].nBlocks * 64.0; }
EXPORT(bloom_fill) f64 bloom_fill(u32 layer) {     // fraction of set bits, for diagnostics
    const Bloom& b = BF[layer];
    u64 set = 0, total = (u64)b.nBlocks * 512;
    for (u64 i = 0; i < (u64)b.nBlocks * 8; i++) set += __builtin_popcountll(b.data[i]);
    return (f64)set / (f64)total;
}

// ----------------------------------------------------- chunked storage ---

static const u32 CSHIFT = 16, CSIZE = 1u << CSHIFT, CMASK = CSIZE - 1;
static const u32 MAXCHUNKS = 65536;

static u16** stChunks;   // states
static u64** hsChunks;   // hashes
static u32** csChunks;   // costs
static u32** nodeChunks; // bucket nodes: pairs (idx, next)
static u32 nEntries = 0, nNodes = 0;

static inline u16* stateAt(u32 i) { return stChunks[i >> CSHIFT] + (u64)(i & CMASK) * N; }
static inline u64& hashAt(u32 i) { return hsChunks[i >> CSHIFT][i & CMASK]; }
static inline u32& costAt(u32 i) { return csChunks[i >> CSHIFT][i & CMASK]; }
static inline u32* nodeAt(u32 i) { return nodeChunks[i >> CSHIFT] + (u64)(i & CMASK) * 2; }

static bool ensureEntry() {
    u32 c = nEntries >> CSHIFT;
    if (c >= MAXCHUNKS) return false;
    if (!stChunks[c]) {
        stChunks[c] = newArr<u16>((u64)CSIZE * N); hsChunks[c] = newArr<u64>(CSIZE); csChunks[c] = newArr<u32>(CSIZE);
        if (!stChunks[c] || !hsChunks[c] || !csChunks[c]) return false;
    }
    return true;
}
static bool ensureNode() {
    u32 c = nNodes >> CSHIFT;
    if (c >= MAXCHUNKS) return false;
    if (!nodeChunks[c]) { nodeChunks[c] = newArr<u32>((u64)CSIZE * 2); if (!nodeChunks[c]) return false; }
    return true;
}

// visited table
static u64* vKeys; static u32* vIdx; static u32 vCap, vMask, vCount;
static const u32 EMPTY = 0xFFFFFFFFu;

static bool vAlloc(u32 cap) {
    u64* nk = newArr<u64>(cap); u32* ni = newArr<u32>(cap);
    if (!nk || !ni) return false;
    for (u32 i = 0; i < cap; i++) ni[i] = EMPTY;
    u64* ok = vKeys; u32* oi = vIdx; u32 ocap = vCap;
    vKeys = nk; vIdx = ni; vCap = cap; vMask = cap - 1;
    if (oi) {
        for (u32 i = 0; i < ocap; i++) {
            if (oi[i] != EMPTY) {
                u32 slot = (u32)ok[i] & vMask;
                while (vIdx[slot] != EMPTY) slot = (slot + 1) & vMask;
                vKeys[slot] = ok[i]; vIdx[slot] = oi[i];
            }
        }
    }
    return true;
}

// returns entry index if present else EMPTY; slot out for insertion
static inline u32 vFind(u64 key, const u16* st, u32& slotOut) {
    u32 slot = (u32)key & vMask;
    for (;;) {
        u32 e = vIdx[slot];
        if (e == EMPTY) { slotOut = slot; return EMPTY; }
        if (vKeys[slot] == key && memcmp(stateAt(e), st, N * 2) == 0) return e;
        slot = (slot + 1) & vMask;
    }
}

static u32 *bucketHead; static u32 bucketCap;
static u32 bfsMax = 0;
static u32 *lvlCount;

static bool pushBucket(u32 c, u32 idx) {
    if (!ensureNode()) return false;
    u32* nd = nodeAt(nNodes);
    nd[0] = idx; nd[1] = bucketHead[c];
    bucketHead[c] = nNodes + 1;     // 0 = empty
    nNodes++;
    return true;
}

static u32 tgtCount = 0;
static u16* tgtStates; static u64* tgtHash;
static u32* tgtTab; static u32 tgtMask;

EXPORT(set_targets) u32 set_targets(const u16* states, u32 count) {
    tgtCount = count;
    tgtStates = newArr<u16>((u64)count * N);
    tgtHash = newArr<u64>(count);
    u32 cap = 16; while (cap < count * 4) cap <<= 1;
    tgtTab = newArr<u32>(cap); tgtMask = cap - 1;
    if (!tgtStates || !tgtHash || !tgtTab) return 0;
    __builtin_memcpy(tgtStates, states, (u64)count * N * 2);
    for (u32 i = 0; i < count; i++) {
        tgtHash[i] = hashState(tgtStates + (u64)i * N);
        u32 slot = (u32)tgtHash[i] & tgtMask;
        while (tgtTab[slot]) slot = (slot + 1) & tgtMask;
        tgtTab[slot] = i + 1;
    }
    return 1;
}
static inline i32 targetFind(u64 h, const u16* st) {
    u32 slot = (u32)h & tgtMask;
    for (;;) {
        u32 e = tgtTab[slot];
        if (!e) return -1;
        if (tgtHash[e - 1] == h && memcmp(tgtStates + (u64)(e - 1) * N, st, N * 2) == 0) return (i32)(e - 1);
        slot = (slot + 1) & tgtMask;
    }
}

// ------------------------------------------------------------- exact BFS ---

EXPORT(bfs_init) u32 bfs_init(u32 maxCost) {
    bfsMax = maxCost;
    stChunks = newArr<u16*>(MAXCHUNKS); hsChunks = newArr<u64*>(MAXCHUNKS);
    csChunks = newArr<u32*>(MAXCHUNKS); nodeChunks = newArr<u32*>(MAXCHUNKS);
    bucketCap = maxCost + maxBCost + 2;
    bucketHead = newArr<u32>(bucketCap); lvlCount = newArr<u32>(bucketCap);
    if (!stChunks || !hsChunks || !csChunks || !nodeChunks || !bucketHead || !lvlCount) return 0;
    vKeys = 0; vIdx = 0; vCount = 0;
    if (!vAlloc(1u << 12)) return 0;
    for (u32 i = 0; i < tgtCount; i++) {
        u32 slot;
        const u16* st = tgtStates + (u64)i * N;
        if (vFind(tgtHash[i], st, slot) != EMPTY) continue;
        if (!ensureEntry()) return 0;
        u32 idx = nEntries++;
        __builtin_memcpy(stateAt(idx), st, N * 2);
        hashAt(idx) = tgtHash[i]; costAt(idx) = 0;
        vKeys[slot] = tgtHash[i]; vIdx[slot] = idx; vCount++;
        if (!pushBucket(0, idx)) return 0;
    }
    return 1;
}

// Finishes cost level c (all its states are final once earlier levels were expanded);
// expands them unless `expand` is 0. Returns the number of states of cost exactly c, or EMPTY on OOM.
EXPORT(bfs_level) u32 bfs_level(u32 c, u32 expand) {
    u32 count = 0;
    u16 nv[512];
    u16* child = newArr<u16>(N);
    if (!child) return EMPTY;
    for (u32 node = bucketHead[c]; node; ) {
        u32* nd = nodeAt(node - 1);
        u32 idx = nd[0];
        node = nd[1];
        if (costAt(idx) != c) continue;
        count++;
        if (!expand) continue;
        for (u32 m = 0; m < NM; m++) {
            if (BCOST[m] >= BIG) continue;
            u32 cc = c + BCOST[m];
            if (cc > bfsMax) continue;
            const u16* par = stateAt(idx);
            u64 h = childHash(par, hashAt(idx), m, nv);
            writeChild(child, par, m, nv);
            u32 slot;
            u32 e = vFind(h, child, slot);
            if (e == EMPTY) {
                if (vCount * 2 >= vCap) {
                    if (!vAlloc(vCap * 2)) return EMPTY;
                    vFind(h, child, slot);
                }
                if (!ensureEntry()) return EMPTY;
                u32 ni = nEntries++;
                __builtin_memcpy(stateAt(ni), child, N * 2);
                hashAt(ni) = h; costAt(ni) = cc;
                vKeys[slot] = h; vIdx[slot] = ni; vCount++;
                if (!pushBucket(cc, ni)) return EMPTY;
            } else if (costAt(e) > cc) {
                costAt(e) = cc;
                if (!pushBucket(cc, e)) return EMPTY;
            }
        }
    }
    lvlCount[c] = count;
    return count;
}
EXPORT(bfs_total) u32 bfs_total() { return nEntries; }

// cumulative number of exact states with cost <= c
EXPORT(bfs_cum) f64 bfs_cum(u32 c) {
    f64 s = 0;
    for (u32 i = 0; i < nEntries; i++) if (costAt(i) <= c) s++;
    return s;
}

// Inserts every exact state into the Bloom layers that should contain it.
EXPORT(bloom_fill_exact) void bloom_fill_exact() {
    for (u32 i = 0; i < nEntries; i++) {
        u32 c = costAt(i);
        u64 x = mix64(hashAt(i));
        for (u32 layer = (c < 1 ? 1 : c); layer <= DEPTH; layer++) if (BF[layer].data) bloomInsert(BF[layer], x);
    }
}

// ----------------------------------------------------- extension passes ---

static u32 extL = 0, extD = 0;
static u32* frontier; static u32 frontierCount;
static u8* hll;                      // (extD - extL + 1) layers of 65536 registers; layer 0 = exact states
static const u32 HLLN = 65536;
static u16* extStack; static u32* extCtx; static u32* extIter; static u32* extG; static u64* extH; static u32 extCap;
static f64 extNodes = 0;

static inline void hllAdd(u8* regs, u64 x) {
    u32 idx = (u32)(x >> 48);
    u64 w = (x << 16) | (1ull << 15);
    u8 rank = (u8)(__builtin_clzll(w) + 1);
    if (regs[idx] < rank) regs[idx] = rank;
}

EXPORT(ext_begin) u32 ext_begin(u32 L, u32 D) {
    extL = L; extD = D;
    frontierCount = 0;
    u32 lo = (L + 1 > maxBCost) ? L + 1 - maxBCost : 0;
    for (u32 i = 0; i < nEntries; i++) if (costAt(i) >= lo && costAt(i) <= L) frontierCount++;
    frontier = newArr<u32>(frontierCount + 1);
    u32 j = 0;
    for (u32 i = 0; i < nEntries; i++) if (costAt(i) >= lo && costAt(i) <= L) frontier[j++] = i;
    extCap = D + 2;
    extStack = newArr<u16>((u64)extCap * N);
    extCtx = newArr<u32>(extCap); extIter = newArr<u32>(extCap); extG = newArr<u32>(extCap); extH = newArr<u64>(extCap);
    hll = newArr<u8>((u64)(D - L + 1) * HLLN);
    if (!frontier || !extStack || !extCtx || !extIter || !extG || !extH || !hll) return 0;
    for (u32 i = 0; i < nEntries; i++) hllAdd(hll, mix64(hashAt(i)));
    return frontierCount;
}
EXPORT(hll_ptr) u8* hll_ptr() { return hll; }
EXPORT(ext_frontier_count) u32 ext_frontier_count() { return frontierCount; }
EXPORT(ext_nodes) f64 ext_nodes() { return extNodes; }

// mode 1: count (HyperLogLog)   mode 2: insert into Bloom layers
// Processes frontier states [from, from+count). Returns the next index.
EXPORT(ext_run) u32 ext_run(u32 mode, u32 from, u32 count) {
    u32 end = from + count; if (end > frontierCount) end = frontierCount;
    u16 nv[512];
    for (u32 fi = from; fi < end; fi++) {
        u32 f = frontier[fi];
        u32 cf = costAt(f);
        if (cf + minBCost > extD) continue;
        __builtin_memcpy(extStack, stateAt(f), N * 2);
        u32 d = 0;
        extCtx[0] = AB.nctx; extIter[0] = AB.off[AB.nctx]; extG[0] = cf; extH[0] = hashAt(f);
        for (;;) {
            if (extIter[d] >= AB.off[extCtx[d] + 1]) { if (d == 0) break; d--; continue; }
            u32 e = extIter[d]++;
            u32 m = AB.mv[e];
            u32 gc = extG[d] + BCOST[m];
            if (gc > extD) continue;
            const u16* par = extStack + (u64)d * N;
            u64 h = childHash(par, extH[d], m, nv);
            extNodes += 1;
            if (gc > extL) {
                u64 x = mix64(h);
                if (mode == 1) hllAdd(hll + (u64)(gc - extL) * HLLN, x);
                else for (u32 layer = gc; layer <= extD; layer++) if (BF[layer].data) bloomInsert(BF[layer], x);
            }
            if (gc + minBCost <= extD) {
                writeChild(extStack + (u64)(d + 1) * N, par, m, nv);
                extCtx[d + 1] = AB.nx[e]; extIter[d + 1] = AB.off[AB.nx[e]]; extG[d + 1] = gc; extH[d + 1] = h;
                d++;
            }
        }
    }
    return end;
}

// ----------------------------------------------------------------- search ---

static u64* preMask; static u64* postMask;   // move bitsets (W words), or null
static u8* commuteAnyPost;                   // per move: commutes with some post-adjust move
static u16* sStack; static u32* sCtx; static u32* sIter; static u32* sG; static u64* sH; static u64* sCm; static u16* sPath;
static u32 sT = 0, sMaxDepth = 0, sDepth = 0;
static bool sActive = false;
static u16* outBuf; static u32 outCap, outPos;
static f64 sNodes = 0;

EXPORT(search_alloc) u32 search_alloc(u32 maxDepth, u32 outWords) {
    sMaxDepth = maxDepth;
    sStack = newArr<u16>((u64)(maxDepth + 2) * N);
    sCtx = newArr<u32>(maxDepth + 2); sIter = newArr<u32>(maxDepth + 2); sG = newArr<u32>(maxDepth + 2);
    sH = newArr<u64>(maxDepth + 2); sCm = newArr<u64>((u64)(maxDepth + 2) * W); sPath = newArr<u16>(maxDepth + 2);
    outBuf = newArr<u16>(outWords); outCap = outWords; outPos = 0;
    preMask = newArr<u64>(W); postMask = newArr<u64>(W); commuteAnyPost = newArr<u8>(NM + 1);
    return (sStack && sCtx && sIter && sG && sH && sCm && sPath && outBuf && preMask && postMask && commuteAnyPost) ? 1 : 0;
}
EXPORT(p_pre_mask) u64* p_pre_mask() { return preMask; }
EXPORT(p_post_mask) u64* p_post_mask() { return postMask; }
EXPORT(p_out) u16* p_out() { return outBuf; }
EXPORT(out_len) u32 out_len() { return outPos; }
EXPORT(out_clear) void out_clear() { outPos = 0; }
EXPORT(search_nodes) f64 search_nodes() { return sNodes; }

EXPORT(search_prepare_masks) void search_prepare_masks() {
    for (u32 m = 0; m < NM; m++) {
        bool any = false;
        for (u32 b = 0; b < NM && !any; b++) {
            if ((postMask[b >> 6] >> (b & 63)) & 1) {
                if ((commute[(u64)m * W + (b >> 6)] >> (b & 63)) & 1) any = true;
            }
        }
        commuteAnyPost[m] = any;
    }
}

// Starts a search of exactly cost T from the given root state. Returns 0 if the root is already
// pruned by the Bloom heuristic (nothing to find at this threshold).
EXPORT(search_begin) u32 search_begin(const u16* root, u32 T) {
    sT = T; sDepth = 0; sActive = true;
    if (T > sMaxDepth) { sActive = false; return 0; }
    u64 h = hashState(root);
    if (T >= 1 && T <= DEPTH && BF[T].data && !bloomTest(BF[T], mix64(h))) { sActive = false; return 0; }
    if (T == 0) {
        sActive = false;
        i32 t = targetFind(h, root);
        if (t >= 0 && outPos + 3 <= outCap) { outBuf[outPos++] = 0; outBuf[outPos++] = (u16)t; }
        return 0;
    }
    __builtin_memcpy(sStack, root, N * 2);
    sCtx[0] = AF.nctx; sIter[0] = AF.off[AF.nctx]; sG[0] = 0; sH[0] = h;
    for (u32 w = 0; w < W; w++) sCm[w] = ~0ull;
    return 1;
}

static bool postEndOk(u32 len) {
    // reject if some post-adjust move could be commuted to the end of the sequence
    for (i32 i = (i32)len - 1; i >= 0; i--) {
        u32 m = sPath[i];
        if ((postMask[m >> 6] >> (m & 63)) & 1) {
            bool all = true;
            for (u32 j = i + 1; j < len; j++) {
                u32 o = sPath[j];
                if (!((commute[(u64)m * W + (o >> 6)] >> (o & 63)) & 1)) { all = false; break; }
            }
            if (all) return false;
        }
        if (!commuteAnyPost[m]) break;
    }
    return true;
}

// Returns 0 = node budget used up (call again), 1 = finished, 2 = output buffer full (drain, call again)
EXPORT(search_step) u32 search_step(f64 budget) {
    if (!sActive) return 1;
    u16 nv[512];
    u32 d = sDepth;
    u32 T = sT;
    bool usePre = false;
    for (u32 w = 0; w < W; w++) if (preMask[w]) { usePre = true; break; }
    u16* tmp = sStack + (u64)(sMaxDepth + 1) * N;
    while (budget > 0) {
        if (outPos + T + 4 > outCap) { sDepth = d; return 2; }
        if (sIter[d] >= AF.off[sCtx[d] + 1]) {
            if (d == 0) { sActive = false; sDepth = 0; return 1; }
            d--;
            continue;
        }
        u32 e = sIter[d]++;
        u32 m = AF.mv[e];
        u32 gc = sG[d] + COST[m];
        if (gc > T) continue;
        if (usePre && ((preMask[m >> 6] >> (m & 63)) & 1) && ((sCm[(u64)d * W + (m >> 6)] >> (m & 63)) & 1)) continue;
        const u16* par = sStack + (u64)d * N;
        u64 h = childHash(par, sH[d], m, nv);
        budget -= 1; sNodes += 1;
        u32 rem = T - gc;
        if (rem == 0) {
            // cheap hash filter first
            u32 slot = (u32)h & tgtMask;
            bool maybe = false;
            for (;;) {
                u32 te = tgtTab[slot];
                if (!te) break;
                if (tgtHash[te - 1] == h) { maybe = true; break; }
                slot = (slot + 1) & tgtMask;
            }
            if (!maybe) continue;
            writeChild(tmp, par, m, nv);
            i32 ti = targetFind(h, tmp);
            if (ti < 0) continue;
            sPath[d] = (u16)m;
            if (!postEndOk(d + 1)) continue;
            outBuf[outPos++] = (u16)(d + 1);
            outBuf[outPos++] = (u16)ti;
            for (u32 i = 0; i <= d; i++) outBuf[outPos++] = sPath[i];
            continue;
        }
        if (rem <= DEPTH && BF[rem].data && !bloomTest(BF[rem], mix64(h))) continue;
        if (d + 1 > sMaxDepth) continue;
        writeChild(sStack + (u64)(d + 1) * N, par, m, nv);
        sPath[d] = (u16)m;
        sCtx[d + 1] = AF.nx[e]; sIter[d + 1] = AF.off[AF.nx[e]]; sG[d + 1] = gc; sH[d + 1] = h;
        if (usePre) for (u32 w = 0; w < W; w++) sCm[(u64)(d + 1) * W + w] = sCm[(u64)d * W + w] & commute[(u64)m * W + w];
        d++;
    }
    sDepth = d;
    return 0;
}

