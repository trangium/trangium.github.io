let targetRunner = null;
let solvingRunner = null;

var Module = {
    onRuntimeInitialized() {
        targetRunner  = new Module.SchreierSimsRunner();
        solvingRunner = new Module.SchreierSimsRunner();
    }
};

try {
    importScripts('a.out.js');
} catch (e) {
    self.postMessage({ type: 'error', message: 'Failed to load WASM: ' + e.message });
}

function vecToArray(v) {
    const a = [];
    for (let i = 0; i < v.size(); i++) a.push(v.get(i));
    return a;
}

function loadRunner(runner, k, perms) {
    runner.reset(k);
    for (const perm of perms) {
        const v = new Module.VectorInt();
        for (const x of perm) v.push_back(x);
        runner.addGenerator(v);
        v.delete();
    }
}

self.onmessage = function ({ data }) {
    if (data.type !== 'compute') return;

    if (!targetRunner || !solvingRunner) {
        self.postMessage({ type: 'error', message: 'WASM not ready' });
        return;
    }

    const { k, targetPerms, startingPerm, solvingPerms } = data;
    try {
        // Step 1: build target BSGS
        loadRunner(targetRunner, k, targetPerms);
        targetRunner.build(40);

        // Step 2: canonicalize starting perm
        const startVec = new Module.VectorInt();
        for (const x of startingPerm) startVec.push_back(x);
        const canonVec = targetRunner.canonicalizePerm(startVec);
        startVec.delete();
        const canonPerm = vecToArray(canonVec);
        canonVec.delete();

        // Step 3: build solving BSGS, get base
        loadRunner(solvingRunner, k, solvingPerms);
        solvingRunner.build(40);
        const baseVec = solvingRunner.getBase();
        const base = vecToArray(baseVec);
        baseVec.delete();

        // Step 4: images of canonicalized perm at each base point
        const images = base.map(b => canonPerm[b]);

        self.postMessage({ type: 'result', base, images, canonPerm });
    } catch (e) {
        self.postMessage({ type: 'error', message: e.message });
    }
};
