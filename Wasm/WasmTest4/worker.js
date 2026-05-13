let wasmModule = null;
let runner = null;

var Module = {
    onRuntimeInitialized() {
        wasmModule = Module;
        runner = new Module.SchreierSimsRunner();
    }
};

try {
    importScripts('a.out.js');
} catch (e) {
    self.postMessage({ type: 'error', message: 'Failed to load WASM: ' + e.message });
}

self.onmessage = function({ data }) {
    if (!wasmModule) {
        self.postMessage({ type: 'error', message: 'WASM not ready' });
        return;
    }

    if (data.type === 'run') {
        const { n, generators, confidence } = data;
        try {
            runner.reset(n);
            for (const gen of generators) {
                const v = new wasmModule.VectorInt();
                for (const x of gen) v.push_back(x);
                runner.addGenerator(v);
                v.delete();
            }
            const result = JSON.parse(runner.run(confidence));
            self.postMessage({ type: 'result', ...result });
        } catch (e) {
            self.postMessage({ type: 'error', message: e.message });
        }
    }
};
