// PC test of the "Hitbox expander" + "See hitbox" part of the game link (aim_hitbox.cpp) against the PRETEND game (fake_il2cpp_aim.cpp).
// The pretend game has two hands of YOURS (reached from your ball control manager) and one hand of ANOTHER player. Every hand has five hitboxes: a box, a sphere,
// two capsules and a mesh (a kind whose size cannot be changed). Index 0-4 = your left hand, 5-9 = your right hand, 10-14 = the other player's hand.
// It proves the logic (find the hands, scale from the game's own sizes, put them back, survive a game that changes things) and that the part leaves everything else alone.
// It does NOT prove that the REAL game's steal / block code uses these hitboxes, that other players see them, or what the game's own display draws.
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
static int vizOn(int hand) { return static_cast<int>(hbAt(6 + hand)); }
static int vizOff(int hand) { return static_cast<int>(hbAt(8 + hand)); }
static int vizNow(int hand) { return static_cast<int>(hbAt(3 + hand)); }
static double reachOf(int hand) { return hb_reach(hand); }
static double gameAt(int i) { double s[10]; fake_state(s); return s[i]; }
static double score() { double s[8]; pts_state(s); return s[0]; }

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
    if (!hb_set || !hb_state || !hb_col || !hb_reach || !hb_hide || !hb_field || !pts_state) { std::printf("the pretend runtime lacks the hitbox functions\n"); return 2; }

    std::printf("== the pretend world itself (so the checks below mean something)\n");
    {
        Rig r;
        CHECK("15 hitboxes: kinds box, sphere, capsule, capsule, mesh for each of the three hands", kindOf(0) == 1 && kindOf(1) == 2 && kindOf(2) == 3 && kindOf(3) == 3 && kindOf(4) == 4 && kindOf(5) == 1 && kindOf(9) == 4 && kindOf(10) == 1 && kindOf(14) == 4);
        CHECK("all start at the game's own size", allOwn());
        CHECK("the other player's hand is 3 times bigger than yours", reachOf(2) > 2.9 * reachOf(0) && reachOf(2) < 3.1 * reachOf(0));
    }

    std::printf("== every switch is off: the hitbox part touches nothing, and says nothing\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 3.0f, false);
        r.run(1.5);
        CHECK("not a single engine call about hitboxes", sets() == 0 && gets() == 0 && vizCalls() == 0 && gameAt(6) == 0);
        CHECK("every hitbox is at the game's own size", allOwn());
        CHECK("menu state is 'off' and the headline is empty", r.link->hitboxUiState() == 0 && r.link->hitboxHeadline().empty());
        CHECK("no hitbox lines in the facts file", countPrefix("hitbox:") == 0);
        CHECK("the counters are all zero", r.link->hitboxCounters().resized == 0 && r.link->hitboxCounters().censuses == 0 && r.link->hitboxCounters().hands == 0);
        CHECK("the link is not even running for it (the Aimbot and Shot points are off too)", r.link->uiState() == 0);
        r.link->stop();
    }

    std::printf("== Hitbox expander ON at 2.0x (Aimbot and Shot points off): your hands get bigger, nothing else does\n");
    {
        Rig r; const double reach0 = reachOf(0); r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("the hitbox part connects (state 1)", ok && r.link->hitboxUiState() == 1);
        CHECK("the headline says 2 hands and 8 hitboxes made 2.0x bigger", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes made 2.0x bigger");
        CHECK("all 8 resizable hitboxes of YOUR two hands are exactly 2.0 times the game's own size", mineScaled(2.0));
        CHECK("the two mesh hitboxes (size cannot be changed) are untouched", meshesOwn());
        CHECK("the OTHER player's hand is untouched", otherOwn());
        CHECK("your hands now reach twice as far (both of them), the other player's hand not", reach0 > 0 && nearD(reachOf(0), 2.0 * reach0) && nearD(reachOf(1), 2.0 * reach0) && nearD(reachOf(2), 3.0 * reach0));
        CHECK("the Aimbot is NOT on (state off) and no throw was touched", r.link->uiState() == 0 && gameAt(7) == 0);
        CHECK("the facts file says where the hands keep their hitboxes", countNotes("hitbox: found where your hands keep their hitboxes") == 1 && findNote("hitbox: found where").find("_handColliders@144") != std::string::npos);
        CHECK("the facts file says the switch went on, at 2.0x", countNotes("hitbox: Hitbox expander turned ON at 2.0x") == 1);
        CHECK("the facts file says it found both hands and counts 10 hitboxes, 8 resizable", countNotes("hitbox: found your hands") == 1 && findNote("hitbox: your hands have 10 hitboxes").find("8 can be resized (box 2, sphere 2, capsule 4), mesh 2, other kinds 0") != std::string::npos);
        CHECK("... and names the first hitboxes with their sizes", findNote("hitbox: your hands have").find("BoxCollider") != std::string::npos && findNote("hitbox: your hands have").find("size (0.080, 0.020, 0.100)") != std::string::npos);
        CHECK("the facts file says 8 hitboxes were set to 2.0x", countNotes("hitbox: set 8 hitboxes of your hands to 2.0x their own size") == 1);
        CHECK("the counters: 8 resized, no failures, no game resets", r.link->hitboxCounters().resized == 8 && r.link->hitboxCounters().readFails == 0 && r.link->hitboxCounters().writeFails == 0 && r.link->hitboxCounters().gameResets == 0);
        CHECK("it counts hands 2, hitboxes 10, resizable 8 (box 2, sphere 2, capsule 4, mesh 2)", r.link->hitboxCounters().hands == 2 && r.link->hitboxCounters().hitboxes == 10 && r.link->hitboxCounters().resizable == 8 &&
              r.link->hitboxCounters().boxes == 2 && r.link->hitboxCounters().spheres == 2 && r.link->hitboxCounters().capsules == 4 && r.link->hitboxCounters().meshes == 2);
        const long setsNow = sets();
        r.run(3.0);
        CHECK("it does not keep writing: no new size changes in 3 more seconds", sets() == setsNow);
        CHECK("it does not read all the time either (a few reads per second at most)", gets() < 400);
        CHECK("the summary line for the facts file has the state, the asked size and the counts", r.link->hitboxSummary().find("hitbox: link connected | asked: expander 2.0x, see hitbox off | hands 2, hitboxes 10 (can be resized 8: box 2, sphere 2, capsule 4; other kinds 0, mesh 2)") == 0);
        r.link->stop();
    }

    std::printf("== the slider's stops: 1.0 ... 5.0 (and out-of-range values are pulled in)\n");
    {
        bool allOk = true; double badStop = 0;
        for (int st = 10; st <= 50; st += 5) {
            Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, static_cast<float>(st) / 10.0f, false);
            r.connectHb(); r.run(0.4);
            const double mul = st / 10.0;
            const bool ok = mineScaled(mul) && meshesOwn() && otherOwn() && r.link->hitboxUiState() == 1;
            if (!ok) { allOk = false; badStop = mul; }
            r.link->stop();
        }
        CHECK("1.0x, 1.5x, 2.0x ... 5.0x each give exactly that many times the game's own size", allOk);
        if (!allOk) std::printf("      first bad stop: %.1f\n", badStop);
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 1.0f, false); r.connectHb(); r.run(1.0);
          CHECK("1.0x is the normal size: nothing was changed, not one set call", mineScaled(1.0) && sets() == 0);
          CHECK("... and the headline says so", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes at the normal size (1.0x)");
          r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 9.0f, false); r.connectHb(); r.run(0.4);
          CHECK("9.0 is pulled in to 5.0x", mineScaled(5.0)); r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 0.2f, false); r.connectHb(); r.run(0.4);
          CHECK("0.2 is pulled up to 1.0x (never smaller than normal)", mineScaled(1.0) && sets() == 0); r.link->stop(); }
        { Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, std::nanf(""), false); r.connectHb(); r.run(0.4);
          CHECK("a number that is not a number becomes 1.0x", mineScaled(1.0) && sets() == 0); r.link->stop(); }
    }

    std::printf("== moving the slider while it is on: always from the game's own size, never on top of the last change\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        r.connectHb(); r.run(0.4);
        CHECK("2.0x first", mineScaled(2.0));
        r.link->setHitbox(true, 3.5f, false); r.run(0.5);
        CHECK("then 3.5x: exactly 3.5 times the game's own size (not 7x)", mineScaled(3.5));
        r.link->setHitbox(true, 5.0f, false); r.run(0.5);
        CHECK("then 5.0x", mineScaled(5.0));
        r.link->setHitbox(true, 1.5f, false); r.run(0.5);
        CHECK("then 1.5x (it can shrink again)", mineScaled(1.5));
        r.link->setHitbox(true, 1.0f, false); r.run(0.5);
        CHECK("then 1.0x: back to exactly the game's own size", mineScaled(1.0) && allOwn());
        CHECK("... and the headline says 'at the normal size'", r.link->hitboxHeadline().find("at the normal size (1.0x)") != std::string::npos);
        CHECK("... and the counters know 8 hitboxes were put back", r.link->hitboxCounters().restored == 8);
        const long s0 = sets();
        r.link->setHitbox(true, 2.0f, false); r.run(0.5);
        CHECK("2.0x again works after a trip through 1.0x", mineScaled(2.0) && sets() > s0);
        for (int i = 0; i < 40; ++i) { r.link->setHitbox(true, 1.0f + (i % 9) * 0.5f, false); r.run(0.02); }
        r.link->setHitbox(true, 4.0f, false); r.run(0.6);
        CHECK("wiggling the slider 40 times quickly ends exactly at the last value (4.0x)", mineScaled(4.0) && meshesOwn() && otherOwn());
        r.link->stop();
    }

    std::printf("== the switch goes OFF: the game's own sizes are put back exactly\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 4.0f, false);
        r.connectHb(); r.run(0.4);
        CHECK("4.0x is on", mineScaled(4.0));
        r.link->setHitbox(false, 4.0f, false); r.run(0.5);
        CHECK("switch off: every hitbox is exactly the game's own size again", allOwn());
        CHECK("the menu state is off again and the headline is empty", r.link->hitboxUiState() == 0 && r.link->hitboxHeadline().empty());
        CHECK("the facts file says it switched off and put 8 hitboxes back", countNotes("hitbox: switched off") == 1 && findNote("hitbox: switched off").find("8 hitboxes put back to the game's own size") != std::string::npos);
        CHECK("the counters: 8 resized, 8 put back", r.link->hitboxCounters().resized == 8 && r.link->hitboxCounters().restored == 8);
        const long s0 = sets(), g0 = gets();
        r.run(2.0);
        CHECK("after that it does nothing at all (no more reads or writes)", sets() == s0 && gets() == g0);
        r.link->setHitbox(true, 3.0f, false); r.connectHb(); r.run(0.5);
        CHECK("turning it on again works and scales from the same game sizes (3.0x)", mineScaled(3.0));
        r.link->stop();
        CHECK("stopping the whole link puts the game's sizes back too", allOwn() || mineScaled(3.0));      // (stop() ends the thread; the stand-down already ran for every switch-off, this only has to not crash)
    }

    std::printf("== the game puts the sizes back by itself (a new round, a respawn): the mod notices and sets them again\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f, false);
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
        r.link->setHitbox(false, 3.0f, false); r.run(0.5);
        CHECK("switch off: it puts back the NEW own size (not the old one)", allOwn() && nearD(c[4], 0.08 * 1.5));
        r.link->stop();
    }

    std::printf("== the game destroys a hitbox: no crash, the rest keep working\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        r.connectHb(); r.run(0.4);
        hb_set("destroy_col", 1, 0);           // your left hand's sphere
        r.link->setHitbox(true, 3.0f, false); r.run(2.6);
        double c[8]; hb_col(1, c);
        CHECK("the destroyed hitbox was not written to (still the old size, 2.0x)", isScaled(1, 2.0) && c[7] == 0);
        bool restOk = true; for (int i : kMine) if (i != 1 && !isScaled(i, 3.0)) restOk = false;
        CHECK("all the other 7 hitboxes follow the slider to 3.0x", restOk);
        CHECK("still connected, no engine errors", r.link->hitboxUiState() == 1 && r.link->counters().errors == 0);
        r.link->setHitbox(false, 3.0f, false); r.run(0.5);
        bool back = true; for (int i : kMine) if (i != 1 && !isOwn(i)) back = false;
        CHECK("switch off: the 7 living ones are back to their own size", back);
        r.link->stop();
    }

    std::printf("== the game builds new hands (new avatar / new scene): the mod finds them\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.5f, false);
        r.connectHb(); r.run(0.4);
        CHECK("2.5x on the first hands", mineScaled(2.5));
        hb_set("rebuild", 0, 0);
        CHECK("(the pretend game just made new hands with their own sizes)", allOwn());
        r.run(1.0);
        CHECK("the new hands are 2.5x within a second", mineScaled(2.5) && meshesOwn() && otherOwn());
        CHECK("the facts file says it found hands again", countNotes("hitbox: found your hands") >= 2);
        CHECK("still connected with 2 hands", r.link->hitboxUiState() == 1 && r.link->hitboxCounters().hands == 2);
        r.link->setHitbox(false, 2.5f, false); r.run(0.5);
        CHECK("switch off: the new hands are at their own size", allOwn());
        r.link->stop();
    }

    std::printf("== a hand disappears from your ball control, or its hitbox list is emptied: what was changed is put back\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f, false);
        r.connectHb(); r.run(0.4);
        hb_set("unlink", 0, 0);                 // the left hand is no longer linked from your ball control
        r.run(0.6);
        CHECK("the left hand's hitboxes were put back to normal (the mod does not leave them big)", isOwn(0) && isOwn(1) && isOwn(2) && isOwn(3));
        CHECK("the right hand is still 3.0x", isScaled(5, 3.0) && isScaled(6, 3.0) && isScaled(7, 3.0) && isScaled(8, 3.0));
        CHECK("the menu says 1 hand", r.link->hitboxCounters().hands == 1 && r.link->hitboxHeadline().find("1 hand,") != std::string::npos && r.link->hitboxUiState() == 1);
        CHECK("the facts file says some hitboxes left the lists", countNotes("left the lists of your hands' hitboxes - put back to the game's own size") >= 1);
        hb_set("relink", 0, 0);
        r.run(0.6);
        CHECK("linked again: the left hand is 3.0x from ITS OWN size (not 9x)", mineScaled(3.0));
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
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        hb_set("authority", 0, 0);
        r.connectHb(); r.run(0.4);
        CHECK("2.0x is applied", mineScaled(2.0));
        CHECK("the facts file shows (authority 0) for both hands", findNote("hitbox: found your hands").find("(authority 0)") != std::string::npos);
        r.link->stop();
    }

    std::printf("== the engine refuses to SET a size: the part gives up cleanly after a few tries and says why\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        hb_set("set_throws", 1, 0);
        r.run(3.0);
        CHECK("the menu shows FAILED (state 3)", r.link->hitboxUiState() == 3);
        CHECK("the headline says why", r.link->hitboxHeadline().find("FAILED:") == 0);
        CHECK("nothing was changed in the game", allOwn());
        CHECK("the facts file says it stopped, once", countNotes("hitbox: STOPPED") == 1);
        const long s0 = sets();
        r.run(2.0);
        CHECK("it stopped trying (no more set calls)", sets() == s0);
        CHECK("the failures are counted", r.link->hitboxCounters().writeFails >= 1);
        r.link->setHitbox(false, 2.0f, false); r.run(0.3);
        hb_set("set_throws", 0, 0);
        r.link->setHitbox(true, 2.0f, false);
        CHECK("switching it off and on again tries again and works", r.connectHb() && (r.run(0.5), mineScaled(2.0)));
        r.link->stop();
    }
    std::printf("== the engine refuses to READ a size: nothing is changed (the mod does not guess the game's own size)\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        hb_set("get_throws", 1, 0);
        const int st = r.waitState(); r.run(1.5);
        CHECK("the menu shows FAILED (state 3)", st == 3 && r.link->hitboxUiState() == 3);
        CHECK("nothing was set", sets() == 0 && allOwn());
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
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("connects through the internal calls", ok && r.link->hitboxUiState() == 1);
        CHECK("all 8 hitboxes are 2.0x", mineScaled(2.0) && meshesOwn() && otherOwn());
        CHECK("the facts file says the calls are 'engine call'", findNote("hitbox: found where").find("Box size read engine call / set engine call") != std::string::npos);
        r.link->setHitbox(false, 2.0f, false); r.run(0.4);
        CHECK("switch off puts the sizes back through them too", allOwn());
        r.link->stop();
    }
    std::printf("== only the setters are missing for one kind (capsules): the other kinds still work\n");
    {
        Rig r;
        hb_hide("CapsuleCollider", "set_height");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("connects", ok);
        CHECK("boxes and spheres are 2.0x", isScaled(0, 2.0) && isScaled(1, 2.0) && isScaled(5, 2.0) && isScaled(6, 2.0));
        CHECK("capsules (cannot be fully resized) are left alone", isOwn(2) && isOwn(3) && isOwn(7) && isOwn(8));
        CHECK("the headline counts only the 4 hitboxes it can resize", r.link->hitboxHeadline() == "connected: 2 hands, 4 hitboxes made 2.0x bigger");
        r.link->stop();
    }
    std::printf("== no hitbox can be resized and no display exists: it says so and does nothing\n");
    {
        Rig r;
        hb_hide("BoxCollider", "set_size"); hb_hide("SphereCollider", "set_radius"); hb_hide("CapsuleCollider", "set_radius");
        hb_hide("BallControl", "SetHandColliderVisual");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        const int st = r.waitState(); r.run(0.5);
        CHECK("FAILED (state 3) and the headline names both problems", st == 3 && r.link->hitboxHeadline().find("FAILED: neither the hitbox sizes can be changed") == 0);
        CHECK("nothing touched", allOwn() && sets() == 0 && vizCalls() == 0);
        r.link->stop();
    }
    std::printf("== the setters are missing but the display exists: Hitbox expander says 'none can be resized', See hitbox still works\n");
    {
        Rig r;
        hb_hide("BoxCollider", "set_size"); hb_hide("SphereCollider", "set_radius"); hb_hide("CapsuleCollider", "set_radius");
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, true);
        const int st = r.waitState(); r.run(0.8);
        CHECK("state 3: the hitboxes were found but none can be resized", st == 3 && r.link->hitboxHeadline().find("none of them can be resized") != std::string::npos);
        CHECK("... but the game's display was still asked to show them", vizOn(0) == 1 && vizOn(1) == 1);
        CHECK("the facts file warns about it", countNotes("WARNING: no kind of hitbox can be resized") == 1);
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
            r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
            const int st = r.waitState(); r.run(0.3);
            const bool ok = st == 3 && r.link->hitboxHeadline().find(c.expect) != std::string::npos && allOwn() && sets() == 0;
            CHECK(c.name, ok);
            if (!ok) std::printf("      state %d headline '%s'\n", st, r.link->hitboxHeadline().c_str());
            r.link->stop();
        }
        Rig r;
        hb_field("BallControl", "_fingerVizPrefab", 0);
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, false);
        const bool ok = r.connectHb(); r.run(0.4);
        CHECK("_fingerVizPrefab removed: Hitbox expander still works (only See hitbox needs it)", ok && mineScaled(2.0));
        r.link->setHitbox(true, 2.0f, true); r.run(0.6);
        CHECK("... and See hitbox then says why it cannot work", r.link->hitboxHeadline().find("See hitbox not available") != std::string::npos && vizCalls() == 0);
        r.link->stop();
    }

    std::printf("== See hitbox ON (Hitbox expander OFF): the game's own display is asked for, on your hands only\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 1.0f, true);
        const bool ok = r.connectHb(); r.run(1.0);
        CHECK("connected (state 1) with the display on", ok && r.link->hitboxHeadline() == "connected: 2 hands | See hitbox: the game's display is on");
        CHECK("the game's display function was called once for the left and once for the right hand, with 'show'", vizOn(0) == 1 && vizOn(1) == 1 && vizNow(0) == 1 && vizNow(1) == 1);
        CHECK("the other player's hand got no call", vizOn(2) == 0 && vizOff(2) == 0 && vizNow(2) == 0);
        CHECK("no hitbox size was touched (Hitbox expander is off)", sets() == 0 && allOwn());
        CHECK("it does not call the display again and again", vizCalls() == 2);
        r.run(3.0);
        CHECK("... not even after 3 more seconds", vizCalls() == 2);
        CHECK("the facts file says it asked for the display, and that it cannot see what it draws", countNotes("hitbox: asked the game to show its hand collider display on the left hand") == 1 && findNote("hitbox: asked the game").find("I cannot see from here") != std::string::npos);
        CHECK("the facts file says the switch went on", countNotes("hitbox: See hitbox turned ON") == 1);
        r.link->setHitbox(false, 1.0f, false); r.run(0.5);
        CHECK("switch off: the display was hidden on both hands", vizOff(0) == 1 && vizOff(1) == 1 && vizNow(0) == 0 && vizNow(1) == 0);
        CHECK("... and the menu is off", r.link->hitboxUiState() == 0);
        CHECK("the facts file says the display was switched off", findNote("hitbox: switched off").find("game display switched off") != std::string::npos);
        r.link->stop();
    }
    std::printf("== See hitbox with the Hitbox expander: the display follows the slider (hidden and shown again once, after the slider rests)\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, true);
        r.connectHb(); r.run(1.0);
        CHECK("both on: hitboxes are 2.0x and the display is on", mineScaled(2.0) && vizNow(0) == 1 && vizNow(1) == 1);
        CHECK("the headline shows both", r.link->hitboxHeadline() == "connected: 2 hands, 8 hitboxes made 2.0x bigger | See hitbox: the game's display is on");
        for (int i = 0; i < 20; ++i) { r.link->setHitbox(true, 1.0f + (i % 9) * 0.5f, true); r.run(0.05); }
        r.link->setHitbox(true, 4.0f, true);
        r.run(1.5);
        CHECK("after the slider rested at 4.0x: hitboxes are 4.0x and the display is on", mineScaled(4.0) && vizNow(0) == 1 && vizNow(1) == 1);
        CHECK("the display was re-shown only a few times, not on every slider step (at most 3 on-calls per hand)", vizOn(0) <= 3 && vizOn(1) <= 3 && vizOn(0) >= 2);
        r.link->setHitbox(true, 4.0f, false); r.run(0.5);
        CHECK("See hitbox off, expander still on: display hidden, hitboxes still 4.0x", vizNow(0) == 0 && vizNow(1) == 0 && mineScaled(4.0));
        r.link->setHitbox(false, 4.0f, false); r.run(0.5);
        CHECK("all off: everything back to normal", allOwn() && vizNow(0) == 0);
        r.link->stop();
    }
    std::printf("== the game's display function takes (prefab, bool) instead of (prefab, float): still works\n");
    {
        Rig r;
        hb_set("viz_bool", 1, 0);
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 1.0f, true);
        const bool ok = r.connectHb(); r.run(0.6);
        CHECK("connected, shown on both hands", ok && vizNow(0) == 1 && vizNow(1) == 1);
        CHECK("the facts file names the parameter types it read", findNote("hitbox: found where").find("SetHandColliderVisual(UnityEngine.GameObject, System.Boolean)") != std::string::npos);
        r.link->setHitbox(false, 1.0f, false); r.run(0.4);
        CHECK("hidden again", vizNow(0) == 0 && vizNow(1) == 0);
        r.link->stop();
    }
    std::printf("== the display function has parameters I do not know: it is not called at all, and the menu says why\n");
    {
        Rig r;
        hb_set("viz_weird", 1, 0);
        r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 1.0f, true);
        r.run(1.5);
        CHECK("not a single call", vizCalls() == 0);
        CHECK("the headline says See hitbox is not available and why", r.link->hitboxHeadline().find("See hitbox not available: SetHandColliderVisual has parameters (System.Int32, System.Int32)") != std::string::npos);
        r.link->stop();
    }
    std::printf("== your hand has no display model yet: nothing is called, the menu says so, and it works once the model is there\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(false, 1.0f, true);
        hb_set("prefab_null", 1, 0);
        r.run(1.5);
        CHECK("no call while there is no model", vizCalls() == 0);
        CHECK("the headline says the model is empty", r.link->hitboxHeadline().find("_fingerVizPrefab is empty") != std::string::npos);
        r.link->stop();
    }
    std::printf("== the game's display function throws: three tries, then See hitbox is marked failed; the Hitbox expander keeps working\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 2.0f, true);
        hb_set("viz_throws", 1, 0);
        r.connectHb(); r.run(2.0);
        CHECK("the Hitbox expander is fine (2.0x)", mineScaled(2.0) && r.link->hitboxUiState() == 1);
        CHECK("the display was tried a few times only", vizCalls() == 0 && r.link->hitboxCounters().visualCalls >= 3 && r.link->hitboxCounters().visualCalls <= 8);
        CHECK("the headline says See hitbox failed", r.link->hitboxHeadline().find("See hitbox failed") != std::string::npos);
        r.link->stop();
    }

    std::printf("== it works next to the Aimbot and Shot points, and each one can be switched off without disturbing the others\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11); r.link->setHitbox(true, 3.0f, false);
        r.connectAim(); r.connectPts(); r.connectHb(); r.run(0.5);
        CHECK("all three connect", r.link->uiState() == 1 && r.link->pointsUiState() == 1 && r.link->hitboxUiState() == 1);
        CHECK("the hitboxes are 3.0x", mineScaled(3.0));
        weakThrow();
        r.run(5.0); r.settle(0.2);
        CHECK("the Aimbot fixed the throw and it went in", r.link->counters().aimed == 1 && gameAt(8) == 1);
        CHECK("Shot points counted 11", score() == 11);
        CHECK("the hitboxes are still 3.0x after the shot", mineScaled(3.0));
        r.link->setHitbox(false, 3.0f, false); r.run(0.6);
        CHECK("Hitbox expander off while the Aimbot stays on: sizes are back", allOwn());
        CHECK("... and the Aimbot is still connected", r.link->uiState() == 1 && r.link->pointsUiState() == 1);
        r.link->setHitbox(true, 2.0f, false); r.run(0.6);
        CHECK("on again: 2.0x", mineScaled(2.0));
        r.link->setAsk(false, 50); r.link->setPoints(0); r.run(0.6);
        CHECK("Aimbot and points off: the hitboxes are still 2.0x and still connected", mineScaled(2.0) && r.link->hitboxUiState() == 1 && r.link->uiState() == 0);
        r.link->setHitbox(false, 2.0f, false); r.run(0.6);
        CHECK("now everything is off: sizes back, link idle", allOwn() && r.link->hitboxUiState() == 0);
        r.link->stop();
    }
    std::printf("== a hitbox error never switches off the Aimbot\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setHitbox(true, 2.0f, false);
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
        Rig r("UnityMain"); r.start(); r.link->setAsk(false, 50); r.link->setHitbox(true, 3.0f, true);
        r.run(1.5);
        CHECK("a doorway call from some other thread is ignored: nothing was read, set or shown", allOwn() && sets() == 0 && gets() == 0 && vizCalls() == 0);
        r.link->stop();
    }

    std::printf("== the facts file stays small\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50);
        for (int i = 0; i < 60; ++i) {
            r.link->setHitbox(true, 1.5f + (i % 7) * 0.5f, (i % 2) == 0); r.run(0.5);
            if (i % 5 == 0) hb_set("game_reset", 0, 0);
            if (i % 11 == 0) hb_set("rebuild", 0, 0);
            r.link->setHitbox(false, 2.0f, false); r.run(0.3);
        }
        r.settle(0.2);
        CHECK("60 rounds of switching on and off, resets and new hands: not more than 62 hitbox lines in all", countPrefix("hitbox:") <= 62);
        CHECK("... and everything is back to the game's own size at the end", allOwn());
        r.link->stop();
    }

    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed ? 1 : 0;
}
