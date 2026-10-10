#!/bin/bash
# PC test of the bank-shot maths (bank.cpp). Touches no game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
g++ -O2 -std=c++17 -Wall -Wextra -I"$HERE/../src" "$HERE/test_bank.cpp" "$HERE/../src/bank.cpp" "$HERE/../src/aimbot.cpp" -o "$W/t" || exit 2
"$W/t"
