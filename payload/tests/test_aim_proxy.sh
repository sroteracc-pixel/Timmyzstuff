#!/bin/bash
# PC test of the WHOLE Aimbot path inside the payload (stage D8): the menu switch -> the controller doorway (called by a thread named "UnityMain",
# like the game does) -> the aim link -> the pretend game's ball -> the facts file. Everything is the real payload code; only the game is pretend
# (fake_il2cpp_aim.cpp: a pretend libil2cpp with a pretend ball, hoops and physics). Proves the WIRING, not the real game.
# Three runs at the same time (each ~16 s): aimbot ON at Unlimited, ON with a 5 m limit (the shot is 9 m away), and OFF.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/aim_link.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" \
  -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
echo 'extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { return 0x00010006; }' > "$W/orig.cpp"
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp_aim.cpp" -o "$W/libil2cpp.so" || exit 2

cat > "$W/ovr.cpp" <<'C'
extern "C" int ovrp_GetInitialized() { return 1; }
extern "C" int ovrp_GetControllerState4(unsigned int, void* out) { unsigned char* p = (unsigned char*)out; for (int i = 0; i < 96; ++i) p[i] = 0; return 0; }
extern "C" long ovrp_EndFrame4(int a, long b, int c, long d) { return a + b + c + d; }
extern "C" long ovrp_BeginFrame4(long a, long b) { return a + b; }
extern "C" long ovrp_WaitToBeginFrame(long a) { return a; }
C
g++ -shared -fPIC "$W/ovr.cpp" -o "$W/libOVRPlugin.so"
cat > "$W/plug.cpp" <<'C'
extern "C" __attribute__((visibility("default"))) void* tz_table[8];
void* tz_table[8];
C
g++ -shared -fPIC "$W/plug.cpp" -o "$W/libOculusXRPlugin.so"

# The host plays the game: the thread called "UnityMain" calls the controller doorway about 185 times a second and steps the pretend physics
# in real time. After HOST_RELEASE_S seconds the player lets go of the ball (a weak throw, 6 degrees off, from about 9 m).
cat > "$W/host.cpp" <<'C'
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int, char** argv) {
  void* ovr = dlopen("libOVRPlugin.so", RTLD_NOW);
  void* plug = dlopen(argv[2], RTLD_NOW);
  void* il = dlopen(argv[3], RTLD_NOW);                    // full path, like the real game's
  if (!ovr || !plug || !il) { std::printf("NOLIB\n"); return 4; }
  void (*create)(int) = (void (*)(int))dlsym(il, "fake_aim_create");
  void (*hold)(double, double, double) = (void (*)(double, double, double))dlsym(il, "fake_aim_hold");
  void (*release)(int, double, double, double) = (void (*)(int, double, double, double))dlsym(il, "fake_aim_release");
  void (*step)() = (void (*)())dlsym(il, "fake_aim_step");
  void (*state)(double*) = (void (*)(double*))dlsym(il, "fake_aim_state");
  double (*fdt)() = (double (*)())dlsym(il, "fake_aim_fdt");
  create(1);
  const double fx = atof(getenv("HOST_FROM_X")), fz = atof(getenv("HOST_FROM_Z"));
  hold(fx, 1.6, fz);
  void** t = (void**)malloc(64 * sizeof(void*));           // the plugin keeps its table on the HEAP
  t[0] = dlsym(ovr, "ovrp_EndFrame4"); t[1] = dlsym(ovr, "ovrp_BeginFrame4"); t[2] = dlsym(ovr, "ovrp_WaitToBeginFrame");
  t[3] = dlsym(ovr, "ovrp_GetControllerState4");
  void* h = dlopen(argv[1], RTLD_NOW);
  int (*f)(void*, void*) = (int(*)(void*, void*))dlsym(h, "JNI_OnLoad");
  std::printf("RESULT=%d\n", f(nullptr, nullptr));
  pthread_setname_np(pthread_self(), "UnityMain");         // from now on this thread is "the game's script thread"
  const double t0 = now(), releaseAt = atof(getenv("HOST_RELEASE_S")), runFor = atof(getenv("HOST_RUN_S"));
  double last = t0, acc = 0; bool released = false; long calls = 0, bad = 0;
  while (now() - t0 < runFor) {
    const double n = now(); acc += n - last; last = n;
    while (acc >= fdt()) { step(); acc -= fdt(); }
    if (!released && n - t0 >= releaseAt) {
      released = true;
      const double tx = 0, tz = 12.66, e = 55 * 3.14159265358979 / 180.0, speed = 9.0;
      const double ang = std::atan2(tx - fx, tz - fz) + 6.0 * 3.14159265358979 / 180.0;
      release(2, std::sin(ang) * speed * std::cos(e), speed * std::sin(e), std::cos(ang) * speed * std::cos(e));
    }
    unsigned char st[128]; for (int k = 0; k < 128; ++k) st[k] = 0xFF;
    if (((int(*)(unsigned, void*))t[3])(3u, st) != 0) ++bad;      // the game must always get the real answer back
    if (st[4] != 0 || st[100] != 0xFF) ++bad;
    ++calls;
    usleep(5000);
  }
  usleep(500000);
  double s[10]; state(s);
  std::printf("STATE invokes=%.0f setvel=%.0f made=%.0f calls=%ld bad=%ld pos=%.2f,%.2f,%.2f\n", s[6], s[7], s[8], calls, bad, s[0], s[1], s[2]);
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl -pthread

run() {   # run <name> <TZ_TEST_AIM or ""> <from x> <from z>
  mkdir -p "$W/$1"
  ( cd "$W" && env LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/$1" TZ_SAMPLE_MS=30000 ${2:+TZ_TEST_AIM=$2} HOST_FROM_X="$3" HOST_FROM_Z="$4" HOST_RELEASE_S=11 HOST_RUN_S=19 \
      "$W/host" "$W/libmain.so" "$W/libOculusXRPlugin.so" "$W/libil2cpp.so" > "$W/$1.out" 2>&1 ) &
}
run on 50 2 4          # Unlimited; the shot starts about 9 m from the north hoop
run cap5 5 2 4         # 5 m limit: the same shot is farther than that, so the throw must be left alone
run off "" 2 4         # the switch never turns on
wait

echo "== aimbot ON, Unlimited: the real payload aims the pretend ball"
O="$W/on.out"; F="$W/on/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_on.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the controller doorway is installed" 'grep -q "input block: ovrp_GetControllerState4 doorway installed in 1 heap slot" "$F"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "the link connected by asking the game for your ball control" 'grep -q "by asking the game" "$F"'
check "the facts say what the menu asks (stage D8 text)" 'grep -q "^aimbot: menu asks aimbot=ON, max shot distance=Unlimited  \[stage D8:" "$F"'
check "one shot was decided and aimed" 'grep -q "decision: AIM" "$F"'
check "the pretend game's ball speed was set exactly once" 'grep -q " setvel=1 " "$O"'
check "the shot went in (the game's own _shotMade says yes)" 'grep -q " made=1 " "$O" && grep -q "_shotMade: YES" "$F"'
check "the report says the game kept our speed" 'grep -q "our speed was kept by the game" "$F"'
check "the summary line says AIMED 1 and scored 1" 'grep -q "^t=[0-9]* aim: link connected.*AIMED 1 (scored 1, missed 0" "$F"'
check "a summary line is written again after the shot (so the numbers are in the file)" '[ "$(grep -c "^t=[0-9]* aim: link" "$F")" -ge 3 ]'
check "no memory addresses written (no 0x)" '! grep -q "0x" "$F"'
check "the movement link was not woken by the Aimbot" '! grep -q "^link: " "$F" && ! grep -q "^movement: " "$F"'

echo "== aimbot ON with a 5 m limit: a 9 m shot is left alone"
O="$W/cap5.out"; F="$W/cap5/timmyzstuff_facts.txt"
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the link connected" 'grep -q "by asking the game" "$F"'
check "the throw was NOT changed (no speed set)" 'grep -q " setvel=0 " "$O"'
check "the decision says too far" 'grep -qi "too far" "$F"'
check "the summary says too far 1 and aimed 0" 'grep -q "AIMED 0 .*too far 1" "$F"'

echo "== aimbot OFF: the payload does not touch the game at all"
O="$W/off.out"; F="$W/off/timmyzstuff_facts.txt"
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "not a single call into the pretend game" 'grep -q " invokes=0 " "$O" && grep -q " setvel=0 " "$O"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "nothing about the aimbot link in the facts" '! grep -q "by asking the game" "$F" && ! grep -q "decision:" "$F"'
check "the ball flew on its own path (it was not aimed)" 'grep -q " made=0 " "$O"'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
