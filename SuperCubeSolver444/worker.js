// Web worker of the 4x4x4 supercube solver page: loads the WASM solver, builds its tables and runs the solves.
// Messages from the page:  {type:'prepare', safe}  build all the tables (both modes)
//                          {type:'run', safe, randomState, scramble, prefix, suffix}
// Messages to the page:    {type:'idle'} (tables ready) {type:'progress', step, total, what} (unused by the page)
//                          {type:'solving'} {type:'result', ...} {type:'error', message}
// bump VERSION whenever solver.js / solver.wasm / solver.data are rebuilt: it keeps browsers from mixing a cached old file with a new one
const VERSION = '4';
importScripts('solver.js?v=' + VERSION);

let M = null;
let queue = Promise.resolve();

const ready = createSolver({
    locateFile: (file) => file + '?v=' + VERSION,
    onProgress: (step, total, what) => postMessage({ type: 'progress', step, total, what }),
    print: () => {},
    printErr: () => {},
    onAbort: (what) => postMessage({ type: 'error', message: 'The solver stopped (' + what + '). It probably ran out of memory; reload the page.' })
}).then((m) => { M = m; });

let built = false;
// the first call builds the tables of both modes; later calls only switch the mode (costs, penalty table, S1 depth)
function ensureTables(safe) {
    M.ccall('sc_init', 'number', ['number'], [safe ? 1 : 0]);
    built = true;
}

function randomSeed() {
    const a = new Uint32Array(2);
    crypto.getRandomValues(a);
    return a;
}

onmessage = (e) => {
    const d = e.data;
    queue = queue.then(async () => {
        try {
            await ready;
            if (d.type === 'prepare') {
                ensureTables(d.safe);
                postMessage({ type: 'idle' });
            } else if (d.type === 'run') {
                ensureTables(d.safe);
                postMessage({ type: 'solving' });
                const seed = randomSeed();
                const json = M.ccall('sc_solve', 'string',
                    ['string', 'string', 'string', 'number', 'number', 'number', 'number'],
                    [d.scramble, d.prefix, d.suffix, d.safe ? 1 : 0, d.randomState ? 1 : 0, seed[0], seed[1]]);
                postMessage(Object.assign({ type: 'result' }, JSON.parse(json)));
            }
        } catch (err) {
            postMessage({ type: 'error', message: String(err && err.message ? err.message : err) });
        }
    });
};
