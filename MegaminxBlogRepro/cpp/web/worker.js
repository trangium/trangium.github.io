// Web worker of the Megaminx solver prototype page: loads the WASM solver (the networks are preloaded into its virtual file system) and runs the solves.
// Messages from the page:  {type:'prepare'}                                                       load the networks
//                          {type:'run', mode:'random', count, beams:[A, B, C]}                      `count` random states, solved one after the other
//                          {type:'run', mode:'given', scramble, beams:[A, B, C]}                    one given position (a scramble of the 12 faces, x2 allowed)
// Messages to the page:    {type:'solving', index, total}   then   {type:'result', index, total, ok, ...}   for every solve (as soon as it is done), finally {type:'idle'};   {type:'error', message}
// A result has: ok, error | a b c (moves per phase, x2 not counted), t1 t2 t3 (seconds), mlp2 mlp3 (full-network states evaluated; phase 1 uses no network), word (solution with x2), inverse (the same word inverted: a scramble of the random state).
// bump VERSION whenever solver.js / solver.wasm / solver.data / randomstate.js are rebuilt: it keeps browsers from mixing a cached old file with a new one
const VERSION = '21';
importScripts('solver.js?v=' + VERSION, 'randomstate.js?v=' + VERSION);

let M = null;
let queue = Promise.resolve();

const ready = createSolver({
    locateFile: (file) => file + '?v=' + VERSION,
    print: () => {},
    printErr: () => {},
    onAbort: (what) => postMessage({ type: 'error', message: 'The solver stopped (' + what + '). It probably ran out of memory; reload the page or use smaller beam widths.' })
}).then((m) => { M = m; });

onmessage = (e) => {
    const d = e.data;
    queue = queue.then(async () => {
        try {
            await ready;
            if (d.type === 'prepare') {
                M.ccall('mg_init', 'number', ['string'], ['/model.bin']);
                postMessage({ type: 'idle' });
            } else if (d.type === 'run') {
                const total = d.mode === 'random' ? d.count : 1;
                for (let index = 1; index <= total; index++) {
                    postMessage({ type: 'solving', index: index, total: total });
                    await new Promise((resolve) => setTimeout(resolve, 20));    // let the page show "Solving"
                    const input = d.mode === 'random' ? randomState().join(',') : d.scramble;
                    const json = M.ccall('mg_solve', 'string', ['string', 'number', 'number', 'number', 'number'], [input, d.mode === 'random' ? 1 : 0, d.beams[0], d.beams[1], d.beams[2]]);
                    postMessage(Object.assign({ type: 'result', index: index, total: total, mode: d.mode, scramble: d.scramble }, JSON.parse(json)));
                }
                postMessage({ type: 'idle' });
            }
        } catch (err) {
            postMessage({ type: 'error', message: String(err && err.message ? err.message : err) });
        }
    });
};
