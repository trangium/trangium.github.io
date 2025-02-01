let wasmModule = null;

var Module = {
    preRun:[],
    onRuntimeInitialized: function load_done_callback() {
        wasmModule = Module;
    },
};

try {
    importScripts('a.out.js');
} catch (error) {
    self.postMessage(`Script import error: ${error.message}`)
}

self.onmessage = async function(e) {
    const res = wasmModule.compute(e.data);
    let acc = "";
    for (let i=0; i<res.size(); i++) {
        acc += res.get(i);
        if (i<res.size()-1) acc += ", ";
    }
    self.postMessage(acc);
};