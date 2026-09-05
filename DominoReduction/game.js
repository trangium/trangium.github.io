import { applyMoveName, solvedState, distanceOfState, parseScramble, MOVE_NAMES } from "./solver.js";
import { randomStateScramble } from "./twophase.js";

let state, distance, scramble;
let triesThisStep, scoreFactors, finished, acceptedMoves;

const scrambleEl = document.getElementById("scramble");
const movesSoFarEl = document.getElementById("movesSoFar");
const distanceEl = document.getElementById("distance");
const logEl = document.getElementById("log");
const moveInput = document.getElementById("moveInput");
const submitBtn = document.getElementById("submitBtn");
const newScrambleBtn = document.getElementById("newScrambleBtn");
const scoreEl = document.getElementById("score");
const buttonsEl = document.getElementById("moveButtons");

function logLine(text, cls) {
  const div = document.createElement("div");
  div.className = "logline" + (cls ? " " + cls : "");
  div.textContent = text;
  logEl.appendChild(div);
  logEl.scrollTop = logEl.scrollHeight;
}

function logBlank() {
  const div = document.createElement("div");
  div.className = "logblank";
  logEl.appendChild(div);
}

function updateStatus() {
  distanceEl.textContent = finished ? "0 (solved!)" : String(distance);
  if (scoreFactors.length === 0) {
    scoreEl.textContent = finished ? "1 (empty product)" : "—";
  } else {
    const runningProduct = scoreFactors.reduce((a, b) => a * b, 1);
    scoreEl.textContent = finished
      ? `${scoreFactors.join(" × ")} = ${runningProduct}`
      : `${scoreFactors.join(" × ")} × ... (so far: ${runningProduct})`;
  }
  moveInput.disabled = finished;
  submitBtn.disabled = finished;
  for (const btn of buttonsEl.querySelectorAll("button")) btn.disabled = finished;
}

function acceptedMovesFrom(fromState, fromDistance) {
  return MOVE_NAMES.filter((m) => distanceOfState(applyMoveName(fromState, m)) < fromDistance);
}

function finishGame() {
  finished = true;
  const score = scoreFactors.reduce((a, b) => a * b, 1);
  const breakdown = scoreFactors.length ? scoreFactors.join(" × ") : "(empty product)";
  logBlank();
  logLine(`Solved! Score = ${breakdown} = ${score}`, "final");
}

function submitMove(moveName) {
  if (finished) return;
  const newState = applyMoveName(state, moveName);
  const newDistance = distanceOfState(newState);

  if (newDistance < distance) {
    const others = acceptedMovesFrom(state, distance).filter((m) => m !== moveName);
    const note = others.length ? ` (${others.join(", ")} would also accept)` : "";
    logLine(`${moveName} -> accepted!${note}`, "accept");
    scoreFactors.push(triesThisStep);
    acceptedMoves.push(moveName);
    movesSoFarEl.textContent = acceptedMoves.join(" ");
    state = newState;
    distance = newDistance;
    triesThisStep = 1;
    if (distance === 0) {
      finishGame();
    } else {
      logBlank();
    }
  } else {
    logLine(`${moveName} -> rejected`, "reject");
    triesThisStep++;
  }
  updateStatus();
}

function newScramble() {
  // Random-state scramble generation (uniformly random legal cube state,
  // solved and inverted) runs a real two-phase solve under the hood and can
  // take a few hundred ms, so show a "generating" state and let it paint
  // before running the blocking computation.
  newScrambleBtn.disabled = true;
  newScrambleBtn.textContent = "Generating…";
  moveInput.disabled = true;
  submitBtn.disabled = true;
  for (const btn of buttonsEl.querySelectorAll("button")) btn.disabled = true;

  setTimeout(() => {
    scramble = randomStateScramble();
    state = solvedState();
    for (const m of scramble) state = applyMoveName(state, m);
    distance = distanceOfState(state);
    triesThisStep = 1;
    scoreFactors = [];
    acceptedMoves = [];
    finished = false;

    scrambleEl.textContent = scramble.join(" ");
    movesSoFarEl.textContent = "";
    logEl.innerHTML = "";
    moveInput.value = "";
    newScrambleBtn.disabled = false;
    newScrambleBtn.textContent = "New Scramble";
    moveInput.disabled = false;
    submitBtn.disabled = false;
    moveInput.focus();

    if (distance === 0) {
      logLine("Scramble is already in G1 -- nothing to do!", "info");
      finishGame();
    }
    updateStatus();
  }, 0);
}

function handleTextSubmit() {
  const raw = moveInput.value.trim();
  if (raw === "") return;
  let moves;
  try {
    moves = parseScramble(raw);
  } catch (e) {
    logLine(e.message, "error");
    return;
  }
  if (moves.length !== 1) {
    logLine(`Enter exactly one move at a time (got "${raw}").`, "error");
    return;
  }
  moveInput.value = "";
  submitMove(moves[0]);
}

submitBtn.addEventListener("click", handleTextSubmit);
moveInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter" || e.keyCode === 13) handleTextSubmit();
});
newScrambleBtn.addEventListener("click", newScramble);

for (const face of ["U", "D", "L", "R", "F", "B"]) {
  const group = document.createElement("div");
  group.className = "movegroup";
  for (const suffix of ["", "2", "'"]) {
    const name = face + suffix;
    const btn = document.createElement("button");
    btn.className = "button movebtn";
    btn.textContent = name;
    btn.addEventListener("click", () => submitMove(name));
    group.appendChild(btn);
  }
  buttonsEl.appendChild(group);
}

newScramble();
