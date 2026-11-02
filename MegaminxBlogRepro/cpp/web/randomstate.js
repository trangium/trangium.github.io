// Uniform random Megaminx states.  Loaded by worker.js (importScripts) and by the Node tests (require).
// A state is 120 sticker ids: state[position] = the id (home position) of the sticker sitting there; solved = 0..119.  Centres never move and are not among the 120.
// The reachable states are exactly: corner permutation EVEN, edge permutation EVEN (a face turn is a 5-cycle of corners and a 5-cycle of edges), sum of the corner twists 0 mod 3, sum of the edge flips 0 mod 2
// (20!/2 * 3^19 * 30!/2 * 2^29 states).  So: Fisher-Yates for each permutation (an odd one is made even by swapping two entries: a bijection of odd onto even permutations, so the result is uniform),
// random orientations for all pieces but the last, which takes the value that makes the sum right.
// Tables (positions of each corner / edge, corners in one rotational direction): python gen_cubies.py  (read from the model's move tables, with a check on random walks).
const CORNERS = [[0,1,2],[3,4,5],[8,6,7],[10,11,9],[14,12,13],[17,15,16],[18,19,20],[21,22,23],[25,26,24],[29,27,28],[30,31,32],[35,33,34],[38,36,37],[39,40,41],[43,44,42],[46,47,45],[49,50,48],[51,52,53],[54,55,56],[57,58,59]];
const EDGES = [[60,61],[62,63],[64,65],[66,67],[68,69],[70,71],[72,73],[74,75],[76,77],[78,79],[80,81],[82,83],[84,85],[86,87],[88,89],[90,91],[92,93],[94,95],[96,97],[98,99],[100,101],[102,103],[104,105],[106,107],[108,109],[110,111],[112,113],[114,115],[116,117],[118,119]];

// uniform integer in [0, n) from crypto (rejection sampling: no modulo bias)
function randomBelow(n) {
    const buf = new Uint32Array(1), limit = 4294967296 - (4294967296 % n);
    do { crypto.getRandomValues(buf); } while (buf[0] >= limit);
    return buf[0] % n;
}

function permutationParity(p) {       // 0 even, 1 odd: (length - number of cycles) mod 2
    const seen = new Array(p.length).fill(false);
    let cycles = 0;
    for (let i = 0; i < p.length; i++) {
        if (seen[i]) continue;
        cycles++;
        for (let j = i; !seen[j]; j = p[j]) seen[j] = true;
    }
    return (p.length - cycles) & 1;
}

// uniform random EVEN permutation of 0..n-1 (Fisher-Yates)
function randomEvenPermutation(n, below) {
    const p = Array.from({ length: n }, (_, i) => i);
    for (let i = n - 1; i > 0; i--) {
        const j = below(i + 1);
        [p[i], p[j]] = [p[j], p[i]];
    }
    if (permutationParity(p)) [p[0], p[1]] = [p[1], p[0]];
    return p;
}

// n orientations in 0..k-1 whose sum is 0 mod k, uniform
function randomTwists(n, k, below) {
    const t = [];
    let sum = 0;
    for (let i = 0; i < n - 1; i++) { t.push(below(k)); sum += t[i]; }
    t.push((k - sum % k) % k);
    return t;
}

// `below(n)`: uniform integer in [0, n) (default: crypto; the tests pass a seeded one)
function randomState(below) {
    below = below || randomBelow;
    const state = new Array(120);
    for (const [pieces, k] of [[CORNERS, 3], [EDGES, 2]]) {
        const perm = randomEvenPermutation(pieces.length, below), twist = randomTwists(pieces.length, k, below);
        for (let slot = 0; slot < pieces.length; slot++) {
            const cubie = pieces[perm[slot]];                                 // the piece sitting in this slot, turned by twist[slot]
            for (let i = 0; i < k; i++) state[pieces[slot][i]] = cubie[(i + twist[slot]) % k];
        }
    }
    return state;
}

if (typeof module !== 'undefined') module.exports = { randomState, randomBelow, permutationParity, CORNERS, EDGES };
