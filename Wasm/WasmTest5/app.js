'use strict';

const worker = new Worker('worker.js');
const $ = id => document.getElementById(id);

// ── Cycle-notation / defs parser ──────────────────────────────────────────────

function parseCycles(str, k, tokenMap) {
    const p = Array.from({ length: k }, (_, i) => i);
    for (const m of str.matchAll(/\(([^)]+)\)/g)) {
        const els = m[1].trim().split(/\s+/).map(tok => tokenMap.get(tok));
        for (let i = 0; i < els.length; i++)
            p[els[i]] = els[(i + 1) % els.length];
    }
    return p;
}

function parseDefs(text) {
    const lines = text.split('\n')
        .map(l => l.replace(/#.*$/, '').trim())
        .filter(Boolean);

    const tokenMap = new Map();
    const getOrAdd = tok => {
        if (!tokenMap.has(tok)) tokenMap.set(tok, tokenMap.size);
        return tokenMap.get(tok);
    };
    for (const l of lines) {
        const ci = l.indexOf(':');
        if (ci < 0 || !l.slice(ci + 1).includes('(')) continue;
        for (const m of l.matchAll(/\(([^)]+)\)/g))
            for (const tok of m[1].trim().split(/\s+/)) getOrAdd(tok);
    }
    if (tokenMap.size === 0) throw new Error('No cycle definitions found in puzzle.');
    const k = tokenMap.size;

    const identity  = () => Array.from({ length: k }, (_, i) => i);
    const compose   = (a, b) => a.map(x => b[x]);
    const invPerm   = p => { const r = new Array(k); for (let i = 0; i < k; i++) r[p[i]] = i; return r; };
    const hasSeq    = s => s.endsWith("'") ? moves.has(s.slice(0, -1)) : moves.has(s);
    const lookupSeq = s => s.endsWith("'") ? invPerm(moves.get(s.slice(0, -1))) : moves.get(s);

    const moves   = new Map();
    const derived = [];
    for (const line of lines) {
        const ci = line.indexOf(':');
        if (ci < 0) continue;
        const name = line.slice(0, ci).trim();
        const def  = line.slice(ci + 1).trim();
        if (def.includes('(')) {
            moves.set(name, parseCycles(def, k, tokenMap));
        } else {
            derived.push({ name, seq: def.split(/\s+/).filter(Boolean) });
        }
    }

    // Fixed-point resolution for derived moves
    let prev = -1;
    while (moves.size !== prev) {
        prev = moves.size;
        for (const { name, seq } of derived) {
            if (moves.has(name)) continue;
            if (seq.every(hasSeq)) {
                let perm = identity();
                for (const s of seq) perm = compose(perm, lookupSeq(s));
                moves.set(name, perm);
            }
        }
    }

    const unresolved = derived.filter(d => !moves.has(d.name));
    if (unresolved.length)
        throw new Error(`Unresolved moves: ${unresolved.map(d => d.name).join(', ')}`);

    for (const name of moves.keys())
        if (name.endsWith("'"))
            throw new Error(`Move "${name}" ends with ' which is disallowed.`);

    for (const [name, perm] of [...moves])
        moves.set(name + "'", invPerm(perm));

    return { k, moves };
}

// ── Algorithm / generator parsers ─────────────────────────────────────────────

// Parse a comma-or-newline-separated list of move-sequence generators
function parseGenerators(text) {
    return text
        .split(/[,\n]/)
        .map(g => g.trim().split(/\s+/).filter(Boolean))
        .filter(g => g.length > 0);
}

// Parse multiple generator groups separated by lines containing only "---".
// Returns an array of arrays of permutations.
function parseTargetGroups(text, moves, k) {
    const groups = text.split(/\n(?:[ \t]*\n)+/).map(s => s.trim()).filter(Boolean);
    if (!groups.length) throw new Error('No target generators entered.');
    return groups.map((groupText, idx) => {
        const algos = parseGenerators(groupText);
        if (!algos.length) throw new Error(`Target group ${idx + 1} is empty.`);
        return algos.map(algo => composeAlgo(algo, moves, k));
    });
}

function composeAlgo(algo, moves, k) {
    let perm = Array.from({ length: k }, (_, i) => i);
    for (const name of algo) {
        const m = moves.get(name);
        if (!m) throw new Error(`Unknown move: "${name}"`);
        perm = perm.map(x => m[x]);
    }
    return perm;
}

// ── UI helpers ────────────────────────────────────────────────────────────────

function setStatus(text, color) {
    const el = $('status');
    el.textContent = text;
    el.style.color = color || '#6b7280';
}

function setResult(html) {
    $('result').innerHTML = html;
}

// ── Worker messages ───────────────────────────────────────────────────────────

worker.addEventListener('message', ({ data }) => {
    if (data.type === 'preview') {
        const { groupPreviews } = data;
        const parts = groupPreviews.map((g, i) =>
            `\nT${i + 1}: ≤ ${g.solvingOrder} / ${g.targetOrder} = ${g.predictedSize} states`
        );
        setStatus('Building tables… ' + parts.join(' · '), '#fbbf24');
    } else if (data.type === 'result') {
        const { solution, tableSizes } = data;
        const tableInfo = tableSizes.map((s, i) => `T${i + 1}: ${s}`).join(', ');
        const statusBase = `Tables built (${tableInfo}).`;
        if (!solution) {
            setStatus(`${statusBase} Starting algorithm not reachable.`, '#fb923c');
            setResult('<span style="color:#6b7280">Not found.</span>');
        } else if (solution.length === 0) {
            setStatus(statusBase, '#4ade80');
            setResult(`<span class="id-display">Already solved.</span>`);
        } else {
            setStatus(statusBase, '#4ade80');
            setResult(`<span class="id-display">${solution.join(' ')}</span>`);
        }
    } else if (data.type === 'error') {
        setStatus('Error: ' + data.message, '#f87171');
        setResult('');
    }
});

// ── Main compute ──────────────────────────────────────────────────────────────

function compute() {
    setStatus('Computing group orders…', '#fbbf24');
    setResult('');

    let k, moves;
    try {
        ({ k, moves } = parseDefs($('puzzle').value));
    } catch (e) {
        setStatus('Puzzle error: ' + e.message, '#f87171');
        return;
    }

    function parseAndCompose(fieldId, label) {
        const algos = parseGenerators($(fieldId).value);
        if (!algos.length) throw new Error(`No ${label} entered.`);
        return algos.map(algo => composeAlgo(algo, moves, k));
    }

    let targetPermsArray, solvingAlgos, solvingPerms, startingPerm;
    try {
        targetPermsArray = parseTargetGroups($('target-gens').value, moves, k);
        solvingAlgos = parseGenerators($('solving-gens').value);
        if (!solvingAlgos.length) throw new Error('No solving generators entered.');
        solvingPerms = solvingAlgos.map(algo => composeAlgo(algo, moves, k));
        const startAlgo = $('starting-algo').value.trim().split(/\s+/).filter(Boolean);
        if (!startAlgo.length) throw new Error('No starting algorithm entered.');
        startingPerm = composeAlgo(startAlgo, moves, k);
    } catch (e) {
        setStatus('Input error: ' + e.message, '#f87171');
        return;
    }

    worker.postMessage({ type: 'compute', k, targetPermsArray, startingPerm, solvingPerms, solvingAlgos });
}

// ── Event listeners ───────────────────────────────────────────────────────────

$('compute-btn').addEventListener('click', compute);
$('starting-algo').addEventListener('keydown', e => { if (e.key === 'Enter') compute(); });

// ── Defaults ──────────────────────────────────────────────────────────────────

$('puzzle').value =
`U: (Ufr Ufl Ubl Ubr) (Uf Ul Ub Ur) (uF uL uB uR) (uFr ufL uBl ubR) (ufR uFl ubL uBr)
y: (B R F L) (Ub Ur Uf Ul) (Db Dr Df Dl) (uB uR uF uL) (dB dR dF dL) (Ubl Ubr Ufr Ufl) (Dbl Dbr Dfr Dfl) (uBl ubR uFr ufL) (uBr ufR uFl ubL) (Bl bR Fr fL) (Br fR Fl bL) (dBl dbR dFr dfL) (dBr dfR dFl dbL)
x: (B D F U) (uB Db dF Uf) (Ub dB Df uF) (Bl Dl Fl Ul) (Br Dr Fr Ur) (bL dL fL uL) (bR dR fR uR) (uBr Dbr dFr Ufr) (Ubr dBr Dfr uFr) (uBl Dbl dFl Ufl) (Ubl dBl Dfl uFl) (ubR dbR dfR ufR) (ubL dbL dfL ufL)
z: x y x'
y2: y y
x2: x x
z2: z z
R: z' U z
F: x U x'
L: z U z'
D: z2 U z2
B: x' U x
U2: U U
R2: R R
F2: F F
L2: L L
D2: D D
B2: B B
`;

$('target-gens').value   = `D, R2 D2 R D2 R2, F2 D2 F D2 F2, L2 D2 L D2 L2, B2 D2 B D2 B2, L R' B L' R, L' R F L R', F B' L F' B, F' B R F B', R D R' D' R' B R B', B R D R' D' B'`;
$('solving-gens').value  = `R, U, F, D, L, B`;
$('starting-algo').value = `R' F R U R U' R' F' R U' R'`;
