#!/bin/bash
# PC test of the aim link (aim_link.cpp) against a PRETEND libil2cpp.so and a pretend basketball world. Proves the logic, not the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp_aim.cpp" -o "$W/libil2cpp.so" || exit 2
g++ -std=c++17 -Wall -Wextra -pthread -I"$HERE/../src" "$HERE/test_aimlink.cpp" "$HERE/../src/aim_link.cpp" "$HERE/../src/aim_bank.cpp" "$HERE/../src/bank.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/il2cpp_scan.cpp" -ldl -o "$W/t" || exit 2
timeout 240 "$W/t" "$W/libil2cpp.so"
