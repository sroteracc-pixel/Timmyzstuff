// PC test of the "Hitbox expander" part of the game link (aim_hitbox.cpp, stage D11b) against the PRETEND game (fake_il2cpp_aim.cpp).
// The pretend game has two hands of YOURS (reached from your ball control manager) and one hand of ANOTHER player. Every hand has five hitboxes: a box, a sphere,
// two capsules and a mesh (a kind whose size cannot be changed). Index 0-4 = your left hand, 5-9 = your right hand, 10-14 = the other player's hand.
// Every hand also has four GRAB-REACH values (Hand.reachDistance, Hand.palmRadius, BallControl._gravityDistance, BallControl._grabVolume's local scale).
// It proves the logic (find the hands, scale from the game's own values, put them back, survive a game that changes things) and that the part leaves everything else alone.
// It does NOT prove that the REAL game's grab uses these numbers, that other players see them, or that very big hands feel good.
#include <dlfcn.h>
#include <unistd.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aim_link.h"
#include "aimbot.h"

using namespace tzaimlink;

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)

static void* gLib = nullptr;
static void (*fake_create)(int) = nullptr;
static void (*fake_hold)(double, double, double) = nullptr;
static void (*fake_release)(int, double, double, double) = nullptr;
static void (*fake_step)() = nullptr;
static void (*fake_state)(double*) = nullptr;
static double (*fake_fdt)() = nullptr;
static void (*pts_state)(double*) = nullptr;
static void (*hb_set)(const char*, double, double) = nullptr;
static void (*hb_state)(double*) = nullptr;
static void (*hb_col)(int, double*) = nullptr;
static double (*hb_reach)(int) = nullptr;
static void (*hb_hide)(const char*, const char*) = nullptr;
static void (*hb_field)(const char*, const char*, int) = nullptr;

static bool provider(tzscan::Api* api, std::string* why) { return tzscan::loadApi(gLib, api, why); }
static std::mutex gNoteMu;
static std::vector<std::string> gNotes;
static void note(const char* f, ...) {
    char b[4200]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap);
    std::lock_guard<std::mutex> lock(gNoteMu); gNotes.push_back(b);
    if (std::getenv("AIMTEST_DUMP")) std::printf("      NOTE: %s\n", b);
}
static int countNotes(const char* needle) { std::lock_guard<std::mutex> lock(gNoteMu); int n = 0; for (const auto& l : gNotes) if (l.find(needle) != std::string::npos) ++n; return n; }
static std::string findNote(const char* needle) { std::lock_guard<std::mutex> lock(gNoteMu); for (const auto& l : gNotes) if (l.find(needle) != std::string::npos) return l; return ""; }
static int countPrefix(const char* prefix) { std::lock_guard<std::mutex> lock(gNoteMu); int n = 0; for (const auto& l : gNotes) if (l.compare(0, std::strlen(prefix), prefix) == 0) ++n; return n; }
static void clearNotes() { std::lock_guard<std::mutex> lock(gNoteMu); gNotes.clear(); }

static double gClock = 1000.0;
static double fakeClock() { return gClock; }

struct Rig {
    std::unique_ptr<AimLink> link;
    double physAcc = 0;
    Config cfg;
    explicit Rig(const char* threadName = "") {
        fake_create(1);
        clearNotes();
        link.reset(new AimLink());
        cfg.provider = provider; cfg.note = note; cfg.linkTickMs = 2; cfg.gameThreadName = threadName; cfg.clock = fakeClock; cfg.allowWrite = true;
        cfg.searchSeconds = 20; cfg.stallSeconds = 5; cfg.retrySoonSeconds = 0.05; cfg.minSearchGapSeconds = 0.05;
    }
    bool start() { return link->start(cfg); }
    void tick() {
        const double dt = 1.0 / 185.0;
        gClock += dt; physAcc += dt;
        const double fdt = fake_fdt();
        while (physAcc >= fdt) { fake_step(); physAcc -= fdt; }
        link->onGameThread();
        usleep(120);
    }
    void run(double seconds) { const int n = static_cast<int>(seconds * 185.0); for (int i = 0; i < n; ++i) tick(); }
    // run until the hitbox part is connected (state 1) or has failed (state 3); bounded
    bool connectHb(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->hitboxUiState() == 1) return true; if (link->hitboxUiState() == 3) return false; }
        return false;
    }
    bool connectAim(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->uiState() == 1) return true; if (link->uiState() == 3) return false; }
        return false;
    }
    bool connectPts(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->pointsUiState() == 1) return true; if (link->pointsUiState() == 3) return false; }
        return false;
    }
    // run until a state other than "looking" (2) shows up, or time is up
    int waitState(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); const int s = link->hitboxUiState(); if (s == 1 || s == 3) return s; }
        return link->hitboxUiState();
    }
    void settle(double seconds) { run(seconds); usleep(30000); }
};

// ---- reading the pretend hands
static int kindOf(int i) { double c[8]; hb_col(i, c); return static_cast<int>(c[0]); }
static int compsOf(int kind) { return kind == 1 ? 3 : (kind == 3 ? 2 : (kind == 2 ? 1 : 0)); }
static bool nearD(double a, double b) { return std::fabs(a - b) <= 1e-5 + 2e-4 * std::fabs(b); }
// does hitbox i have exactly `mul` times its own size (only the numbers its kind has)?
static bool isScaled(int i, double mul) {
    double c[8]; hb_col(i, c);
    const int n = compsOf(static_cast<int>(c[0]));
    for (int j = 0; j < n; ++j) if (!nearD(c[1 + j], c[4 + j] * mul)) return false;
    return true;
}
// is hitbox i exactly at the game's own size?
static bool isOwn(int i) { return isScaled(i, 1.0); }
static const int kMine[8] = {0, 1, 2, 3, 5, 6, 7, 8};       // your resizable hitboxes
static bool mineScaled(double mul) { for (int i : kMine) if (!isScaled(i, mul)) return false; return true; }
static bool meshesOwn() { return isOwn(4) && isOwn(9); }
static bool otherOwn() { for (int i = 10; i < 15; ++i) if (!isOwn(i)) return false; return true; }
static bool allOwn() { for (int i = 0; i < 15; ++i) if (!isOwn(i)) return false; return true; }
static bool noneAlive(int a, int b) { for (int i = a; i < b; ++i) { double c[8]; hb_col(i, c); if (c[7] != 0) return false; } return true; }
static double hbAt(int i) { double s[12]; hb_state(s); return s[i]; }
static long sets() { return static_cast<long>(hbAt(0)); }
static long gets() { return static_cast<long>(hbAt(1)); }
static long vizCalls() { return static_cast<long>(hbAt(2)); }
static double reachOf(int hand) { return hb_reach(hand); }
static double gameAt(int i) { double s[10]; fake_state(s); return s[i]; }
static double score() { double s[8]; pts_state(s); return s[0]; }
// ---- the grab-reach values: 0 reachDistance, 1 palmRadius, 2 _gravityDistance, 3-5 grab volume scale now; 6-11 the game's own; 12 alive
static void (*hb_tw)(int, double*) = nullptr;
static void (*hb_twset)(int, int, double, int) = nullptr;
static void (*hb_tfcounts)(double*) = nullptr;
static bool twScaled(int hand, double mul) {
    double t[13]; hb_tw(hand, t);
    for (int j = 0; j < 6; ++j) if (!nearD(t[j], t[6 + j] * mul)) return false;
    return true;
}
static bool twOwn(int hand) { return twScaled(hand, 1.0); }
static bool mineTw(double mul) { return twScaled(0, mul) && twScaled(1, mul); }
static bool otherTw() { return twOwn(2); }
static double twAt(int hand, int j) { double t[13]; hb_tw(hand, t); return t[j]; }
static long tfGets() { double c[2]; hb_tfcounts(c); return static_cast<long>(c[0]); }
static long tfSets() { double c[2]; hb_tfcounts(c); return static_cast<long>(c[1]); }
static bool everythingOwn() { return allOwn() && twOwn(0) && twOwn(1) && twOwn(2); }

static void throwFrom(double fx, double fy, double fz, double tx, double tz, double elevDeg, double speed, double offDeg, int hand = 2) {
    const double dx = tx - fx, dz = tz - fz;
    double ang = std::atan2(dx, dz) + offDeg * 3.14159265358979 / 180.0;
    const double e = elevDeg * 3.14159265358979 / 180.0;
    fake_hold(fx, fy, fz);
    fake_release(hand, std::sin(ang) * speed * std::cos(e), speed * std::sin(e), std::cos(ang) * speed * std::cos(e));
}
static void weakThrow() { throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0); }

int main(int, char** argv) {
    gLib = dlopen(argv[1], RTLD_NOW);
    if (!gLib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    fake_create = reinterpret_cast<void (*)(int)>(dlsym(gLib, "fake_aim_create"));
    fake_hold = reinterpret_cast<void (*)(double, double, double)>(dlsym(gLib, "fake_aim_hold"));
    fake_release = reinterpret_cast<void (*)(int, double, double, double)>(dlsym(gLib, "fake_aim_release"));
    fake_step = reinterpret_cast<void (*)()>(dlsym(gLib, "fake_aim_step"));
    fake_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_aim_state"));
    fake_fdt = reinterpret_cast<double (*)()>(dlsym(gLib, "fake_aim_fdt"));
    pts_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_pts_state"));
    hb_set = reinterpret_cast<void (*)(const char*, double, double)>(dlsym(gLib, "fake_hb_set"));
    hb_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_hb_state"));
    hb_col = reinterpret_cast<void (*)(int, double*)>(dlsym(gLib, "fake_hb_col"));
    hb_reach = reinterpret_cast<double (*)(int)>(dlsym(gLib, "fake_hb_reach"));
    hb_hide = reinterpret_cast<void (*)(const char*, const char*)>(dlsym(gLib, "fake_hb_hide"));
    hb_field = reinterpret_cast<void (*)(const char*, const char*, int)>(dlsym(gLib, "fake_hb_field"));
    hb_tw = reinterpret_cast<void (*)(int, double*)>(dlsym(gLib, "fake_hb_tw"));
    hb_twset = reinterpret_cast<void (*)(int, int, double, int)>(dlsym(gLib, "fake_hb_twset"));
    hb_tfcounts = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_hb_tfcounts"));
    if (!hb_tw || !hb_twset || !hb_tfcounts) { std::printf("the pretend runtime lacks the grab-reach functions\n"); return 2; }
    if (!hb_set || !hb_state || !hb_col || !hb_reach || !hb_hide || !hb_field || !pts_state) { std::printf("the pretend runtime lacks the hitbox functions\n"); return 2; }

    std::printf("== the pretend world itself (so the checks below mean something)\n");
    {
        Rig r;
        CHECK("15 hitboxes: kinds box, sphere, capsule, capsule, mesh for each of the three hands", kindOf(0) == 1 && kindOf(1) == 2 && kindOf(2) == 3 && kindOf(3) == 3 && kindOf(4) == 4 && kindOf(5) == 1 && kindOf(9) == 4 && kindOf(10) == 1 && kindOf(14) == 4);
        CHECK("all start at the game's own size", allOwn());
        CHECK("the other player's hand is 3 times bigger than yours", reachOf(2) > 2.9 * reachOf(0) && reachOf(2) < 3.1 * reachOf(0));
        CHECK("every hand has its grab-reach values, at the game's own numbers (0.20, 0.05, 0.07, scale 0.10)", everythingOwn() && nearD(twAt(0, 0), 0.20) && nearD(twAt(0, 1), 0.05) && nearD(twAt(0, 2), 0.07) && nearD(twAt(0, 3), 0.10) && nearD(twAt(2, 0), 0.60));
    }

    std::printf("== every switch is off: the hitbox part touches nothing, and says nothing\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 3.0f);
        r.run(1.5);
        CHECK("not a single engine call about hitboxes or the grab volume", sets() == 0 && gets() == 0 && vizCalls() == 0 && tfGets() == 0 && tfSets() == 0 && gameAt(6) == 0);
        CHECK("every hitbox and every grab-reach value is at the game's own number", everythingOwn());
        CHECK("menu state is 'off' and the headline is empty", r.link->hitboxUiState() == 0 && r.link->hitboxHeadline().empty());
        CHECK("no hitbox lines in the facts file", countPrefix("hitbox:") == 0);
        CHECK("the counters are all zero", r.link->hitboxCounters().resized == 0 && r.link->hitboxCounters().censuses == 0 && r.link->hitboxCounters().hands == 0 && r.link->hitboxCounters().grabSet == 0);
        CHECK("the link is not even running for it (the Aimbot and Shot points are off too)", r.link->uiState() == 0);
        r.link->stop();
    }

    std::printf("== Hitbox expander ON at 2.0x (Aimbot and Shot points off): your hands get bigger AND reach further, nothing else does\n");
    {
        Rig r; const double reach0 = reachOf(0); r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("the hitbox part connects (state 1)", ok && r.link->hitboxUiState() == 1);
        CHECK("the headline says 2 hands, 8 hitboxes and 8 grab values made 2.0x bigger", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes + 8 grab values made 2.0x bigger");
        CHECK("all 8 resizable hitboxes of YOUR two hands are exactly 2.0 times the game's own size", mineScaled(2.0));
        CHECK("the two mesh hitboxes (size cannot be changed) are untouched", meshesOwn());
        CHECK("the OTHER player's hand is untouched (hitboxes and grab-reach values)", otherOwn() && otherTw());
        CHECK("your hands now reach twice as far (both of them), the other player's hand not", reach0 > 0 && nearD(reachOf(0), 2.0 * reach0) && nearD(reachOf(1), 2.0 * reach0) && nearD(reachOf(2), 3.0 * reach0));
        CHECK("Hand.reachDistance, Hand.palmRadius, BallControl._gravityDistance and the grab volume's scale are exactly 2.0x on both of your hands", mineTw(2.0));
        CHECK("... as real numbers: reach 0.40, palm radius 0.10, gravity distance 0.14, grab volume scale 0.20", nearD(twAt(0, 0), 0.40) && nearD(twAt(1, 1), 0.10) && nearD(twAt(0, 2), 0.14) && nearD(twAt(1, 3), 0.20) && nearD(twAt(1, 5), 0.20));
        CHECK("the Aimbot is NOT on (state off) and no throw was touched", r.link->uiState() == 0 && gameAt(7) == 0);
        CHECK("the game's own display function was never called (See hitbox is gone)", vizCalls() == 0);
        CHECK("the facts file says where the hands keep their hitboxes", countNotes("hitbox: found where your hands keep their hitboxes") == 1 && findNote("hitbox: found where").find("_handColliders@144") != std::string::npos);
        CHECK("the facts file lists the 4 grab-reach values it found, with their places", findNote("hitbox: GRAB-REACH values found").find("Hand.reachDistance@92(Hand), Hand.palmRadius@96(Hand), BallControl._gravityDistance@168(BallControl), BallControl._grabVolume@152(BallControl)") != std::string::npos);
        CHECK("... and the way the Transform scale is read", findNote("hitbox: GRAB-REACH values found").find("Transform scale call: game call") != std::string::npos);
        CHECK("the facts file says the switch went on, at 2.0x", countNotes("hitbox: Hitbox expander turned ON at 2.0x") == 1);
        CHECK("the facts file says it found both hands and counts 10 hitboxes, 8 resizable", countNotes("hitbox: found your hands") == 1 && findNote("hitbox: your hands have 10 hitboxes").find("8 can be resized (box 2, sphere 2, capsule 4), mesh 2, other kinds 0") != std::string::npos);
        CHECK("... and names the first hitboxes with their sizes", findNote("hitbox: your hands have").find("BoxCollider") != std::string::npos && findNote("hitbox: your hands have").find("size (0.080, 0.020, 0.100)") != std::string::npos);
        CHECK("the facts file says what the game's own grab-reach values were, for each hand", findNote("hitbox: the game's own grab-reach values").find("left hand: Hand.reachDistance 0.200, Hand.palmRadius 0.050, BallControl._gravityDistance 0.070, BallControl._grabVolume scale (0.100, 0.100, 0.100) | right hand: Hand.reachDistance 0.200") != std::string::npos);
        CHECK("the facts file says 8 hitboxes and 8 grab-reach values were set to 2.0x", countNotes("hitbox: set 8 hitboxes of your hands to 2.0x their own size") == 1 && countNotes("hitbox: set 8 grab-reach values of your hands to 2.0x their own value") == 1);
        CHECK("the counters: 8 resized, no failures, no game resets", r.link->hitboxCounters().resized == 8 && r.link->hitboxCounters().readFails == 0 && r.link->hitboxCounters().writeFails == 0 && r.link->hitboxCounters().gameResets == 0);
        CHECK("the grab counters: 8 values can be changed, 8 set, none failed, the game did not change any", r.link->hitboxCounters().grabValues == 8 && r.link->hitboxCounters().grabSet == 8 && r.link->hitboxCounters().grabWriteFails == 0 && r.link->hitboxCounters().grabReadFails == 0 && r.link->hitboxCounters().grabResets == 0);
        CHECK("it counts hands 2, hitboxes 10, resizable 8 (box 2, sphere 2, capsule 4, mesh 2)", r.link->hitboxCounters().hands == 2 && r.link->hitboxCounters().hitboxes == 10 && r.link->hitboxCounters().resizable == 8 &&
              r.link->hitboxCounters().boxes == 2 && r.link->hitboxCounters().spheres == 2 && r.link->hitboxCounters().capsules == 4 && r.link->hitboxCounters().meshes == 2);
        const long setsNow = sets(), tfNow = tfSets();
        r.run(3.0);
        CHECK("it does not keep writing: no new size changes in 3 more seconds", sets() == setsNow && tfSets() == tfNow && mineScaled(2.0) && mineTw(2.0));
        CHECK("it does not read all the time either (a few reads per second at most)", gets() < 400 && tfGets() < 400);
        CHECK("the summary line for the facts file has the state, the asked size and the counts", r.link->hitboxSummary().find("hitbox: link connected | asked: expander 2.0x | hands 2, hitboxes 10 (can be resized 8: box 2, sphere 2, capsule 4; other kinds 0, mesh 2)") == 0);
        CHECK("... and the grab-reach numbers", r.link->hitboxSummary().find("GRAB-REACH values: 8 can be changed, set 8 times, put back 0 times") != std::string::npos);
        r.link->stop();
    }

    std::printf("== the slider's stops: 1.0 ... 10.0 in steps of 0.5 (and out-of-range values are pulled in)\n");
    {
        bool allOk = true; double badStop = 0;
        for (int st = 10; st <= 100; st += 5) {
            Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, static_cast<float>(st) / 10.0f);
            r.connectHb(); r.run(0.4);
            const double mul = st / 10.0;
            const bool ok = mineScaled(mul) && mineTw(mul) && meshesOwn() && otherOwn() && otherTw() && r.link->hitboxUiState() == 1;
            if (!ok) { allOk = false; badStop = mul; }
            r.link->stop();
        }
        CHECK("1.0x, 1.5x, 2.0x ... 10.0x (19 stops) each give exactly that many times the game's own size AND grab-reach values", allOk);
        if (!allOk) std::printf("      first bad stop: %.1f\n", badStop);
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 1.0f); r.connectHb(); r.run(1.0);
          CHECK("1.0x is the normal size: nothing was changed, not one set call", mineScaled(1.0) && mineTw(1.0) && sets() == 0 && tfSets() == 0);
          CHECK("... and the headline says so", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes + 8 grab values at the normal size (1.0x)");
          r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 12.0f); r.connectHb(); r.run(0.4);
          CHECK("12.0 is pulled in to 10.0x", mineScaled(10.0) && mineTw(10.0)); r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 0.2f); r.connectHb(); r.run(0.4);
          CHECK("0.2 is pulled up to 1.0x (never smaller than normal)", mineScaled(1.0) && mineTw(1.0) && sets() == 0); r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, std::nanf("")); r.connectHb(); r.run(0.4);
          CHECK("a number that is not a number becomes 1.0x", mineScaled(1.0) && mineTw(1.0) && sets() == 0); r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 10.0f); r.connectHb(); r.run(0.4);
          CHECK("at 10.0x: the grab reach is 2.0 m, palm radius 0.5, gravity distance 0.7 m, grab volume scale 1.0", nearD(twAt(0, 0), 2.0) && nearD(twAt(0, 1), 0.5) && nearD(twAt(0, 2), 0.7) && nearD(twAt(0, 3), 1.0));
          CHECK("... and the headline says 10.0x", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes + 8 grab values made 10.0x bigger"); r.link->stop(); }
    }

    std::printf("== moving the slider while it is on: always from the game's own size, never on top of the last change\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        r.connectHb(); r.run(0.4);
        CHECK("2.0x first", mineScaled(2.0) && mineTw(2.0));
        r.link->setHitbox(true, 3.5f); r.run(0.5);
        CHECK("then 3.5x: exactly 3.5 times the game's own value (not 7x)", mineScaled(3.5) && mineTw(3.5));
        r.link->setHitbox(true, 10.0f); r.run(0.5);
        CHECK("then 10.0x", mineScaled(10.0) && mineTw(10.0));
        r.link->setHitbox(true, 1.5f); r.run(0.5);
        CHECK("then 1.5x (it can shrink again)", mineScaled(1.5) && mineTw(1.5));
        r.link->setHitbox(true, 1.0f); r.run(0.5);
        CHECK("then 1.0x: back to exactly the game's own values", mineScaled(1.0) && allOwn() && everythingOwn());
        CHECK("... and the headline says 'at the normal size'", r.link->hitboxHeadline().find("at the normal size (1.0x)") != std::string::npos);
        CHECK("... and the counters know 8 hitboxes and 8 grab values were put back", r.link->hitboxCounters().restored == 8 && r.link->hitboxCounters().grabRestored == 8);
        const long s0 = sets();
        r.link->setHitbox(true, 2.0f); r.run(0.5);
        CHECK("2.0x again works after a trip through 1.0x", mineScaled(2.0) && mineTw(2.0) && sets() > s0);
        for (int i = 0; i < 40; ++i) { r.link->setHitbox(true, 1.0f + (i % 9) * 0.5f); r.run(0.02); }
        r.link->setHitbox(true, 4.0f); r.run(0.6);
        CHECK("wiggling the slider 40 times quickly ends exactly at the last value (4.0x)", mineScaled(4.0) && mineTw(4.0) && meshesOwn() && otherOwn() && otherTw());
        r.link->stop();
    }

    std::printf("== the switch goes OFF: the game's own values are put back exactly\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 4.0f);
        r.connectHb(); r.run(0.4);
        CHECK("4.0x is on", mineScaled(4.0) && mineTw(4.0));
        r.link->setHitbox(false, 4.0f); r.run(0.5);
        CHECK("switch off: every hitbox and every grab-reach value is exactly the game's own again", allOwn() && everythingOwn());
        CHECK("the menu state is off again and the headline is empty", r.link->hitboxUiState() == 0 && r.link->hitboxHeadline().empty());
        CHECK("the facts file says it switched off and put 8 hitboxes and 8 grab-reach values back", countNotes("hitbox: switched off") == 1 && findNote("hitbox: switched off").find("8 hitboxes and 8 grab-reach values put back to the game's own") != std::string::npos);
        CHECK("the counters: 8 resized, 8 put back; 8 grab values set, 8 put back", r.link->hitboxCounters().resized == 8 && r.link->hitboxCounters().restored == 8 && r.link->hitboxCounters().grabSet == 8 && r.link->hitboxCounters().grabRestored == 8);
        const long s0 = sets(), g0 = gets(), t0 = tfGets(), t1 = tfSets();
        r.run(2.0);
        CHECK("after that it does nothing at all (no more reads or writes)", sets() == s0 && gets() == g0 && tfGets() == t0 && tfSets() == t1);
        r.link->setHitbox(true, 3.0f); r.connectHb(); r.run(0.5);
        CHECK("turning it on again works and scales from the same game values (3.0x)", mineScaled(3.0) && mineTw(3.0));
        r.link->stop();
    }

    std::printf("== the game puts the sizes back by itself (a new round, a respawn): the mod notices and sets them again\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f);
        r.connectHb(); r.run(0.4);
        hb_set("game_reset", 0, 0);
        CHECK("(the pretend game just put all hitboxes back to its own size)", allOwn());
        r.run(2.6);
        CHECK("within about two seconds they are 3.0x again", mineScaled(3.0));
        CHECK("the counters and the facts file say the game changed 8 hitboxes (ONE line, not one per hitbox)", r.link->hitboxCounters().gameResets >= 8 && countNotes("hitbox: the game changed 8 of your hitboxes") == 1);
        CHECK("... and that they went back to the game's own size", findNote("hitbox: the game changed").find("(8 back to its own size, 0 to a NEW own size)") != std::string::npos);
        hb_set("game_new_size", 1.5, 0);
        r.run(2.6);
        double c[8]; hb_col(0, c);
        CHECK("the game gave the hitboxes a NEW own size (1.5x bigger): the mod scales from that new size", mineScaled(3.0) && nearD(c[4], 0.08 * 1.5));
        CHECK("... and the facts file says it was a NEW own size", countNotes("(0 back to its own size, 8 to a NEW own size)") == 1);
        r.link->setHitbox(false, 3.0f); r.run(0.5);
        CHECK("switch off: it puts back the NEW own size (not the old one)", allOwn() && nearD(c[4], 0.08 * 1.5));
        r.link->stop();
    }

    std::printf("== the game destroys a hitbox: no crash, the rest keep working\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        r.connectHb(); r.run(0.4);
        hb_set("destroy_col", 1, 0);           // your left hand's sphere
        r.link->setHitbox(true, 3.0f); r.run(2.6);
        double c[8]; hb_col(1, c);
        CHECK("the destroyed hitbox was not written to (still the old size, 2.0x)", isScaled(1, 2.0) && c[7] == 0);
        bool restOk = true; for (int i : kMine) if (i != 1 && !isScaled(i, 3.0)) restOk = false;
        CHECK("all the other 7 hitboxes follow the slider to 3.0x", restOk);
        CHECK("still connected, no engine errors", r.link->hitboxUiState() == 1 && r.link->counters().errors == 0);
        r.link->setHitbox(false, 3.0f); r.run(0.5);
        bool back = true; for (int i : kMine) if (i != 1 && !isOwn(i)) back = false;
        CHECK("switch off: the 7 living ones are back to their own size", back);
        r.link->stop();
    }

    std::printf("== the game builds new hands (new avatar / new scene): the mod finds them\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.5f);
        r.connectHb(); r.run(0.4);
        CHECK("2.5x on the first hands", mineScaled(2.5));
        hb_set("rebuild", 0, 0);
        CHECK("(the pretend game just made new hands with their own sizes)", allOwn());
        r.run(1.0);
        CHECK("the new hands are 2.5x within a second (hitboxes and grab-reach values, each from the new hand's own numbers)", mineScaled(2.5) && mineTw(2.5) && meshesOwn() && otherOwn() && otherTw());
        CHECK("the facts file says it found hands again", countNotes("hitbox: found your hands") >= 2);
        CHECK("still connected with 2 hands", r.link->hitboxUiState() == 1 && r.link->hitboxCounters().hands == 2);
        r.link->setHitbox(false, 2.5f); r.run(0.5);
        CHECK("switch off: the new hands are at their own size and values", allOwn() && everythingOwn());
        r.link->stop();
    }

    std::printf("== a hand disappears from your ball control, or its hitbox list is emptied: what was changed is put back\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f);
        r.connectHb(); r.run(0.4);
        hb_set("unlink", 0, 0);                 // the left hand is no longer linked from your ball control
        r.run(0.6);
        CHECK("the left hand's hitboxes were put back to normal (the mod does not leave them big)", isOwn(0) && isOwn(1) && isOwn(2) && isOwn(3));
        CHECK("... and so were its grab-reach values", twOwn(0));
        CHECK("the right hand is still 3.0x (hitboxes and grab-reach values)", isScaled(5, 3.0) && isScaled(6, 3.0) && isScaled(7, 3.0) && isScaled(8, 3.0) && twScaled(1, 3.0));
        CHECK("the menu says 1 hand", r.link->hitboxCounters().hands == 1 && r.link->hitboxHeadline().find("1 hand,") != std::string::npos && r.link->hitboxUiState() == 1);
        CHECK("the facts file says some hitboxes left the lists", countNotes("left the lists of your hands' hitboxes - put back to the game's own size") >= 1);
        hb_set("relink", 0, 0);
        r.run(0.6);
        CHECK("linked again: the left hand is 3.0x from ITS OWN size (not 9x), the grab-reach values too", mineScaled(3.0) && mineTw(3.0));
        hb_set("arr_null", 1, 0);               // the right hand's list of hitboxes is gone
        r.run(0.6);
        CHECK("the right hand's list is empty: its hitboxes are put back; the left hand stays 3.0x", isOwn(5) && isOwn(6) && isOwn(7) && isOwn(8) && isScaled(0, 3.0) && isScaled(3, 3.0));
        hb_set("arr_back", 1, 0);
        r.run(0.6);
        CHECK("the list is back: 3.0x again on both hands", mineScaled(3.0));
        hb_set("destroy_hand", 1, 0);
        hb_set("destroy_bc", 0, 0);
        r.run(0.8);
        CHECK("both hands destroyed by the game: state is 'looking for your hands', nothing crashes", r.link->hitboxUiState() == 2 && r.link->hitboxHeadline() == "looking for your hands ..." && r.link->hitboxCounters().hands == 0);
        CHECK("the facts file says the hands are gone", countNotes("hitbox: your hands are gone") >= 1);
        r.link->stop();
    }
    std::printf("== a hand that the game says it has no authority over is still handled, and the report shows the flag\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("authority", 0, 0);
        r.connectHb(); r.run(0.4);
        CHECK("2.0x is applied", mineScaled(2.0));
        CHECK("the facts file shows (authority 0) for both hands", findNote("hitbox: found your hands").find("(authority 0)") != std::string::npos);
        r.link->stop();
    }

    std::printf("== the engine refuses to SET a size: the part gives up cleanly after a few tries and says why\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("set_throws", 1, 0);
        r.run(3.0);
        CHECK("the menu shows FAILED (state 3)", r.link->hitboxUiState() == 3);
        CHECK("the headline says why", r.link->hitboxHeadline().find("FAILED:") == 0);
        CHECK("nothing is left changed in the game (what was set before it stopped is put back)", everythingOwn());
        CHECK("the facts file says it stopped, once", countNotes("hitbox: STOPPED") == 1);
        const long s0 = sets();
        r.run(2.0);
        CHECK("it stopped trying (no more set calls)", sets() == s0);
        CHECK("the failures are counted", r.link->hitboxCounters().writeFails >= 1);
        r.link->setHitbox(false, 2.0f); r.run(0.3);
        hb_set("set_throws", 0, 0);
        r.link->setHitbox(true, 2.0f);
        CHECK("switching it off and on again tries again and works", r.connectHb() && (r.run(0.5), mineScaled(2.0) && mineTw(2.0)));
        r.link->stop();
    }
    std::printf("== the engine refuses to READ a size: nothing is changed (the mod does not guess the game's own size)\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("get_throws", 1, 0);
        const int st = r.waitState(); r.run(1.5);
        CHECK("the menu shows FAILED (state 3)", st == 3 && r.link->hitboxUiState() == 3);
        r.run(0.5);
        CHECK("nothing was set, and nothing is left changed", sets() == 0 && everythingOwn());
        CHECK("the read failures are counted", r.link->hitboxCounters().readFails >= 1);
        r.link->stop();
    }

    std::printf("== the game's own set_ calls are missing (stripped): the engine's internal calls are used instead\n");
    {
        Rig r;
        hb_set("icalls", 1, 0);
        hb_hide("BoxCollider", "get_size"); hb_hide("BoxCollider", "set_size");
        hb_hide("SphereCollider", "get_radius"); hb_hide("SphereCollider", "set_radius");
        hb_hide("CapsuleCollider", "get_radius"); hb_hide("CapsuleCollider", "set_radius"); hb_hide("CapsuleCollider", "get_height"); hb_hide("CapsuleCollider", "set_height");
        hb_hide("Transform", "get_localScale"); hb_hide("Transform", "set_localScale");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("connects through the internal calls", ok && r.link->hitboxUiState() == 1);
        CHECK("all 8 hitboxes are 2.0x, and so are the grab-reach values (the grab volume's scale through the engine's internal call)", mineScaled(2.0) && mineTw(2.0) && meshesOwn() && otherOwn() && otherTw() && tfSets() >= 2);
        CHECK("the facts file says the calls are 'engine call'", findNote("hitbox: found where").find("Box size read engine call / set engine call") != std::string::npos && findNote("hitbox: GRAB-REACH values found").find("Transform scale call: engine call") != std::string::npos);
        r.link->setHitbox(false, 2.0f); r.run(0.4);
        CHECK("switch off puts the sizes back through them too", allOwn() && everythingOwn());
        r.link->stop();
    }
    std::printf("== only the setters are missing for one kind (capsules): the other kinds still work\n");
    {
        Rig r;
        hb_hide("CapsuleCollider", "set_height");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("connects", ok);
        CHECK("boxes and spheres are 2.0x", isScaled(0, 2.0) && isScaled(1, 2.0) && isScaled(5, 2.0) && isScaled(6, 2.0));
        CHECK("capsules (cannot be fully resized) are left alone", isOwn(2) && isOwn(3) && isOwn(7) && isOwn(8));
        CHECK("the headline counts only the 4 hitboxes it can resize", r.link->hitboxHeadline() == "connected: 2 hands, 4 hitboxes + 8 grab values made 2.0x bigger");
        r.link->stop();
    }
    std::printf("== no hitbox can be resized: the grab-reach values still work\n");
    {
        Rig r;
        hb_hide("BoxCollider", "set_size"); hb_hide("SphereCollider", "set_radius"); hb_hide("CapsuleCollider", "set_radius");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        const bool ok = r.connectHb(); r.run(0.8);
        CHECK("connected (state 1): the grab-reach values can be changed", ok && r.link->hitboxUiState() == 1);
        CHECK("the hitboxes are untouched, the grab-reach values are 2.0x", allOwn() && sets() == 0 && mineTw(2.0));
        CHECK("the headline says 0 hitboxes + 8 grab values", r.link->hitboxHeadline() == "connected: 2 hands, 0 hitboxes + 8 grab values made 2.0x bigger");
        CHECK("the facts file warns that no kind of hitbox can be resized", countNotes("WARNING: no kind of hitbox can be resized") == 1);
        r.link->setHitbox(false, 2.0f); r.run(0.5);
        CHECK("switch off: the grab-reach values are back", everythingOwn());
        r.link->stop();
    }
    std::printf("== no hitbox can be resized AND the game has none of the grab-reach values: it says so and does nothing\n");
    {
        Rig r;
        hb_hide("BoxCollider", "set_size"); hb_hide("SphereCollider", "set_radius"); hb_hide("CapsuleCollider", "set_radius");
        hb_field("Hand", "reachDistance", 0); hb_field("Hand", "palmRadius", 0); hb_field("BallControl", "_gravityDistance", 0); hb_field("BallControl", "_grabVolume", 0);
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        const int st = r.waitState(); r.run(0.5);
        CHECK("FAILED (state 3) and the headline names both problems", st == 3 && r.link->hitboxHeadline().find("FAILED: neither the hitbox sizes can be changed") == 0 && r.link->hitboxHeadline().find("Hand.reachDistance, Hand.palmRadius, BallControl._gravityDistance, BallControl._grabVolume") != std::string::npos);
        CHECK("nothing touched", everythingOwn() && sets() == 0 && tfSets() == 0 && vizCalls() == 0);
        r.link->stop();
    }

    std::printf("== the game was updated: a field is gone, has another type, or a class is gone -> FAILED with the reason, nothing changed\n");
    {
        struct Case { const char* name; const char* klass; const char* field; int action; const char* expect; };
        const Case cases[] = {
            {"Hand._handColliders removed", "Hand", "_handColliders", 0, "_handColliders is missing"},
            {"Hand._handColliders became an int", "Hand", "_handColliders", 1, "_handColliders in Autohand.Hand has type"},
            {"BallControl._hand removed", "BallControl", "_hand", 0, "_hand is missing"},
            {"BallControlManager._leftBallControl removed", "BallControlManager", "_leftBallControl", 0, "_leftBallControl is missing"},
            {"BallControlManager._rightBallControl became an int", "BallControlManager", "_rightBallControl", 1, "_rightBallControl in ShovelTools.BallControlManager has type"},
            {"the class Autohand.Hand removed", "Hand", "", 2, "FAILED"},
        };
        for (const Case& c : cases) {
            Rig r;
            hb_field(c.klass, c.field, c.action);
            r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
            const int st = r.waitState(); r.run(0.3);
            const bool ok = st == 3 && r.link->hitboxHeadline().find(c.expect) != std::string::npos && allOwn() && sets() == 0;
            CHECK(c.name, ok);
            if (!ok) std::printf("      state %d headline '%s'\n", st, r.link->hitboxHeadline().c_str());
            r.link->stop();
        }
    }

    std::printf("== a grab-reach value is missing or changed its type in the game: the other values still work, and the facts file says which one is missing\n");
    {
        struct Case { const char* name; const char* klass; const char* field; int action; const char* missing; int values; };
        const Case cases[] = {
            {"Hand.reachDistance removed", "Hand", "reachDistance", 0, "not in this game: Hand.reachDistance", 6},
            {"Hand.palmRadius became an int", "Hand", "palmRadius", 1, "not in this game: Hand.palmRadius", 6},
            {"BallControl._gravityDistance removed", "BallControl", "_gravityDistance", 0, "not in this game: BallControl._gravityDistance", 6},
            {"BallControl._grabVolume removed", "BallControl", "_grabVolume", 0, "not in this game: BallControl._grabVolume", 6},
        };
        for (const Case& c : cases) {
            Rig r;
            hb_field(c.klass, c.field, c.action);
            r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
            const bool ok = r.connectHb(); r.run(0.5);
            const bool good = ok && mineScaled(2.0) && r.link->hitboxCounters().grabValues == c.values && findNote("hitbox: GRAB-REACH values found").find(c.missing) != std::string::npos &&
                              r.link->hitboxHeadline() == std::string("connected: 2 hands, 8 hitboxes + ") + std::to_string(c.values) + " grab values made 2.0x bigger";
            CHECK(c.name, good);
            if (!good) std::printf("      headline '%s'\n", r.link->hitboxHeadline().c_str());
            r.link->setHitbox(false, 2.0f); r.run(0.4);
            CHECK("... and switching off puts everything back", everythingOwn());
            r.link->stop();
        }
    }

    std::printf("== the grab volume (a Transform): the game has no way to read or set its scale -> it is left alone, the rest works\n");
    {
        Rig r;
        hb_hide("Transform", "get_localScale"); hb_hide("Transform", "set_localScale");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f);
        const bool ok = r.connectHb(); r.run(0.5);
        CHECK("connected, with 6 grab values (3 per hand)", ok && r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes + 6 grab values made 3.0x bigger");
        double t[13]; hb_tw(0, t);
        CHECK("reach, palm radius and gravity distance are 3.0x; the grab volume's scale was not touched", nearD(t[0], 0.60) && nearD(t[1], 0.15) && nearD(t[2], 0.21) && nearD(t[3], 0.10) && tfSets() == 0);
        CHECK("the facts file says why the grab volume is left alone", findNote("hitbox: GRAB-REACH values found").find("BallControl._grabVolume left alone: neither the game nor the engine has Transform.get_localScale / set_localScale") != std::string::npos);
        r.link->setHitbox(false, 3.0f); r.run(0.4);
        CHECK("switch off: all values back", everythingOwn());
        r.link->stop();
    }
    std::printf("== the grab volume's scale cannot be SET (the engine refuses): that one value is given up after 3 tries, the rest keeps working, no stop\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("tf_set_throws", 1, 0);
        r.connectHb(); r.run(1.5);
        double t[13]; hb_tw(0, t);
        CHECK("the other 3 values per hand are 2.0x; the grab volume is at its own scale", nearD(t[0], 0.40) && nearD(t[1], 0.10) && nearD(t[2], 0.14) && nearD(t[3], 0.10) && mineScaled(2.0));
        CHECK("the part did not stop (state 1), 6 grab values are left, 6 refused writes were counted", r.link->hitboxUiState() == 1 && r.link->hitboxCounters().grabValues == 6 && r.link->hitboxCounters().grabWriteFails == 6);
        CHECK("the facts file says the grab volume could not be set (left hand and right hand)", countNotes("BallControl._grabVolume (left hand) could not be set 3 times - left alone") == 1 && countNotes("BallControl._grabVolume (right hand) could not be set 3 times - left alone") == 1);
        const long t0 = tfSets();
        r.run(2.0);
        CHECK("it does not keep trying", tfSets() == t0);
        r.link->setHitbox(false, 2.0f); r.run(0.4);
        CHECK("switch off: everything back", everythingOwn());
        r.link->stop();
    }
    std::printf("== the grab volume's scale cannot be READ: that value is given up, the rest keeps working\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("tf_get_throws", 1, 0);
        r.connectHb(); r.run(1.5);
        double t[13]; hb_tw(0, t);
        CHECK("the other values are 2.0x, the grab volume untouched, not stopped", nearD(t[0], 0.40) && nearD(t[2], 0.14) && nearD(t[3], 0.10) && mineScaled(2.0) && r.link->hitboxUiState() == 1 && tfSets() == 0);
        CHECK("the facts file says the grab volume cannot be read", countNotes("BallControl._grabVolume (left hand) cannot be read - left alone") == 1);
        r.link->setHitbox(false, 2.0f); r.run(0.4);
        CHECK("switch off: everything back", everythingOwn());
        r.link->stop();
    }

    std::printf("== the game puts its grab-reach values back by itself: noticed within a moment, set again, and counted\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f);
        r.connectHb(); r.run(0.4);
        hb_set("tw_game_reset", 0, 0);
        CHECK("(the pretend game just put all grab-reach values back to its own)", twOwn(0) && twOwn(1));
        r.run(0.6);
        CHECK("within a fraction of a second they are 3.0x again", mineTw(3.0));
        CHECK("the facts file says the game changed 8 values (ONE line) and that they went back to the game's own value", countNotes("hitbox: the game changed 8 of your grab-reach values") == 1 && findNote("hitbox: the game changed 8 of your grab-reach values").find("(8 back to its own value, 0 to a NEW own value)") != std::string::npos);
        CHECK("... the counter knows", r.link->hitboxCounters().grabResets == 8);
        hb_set("tw_game_new", 1.5, 0);
        r.run(0.6);
        CHECK("the game gave NEW own values (1.5x): the mod scales from them", mineTw(3.0) && nearD(twAt(0, 6), 0.30) && nearD(twAt(0, 0), 0.90));
        CHECK("... and the facts file says they were NEW own values", countNotes("(0 back to its own value, 8 to a NEW own value)") == 1);
        r.link->setHitbox(false, 3.0f); r.run(0.5);
        CHECK("switch off: the NEW own values are put back, not the old ones", twOwn(0) && twOwn(1) && nearD(twAt(0, 0), 0.30));
        r.link->stop();
    }
    std::printf("== the game changes ONE value for its own reasons (a new ball type, ...) while the expander is on\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        r.connectHb(); r.run(0.4);
        hb_twset(0, 2, 0.10, 1);               // the game sets _gravityDistance of your left ball control to 0.10 and keeps that as ITS value
        r.run(0.5);
        CHECK("the new game value is made 2.0x (0.20), the other values are untouched", nearD(twAt(0, 2), 0.20) && nearD(twAt(0, 0), 0.40) && nearD(twAt(1, 2), 0.14));
        hb_twset(0, 3, 0.5, 1);                // the game changes the grab volume's x scale
        r.run(0.5);
        CHECK("same for the grab volume's x scale: 1.0, and y and z stay 0.20", nearD(twAt(0, 3), 1.0) && nearD(twAt(0, 4), 0.20) && nearD(twAt(0, 5), 0.20));
        r.link->setHitbox(false, 2.0f); r.run(0.4);
        CHECK("switch off: the game's latest values are back (gravity 0.10, scale x 0.5)", nearD(twAt(0, 2), 0.10) && nearD(twAt(0, 3), 0.5) && nearD(twAt(0, 4), 0.10));
        r.link->stop();
    }
    std::printf("== the expander is OFF and the game changes a grab-reach value: the mod does not interfere, and later scales from the new value\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 1.0f);
        r.connectHb(); r.run(0.4);
        hb_twset(0, 0, 0.30, 1);
        r.run(0.5);
        CHECK("at 1.0x nothing is written", tfSets() == 0 && nearD(twAt(0, 0), 0.30));
        r.link->setHitbox(true, 2.0f); r.run(0.5);
        CHECK("then 2.0x is made from the NEW value (0.60), not from the old 0.20", nearD(twAt(0, 0), 0.60));
        r.link->stop();
    }
    std::printf("== the grab volume is destroyed, emptied, replaced or comes back\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        r.connectHb(); r.run(0.4);
        hb_set("tf_destroy", 0, 0);
        r.run(0.8);
        CHECK("a destroyed grab volume: no crash, the other values keep working, the right hand's grab volume too", r.link->hitboxUiState() == 1 && nearD(twAt(0, 0), 0.40) && nearD(twAt(1, 3), 0.20) && r.link->counters().errors == 0);
        hb_set("tf_null", 1, 0);
        r.run(0.5);
        hb_set("tf_back", 1, 0);
        r.run(0.5);
        CHECK("the right hand's grab volume field is emptied and filled again with the SAME object: it is still 2.0x (not 4x)", nearD(twAt(1, 3), 0.20));
        hb_set("tf_swap", 1, 0);
        r.run(0.5);
        CHECK("the game gives the right hand a NEW grab volume: it is scaled from ITS own scale (0.20), not 0.40", nearD(twAt(1, 3), 0.20) && nearD(twAt(1, 4), 0.20));
        r.link->setHitbox(false, 2.0f); r.run(0.4);
        CHECK("switch off: the new grab volume is back at its own scale", nearD(twAt(1, 3), 0.10) && nearD(twAt(1, 5), 0.10));
        r.link->stop();
    }
    std::printf("== a grab-reach value of 0 (multiplying it changes nothing): left alone, said once\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f);
        hb_set("tw_zero", 0, 0);
        r.connectHb(); r.run(0.5);
        CHECK("Hand.reachDistance of the left hand is left at 0, everything else is 2.0x", nearD(twAt(0, 0), 0.0) && nearD(twAt(1, 0), 0.40) && nearD(twAt(0, 1), 0.10) && mineScaled(2.0));
        CHECK("the facts file says so, once", countNotes("Hand.reachDistance (left hand) is 0.000 - multiplying that changes nothing, so it is left alone") == 1);
        r.link->stop();
    }

    std::printf("== it works next to the Aimbot and Shot points, and each one can be switched off without disturbing the others\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11); r.link->setHitbox(true, 3.0f);
        r.connectAim(); r.connectPts(); r.connectHb(); r.run(0.5);
        CHECK("all three connect", r.link->uiState() == 1 && r.link->pointsUiState() == 1 && r.link->hitboxUiState() == 1);
        CHECK("the hitboxes are 3.0x", mineScaled(3.0));
        weakThrow();
        r.run(5.0); r.settle(0.2);
        CHECK("the Aimbot fixed the throw and it went in", r.link->counters().aimed == 1 && gameAt(8) == 1);
        CHECK("Shot points counted 11", score() == 11);
        CHECK("the hitboxes are still 3.0x after the shot", mineScaled(3.0));
        r.link->setHitbox(false, 3.0f); r.run(0.6);
        CHECK("Hitbox expander off while the Aimbot stays on: sizes are back", allOwn());
        CHECK("... and the Aimbot is still connected", r.link->uiState() == 1 && r.link->pointsUiState() == 1);
        r.link->setHitbox(true, 2.0f); r.run(0.6);
        CHECK("on again: 2.0x", mineScaled(2.0));
        r.link->setAsk(false, 50); r.link->setPoints(0); r.run(0.6);
        CHECK("Aimbot and points off: the hitboxes are still 2.0x and still connected", mineScaled(2.0) && r.link->hitboxUiState() == 1 && r.link->uiState() == 0);
        r.link->setHitbox(false, 2.0f); r.run(0.6);
        CHECK("now everything is off: sizes back, link idle", allOwn() && r.link->hitboxUiState() == 0);
        r.link->stop();
    }
    std::printf("== a hitbox error never switches off the Aimbot\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setHitbox(true, 2.0f);
        hb_set("set_throws", 1, 0);
        r.connectAim(); r.run(3.0);
        CHECK("the hitbox part failed", r.link->hitboxUiState() == 3);
        weakThrow();
        r.run(5.0); r.settle(0.2);
        CHECK("the Aimbot still fixes the throw and it goes in", r.link->uiState() == 1 && r.link->counters().aimed == 1 && gameAt(8) == 1);
        r.link->stop();
    }

    std::printf("== not on the game's own thread: nothing happens\n");
    {
        Rig r("UnityMain"); r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f);
        r.run(1.5);
        CHECK("a doorway call from some other thread is ignored: nothing was read or set", everythingOwn() && sets() == 0 && gets() == 0 && vizCalls() == 0 && tfGets() == 0 && tfSets() == 0);
        r.link->stop();
    }

    std::printf("== the facts file stays small\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50);
        for (int i = 0; i < 60; ++i) {
            r.link->setHitbox(true, 1.5f + (i % 7) * 0.5f); r.run(0.5);
            if (i % 5 == 0) hb_set("game_reset", 0, 0);
            if (i % 11 == 0) hb_set("rebuild", 0, 0);
            r.link->setHitbox(false, 2.0f); r.run(0.3);
        }
        r.settle(0.2);
        CHECK("60 rounds of switching on and off, resets and new hands: not more than 62 hitbox lines in all", countPrefix("hitbox:") <= 62);
        CHECK("... and everything is back to the game's own size and values at the end", everythingOwn());
        r.link->stop();
    }

    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed ? 1 : 0;
}
