'use strict';
(function () {

// ── Cycle-notation parser (JS — string work belongs here, not in C++) ─────────

// "(a b c)(d e)" → flat identity-based permutation array of length k
function parseCycles(str, k) {
    const p = Array.from({ length: k }, (_, i) => i);
    for (const m of str.matchAll(/\(([^)]+)\)/g)) {
        const els = m[1].trim().split(/\s+/).map(Number);
        for (let i = 0; i < els.length; i++)
            p[els[i]] = els[(i + 1) % els.length];
    }
    return p;
}

// Parse the definitions textarea into a plain data object ready to ship to C++.
// Returns { k, atomicMoves: [{name, perm}], derivedMoves: [{name, seq}] }
// or throws a descriptive error.
function parseDefs(text) {
    const lines = text.split('\n')
        .map(l => l.replace(/#.*$/, '').trim())
        .filter(Boolean);

    // Infer k from the largest element index that appears inside any cycle.
    let maxEl = -1;
    for (const l of lines) {
        const ci = l.indexOf(':');
        if (ci < 0 || !l.slice(ci + 1).includes('(')) continue;
        for (const m of l.matchAll(/\d+/g)) {
            const v = +m[0];
            if (v > maxEl) maxEl = v;
        }
    }
    if (maxEl < 0) throw new Error('No cycle definitions found.');
    const k = maxEl + 1;

    const atomicMoves = [];
    const derivedMoves = [];
    for (const line of lines) {
        const ci = line.indexOf(':');
        if (ci < 0) continue;
        const name = line.slice(0, ci).trim();
        const def  = line.slice(ci + 1).trim();
        if (def.includes('(')) {
            atomicMoves.push({ name, perm: parseCycles(def, k) });
        } else {
            derivedMoves.push({ name, seq: def.split(/\s+/).filter(Boolean) });
        }
    }
    return { k, atomicMoves, derivedMoves };
}

// ── State ─────────────────────────────────────────────────────────────────────

let defsChanged = true;   // true → must re-send cube-setup before next cube-eval
let pendingEval  = null;  // { base, sequence } waiting for cube-ready

// ── UI helpers ────────────────────────────────────────────────────────────────

const $ = id => document.getElementById(id);

function setStatus(text, color) {
    const el = $('cube-parse-status');
    el.textContent = text;
    el.style.color = color;
}

function parseBase() {
    return $('cube-base').value.trim().split(/[\s,]+/).map(Number).filter(n => !isNaN(n));
}

function parseAlgo() {
    return $('cube-algo').value.trim().split(/\s+/).filter(Boolean);
}

// Attempt JS parse; on success sends cube-setup and returns true.
function sendSetup() {
    let parsed;
    try {
        parsed = parseDefs($('cube-defs').value);
        setStatus(`Parsed — k=${parsed.k} · sending to C++…`, '#fbbf24');
    } catch (e) {
        setStatus('✗  ' + e.message, '#f87171');
        return false;
    }
    worker.postMessage({ type: 'cube-setup', ...parsed });
    defsChanged = false;
    return true;
}

// ── Worker message handling ───────────────────────────────────────────────────

worker.addEventListener('message', ({ data }) => {
    if (data.type === 'cube-ready') {
        setStatus('✓  ' + data.moveNames.join('  '), '#4ade80');
        if (pendingEval) {
            worker.postMessage({ type: 'cube-eval', ...pendingEval });
            pendingEval = null;
        }

    } else if (data.type === 'cube-result') {
        const { base, positions } = data;
        const rows = base.map((b, i) => {
            const cls = positions[i] === b ? 'fixed' : 'moved';
            return `<tr>
                <td class="bp">${b}</td>
                <td class="arr">→</td>
                <td class="${cls}">${positions[i]}</td>
            </tr>`;
        }).join('');
        $('cube-result').innerHTML = `<table class="result-table">${rows}</table>`;

    } else if (data.type === 'cube-error') {
        defsChanged = true;
        pendingEval = null;
        setStatus('✗  ' + data.message, '#f87171');
        $('cube-result').innerHTML = '';
    }
});

// ── Event listeners ───────────────────────────────────────────────────────────

$('cube-defs').addEventListener('input', () => {
    defsChanged = true;
    setStatus('(modified)', '#6b7280');
});

$('cube-parse-btn').addEventListener('click', () => {
    pendingEval = null;
    sendSetup();
});

$('cube-eval-btn').addEventListener('click', doEval);
$('cube-algo').addEventListener('keydown', e => { if (e.key === 'Enter') doEval(); });

function doEval() {
    const base     = parseBase();
    const sequence = parseAlgo();
    if (!base.length)     { $('cube-result').innerHTML = '<span style="color:#6b7280">Enter base points.</span>';  return; }
    if (!sequence.length) { $('cube-result').innerHTML = '<span style="color:#6b7280">Enter an algorithm.</span>'; return; }

    if (defsChanged) {
        pendingEval = { base, sequence };
        if (!sendSetup()) { pendingEval = null; }
        // eval fires automatically from the 'cube-ready' handler
    } else {
        worker.postMessage({ type: 'cube-eval', base, sequence });
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

$('cube-defs').value =
`U: (0 1 2 3) (4 16 12 8) (5 17 13 9)
R: (12 13 14 15) (1 19 21 9) (2 16 22 10)
F: (8 9 10 11) (3 12 21 6) (2 15 20 5)
U2: U U
U': U U U
R2: R R
R': R R R
F2: F F
F': F F F`;

$('cube-base').value = '0 1 2 3 20 21 22';

})();
