import { identity } from "./Perm.js";
export class Puzzle {
    constructor(moves, coords) {
        this.moves = moves;
        this.gelemTable = [];
        this.orbitIndices = new Map();
        let globalOrbitIndex = 0;
        for (let coordinate of coords) {
            const orbits = this.getOrbits(moves, coordinate);
            // Give each orbit an index
            for (let orbit of orbits) {
                const orbitKey = this.getCoordPermKey(orbit, coordinate);
                if (!this.orbitIndices.has(orbitKey)) {
                    this.orbitIndices.set(orbitKey, globalOrbitIndex++);
                }
            }
            // Calculate transitions for each move and orbit
            for (let moveInd = 0; moveInd < moves.length; moveInd++) {
                let move = moves[moveInd];
                this.gelemTable.push([]); // ugly hack TODO: fix this
                for (let orbit of orbits) {
                    const initialOrbitKey = this.getCoordPermKey(orbit, coordinate);
                    orbit = this.executePerm(orbit, move);
                    const finalOrbitKey = this.getCoordPermKey(orbit, coordinate);
                    const initialIndex = this.orbitIndices.get(initialOrbitKey);
                    const finalIndex = this.orbitIndices.get(finalOrbitKey);
                    this.gelemTable[moveInd][initialIndex] = finalIndex;
                }
            }
        }
    }
    getOrbits(moves, coordinate) {
        // this returns a list distinct permutations reachable from the identity
        // using permutations in moves, where
        // permutations that differ only in entries not within the coordinate set
        // are considered equivalent, and this list is as large as possible
        // that is, all distinct permutations under this equivalence relation are reached
        // TODO: Use Schreier-Sims to do this with drastically lower memory usage
        // would use O(n) space where n = (size of permutation), by using yield keyword
        const reached = new Map();
        const identityKey = this.getPermKey(this.identity());
        let newlyReached = new Map([[identityKey, this.identity()]]);
        const allOrbits = new Set();
        while (newlyReached.size > 0) {
            const nextNewlyReached = new Map();
            for (const [permKey, currentPerm] of newlyReached) {
                for (const move of moves) {
                    const newPerm = this.executePerm(currentPerm, move);
                    const newPermKey = this.getPermKey(newPerm);
                    if (!reached.has(newPermKey)) {
                        nextNewlyReached.set(newPermKey, newPerm);
                        reached.set(newPermKey, newPerm);
                        allOrbits.add(newPerm);
                    }
                }
            }
            newlyReached = nextNewlyReached;
        }
        return allOrbits;
    }
    getCoordPermKey(perm, coordinate) {
        // Create a unique key for a perm, considering only the relevant coordinates
        // TODO?: generate the key using only base elements (requires Schreier-Sims)
        return coordinate.map(i => perm.p[i]).concat(coordinate[0]).join(',');
    }
    getPermKey(perm) {
        // Create a unique key for a perm, considering all coordinates
        // TODO: generate the key using only base elements (requires Schreier-Sims)
        return perm.p.join(',');
    }
    identity() {
        return identity(this.moves[0].n);
    }
    executePerm(start, move) {
        return start.mul(move);
    }
    execute(start, move) {
        return start.mul(this.moves[move]);
    }
    execiGelem(start, move) {
        start.p.map(elem => this.gelemTable[move][elem]);
    }
}
