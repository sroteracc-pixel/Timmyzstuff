#!/bin/bash
# PC test of the game link (game_link.cpp) against a PRETEND libil2cpp.so and a pretend player object. Proves the logic, not the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp.cpp" -o "$W/libil2cpp.so" || exit 2
g++ -std=c++17 -Wall -pthread -I"$HERE/../src" "$HERE/test_gamelink.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/movement.cpp" -ldl -o "$W/t" || exit 2
timeout 180 "$W/t" "$W/libil2cpp.so"
