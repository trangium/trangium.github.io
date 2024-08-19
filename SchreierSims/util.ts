import {schreierSims, canonStr} from "./SchreierSims.js";
import {Perm, CycPerm, identity} from "./Perm.js";

export type Move = {
    str: string;
    perm: Perm; // cycPerm
    weight: number;
}

export class Puzzle {
    public moveset: Move[];
    public commTable: Map<Move, Set<Move>>;
    public identity: Perm; // oriPerm

    private static _isValidPair(permset: Set<String>, i: Perm, j: Perm) {
        if (!j.commutes(i)) {return true};
        if (permset.has(j.rmul(i).toString())) {return false};
        if (j.compareTo(i.inv()) === 0) {return false};
        return j.compareTo(i) > 0;
    }

    private static _getCommTable(moveset: Move[]): Map<Move, Set<Move>> {
        const table = new Map<Move, Set<Move>>();
        let permStrSet = new Set(moveset.map(move => move.perm.toString()));
        moveset.forEach((i: Move) => {
            const commutativeMoves = new Set(moveset.filter((j: Move) => Puzzle._isValidPair(permStrSet, i.perm, j.perm)));
            table.set(i, commutativeMoves);
        });
        return table;
    }

    constructor(_moveset: Move[]) {
        this.moveset = _moveset;
        this.identity = identity(_moveset[0].perm.n);
        this.commTable = Puzzle._getCommTable(_moveset);
    }
}

export function exec(puzzle: Puzzle, str: string): Perm {
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

function getEndTable(puzzle: Puzzle, sgs: CycPerm[][], maxWeight: number, end: Perm): Map<string, number> {
    const invTable: Map<Move, Perm> = new Map(puzzle.moveset.map((move) => [move, move.perm.inv()]));
    const endTable = new Map<string, number>();
    const stack: [Perm, number, (Move | null)][] = [[end, 0, null]];

    while (stack.length > 0) {
        const [currentPerm, currentWeight, prevMove] = stack.pop();
        const permKey = canonStr(sgs, currentPerm);

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

function* dfs(puzzle: Puzzle, sgs: CycPerm[][], maxWeight: number, start: Perm, startStr: string, endStr: string, endTable: Map<string, number>, endDepth: number, moves: Move[] = []) {
    if (startStr === endStr) {
        yield moves.reduce((partial, current) => partial + current.str + " ", "");
    }
    for (let move of puzzle.moveset) {
        let next = start.rmul(move.perm);
        let nextStr = canonStr(sgs, next);
        let distCheck = endTable.has(nextStr) ? (maxWeight-move.weight >= endTable.get(nextStr)) : (maxWeight-move.weight > endDepth);
        if ((moves.length === 0 || (puzzle.commTable.get(moves[moves.length-1]).has(move))) && distCheck) {
            yield* dfs(puzzle, sgs, maxWeight-move.weight, next, nextStr, endStr, endTable, endDepth, moves.concat([move]));
        }
    }
}

export function* solve(puzzle: Puzzle, sgs: CycPerm[][], pruneWeight: number, maxWeight: number, start: Perm, end: Perm) {
    let endTable = getEndTable(puzzle, sgs, pruneWeight, end);
    yield* dfs(puzzle, sgs, maxWeight, start, canonStr(sgs, start), canonStr(sgs, end), endTable, pruneWeight);
}
