// The whole batch run: fields in, messages out. worker.js is a thin shell around runBatch(),
// and the Node tests call it directly.
//
// Messages posted to `host.post`:
//   {type:"status",      value:string}                          what is being built right now ("" clears it)
//   {type:"warning",     value:string}                          something the user should know about
//   {type:"batching",    value:string}                          what batching did (cases per search, equivalences chosen by "full")
//   {type:"tables",      value:{row, depth, states, bytes, filterBytes, exactStates}}   size of one subgroup's endtables
//   {type:"num-states",  value:number|string}                   number of cases (a string while still counting)
//   {type:"cases",       value:[{index, num, setup}]}           every case, once, before any search starts
//   {type:"depthUpdate", value:{depth, index}}                  searching at this cost (index: the case, or -1 for several)
//   {type:"solutions",   value:[{index, rows:[[metric, alg]]}]} current ranked top lists of the listed cases (replace the old ones)
//   {type:"case-done",   value:{index, top, mcc, stm, sqtm, esq}}  the case is finished (top null: no solution found)
//   {type:"stop",        value:string|null}                     error message, or null on normal completion
//
// Subgroup rows are processed one after the other and each row's endtables are dropped before the next
// row is built, so memory use is that of the largest row, not the sum.

import { BatchError, parseMoveToken, invert, compose, actionKey } from "./puzzle.js";
import { compileEngine, Engine } from "./engine.js";
import { buildBase, buildSubgroup, startVariants, parsePrune, parseSearch, stateKey } from "./model.js";
import { generateCases } from "./cases.js";
import { parseMask, maskedStateOfAction, applyToMasked, fullBatchMask } from "./mask.js";
import { parseESQ, weightFor, stm, sqtm, moveCount, removeParens } from "./esq.js";

/**
 * @typedef {object} BatchInput
 * @property {string} puzzle  @property {string} ignore  Unique Orientations & Equivalences
 * @property {"none"|"full"|"custom"} [batching]  none: one search per case; full: batch as much as possible; custom: use `scrambleIgnore`
 *   (default: custom if `scrambleIgnore` is not empty, otherwise none)
 * @property {string} [scrambleIgnore]  Batching mask for "custom": a Unique Orientations & Equivalences text that ignores more than `ignore`
 * @property {string} solve
 * @property {string} preAdjust  @property {string} postAdjust
 * @property {{subgroup:string, prune:string, search:string}[]} subgroups
 * @property {{type:string, pieces:string}[]} sorting
 * @property {string} esq  Generation ESQ  @property {string} rankesq  Rank ESQ
 * @property {boolean} showPost
 * @property {string} sortBy  MCC | STM | SQTM | ESQ
 * @property {string} secondary  None | MCC | STM | SQTM | ESQ
 * @property {number} maxSolutions
 * @property {number[]} mcc  the ten MCC parameters
 * @property {number} [extension]  exact-storage margin K (default 4)
 * @property {number} [fpr]  @property {number} [fprLast]
 */

/** Maps each composite move name (and its inverse) to the plain moves it is made of, for MCC. */
function compositeExpander(puzzle) {
    const defByName = new Map(puzzle.defs.map(d => [d.name, d]));
    if (!puzzle.defs.some(d => d.tokens)) return null;
    // R -> R', R' -> R, and U2 stays U2 (algSpeed only knows "U2"; "U2'" is not a move it can read)
    const invertToken = (t) => t.endsWith("'") ? t.slice(0, -1) : /\d$/.test(t) ? t : t + "'";
    const expandToken = (tok, depth) => {
        const pt = parseMoveToken(tok);
        const def = pt && defByName.get(pt.name);
        if (!def || !def.tokens || depth > 50) return [tok];
        let seq = [];
        for (const t of def.tokens) seq = seq.concat(expandToken(t, depth + 1));
        let out = [];
        for (let i = 0; i < pt.power; i++) out = out.concat(seq);
        return pt.inverse ? out.reverse().map(invertToken) : out;
    };
    const cache = new Map();
    return (alg) => alg.split(/\s+/).filter(x => x !== "").map(tok => {
        let r = cache.get(tok);
        if (r === undefined) { r = expandToken(tok, 0).join(" "); cache.set(tok, r); }
        return r;
    }).join(" ");
}

class Metrics {
    constructor(input, puzzle, algSpeed) {
        const w = parseESQ(input.rankesq || "");
        this.fns = {
            STM: (alg) => moveCount(alg, stm),
            SQTM: (alg) => moveCount(alg, sqtm),
            ESQ: (alg) => moveCount(alg, (m) => weightFor(w, m)),
        };
        this.badMCC = null;        // first algorithm whose MCC could not be computed (reported once as a warning)
        const expand = compositeExpander(puzzle);
        const p = input.mcc || [0.8, 1.3, 1.4, 0.5, 1, 1.65, 1.25, 2.25, 0.8, 3.5];
        this.fns.MCC = (alg) => {
            if (!algSpeed) return NaN;
            let s = removeParens(alg);
            if (expand) s = expand(s);
            const v = parseFloat(algSpeed(s, false, false, ...p));
            if (v !== v && s.trim() !== "" && this.badMCC === null) this.badMCC = s;
            return v;
        };
    }
    value(name, alg) { return this.fns[name](alg); }
}

const formatBytes = (b) => b >= 1048576 ? (b / 1048576).toFixed(1) + " MB" : Math.max(1, Math.round(b / 1024)) + " KB";
const sortValue = (x) => (x === x ? x : Infinity);

/** The best `limit` distinct solutions of one case, ordered like the old Batch Solver. */
class Ranker {
    constructor(limit, metrics, primary, secondary) {
        this.limit = Math.max(1, limit);
        this.metrics = metrics; this.primary = primary; this.secondary = secondary;
        this.byKey = new Map();    // removeParens(alg) -> item
        this.rejected = new Set(); // recently seen keys that were not good enough
        this.dirty = false;
    }
    add(alg) {
        const key = removeParens(alg);
        if (this.byKey.has(key) || this.rejected.has(key)) return;   // same moves with another adjustment: the first one found stays
        const item = {
            alg, key,
            speed: this.metrics.value(this.primary, alg),
            sec: this.secondary ? this.metrics.value(this.secondary, alg) : null,
        };
        if (this.byKey.size >= this.limit && Ranker.compare(item, this.worst) >= 0) {
            if (this.rejected.size > 4096) this.rejected.clear();
            this.rejected.add(key);
            return;
        }
        this.byKey.set(key, item);
        this.dirty = true;
        if (this.byKey.size > 2 * this.limit + 64) this.trim();
        else if (this.byKey.size >= this.limit) this.updateWorst();
    }
    static compare(a, b) {
        const x = sortValue(a.speed), y = sortValue(b.speed);
        if (x !== y) return x < y ? -1 : 1;
        if (a.sec !== null && a.sec !== b.sec) return a.sec < b.sec ? -1 : 1;
        return a.key < b.key ? -1 : a.key > b.key ? 1 : 0;
    }
    sorted() { return [...this.byKey.values()].sort(Ranker.compare); }
    updateWorst() {
        let worst = null;
        for (const it of this.byKey.values()) if (!worst || Ranker.compare(it, worst) > 0) worst = it;
        this.worst = worst;
    }
    trim() {
        const keep = this.sorted().slice(0, this.limit);
        this.byKey = new Map(keep.map(i => [i.key, i]));
        this.worst = keep[keep.length - 1];
    }
    rows() {
        return this.sorted().slice(0, this.limit).map(i => [
            String(i.speed),
            this.secondary ? i.alg + " [" + i.sec + " " + this.secondary + "]" : i.alg,
        ]);
    }
}

/** Scramble UO&E may only ignore more than UO&E: every fine equivalence must also be a coarse one. */
export function checkCoarser(puzzle, fine, coarse) {
    for (let p = 0; p < puzzle.n; p++) {
        const name = puzzle.pieceNames[p];
        if (coarse.cls[fine.cls[p]] !== coarse.cls[p]) {
            throw new BatchError('Batching must keep every equivalence of Unique Orientations & Equivalences, ' +
                'but "' + name + '" and "' + puzzle.pieceNames[fine.cls[p]] + '" are treated as different there.');
        }
        if (fine.k[p] % coarse.k[p] !== 0) {
            throw new BatchError('Batching must distinguish no more orientations than Unique Orientations & Equivalences, ' +
                'but piece "' + name + '" has ' + coarse.k[p] + " there and " + fine.k[p] + " in the main field.");
        }
    }
}

/**
 * @param {BatchInput} input
 * @param {{post:(msg:object)=>void, wasm:string|ArrayBuffer, loadText:(path:string)=>Promise<string>}} host
 */
export async function runBatch(input, host) {
    const post = host.post;
    try {
        await runInner(input, host);
    } catch (e) {
        if (e instanceof BatchError) post({ type: "stop", value: e.message });
        else post({ type: "stop", value: "Internal error: " + (e && e.stack ? e.stack : e) });
    }
}

async function runInner(input, host) {
    const { post } = host;
    if (input.solve.includes(":")) throw new BatchError("Colon notation for indicating adjust moves is deprecated.");

    post({ type: "status", value: "Reading the puzzle" });
    const base = buildBase({
        puzzleText: input.puzzle, ignoreText: input.ignore,
        preText: input.preAdjust, postText: input.postAdjust, genEsq: input.esq,
    });
    const { puzzle } = base;
    const fine = base.mask;
    const rows = input.subgroups.map(r => ({ ...r, prune: parsePrune(r.prune) }));
    const sorting = input.sorting && input.sorting.length ? input.sorting : [{ type: "priority", pieces: "" }];

    // Batching: the search runs on a coarser mask, and each solution is then matched to the cases it solves.
    const scrambleText = (input.scrambleIgnore || "").trim();
    const mode = input.batching || (scrambleText === "" ? "none" : "custom");
    let coarse = null, fullText = "";
    if (mode === "custom" && scrambleText !== "") {
        coarse = parseMask(puzzle, input.scrambleIgnore, "Batching");
        checkCoarser(puzzle, fine, coarse);
    }

    post({ type: "status", value: "Generating cases" });
    const gen = generateCases(base, input.solve, sorting, (m) => post({ type: "num-states", value: m }), mode !== "none");
    post({ type: "num-states", value: gen.total });
    post({ type: "cases", value: gen.cases.map((c, index) => ({ index, num: c.num, setup: c.setup })) });
    if (mode === "full" && gen.cases.length > 0) {
        const full = fullBatchMask(puzzle, fine, gen.cases.map(c => c.state));
        coarse = full.mask;
        checkCoarser(puzzle, fine, coarse);
        fullText = full.text;
    }
    const searchMask = coarse || fine;

    let algSpeed = null;
    try {
        const src = await host.loadText("../algSpeed.js");
        algSpeed = new Function(src + "\n;return algSpeed;")();
    } catch (e) { algSpeed = null; }
    const metrics = new Metrics(input, puzzle, algSpeed);
    if (!algSpeed && (input.sortBy === "MCC" || input.secondary === "MCC")) {
        throw new BatchError("Could not load algSpeed.js, which is needed to compute MCC.");
    }

    // ---- search units: one case on its own, or all cases that look alike under Scramble UO&E
    const units = [];
    if (!coarse) {
        gen.cases.forEach((c, i) => units.push({ cases: [i], start: c.state }));
    } else {
        const byStart = new Map();
        gen.cases.forEach((c, i) => {
            const st = maskedStateOfAction(coarse, c.action);
            const key = stateKey(st);
            if (!byStart.has(key)) { const u = { cases: [], start: st }; byStart.set(key, u); units.push(u); }
            byStart.get(key).cases.push(i);
        });
    }

    if (coarse) {
        post({
            type: "batching",
            value: "Batching: " + gen.cases.length + (gen.cases.length === 1 ? " case" : " cases") + " in " + units.length + (units.length === 1 ? " search" : " searches") +
                (fullText ? "\nEquivalences used:\n" + fullText : ""),
        });
    }
    const moduleWasm = await compileEngine(host.wasm);
    const nameOf = (m) => puzzle.moves[m].name;
    const limit = input.maxSolutions || 20;
    const primary = input.sortBy || "MCC";
    const secondary = input.secondary && input.secondary !== "None" ? input.secondary : null;
    const rankers = new Array(gen.cases.length).fill(null);
    const dirty = new Set();
    let lastFlush = Date.now(), lastDepthPost = 0;
    const flush = (force) => {
        const now = Date.now();
        if (dirty.size === 0 || (!force && now - lastFlush < Math.max(250, 2 * dirty.size))) return;
        const out = [];
        for (const index of dirty) out.push({ index, rows: rankers[index].rows() });
        dirty.clear();
        lastFlush = now;
        if (metrics.badMCC !== null && !warned.has("mcc")) {
            warned.add("mcc");
            post({ type: "warning", value: "MCC could not be computed for \"" + metrics.badMCC + "\", so algorithms like it are ranked last. " +
                "Check that every move it contains is one MCC understands." });
        }
        post({ type: "solutions", value: out });
    };
    const addSolution = (index, alg) => {
        const r = rankers[index] || (rankers[index] = new Ranker(limit, metrics, primary, secondary));
        r.add(alg);
        if (r.dirty) { r.dirty = false; dirty.add(index); }
    };

    // adjust-group helpers for the Scramble UO&E case: products of group elements, as move sequences
    const productSeq = (group, cache, a, b) => {
        const ck = a + "," + b;
        let r = cache.get(ck);
        if (r === undefined) {
            const key = actionKey(compose(group[a].action, group[b].action, puzzle.ori));
            r = group.find(e => actionKey(e.action) === key).seq;
            cache.set(ck, r);
        }
        return r;
    };
    const preCache = new Map(), postCache = new Map();
    const preInv = base.preGroup.map(e => invert(e.action, puzzle.ori));
    const postInv = base.postGroup.map(e => invert(e.action, puzzle.ori));
    const seqText = (seq) => seq.map(nameOf).join(" ");

    const warned = new Set();
    for (let r = 0; r < rows.length; r++) {
        const isLast = r === rows.length - 1;
        const label = rows.length > 1 ? "Subgroup " + (r + 1) + ": " : "";
        const sub = buildSubgroup(base, rows[r].subgroup, searchMask);
        if (sub.rounded.length) {
            const w = "Generation ESQ weights are rounded to whole numbers (" +
                sub.rounded.slice(0, 4).map(x => x.name + ": " + x.from + " → " + x.to).join(", ") + (sub.rounded.length > 4 ? ", …" : "") + ").";
            if (!warned.has(w)) { warned.add(w); post({ type: "warning", value: w }); }
        }
        let eng = new Engine(moduleWasm, sub.masked);
        eng.setTargets(sub.targets.map(t => t.state));
        const pr = rows[r].prune;
        let lastStatus = 0;
        const stats = eng.buildTables({
            prune: pr.states !== undefined ? { states: pr.states } : pr.depth,
            extension: input.extension, fpr: input.fpr, fprLast: input.fprLast,
            onProgress: (m) => {       // progress arrives very often; the page only needs a few updates per second
                const now = Date.now();
                if (now - lastStatus > 100 || !m.includes("%")) { lastStatus = now; post({ type: "status", value: label + m }); }
            },
        });
        const maxCost = parseSearch(rows[r].search, eng.depth);
        eng.prepareSearch(maxCost + 1, sub.preSet, sub.postSet);
        post({
            type: "tables",
            value: {
                row: r, depth: eng.depth, states: stats.layerCounts[stats.layerCounts.length - 1],
                bytes: stats.memoryBytes, filterBytes: stats.bloomBytes, exactStates: stats.exactStates,
            },
        });
        post({ type: "status", value: label + "searching" });

        const invMoves = sub.masked.inv.map(i => sub.masked.moves[i]);   // engine move -> action of its inverse
        const bufA = new Uint16Array(puzzle.n), bufB = new Uint16Array(puzzle.n);

        for (const unit of units) {
            const variants = startVariants(base, unit.start, searchMask);
            const single = unit.cases.length === 1 ? unit.cases[0] : -1;
            for (let T = 0; T <= maxCost; T++) {
                const nowD = Date.now();
                if (nowD - lastDepthPost > 100) { lastDepthPost = nowD; post({ type: "depthUpdate", value: { depth: T, index: single } }); }
                for (const v of variants) {
                    for (const batch of eng.search(v.state, T)) {
                        for (const s of batch) {
                            let index, preSeq, postSeq;
                            if (!coarse) {
                                index = unit.cases[0];
                                preSeq = v.pre;
                                postSeq = sub.targets[s.target].post;
                            } else {
                                // fine state of the inverse of the whole algorithm: do (post)^-1, the core backwards, then (pre)^-1
                                const qt = sub.targets[s.target].qi;
                                let cur = applyToMasked(fine, fine.solved, postInv[qt], bufA), other = bufB;
                                for (let i = s.moves.length - 1; i >= 0; i--) {
                                    other = applyToMasked(fine, cur, invMoves[s.moves[i]], other);
                                    [cur, other] = [other, cur];
                                }
                                applyToMasked(fine, cur, preInv[v.pi], other);
                                const hit = gen.classMap.get(stateKey(other));
                                if (!hit) continue;
                                index = hit.c;
                                preSeq = productSeq(base.preGroup, preCache, hit.pi, v.pi);
                                postSeq = productSeq(base.postGroup, postCache, qt, hit.qi);
                            }
                            let alg = (preSeq.length ? "(" + seqText(preSeq) + ") " : "") + s.moves.map(m => sub.names[m]).join(" ");
                            if (input.showPost && postSeq.length) alg += " (" + seqText(postSeq) + ")";
                            addSolution(index, alg.trim());
                        }
                        flush(false);
                    }
                }
            }
            flush(true);
            if (isLast) {
                for (const index of unit.cases) {
                    const best = rankers[index] ? rankers[index].sorted()[0] : null;
                    post({
                        type: "case-done",
                        value: best ? {
                            index, top: best.alg,
                            mcc: metrics.value("MCC", best.alg), stm: metrics.value("STM", best.alg),
                            sqtm: metrics.value("SQTM", best.alg), esq: metrics.value("ESQ", best.alg),
                        } : { index, top: null },
                    });
                }
            }
        }
        eng = null;     // the endtables of this row are no longer needed: let the memory go before the next row is built
    }
    post({ type: "status", value: "" });
    post({ type: "stop", value: null });
}
