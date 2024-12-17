// worker.js
let wasmModule = null;

// Use importScripts to load the Emscripten-generated JavaScript
try {
    importScripts('compute.js');
    let p = Module();
    p.then((value) => {wasmModule = value});
} catch (error) {
    self.postMessage(`Script import error: ${error.message}`)
}

// Wait for the WebAssembly module to be ready
self.onmessage = async function(e) {
    self.postMessage(wasmModule._compute(13))
};