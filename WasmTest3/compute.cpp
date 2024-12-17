#include <emscripten/emscripten.h>

// Use extern "C" to prevent name mangling
extern "C" {
    // Explicitly mark for export
    EMSCRIPTEN_KEEPALIVE
    int compute(int input) {
        // Example computation: square the input and add 10
        int a = input;
        int ct = 0;
        while (a != 1) {
            a *= input;
            ct++;
        }
        return ct;
    }
}