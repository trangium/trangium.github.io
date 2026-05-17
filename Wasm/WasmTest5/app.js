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

// Expand one piece-mode cycle (the text between parentheses) into perm.
// Handles plain, oriented, and skew-oriented piece cycles.
function parsePieceCycle(content, perm, pieceInfo) {
    const tokens = content.trim().split(/\s+/).filter(Boolean);
    if (tokens.length === 0) return;

    const elements = tokens.map(tok => {
        const match = tok.match(/^([^+\-]+)([+-]\d+)?$/);
        if (!match) throw new Error(`Invalid cycle token: "${tok}"`);
        const name   = match[1];
        const offset = match[2] ? parseInt(match[2]) : 0;
        if (!pieceInfo.has(name))
            throw new Error(`Unknown piece "${name}" — every token must be a declared piece name.`);
        return { name, offset };
    });

    // Skew cycle: last token is the same piece as the first (carries kfinal only).
    let kfinal, pieces;
    if (elements.length > 1 && elements[elements.length - 1].name === elements[0].name) {
        kfinal = elements[elements.length - 1].offset;
        pieces = elements.slice(0, -1);
    } else {
        kfinal = elements[0].offset;
        pieces = elements;
    }

    const t = pieces.length;
    if (t === 0) return;

    const info0 = pieceInfo.get(pieces[0].name);
    const m     = info0.m;
    const type0 = info0.type;
    for (let i = 1; i < t; i++) {
        const info = pieceInfo.get(pieces[i].name);
        if (info.type !== type0)
            throw new Error(
                `Cannot mix piece types in a cycle: "${pieces[i].name}" is ${info.type}, expected ${type0}.`);
    }

    // Pi_s  →  P(i+1)_{ (s − ki + k(i+1)) mod m }
    // Last transition uses kfinal instead of k[0].
    for (let i = 0; i < t; i++) {
        const next  = (i + 1) % t;
        const ki    = pieces[i].offset;
        const knext = (i === t - 1) ? kfinal : pieces[next].offset;
        const bi    = pieceInfo.get(pieces[i].name).base;
        const bn    = pieceInfo.get(pieces[next].name).base;
        for (let s = 0; s < m; s++)
            perm[bi + s] = bn + ((s - ki + knext) % m + m) % m;
    }
}

function parsePieceCycles(str, k, pieceInfo) {
    const p = Array.from({ length: k }, (_, i) => i);
    for (const match of str.matchAll(/\(([^)]+)\)/g))
        parsePieceCycle(match[1], p, pieceInfo);
    return p;
}

function parseDefs(text) {
    const lines = text.split('\n')
        .map(l => l.replace(/#.*$/, '').trim())
        .filter(Boolean);

    // ── Parse piece-type header lines ─────────────────────────────────────────
    // Format:  TYPENAME N name1 name2 ...   (no colon; TYPENAME is ALL-CAPS)
    const pieceInfo = new Map();  // piece name → { base: int, m: int, type: string }
    for (const l of lines) {
        const hm = l.match(/^([A-Z][A-Z0-9_-]*)\s+(\d+)\s+(.+)$/);
        if (!hm) continue;
        const type = hm[1];
        const m    = parseInt(hm[2]);
        if (m < 1) throw new Error(`Piece type "${type}" must have at least 1 orientation.`);
        for (const name of hm[3].trim().split(/\s+/).filter(Boolean)) {
            if (pieceInfo.has(name)) throw new Error(`Piece "${name}" declared twice.`);
            pieceInfo.set(name, { base: -1, m, type });
        }
    }

    if (pieceInfo.size === 0)
        throw new Error('No piece-type headers found. Declare pieces with e.g. "EDGES 2 UF UL …"');

    // Assign consecutive sticker indices in declaration order.
    const tokenMap = new Map();
    let idx = 0;
    for (const [name, info] of pieceInfo) {
        info.base = idx;
        for (let s = 0; s < info.m; s++)
            tokenMap.set(`${name}_${s}`, idx + s);
        idx += info.m;
    }
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
            moves.set(name, parsePieceCycles(def, k, pieceInfo));
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

    return { k, moves, tokenMap, pieceInfo };
}

// ── Algorithm / generator parsers ─────────────────────────────────────────────

function parseGenerators(text) {
    return text
        .split(',')
        .map(g => [...g.matchAll(/\([^)]*\)|\S+/g)].map(m => m[0]))
        .filter(g => g.length > 0);
}

// Parse one OrientPerm block (array of non-empty trimmed lines) into a flat class list.
// Each line: [d:] item item ...  where item is {P1 P2 ...} or Pname.
// Unmentioned pieces are appended as singletons with full orientation_mod.
function parseOrientPermBlock(lines, pieceInfo) {
    const classes = [];
    const assigned = new Set();

    for (const line of lines) {
        const dMatch = line.match(/^(\d+):(.*)/);
        const orientModOverride = dMatch ? parseInt(dMatch[1]) : null;
        const rest = dMatch ? dMatch[2].trim() : line;

        const itemRe = /\{([^}]*)\}|(\S+)/g;
        let mt;
        while ((mt = itemRe.exec(rest)) !== null) {
            const names = mt[1] !== undefined
                ? mt[1].trim().split(/\s+/).filter(Boolean)
                : [mt[2]];
            if (names.length === 0) continue;

            for (const name of names) {
                if (!pieceInfo.has(name))
                    throw new Error(`Unknown piece "${name}" in OrientPerm group.`);
                if (assigned.has(name))
                    throw new Error(`Piece "${name}" appears in multiple classes in the same OrientPerm group.`);
            }

            const info0 = pieceInfo.get(names[0]);
            for (let i = 1; i < names.length; i++) {
                const info = pieceInfo.get(names[i]);
                if (info.type !== info0.type)
                    throw new Error(
                        `Pieces in a class must share the same type: "${names[i]}" is ${info.type}, expected ${info0.type}.`);
            }

            const orientMod = orientModOverride !== null ? orientModOverride : info0.m;
            for (const name of names) assigned.add(name);
            classes.push({
                bases: names.map(n => pieceInfo.get(n).base),
                m: info0.m,
                typeName: info0.type,
                orientation_mod: orientMod,
            });
        }
    }

    for (const [name, info] of pieceInfo) {
        if (!assigned.has(name))
            classes.push({ bases: [info.base], m: info.m, typeName: info.type, orientation_mod: info.m });
    }

    return classes;
}

// Split textarea into blocks (separated by blank lines).
// A block containing { or a /^\d+:/ line is one OrientPerm group.
// Otherwise each non-blank line in the block is a separate generator group.
function parseTargetGroups(text, moves, k, tokenMap, pieceInfo) {
    const groups = [];
    for (const block of text.split(/\n[ \t]*\n/)) {
        const lines = block.split('\n').map(s => s.trim()).filter(Boolean);
        if (lines.length === 0) continue;

        if (lines.some(l => l.includes('{') || /^\d+:/.test(l))) {
            groups.push({ kind: 'orientperm', classes: parseOrientPermBlock(lines, pieceInfo) });
        } else {
            for (const line of lines) {
                const algos = parseGenerators(line);
                if (algos.length === 0) continue;
                groups.push({ kind: 'generator', perms: algos.map(algo => composeAlgo(algo, moves, k, tokenMap)) });
            }
        }
    }
    if (groups.length === 0) throw new Error('No target groups entered.');
    return groups;
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
            setStatus(`${statusBase} Solutions found: ${count}.`, '#6b7280');
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

    let k, moves, tokenMap, pieceInfo;
    try {
        ({ k, moves, tokenMap, pieceInfo } = parseDefs($('puzzle').value));
    } catch (e) {
        setStatus('Puzzle error: ' + e.message, '#ee2727');
        return;
    }

    function parseAndCompose(fieldId, label) {
        const algos = parseGenerators($(fieldId).value);
        if (!algos.length) throw new Error(`No ${label} entered.`);
        return algos.map(algo => composeAlgo(algo, moves, k, tokenMap));
    }

    let targetGroups, solvingAlgos, solvingPerms, startingPerm;
    try {
        targetGroups = parseTargetGroups($('target-gens').value, moves, k, tokenMap, pieceInfo);
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
    worker.postMessage({ type: 'compute', k, targetGroups, startingPerm, solvingPerms, solvingAlgos, minMoves, maxMoves, slack });
}

// ── Event listeners ───────────────────────────────────────────────────────────

$('compute-btn').addEventListener('click', () => {
    if ($('compute-btn').textContent === 'Stop') stopComputation();
    else compute();
});
$('starting-algo').addEventListener('keydown', e => { if (e.key === 'Enter' && $('compute-btn').textContent !== 'Stop') compute(); });

// ── Defaults ──────────────────────────────────────────────────────────────────

$('puzzle').value =
`EDGES 2 UF UL UB UR DF DL DB DR FR FL BL BR
CORNERS 3 UFR UBR UBL UFL DFR DFL DBL DBR

U: (UF UL UB UR) (UFR UFL UBL UBR)
R: (UR BR DR FR) (UFR UBR+1 DBR DFR+1)
F: (UF FR+1 DF FL+1) (UFR DFR-1 DFL UFL-1)
D: (DF DR DB DL) (DFR DBR DBL DFL)
L: (UL FL DL BL) (UFL DFL-1 DBL UBL-1)
B: (UB BL+1 DB BR+1) (UBR UBL+1 DBL DBR+1)
U2: U U    
R2: R R    
F2: F F    
D2: D D    
L2: L L    
B2: B B    
`;

$('target-gens').value   = `R, U, F, B L B'

R, U, B, F' L F

R, U, D, L

R, U, D2 L D2, L F L', L' B' L`;
$('solving-gens').value  = `U, U2, R, R2, F, F2, D, D2, L, L2, B, B2`;
$('starting-algo').value = `R2 F R2 D' U F2 R F' B' D U R2 D R L' D U' L D' U' L2`;
