'use strict';

let worker = new Worker('worker.js');
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

    return { k, moves, tokenMap };
}

// ── Algorithm / generator parsers ─────────────────────────────────────────────

function parseGenerators(text) {
    return text
        .split(',')
        .map(g => [...g.matchAll(/\([^)]*\)|\S+/g)].map(m => m[0]))
        .filter(g => g.length > 0);
}

// Each non-blank line is one target group; generators within a group are comma-separated.
function parseTargetGroups(text, moves, k, tokenMap) {
    const groups = text.split('\n').map(s => s.trim()).filter(Boolean);
    if (!groups.length) throw new Error('No target generators entered.');
    return groups.map((groupText, idx) => {
        const algos = parseGenerators(groupText);
        if (!algos.length) throw new Error(`Target group ${idx + 1} is empty.`);
        return algos.map(algo => composeAlgo(algo, moves, k, tokenMap));
    });
}

function composeAlgo(algo, moves, k, tokenMap) {
    let perm = Array.from({ length: k }, (_, i) => i);
    for (const token of algo) {
        if (token.startsWith('(')) {
            for (const m of token.matchAll(/\(([^)]+)\)/g))
                for (const tok of m[1].trim().split(/\s+/))
                    if (!tokenMap.has(tok))
                        throw new Error(`Unknown piece/sticker "${tok}" in cycle notation.`);
            const cyclePerm = parseCycles(token, k, tokenMap);
            perm = perm.map(x => cyclePerm[x]);
        } else {
            const m = moves.get(token);
            if (!m) throw new Error(`Unknown move: "${token}"`);
            perm = perm.map(x => m[x]);
        }
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

function setComputing(on) {
    $('compute-btn').textContent = on ? 'Stop' : 'Compute';
}

// ── Worker messages ───────────────────────────────────────────────────────────

let currentTableInfo = '';

function onWorkerMessage({ data }) {
    if (data.type === 'cache_hit') {
        const { tableSizes } = data;
        currentTableInfo = tableSizes.map((s, i) => `T${i + 1}: ${s}`).join(', ');
        setStatus(`Tables built (${currentTableInfo}). Solving...`, '#6b7280');
    } else if (data.type === 'depth') {
        setStatus(`Tables built (${currentTableInfo}). Searching depth ${data.depth}.`, '#6b7280');
    } else if (data.type === 'preview') {
        const { groupPreviews } = data;
        const parts = groupPreviews.map((g, i) =>
            `\nT${i + 1}: ≤ ${g.predictedSize} states (${g.solvingOrder} / ${g.targetOrder})`
        );
        setStatus('Building tables… ' + parts.join(''), '#6b7280');
    } else if (data.type === 'tables_built') {
        const { tableSizes } = data;
        currentTableInfo = tableSizes.map((s, i) => `T${i + 1}: ${s}`).join(', ');
        setStatus(`Tables built (${currentTableInfo}). Solving...`, '#6b7280');
    } else if (data.type === 'solution') {
        const { solution } = data;
        const resultEl = $('result');
        if (solution.length === 0) {
            resultEl.innerHTML = '<span class="id-display">Already solved.</span>';
        } else {
            if (resultEl.hasChildNodes()) resultEl.appendChild(document.createElement('br'));
            const span = document.createElement('span');
            span.className = 'id-display';
            span.textContent = solution.join(' ') + ` (${solution.length})`;
            resultEl.appendChild(span);
        }
    } else if (data.type === 'done') {
        const { tableSizes, unreachable } = data;
        const tableInfo = tableSizes.map((s, i) => `T${i + 1}: ${s}`).join(', ');
        const statusBase = `Tables saved (${tableInfo}).`;
        setComputing(false);
        if (unreachable) {
            setStatus(`${statusBase} Starting algorithm not reachable.`, '#6b7280');
            if (!$('result').hasChildNodes())
                setResult('<span style="color:#6b7280">Not found.</span>');
        } else {
            const count = $('result').querySelectorAll('.id-display').length;
            setStatus(`${statusBase} Solutions found: ${count}.`, '#228822');
        }
    } else if (data.type === 'error') {
        setComputing(false);
        setStatus('Error: ' + data.message, '#ee2727');
        setResult('');
    }
}
worker.addEventListener('message', onWorkerMessage);

// ── Main compute ──────────────────────────────────────────────────────────────

function stopComputation() {
    worker.terminate();
    worker = new Worker('worker.js');
    worker.addEventListener('message', onWorkerMessage);
    setComputing(false);
    setStatus('Stopped.', '#6b7280');
}

function compute() {
    setStatus('Computing group orders…', '#6b7280');
    setResult('');

    let k, moves, tokenMap;
    try {
        ({ k, moves, tokenMap } = parseDefs($('puzzle').value));
    } catch (e) {
        setStatus('Puzzle error: ' + e.message, '#ee2727');
        return;
    }

    function parseAndCompose(fieldId, label) {
        const algos = parseGenerators($(fieldId).value);
        if (!algos.length) throw new Error(`No ${label} entered.`);
        return algos.map(algo => composeAlgo(algo, moves, k, tokenMap));
    }

    let targetPermsArray, solvingAlgos, solvingPerms, startingPerm;
    try {
        targetPermsArray = parseTargetGroups($('target-gens').value, moves, k, tokenMap);
        solvingAlgos = parseGenerators($('solving-gens').value);
        if (!solvingAlgos.length) throw new Error('No solving generators entered.');
        solvingPerms = solvingAlgos.map(algo => composeAlgo(algo, moves, k, tokenMap));
        const startAlgo = [...$('starting-algo').value.matchAll(/\([^)]*\)|\S+/g)].map(m => m[0]);
        if (!startAlgo.length) throw new Error('No starting algorithm entered.');
        startingPerm = composeAlgo(startAlgo, moves, k, tokenMap);
    } catch (e) {
        setStatus('Input error: ' + e.message, '#ee2727');
        return;
    }

    const minMovesVal = $('min-moves').value.trim();
    const maxMovesVal = $('max-moves').value.trim();
    const slackVal    = $('slack').value.trim();
    const minMoves = minMovesVal === '' ? 0 : Math.max(0, parseInt(minMovesVal) || 0);
    const maxMoves = maxMovesVal === '' ? 2147483647 : Math.max(0, parseInt(maxMovesVal) || 0);
    const slack    = slackVal    === '' ? 0 : Math.max(0, parseInt(slackVal)    || 0);

    setComputing(true);
    worker.postMessage({ type: 'compute', k, targetPermsArray, startingPerm, solvingPerms, solvingAlgos, minMoves, maxMoves, slack });
}

// ── Event listeners ───────────────────────────────────────────────────────────

$('compute-btn').addEventListener('click', () => {
    if ($('compute-btn').textContent === 'Stop') stopComputation();
    else compute();
});
$('starting-algo').addEventListener('keydown', e => { if (e.key === 'Enter' && $('compute-btn').textContent !== 'Stop') compute(); });

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

$('target-gens').value   = `R, U, F, B L B'
R, U, B, F' L F
R, U, D, L
R, U, D2 L D2, L F L', L' B' L`;
$('solving-gens').value  = `U, U2, R, R2, F, F2, D, D2, L, L2, B, B2`;
$('starting-algo').value = `R2 F R2 D' U F2 R F' B' D U R2 D R L' D U' L D' U' L2`;
