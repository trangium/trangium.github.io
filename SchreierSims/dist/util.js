import { schreierSims, canonStr } from "./SchreierSims.js";
import { identity } from "./Perm.js";
export class Puzzle {
    static _isValidPair(permset, i, j) {
        if (!j.commutes(i)) {
            return true;
        }
        ;
        if (permset.has(j.rmul(i).toString())) {
            return false;
        }
        ;
        if (j.compareTo(i.inv()) === 0) {
            return false;
        }
        ;
        return j.compareTo(i) > 0;
    }
    static _getCommTable(moveset) {
        const table = new Map();
        let permStrSet = new Set(moveset.map(move => move.perm.toString()));
        moveset.forEach((i) => {
            const commutativeMoves = new Set(moveset.filter((j) => Puzzle._isValidPair(permStrSet, i.perm, j.perm)));
            table.set(i, commutativeMoves);
        });
        return table;
    }
    constructor(_moveset, _subgroup) {
        this.moveset = _moveset;
        this.identity = identity(_moveset[0].perm.n);
        this.commTable = Puzzle._getCommTable(_moveset);
        this.sgs = schreierSims(_subgroup);
    }
}
export function exec(puzzle, str) {
    let pzl = puzzle.identity;
    for (let moveStr of str.split(" ")) {
        for (let move of puzzle.moveset) {
            if (move.str === moveStr) {
                pzl = pzl.rmul(move.perm);
            }
        }
    }
    return pzl;
}
function getEndTable(puzzle, maxWeight, end) {
    const invTable = new Map(puzzle.moveset.map((move) => [move, move.perm.inv()]));
    const endTable = new Map();
    const stack = [[end, 0, null]];
    while (stack.length > 0) {
        const [currentPerm, currentWeight, prevMove] = stack.pop();
        const permKey = canonStr(puzzle.sgs, currentPerm);
        if (!endTable.has(permKey) || endTable.get(permKey) > currentWeight) {
            endTable.set(permKey, currentWeight);
        }
        for (const move of puzzle.moveset) {
            if ((prevMove === null || puzzle.commTable.get(prevMove).has(move)) && currentWeight + move.weight <= maxWeight) {
                stack.push([currentPerm.rmul(invTable.get(move)), currentWeight + move.weight, move]);
            }
        }
    }
    return endTable;
}
function* dfs(puzzle, maxWeight, start, startStr, endStr, endTable, endDepth, moves = []) {
    if (startStr === endStr) {
        yield moves.reduce((partial, current) => partial + current.str + " ", "");
    }
    for (let move of puzzle.moveset) {
        let next = start.rmul(move.perm);
        let nextStr = canonStr(puzzle.sgs, next);
        let distCheck = endTable.has(nextStr) ? (maxWeight - move.weight >= endTable.get(nextStr)) : (maxWeight - move.weight > endDepth);
        if ((moves.length === 0 || (puzzle.commTable.get(moves[moves.length - 1]).has(move))) && distCheck) {
            yield* dfs(puzzle, maxWeight - move.weight, next, nextStr, endStr, endTable, endDepth, moves.concat([move]));
        }
    }
}
export function* solve(puzzle, pruneWeight, maxWeight, start, end) {
    let endTable = getEndTable(puzzle, pruneWeight, end);
    yield* dfs(puzzle, maxWeight, start, canonStr(puzzle.sgs, start), canonStr(puzzle.sgs, end), endTable, pruneWeight);
}
