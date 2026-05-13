let wasmModule = null;
let cubeEv     = null;
let simsTable  = null;
let cubeK      = 0;   // domain size of the current cube setup

var Module = {
    onRuntimeInitialized() {
        wasmModule = Module;
        cubeEv    = new Module.CubeEvaluator();
        simsTable = new Module.SchreierSimsTable();
    }
};

try {
    importScripts('a.out.js');
} catch (e) {
    self.postMessage({ type: 'error', message: 'Failed to load WASM: ' + e.message });
}

function notReady() { self.postMessage({ type: 'error', message: 'WASM not ready' }); }

// Helper: VectorInt → plain JS array
function vecToArray(v) {
    const a = [];
    for (let i = 0; i < v.size(); i++) a.push(v.get(i));
    return a;
}

self.onmessage = function({ data }) {
    if (!wasmModule) { notReady(); return; }

    // ── Min-plus ───────────────────────────────────────────────────────────────
    if (data.type === 'evaluate') {
        const { initialVector, moves, sequence } = data;
        try {
            const ev = new wasmModule.AlgorithmEvaluator();

            const vec = new wasmModule.VectorFloat();
            for (const v of initialVector) vec.push_back(v);
            ev.setInitialVector(vec);
            vec.delete();

            for (const [name, flat] of Object.entries(moves)) {
                const mv = new wasmModule.VectorFloat();
                for (const v of flat) mv.push_back(v);
                ev.addMove(name, mv);
                mv.delete();
            }

            const seqVec = new wasmModule.VectorString();
            for (const s of sequence) seqVec.push_back(s);
            const result = ev.evaluate(seqVec);
            seqVec.delete();
            ev.delete();

            self.postMessage({ type: 'result', value: result });
        } catch (e) {
            self.postMessage({ type: 'error', message: e.message });
        }

    // ── Cube setup ─────────────────────────────────────────────────────────────
    } else if (data.type === 'cube-setup') {
        const { k, atomicMoves, derivedMoves } = data;
        try {
            cubeEv.setK(k);
            cubeK = k;

            for (const { name, perm } of atomicMoves) {
                const v = new wasmModule.VectorInt();
                for (const x of perm) v.push_back(x);
                cubeEv.addMove(name, v);
                v.delete();
            }

            const moveNames = atomicMoves.map(m => m.name);
            for (const { name, seq } of derivedMoves) {
                const sv = new wasmModule.VectorString();
                for (const s of seq) sv.push_back(s);
                cubeEv.composeMove(name, sv);
                sv.delete();
                moveNames.push(name);
            }

            self.postMessage({ type: 'cube-ready', moveNames });
        } catch (e) {
            self.postMessage({ type: 'cube-error', message: e.message });
        }

    // ── Cube eval (base points) ────────────────────────────────────────────────
    } else if (data.type === 'cube-eval') {
        const { base, sequence } = data;
        try {
            const baseVec = new wasmModule.VectorInt();
            for (const x of base) baseVec.push_back(x);
            const seqVec = new wasmModule.VectorString();
            for (const s of sequence) seqVec.push_back(s);

            const res = cubeEv.evaluate(baseVec, seqVec);
            const positions = vecToArray(res);
            res.delete(); baseVec.delete(); seqVec.delete();

            self.postMessage({ type: 'cube-result', base, positions });
        } catch (e) {
            self.postMessage({ type: 'cube-error', message: e.message });
        }

    // ── Schreier-Sims build ────────────────────────────────────────────────────
    } else if (data.type === 'sims-build') {
        if (cubeK === 0) {
            self.postMessage({ type: 'sims-error', message: 'Set up cube moves first (Parse).' });
            return;
        }
        const { generatorAlgos } = data;
        try {
            simsTable.reset(cubeK);

            for (const algo of generatorAlgos) {
                if (!algo.length) continue;
                const sv = new wasmModule.VectorString();
                for (const s of algo) sv.push_back(s);
                const permVec = cubeEv.evaluateFull(sv);
                sv.delete();

                const pv = new wasmModule.VectorInt();
                for (let i = 0; i < permVec.size(); i++) pv.push_back(permVec.get(i));
                permVec.delete();

                simsTable.addGenerator(pv);
                pv.delete();
            }

            simsTable.build();

            const bv = simsTable.getBase();
            const base = vecToArray(bv);
            bv.delete();

            const ov = simsTable.getOrbitSizes();
            const orbitSizes = vecToArray(ov);
            ov.delete();

            self.postMessage({ type: 'sims-ready', base, orbitSizes });
        } catch (e) {
            self.postMessage({ type: 'sims-error', message: e.message });
        }

    // ── Schreier-Sims membership test ─────────────────────────────────────────
    } else if (data.type === 'sims-test') {
        const { algo } = data;
        try {
            const sv = new wasmModule.VectorString();
            for (const s of algo) sv.push_back(s);
            const permVec = cubeEv.evaluateFull(sv);
            sv.delete();

            const pv = new wasmModule.VectorInt();
            for (let i = 0; i < permVec.size(); i++) pv.push_back(permVec.get(i));
            permVec.delete();

            const inGroup = simsTable.isMember(pv);
            pv.delete();

            self.postMessage({ type: 'sims-result', inGroup });
        } catch (e) {
            self.postMessage({ type: 'sims-error', message: e.message });
        }
    }
};
