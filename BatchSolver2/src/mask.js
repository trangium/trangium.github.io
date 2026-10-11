// Unique Orientations & Equivalences ("UO&E"): which states count as "the same".
//
// Pieces listed in {braces} are interchangeable, and `k: pieces` reduces the
// number of distinguishable orientations of those pieces to k. A masked state
// replaces every piece by its equivalence class and every orientation by
// orientation mod k. Masked states are what the engine searches over.
//
// Masked state layout (Uint16Array, one entry per location):
//     value = classRep | (orientation << 8)
// The orientation of a piece is always reduced modulo the number of unique orientations k of
// that piece's class (mask.k[classRep]) - never modulo something that belongs to the location -
// so a move may carry a piece onto a location whose own piece has a different k.

import { BatchError, compose, invert, identityAction } from "./puzzle.js";

/**
 * @typedef {{n:number, cls:Uint8Array, k:Uint8Array, solved:Uint16Array}} Mask
 */

function stripBrackets(s) {
    return s.replace(/[(){}<>\[\]]/g, " ");
}

/** @return {Mask} */
export function parseMask(puzzle, text, what = "Unique Orientations & Equivalences") {
    const n = puzzle.n;
    const pieceOf = (name) => {
        const id = puzzle.pieceIndex.get(name);
        if (id === undefined) throw new BatchError('"' + name + '" is not a piece. (error in ' + what + ")");
        return id;
    };

    const k = Uint8Array.from(puzzle.ori);
    const parent = Array.from({ length: n }, (_, i) => i);
    const find = (x) => { while (parent[x] !== x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };

    const clean = text.split(/\r?\n/).map(l => l.split("//")[0]).join("\n");

    // orientation counts
    for (const line of clean.split("\n")) {
        const colon = line.indexOf(":");
        if (colon < 0) continue;
        const header = line.slice(0, colon).trim();
        const kk = parseInt(header, 10);
        if (!(kk === kk) || String(kk) !== header) throw new BatchError('"' + header + ':" is not a valid header in ' + what + ".");
        if (kk <= 0) throw new BatchError('"' + header + ':" is invalid because the number of orientations must be positive.');
        for (const name of stripBrackets(line.slice(colon + 1)).split(/\s+/).filter(x => x !== "")) {
            const id = pieceOf(name);
            if (puzzle.ori[id] % kk !== 0) {
                throw new BatchError("Cannot set the number of orientations of piece " + name + " to " + kk + " because " + puzzle.ori[id] + " is not divisible by " + kk + ".");
            }
            k[id] = kk;
        }
    }

    // equivalence sets
    let rest = clean;
    for (;;) {
        const open = rest.indexOf("{");
        if (open < 0) break;
        const close = rest.indexOf("}", open);
        if (close < 0) throw new BatchError('Missing "}" in ' + what + ".");
        const names = rest.slice(open + 1, close).split(/\s+/).filter(x => x !== "");
        rest = rest.slice(close + 1);
        const ids = names.map(pieceOf);
        for (let i = 1; i < ids.length; i++) {
            if (puzzle.ori[ids[i]] !== puzzle.ori[ids[0]]) {
                throw new BatchError('"' + names[i] + '" and "' + names[0] + '" cannot be in the same equivalence set because they are different types of pieces.');
            }
            const a = find(ids[0]), b = find(ids[i]);
            if (a !== b) parent[Math.max(a, b)] = Math.min(a, b);
        }
    }

    const cls = new Uint8Array(n);
    for (let i = 0; i < n; i++) cls[i] = find(i);
    for (let i = 0; i < n; i++) {
        if (k[i] !== k[cls[i]]) {
            throw new BatchError('"' + puzzle.pieceNames[i] + '" and "' + puzzle.pieceNames[cls[i]] + '" are equivalent but have different numbers of unique orientations.');
        }
    }

    const solved = new Uint16Array(n);
    for (let p = 0; p < n; p++) solved[p] = cls[p];
    return { n, cls, k, solved };
}

/** Masked state obtained by applying `action` to the solved puzzle. */
export function maskedStateOfAction(mask, action, out = new Uint16Array(mask.n)) {
    for (let p = 0; p < mask.n; p++) {
        const piece = action.src[p];
        out[p] = mask.cls[piece] | ((action.tw[p] % mask.k[piece]) << 8);
    }
    return out;
}

/** Applies `action` to a masked state (right action: "do the state, then the move"). */
export function applyToMasked(mask, state, action, out = new Uint16Array(mask.n)) {
    for (let p = 0; p < mask.n; p++) {
        const v = state[action.src[p]];
        const cl = v & 255;
        out[p] = cl | (((v >> 8) + action.tw[p]) % mask.k[cl] << 8);
    }
    return out;
}

/**
 * q . s : the masked state of "perform q on the solved puzzle, then perform whatever
 * produced s". Only meaningful if q normalises the solved group (see checkNormalizes).
 */
export function leftMultiplyMasked(mask, q, state, out = new Uint16Array(mask.n)) {
    for (let p = 0; p < mask.n; p++) {
        const v = state[p];
        const r = v & 255, o = v >> 8;
        const piece = mask.cls[q.src[r]];
        out[p] = piece | (((q.tw[r] + o) % mask.k[piece]) << 8);
    }
    return out;
}

/** Is `action` a member of the group H of states that look solved? */
function inSolvedGroup(mask, action) {
    for (let p = 0; p < mask.n; p++) {
        if (mask.cls[action.src[p]] !== mask.cls[p]) return false;
        if (action.tw[p] % mask.k[action.src[p]] !== 0) return false;
    }
    return true;
}

/** Generators of the group H of actions that leave the masked solved state unchanged. */
function solvedGroupGenerators(puzzle, mask) {
    const gens = [];
    const members = new Map();
    for (let p = 0; p < mask.n; p++) {
        if (!members.has(mask.cls[p])) members.set(mask.cls[p], []);
        members.get(mask.cls[p]).push(p);
    }
    for (const list of members.values()) {
        for (let i = 0; i + 1 < list.length; i++) {
            const g = identityAction(mask.n);
            g.src[list[i]] = list[i + 1];
            g.src[list[i + 1]] = list[i];
            gens.push(g);
        }
    }
    for (let p = 0; p < mask.n; p++) {
        if (mask.k[p] < puzzle.ori[p]) {
            const g = identityAction(mask.n);
            g.tw[p] = mask.k[p];
            gens.push(g);
        }
    }
    return gens;
}

/**
 * The solved group H must be closed under conjugation by every post-adjust move,
 * otherwise "post-adjust" does not make sense. Throws a BatchError naming the offender.
 */
export function checkNormalizes(puzzle, mask, moveIndices) {
    const gens = solvedGroupGenerators(puzzle, mask);
    for (const mi of moveIndices) {
        const q = puzzle.moves[mi];
        const qInv = invert(q, puzzle.ori);
        for (const g of gens) {
            const conj = compose(compose(qInv, g, puzzle.ori), q, puzzle.ori);
            if (!inSolvedGroup(mask, conj)) {
                throw new BatchError('Invalid post-adjust: the move "' + q.name + '" changes which states count as solved, so it cannot be an adjustment after the solution. ' +
                    "Pieces that Unique Orientations & Equivalences treats as the same must be mapped to equivalent pieces, with matching orientation, by every post-adjust move.");
            }
        }
    }
}

const gcd = (a, b) => { while (b) { [a, b] = [b, a % b]; } return a; };

/**
 * The minimal mask above `fine` (every fine equivalence is kept) in which all the given masked states look the same:
 * pieces that two states put at one location are merged into one set, and the orientations of a set are reduced as far
 * as needed for the states to agree. (Pieces that every state disturbs in the same way stay distinct.)
 * @param {{pieceNames:string[], n:number, ori:Uint8Array|number[]}} puzzle
 * @param {Mask} fine
 * @param {Uint16Array[]} states masked (fine) states
 * @return {{mask: Mask, text: string}} the mask and an equivalent Unique-Orientations-&-Equivalences text
 */
export function fullBatchMask(puzzle, fine, states) {
    const n = puzzle.n;
    const parent = Array.from({ length: n }, (_, i) => fine.cls[i]);
    const find = (x) => { while (parent[x] !== x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    const union = (a, b) => { a = find(a); b = find(b); if (a !== b) parent[Math.max(a, b)] = Math.min(a, b); };
    const ref = states.length ? states[0] : null;
    for (const st of states) for (let p = 0; p < n; p++) union(st[p] & 255, ref[p] & 255);
    // number of orientations kept per set: the largest d dividing every piece's own count and every disagreement between states
    const g = new Uint8Array(n);
    for (let i = 0; i < n; i++) { const r = find(i); g[r] = gcd(g[r], fine.k[i]); }
    for (const st of states) {
        for (let p = 0; p < n; p++) {
            const r = find(ref[p] & 255);
            g[r] = gcd(g[r], Math.abs((st[p] >> 8) - (ref[p] >> 8)));
        }
    }
    const cls = new Uint8Array(n), k = new Uint8Array(n);
    for (let i = 0; i < n; i++) { cls[i] = find(i); k[i] = g[cls[i]]; }
    const solved = new Uint16Array(n);
    for (let p = 0; p < n; p++) solved[p] = cls[p];

    const members = new Map();
    for (let i = 0; i < n; i++) { if (!members.has(cls[i])) members.set(cls[i], []); members.get(cls[i]).push(i); }
    const lines = [];
    for (const list of members.values()) {
        const names = list.map(i => puzzle.pieceNames[i]);
        const reduced = k[list[0]] !== puzzle.ori[list[0]];
        if (list.length === 1 && !reduced) continue;
        const body = list.length > 1 ? "{" + names.join(" ") + "}" : names[0];
        lines.push((reduced ? k[list[0]] + ": " : "") + body);
    }
    lines.sort((x, y) => (/^\d+:/.test(y) ? 1 : 0) - (/^\d+:/.test(x) ? 1 : 0));
    return { mask: { n, cls, k, solved }, text: lines.join("\n") };
}
