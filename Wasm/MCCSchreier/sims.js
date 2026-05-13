'use strict';
(function () {

const $ = id => document.getElementById(id);

// ── State ─────────────────────────────────────────────────────────────────────

let simsBuilt     = false;
let pendingTest   = null;   // algo array waiting for sims-ready

// ── Worker messages ───────────────────────────────────────────────────────────

worker.addEventListener('message', ({ data }) => {
    if (data.type === 'sims-ready') {
        simsBuilt = true;
        const order = data.orbitSizes.reduce((acc, s) => acc * BigInt(s), 1n);
        $('sims-status').textContent =
            `✓  Base: [${data.base.join(', ')}]  ·  Orbit sizes: [${data.orbitSizes.join(' × ')}]  ·  Order: ${order}`;
        $('sims-status').style.color = '#4ade80';

        if (pendingTest) {
            worker.postMessage({ type: 'sims-test', algo: pendingTest });
            pendingTest = null;
        }

    } else if (data.type === 'sims-result') {
        const el = $('sims-result');
        el.textContent   = data.inGroup ? '✓  In subgroup' : '✗  Not in subgroup';
        el.style.color   = data.inGroup ? '#4ade80' : '#f87171';

    } else if (data.type === 'sims-error') {
        simsBuilt = false;
        pendingTest = null;
        $('sims-status').textContent = '✗  ' + data.message;
        $('sims-status').style.color = '#f87171';
        $('sims-result').textContent = '';
    }
});

// ── Helpers ───────────────────────────────────────────────────────────────────

function parseGeneratorAlgos() {
    return $('sims-generators').value
        .split('\n')
        .map(l => l.trim().split(/\s+/).filter(Boolean))
        .filter(a => a.length > 0);
}

function sendBuild() {
    const generatorAlgos = parseGeneratorAlgos();
    $('sims-status').textContent = 'Building…';
    $('sims-status').style.color = '#fbbf24';
    $('sims-result').textContent = '';
    simsBuilt = false;
    worker.postMessage({ type: 'sims-build', generatorAlgos });
}

function doTest() {
    const algo = $('sims-test-algo').value.trim().split(/\s+/).filter(Boolean);
    if (!algo.length) return;

    if (!simsBuilt) {
        pendingTest = algo;
        sendBuild();
    } else {
        worker.postMessage({ type: 'sims-test', algo });
    }
}

// ── Event listeners ───────────────────────────────────────────────────────────

$('sims-generators').addEventListener('input', () => { simsBuilt = false; });
$('sims-build-btn').addEventListener('click', () => { pendingTest = null; sendBuild(); });
$('sims-test-btn').addEventListener('click', doTest);
$('sims-test-algo').addEventListener('keydown', e => { if (e.key === 'Enter') doTest(); });

})();
