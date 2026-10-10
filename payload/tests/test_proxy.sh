#!/bin/bash
# PC test of the pass-through logic using a FAKE "original" library.
# Proves: it finds the _orig library next to itself, forwards JNI_OnLoad, and fails loudly if it can't.
# Does NOT prove anything about the real game's library - that needs your Quest.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -I"$HERE/fake_jni" -DTZ_FAST_TEST -fvisibility=hidden -I"$HERE/../src" -I"$HERE/../../menu/src" "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
cat > "$W/orig.cpp" <<'C'
#include <cstdio>
extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { std::fputs("[fake original] JNI_OnLoad was called\n", stderr); return 0x00010006; }
C
cat > "$W/orig_nofn.cpp" <<'C'
extern "C" int something_else() { return 1; }
C
cat > "$W/host.cpp" <<'C'
#include <dlfcn.h>
#include <cstdio>
int main(int, char** argv) {
  void* h = dlopen(argv[1], RTLD_NOW);
  if (!h) { std::printf("LOADFAIL\n"); return 3; }
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl

echo "== original present with JNI_OnLoad"
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"
out=$("$W/host" "$W/libmain.so" 2>&1)
check "forwards to the original and returns its value (65542 = 0x10006)" '[[ "$out" == *"RESULT=65542"* ]]'
check "the original's JNI_OnLoad really ran" '[[ "$out" == *"fake original] JNI_OnLoad was called"* ]]'
check "our own log line appeared" '[[ "$out" == *"payload loaded"* ]]'

echo "== original missing"
rm "$W/libmain_orig.so"; out=$("$W/host" "$W/libmain.so" 2>&1)
check "returns JNI_ERR (-1) instead of pretending" '[[ "$out" == *"RESULT=-1"* && "$out" == *"could not load the original"* ]]'

echo "== original has no JNI_OnLoad"
g++ -shared -fPIC "$W/orig_nofn.cpp" -o "$W/libmain_orig.so"; out=$("$W/host" "$W/libmain.so" 2>&1)
check "returns JNI_VERSION_1_6 with a warning" '[[ "$out" == *"RESULT=65542"* && "$out" == *"no JNI_OnLoad"* ]]'

echo "== exports"
syms=$(nm -D --defined-only "$W/libmain.so" | awk '{print $3}' | grep -v '^_' | sort | tr '\n' ' ')
check "exports exactly JNI_OnLoad" '[[ "$syms" == "JNI_OnLoad " ]]'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
