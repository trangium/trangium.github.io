#include <emscripten/bind.h>
#include <string>

using namespace emscripten;
using std::string;

string run(string x) {
    return x + " and " + x;
}

EMSCRIPTEN_BINDINGS(my_module) {
    function("add", &run);
}