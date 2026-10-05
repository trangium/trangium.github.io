// WASM entry points of the production build (see web_solver.h, build_wasm.bat, ../worker.js).
//   sc_init(supercube_safe)        builds the tables of that mode (progress: Module.onProgress(step, total, what));
//                                  calling it again with the other mode only rebuilds the first-phase table
//   sc_solve(scramble, prefix, suffix, supercube_safe, random_state, seed_hi, seed_lo)
//                                  -> JSON text {"ok":true,"text":...,"moves":N,"nodes":N,"ms":N} or {"ok":false,"error":...}
#include "web_solver.h"
#include <emscripten.h>
#include <emscripten/heap.h>

EM_JS(void, js_progress, (int step, int total, const char* what), {
    if (Module['onProgress']) Module['onProgress'](step, total, UTF8ToString(what));
});

static void progress_hook(int step, int total, const char* what) { js_progress(step, total, what); }

static std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static std::string g_out;

extern "C" {

EMSCRIPTEN_KEEPALIVE int sc_init(int supercube_safe) {
    web::g_progress = progress_hook;
    web::g_cache_dir = "/cache";
    web::build_tables(supercube_safe != 0);
    return 0;
}

// diagnostics: step timings of the table building (printed through Module.print), current size of the WASM heap in bytes
EMSCRIPTEN_KEEPALIVE void sc_set_verbose(int on) { g_startup_times = on != 0; }
EMSCRIPTEN_KEEPALIVE double sc_heap_bytes() { return (double)emscripten_get_heap_size(); }

EMSCRIPTEN_KEEPALIVE const char* sc_solve(const char* scramble, const char* prefix, const char* suffix,
                                          int supercube_safe, int random_state, unsigned seed_hi, unsigned seed_lo) {
    const uint64_t seed = ((uint64_t)seed_hi << 32) | seed_lo;
    web::Result r = web::run(scramble, prefix, suffix, supercube_safe != 0, random_state != 0, seed);
    if (r.ok) {
        char b[160];
        snprintf(b, sizeof b, "\",\"moves\":%d,\"nodes\":%lld,\"ms\":%.0f,\"random\":%s}", r.moves, r.nodes, r.ms, r.random_state ? "true" : "false");
        g_out = "{\"ok\":true,\"text\":\"" + json_escape(r.text) + b;
    } else {
        g_out = "{\"ok\":false,\"error\":\"" + json_escape(r.error) + "\"}";
    }
    return g_out.c_str();
}

}
