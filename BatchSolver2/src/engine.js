// JS wrapper around wasm/engine.wasm.
//
// One Engine instance == one WebAssembly instance == one set of endtables
// (the wasm allocator never frees, so a new table set gets a fresh instance).

import { BatchError } from "./puzzle.js";

const EMPTY = 0xFFFFFFFF;

/** Compiles the module once; pass `bytes` (ArrayBuffer/Uint8Array) or a URL string. */
export async function compileEngine(source) {
    if (typeof source === "string") {
        if (typeof process !== "undefined" && process.versions && process.versions.node && !/^https?:/.test(source)) {
            const fs = await import("node:fs");
            const bytes = fs.readFileSync(source.startsWith("file:") ? new URL(source) : source);
            return WebAssembly.compile(bytes);
        }
        const resp = await fetch(source);
        if (WebAssembly.compileStreaming && resp.headers.get("content-type") === "application/wasm") return WebAssembly.compileStreaming(resp);
        return WebAssembly.compile(await resp.arrayBuffer());
    }
    return WebAssembly.compile(source);
}

// ------------------------------------------------------------ Bloom sizing ---

const BLOCK_BITS = 512;
const MAX_WASM_BYTES = 4294901760;   // matches --max-memory in wasm/build.sh

/** False-positive rate of a blocked Bloom filter with `lambda` keys per 512-bit block and k bits/key. */
export function blockedFpr(lambda, k) {
    let p = Math.exp(-lambda), fpr = 0;
    const jmax = Math.ceil(lambda + 10 * Math.sqrt(lambda) + 20);
    for (let j = 0; j <= jmax; j++) {
        if (j > 0) p *= lambda / j;
        const fill = 1 - Math.pow(1 - 1 / BLOCK_BITS, k * j);
        fpr += p * Math.pow(fill, k);
    }
    return fpr;
}

/** Smallest blocked filter (number of blocks, bits per key k) holding n keys with false-positive rate <= target. */
export function bloomParams(n, target) {
    let best = null;
    for (let k = 3; k <= 10; k++) {
        let lo = 0.5, hi = 120;       // keys per block
        for (let it = 0; it < 40; it++) {
            const mid = (lo + hi) / 2;
            if (blockedFpr(mid, k) <= target) lo = mid; else hi = mid;
        }
        if (!best || lo > best.lambda) best = { lambda: lo, k };
    }
    const nBlocks = Math.max(1, Math.ceil(Math.max(n, 1) / best.lambda));
    return { nBlocks, k: best.k, bitsPerKey: (nBlocks * BLOCK_BITS) / Math.max(n, 1) };
}

// ------------------------------------------------------------------ Engine ---

/**
 * wasm i32 results arrive in JS as signed numbers, so a pointer above 2 GB (or the 0xFFFFFFFF "out of
 * memory" marker) would be negative. Everything the engine returns is unsigned, so reinterpret
 * negative integers as unsigned. (Non-integer f64 results and non-functions pass through untouched.)
 */
function unsigned(exports) {
    const out = {};
    for (const [name, value] of Object.entries(exports)) {
        out[name] = typeof value !== "function" ? value : (...args) => {
            const r = value(...args);
            return typeof r === "number" && r < 0 && Number.isInteger(r) ? r >>> 0 : r;
        };
    }
    return out;
}

export class Engine {
    /**
     * @param {WebAssembly.Module} module
     * @param {{n:number, k:Uint8Array, ori:Uint8Array, moves:{src:Uint8Array,tw:Uint8Array}[], cost:number[], bcost:number[], inv:number[]}} puz  masked puzzle (see model.buildSubgroup)
     */
    constructor(module, puz) {
        this.puz = puz;
        this.n = puz.n;
        this.nm = puz.moves.length;
        this.x = unsigned(new WebAssembly.Instance(module, {}).exports);
        const omax = Math.max(...puz.k);
        if (!this.x.init(this.n, this.nm, omax)) throw new BatchError("Out of memory while creating the puzzle.");
        const mem = () => new Uint8Array(this.x.memory.buffer);
        mem().set(puz.k, this.x.p_k());
        mem().set(puz.ori, this.x.p_ori());
        const srcPtr = this.x.p_src(), twPtr = this.x.p_tw();
        puz.moves.forEach((mv, i) => {
            mem().set(mv.src, srcPtr + i * this.n);
            mem().set(mv.tw, twPtr + i * this.n);
        });
        new Uint32Array(this.x.memory.buffer, this.x.p_cost(), this.nm).set(puz.cost);
        new Uint32Array(this.x.memory.buffer, this.x.p_bcost(), this.nm).set(puz.bcost);
        new Uint32Array(this.x.memory.buffer, this.x.p_inv(), this.nm).set(puz.inv);
        if (!this.x.prepare()) throw new BatchError("Out of memory while preparing the puzzle.");
        this.depth = 0;
        this.stats = {};
    }

    u16(ptr, len) { return new Uint16Array(this.x.memory.buffer, ptr, len); }
    u32(ptr, len) { return new Uint32Array(this.x.memory.buffer, ptr, len); }

    setTargets(states) {
        const n = this.n;
        const ptr = this.x.wasm_alloc(states.length * n * 2);
        if (!ptr) throw new BatchError("Out of memory.");
        states.forEach((s, i) => this.u16(ptr + i * n * 2, n).set(s));
        if (!this.x.set_targets(ptr, states.length)) throw new BatchError("Out of memory.");
        this.numTargets = states.length;
    }

    /**
     * Builds the endtables: Bloom layers 1..D, where layer i holds every state within cost i of solved.
     *
     * Exact phase: breadth-first search stores the states of cost 0, 1, ... L and fills layers 1..L from them. L is the
     * first depth (at least 1) at which the projected number of states of the next depth exceeds `exactLimit`.
     * Extension phase: layer L+j is filled by walking every move sequence of cost <= j from the exact states, and the
     * size of a layer is projected from the two before it: n(i+1) ~ n(i)^2 / n(i-1) (counts grow roughly
     * exponentially). The real size of a filled layer is read back from its fill ratio; if the projection was
     * far too low the layer is rebuilt larger.
     * @param {{prune:number|{states:number}, exactLimit?:number, fpr?:number, fprLast?:number, onProgress?:(what:string, detail?:string)=>void}} opt
     * prune: depth (cost), or {states: limit}: use the deepest layer that is projected to hold at most `limit` states.
     */
    buildTables(opt) {
        const x = this.x;
        const exactLimit = opt.exactLimit === undefined ? 5e5 : opt.exactLimit;
        const fpr = opt.fpr === undefined ? 1e-3 : opt.fpr;
        const fprLast = opt.fprLast === undefined ? 1e-2 : opt.fprLast;
        const say = opt.onProgress || (() => {});
        const sized = typeof opt.prune === "object";
        const budget = sized ? opt.prune.states : Infinity;
        const want = sized ? Infinity : opt.prune;          // requested depth
        if (!sized && (want < 0 || want > 255 || !Number.isInteger(want))) throw new BatchError('"' + opt.prune + '" is not a valid prune depth.');
        const project = (n1, n2) => n1 * n1 / Math.max(n2, 1);   // size of the next layer from the two before it
        const gb = (v) => (v / 1073741824).toFixed(1) + " GB";
        const tooBig = (depth, bytes) => new BatchError("Prune depth " + depth + " would need about " + gb(bytes + x.bytes_used()) + " of memory for the endtables, but WebAssembly can address at most 4 GB. " +
            "Use a smaller prune depth" + (depth > 1 ? " (such as " + (depth - 1) + ")" : "") + " or a larger Bloom false-positive rate.");

        // ---- exact phase
        if (!x.bfs_init(sized ? 255 : want)) throw new BatchError("Out of memory while building the prune table.");
        const cum = [];          // cumulative exact counts
        let L = -1, D = -1;      // L: deepest exact level; D: deepest layer. D <= L when everything is exact
        for (let level = 0; ; level++) {
            say("Exact states", "depth " + level);
            const c = x.bfs_level(level, 0);
            if (c === EMPTY) throw new BatchError("Out of memory while building the prune table.");
            const n = (cum.length ? cum[cum.length - 1] : 0) + c;
            cum.push(n);
            if (!sized && level >= want) { L = D = want; break; }
            if (sized && level === 255) { L = D = 255; break; }
            if (sized && level >= 1 && n > budget) { L = level; D = level - 1; break; }
            if (sized && level === 0 && n > budget) { L = 0; D = 0; break; }
            const proj = level >= 1 ? project(n, cum[level - 1]) : 0;
            if (level >= 1) {
                if (sized && proj > budget) { L = D = level; break; }
                if (proj > exactLimit) { L = level; break; }
            }
            if (x.bfs_level(level, 1) === EMPTY) throw new BatchError("Out of memory while building the prune table.");
            if (x.bfs_total() === n) {                     // nothing lies deeper: every state is exact
                L = level;
                if (sized) D = level;
                else { while (cum.length <= want) cum.push(n); L = D = want; }
                break;
            }
        }
        this.stats.exactStates = cum[Math.min(L, cum.length - 1)];

        // ---- sizes and allocation of the exact layers
        const cnt = cum.slice(0, L + 1);       // layer sizes: exact up to L, then measured
        this.stats.layers = [];
        let bytes = 0;
        const room = () => MAX_WASM_BYTES - x.bytes_used() - (64 << 20);
        const allocLayer = (i, n, target) => {
            const p = bloomParams(n, target);
            if (p.nBlocks * 64 > room()) throw tooBig(D > 0 ? D : i, bytes + p.nBlocks * 64);
            if (!x.bloom_alloc(i, p.nBlocks, p.k)) throw new BatchError("Out of memory while building the endtables. Try a smaller prune depth.");
            return p;
        };
        const record = (i, p, keys, prev) => {
            if (prev) bytes -= prev.nBlocks * 64;
            bytes += p.nBlocks * 64;
            this.stats.layers[i - 1] = { layer: i, keys, blocks: p.nBlocks, k: p.k, bitsPerKey: p.bitsPerKey };
        };
        const exactTop = D >= 0 ? Math.min(D, L) : L;     // D < 0: still unknown, but then D > L
        if (D < 0 && !sized) D = want;                    // depth mode: the exact phase ended early by switching to extension
        // Refuse early if the projected layers cannot fit in memory.
        if (!sized && L < D) {
            let a = cum[L - 1], b = cum[L], total = 0;
            for (let i = 1; i <= L; i++) total += bloomParams(cum[i], fpr).nBlocks * 64;
            for (let i = L + 1; i <= D; i++) {
                const n = project(b, a);
                total += bloomParams(n, i === D ? fprLast : fpr).nBlocks * 64;
                a = b; b = n;
            }
            if (total > room()) throw tooBig(D, total);
        }
        for (let i = 1; i <= exactTop; i++) {
            const last = D >= 0 && i === D;
            const p = allocLayer(i, cnt[i], last ? fprLast : fpr);
            x.bloom_fill_exact(i);
            record(i, p, cnt[i]);
        }

        // ---- extension phase
        if (L >= 1 && (D < 0 || D > L)) {
            const frontierCount = x.ext_begin(L);
            if (frontierCount === EMPTY) throw new BatchError("Out of memory while building the endtables.");
            const chunk = Math.max(1, Math.min(2000, Math.ceil(frontierCount / 200)));
            for (let layer = L + 1; ; layer++) {
                if (!sized && layer > want) break;
                const est = project(cnt[layer - 1], cnt[layer - 2]);
                if (sized && est > budget) { D = layer - 1; break; }
                const last = sized ? project(est, cnt[layer - 1]) > budget : layer === want;
                const target = last ? fprLast : fpr;
                let n = est, p = null, nHat = 0;
                for (let attempt = 0; attempt < 4; attempt++) {
                    const prev = p;
                    p = allocLayer(layer, n, target);
                    x.bloom_fill_exact(layer);
                    let done = 0;
                    while (done < frontierCount) {
                        done = x.ext_run(layer, done, chunk);
                        say("Filling endtables", "depth " + layer + ", " + Math.floor(100 * done / Math.max(1, frontierCount)) + "%");
                    }
                    // read the real size back from the fraction of bits set
                    const q = 1 - Math.pow(1 - 1 / BLOCK_BITS, p.k);
                    const frac = x.bloom_fill(layer);
                    const saturated = frac > 0.97;
                    nHat = saturated ? n * 4 : -Math.log(1 - frac) / q * p.nBlocks;
                    record(layer, p, nHat, prev);
                    if (saturated) { n = n * 4; continue; }
                    if (blockedFpr(nHat / p.nBlocks, p.k) > 2.5 * target) { n = nHat * 1.05; continue; }
                    break;
                }
                cnt[layer] = nHat;
                D = layer;
                if (sized && last) break;
            }
            this.stats.countNodes = x.ext_nodes();
        }
        this.stats.layerCounts = cnt;
        this.stats.bloomBytes = bytes;
        this.stats.memoryBytes = x.bytes_used();
        this.depth = D;
        this.exactDepth = L;
        this.stats.depth = D;
        return this.stats;
    }

    /** Registers pre-/post-adjust move sets (arrays of move indices) and allocates the search stack. */
    prepareSearch(maxDepth, preMoves, postMoves, outWords = 1 << 18) {
        const x = this.x;
        if (!x.search_alloc(maxDepth, outWords)) throw new BatchError("Out of memory.");
        const W = Math.ceil(this.nm / 32);
        const pre = this.u32(x.p_pre_mask(), W + (W & 1)), post = this.u32(x.p_post_mask(), W + (W & 1));
        pre.fill(0); post.fill(0);
        for (const m of preMoves) pre[m >> 5] |= 1 << (m & 31);
        for (const m of postMoves) post[m >> 5] |= 1 << (m & 31);
        x.search_prepare_masks();
        this.rootPtr = x.wasm_alloc(this.n * 2);
        this.maxDepth = maxDepth;
    }

    /**
     * Generator: finds all solutions of exactly cost T from `start` (a masked state).
     * Yields arrays of {moves:number[], target:number}.
     */
    *search(start, T, nodeChunk = 2e6) {
        const x = this.x;
        this.u16(this.rootPtr, this.n).set(start);
        x.out_clear();
        const begun = x.search_begin(this.rootPtr, T);
        const drain = () => {
            const len = x.out_len();
            const out = [];
            if (len) {
                const buf = this.u16(x.p_out(), len);
                for (let i = 0; i < len; ) {
                    const l = buf[i], t = buf[i + 1];
                    out.push({ moves: Array.from(buf.subarray(i + 2, i + 2 + l)), target: t });
                    i += 2 + l;
                }
                x.out_clear();
            }
            return out;
        };
        if (!begun) { const s = drain(); if (s.length) yield s; return; }
        for (;;) {
            const status = x.search_step(nodeChunk);
            const s = drain();
            if (s.length) yield s;
            if (status === 1) return;
            if (status === 0 && !s.length) yield [];       // give the caller a chance to report progress / stop
        }
    }

    get nodes() { return this.x.search_nodes(); }
}
