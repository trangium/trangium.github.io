// import {Module} from './a.out.js';  // Import the WebAssembly module

let wasmModule = null;

console.log("reached")

// Load the WebAssembly module asynchronously
Module().then((mod) => {
    wasmModule = mod;
    console.log("WebAssembly module loaded in worker");
}).catch((err) => {
    console.error("Error loading WebAssembly module:", err);
});

self.onmessage = function(event) {
    if (!wasmModule) {
        postMessage("WebAssembly module not loaded yet.");
        return;
    }

    // Use the embind-exposed function (e.g., add)
    const value = parseInt(event.data, 10); // Convert input to integer
    const result = wasmModule.add(value, 10); // Call the WebAssembly function exposed by embind
    postMessage(result); // Send the result back to the main thread
};