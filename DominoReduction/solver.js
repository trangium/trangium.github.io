// Domino Reduction solver.
//
// Finds a shortest sequence of face turns (from the standard 18-move HTM set)
// that brings a scrambled cube into G1 = <U, D, R2, L2, F2, B2> -- i.e. the
// shortest path to "solved" in the quotient G0 / G1, where:
//   - corners only carry orientation (their permutation is forgotten)
//   - the 8 U/D-face edges only carry orientation (their permutation is
//     forgotten)
//   - the 4 equator (E-slice) edges keep full identity + orientation, but
//     since G1 acts transitively on their arrangement within the E-slice,
//     membership in G1 only requires that E-slice pieces occupy E-slice
//     slots (their particular arrangement there is irrelevant).
//
// This is exactly Kociemba's phase-1 reduction, solved with IDA* over three
// independent coordinates:
//   - corner orientation (3^7 = 2187 states)
//   - edge orientation   (2^11 = 2048 states)
//   - E-slice location   (C(12,4) = 495 states: which 4 of the 12 edge slots
//                          are occupied by E-slice pieces)

import { MOVES, E_SLICE_EDGE_INDICES } from "./moves-data.js";

export const MOVE_NAMES = Object.keys(MOVES);
const NUM_MOVES = MOVE_NAMES.length; // 18

const FACE_OF = MOVE_NAMES.map((n) => n[0]);
const FACE_AXIS = { U: 0, D: 0, L: 1, R: 1, F: 2, B: 2 };
const FACE_RANK = { U: 0, D: 1, L: 0, R: 1, F: 0, B: 1 }; // lower rank goes first on an axis

const CORNER_MOD = 3;
const EDGE_MOD = 2;
const NUM_CORNER_ORI = 2187; // 3^7
const NUM_EDGE_ORI = 2048; // 2^11
const NUM_SLICE = 495; // C(12,4)

// ---------------------------------------------------------------------------
// Combination table for the E-slice location coordinate.

const combos = [];
for (let a = 0; a < 12; a++)
  for (let b = a + 1; b < 12; b++)
    for (let c = b + 1; c < 12; c++)
      for (let d = c + 1; d < 12; d++) combos.push([a, b, c, d]);
combos.sort((x, y) => x[0] - y[0] || x[1] - y[1] || x[2] - y[2] || x[3] - y[3]);
if (combos.length !== NUM_SLICE) throw new Error("combo table size mismatch");
const comboIndex = new Map();
combos.forEach((combo, i) => comboIndex.set(combo.join(","), i));

const E_SLICE_SET = new Set(E_SLICE_EDGE_INDICES);

const IDENTITY_TYPE = new Array(12).fill(0);
for (const i of E_SLICE_EDGE_INDICES) IDENTITY_TYPE[i] = 1;
const IDENTITY_SLICE = encodeSlice(IDENTITY_TYPE);

function encodeSlice(typeArr) {
  const positions = [];
  for (let i = 0; i < 12; i++) if (typeArr[i] === 1) positions.push(i);
  return comboIndex.get(positions.join(","));
}
function decodeSlice(index) {
  const arr = new Array(12).fill(0);
  for (const p of combos[index]) arr[p] = 1;
  return arr;
}

// Which of the 12 edge slots currently hold an E-slice piece, derived from a
// full edge-permutation array (edgePerm[slot] = original identity of the
// piece now in that slot).
function sliceTypeFromEdgePerm(edgePerm) {
  return edgePerm.map((v) => (E_SLICE_SET.has(v) ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Corner / edge orientation coordinate encode/decode. The last component is
// determined by the invariant that the total orientation sums to 0 mod base.

function encodeCornerOri(arr) {
  let idx = 0;
  for (let i = 0; i < 7; i++) idx = idx * CORNER_MOD + arr[i];
  return idx;
}
function decodeCornerOri(idx) {
  const arr = new Array(8).fill(0);
  let sum = 0;
  for (let i = 6; i >= 0; i--) {
    arr[i] = idx % CORNER_MOD;
    idx = Math.floor(idx / CORNER_MOD);
    sum += arr[i];
  }
  arr[7] = ((CORNER_MOD - (sum % CORNER_MOD)) % CORNER_MOD);
  return arr;
}
function encodeEdgeOri(arr) {
  let idx = 0;
  for (let i = 0; i < 11; i++) idx = idx * EDGE_MOD + arr[i];
  return idx;
}
function decodeEdgeOri(idx) {
  const arr = new Array(12).fill(0);
  let sum = 0;
  for (let i = 10; i >= 0; i--) {
    arr[i] = idx % EDGE_MOD;
    idx = Math.floor(idx / EDGE_MOD);
    sum += arr[i];
  }
  arr[11] = ((EDGE_MOD - (sum % EDGE_MOD)) % EDGE_MOD);
  return arr;
}

// ---------------------------------------------------------------------------
// Apply a move's transformation to a "pattern" array (values indexed by
// position). Matches cubing.js's transformation-application convention:
//   newVal[idx] = (val[perm[idx]] + delta[idx]) % mod
// (delta all-zero and mod=1 gives pure permutation pull, used for the
// E-slice type array.)

function applyOri(arr, perm, delta, mod) {
  const n = arr.length;
  const out = new Array(n);
  for (let i = 0; i < n; i++) out[i] = (arr[perm[i]] + delta[i]) % mod;
  return out;
}
function applyPerm(arr, perm) {
  const n = arr.length;
  const out = new Array(n);
  for (let i = 0; i < n; i++) out[i] = arr[perm[i]];
  return out;
}

// ---------------------------------------------------------------------------
// Per-coordinate move transition tables.

function buildTable(numStates, decode, encode, applyFn) {
  const table = new Array(numStates);
  for (let s = 0; s < numStates; s++) {
    const arr = decode(s);
    const row = new Uint16Array(NUM_MOVES);
    for (let m = 0; m < NUM_MOVES; m++) {
      row[m] = encode(applyFn(arr, MOVE_NAMES[m]));
    }
    table[s] = row;
  }
  return table;
}

const cornerOriTable = buildTable(
  NUM_CORNER_ORI,
  decodeCornerOri,
  encodeCornerOri,
  (arr, moveName) => applyOri(arr, MOVES[moveName].CORNERS.permutation, MOVES[moveName].CORNERS.orientationDelta, CORNER_MOD),
);
const edgeOriTable = buildTable(
  NUM_EDGE_ORI,
  decodeEdgeOri,
  encodeEdgeOri,
  (arr, moveName) => applyOri(arr, MOVES[moveName].EDGES.permutation, MOVES[moveName].EDGES.orientationDelta, EDGE_MOD),
);
const sliceTable = buildTable(
  NUM_SLICE,
  decodeSlice,
  encodeSlice,
  (arr, moveName) => applyPerm(arr, MOVES[moveName].EDGES.permutation),
);

// ---------------------------------------------------------------------------
// Pruning tables: BFS from the solved coset over the combined
// (orientation, slice) coordinate pair.

function buildPruneTable(oriTable, oriGoal) {
  const n = oriTable.length * NUM_SLICE;
  const dist = new Int8Array(n).fill(-1);
  const startIdx = oriGoal * NUM_SLICE + IDENTITY_SLICE;
  dist[startIdx] = 0;
  let frontier = [startIdx];
  let depth = 0;
  let visited = 1;
  while (frontier.length > 0 && visited < n) {
    const next = [];
    for (const idx of frontier) {
      const ori = Math.floor(idx / NUM_SLICE);
      const slice = idx % NUM_SLICE;
      const oriRow = oriTable[ori];
      for (let m = 0; m < NUM_MOVES; m++) {
        const newOri = oriRow[m];
        const newSlice = sliceTable[slice][m];
        const newIdx = newOri * NUM_SLICE + newSlice;
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

const pruneCornerSlice = buildPruneTable(cornerOriTable, 0);
const pruneEdgeSlice = buildPruneTable(edgeOriTable, 0);

function heuristic(c, e, s) {
  const h1 = pruneCornerSlice[c * NUM_SLICE + s];
  const h2 = pruneEdgeSlice[e * NUM_SLICE + s];
  return Math.max(h1, h2);
}

// ---------------------------------------------------------------------------
// Move-sequence application (for turning a scramble string into starting
// coordinates).

// Full cubie-level state: cornerPerm[slot]/edgePerm[slot] is the identity
// (original index) of the piece currently in that slot; cornerOri[slot]/
// edgeOri[slot] is its orientation. Tracking full permutation identity (not
// just phase-1's orientation + E-slice-membership) is what lets solver2.js
// solve a position *within* G1 down to fully solved.
export function applyMoveName(state, moveName) {
  const move = MOVES[moveName];
  return {
    cornerPerm: applyPerm(state.cornerPerm, move.CORNERS.permutation),
    cornerOri: applyOri(state.cornerOri, move.CORNERS.permutation, move.CORNERS.orientationDelta, CORNER_MOD),
    edgePerm: applyPerm(state.edgePerm, move.EDGES.permutation),
    edgeOri: applyOri(state.edgeOri, move.EDGES.permutation, move.EDGES.orientationDelta, EDGE_MOD),
  };
}

export function solvedState() {
  return {
    cornerPerm: [0, 1, 2, 3, 4, 5, 6, 7],
    cornerOri: new Array(8).fill(0),
    edgePerm: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    edgeOri: new Array(12).fill(0),
  };
}

export function parseScramble(scramble) {
  const trimmed = scramble.trim();
  if (trimmed === "") return [];
  const tokens = trimmed.split(/\s+/);
  const moves = [];
  for (const tok of tokens) {
    const m = /^([UDLRFB])(2|')?$/.exec(tok);
    if (!m) throw new Error(`Invalid move: "${tok}"`);
    const face = m[1];
    const suffix = m[2] || "";
    const name = face + suffix;
    if (!(name in MOVES)) throw new Error(`Invalid move: "${tok}"`);
    moves.push(name);
  }
  return moves;
}

export function coordsOfMoveSequence(moveNames) {
  let state = solvedState();
  for (const name of moveNames) state = applyMoveName(state, name);
  return stateToCoords(state);
}

export function stateToCoords(state) {
  return {
    corner: encodeCornerOri(state.cornerOri),
    edge: encodeEdgeOri(state.edgeOri),
    slice: encodeSlice(sliceTypeFromEdgePerm(state.edgePerm)),
  };
}

// ---------------------------------------------------------------------------
// IDA* search.
//
// Note: heuristic(c, e, s) -- the max of the two coordinate-pair pruning
// tables -- is only a lower bound on the true distance to G1, not always
// exact. (Each pruning table is exact *within its own relaxed quotient*, but
// the max of two such values can still fall strictly short of the true
// distance at some states, so a plain "does any single move reduce the
// heuristic" check can wrongly reject every move at a non-solved state.) The
// full IDA* search below, which backtracks across increasing bounds rather
// than requiring the heuristic to decrease every step, still finds the true
// shortest path -- that's the only exact source of distance in this module.

function allowedNext(prevFace, face) {
  if (prevFace === null) return true;
  if (prevFace === face) return false; // no point repeating the same face
  if (FACE_AXIS[prevFace] === FACE_AXIS[face]) {
    // opposite faces on the same axis commute -- only allow canonical order
    return FACE_RANK[prevFace] < FACE_RANK[face];
  }
  return true;
}

const MAX_SEARCH_DEPTH = 13;

// Exact shortest move sequence from the given (corner, edge, slice) triple to
// G1 (empty array if already in G1). This is the single source of truth for
// "distance to G1" in this module -- see the note above allowedNext() for why
// the pruning-table heuristic alone is not enough.
function searchCoords(corner, edge, slice) {
  if (corner === 0 && edge === 0 && slice === IDENTITY_SLICE) {
    return [];
  }

  const path = [];

  function dfs(c, e, s, g, bound, prevFace) {
    const h = heuristic(c, e, s);
    const f = g + h;
    if (f > bound) return f;
    if (h === 0 && c === 0 && e === 0 && s === IDENTITY_SLICE) return "FOUND";

    let min = Infinity;
    for (let m = 0; m < NUM_MOVES; m++) {
      const name = MOVE_NAMES[m];
      const face = FACE_OF[m];
      if (!allowedNext(prevFace, face)) continue;
      const nc = cornerOriTable[c][m];
      const ne = edgeOriTable[e][m];
      const ns = sliceTable[s][m];
      path.push(name);
      const result = dfs(nc, ne, ns, g + 1, bound, face);
      if (result === "FOUND") return "FOUND";
      if (result < min) min = result;
      path.pop();
    }
    return min;
  }

  let bound = heuristic(corner, edge, slice);
  while (bound <= MAX_SEARCH_DEPTH) {
    const result = dfs(corner, edge, slice, 0, bound, null);
    if (result === "FOUND") return path.slice();
    if (result === Infinity) break;
    bound = result;
  }
  throw new Error("No solution found within depth limit");
}

export function solve(scramble) {
  const moveNames = parseScramble(scramble);
  const { corner, edge, slice } = coordsOfMoveSequence(moveNames);
  return searchCoords(corner, edge, slice);
}

// Shortest phase-1 move sequence (general position -> G1) starting from an
// arbitrary full cubie state, rather than from a scramble string applied to
// solved. Used by twophase.js to reduce a random state before phase 2 takes
// over.
export function solvePhase1FromState(state) {
  const { corner, edge, slice } = stateToCoords(state);
  return searchCoords(corner, edge, slice);
}

// Exact word-distance from the given state to G1.
export function distanceOfState(state) {
  return solvePhase1FromState(state).length;
}
