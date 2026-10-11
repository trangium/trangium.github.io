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

/** HyperLogLog cardinality estimate from `m` 6-bit registers. */
export function hllEstimate(regs) {
    const m = regs.length;
    let sum = 0, zeros = 0;
    for (let i = 0; i < m; i++) {
        sum += Math.pow(2, -regs[i]);
        if (regs[i] === 0) zeros++;
    }
    const alpha = 0.7213 / (1 + 1.079 / m);
    let est = alpha * m * m / sum;
    if (est <= 2.5 * m && zeros > 0) est = m * Math.log(m / zeros);
    return est;
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
     * Builds the endtables: exact states up to cost D-K, Bloom layers 1..D.
     * @param {{prune:number|{states:number}, extension?:number, fpr?:number, fprLast?:number, onProgress?:(msg:string)=>void}} opt
     * prune: depth (cost), or {states: limit} to choose the depth from a state budget.
     */
    buildTables(opt) {
        const x = this.x;
        const K = opt.extension === undefined ? 4 : opt.extension;
        const fpr = opt.fpr === undefined ? 1e-3 : opt.fpr;
        const fprLast = opt.fprLast === undefined ? 1e-2 : opt.fprLast;
        const say = opt.onProgress || (() => {});
        const sized = typeof opt.prune === "object";
        let D = sized ? 0 : opt.prune;
        if (!sized && (D < 0 || D > 255 || !Number.isInteger(D))) throw new BatchError('"' + opt.prune + '" is not a valid prune depth.');

        // ---- exact breadth-first search
        let L = sized ? 255 : Math.max(0, D - K);
        if (!x.bfs_init(L)) throw new BatchError("Out of memory while building the prune table.");
        const cum = [];     // cumulative exact counts
        let level = 0;
        for (; level <= L; level++) {
            say("Exact states: depth " + level);
            let c = x.bfs_level(level, 0);
            if (c === EMPTY) throw new BatchError("Out of memory while building the prune table.");
            cum.push((cum.length ? cum[cum.length - 1] : 0) + c);
            if (sized) {
                const limit = opt.prune.states;
                const r = cum.length >= 2 && cum[cum.length - 2] > 0 ? cum[cum.length - 1] / cum[cum.length - 2] : Infinity;
                const finished = c === 0;
                const projected = cum[cum.length - 1] * Math.pow(r, K);
                if (finished || (level >= 1 && projected > limit) || cum[cum.length - 1] > limit) {
                    // stop here: this level is the last exact one
                    L = level;
                    let j = 0;
                    if (!finished) while (j < K && cum[cum.length - 1] * Math.pow(r, j + 1) <= limit && cum[cum.length - 1] <= limit) j++;
                    D = L + j;
                    // the final level is not expanded (its children would be deeper than L)
                    break;
                }
            }
            if (!sized && level === L) break;
            c = x.bfs_level(level, 1);
            if (c === EMPTY) throw new BatchError("Out of memory while building the prune table.");
        }
        if (sized && level > 255) throw new BatchError("The prune size is too large to reach a depth limit.");
        if (sized && D > 255) D = 255;
        const exactCum = (i) => cum[Math.min(i, cum.length - 1)];
        this.depth = D;
        this.exactDepth = L;
        this.stats.exactStates = x.bfs_total();

        // ---- layer sizes
        const counts = new Array(D + 1).fill(0);
        for (let i = 0; i <= Math.min(L, D); i++) counts[i] = exactCum(i);
        if (D > L) {
            const frontier = x.ext_begin(L, D);
            if (!frontier && x.hll_ptr() === 0) throw new BatchError("Out of memory while counting states.");
            const total = frontier;
            let done = 0;
            const chunk = Math.max(1, Math.min(2000, Math.ceil(total / 200)));
            while (done < total) {
                done = x.ext_run(1, done, chunk);
                say("Counting states: " + Math.floor(100 * done / Math.max(1, total)) + "%");
            }
            const hllLen = 65536;
            const regs = new Uint8Array(this.x.memory.buffer, x.hll_ptr(), (D - L + 1) * hllLen);
            const merged = Uint8Array.from(regs.subarray(0, hllLen));
            for (let i = L + 1; i <= D; i++) {
                const layer = regs.subarray((i - L) * hllLen, (i - L + 1) * hllLen);
                for (let j = 0; j < hllLen; j++) if (layer[j] > merged[j]) merged[j] = layer[j];
                counts[i] = hllEstimate(merged);
            }
            this.stats.countNodes = x.ext_nodes();
        }
        this.stats.layerCounts = counts.slice();

        // ---- allocate and fill
        // Work out all filter sizes first: if they cannot fit in WebAssembly's 4 GB address space, say so now
        // instead of after minutes of work.
        const plans = [];
        let bytes = 0;
        for (let i = 1; i <= D; i++) {
            const est = i <= L ? counts[i] : counts[i] * 1.04 + 64;
            const p = bloomParams(est, i === D ? fprLast : fpr);
            plans.push(p);
            bytes += p.nBlocks * 64;
        }
        const room = MAX_WASM_BYTES - x.bytes_used() - (64 << 20);
        if (bytes > room) {
            const gb = (v) => (v / 1073741824).toFixed(1) + " GB";
            throw new BatchError("Prune depth " + D + " would need about " + gb(bytes + x.bytes_used()) + " of memory for the endtables, but WebAssembly can address at most 4 GB. " +
                "Use a smaller prune depth" + (D > 1 ? " (such as " + (D - 1) + ")" : "") + " or a larger Bloom false-positive rate.");
        }
        this.stats.layers = [];
        plans.forEach((p, idx) => {
            const i = idx + 1;
            if (!x.bloom_alloc(i, p.nBlocks, p.k)) throw new BatchError("Out of memory while building the endtables. Try a smaller prune depth.");
            this.stats.layers.push({ layer: i, keys: counts[i], blocks: p.nBlocks, k: p.k, bitsPerKey: p.bitsPerKey });
        });
        this.stats.bloomBytes = bytes;
        say("Filling endtables");
        x.bloom_fill_exact();
        if (D > L) {
            let done = 0;
            const frontierCount = this._frontierCount();
            const chunk = Math.max(1, Math.min(2000, Math.ceil(frontierCount / 200)));
            while (done < frontierCount) {
                done = x.ext_run(2, done, chunk);
                say("Filling endtables: " + Math.floor(100 * done / Math.max(1, frontierCount)) + "%");
            }
        }
        this.stats.memoryBytes = x.bytes_used();
        this.stats.depth = D;
        return this.stats;
    }

    _frontierCount() { return this.x.ext_frontier_count(); }

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
