#!/bin/bash
# PC test of the movement rules (movement.cpp) against a pretend game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -std=c++17 -Wall -I"$HERE/../src" "$HERE/test_movement.cpp" "$HERE/../src/movement.cpp" -o "$W/t" || exit 2
"$W/t"
