#!/bin/bash
# PC test of the whole "Scan game code" path inside the payload: the scan thread finds the (pretend) libil2cpp.so
# in the process, reads it, writes the facts file and reports back to the menu. In this test build the scan starts by
# itself after ~2 s (a person would press the button). Pretend game only - proves nothing about the real one.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" \
  -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
echo 'extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { return 0x00010006; }' > "$W/orig.cpp"
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp.cpp" -o "$W/libil2cpp.so" || exit 2
# a minimal fake Meta library, so the payload's main loop (which only runs in a VR game) starts
cat > "$W/ovr.cpp" <<'C'
#include <cstring>
extern "C" int ovrp_GetInitialized() { return 1; }
extern "C" int ovrp_GetControllerState4(unsigned int, void* out) { std::memset(out, 0, 200); return 0; }
extern "C" int ovrp_GetNodePoseState3(int, int, int, void* out) { if (!out) return -1001; std::memset(out, 0, 88); return 0; }
C
g++ -shared -fPIC "$W/ovr.cpp" -o "$W/libOVRPlugin.so"
mkdir -p "$W/facts"

cat > "$W/host.cpp" <<'C'
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
int main(int, char** argv) {
  if (!dlopen("libOVRPlugin.so", RTLD_NOW)) { std::printf("NOOVR\n"); return 4; }
  if (argv[2][0] != '-') {                                                   // the pretend game runtime (full path, like the real one)
    void* il = dlopen(argv[2], RTLD_NOW);
    if (!il) { std::printf("NOLIB\n"); return 4; }
    void* (*mk)(int) = (void* (*)(int))dlsym(il, "fake_make_locomotion");   // one running copy of the important class, in ordinary memory
    if (mk) mk(0);
  }
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  // wait (up to 30 s) until the scan has written its last line, then a little more
  std::string path = std::string(getenv("TZ_FACTS_DIR")) + "/timmyzstuff_facts.txt";
  for (int i = 0; i < 300; ++i) {
    usleep(100000);
    std::ifstream in(path); std::string line; bool done = false;
    while (std::getline(in, line)) if (line.rfind("--- scan finished", 0) == 0) { done = true; break; }
    if (done) break;
  }
  sleep(1);
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl

echo "== pretend libil2cpp.so is loaded in the game"
out=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host" "$W/libmain.so" "$W/libil2cpp.so" 2>&1)
F="$W/facts/timmyzstuff_facts.txt"
cp -f "$F" /tmp/facts_scan.txt
check "game still starts (65542)" '[[ "$out" == *"RESULT=65542"* ]]'
check "scan was requested and logged" 'grep -q "^--- scan requested from the menu" "$F"'
check "scan wrote the game classes into the facts file" 'grep -q "^scan: CLASS Game.PlayerMovement : MonoBehaviour" "$F"'
check "scan wrote fields and methods" 'grep -q "^scan:   field walkSpeed : System.Single @32" "$F" && grep -q "^scan:   method Jump(1)" "$F"'
check "scan wrote a field hit and a type hit" 'grep -q "^scan: field-hit Game.GameSettings.speedMultiplier" "$F" && grep -q "^scan: type-hit Game.PhysicsBody.rb" "$F"'
check "scan finished OK" 'grep -q "^--- scan finished: ok ---" "$F" && grep -q "^scan: DONE" "$F"'
check "the index and the full detail of the important class are written" 'grep -q "^scan: index Game.MobilePlayerLocomotion" "$F" && grep -q "^scan: CLASS Game.MobilePlayerLocomotion" "$F" && grep -q "method SetJumpHeight(1) : System.Void rva=" "$F"'
check "the running copy was found in memory and its values written" 'grep -q "^scan: live Game.MobilePlayerLocomotion #1 size=80" "$F" && grep -q "^scan:   live _maxSpeed = 4.25 " "$F" && grep -q "^scan:   live _jumpHeight = 1.5 " "$F"'
check "the facts header says stage D5c" 'head -1 "$F" | grep -q "stage D5c"'
check "no memory addresses written (no 0x)" '! grep -q "0x" "$F"'
check "progress lines for every step are written" 'grep -q "^scan: step 3 of 6" "$F" && grep -q "^scan: step 4 done" "$F" && grep -q "^scan: step 5 done: 4 class" "$F" && grep -q "^scan: step 6 of 6" "$F"'
check "the memory search line tells the pipe size and trouble count" 'grep -q "^scan: memory search read .* copy pipe=[0-9]* bytes (chunk [0-9]* KB); .*copy trouble=0" "$F"'
check "normal run does not say stuck" '! grep -q "STUCK\|GAVE UP" "$F"'

runscan() {            # runscan <name> VAR=value ...   -> facts copied to $W/<name>.txt
  local name="$1"; shift
  rm -f "$W/facts/timmyzstuff_facts.txt"
  out_last=$(cd "$W" && env LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$@" "$W/host" "$W/libmain.so" "$W/libil2cpp.so" 2>&1)
  cp -f "$W/facts/timmyzstuff_facts.txt" "$W/$name.txt"; cp -f "$W/$name.txt" "/tmp/facts_scan_$name.txt"
}

echo "== BRIEF scan through the real button path (what the headset runs)"
runscan brief TZ_SAMPLE_MS=20000 TZ_SCAN_BRIEF=1
F5="$W/brief.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "brief scan finishes ok" 'grep -q "^--- scan finished: ok ---" "$F5" && grep -q "^scan: DONE" "$F5"'
check "brief scan leaves out the index and the single hits" '! grep -q "^scan: index " "$F5" && ! grep -q "^scan: field-hit" "$F5" && grep -q "^scan: BRIEF scan" "$F5"'
check "brief scan writes the headset class in full and looks for it in memory" 'grep -q "^scan: CLASS ShovelTools.PlayerLocomotion" "$F5" && grep -q "^scan: step 5 done: .*ShovelTools.PlayerLocomotion" "$F5"'
check "brief scan output is small" '[ "$(wc -c < "$F5")" -lt 60000 ]'

echo "== the copy pipe is only ONE page (4 KB): the real headset hung like this with the old code"
runscan tinypipe TZ_SAMPLE_MS=20000 TZ_SCAN_TEST_PIPE_BYTES=4096
F2="$W/tinypipe.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "pipe is reported as 4096 bytes / 4 KB chunks" 'grep -q "copy pipe=4096 bytes (chunk 4 KB)" "$F2"'
check "scan still finishes and finds the running copy" 'grep -q "^--- scan finished: ok ---" "$F2" && grep -q "^scan:   live _maxSpeed = 4.25 " "$F2"'
check "no stuck message" '! grep -q "STUCK" "$F2"'

echo "== the memory read freezes after 3 chunks (a read that never returns)"
runscan hang TZ_SAMPLE_MS=20000 TZ_SCAN_TEST_HANG_CHUNKS=3 TZ_SCAN_TEST_STALL_S=2
F3="$W/hang.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the class lists were written before" 'grep -q "^scan: CLASS Game.MobilePlayerLocomotion" "$F3"'
check "the stuck search is reported with the region" 'grep -q "^scan: MEMORY SEARCH STUCK and given up on (region [0-9]* of [0-9]*: [0-9]* KB, " "$F3"'
check "the scan still ends: DONE line + finished ok" 'grep -q "^scan: DONE\..*(live search stuck)" "$F3" && grep -q "^--- scan finished: ok ---" "$F3"'
check "progress / stuck lines contain no 0x" '! grep -q "0x" "$F3"'

echo "== the game's runtime never answers in step 5 (top-level watchdog)"
runscan runtime TZ_SAMPLE_MS=20000 FAKE_IL2CPP_HANG_STEP5=1 TZ_SCAN_DEADLINE_S=4
F4="$W/runtime.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the watchdog names the step it is stuck in" 'grep -q "^scan: GAVE UP waiting after [0-9]* s: the scan is still in step \"5 of 6: planning the live search\"" "$F4"'
check "the menu is told it FAILED (not left on Scanning)" 'grep -q "^--- scan finished: FAILED ---" "$F4"'

echo "== libil2cpp.so is NOT in the game"
rm -f "$F"
out2=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host" "$W/libmain.so" - 2>&1)
check "game still starts (65542)" '[[ "$out2" == *"RESULT=65542"* ]]'
check "says plainly that there is nothing to read" 'grep -q "scan: libil2cpp.so is not loaded in the game" "$F"'
check "the scan still ends (reports FAILED to the menu), no crash" 'grep -q "^--- scan finished: FAILED ---" "$F"'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
