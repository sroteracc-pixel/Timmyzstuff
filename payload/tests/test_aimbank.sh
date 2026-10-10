#!/bin/bash
# PC test of the Aimbot's BANK mode (stage D9) against a PRETEND libil2cpp.so with two pretend backboards. Proves the logic, not the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp_aim.cpp" -o "$W/libil2cpp.so" || exit 2
g++ -std=c++17 -Wall -Wextra -pthread -I"$HERE/../src" "$HERE/test_aimbank.cpp" "$HERE/../src/aim_link.cpp" "$HERE/../src/aim_bank.cpp" "$HERE/../src/aim_points.cpp" "$HERE/../src/aim_hitbox.cpp" "$HERE/../src/bank.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/il2cpp_scan.cpp" -ldl -o "$W/t" || exit 2
timeout 900 "$W/t" "$W/libil2cpp.so"
