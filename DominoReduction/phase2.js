// Phase-2 solver: given a state already reduced into G1 = <U, D, R2, L2, F2,
// B2> (corner orientation 0, edge orientation 0, E-slice pieces in E-slice
// slots -- exactly what solver.js's phase-1 search produces), find a
// shortest sequence *within G1* that finishes solving it completely.
//
// This is the second half of Kociemba's two-phase algorithm. Only the 10 G1
// generators are legal moves here: applying anything else would undo phase
// 1's work (knock corner/edge orientation away from 0).
//
// Coordinates (well-defined only once already in G1):
//   - corner permutation        (8! = 40320)
//   - U/D-face edge permutation (8! = 40320, the 8 non-slice edges)
//   - E-slice edge permutation  (4! = 24)
// exactly mirroring phase 1's "two coordinate-pair pruning tables, take the
// max" structure, and exactly like phase 1, that max is an admissible lower
// bound but not always exact -- so distance/solving both go through a full
// IDA* search (see solver.js's note on this), never a greedy heuristic walk.

import { MOVES } from "./moves-data.js";

export const G1_MOVE_NAMES = ["U", "U2", "U'", "D", "D2", "D'", "L2", "R2", "F2", "B2"];
const NUM_G1_MOVES = G1_MOVE_NAMES.length;
const FACE_OF = G1_MOVE_NAMES.map((n) => n[0]);
const FACE_AXIS = { U: 0, D: 0, L: 1, R: 1, F: 2, B: 2 };
const FACE_RANK = { U: 0, D: 1, L: 0, R: 1, F: 0, B: 1 };

const NUM_CORNER_PERM = 40320; // 8!
const NUM_UD_EDGE_PERM = 40320; // 8!
const NUM_SLICE_PERM = 24; // 4!

// ---------------------------------------------------------------------------
// Lehmer-code permutation ranking (standard factorial number system): arr is
// a permutation of {0, ..., n-1}; rank is its index in [0, n!).

function factorial(k) {
  let f = 1;
  for (let i = 2; i <= k; i++) f *= i;
  return f;
}

function permRank(arr) {
  const n = arr.length;
  const used = new Array(n).fill(false);
  let rank = 0;
  for (let i = 0; i < n; i++) {
    let smaller = 0;
    for (let v = 0; v < arr[i]; v++) if (!used[v]) smaller++;
    rank += smaller * factorial(n - 1 - i);
    used[arr[i]] = true;
  }
  return rank;
}

function permUnrank(n, rank) {
  const used = new Array(n).fill(false);
  const arr = new Array(n);
  for (let i = 0; i < n; i++) {
    const f = factorial(n - 1 - i);
    const smaller = Math.floor(rank / f);
    rank %= f;
    let v = 0;
    let count = 0;
    for (; v < n; v++) {
      if (!used[v]) {
        if (count === smaller) break;
        count++;
      }
    }
    arr[i] = v;
    used[v] = true;
  }
  return arr;
}

function applyPerm(arr, perm) {
  const n = arr.length;
  const out = new Array(n);
  for (let i = 0; i < n; i++) out[i] = arr[perm[i]];
  return out;
}

// ---------------------------------------------------------------------------
// Coordinate encode/decode from a full cubie state (see solver.js).

export function cornerPermCoord(state) {
  return permRank(state.cornerPerm);
}
export function udEdgePermCoord(state) {
  return permRank(state.edgePerm.slice(0, 8));
}
export function sliceEdgePermCoord(state) {
  return permRank(state.edgePerm.slice(8, 12).map((v) => v - 8));
}

// ---------------------------------------------------------------------------
// Per-coordinate move transition tables (only the 10 G1 moves).

function buildTable(numStates, decode, encode, applyFn) {
  const table = new Array(numStates);
  for (let s = 0; s < numStates; s++) {
    const arr = decode(s);
    const row = new Uint16Array(NUM_G1_MOVES);
    for (let m = 0; m < NUM_G1_MOVES; m++) row[m] = encode(applyFn(arr, G1_MOVE_NAMES[m]));
    table[s] = row;
  }
  return table;
}

const cornerPermTable = buildTable(
  NUM_CORNER_PERM,
  (idx) => permUnrank(8, idx),
  permRank,
  (arr, moveName) => applyPerm(arr, MOVES[moveName].CORNERS.permutation),
);

// The 8 UD-edge and 4 slice-edge sub-permutations transition independently
// under any G1 move (G1 never mixes UD-edge slots with E-slice slots), but
// building their tables goes through the shared full 12-slot permutation.
function applyToUdEdges(arr8, moveName) {
  const full = arr8.concat([8, 9, 10, 11]);
  const moved = applyPerm(full, MOVES[moveName].EDGES.permutation);
  return moved.slice(0, 8);
}
function applyToSliceEdges(arr4, moveName) {
  const full = [0, 1, 2, 3, 4, 5, 6, 7].concat(arr4.map((v) => v + 8));
  const moved = applyPerm(full, MOVES[moveName].EDGES.permutation);
  return moved.slice(8, 12).map((v) => v - 8);
}

const udEdgePermTable = buildTable(NUM_UD_EDGE_PERM, (idx) => permUnrank(8, idx), permRank, applyToUdEdges);
const sliceEdgePermTable = buildTable(NUM_SLICE_PERM, (idx) => permUnrank(4, idx), permRank, applyToSliceEdges);

// ---------------------------------------------------------------------------
// Pruning tables: BFS from solved over the combined (permutation, slice)
// coordinate pair.

function buildPruneTable(permTable) {
  const n = permTable.length * NUM_SLICE_PERM;
  const dist = new Int8Array(n).fill(-1);
  const startIdx = 0 * NUM_SLICE_PERM + 0; // solved: identity permutation, identity slice order
  dist[startIdx] = 0;
  let frontier = [startIdx];
  let depth = 0;
  let visited = 1;
  while (frontier.length > 0 && visited < n) {
    const next = [];
    for (const idx of frontier) {
      const perm = Math.floor(idx / NUM_SLICE_PERM);
      const slice = idx % NUM_SLICE_PERM;
      const permRow = permTable[perm];
      for (let m = 0; m < NUM_G1_MOVES; m++) {
        const newPerm = permRow[m];
        const newSlice = sliceEdgePermTable[slice][m];
        const newIdx = newPerm * NUM_SLICE_PERM + newSlice;
        if (dist[newIdx] === -1) {
          dist[newIdx] = depth + 1;
          next.push(newIdx);
          visited++;
        }
      }
    }
    frontier = next;
    depth++;
  }
  return dist;
}

const pruneCornerSlice = buildPruneTable(cornerPermTable);
const pruneUdEdgeSlice = buildPruneTable(udEdgePermTable);

function heuristic(corner, udEdge, slice) {
  const h1 = pruneCornerSlice[corner * NUM_SLICE_PERM + slice];
  const h2 = pruneUdEdgeSlice[udEdge * NUM_SLICE_PERM + slice];
  return Math.max(h1, h2);
}

// ---------------------------------------------------------------------------
// IDA* search (same canonical-adjacency move restriction as phase 1, applied
// to this smaller 10-move set).

function allowedNext(prevFace, face) {
  if (prevFace === null) return true;
  if (prevFace === face) return false;
  if (FACE_AXIS[prevFace] === FACE_AXIS[face]) return FACE_RANK[prevFace] < FACE_RANK[face];
  return true;
}

const MAX_SEARCH_DEPTH = 18;

function searchCoords(corner, udEdge, slice) {
  if (corner === 0 && udEdge === 0 && slice === 0) return [];

  const path = [];

  function dfs(c, u, s, g, bound, prevFace) {
    const h = heuristic(c, u, s);
    const f = g + h;
    if (f > bound) return f;
    if (h === 0 && c === 0 && u === 0 && s === 0) return "FOUND";

    let min = Infinity;
    for (let m = 0; m < NUM_G1_MOVES; m++) {
      const face = FACE_OF[m];
      if (!allowedNext(prevFace, face)) continue;
      const nc = cornerPermTable[c][m];
      const nu = udEdgePermTable[u][m];
      const ns = sliceEdgePermTable[s][m];
      path.push(G1_MOVE_NAMES[m]);
      const result = dfs(nc, nu, ns, g + 1, bound, face);
      if (result === "FOUND") return "FOUND";
      if (result < min) min = result;
      path.pop();
    }
    return min;
  }

  let bound = heuristic(corner, udEdge, slice);
  while (bound <= MAX_SEARCH_DEPTH) {
    const result = dfs(corner, udEdge, slice, 0, bound, null);
    if (result === "FOUND") return path.slice();
    if (result === Infinity) break;
    bound = result;
  }
  throw new Error("Phase 2: no solution found within depth limit");
}

// Shortest sequence of G1 moves that fully solves a state already in G1.
// (Behavior is undefined if the state isn't actually in G1 -- callers should
// run phase 1 first.)
export function solvePhase2(state) {
  return searchCoords(cornerPermCoord(state), udEdgePermCoord(state), sliceEdgePermCoord(state));
}
