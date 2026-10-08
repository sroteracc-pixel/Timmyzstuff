#!/bin/bash
# PC test of the facts probe using a FAKE libOVRPlugin.so and a FAKE original library.
# Does NOT prove anything about the real game or the real OVRPlugin - that needs your Quest.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -I"$HERE/fake_jni" -DTZ_FAST_TEST "$HERE/../src/proxy.cpp" -ldl -pthread -o "$W/libmain.so" || exit 2
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
  unsigned int connected = mask; std::memcpy(p, &connected, 4);
  if (calls > 10) { unsigned int b = 0x1; std::memcpy(p + 4, &b, 4); float t = 1.0f; std::memcpy(p + 16, &t, 4); }
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
  sleep(7);
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
check "lists libOVRPlugin.so as loaded" 'grep -q "libOVRPlugin.so is loaded" "$F"'
check "reports the controller function as found" 'grep -q "function ovrp_GetControllerState4: found" "$F"'
check "reports a missing function as MISSING" 'grep -q "function ovrp_SetupLayer: MISSING" "$F"'
check "records the first controller sample" 'grep -qE "rc=0 bytes=03000000" "$F"'
check "records the later change (button + trigger)" 'grep -qE "bytes=030000000100000000000000000000000000803f" "$F"'
check "never writes memory addresses (no 0x)" '! grep -q "0x" "$F"'
check "finishes cleanly" 'grep -q "^done:" "$F"'
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
check "still exports exactly JNI_OnLoad" '[[ "$syms" == "JNI_OnLoad " ]]'
echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
