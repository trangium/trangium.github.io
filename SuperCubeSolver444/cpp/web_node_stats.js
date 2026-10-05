// Statistics of N random-state solves of the WASM build in Node:  node web_node_stats.js [normal|safe] [N]
const path = require('path');
const createSolver = require('../solver.js');

(async () => {
    const mode = process.argv[2] || 'normal';
    const n = parseInt(process.argv[3] || '50');
    const safe = mode === 'safe' ? 1 : 0;
    const M = await createSolver({ locateFile: (f) => path.join(__dirname, '..', f), print: () => {}, printErr: () => {} });
    let t = Date.now();
    M.ccall('sc_init', 'number', ['number'], [safe]);
    const initMs = Date.now() - t;
    const rows = [];
    for (let i = 0; i < n; i++) {
        const r = JSON.parse(M.ccall('sc_solve', 'string', ['string', 'string', 'string', 'number', 'number', 'number', 'number'], ['', '', '', safe, 1, 99, 5000 + i]));
        if (!r.ok) { console.log('FAILED', i, r.error); continue; }
        rows.push(r);
    }
    const pick = (a, q) => a[Math.min(a.length - 1, Math.floor(a.length * q))];
    const nodes = rows.map((r) => r.nodes).sort((a, b) => a - b), ms = rows.map((r) => r.ms).sort((a, b) => a - b);
    const avg = (a) => a.reduce((x, y) => x + y, 0) / a.length;
    console.log(`${mode}: tables ${(initMs / 1000).toFixed(2)} s; ${rows.length}/${n} ok; avg length ${avg(rows.map((r) => r.moves)).toFixed(2)};`
        + ` nodes median ${pick(nodes, 0.5)} p90 ${pick(nodes, 0.9)} max ${nodes[nodes.length - 1]};`
        + ` ms median ${pick(ms, 0.5)} p90 ${pick(ms, 0.9)} max ${ms[ms.length - 1]}; wasm heap ${(M.ccall('sc_heap_bytes', 'number', [], []) / 1048576).toFixed(0)} MB`);
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
