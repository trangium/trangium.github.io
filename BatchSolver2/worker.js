// Web Worker shell: receives the fields from the page, runs the batch, streams messages back.
import { runBatch } from "./src/pipeline.js";

self.onmessage = (msg) => {
    if (!msg.data || msg.data.puzzle === undefined) return;
    runBatch(msg.data, {
        post: (m) => self.postMessage(m),
        wasm: new URL("./wasm/engine.wasm", import.meta.url).href,
        loadText: async (rel) => {
            const r = await fetch(new URL(rel, import.meta.url));
            if (!r.ok) throw new Error("HTTP " + r.status);
            return r.text();
        },
    });
};
