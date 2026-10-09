#!/bin/bash
# PC test of the "Scan game code" reader against a PRETEND libil2cpp.so. Proves the logic, not the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp.cpp" -o "$W/libil2cpp.so" || exit 2
g++ -std=c++17 -Wall -I"$HERE/../src" "$HERE/test_scan.cpp" "$HERE/../src/il2cpp_scan.cpp" -ldl -o "$W/t" || exit 2
"$W/t" "$W/libil2cpp.so"
