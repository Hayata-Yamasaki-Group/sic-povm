#!/usr/bin/env bash
# Build the C++ code and run all tests. Run from anywhere.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== C++: configure, build, test"
cmake -S "$ROOT/cpp" -B "$ROOT/cpp/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/cpp/build" -j
ctest --test-dir "$ROOT/cpp/build" --output-on-failure

echo "== All checks passed"
