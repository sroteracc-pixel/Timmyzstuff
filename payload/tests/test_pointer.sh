#!/bin/bash
# PC test of the pointing/clicking rules (pointer.cpp), the number pad, the slider and saved settings.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -O2 -std=c++17 -Wall -I"$HERE/../src" "$HERE/test_pointer.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" -o "$W/t" || exit 2
"$W/t"
