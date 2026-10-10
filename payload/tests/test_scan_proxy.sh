#!/bin/bash
# PC test of the whole "Scan game code" path inside the payload: the scan thread finds the (pretend) libil2cpp.so
# in the process, reads it, writes the facts file and reports back to the menu. In this test build the scan starts by
# itself after ~2 s (a person would press the button). Pretend game only - proves nothing about the real one.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/aim_link.cpp" "$HERE/../src/aim_bank.cpp" "$HERE/../src/bank.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" \
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
#include <cstring>
#include <fstream>
#include <string>
static unsigned char* gPlayer = nullptr;
static void show(const char* tag) {
  if (!gPlayer) return;
  float fwd, jump, gy; std::memcpy(&fwd, gPlayer + 684, 4); std::memcpy(&jump, gPlayer + 732, 4); std::memcpy(&gy, gPlayer + 528, 4);
  std::printf("%s fwd=%g jump=%g gy=%g\n", tag, (double)fwd, (double)jump, (double)gy); std::fflush(stdout);
}
static bool fileHas(const std::string& path, const std::string& prefix) {
  std::ifstream in(path); std::string line;
  while (std::getline(in, line)) if (line.find(prefix) != std::string::npos) return true;
  return false;
}
int main(int, char** argv) {
  if (!dlopen("libOVRPlugin.so", RTLD_NOW)) { std::printf("NOOVR\n"); return 4; }
  if (argv[2][0] != '-') {                                                   // the pretend game runtime (full path, like the real one)
    void* il = dlopen(argv[2], RTLD_NOW);
    if (!il) { std::printf("NOLIB\n"); return 4; }
    void* (*mk)(int) = (void* (*)(int))dlsym(il, "fake_make_locomotion");   // one running copy of the important class, in ordinary memory
    if (mk) mk(0);
    if (getenv("HOST_SHOT_WORLD")) {                                         // stage D7: the pretend game also has rims, a ball and the engine classes
      void (*sw)(int) = (void (*)(int))dlsym(il, "fake_set_shot_world"); if (sw) sw(1);
      void* (*mo)(int, int) = (void* (*)(int, int))dlsym(il, "fake_make_shot_object");
      if (mo) { mo(0, 0); mo(0, 1); mo(1, 0); mo(1, 1); mo(2, 1); mo(7, 0); mo(7, 1); mo(8, 0); mo(9, 0); mo(9, 1); mo(10, 0); mo(11, 0); mo(12, 1); }
    }
    if (getenv("HOST_PLAYER")) {                                             // the headset's player object (the one the movement link looks for)
      void* (*mkp)(int) = (void* (*)(int))dlsym(il, "fake_make_player_locomotion");
      if (mkp) gPlayer = (unsigned char*)mkp(0);
    }
  }
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  // wait (up to 40 s) until the facts file has the wanted last line, then a little more
  std::string path = std::string(getenv("TZ_FACTS_DIR")) + "/timmyzstuff_facts.txt";
  const std::string until = getenv("HOST_WAIT_LINE") ? getenv("HOST_WAIT_LINE") : "--- scan finished";
  bool shownMid = false;
  for (int i = 0; i < 400; ++i) {
    usleep(100000);
    if (!shownMid && gPlayer && fileHas(path, "link: applied speed")) { usleep(300000); show("MID"); shownMid = true; }
    if (fileHas(path, until)) break;
  }
  sleep(1);
  show("END");
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl

echo "== pretend libil2cpp.so is loaded in the game"
out=$(cd "$W" && LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/facts" "$W/host" "$W/libmain.so" "$W/libil2cpp.so" 2>&1)
F="$W/facts/timmyzstuff_facts.txt"
cp -f "$F" /tmp/facts_scan.txt
check "game still starts (65542)" '[[ "$out" == *"RESULT=65542"* ]]'
check "scan was requested and logged" 'grep -q "^--- movement scan requested from the menu" "$F"'
check "scan wrote the game classes into the facts file" 'grep -q "^scan: CLASS Game.PlayerMovement : MonoBehaviour" "$F"'
check "scan wrote fields and methods" 'grep -q "^scan:   field walkSpeed : System.Single @32" "$F" && grep -q "^scan:   method Jump(1)" "$F"'
check "scan wrote a field hit and a type hit" 'grep -q "^scan: field-hit Game.GameSettings.speedMultiplier" "$F" && grep -q "^scan: type-hit Game.PhysicsBody.rb" "$F"'
check "scan finished OK" 'grep -q "^--- scan finished: ok ---" "$F" && grep -q "^scan: DONE" "$F"'
check "the index and the full detail of the important class are written" 'grep -q "^scan: index Game.MobilePlayerLocomotion" "$F" && grep -q "^scan: CLASS Game.MobilePlayerLocomotion" "$F" && grep -q "method SetJumpHeight(1) : System.Void rva=" "$F"'
check "the running copy was found in memory and its values written" 'grep -q "^scan: live Game.MobilePlayerLocomotion #1 size=80" "$F" && grep -q "^scan:   live _maxSpeed = 4.25 " "$F" && grep -q "^scan:   live _jumpHeight = 1.5 " "$F"'
check "the facts header says stage D9" 'head -1 "$F" | grep -q "stage D9"'
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

echo "== a movement switch is turned on (pretend menu), the pretend game has the headset player object"
runscan move TZ_SAMPLE_MS=18000 HOST_PLAYER=1 TZ_TEST_ASK="2.5,2.0,1,40" TZ_TEST_ASK_ON_S=3 TZ_TEST_ASK_OFF_S=9 "HOST_WAIT_LINE=link: the game's own values were put back"
F6="$W/move.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the facts say what the menu asked and what the game gets" 'grep -q "^movement: menu asks speed=ON 2.5x | jump=ON 2.0x | gravity=LOW 40%  ->  game gets speed x2.50, jump height x2.00, gravity x0.60" "$F6"'
check "the link searched, found exactly one active player object and connected" 'grep -q "^link: search #[0-9]*: .* 1 look like the active player .* CONNECTED to 1 object" "$F6"'
check "the link wrote the class and field positions it found" 'grep -q "^link: found the class ShovelTools.PlayerLocomotion (object size 1056)" "$F6" && grep -q "_forwardMaxSpeed@684" "$F6"'
check "the pretend game really got speed x2.5, jump x2.0, gravity x0.6 (2.5*2.5, 1.5*2, -0.9*0.6)" 'grep -q "^MID fwd=6.25 jump=3 gy=-0.54$" <<< "$out_last"'
check "the facts have the read-back of the writes" 'grep -q "^link: applied speed x2.50, jump height x2.00, gravity x0.60. Read-back from the game" "$F6"'
check "switching it off puts the game back exactly (2.5, 1.5, -0.9)" 'grep -q "^END fwd=2.5 jump=1.5 gy=-0.9$" <<< "$out_last"'
check "the link summary lines are written (state + counters)" 'grep -q "^t=[0-9]* link: connected | objects alive 1 of 1 known" "$F6"'
check "the scan (which also ran) still finished OK" 'grep -q "^--- scan finished: ok ---" "$F6"'
check "no memory addresses written with a 0x prefix" '! grep -q "0x[0-9a-f]\{4,\}" "$F6"'
check "the whole file stays small" '[ "$(wc -c < "$F6")" -lt 300000 ]'

echo "== a switch is on but the game has NO active player object"
runscan noplayer TZ_SAMPLE_MS=12000 TZ_TEST_ASK="2.0,1,0,0" TZ_TEST_ASK_ON_S=3 "HOST_WAIT_LINE=link: search #3"
F7="$W/noplayer.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the link looked and said there is no active player yet (not a crash, not a failure)" 'grep -q "^link: search #1: .* 0 look like the active player" "$F7" && ! grep -q "^link: FAILED" "$F7"'
check "the movement line says nothing was changed in the game" 'grep -q "\[game link not connected (yet): nothing changed in the game so far\]" "$F7"'

echo "== no switch is turned on at all: the link must stay completely quiet"
runscan quiet TZ_SAMPLE_MS=9000 HOST_PLAYER=1 "HOST_WAIT_LINE=scan: DONE"
F8="$W/quiet.txt"
check "no link search and no link lines at all" '! grep -q "^link: " "$F8" && ! grep -q " link: " "$F8"'
check "no aimbot line either (the switch was never touched)" '! grep -q "^aimbot: " "$F8"'
check "the pretend player object was not touched" 'grep -q "^END fwd=2.5 jump=1.5 gy=-0.9$" <<< "$out_last"'

echo "== stage D7: the Basketball page's 'Scan ball and hoops' button, through the real path"
runscan shot TZ_SAMPLE_MS=20000 TZ_TEST_SHOT_SCAN=1 HOST_SHOT_WORLD=1 "HOST_WAIT_LINE=--- scan finished"
F9="$W/shot.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the ball and hoops scan was requested and logged" 'grep -q "^--- BALL AND HOOPS scan requested from the menu" "$F9"'
check "it says what it is" 'grep -q "^scan: BALL AND HOOPS scan (stage D7)" "$F9"'
check "the ball, the game's shot assist and the hoop classes are written with their fields" 'grep -q "^scan: CLASS ShovelTools.Basketball : MonoBehaviour" "$F9" && grep -q "^scan:   field _isHeld : System.Boolean @36" "$F9" && grep -q "^scan: CLASS ShovelTools.BasketballShotAssist" "$F9" && grep -q "^scan: CLASS ShovelTools.BasketballGoal" "$F9" && grep -q "^scan: CLASS ShovelTools.GameManager" "$F9"'
check "the long class index and the assembly list are left out of this report" '! grep -q "^scan: index " "$F9" && ! grep -q "^scan: assembly " "$F9" && grep -q "the class-name index is left out" "$F9"'
check "the classes that were asked for by name but are missing are reported in one line" 'grep -q "^scan: classes asked for by name but NOT found in this game: .*BankShotCandidate" "$F9"'
check "the engine functions are looked up and reported" 'grep -q "^scan: engine class UnityEngine.Rigidbody found" "$F9" && grep -q "^scan:   method set_velocity(1) : System.Void rva=" "$F9" && grep -q "^scan: icall UnityEngine.Rigidbody::get_velocity_Injected(UnityEngine.Vector3&) -> found" "$F9"'
check "the running balls, hoops and the game's shot assist were found in memory and their values written" 'grep -q "^scan: live ShovelTools.Basketball #1 size=64" "$F9" && grep -q "^scan:   live _strength = 0.6 " "$F9" && grep -q "^scan: live ShovelTools.BasketballGoal #1" "$F9" && grep -q "^scan:   live _officialMatch = true " "$F9"'
check "the 25-second pause is a test setting: off here (no 'waiting' line)" '! grep -q "^scan: waiting " "$F9"'
check "the scan finished OK and the menu was told" 'grep -q "^--- scan finished: ok ---" "$F9" && grep -q "^scan: DONE" "$F9"'
check "no movement classes in this report" '! grep -q "^scan: CLASS Game.PlayerMovement" "$F9" && ! grep -q "^scan: field-hit" "$F9"'
check "the report is small" '[ "$(wc -c < "$F9")" -lt 100000 ]'
check "no memory addresses written with a 0x prefix" '! grep -q "0x[0-9a-f]\{4,\}" "$F9"'

echo "== stage D7c: the pause before the live search, through the real path (3 s in this test; 25 s in the headset)"
runscan shotwait TZ_SAMPLE_MS=20000 TZ_TEST_SHOT_SCAN=1 HOST_SHOT_WORLD=1 TZ_SHOT_LIVE_DELAY_S=3 "HOST_WAIT_LINE=--- scan finished"
F9W="$W/shotwait.txt"
check "the scan tells the person it is waiting, and why" 'grep -q "^scan: waiting 3 seconds before looking at the running objects, so you can close the menu, pick up a ball and take a shot" "$F9W"'
check "after the pause the live objects are still found and the scan finishes OK" 'grep -q "^scan: live ShovelTools.Basketball #1" "$F9W" && grep -q "^--- scan finished: ok ---" "$F9W"'

echo "== stage D7: the Aimbot switch is on at 23 m, then at Unlimited (pretend menu)"
runscan aim TZ_SAMPLE_MS=7000 TZ_TEST_AIM=23 "HOST_WAIT_LINE=aimbot: menu asks aimbot=ON"
F10="$W/aim.txt"
check "game still starts (65542)" '[[ "$out_last" == *"RESULT=65542"* ]]'
check "the facts say what the Aimbot page asks (stage D8 text)" 'grep -q "^aimbot: menu asks aimbot=ON (Direct), max shot distance=23 m, hold Y to aim=off  \[stage D9: when ON, a throw that the rules accept .*gets a new launch speed; the link reports every throw below\]" "$F10"'
check "the Aimbot does not wake the movement link (no link lines)" '! grep -q "^link: " "$F10" && ! grep -q "^movement: " "$F10"'
runscan aim50 TZ_SAMPLE_MS=7000 TZ_TEST_AIM=50 "HOST_WAIT_LINE=aimbot: menu asks aimbot=ON"
check "at 50 the facts say Unlimited" 'grep -q "^aimbot: menu asks aimbot=ON (Direct), max shot distance=Unlimited, hold Y to aim=off " "$W/aim50.txt"'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
