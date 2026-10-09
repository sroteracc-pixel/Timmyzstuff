#!/bin/bash
# PC test of the Stage D1 frame watcher. A FAKE "libOculusXRPlugin.so" keeps a table of function
# addresses (like the real plugin does) and calls through it every 50 ms. We check that the watcher
# swaps the table entries, counts the calls, and that every call still reaches the REAL function with
# the same arguments and gives back the same answer. Does NOT prove anything about the real plugin.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../../menu/src/menu_input.cpp" \
  -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
cat > "$W/orig.cpp" <<'C'
extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { return 0x00010006; }
C
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"

# fake Meta library: the three frame functions, each counting its own calls
cat > "$W/ovr.cpp" <<'C'
static long endCalls = 0, beginCalls = 0, waitCalls = 0;
extern "C" int ovrp_GetInitialized() { return 1; }
extern "C" int ovrp_GetControllerState4(unsigned int, void* out) { unsigned char* p = (unsigned char*)out; for (int i = 0; i < 64; ++i) p[i] = 0; return 0; }
extern "C" long ovrp_EndFrame4(long a, long b, long c, long d, long e, long f, long g, long h) { ++endCalls; return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8; }
extern "C" long ovrp_BeginFrame4(long a, long b) { ++beginCalls; return a + b; }
extern "C" long ovrp_WaitToBeginFrame(long a) { ++waitCalls; return a * 10; }
extern "C" long fake_end_calls() { return endCalls; }
extern "C" long fake_begin_calls() { return beginCalls; }
C
g++ -shared -fPIC "$W/ovr.cpp" -o "$W/libOVRPlugin.so"

# fake plugin: just a table of saved addresses (zero-filled = lives in its writable memory)
cat > "$W/plug.cpp" <<'C'
extern "C" __attribute__((visibility("default"))) void* tz_table[8];
void* tz_table[8];
C
g++ -shared -fPIC "$W/plug.cpp" -o "$W/libOculusXRPlugin.so"

cat > "$W/host.cpp" <<'C'
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
int main(int, char** argv) {
  void* ovr = dlopen("libOVRPlugin.so", RTLD_NOW);
  void* plug = dlopen(argv[2], RTLD_NOW);                 // full path, so the maps line names it
  if (!ovr || !plug) { std::printf("NOLIB\n"); return 4; }
  void** t = (void**)dlsym(plug, "tz_table");
  t[0] = dlsym(ovr, "ovrp_EndFrame4"); t[1] = dlsym(ovr, "ovrp_BeginFrame4"); t[2] = dlsym(ovr, "ovrp_WaitToBeginFrame");
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  long hostEnd = 0, bad = 0;
  for (int i = 0; i < 160; ++i) {                          // ~8 seconds of "frames"
    long r = ((long(*)(long,long,long,long,long,long,long,long))t[0])(i, 2, 3, 4, 5, 6, 7, 8);
    if (r != i + 4 + 9 + 16 + 25 + 36 + 49 + 64) ++bad;    // must equal what the real function returns
    ++hostEnd;
    if (((long(*)(long,long))t[1])(i, 1) != i + 1) ++bad;
    if (((long(*)(long))t[2])(i) != i * 10) ++bad;
    usleep(50 * 1000);
  }
  long (*ec)() = (long(*)())dlsym(ovr, "fake_end_calls");
  std::printf("HOST_END=%ld REAL_END=%ld BAD=%ld\n", hostEnd, ec(), bad);
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl
mkdir -p "$W/facts"
out=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host" "$W/libmain.so" "$W/libOculusXRPlugin.so" 2>&1)
F="$W/facts/timmyzstuff_facts.txt"
echo "$out" | grep -E "RESULT|HOST_END"

echo "== frame watcher on"
check "game's JNI_OnLoad result is unchanged (65542)" '[[ "$out" == *"RESULT=65542"* ]]'
check "all 3 watches reported installed in 1 slot each" '[ "$(grep -c "installed in 1 slot" "$F")" = 3 ]'
check "no memory addresses written (no 0x)" '! grep -q "0x" "$F"'
check "every call still reached the REAL function with the right answer (BAD=0)" '[[ "$out" == *"BAD=0"* ]]'
check "real function saw every call the host made" '( h=$(echo "$out" | sed -n "s/.*HOST_END=\([0-9]*\) REAL_END=\([0-9]*\).*/\1 \2/p"); set -- $h; [ "$1" = "$2" ] && [ "$1" -gt 100 ] )'
check "watcher counted calls (EndFrame4 count > 0 in the file)" 'grep -E "frame watch ovrp_EndFrame4: installed, [1-9][0-9]* calls" "$F" >/dev/null'
check "frames-per-second line is written" 'grep -q "^frames t=" "$F"'
check "latest args are recorded as small numbers" 'grep -q "EndFrame4 latest args: [0-9]* 2 3 4 5 6 7 8" "$F"'
check "still exports exactly JNI_OnLoad" '[ "$(nm -D --defined-only "$W/libmain.so" | wc -l)" = 1 ]'

echo "== plugin never saves the addresses"
rm -f "$F"
cat > "$W/host3.cpp" <<'C'
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
int main(int, char** argv) {
  void* ovr = dlopen("libOVRPlugin.so", RTLD_NOW); void* plug = dlopen(argv[2], RTLD_NOW);
  if (!ovr || !plug) return 4;
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  sleep(9);
  return 0;
}
C
g++ -o "$W/host3" "$W/host3.cpp" -ldl
out3=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host3" "$W/libmain.so" "$W/libOculusXRPlugin.so" 2>&1)
check "game still starts normally" '[[ "$out3" == *"RESULT=65542"* ]]'
check "honestly says NOT installed" 'grep -q "NOT installed" "$F"'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
