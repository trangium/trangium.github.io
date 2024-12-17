// Load the WebAssembly module asynchronously
// Module().then((mod) => {
//     wasmModule = mod;
//     console.log("WebAssembly module loaded in worker");
// }).catch((err) => {
//     console.error("Error loading WebAssembly module:", err);
// });

var g_objInstance = null;   

self.onmessage = function (evt) {
    console.log(evt);  
    var objData = evt.data;     
    var sMessagePurpose = objData.MessagePurpose; 
    console.log(g_objInstance);
    postMessage(20);
}