// ESQ weights ("Rank ESQ" / "Generation ESQ") and the simple move-count metrics.
import { splitMoveList } from "./puzzle.js";

/** Parses lines like `R_ L_ r_ l_: 1` into a Map(moveOrFamily -> weight). */
export function parseESQ(text) {
    const weights = new Map();
    for (const line of (text || "").split(/\r?\n/)) {
        const parts = line.split(":");
        if (parts.length === 2) {
            const value = parseFloat(parts[1].trim());
            if (value !== value) continue;
            for (const name of splitMoveList(parts[0])) weights.set(name, value);
        }
    }
    return weights;
}

export function lastAlpha(move) {
    for (let i = move.length - 1; i >= 0; i--) if (/[a-zA-Z]/.test(move[i])) return i;
    return -1;
}

/** Weight of a move: exact name, then `Name_`, then `_amount`, then `__`, else 1. */
export function weightFor(weights, move) {
    const cut = lastAlpha(move) + 1;
    const type = move.slice(0, cut) + "_";
    const amount = "_" + move.slice(cut);
    if (weights.has(move)) return weights.get(move);
    if (weights.has(type)) return weights.get(type);
    if (weights.has(amount)) return weights.get(amount);
    if (weights.has("__")) return weights.get("__");
    return 1;
}

export const stm = () => 1;
export function sqtm(move) {
    const amount = move.slice(lastAlpha(move) + 1).replace("'", "");
    return amount === "" ? 1 : parseInt(amount, 10);
}

/** Strips (adjust) and [secondary metric] parts. */
export function removeParens(alg) {
    let out = "", inside = false;
    for (const ch of alg) {
        if (ch === ")" || ch === "]") inside = false;
        else if (ch === "(" || ch === "[") inside = true;
        else if (!inside) out += ch;
    }
    return out.trim();
}

/** Sum of metric(move) over the moves of an algorithm; adjust moves in (parens) don't count. */
export function moveCount(alg, metric) {
    let count = 0, inside = false;
    for (const move of alg.split(" ")) {
        if (move === "") continue;
        if (move.includes(")") || move.includes("]")) inside = false;
        else if (move.includes("(") || move.includes("[")) inside = true;
        else if (!inside) count += metric(move);
    }
    return Math.round(count * 1e3) / 1e3;
}
