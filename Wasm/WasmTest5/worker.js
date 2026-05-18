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

let lastTableKey = null;
let cachedAllMoveNames = [];
let cachedTableSizes = [];

self.onmessage = function ({ data }) {
    if (data.type !== 'compute') return;

    if (!solver) {
        self.postMessage({ type: 'error', message: 'WASM not ready' });
        return;
    }

    const { k, targetGroups, startingPerm, solvingPerms, solvingAlgos, minMoves, maxMoves, slack, basePoints } = data;
    const tableKey = JSON.stringify({ k, targetGroups, solvingPerms, solvingAlgos, basePoints });

    try {
        if (tableKey === lastTableKey) {
            // ── Tables already built — jump straight to solve ─────────────────
            self.postMessage({ type: 'cache_hit', tableSizes: cachedTableSizes });
            setTimeout(() => {
                try {
                    solver.setCallback(function(moveIndicesArr) {
                        const solution = [];
                        for (let i = 0; i < moveIndicesArr.length; i++)
                            solution.push(cachedAllMoveNames[moveIndicesArr[i]]);
                        self.postMessage({ type: 'solution', solution });
                    });
                    solver.setDepthCallback(function(depth) {
                        self.postMessage({ type: 'depth', depth });
                    });

                    const startVec = new Module.VectorInt();
                    for (const x of startingPerm) startVec.push_back(x);
                    const result = solver.solve(startVec, minMoves, maxMoves, slack);
                    startVec.delete();

                    const unreachable = result.size() === 1 && result.get(0) === -1;
                    result.delete();

                    self.postMessage({ type: 'done', tableSizes: cachedTableSizes, unreachable });
                } catch (e) {
                    self.postMessage({ type: 'error', message: e.message });
                }
            }, 0);
            return;
        }

        // ── Phase 1: build BSGSes (fast) ─────────────────────────────────────

        solver.reset(k);
        for (const pt of basePoints) solver.addBasePoint(pt);

        // First pass: GENERAL target groups.
        for (const group of targetGroups) {
            if (group.kind === 'orientperm') continue;
            solver.beginTargetGroup();
            for (const perm of group.perms) {
                const v = new Module.VectorInt();
                for (const x of perm) v.push_back(x);
                solver.addTargetGenerator(v);
                v.delete();
            }
            solver.buildTargetGroup(100);
        }

        for (const perm of solvingPerms) {
            const v = new Module.VectorInt();
            for (const x of perm) v.push_back(x);
            solver.addSolvingGenerator(v);
            v.delete();
        }
        solver.buildSolvingBSGS(100);

        // Second pass: ORIENTPERM groups — must come after buildSolvingBSGS so that
        // solving_generators_ is complete when buildOrientPermGroup() calls build().
        for (const group of targetGroups) {
            if (group.kind !== 'orientperm') continue;
            solver.beginOrientPermGroup();
            for (const cls of group.classes) {
                const v = new Module.VectorInt();
                for (const b of cls.bases) v.push_back(b);
                solver.addOrientPermClass(v, cls.m, cls.orientation_mod, cls.typeName);
                v.delete();
            }
            solver.buildOrientPermGroup();
        }

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
        const orbitSizesToOrder = v => {
            let ord = 1n;
            for (let i = 0; i < v.size(); i++) ord *= BigInt(v.get(i));
            v.delete();
            return ord.toString();
        };
        const solvingOrder = orbitSizesToOrder(solver.getSolvingOrbitSizes());
        const groupPreviews = [];
        for (let g = 0; g < solver.getNumGroups(); g++) {
            const targetOrder = orbitSizesToOrder(solver.getTargetGroupOrbitSizes(g));
            const predictedSize = (BigInt(solvingOrder) / BigInt(targetOrder)).toString();
            groupPreviews.push({ solvingOrder, targetOrder, predictedSize });
        }
        self.postMessage({ type: 'preview', groupPreviews });

        // ── Phase 2: heavy work (deferred) ───────────────────────────────────
        setTimeout(() => {
            try {
                solver.buildTables();

                const tableSizes = [];
                for (let g = 0; g < solver.getNumGroups(); g++)
                    tableSizes.push(solver.getGroupTableSize(g));

                lastTableKey = tableKey;
                cachedAllMoveNames = allMoveNames;
                cachedTableSizes = tableSizes;

                self.postMessage({ type: 'tables_built', tableSizes });

                solver.setCallback(function(moveIndicesArr) {
                    const solution = [];
                    for (let i = 0; i < moveIndicesArr.length; i++)
                        solution.push(allMoveNames[moveIndicesArr[i]]);
                    self.postMessage({ type: 'solution', solution });
                });
                solver.setDepthCallback(function(depth) {
                    self.postMessage({ type: 'depth', depth });
                });

                const startVec = new Module.VectorInt();
                for (const x of startingPerm) startVec.push_back(x);
                const result = solver.solve(startVec, minMoves, maxMoves, slack);
                startVec.delete();

                const unreachable = result.size() === 1 && result.get(0) === -1;
                result.delete();

                self.postMessage({ type: 'done', tableSizes, unreachable });
            } catch (e) {
                self.postMessage({ type: 'error', message: e.message });
            }
        }, 0);

    } catch (e) {
        self.postMessage({ type: 'error', message: e.message });
    }
};
