// Scramble parsing and case generation.
//
// A "case" is a distinct masked state reachable by the Scramble field. Cases that differ
// only by pre-adjust (at the end of the scramble) or post-adjust (at the start) are merged.

import {
    BatchError, expandToken, compose, identityAction,
} from "./puzzle.js";
import { applyToMasked, leftMultiplyMasked, maskedStateOfAction } from "./mask.js";
import { stateKey } from "./model.js";

/** "#3,5-9,20+" -> {set, startNum}. */
export function parseModifiers(input) {
    const parse = (x) => {
        const v = parseInt(x, 10);
        if (!(v === v) || v <= 0) throw new BatchError('"' + x + '" is not a positive number. (Error in Scramble)');
        return v;
    };
    input = input.replaceAll("\n", "");
    const pound = input.indexOf("#");
    if (pound === -1) return { set: new Set(), startNum: 1 };
    const set = new Set();
    let startNum = Infinity;
    for (const mod of input.slice(pound + 1).split(",").filter(x => x.trim() !== "")) {
        if (mod.includes("+")) startNum = Math.min(startNum, parse(mod));
        else if (mod.includes("-")) {
            const a = parse(mod.split("-")[0]), b = parse(mod.split("-")[1]);
            if (b <= a) throw new BatchError('Invalid range: "' + mod + '" (Error in Scramble)');
            for (let i = a; i <= b; i++) set.add(i);
        } else set.add(parse(mod));
    }
    return { set, startNum };
}

/** Splits the scramble into plain text, [multi-path] and <generator> segments. */
export function parseBatch(input) {
    const segments = [];
    const close = { "[": "]", "<": ">" };
    let i = 0;
    let plain = "";
    while (i < input.length) {
        const ch = input[i];
        if (ch === "[" || ch === "<") {
            if (plain.trim() !== "") segments.push(["", plain]);
            plain = "";
            const end = input.indexOf(close[ch], i + 1);
            if (end < 0) throw new BatchError('Missing "' + close[ch] + '" in Scramble');
            segments.push([close[ch], input.slice(i + 1, end)]);
            i = end + 1;
        } else { plain += ch; i++; }
    }
    if (plain.trim() !== "") segments.push(["", plain]);
    return segments;
}

function algAction(puzzle, alg) {
    let act = identityAction(puzzle.n);
    for (const tok of alg.split(/\s+/).filter(x => x !== "")) {
        const mv = expandToken(puzzle, tok);
        if (!mv) throw new BatchError('Unexpected token in Scramble: "' + tok + '"');
        for (const m of mv) act = compose(act, puzzle.moves[m], puzzle.ori);
    }
    return act;
}

function priorityVector(puzzle, action, sorting) {
    const n = puzzle.n;
    let minIndex = 0;
    const pcPriority = Array.from({ length: n }, (_, i) => i);
    const out = [];
    const where = (piece) => { for (let l = 0; l < n; l++) if (action.src[l] === piece) return l; return -1; };
    for (const crit of sorting) {
        const names = crit.pieces.split(/\s+/).filter(x => x !== "");
        if (names.length === 0) continue;
        if (crit.type === "priority") {
            minIndex -= n;
            for (const nm of names) {
                const idx = puzzle.pieceIndex.get(nm);
                if (idx === undefined) throw new BatchError('Invalid piece: "' + nm + '" (in Case Sorting)');
                pcPriority[idx] = minIndex++;
            }
            minIndex -= n;
        } else {
            const vals = [];
            for (const nm of names) {
                const idx = puzzle.pieceIndex.get(nm);
                if (idx === undefined) throw new BatchError('Invalid piece: "' + nm + '" (in Case Sorting)');
                if (crit.type === "ori-at") vals.push(action.tw[idx]);
                else if (crit.type === "ori-of") vals.push(action.tw[where(idx)]);
                else if (crit.type === "perm-at") vals.push(pcPriority[action.src[idx]]);
                else if (crit.type === "perm-of") vals.push(pcPriority[where(idx)]);
            }
            if (crit.type === "ori-at" || crit.type === "ori-of") out.push(...vals.slice().sort((a, b) => a - b));
            out.push(...vals);
        }
    }
    // Default tiebreak for cases that are equal under every criterion above:
    // orientation at piece 0, 1, ..., then permutation at piece 0, 1, ...
    for (let i = 0; i < n; i++) out.push(action.tw[i]);
    for (let i = 0; i < n; i++) out.push(pcPriority[action.src[i]]);
    return out;
}

/**
 * @param base result of buildBase
 * @param {string} scramble raw Scramble field
 * @param {{type:string, pieces:string}[]} sorting Case Sorting rows
 * @param {(msg:string)=>void} [progress]
 * @param {boolean} [wantClassMap] also return the map from every equivalent state to its case (needed for Scramble UO&E)
 * @return {{cases:{num:number, setup:string, state:Uint16Array}[], total:number}} selected cases (modifiers applied) and total count
 */
/** More cases than this cannot be held (or looked at) in a browser tab; say so instead of running out of memory. */
export const MAX_CASES = 2000000;

export function generateCases(base, scramble, sorting, progress = () => {}, wantClassMap = false) {
    const { puzzle, mask } = base;
    const { set, startNum } = parseModifiers(scramble);
    const text = (scramble.includes("#") ? scramble.slice(0, scramble.indexOf("#")) : scramble).replaceAll("\n", " ");
    const root = { str: "", state: Uint16Array.from(mask.solved), action: identityAction(puzzle.n) };

    const extend = (item, alg, algAct) => {
        const state = applyToMasked(mask, item.state, algAct);
        return { str: (item.str + " " + alg).trim(), state, action: null, parent: item, algAct };
    };
    const finalize = (it) => { if (!it.action) it.action = compose(it.parent.action, it.algAct, puzzle.ori); return it; };

    let items = [root];
    for (const [type, data] of parseBatch(text)) {
        if (type === "") {
            const act = algAction(puzzle, data);
            items = items.map(it => finalize(extend(it, data.trim(), act)));
        } else if (type === "]") {
            const algs = data.split(",").map(a => a.trim());
            const acts = algs.map(a => algAction(puzzle, a));
            const next = new Map();
            for (const it of items) algs.forEach((a, i) => next.set(stateKey(applyToMasked(mask, it.state, acts[i])), extend(it, a, acts[i])));
            items = [...next.values()].map(finalize);
        } else if (type === ">") {
            const gens = data.split(",").map(a => a.trim()).filter(a => a !== "");
            const acts = gens.map(a => algAction(puzzle, a));
            const all = new Map(items.map(it => [stateKey(it.state), it]));
            let frontier = [...all.values()];
            while (frontier.length) {
                const next = [];
                for (const it of frontier) {
                    for (let g = 0; g < gens.length; g++) {
                        const st = applyToMasked(mask, it.state, acts[g]);
                        const key = stateKey(st);
                        if (all.has(key)) continue;
                        const child = finalize({ str: (it.str + " " + gens[g]).trim(), state: st, action: null, parent: it, algAct: acts[g] });
                        all.set(key, child);
                        next.push(child);
                        if (all.size > MAX_CASES) {
                            throw new BatchError("The scramble generates more than " + MAX_CASES.toLocaleString() + " distinct states. " +
                                "Ignore more pieces in Unique Orientations & Equivalences, or generate fewer states, so that the number of cases is manageable.");
                        }
                    }
                }
                frontier = next;
                progress(all.size + " (not reduced)");
            }
            items = [...all.values()];
        }
        for (const it of items) finalize(it);
    }

    // sort, then merge cases that only differ by pre-/post-adjust
    const vec = new Map(items.map(it => [it, priorityVector(puzzle, it.action, sorting)]));
    items.sort((a, b) => {
        const va = vec.get(a), vb = vec.get(b);
        for (let i = 0; i < va.length; i++) { if (va[i] !== vb[i]) return va[i] - vb[i]; }
        return 0;
    });
    const solvedKey = stateKey(mask.solved);
    const dup = new Set();
    const reduced = [];
    const members = [];      // per reduced case: [stateKey, post index, pre index] of every state equivalent to it
    for (const it of items) {
        const key = stateKey(it.state);
        if (dup.has(key)) continue;
        reduced.push(it);
        const mine = [];
        base.postGroup.forEach((post, qi) => {
            const left = leftMultiplyMasked(mask, post.action, it.state);
            base.preGroup.forEach((pre, pi) => {
                const k = stateKey(applyToMasked(mask, left, pre.action));
                dup.add(k);
                if (wantClassMap) mine.push([k, qi, pi]);
            });
        });
        members.push(mine);
    }
    const cases = [];
    const classMap = new Map();   // member state -> {c: index into `cases`, qi, pi}:  member = apply(left(post[qi], case), pre[pi])
    let num = 1;
    reduced.forEach((it, ri) => {
        if (stateKey(it.state) === solvedKey) return;
        if (num >= startNum || set.has(num)) {
            if (wantClassMap) for (const [k, qi, pi] of members[ri]) if (!classMap.has(k)) classMap.set(k, { c: cases.length, qi, pi });
            cases.push({ num, setup: it.str, state: it.state, action: it.action });
        }
        num++;
    });
    return { cases, total: cases.length, classMap };
}
