import { solve, applyMoveName, solvedState, distanceOfState, MOVE_NAMES } from "../solver.js";
import { randomStateScramble, invertMoves, fullSolve, randomValidState, mergeAdjacentSameFace } from "../twophase.js";

let failures = 0;
function check(label, ok) {
  console.log(`${ok ? "ok" : "FAIL"} - ${label}`);
  if (!ok) failures++;
}

// Phase-1 solver: known examples.
const cases = [
  { scramble: "L' U R' U2", expected: "L U' R" },
  { scramble: "R2 U R2 U F2 R2 F B R2 F B", expected: "" },
  { scramble: "R' U R' F' R U R' U' R'", expected: "F R2 U' R" },
];
for (const { scramble, expected } of cases) {
  const sol = solve(scramble);
  check(`solve("${scramble}") === "${expected}"`, sol.join(" ") === expected);
}

// Every non-solved state reachable from a domino-reduction scramble must
// have at least one move that strictly reduces distanceOfState (regression
// test for the heuristic-vs-exact-distance bug).
for (let i = 0; i < 20; i++) {
  let state = solvedState();
  for (let j = 0; j < 20; j++) state = applyMoveName(state, MOVE_NAMES[Math.floor(Math.random() * MOVE_NAMES.length)]);
  const dist = distanceOfState(state);
  if (dist === 0) continue;
  const anyAccept = MOVE_NAMES.some((m) => distanceOfState(applyMoveName(state, m)) < dist);
  check(`non-solved state at distance ${dist} has an accepting move`, anyAccept);
}

// Two-phase: random-state scrambles round-trip exactly, contain no
// adjacent-same-face moves, and mergeAdjacentSameFace is a correct identity
// on already-clean sequences.
for (let i = 0; i < 20; i++) {
  const target = randomValidState();
  const solution = fullSolve(target);
  const scramble = invertMoves(solution);

  let state = solvedState();
  for (const m of scramble) state = applyMoveName(state, m);
  check(`random-state scramble ${i} round-trips to the intended state`, JSON.stringify(state) === JSON.stringify(target));

  const clean = scramble.every((m, idx) => idx === 0 || m[0] !== scramble[idx - 1][0]);
  check(`random-state scramble ${i} has no adjacent same-face moves`, clean);
}

check("mergeAdjacentSameFace(R U R') === R U R'", mergeAdjacentSameFace(["R", "U", "R'"]).join(" ") === "R U R'");
check("mergeAdjacentSameFace(R R R2) === (empty)", mergeAdjacentSameFace(["R", "R", "R2"]).length === 0);
check("mergeAdjacentSameFace(R U U' R2) === R'", mergeAdjacentSameFace(["R", "U", "U'", "R2"]).join(" ") === "R'");

console.log(failures === 0 ? "\nAll checks passed." : `\n${failures} check(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
