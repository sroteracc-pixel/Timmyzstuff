#!/bin/bash
# PC test of the Stage C input probe using a FAKE libOVRPlugin.so and a FAKE original library.
# The fake "controllers" do: idle, both triggers, A click (menu should OPEN), release, B click
# (menu should CLOSE). Does NOT prove anything about the real game or OVRPlugin - that needs your Quest.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
cat > "$W/orig.cpp" <<'C'
extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { return 0x00010006; }
C
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"

cat > "$W/ovr.cpp" <<'C'
#include <cstring>
static int calls = 0;
extern "C" int ovrp_GetInitialized() { return 1; }
extern "C" int ovrp_GetControllerState4(unsigned int mask, void* out) {
  ++calls; unsigned char* p = (unsigned char*)out; std::memset(p, 0, 200);
  std::memcpy(p, &mask, 4);
  float one = 1.0f; unsigned int a = 1, b = 2;
  if (calls >= 6 && calls <= 30) { std::memcpy(p + 16, &one, 4); std::memcpy(p + 20, &one, 4); }   // both triggers
  if (calls >= 12 && calls <= 16) std::memcpy(p + 4, &a, 4);                                        // A click
  if (calls >= 40 && calls <= 44) std::memcpy(p + 4, &b, 4);                                        // B click
  return 0;
}
// fake pose function: writes exactly 88 bytes (like the real one) and refuses a null pointer
extern "C" int ovrp_GetNodePoseState3(int step, int frame, int node, void* out) {
  if (!out) return -1001;
  unsigned char* p = (unsigned char*)out; std::memset(p, 0, 88); p[0] = (unsigned char)node; p[1] = (unsigned char)(step & 0xff); p[2] = (unsigned char)(frame & 0xff);
  return 0;
}
C
g++ -shared -fPIC "$W/ovr.cpp" -o "$W/libOVRPlugin.so"

cat > "$W/host.cpp" <<'C'
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
int main(int, char** argv) {
  void* ovr = dlopen("libOVRPlugin.so", RTLD_NOW);
  if (!ovr) { std::printf("NOOVR\n"); return 4; }
  void* h = dlopen(argv[1], RTLD_NOW);
  if (!h) { std::printf("LOADFAIL\n"); return 3; }
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  sleep(9);
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl
mkdir -p "$W/facts"
out=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host" "$W/libmain.so" 2>&1)
F="$W/facts/timmyzstuff_facts.txt"

echo "== probe on"
check "game's JNI_OnLoad result is unchanged (65542)" '[[ "$out" == *"RESULT=65542"* ]]'
check "facts file was written" '[ -s "$F" ]'
check "reports the controller function as found" 'grep -q "function ovrp_GetControllerState4: found" "$F"'
check "menu OPENS on both triggers + A" 'grep -q "MENU OPEN  (#1)" "$F"'
check "menu CLOSES on B" 'grep -q "MENU CLOSE (#1)" "$F"'
check "summary says 1 open, 1 close" 'grep -q "summary: menu opened 1 times, closed 1 times" "$F"'
check "pose snapshot taken at start and when A is pressed" 'grep -q "pose snapshot 1 (at start)" "$F" && grep -q "(A pressed)" "$F"'
check "pose lines list 8 nodes x 4 step/frame combos per snapshot" '[ "$(grep -c "^pose step=" "$F")" -ge 64 ]'
check "pose bytes are 88 long (176 hex chars)" 'grep "^pose step=" "$F" | head -1 | grep -qE "bytes=[0-9a-f]{176}$"'
check "never writes memory addresses (no 0x)" '! grep -q "0x" "$F"'
check "finishes cleanly" 'grep -q "^summary:" "$F" && grep -q "^done:" "$F"'

echo "== VR library never appears"
rm -f "$F"; mv "$W/libOVRPlugin.so" "$W/libOVRPlugin.so.off"
cat > "$W/host2.cpp" <<'C'
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
int main(int, char** argv) {
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  sleep(7);
  return 0;
}
C
g++ -o "$W/host2" "$W/host2.cpp" -ldl
out2=$(cd "$W" && TZ_FACTS_DIR="$W/facts" "$W/host2" "$W/libmain.so" 2>&1)
check "game still starts normally" '[[ "$out2" == *"RESULT=65542"* ]]'
check "says the VR library never appeared" 'grep -q "never appeared" "$F"'

echo "== exports"
syms=$(nm -D --defined-only "$W/libmain.so" | awk '{print $3}' | grep -v '^_' | sort | tr '\n' ' ')
check "exports exactly JNI_OnLoad (no C++ names leak out)" '[ "$(nm -D --defined-only "$W/libmain.so" | wc -l)" = 1 ] && [[ "$syms" == "JNI_OnLoad " ]]'
echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
