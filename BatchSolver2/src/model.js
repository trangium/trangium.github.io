// Turns the user's fields into everything the engine needs:
// the masked puzzle restricted to a subgroup, move costs, the target set (solved states plus
// post-adjust images) and the pre-adjust start variants.

import {
    BatchError, parsePuzzle, splitMoveList, selectMoves, actionKey, enumerateGroup,
    invert, compose, isIdentity,
} from "./puzzle.js";
import { parseMask, applyToMasked, maskedStateOfAction, checkNormalizes } from "./mask.js";
import { parseESQ, weightFor } from "./esq.js";

export function stateKey(state) {
    return String.fromCharCode.apply(null, state);
}

/** Parses the Prune field: "7" -> {depth:7}; "300k"/"5m" -> {states:...}. */
export function parsePrune(text) {
    const t = text.trim().toLowerCase();
    const num = parseFloat(t);
    if (!(num === num) || num < 0) throw new BatchError('"' + text + '" is not a valid prune depth.');
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
 * @param {{puzzleText:string, ignoreText:string, preText:string, postText:string, genEsq:string}} cfg
 */
export function buildBase(cfg) {
    const puzzle = parsePuzzle(cfg.puzzleText);
    const mask = parseMask(puzzle, cfg.ignoreText);
    const preMoves = selectMoves(puzzle, splitMoveList(cfg.preText), "Pre-Adjust");
    const postMoves = selectMoves(puzzle, splitMoveList(cfg.postText), "Post-Adjust");
    checkNormalizes(puzzle, mask, postMoves);
    const groupOf = (moves) => enumerateGroup(moves.map(m => puzzle.moves[m]), puzzle.ori)
        .map(e => ({ action: e.action, seq: e.seq.map(g => moves[g]) }));
    const preGroup = groupOf(preMoves), postGroup = groupOf(postMoves);
    return { puzzle, mask, preMoves, postMoves, preGroup, postGroup, genWeights: parseESQ(cfg.genEsq || "") };
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
    const keysOf = (group) => new Set(group.filter(e => !isIdentity(e.action)).map(e => actionKey(e.action)));
    const preKeys = keysOf(base.preGroup), postKeys = keysOf(base.postGroup);
    const preSet = [], postSet = [];
    sub.forEach((m, i) => {
        const key = actionKey(puzzle.moves[m]);
        if (preKeys.has(key)) preSet.push(i);
        if (postKeys.has(key)) postSet.push(i);
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
