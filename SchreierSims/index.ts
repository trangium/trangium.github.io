import {schreierSims} from "./SchreierSims.js";
import {Perm, identity} from "./Perm.js";

function cleansgs(sgs: Perm[][]): void {
    for (let i=0; i<sgs.length; i++) {
        sgs[i] = sgs[i].filter(x => x).map(perm => perm.inv());
    }
}

function canonicalize(sgs: Perm[][], state: Perm): Perm {
    for (let stabIndex = sgs.length-1; stabIndex >= 0; stabIndex--) {
        let stabilizers = sgs[stabIndex];
        let max_stabilizer: Perm;

        // choose stabilizer to maximize stabilizer[state[stabIndex]]
        {
            max_stabilizer = stabilizers[0];
            let max_val = -1;
            for (let stabilizer of stabilizers) {
                let val = stabilizer.at(state.at(stabIndex));
                if (val > max_val) {
                    max_val = val;
                    max_stabilizer = stabilizer;
                }
            }
        }

        state = state.mul(max_stabilizer);
    }
    return state;
}

function canonStr(sgs: Perm[][], state: Perm): string {
    return canonicalize(sgs, state).toString();
}

function exec(puzzle: Puzzle, str: string): Perm {
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

type Move = {
    str: string;
    perm: Perm;
    weight: number;
}

class Puzzle {
    public moveset: Move[];
    public commTable: Map<Move, Set<Move>>;
    public identity: Perm;
    public sgs: Perm[][];

    private static _isValidPair(permset: Set<String>, i: Perm, j: Perm) {
        if (!j.commutes(i)) {return true};
        if (permset.has(j.mul(i).toString())) {return false};
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

    constructor(_moveset: Move[], _subgroup: Perm[]) {
        this.moveset = _moveset;
        this.identity = identity(_moveset[0].perm.n);
        this.commTable = Puzzle._getCommTable(_moveset);
        this.sgs = schreierSims(_subgroup);
        cleansgs(this.sgs);
    }
}

function getEndTable(puzzle: Puzzle, maxWeight: number, end: Perm): Map<string, number> {
    const invTable: Map<Move, Perm> = new Map(puzzle.moveset.map((move) => [move, move.perm.inv()]));
    const endTable = new Map<string, number>();
    const stack: [Perm, number, (Move | null)][] = [[end, 0, null]];

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

function* dfs(puzzle: Puzzle, maxWeight: number, start: Perm, startStr: string, endStr: string, endTable: Map<string, number>, endDepth: number, moves: Move[] = []) {
    if (startStr === endStr) {
        yield moves.reduce((partial, current) => partial + current.str + " ", "");
    }
    for (let move of puzzle.moveset) {
        let next = start.rmul(move.perm);
        let nextStr = canonStr(puzzle.sgs, next);
        let distCheck = endTable.has(nextStr) ? (maxWeight-move.weight >= endTable.get(nextStr)) : (maxWeight-move.weight > endDepth);
        if ((moves.length === 0 || (puzzle.commTable.get(moves[moves.length-1]).has(move))) && distCheck) {
            yield* dfs(puzzle, maxWeight-move.weight, next, nextStr, endStr, endTable, endDepth, moves.concat([move]));
        }
    }
}

function* solve(puzzle: Puzzle, pruneWeight: number, maxWeight: number, start: Perm, end: Perm) {
    let endTable = getEndTable(puzzle, pruneWeight, end);
    yield* dfs(puzzle, maxWeight, start, canonStr(puzzle.sgs, start), canonStr(puzzle.sgs, end), endTable, pruneWeight);
}

// console.log(canonicalize(sgs, new Perm([2, 4, 3, 0, 1]))); // expected: [0, 2, 1, 3, 4]

let U1: Move = {str: "U", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]), weight: 1}
let U2: Move = {str: "U2", perm: U1.perm.mul(U1.perm), weight: 1}
let U3: Move = {str: "U'", perm: U2.perm.mul(U1.perm), weight: 1}
let D1: Move = {str: "D", perm: new Perm([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18, 19, 12, 13, 14, 15, 16, 17]), weight: 1}
let D2: Move = {str: "D2", perm: D1.perm.mul(D1.perm), weight: 1}
let D3: Move = {str: "D'", perm: D2.perm.mul(D1.perm), weight: 1}
let y1: Move = {str: "y", perm: new Perm([6, 7, 0, 1, 2, 3, 4, 5, 11, 8, 9, 10, 14, 15, 16, 17, 18, 19, 12, 13]), weight: 10}
let R2: Move = {str: "R2", perm: new Perm([0, 1, 14, 15, 16, 5, 6, 7, 8, 10, 9, 11, 12, 13, 2, 3, 4, 17, 18, 19]), weight: 1}
let B2: Move = {str: "B2", perm: y1.perm.rmul(R2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}
let L2: Move = {str: "L2", perm: y1.perm.rmul(B2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}
let F2: Move = {str: "F2", perm: y1.perm.rmul(L2.perm.rmul(y1.perm.rmul(y1.perm.rmul(y1.perm)))), weight: 1}

let drhtr = new Puzzle([U1, U2, U3, D1, D2, D3, R2, B2, L2, F2], [U2.perm, D2.perm, R2.perm, B2.perm, L2.perm, F2.perm]);

let gen = solve(drhtr, 6, 12, exec(drhtr, "R2 U' R2 U' R2 U R2 D' R2 U R2 U' R2 D R2"), 
    drhtr.identity);

for (let val of gen) {
    console.log(val);
}