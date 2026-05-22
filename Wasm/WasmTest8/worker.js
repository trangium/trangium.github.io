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

    const { k, targetGroups, startingPerm, solvingPerms, solvingAlgos, minMoves, maxMoves, slack, basePoints, incompleteGroupSpecs, productTableSpecs } = data;
    const tableKey = JSON.stringify({ k, targetGroups, solvingPerms, solvingAlgos, basePoints, incompleteGroupSpecs, productTableSpecs });

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

        // appearanceSolverIdx[i] = solver group index for complete group i,
        // or -1 if group i is incomplete (no solver group slot).
        const appearanceSolverIdx = new Array(targetGroups.length).fill(-1);
        const totalCompleteGen = targetGroups.filter(g => g.kind !== 'orientperm' && g.maxDepth == null).length;

        solver.reset(k);
        for (const pt of basePoints) solver.addBasePoint(pt);

        // Pass 1a: complete GENERAL target groups.
        let solverGenCount = 0;
        for (let i = 0; i < targetGroups.length; i++) {
            const group = targetGroups[i];
            if (group.kind === 'orientperm' || group.maxDepth != null) continue;
            appearanceSolverIdx[i] = solverGenCount++;
            solver.beginTargetGroup();
            for (const perm of group.perms) {
                const v = new Module.VectorInt();
                for (const x of perm) v.push_back(x);
                solver.addTargetGenerator(v);
                v.delete();
            }
            solver.buildTargetGroup(100);
        }

        // Pass 1b: incomplete GENERAL groups.
        for (let i = 0; i < targetGroups.length; i++) {
            const group = targetGroups[i];
            if (group.kind === 'orientperm' || group.maxDepth == null) continue;
            solver.beginIncompleteGroup();
            for (const perm of group.perms) {
                const v = new Module.VectorInt();
                for (const x of perm) v.push_back(x);
                solver.addIncompleteGroupGenerator(v);
                v.delete();
            }
            solver.buildIncompleteGroup(100, group.maxDepth);
        }

        for (const perm of solvingPerms) {
            const v = new Module.VectorInt();
            for (const x of perm) v.push_back(x);
            solver.addSolvingGenerator(v);
            v.delete();
        }
        solver.buildSolvingBSGS(100);

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
        for (let i = 0; i < targetGroups.length; i++) {
            const si = appearanceSolverIdx[i];
            if (si === -1) continue;  // incomplete groups have no preview
            const targetOrder = orbitSizesToOrder(solver.getTargetGroupOrbitSizes(si));
            const predictedSize = (BigInt(solvingOrder) / BigInt(targetOrder)).toString();
            groupPreviews.push({ solvingOrder, targetOrder, predictedSize });
        }
        self.postMessage({ type: 'preview', groupPreviews });

        // ── Phase 2: heavy work (deferred) ───────────────────────────────────
        setTimeout(() => {
            try {
                // Pass 2a: complete ORIENTPERM groups — must come after buildSolvingBSGS.
                let solverOpCount = 0;
                for (let i = 0; i < targetGroups.length; i++) {
                    const group = targetGroups[i];
                    if (group.kind !== 'orientperm' || group.maxDepth != null) continue;
                    appearanceSolverIdx[i] = totalCompleteGen + solverOpCount++;
                    solver.beginOrientPermGroup();
                    for (const cls of group.classes) {
                        const v = new Module.VectorInt();
                        for (const b of cls.bases) v.push_back(b);
                        solver.addOrientPermClass(v, cls.m, cls.orientation_mod, cls.typeName);
                        v.delete();
                    }
                    solver.buildOrientPermGroup();
                    if (group.noTransition) solver.setNoTransitionGroup();
                }

                // Pass 2b: incomplete ORIENTPERM groups.
                for (let i = 0; i < targetGroups.length; i++) {
                    const group = targetGroups[i];
                    if (group.kind !== 'orientperm' || group.maxDepth == null) continue;
                    solver.beginIncompleteOrientPermGroup();
                    for (const cls of group.classes) {
                        const v = new Module.VectorInt();
                        for (const b of cls.bases) v.push_back(b);
                        solver.addIncompleteOrientPermClass(v, cls.m, cls.orientation_mod, cls.typeName);
                        v.delete();
                    }
                    solver.buildIncompleteOrientPermGroup(group.maxDepth);
                }

                // Product distance tables: each spec is exactly [T-num-0, T-num-1].
                if (productTableSpecs && productTableSpecs.length > 0) {
                    const completeGroupAppearances = [];
                    for (let i = 0; i < targetGroups.length; i++)
                        if (targetGroups[i].maxDepth == null) completeGroupAppearances.push(i);
                    for (const [tNum0, tNum1] of productTableSpecs) {
                        const idx0 = appearanceSolverIdx[completeGroupAppearances[tNum0 - 1]];
                        const idx1 = appearanceSolverIdx[completeGroupAppearances[tNum1 - 1]];
                        solver.addProductDistanceTable(idx0, idx1);
                    }
                }

                solver.buildTables();

                // Report table sizes in appearance order (complete groups, incomplete groups, product tables).
                const tableSizes = [];
                for (let i = 0; i < targetGroups.length; i++) {
                    const si = appearanceSolverIdx[i];
                    if (si !== -1) tableSizes.push(solver.getGroupTableSize(si));
                }
                for (let i = 0; i < solver.getNumIncompleteGroups(); i++)
                    tableSizes.push(solver.getIncompleteGroupTableSize(i));
                for (let p = 0; p < solver.getNumProductTables(); p++)
                    tableSizes.push(solver.getProductTableSize(p));

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
