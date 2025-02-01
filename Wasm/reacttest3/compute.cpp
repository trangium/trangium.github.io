#include <emscripten/bind.h>
#include <string>
#include <vector>

using namespace emscripten;
using std::string;
using std::vector;

#define ll long long

vector<string> compute(string x) { 
    vector<string> res;
    string acc = "";
    for (int i=0; i<x.length(); i++) {
        acc += x;
        res.push_back(acc);
    }
    return res;
}

EMSCRIPTEN_BINDINGS(my_module) {
    // Explicitly register std::string and std::vector types
    register_vector<string>("doesntmatter");
    
    // Bind the function with explicit type
    function("compute", &compute, 
             allow_raw_pointers());
}