// Runs the WASM solver in Node:  node web_node_test.js [normal|safe] [n]
// (from SuperCubeSolver444\cpp; needs ..\solver.js, solver.wasm, solver.data built by build_wasm.ps1)
const path = require('path');
const createSolver = require('../solver.js');

const SCRAMBLE = "Fw2 Uw' R2 Rw' D' B2 Lw2 F U2 Rw Dw' B' L2 Fw U Rw2 D' R2 Bw F' Uw2 R B' L Dw2 F2 U' Lw R' B Rw2 U2 L' D2 Fw' Uw R' F Dw' L2 B2 R2 U Bw' D F2 Lw' Uw2 Rw B2 D' L";

let M = null;
const heapMB = () => (M.ccall('sc_heap_bytes', 'number', [], []) / 1048576).toFixed(0) + ' MB wasm heap, ' + (process.memoryUsage().rss / 1048576).toFixed(0);
(async () => {
    const t0 = Date.now();
    const log = (...a) => console.log(`[${((Date.now() - t0) / 1000).toFixed(2)}s]`, ...a);
    M = await createSolver({
        locateFile: (f) => path.join(__dirname, '..', f),
        onProgress: (step, total, what) => log(`  progress ${step}/${total}: ${what}`),
        print: (t) => log('  c++:', t),
        printErr: (t) => log('  c++ err:', t),
    });
    log('module loaded, process rss', heapMB(), 'MB');
    if (process.env.VERBOSE) M.ccall('sc_set_verbose', null, ['number'], [1]);
    const init = (safe) => M.ccall('sc_init', 'number', ['number'], [safe]);
    const solve = (scr, pre, suf, safe, rand, hi, lo) => JSON.parse(M.ccall('sc_solve', 'string',
        ['string', 'string', 'string', 'number', 'number', 'number', 'number'], [scr, pre, suf, safe, rand, hi, lo]));
    const mode = process.argv[2] || 'safe';
    const n = parseInt(process.argv[3] || '3');
    const safe = mode === 'safe' ? 1 : 0;

    let t = Date.now();
    init(safe);
    log(`tables (${mode}) built in ${((Date.now() - t) / 1000).toFixed(2)} s, process rss ${heapMB()} MB`);

    t = Date.now();
    let r = solve(SCRAMBLE, '', '', safe, 0, 0, 1);
    log('given scramble:', JSON.stringify(r), `(${Date.now() - t} ms wall)`);
    r = solve(SCRAMBLE, 'R U', "F2 Rw'", safe, 0, 0, 1);
    log('given scramble with forced ends:', JSON.stringify(r));
    for (let i = 0; i < n; i++) {
        t = Date.now();
        r = solve('', '', '', safe, 1, 123, 1000 + i);
        log(`random state ${i}:`, JSON.stringify(r), `(${Date.now() - t} ms wall)`);
    }
    r = solve('R U Q', '', '', safe, 0, 0, 1);
    log('bad token:', JSON.stringify(r));
    // switching the mode only rebuilds the first-phase table
    t = Date.now();
    init(safe ? 0 : 1);
    log(`switched mode, rebuilt in ${((Date.now() - t) / 1000).toFixed(2)} s, process rss ${heapMB()} MB`);
    r = solve('', '', '', safe ? 0 : 1, 1, 5, 6);
    log('random state in the other mode:', JSON.stringify(r));
    log('done, process rss', heapMB(), 'MB');
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
