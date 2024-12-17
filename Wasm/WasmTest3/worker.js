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
    self.postMessage(wasmModule.compute(e.data.value))
};