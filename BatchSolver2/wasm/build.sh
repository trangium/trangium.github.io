#!/bin/sh
# Builds engine.wasm from engine.cpp. Needs clang (with the wasm32 backend) and lld.
set -e
cd "$(dirname "$0")"
clang++ --target=wasm32 -O3 -std=c++17 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
  -mbulk-memory -fuse-ld=lld \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--max-memory=4294901760 -Wl,-z,stack-size=1048576 -Wl,--initial-memory=2097152 \
  -o engine.wasm engine.cpp
ls -l engine.wasm
