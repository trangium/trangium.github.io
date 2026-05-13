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
        // Step 1: build target BSGS (used for canonicalization inside buildTable)
        loadRunner(targetRunner, k, targetPerms);
        targetRunner.build(40);

        // Step 2: build solving BSGS, get base points
        loadRunner(solvingRunner, k, solvingPerms);
        solvingRunner.build(40);
        const baseVec = solvingRunner.getBase();
        const base = vecToArray(baseVec);
        baseVec.delete();

        // Step 3: load solving moves into targetRunner and run C++ DFS
        targetRunner.clearSolvingMoves();
        for (const move of solvingPerms) {
            const v = new Module.VectorInt();
            for (const x of move) v.push_back(x);
            targetRunner.addSolvingMove(v);
            v.delete();
        }

        const baseVec2 = new Module.VectorInt();
        for (const b of base) baseVec2.push_back(b);
        const tableSize = targetRunner.buildTable(baseVec2);
        baseVec2.delete();

        // Step 4: look up canonical ID of the starting algorithm
        const startVec = new Module.VectorInt();
        for (const x of startingPerm) startVec.push_back(x);
        const id = targetRunner.lookupCanonId(startVec);
        startVec.delete();

        self.postMessage({ type: 'result', id, tableSize });
    } catch (e) {
        self.postMessage({ type: 'error', message: e.message });
    }
};
