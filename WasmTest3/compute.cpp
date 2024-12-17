#include <emscripten/bind.h>
#include <string>

using namespace emscripten;
using std::string;

string compute(string x) { 
    return x + " and " + x;
}

EMSCRIPTEN_BINDINGS(my_module) {
    function("compute", &compute);
}