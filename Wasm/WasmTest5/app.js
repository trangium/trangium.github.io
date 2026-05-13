'use strict';

const worker = new Worker('worker.js');
const $ = id => document.getElementById(id);

// ── Cycle-notation / defs parser ──────────────────────────────────────────────

function parseCycles(str, k) {
    const p = Array.from({ length: k }, (_, i) => i);
    for (const m of str.matchAll(/\(([^)]+)\)/g)) {
        const els = m[1].trim().split(/\s+/).map(Number);
        for (let i = 0; i < els.length; i++)
            p[els[i]] = els[(i + 1) % els.length];
    }
    return p;
}

function parseDefs(text) {
    const lines = text.split('\n')
        .map(l => l.replace(/#.*$/, '').trim())
        .filter(Boolean);

    let maxEl = -1;
    for (const l of lines) {
        const ci = l.indexOf(':');
        if (ci < 0 || !l.slice(ci + 1).includes('(')) continue;
        for (const m of l.matchAll(/\d+/g)) {
            const v = +m[0];
            if (v > maxEl) maxEl = v;
        }
    }
    if (maxEl < 0) throw new Error('No cycle definitions found in puzzle.');
    const k = maxEl + 1;

    const identity = () => Array.from({ length: k }, (_, i) => i);
    const compose  = (a, b) => a.map(x => b[x]);

    const moves   = new Map();
    const derived = [];
    for (const line of lines) {
        const ci = line.indexOf(':');
        if (ci < 0) continue;
        const name = line.slice(0, ci).trim();
        const def  = line.slice(ci + 1).trim();
        if (def.includes('(')) {
            moves.set(name, parseCycles(def, k));
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
            if (seq.every(s => moves.has(s))) {
                let perm = identity();
                for (const s of seq) perm = compose(perm, moves.get(s));
                moves.set(name, perm);
            }
        }
    }

    const unresolved = derived.filter(d => !moves.has(d.name));
    if (unresolved.length)
        throw new Error(`Unresolved moves: ${unresolved.map(d => d.name).join(', ')}`);

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
    if (data.type === 'result') {
        const { id, tableSize } = data;
        if (id === -1) {
            setStatus(`Table built — ${tableSize} canonical ID(s). Starting algorithm is not in the table.`, '#fb923c');
            setResult('<span style="color:#6b7280">Not found.</span>');
        } else {
            setStatus(`Table built — ${tableSize} canonical ID(s).`, '#4ade80');
            setResult(`<span class="id-display">${id}</span>`);
        }
    } else if (data.type === 'error') {
        setStatus('Error: ' + data.message, '#f87171');
        setResult('');
    }
});

// ── Main compute ──────────────────────────────────────────────────────────────

function compute() {
    setStatus('Computing…', '#fbbf24');
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

    let targetPerms, solvingPerms, startingPerm;
    try {
        targetPerms  = parseAndCompose('target-gens', 'target generators');
        solvingPerms = parseAndCompose('solving-gens', 'solving generators');
        const startAlgo = $('starting-algo').value.trim().split(/\s+/).filter(Boolean);
        if (!startAlgo.length) throw new Error('No starting algorithm entered.');
        startingPerm = composeAlgo(startAlgo, moves, k);
    } catch (e) {
        setStatus('Input error: ' + e.message, '#f87171');
        return;
    }

    worker.postMessage({ type: 'compute', k, targetPerms, startingPerm, solvingPerms });
}

// ── Event listeners ───────────────────────────────────────────────────────────

$('compute-btn').addEventListener('click', compute);
$('starting-algo').addEventListener('keydown', e => { if (e.key === 'Enter') compute(); });

// ── Defaults ──────────────────────────────────────────────────────────────────

$('puzzle').value =
`U: (0 1 2 3) (4 16 12 8) (5 17 13 9)
R: (12 13 14 15) (1 19 21 9) (2 16 22 10)
F: (8 9 10 11) (3 12 21 6) (2 15 20 5)
U2: U U
U': U U U
R2: R R
R': R R R
F2: F F
F': F F F`;

$('target-gens').value   = `F R U R' U' F', R U R' U R U2 R'`;
$('solving-gens').value  = `R, U, F`;
$('starting-algo').value = `R' F R U R U' R' F' R U' R'`;
