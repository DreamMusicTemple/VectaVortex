#!/bin/sh
# Builds the freestanding WebAssembly LUA engine used by the web app (lx.wasm, embedded as base64 in the page).
clang++ --target=wasm32 -O2 -std=c++17 -nostdlib -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -mbulk-memory -mnontrapping-fptoint -ffp-contract=off -w -Wl,--no-entry -Wl,--allow-undefined -Wl,--initial-memory=33554432 -Wl,--max-memory=536870912 -Wl,-z,stack-size=524288 -o "${1:-lx.wasm}" lx_wasm.cpp
