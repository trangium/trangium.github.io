// Combines the phase-1 (solver.js: general -> G1) and phase-2 (phase2.js:
// G1 -> solved) solvers into a full two-phase 3x3x3 solver, and uses it to
// generate genuine random-*state* scrambles: pick a uniformly random valid
// cube state, solve it, and invert that solution. Applying the inverse to a
// solved cube reproduces exactly that random state, so (unlike a random
// *move* scramble) every reachable cube state is equally likely.

import { MOVE_NAMES, applyMoveName, solvePhase1FromState } from "./solver.js";
import { solvePhase2 } from "./phase2.js";

const INVERSE_OF = {};
for (const name of MOVE_NAMES) {
  const face = name[0];
  const suffix = name.slice(1);
  INVERSE_OF[name] = suffix === "'" ? face : suffix === "2" ? name : face + "'";
}

export function invertMoves(moves) {
  return moves
    .slice()
    .reverse()
    .map((m) => INVERSE_OF[m]);
}

// Neither phase-1 nor phase-2's own search ever produces two adjacent moves
// on the same face (each has its own canonical-adjacency restriction), but
// concatenating their two independently-searched solutions can leave exactly
// one such seam (e.g. phase 1 ending in "...L2" right where phase 2 begins
// with "L'..."), which trivially simplifies (here to a single "L"). Collapse
// any such adjacent same-face run -- including cascades exposed by a full
// cancellation -- into its simplest form.
const AMOUNT_OF_SUFFIX = { "": 1, "2": 2, "'": 3 };
const SUFFIX_OF_AMOUNT = { 1: "", 2: "2", 3: "'" };

export function mergeAdjacentSameFace(moves) {
  const result = [];
  for (const mv of moves) {
    const face = mv[0];
    let amt = AMOUNT_OF_SUFFIX[mv.slice(1)];
    let cancelled = false;
    while (result.length > 0 && result[result.length - 1][0] === face) {
      const prev = result.pop();
      amt = (AMOUNT_OF_SUFFIX[prev.slice(1)] + amt) % 4;
      if (amt === 0) {
        cancelled = true;
        break;
      }
    }
    if (!cancelled) result.push(face + SUFFIX_OF_AMOUNT[amt]);
  }
  return result;
}

// A uniformly random legal cube state: independent uniformly random corner
// and edge permutations, corrected so their combined permutation parity is
// even (swap two edges if the parities don't already match), plus
// independently random orientations with the last value forced so the total
// is 0 mod 3 (corners) / mod 2 (edges) -- the standard necessary-and-
// sufficient validity conditions for a reachable 3x3x3 state.
function randomPermutation(n) {
  const arr = Array.from({ length: n }, (_, i) => i);
  for (let i = n - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}
function permutationParity(arr) {
  const n = arr.length;
  const visited = new Array(n).fill(false);
  let parity = 0;
  for (let i = 0; i < n; i++) {
    if (visited[i]) continue;
    let len = 0;
    let j = i;
    while (!visited[j]) {
      visited[j] = true;
      j = arr[j];
      len++;
    }
    if (len > 0) parity ^= (len - 1) % 2;
  }
  return parity;
}

export function randomValidState() {
  const cornerPerm = randomPermutation(8);
  const edgePerm = randomPermutation(12);
  if (permutationParity(cornerPerm) !== permutationParity(edgePerm)) {
    [edgePerm[0], edgePerm[1]] = [edgePerm[1], edgePerm[0]];
  }

  const cornerOri = Array.from({ length: 8 }, () => Math.floor(Math.random() * 3));
  let cSum = 0;
  for (let i = 0; i < 7; i++) cSum += cornerOri[i];
  cornerOri[7] = (3 - (cSum % 3)) % 3;

  const edgeOri = Array.from({ length: 12 }, () => Math.floor(Math.random() * 2));
  let eSum = 0;
  for (let i = 0; i < 11; i++) eSum += edgeOri[i];
  edgeOri[11] = (2 - (eSum % 2)) % 2;

  return { cornerPerm, cornerOri, edgePerm, edgeOri };
}

// Full solve: phase-1 moves (-> G1) followed by phase-2 moves (G1 ->
// solved). Not guaranteed globally shortest (a true optimal two-phase solver
// tries multiple phase-1 solutions of near-minimal length and keeps the best
// combined result) but always correct, and each phase is itself optimal.
export function fullSolve(state) {
  const phase1Moves = solvePhase1FromState(state);
  let s = state;
  for (const m of phase1Moves) s = applyMoveName(s, m);
  const phase2Moves = solvePhase2(s);
  return mergeAdjacentSameFace(phase1Moves.concat(phase2Moves));
}

export function randomStateScramble() {
  const state = randomValidState();
  const solution = fullSolve(state);
  return solution;
}
