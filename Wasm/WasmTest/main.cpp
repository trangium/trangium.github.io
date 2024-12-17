#include <emscripten/bind.h>
#include <string>

using namespace emscripten;
using std::string;

string run(string x) { 
    int ct = 0;
    for (int i=0; i<1e4; i++) {
        for (int j=0; j<1e5; j++) {
            if (i*j==240000) {
                ct++;
            }
        }
    }
    if (ct > 4) {
        return x + " and " + x;
    } else {
        return "and????";
    }
}

EMSCRIPTEN_BINDINGS(my_module) {
    function("add", &run);
}