# Builds the browser solver:  powershell -File build_wasm.ps1      (run from SuperCubeSolver444\cpp)
# Output (next to index.html, i.e. in SuperCubeSolver444\): solver.js, solver.wasm, solver.data (the two cached S3'->S4 tables)
# Needs the emsdk of this repository (..\..\emsdk, already activated).
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$emsdk = Resolve-Path (Join-Path $here '..\..\emsdk')
$env:EM_CONFIG = Join-Path $emsdk '.emscripten'
$env:PATH = "$emsdk\upstream\emscripten;$emsdk\python\3.13.3_64bit;$emsdk\node\22.16.0_64bit\bin;" + $env:PATH
Set-Location $here
$out = Resolve-Path '..'
$args = @(
    '-O3', '-std=c++17', '-fno-exceptions', 'web_main.cpp',
    '-o', "$out\solver.js",
    '-sMODULARIZE=1', '-sEXPORT_NAME=createSolver', '-sENVIRONMENT=web,worker,node',
    '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=167772160', '-sMAXIMUM_MEMORY=1073741824', '-sSTACK_SIZE=8388608',
    '-sEXPORTED_FUNCTIONS=_sc_init,_sc_solve,_sc_set_verbose,_sc_heap_bytes', '-sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString',
    '--preload-file', 's4_sym_layer_ud.bin@/cache/s4_sym_layer_ud.bin',
    '--preload-file', 's4_sym_eqpllLRFB_ud.bin@/cache/s4_sym_eqpllLRFB_ud.bin'
)
& emcc @args
if ($LASTEXITCODE -ne 0) { throw "emcc failed" }
Get-ChildItem "$out\solver.*" | Select-Object Name, Length
