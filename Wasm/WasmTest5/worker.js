let solver = null;

var Module = {
    onRuntimeInitialized() {
        solver = new Module.MultiTargetSolver();
    }
};

try {
    importScripts('a.out.js');
} catch (e) {
    self.postMessage({ type: 'error', message: 'Failed to load WASM: ' + e.message });
}

self.onmessage = function ({ data }) {
    if (data.type !== 'compute') return;

    if (!solver) {
        self.postMessage({ type: 'error', message: 'WASM not ready' });
        return;
    }

    const { k, targetPermsArray, startingPerm, solvingPerms, solvingAlgos } = data;
    try {
        solver.reset(k);

        // ── Phase 1: build BSGSes (fast) ─────────────────────────────────────

        for (const targetPerms of targetPermsArray) {
            solver.beginTargetGroup();
            for (const perm of targetPerms) {
                const v = new Module.VectorInt();
                for (const x of perm) v.push_back(x);
                solver.addTargetGenerator(v);
                v.delete();
            }
            solver.buildTargetGroup(40);
        }

        for (const perm of solvingPerms) {
            const v = new Module.VectorInt();
            for (const x of perm) v.push_back(x);
            solver.addSolvingGenerator(v);
            v.delete();
        }
        solver.buildSolvingBSGS(40);

        // Build solving move list and parallel name list
        const permStr = p => p.join(',');
        const invPerm = p => { const r = new Array(k); for (let i = 0; i < k; i++) r[p[i]] = i; return r; };
        const allMoveNames = [];
        const seenPerms = new Set();
        solver.clearSolvingMoves();
        for (let i = 0; i < solvingPerms.length; i++) {
            const p = solvingPerms[i];
            const name = solvingAlgos[i].join(' ');
            const ps = permStr(p);
            if (!seenPerms.has(ps)) {
                seenPerms.add(ps);
                allMoveNames.push(name);
                const v = new Module.VectorInt();
                for (const x of p) v.push_back(x);
                solver.addSolvingMove(v);
                v.delete();
            }
            const pInv = invPerm(p);
            const pInvS = permStr(pInv);
            if (pInvS !== ps && !seenPerms.has(pInvS)) {
                seenPerms.add(pInvS);
                allMoveNames.push(solvingAlgos[i].length === 1 ? name + "'" : "(" + name + ")'");
            }
        }

        // Post group sizes so the UI can show predicted table sizes before the
        // heavy work starts. setTimeout(0) yields back to the event loop so the
        // main thread receives this message first.
        const solvingOrder = solver.getSolvingOrderStr();
        const groupPreviews = [];
        for (let g = 0; g < solver.getNumGroups(); g++) {
            const targetOrder = solver.getTargetGroupOrderStr(g);
            const predictedSize = (BigInt(solvingOrder) / BigInt(targetOrder)).toString();
            groupPreviews.push({ solvingOrder, targetOrder, predictedSize });
        }
        self.postMessage({ type: 'preview', groupPreviews });

        // ── Phase 2: heavy work (deferred) ───────────────────────────────────
        setTimeout(() => {
            try {
                solver.buildTables();

                const startVec = new Module.VectorInt();
                for (const x of startingPerm) startVec.push_back(x);
                const moveIndices = solver.solve(startVec);
                startVec.delete();

                let solution = null;
                if (!(moveIndices.size() === 1 && moveIndices.get(0) === -1)) {
                    solution = [];
                    for (let i = 0; i < moveIndices.size(); i++)
                        solution.push(allMoveNames[moveIndices.get(i)]);
                }
                moveIndices.delete();

                const tableSizes = [];
                for (let g = 0; g < solver.getNumGroups(); g++)
                    tableSizes.push(solver.getGroupTableSize(g));

                self.postMessage({ type: 'result', solution, tableSizes });
            } catch (e) {
                self.postMessage({ type: 'error', message: e.message });
            }
        }, 0);

    } catch (e) {
        self.postMessage({ type: 'error', message: e.message });
    }
};
