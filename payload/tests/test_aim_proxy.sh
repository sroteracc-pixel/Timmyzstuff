#!/bin/bash
# PC test of the WHOLE Aimbot path inside the payload (stage D8): the menu switch -> the controller doorway (called by a thread named "UnityMain",
# like the game does) -> the aim link -> the pretend game's ball -> the facts file. Everything is the real payload code; only the game is pretend
# (fake_il2cpp_aim.cpp: a pretend libil2cpp with a pretend ball, hoops and physics). Proves the WIRING, not the real game.
# Seven runs at the same time (each ~20 s): aimbot ON at Unlimited, ON with a 5 m limit (the shot is 9 m away), OFF,
# and (stage D8c) "Hold Y to aim" ON with a pretend Y button: seen only by the game's thread / only by the menu's thread / never held / held only AFTER the shot.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }

g++ -std=c++17 -shared -fPIC -fvisibility=hidden -I"$HERE/fake_jni" -I"$HERE/../src" -I"$HERE/../../menu/src" -DTZ_FAST_TEST \
  "$HERE/../src/proxy.cpp" "$HERE/../src/frame_stubs.cpp" "$HERE/../src/overlay.cpp" "$HERE/../src/pointer.cpp" "$HERE/../src/movement.cpp" "$HERE/../src/game_link.cpp" "$HERE/../src/aimbot.cpp" "$HERE/../src/aim_link.cpp" "$HERE/../src/aim_bank.cpp" "$HERE/../src/aim_points.cpp" "$HERE/../src/aim_hitbox.cpp" "$HERE/../src/bank.cpp" "$HERE/../src/il2cpp_scan.cpp" "$HERE/../src/panel.cpp" "$HERE/../src/panel_font.cpp" "$HERE/../../menu/src/menu_input.cpp" \
  -ldl -pthread -Wl,--version-script="$HERE/../exports.map" -o "$W/libmain.so" || exit 2
echo 'extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void*, void*) { return 0x00010006; }' > "$W/orig.cpp"
g++ -shared -fPIC "$W/orig.cpp" -o "$W/libmain_orig.so"
g++ -std=c++17 -shared -fPIC "$HERE/fake_il2cpp_aim.cpp" -o "$W/libil2cpp.so" || exit 2

cat > "$W/ovr.cpp" <<'C'
#include <sys/prctl.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static double nowS() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static const double t0 = nowS();
extern "C" int ovrp_GetInitialized() { return 1; }
// The pretend Touch controllers. TZ_PRETEND_Y = "who,from,to": the Y button (0x200 in the buttons number at +4) is held between `from` and `to` seconds after
// this library was loaded, and only for `who` = game (the thread called UnityMain, i.e. the controller doorway's caller), probe (every other thread) or both.
extern "C" int ovrp_GetControllerState4(unsigned int mask, void* out) {
  unsigned char* p = (unsigned char*)out; for (int i = 0; i < 96; ++i) p[i] = 0;
  if (const char* e = getenv("TZ_PRETEND_Y")) {
    char who[16] = {0}; double a = 0, b = 0;
    if (sscanf(e, "%15[a-z],%lf,%lf", who, &a, &b) == 3 && (mask & 1u)) {
      char name[32] = {0}; prctl(PR_GET_NAME, (unsigned long)name, 0, 0, 0);
      const bool isGame = strncmp(name, "UnityMain", 15) == 0;
      const bool seen = !strcmp(who, "both") || (!strcmp(who, "game") && isGame) || (!strcmp(who, "probe") && !isGame);
      const double t = nowS() - t0;
      if (seen && t >= a && t <= b) { unsigned int y = 0x200u; memcpy(p + 4, &y, 4); }
    }
  }
  return 0;
}
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
  void (*setk)(const char*, double, double, double) = (void (*)(const char*, double, double, double))dlsym(il, "fake_aim_set");
  create(1);
  if (getenv("HOST_VSTATE")) setk("vstate", atof(getenv("HOST_VSTATE")), 0, 0);      // the pretend player's vertical state: 0 = on the floor, 1 = jumping (report only since stage D8c)
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
  double last = t0, acc = 0; bool released = false; long calls = 0, bad = 0, yseen = 0;
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
    if (st[5] == 0x02) ++yseen; else if (st[5] != 0) ++bad;      // the only button the pretend controller ever reports is Y (0x200 = byte 5, value 2): the game must see it unchanged
    ++calls;
    usleep(5000);
  }
  usleep(500000);
  double s[10]; state(s);
  std::printf("STATE invokes=%.0f setvel=%.0f made=%.0f calls=%ld bad=%ld ygame=%ld pos=%.2f,%.2f,%.2f\n", s[6], s[7], s[8], calls, bad, yseen, s[0], s[1], s[2]);
  void (*pstate)(double*) = (void (*)(double*))dlsym(il, "fake_pts_state");      // stage D10: the pretend scoreboard and the ball's point value
  double p[8] = {0}; if (pstate) pstate(p);
  std::printf("PTS score=%.0f baskets=%.0f value=%.0f\n", p[0], p[1], p[2]);
  {   // stage D11b: how big the pretend hitboxes and grab-reach values are at the end (value now / the game's own value), and whether the game's own display function was (wrongly) called
    void (*hcol)(int, double*) = (void (*)(int, double*))dlsym(il, "fake_hb_col");
    void (*hst)(double*) = (void (*)(double*))dlsym(il, "fake_hb_state");
    void (*htw)(int, double*) = (void (*)(int, double*))dlsym(il, "fake_hb_tw");
    double c0[8] = {0}, c5[8] = {0}, c10[8] = {0}, c4[8] = {0}, hs[12] = {0}, w0[13] = {0}, w1[13] = {0}, w2[13] = {0};
    if (hcol) { hcol(0, c0); hcol(5, c5); hcol(10, c10); hcol(4, c4); }
    if (hst) hst(hs);
    if (htw) { htw(0, w0); htw(1, w1); htw(2, w2); }
    auto rt = [](const double* w, int now, int own) { return w[own] > 0 ? w[now] / w[own] : -1.0; };
    std::printf("HB mine_left=%.2f mine_right=%.2f other=%.2f mesh=%.2f sets=%.0f vizcalls=%.0f grab=%.2f,%.2f,%.2f gscale=%.2f,%.2f,%.2f gdist=%.2f,%.2f,%.2f\n", c0[4] > 0 ? c0[1] / c0[4] : -1, c5[4] > 0 ? c5[1] / c5[4] : -1, c10[4] > 0 ? c10[1] / c10[4] : -1,
                c4[4] > 0 ? c4[1] / c4[4] : 1.0, hs[0], hs[2], rt(w0, 0, 6), rt(w1, 0, 6), rt(w2, 0, 6), rt(w0, 3, 9), rt(w1, 3, 9), rt(w2, 3, 9), rt(w0, 2, 8), rt(w1, 2, 8), rt(w2, 2, 8));
  }
  return 0;
}
C
g++ -o "$W/host" "$W/host.cpp" -ldl -pthread

run() {   # run <name> <TZ_TEST_AIM or ""> <from x> <from z> [extra VAR=value ...]
  local name="$1" aim="$2" fx="$3" fz="$4"; shift 4
  mkdir -p "$W/$name"
  ( cd "$W" && env LD_LIBRARY_PATH="$W" TZ_FACTS_DIR="$W/$name" TZ_SAMPLE_MS=30000 ${aim:+TZ_TEST_AIM=$aim} HOST_FROM_X="$fx" HOST_FROM_Z="$fz" HOST_RELEASE_S=11 HOST_RUN_S=19 "$@" \
      "$W/host" "$W/libmain.so" "$W/libOculusXRPlugin.so" "$W/libil2cpp.so" > "$W/$name.out" 2>&1 ) &
}
run on 50 2 4          # Unlimited; the shot starts about 9 m from the north hoop
run cap5 5 2 4         # 5 m limit: the same shot is farther than that, so the throw must be left alone
run off "" 2 4         # the switch never turns on
run ygame 50 2 4 TZ_TEST_AIM_Y=1 TZ_PRETEND_Y=game,6,19 HOST_VSTATE=0      # "hold Y to aim" ON; Y is held from 6 s on, but only the game's thread can see it (the player stands on the floor: that no longer matters)
run yprobe 50 2 4 TZ_TEST_AIM_Y=1 TZ_PRETEND_Y=probe,6,19 HOST_VSTATE=0    # ... only the menu's thread can see it
run ynot 50 2 4 TZ_TEST_AIM_Y=1 HOST_VSTATE=1                                # "hold Y to aim" ON; Y is never pressed (the player is in the air: that no longer matters)
run ylate 50 2 4 TZ_TEST_AIM_Y=1 TZ_PRETEND_Y=both,13,19                    # "hold Y to aim" ON; Y is pressed only 2 s AFTER the shot
run bank 50 0 6 TZ_TEST_AIM_BANK=1                                           # stage D9: Aimbot Bank ON (the Direct Aimbot is off), a weak crooked throw from 6.7 m in front of the hoop
run bankno 50 -2.5 12.0 TZ_TEST_AIM_BANK=1                                   # Aimbot Bank ON, but the player is 1 m from the board: "Bank unavailable", the throw must stay exactly as thrown
run bankynot 50 0 6 TZ_TEST_AIM_BANK=1 TZ_TEST_AIM_Y=1                       # Aimbot Bank ON with "hold Y to aim" ON and Y never pressed: left alone
run pts "" 2 4 TZ_TEST_POINTS=11                                             # stage D10: Shot points ON at 11, the Aimbot never turns on, the same weak crooked throw
run ptsaim 50 2 4 TZ_TEST_POINTS=11                                          # Shot points ON at 11 AND the Direct Aimbot ON: the throw goes in and the pretend scoreboard counts 11
run ptsbig 50 2 4 TZ_TEST_POINTS=999                                         # Shot points at the last stop (999)
run hbx "" 2 4 TZ_TEST_HITBOX=3.0                                             # stage D11: Hitbox expander ON at 3.0x from 3 s on, the Aimbot never turns on
run hbmax "" 2 4 TZ_TEST_HITBOX=10.0                                          # stage D11b: Hitbox expander ON at the top of the slider (10.0x)
run hball 50 2 4 TZ_TEST_HITBOX=2.5 TZ_TEST_POINTS=11                         # stage D11b: everything at once: Aimbot (Direct) + Shot points 11 + Hitbox expander 2.5x
wait

echo "== aimbot ON, Unlimited: the real payload aims the pretend ball"
O="$W/on.out"; F="$W/on/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_on.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the controller doorway is installed" 'grep -q "input block: ovrp_GetControllerState4 doorway installed in 1 heap slot" "$F"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "the link connected by asking the game for your ball control" 'grep -q "by asking the game" "$F"'
check "the facts say what the menu asks (stage D8 text)" 'grep -q "^aimbot: menu asks aimbot=ON (Direct), max shot distance=Unlimited, hold Y to aim=off  \[stage D9:" "$F"'
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

echo "== 'Hold Y to aim' ON, Y held, seen only by the game's thread (the controller doorway)"
O="$W/ygame.out"; F="$W/ygame/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_ygame.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the game always got the real controller answer, including the Y button (BAD=0, Y seen by the game)" 'grep -q "bad=0" "$O" && ! grep -q "ygame=0 " "$O"'
check "the facts say what the menu asks (hold Y ON)" 'grep -q "^aimbot: menu asks aimbot=ON (Direct), max shot distance=Unlimited, hold Y to aim=ON  \[stage D9:" "$F"'
check "the shot was aimed and went in" 'grep -q " setvel=1 " "$O" && grep -q " made=1 " "$O" && grep -q "_shotMade: YES" "$F"'
check "the report says Y was held" 'grep -q "Y button: HELD when you let go" "$F" && grep -q "hold Y to aim: ON" "$F"'
check "the report still shows the jump state (floor) - it no longer decides anything" 'grep -q "vertical state 0 = ON THE FLOOR" "$F"'
check "the summary says AIMED 1, Y not held 0, and the doorway saw Y held" 'grep -q "AIMED 1 .*Y not held 0" "$F" && grep -qE "doorway [0-9]+ \(held in [1-9][0-9]*\)" "$F"'
check "the menu's own thread saw NO Y (only the game's thread was given it)" 'grep -q "menu thread [0-9]* (held in 0)" "$F"'

echo "== 'Hold Y to aim' ON, Y held, seen only by the menu's thread"
O="$W/yprobe.out"; F="$W/yprobe/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_yprobe.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the shot was aimed and went in" 'grep -q " setvel=1 " "$O" && grep -q " made=1 " "$O"'
check "the report says Y was held" 'grep -q "Y button: HELD when you let go" "$F"'
check "the summary shows the menu thread saw Y and the doorway did not" 'grep -qE "menu thread [0-9]+ \(held in [1-9][0-9]*\)" "$F" && grep -q "doorway [0-9]* (held in 0)" "$F"'

echo "== 'Hold Y to aim' ON, Y never pressed: the throw is left alone"
O="$W/ynot.out"; F="$W/ynot/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_ynot.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the throw was NOT changed (no speed set)" 'grep -q " setvel=0 " "$O"'
check "the report says Y was not held" 'grep -q "Y button: NOT held" "$F" && grep -q "the Y button was not held when you let go" "$F"'
check "the summary says Y not held 1, aimed 0" 'grep -q "AIMED 0 .*Y not held 1" "$F"'
check "the player being in the air did not make it aim" 'grep -q "vertical state 1 = IN THE AIR" "$F"'

echo "== 'Hold Y to aim' ON, Y pressed only 2 s after the shot: the throw is left alone"
O="$W/ylate.out"; F="$W/ylate/timmyzstuff_facts.txt"
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the throw was NOT changed (no speed set)" 'grep -q " setvel=0 " "$O"'
check "the summary says Y not held 1, aimed 0" 'grep -q "AIMED 0 .*Y not held 1" "$F"'

echo "== stage D9: Aimbot Bank ON: the real payload makes a bank shot with the pretend ball"
O="$W/bank.out"; F="$W/bank/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_aim_bank.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "the facts header says stage D11b" 'grep -q "^Timmyzstuff facts (stage D11b" "$F"'
check "the facts say what the menu asks: BANK mode" 'grep -q "^aimbot: menu asks aimbot=ON (BANK), max shot distance=Unlimited, hold Y to aim=off  \[stage D9:" "$F"'
check "the bank code found the game's backboard code" 'grep -q "aim: BANK: found the game.s backboard code" "$F"'
check "the backboards were measured" 'grep -q "aim: BANK: measured the backboard of the goal with its ring at" "$F"'
check "a bank shot was decided, and NO direct shot" 'grep -q "decision: BANK SHOT" "$F" && ! grep -q "decision: AIM" "$F"'
check "the pretend game's ball speed was set exactly once" 'grep -q " setvel=1 " "$O"'
check "the ball went in after the board (the game's _shotMade says yes)" 'grep -q " made=1 " "$O" && grep -q "_shotMade: YES" "$F"'
check "the report measured the bounce and says the timing model holds" 'grep -q "MEASURED BOUNCE" "$F" && grep -q "the timing model holds" "$F"'
check "the summary says mode BANK, BANK shots 1 (scored 1)" 'grep -q "mode BANK | BANK shots 1 (scored 1, missed 0), Bank unavailable 0" "$F"'
check "no memory addresses written (no 0x)" '! grep -q "0x" "$F"'
check "the movement link was not woken by the Aimbot" '! grep -q "^link: " "$F" && ! grep -q "^movement: " "$F"'

echo "== stage D9: Aimbot Bank ON, 1 m from the backboard: 'Bank unavailable', the throw is left alone"
O="$W/bankno.out"; F="$W/bankno/timmyzstuff_facts.txt"
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the throw was NOT changed (no speed set)" 'grep -q " setvel=0 " "$O"'
check "the decision says Bank unavailable and why" 'grep -q "decision: LEFT ALONE - Bank unavailable: too close to the backboard" "$F"'
check "it says the throw is left exactly as thrown (no direct shot)" 'grep -q "the throw is left exactly as you threw it (no direct shot)" "$F" && ! grep -q "decision: AIM" "$F"'
check "the summary counts it: BANK shots 0, Bank unavailable 1" 'grep -q "BANK shots 0 (scored 0, missed 0), Bank unavailable 1" "$F"'

echo "== stage D9: Aimbot Bank ON + 'Hold Y to aim' ON, Y never pressed: left alone"
O="$W/bankynot.out"; F="$W/bankynot/timmyzstuff_facts.txt"
check "the throw was NOT changed (no speed set)" 'grep -q " setvel=0 " "$O"'
check "the report says Y was not held (not 'Bank unavailable')" 'grep -q "the Y button was not held when you let go" "$F" && ! grep -q "Bank unavailable:" "$F"'

echo "== stage D10: Shot points ON alone (the Aimbot never turns on)"
O="$W/pts.out"; F="$W/pts/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_points.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "the facts say what the menu asks: Shot points ON at 11" 'grep -q "^points: menu asks Shot points=ON (11 points per basket)" "$F"'
check "the points part found where the ball keeps its point value" 'grep -q "^points: found where the ball keeps its point value" "$F"'
check "the points part connected and is kept at 11" 'grep -q "^t=[0-9]* points: link connected | asked 11 |" "$F"'
check "the game put its own number into the ball when the throw was released, and the report says so" 'grep -q "^points: the game put its own number 3 into the ball.s point value" "$F"'
check "the ball carries 11 at the end of the run (the game wrote 3 at the release)" 'grep -q "^PTS score=0 baskets=0 value=11" "$O"'
check "the Aimbot did NOT wake up: no aim switch line, no speed set, no decision" '! grep -q "^aim: switch turned ON" "$F" && grep -q " setvel=0 " "$O" && ! grep -q "decision:" "$F"'
check "the movement link was not woken either" '! grep -q "^link: " "$F" && ! grep -q "^movement: " "$F"'
check "no memory addresses written except the short ball ids in points lines (no 0x)" '! grep -q "0x" "$F"'
echo "== stage D10: Shot points + the Direct Aimbot: the throw goes in and the pretend scoreboard counts 11"
O="$W/ptsaim.out"; F="$W/ptsaim/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_points_aim.txt
check "the throw was aimed (speed set once) and went in" 'grep -q " setvel=1 " "$O" && grep -q " made=1 " "$O"'
check "the pretend scoreboard counted 11 points (not the game's 3)" 'grep -q "^PTS score=11 baskets=1 value=11" "$O"'
check "the facts file has the basket line" 'grep -q "^points: BASKET on ball .*the ball.s point value was 11" "$F"'
check "the points summary counts one basket" 'grep -q "points: link .* | asked 11 | .*baskets seen 1," "$F"'
echo "== stage D10: Shot points at the last stop (999)"
O="$W/ptsbig.out"; F="$W/ptsbig/timmyzstuff_facts.txt"
check "the pretend scoreboard counted 999 points" 'grep -q "^PTS score=999 baskets=1 value=999" "$O"'
echo "== stage D11: Hitbox expander ON at 3.0x alone (the Aimbot and Shot points never turn on)"
O="$W/hbx.out"; F="$W/hbx/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_hitbox.txt
check "the game still starts (65542)" 'grep -q "RESULT=65542" "$O"'
check "the game always got the real controller answer (BAD=0)" 'grep -q "bad=0" "$O"'
check "the facts say what the menu asks: Hitbox expander ON at 3.0x" 'grep -q "^hitbox: Hitbox expander turned ON at 3.0x" "$F"'
check "the hitbox part found where the hands keep their hitboxes" 'grep -q "^hitbox: found where your hands keep their hitboxes" "$F"'
check "it found both hands and 10 hitboxes (8 can be resized)" 'grep -q "^hitbox: your hands have 10 hitboxes (left hand 5, right hand 5): 8 can be resized" "$F"'
check "it set 8 hitboxes to 3.0x" 'grep -q "^hitbox: set 8 hitboxes of your hands to 3.0x their own size" "$F"'
check "at the end both of YOUR hands are 3.0 times the game size; the other player's hand and the mesh hitboxes are exactly the game size" 'grep -q "^HB mine_left=3.00 mine_right=3.00 other=1.00 mesh=1.00 " "$O"'
check "the summary line says connected, 3.0x, hands 2, hitboxes 10" 'grep -q "hitbox: link connected | asked: expander 3.0x | hands 2, hitboxes 10 (can be resized 8" "$F"'
check "it set 8 grab-reach values (reach, palm radius, gravity distance, grab volume on both hands) to 3.0x" 'grep -q "^hitbox: set 8 grab-reach values of your hands to 3.0x their own value" "$F" && grep -q "^hitbox: GRAB-REACH values found" "$F"'
check "at the end the grab reach, the grab volume and the gravity distance of YOUR hands are 3.0x, the other player's hand 1.0x" 'grep -q " grab=3.00,3.00,1.00 gscale=3.00,3.00,1.00 gdist=3.00,3.00,1.00$" "$O"'
check "the game's own display function was never called" 'grep -q " vizcalls=0 " "$O"'
check "the Aimbot did NOT wake up: no aim switch line, no speed set, no decision" '! grep -q "^aim: switch turned ON" "$F" && grep -q " setvel=0 " "$O" && ! grep -q "decision:" "$F"'
check "Shot points did not wake up either" '! grep -q "points: switch turned ON" "$F"'
check "the movement link was not woken" '! grep -q "^link: " "$F" && ! grep -q "^movement: " "$F"'
check "no memory addresses (0x1234...) in the facts file (sizes like 3.0x are fine)" '! grep -q "0x[0-9a-f]" "$F"'
echo "== stage D11b: Hitbox expander ON at the top of the slider (10.0x)"
O="$W/hbmax.out"; F="$W/hbmax/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_hitbox_max.txt
check "the game still starts (65542) and got the real answers (BAD=0)" 'grep -q "RESULT=65542" "$O" && grep -q "bad=0" "$O"'
check "the facts say Hitbox expander ON at 10.0x" 'grep -q "^hitbox: Hitbox expander turned ON at 10.0x" "$F"'
check "hitboxes and grab-reach values of YOUR hands are exactly 10 times the game's own; the other hand untouched" 'grep -q "^HB mine_left=10.00 mine_right=10.00 other=1.00 mesh=1.00 " "$O" && grep -q " grab=10.00,10.00,1.00 gscale=10.00,10.00,1.00 gdist=10.00,10.00,1.00$" "$O"'
echo "== stage D11b: everything at once: Aimbot (Direct) + Shot points 11 + Hitbox expander 2.5x"
O="$W/hball.out"; F="$W/hball/timmyzstuff_facts.txt"; cp -f "$F" /tmp/facts_hitbox_all.txt
check "the game still starts (65542) and got the real answers (BAD=0)" 'grep -q "RESULT=65542" "$O" && grep -q "bad=0" "$O"'
check "the throw was aimed once and went in" 'grep -q " setvel=1 " "$O" && grep -q " made=1 " "$O"'
check "the pretend scoreboard counted 11 points" 'grep -q "^PTS score=11 baskets=1 value=11" "$O"'
check "the hitboxes ended at 2.5x (mine), the other hand and the meshes untouched" 'grep -q "^HB mine_left=2.50 mine_right=2.50 other=1.00 mesh=1.00 " "$O"'
check "the grab-reach values ended at 2.5x on your hands too" 'grep -q " grab=2.50,2.50,1.00 gscale=2.50,2.50,1.00 gdist=2.50,2.50,1.00$" "$O"'
check "the facts file has the lines of all three parts" 'grep -q "^hitbox: set 8 hitboxes of your hands to 2.5x" "$F" && grep -q "^points: BASKET on ball" "$F" && grep -q "decision: AIM" "$F"'
echo "== the other runs never touched the points or the hitboxes"
for n in on bank pts; do check "run '$n': no hitbox activity and the pretend hitboxes are exactly the game's size" '! grep -q "hitbox: Hitbox expander turned ON\|hitbox: set [0-9]" "$W/$n/timmyzstuff_facts.txt" && grep -q "^HB mine_left=1.00 mine_right=1.00 other=1.00 mesh=1.00 sets=0 vizcalls=0 grab=1.00,1.00,1.00 gscale=1.00,1.00,1.00 gdist=1.00,1.00,1.00$" "$W/$n.out"'; done
echo "== the other runs never touched the points"
check "Aimbot only: the scoreboard counted the game's own 3 (shot aimed) and there is no points activity" 'grep -q "^PTS score=3 baskets=1 value=3" "$W/on.out" || grep -q "^PTS score=0 baskets=0 value=3" "$W/on.out"; ! grep -q "points: switch turned ON" "$W/on/timmyzstuff_facts.txt"'
check "Bank only: no points activity" '! grep -q "points: switch turned ON" "$W/bank/timmyzstuff_facts.txt"'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
