#!/bin/sh
# Builds and runs every C++ test (golden parity vs the JavaScript worklets, stress, engine). Works on Linux and macOS.
set -e
cd "$(dirname "$0")"
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build . -j 4
ctest --output-on-failure
