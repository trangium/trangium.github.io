// Turns the user's fields into everything the engine needs:
// the masked puzzle restricted to a subgroup, move costs, the target set (solved states plus
// post-adjust images) and the pre-adjust start variants.

import {
    BatchError, parsePuzzle, splitMoveList, selectMoves, actionKey, enumerateGroup,
    invert, compose, isIdentity, identityAction,
} from "./puzzle.js";
import { parseMask, applyToMasked, maskedStateOfAction, checkNormalizes } from "./mask.js";
import { parseESQ, weightFor } from "./esq.js";

export function stateKey(state) {
    return String.fromCharCode.apply(null, state);
}

/** Parses the Prune field: "7" -> {depth:7}; "300k"/"5m"/"1b"/"1g" -> {states:...}. */
export function parsePrune(text) {
    const t = text.trim().toLowerCase();
    const num = parseFloat(t);
    if (!(num === num) || num < 0) throw new BatchError('"' + text + '" is not a valid prune depth.');
    if (t.endsWith("g") || t.endsWith("b")) return { states: num * 1e9 };
    if (t.endsWith("m")) return { states: num * 1e6 };
    if (t.endsWith("k")) return { states: num * 1e3 };
    if (!Number.isInteger(num)) throw new BatchError('"' + text + '" is not a valid prune depth.');
    return { depth: num };
}

/** Maximum total search cost from the Search field (n, =, +, -). */
export function parseSearch(text, pruneDepth) {
    const t = text.trim();
    const n = parseInt(t, 10);
    let extra;
    if (n === n && /^-?\d+$/.test(t)) extra = n;
    else if (t[0] === "=") extra = pruneDepth;
    else if (t[0] === "+") extra = pruneDepth + (t.split("+").length - 1);
    else if (t[0] === "-") extra = pruneDepth - (t.split("-").length - 1);
    else throw new BatchError('"' + text + '" is not a valid search depth.');
    return Math.max(0, pruneDepth + extra);
}

/**
 * Keys that identify an adjust action by what it does to masked states, so that two actions that differ only
 * in how they treat equivalent pieces (or ignored orientations) count as one group element.
 *  preKey:  the effect of applyToMasked (a right action): which location each location takes its piece from,
 *           and the twist (only its value modulo the orientation counts of the mask matters).
 *  postKey: the effect of leftMultiplyMasked: for each class, the class and twist it is sent to.
 */
export function adjustKeys(puzzle, mask) {
    const gcd = (a, b) => { while (b) { [a, b] = [b, a % b]; } return a; };
    let L = 1;
    for (let i = 0; i < mask.n; i++) if (mask.cls[i] === i) L = L / gcd(L, mask.k[i]) * mask.k[i];
    const reps = [];
    for (let i = 0; i < mask.n; i++) if (mask.cls[i] === i) reps.push(i);
    const preKey = (a) => {
        const parts = new Array(mask.n);
        for (let p = 0; p < mask.n; p++) parts[p] = a.src[p] + ":" + (a.tw[p] % L);
        return parts.join(",");
    };
    const postKey = (a) => reps.map(r => {
        const c = mask.cls[a.src[r]];
        return c + ":" + (a.tw[r] % mask.k[c]);
    }).join(",");
    return { preKey, postKey };
}

/**
 * @param {{puzzleText:string, ignoreText:string, preText:string, postText:string, genEsq:string}} cfg
 */
export function buildBase(cfg) {
    const puzzle = parsePuzzle(cfg.puzzleText);
    const mask = parseMask(puzzle, cfg.ignoreText);
    const preMoves = selectMoves(puzzle, splitMoveList(cfg.preText), "Pre-Adjust");
    const postMoves = selectMoves(puzzle, splitMoveList(cfg.postText), "Post-Adjust");
    checkNormalizes(puzzle, mask, postMoves);
    const { preKey, postKey } = adjustKeys(puzzle, mask);
    const groupOf = (moves, keyOf) => enumerateGroup(moves.map(m => puzzle.moves[m]), puzzle.ori, cfg.maxAdjust || 2000, keyOf)
        .map(e => ({ action: e.action, seq: e.seq.map(g => moves[g]) }));
    const preGroup = groupOf(preMoves, preKey), postGroup = groupOf(postMoves, postKey);
    return { puzzle, mask, preMoves, postMoves, preGroup, postGroup, preKey, postKey, genWeights: parseESQ(cfg.genEsq || "") };
}

/** Cost of a move that may not be used in a given direction (one-way moves, see buildSubgroup). */
export const NOT_ALLOWED = 1 << 28;

/**
 * Per-subgroup data: masked move tables, costs, targets and adjust masks.
 * @param base result of buildBase
 * @param {string} subgroupText
 * @param mask the mask the search runs on (default: Unique Orientations & Equivalences)
 *
 * The engine's move list is closed under inverses. A one-way move (`U ->`) gets a hidden partner,
 * its inverse, that the search never plays forwards but the endtable construction (which walks
 * backwards from the solved states) needs. Hidden moves come after the real ones, so engine move
 * indices below `names.length` are real moves.
 */
export function buildSubgroup(base, subgroupText, mask = base.mask) {
    const { puzzle } = base;
    const entries = splitMoveList(subgroupText);
    const sub = entries.length ? selectMoves(puzzle, entries, "Subgroup") : puzzle.moves.map((_, i) => i);
    const local = new Map(sub.map((m, i) => [m, i]));
    const rounded = [];
    const cost = sub.map(m => {
        const name = puzzle.moves[m].name;
        const w = weightFor(base.genWeights, name);
        const r = Math.max(1, Math.round(w));
        if (r !== w) rounded.push({ name, from: w, to: r });
        return r;
    });
    const engineMoves = sub.map(m => ({ src: puzzle.moves[m].src, tw: puzzle.moves[m].tw }));
    const inv = sub.map(m => {
        const im = puzzle.inverse[m];
        const i = im >= 0 ? local.get(im) : undefined;
        return i === undefined ? -1 : i;
    });
    for (let i = 0; i < sub.length; i++) {
        if (inv[i] >= 0) continue;
        const hidden = invert(puzzle.moves[sub[i]], puzzle.ori);
        engineMoves.push({ src: hidden.src, tw: hidden.tw });
        cost.push(NOT_ALLOWED);
        inv[i] = engineMoves.length - 1;
        inv[engineMoves.length - 1] = i;
    }
    const bcost = inv.map(i => cost[i]);
    const masked = { n: puzzle.n, k: mask.k, ori: puzzle.ori, moves: engineMoves, cost, bcost, inv };

    // targets: the solved state, plus the images of "solved" under inverse post-adjusts
    const targets = [], seen = new Set();
    base.postGroup.forEach((q, qi) => {
        const st = applyToMasked(mask, mask.solved, invert(q.action, puzzle.ori));
        const key = stateKey(st);
        if (seen.has(key)) return;
        seen.add(key);
        targets.push({ state: st, post: q.seq, qi });
    });
    // adjust move sets in subgroup indices
    // A move is an adjust move if it has the same effect on masked states as an element of the group. For Post-Adjust
    // this includes a move that does nothing to masked states at all (it lies in the group of states that look solved),
    // e.g. U when every piece U moves is equivalent to the piece it replaces: it is still one of the user's adjust moves.
    const idPre = base.preKey(identityAction(puzzle.n)), idPost = base.postKey(identityAction(puzzle.n));
    const preKeys = new Set(base.preGroup.map(e => base.preKey(e.action)));
    const postKeys = new Set(base.postGroup.map(e => base.postKey(e.action)));
    preKeys.delete(idPre);
    if (base.postMoves.length === 0) postKeys.delete(idPost);
    const preSet = [], postSet = [];
    sub.forEach((m, i) => {
        if (preKeys.has(base.preKey(puzzle.moves[m]))) preSet.push(i);
        if (postKeys.has(base.postKey(puzzle.moves[m]))) postSet.push(i);
    });
    return { sub, masked, targets, preSet, postSet, rounded, names: sub.map(m => puzzle.moves[m].name) };
}

/** Start variants for a case: the case state followed by each distinct pre-adjust. */
export function startVariants(base, caseState, mask = base.mask) {
    const out = [], seen = new Set();
    base.preGroup.forEach((q, pi) => {
        const st = applyToMasked(mask, caseState, q.action);
        const key = stateKey(st);
        if (seen.has(key)) return;
        seen.add(key);
        out.push({ state: st, pre: q.seq, pi });
    });
    return out;
}
