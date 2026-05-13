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

    const { k, targetPerms, startingPerm, solvingPerms, solvingAlgos } = data;
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
        // Mirror addSolvingMove's dedup logic to build a parallel name list.
        const permStr = p => p.join(',');
        const invPerm = p => { const r = new Array(k); for (let i = 0; i < k; i++) r[p[i]] = i; return r; };
        const allMoveNames = [];
        const seenPerms = new Set();
        targetRunner.clearSolvingMoves();
        for (let i = 0; i < solvingPerms.length; i++) {
            const p = solvingPerms[i];
            const name = solvingAlgos[i].join(' ');
            const ps = permStr(p);
            if (!seenPerms.has(ps)) {
                seenPerms.add(ps);
                allMoveNames.push(name);
                const v = new Module.VectorInt();
                for (const x of p) v.push_back(x);
                targetRunner.addSolvingMove(v);
                v.delete();
            }
            const pInv = invPerm(p);
            const pInvS = permStr(pInv);
            if (pInvS !== ps && !seenPerms.has(pInvS)) {
                seenPerms.add(pInvS);
                const invName = solvingAlgos[i].length === 1 ? name + "'" : "(" + name + ")'";
                allMoveNames.push(invName);
            }
        }

        const baseVec2 = new Module.VectorInt();
        for (const b of base) baseVec2.push_back(b);
        const tableSize = targetRunner.buildTable(baseVec2);
        baseVec2.delete();

        targetRunner.buildTransitionTable();
        targetRunner.buildDistanceTable();

        // Step 4: reconstruct solution path from starting algorithm to identity
        const startVec = new Module.VectorInt();
        for (const x of startingPerm) startVec.push_back(x);
        const canonId = targetRunner.lookupCanonId(startVec);
        startVec.delete();

        let solution = null;
        if (canonId !== -1) {
            solution = [];
            let currentId = canonId;
            let d = targetRunner.getDistance(canonId);
            while (d > 0) {
                const row = targetRunner.getTransitionRow(currentId);
                let nextId = -1, nextMi = -1;
                for (let mi = 0; mi < row.size(); mi++) {
                    const nid = row.get(mi);
                    if (targetRunner.getDistance(nid) === d - 1) {
                        nextId = nid; nextMi = mi; break;
                    }
                }
                row.delete();
                if (nextId === -1) break;
                solution.push(allMoveNames[nextMi]);
                currentId = nextId;
                d--;
            }
        }

        self.postMessage({ type: 'result', solution, tableSize });
    } catch (e) {
        self.postMessage({ type: 'error', message: e.message });
    }
};
