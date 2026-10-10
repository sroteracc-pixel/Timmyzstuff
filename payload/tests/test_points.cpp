// PC test of the "Shot points" part of the game link (aim_points.cpp) against the PRETEND game (fake_il2cpp_aim.cpp).
// The pretend game does what the real game's files say it does: every ball has a settings object with a whole number _pointValue; the game writes its own number there
// when a throw is released; the pretend scoreboard counts a basket from that number (three ways - see the fake). It proves the logic of the points part and that it leaves
// everything else alone. It does NOT prove that the REAL game's scoring reads that number.
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
#include "points.h"

using namespace tzaimlink;

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)

static void* gLib = nullptr;
static void (*fake_create)(int) = nullptr;
static void (*fake_set)(const char*, double, double, double) = nullptr;
static void (*fake_hold)(double, double, double) = nullptr;
static void (*fake_release)(int, double, double, double) = nullptr;
static void (*fake_step)() = nullptr;
static void (*fake_state)(double*) = nullptr;
static double (*fake_fdt)() = nullptr;
static void (*pts_set)(const char*, double, double) = nullptr;
static void (*pts_state)(double*) = nullptr;
static void (*pts_make_other)() = nullptr;
static void (*pts_switch_ball)(int) = nullptr;

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
    // run until the POINTS part is connected (bounded)
    bool connectPts(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->pointsUiState() == 1) return true; if (link->pointsUiState() == 3) return false; }
        return false;
    }
    bool connectAim(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->uiState() == 1) return true; if (link->uiState() == 3) return false; }
        return false;
    }
    void settle(double seconds) { run(seconds); usleep(30000); }
};

static double ptsAt(int i) { double s[8]; pts_state(s); return s[i]; }
static double gameAt(int i) { double s[10]; fake_state(s); return s[i]; }
static double score() { return ptsAt(0); }
static double myValue() { return ptsAt(2); }
static double otherValue() { return ptsAt(3); }

static void throwFrom(double fx, double fy, double fz, double tx, double tz, double elevDeg, double speed, double offDeg, int hand = 2) {
    const double dx = tx - fx, dz = tz - fz;
    double ang = std::atan2(dx, dz) + offDeg * 3.14159265358979 / 180.0;
    const double e = elevDeg * 3.14159265358979 / 180.0;
    fake_hold(fx, fy, fz);
    fake_release(hand, std::sin(ang) * speed * std::cos(e), speed * std::sin(e), std::cos(ang) * speed * std::cos(e));
}
// the throw that is too weak and 6 degrees off: it only goes in when the Aimbot (Direct) fixes it
static void weakThrow() { throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0); }

int main(int, char** argv) {
    gLib = dlopen(argv[1], RTLD_NOW);
    if (!gLib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    fake_create = reinterpret_cast<void (*)(int)>(dlsym(gLib, "fake_aim_create"));
    fake_set = reinterpret_cast<void (*)(const char*, double, double, double)>(dlsym(gLib, "fake_aim_set"));
    fake_hold = reinterpret_cast<void (*)(double, double, double)>(dlsym(gLib, "fake_aim_hold"));
    fake_release = reinterpret_cast<void (*)(int, double, double, double)>(dlsym(gLib, "fake_aim_release"));
    fake_step = reinterpret_cast<void (*)()>(dlsym(gLib, "fake_aim_step"));
    fake_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_aim_state"));
    fake_fdt = reinterpret_cast<double (*)()>(dlsym(gLib, "fake_aim_fdt"));
    pts_set = reinterpret_cast<void (*)(const char*, double, double)>(dlsym(gLib, "fake_pts_set"));
    pts_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_pts_state"));
    pts_make_other = reinterpret_cast<void (*)()>(dlsym(gLib, "fake_pts_make_other"));
    pts_switch_ball = reinterpret_cast<void (*)(int)>(dlsym(gLib, "fake_pts_switch_ball"));
    (void)fake_set;

    std::printf("== the slider's stops (points.h)\n");
    {
        bool ok = true;
        for (int st = 1; st <= 11; ++st) ok = ok && tzpoints::pointsForStop(static_cast<float>(st)) == st;
        CHECK("stops 1 ... 11 mean exactly 1 ... 11 points", ok);
        CHECK("stop 12 means 999", tzpoints::pointsForStop(12.0f) == 999);
        CHECK("out of range is pulled in: 0 -> 1, 99 -> 999, -3 -> 1", tzpoints::pointsForStop(0.0f) == 1 && tzpoints::pointsForStop(99.0f) == 999 && tzpoints::pointsForStop(-3.0f) == 1);
        CHECK("a slider value between stops rounds to the nearest stop (11.4 -> 11, 11.6 -> 999)", tzpoints::pointsForStop(11.4f) == 11 && tzpoints::pointsForStop(11.6f) == 999);
        CHECK("points -> stop and back: 7 -> 7, 11 -> 11, 999 -> 12, 50 -> 12", tzpoints::stopForPoints(7) == 7.0f && tzpoints::stopForPoints(11) == 11.0f && tzpoints::stopForPoints(999) == 12.0f && tzpoints::stopForPoints(50) == 12.0f);
        CHECK("the text is the plain number", tzpoints::pointsText(11) == "11" && tzpoints::pointsText(999) == "999");
    }

    std::printf("== the switch is off: the points part touches nothing, and says nothing\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(0);
        pts_set("value", 5, 0);
        r.run(0.5);
        weakThrow();
        r.run(3.0);
        CHECK("not a single call into the game", gameAt(6) == 0);
        CHECK("the ball's point value is exactly what the game wrote at the release (3)", myValue() == 3);
        CHECK("the menu state is 'off' and the headline is empty", r.link->pointsUiState() == 0 && r.link->pointsHeadline().empty());
        CHECK("no points lines in the facts file", countPrefix("points:") == 0);
        CHECK("the counters are all zero", r.link->pointsCounters().writes == 0 && r.link->pointsCounters().balls == 0);
        r.link->stop();
    }

    std::printf("== Shot points ON alone (the Aimbot is OFF): the ball is kept at the asked number\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("value", 5, 0);          // the number the ball had before we looked
        const bool ok = r.connectPts();
        r.run(0.2);
        CHECK("the points part connects (state 1) and says what it does", ok && r.link->pointsUiState() == 1 && r.link->pointsHeadline().find("connected - your ball is kept at 11 points") == 0);
        CHECK("the ball you hold now has 11 points", myValue() == 11);
        CHECK("the Aimbot is NOT on: its state is off and its headline says so", r.link->uiState() == 0 && r.link->headline() == "Aimbot is off");
        CHECK("the facts file says where the number lives", countNotes("points: found where the ball keeps its point value") == 1 && findNote("points: found where").find("_pointValue@164") != std::string::npos);
        CHECK("the facts file says the switch went on, and what the ball had at the first look (5)", countNotes("points: switch turned ON: every basket of yours is set to 11 points") == 1 && findNote("first look at a ball").find("point value is 5") != std::string::npos);
        weakThrow();
        r.run(0.03);
        CHECK("just after the release the game's own number (3) was replaced by ours within a few hundredths of a second", myValue() == 11);
        r.run(3.0); r.settle(0.2);
        CHECK("the ball still has 11 after the flight", myValue() == 11);
        CHECK("the game put its own number back once (at the release) and the report says so", r.link->pointsCounters().gameWrites >= 1 && countNotes("the game put its own number 3 into the ball's point value") >= 1);
        CHECK("... and that the line names the time after you let go", findNote("the game put its own number").find("s after you let go") != std::string::npos);
        CHECK("the Aimbot did NOT touch the throw: the ball's speed was never set", gameAt(7) == 0);
        CHECK("... and the throw was not even looked at as a shot", r.link->counters().releases == 0 && r.link->counters().shotLike == 0);
        CHECK("no aim lines about a decision", countNotes("decision:") == 0 && countNotes("SHOT #") == 0);
        CHECK("writes were few (about once per change, not once per tick)", r.link->pointsCounters().writes >= 2 && r.link->pointsCounters().writes <= 6);
        r.link->stop();
    }

    std::printf("== a basket: the pretend scoreboard counts the number the ball carries (11)\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
        r.connectAim(); r.connectPts(); r.run(0.5);
        weakThrow();
        r.run(5.0); r.settle(0.2);
        CHECK("the Aimbot (Direct) fixed the throw and it went in", r.link->counters().aimed == 1 && gameAt(8) == 1);
        CHECK("the pretend scoreboard counted 11 points", score() == 11 && ptsAt(1) == 1);
        CHECK("the menu says Basket! and the number the ball carried", r.link->pointsLastText().find("Basket! The ball carried 11 points when it went in") == 0);
        CHECK("... and that the game wanted 3", r.link->pointsLastText().find("(the game wanted 3)") != std::string::npos);
        CHECK("the facts file has one BASKET line that asks the user to check the scoreboard", countNotes("points: BASKET on ball") == 1 && findNote("points: BASKET").find("did it go up by 11?") != std::string::npos);
        CHECK("the basket was counted once", r.link->pointsCounters().baskets == 1);
        CHECK("the Aimbot's own report still works next to it (result line, SCORED)", countNotes("result") == 1 && findNote("result").find("_shotMade: YES") != std::string::npos);
        r.link->stop();
    }
    std::printf("== every stop of the slider, one basket each\n");
    {
        const int values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 999};
        bool allOk = true; int bad = 0;
        for (int v : values) {
            Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(v);
            r.connectAim(); r.connectPts(); r.run(0.4);
            weakThrow();
            r.run(4.5); r.settle(0.1);
            const bool ok = gameAt(8) == 1 && score() == v && r.link->pointsCounters().baskets == 1;
            if (!ok) { allOk = false; bad = v; std::printf("      (stop %d: made=%.0f score=%.0f)\n", v, gameAt(8), score()); }
            r.link->stop();
        }
        CHECK("a basket counts exactly 1, 2, 3 ... 11 and 999 points", allOk);
        if (!allOk) std::printf("      first bad stop: %d\n", bad);
    }
    std::printf("== the game writes its own number AGAIN later in the flight: ours is put back before the ball goes in\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(9);
        pts_set("rewrite", 40, 2);        // 40 physics steps after the release the game writes 2 again
        r.connectAim(); r.connectPts(); r.run(0.4);
        weakThrow();
        r.run(5.0); r.settle(0.1);
        CHECK("the basket counted 9, not 2", gameAt(8) == 1 && score() == 9);
        CHECK("the report names both game writes (3 at the release, 2 later)", countNotes("the game put its own number 3 into") >= 1 && countNotes("the game put its own number 2 into") >= 1);
        r.link->stop();
    }
    std::printf("== a game that copies the number at the release (so changing it later cannot work): nothing breaks, and the report stays honest\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
        pts_set("mode", 1, 0);
        r.connectAim(); r.connectPts(); r.run(0.4);
        weakThrow();
        r.run(5.0); r.settle(0.1);
        CHECK("the pretend scoreboard counted the game's own 3 (our number was ignored)", gameAt(8) == 1 && score() == 3);
        CHECK("the points part still shows connected, no crash, no errors", r.link->pointsUiState() == 1 && r.link->counters().errors == 0);
        CHECK("the menu text says what the BALL carried, nothing about the scoreboard", r.link->pointsLastText().find("Basket! The ball carried 11 points") == 0);
        CHECK("the facts file asks whether the score went up by 11 (so the user's answer shows the truth)", findNote("points: BASKET").find("did it go up by 11?") != std::string::npos);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
        pts_set("mode", 2, 0);            // a game that ignores the ball's number altogether
        r.connectAim(); r.connectPts(); r.run(0.4);
        weakThrow();
        r.run(5.0); r.settle(0.1);
        CHECK("a game that ignores the number: the scoreboard says 3, nothing breaks", gameAt(8) == 1 && score() == 3 && r.link->pointsUiState() == 1);
        r.link->stop();
    }

    std::printf("== the Aimbot and the points are independent\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(0);
        r.connectAim(); r.run(0.4);
        weakThrow();
        r.run(5.0); r.settle(0.1);
        CHECK("Aimbot on, points off: the shot is aimed, scores the game's own 3, and no points line exists", r.link->counters().aimed == 1 && score() == 3 && countPrefix("points:") == 0);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
        r.connectAim(); r.connectPts(); r.run(0.4);
        weakThrow(); r.run(2.0);
        r.link->setAsk(false, 50);                                 // the Aimbot goes off while the points stay on
        r.run(0.3);
        weakThrow(); r.run(3.0); r.settle(0.1);
        CHECK("Aimbot turned off, points still on: the 2nd throw was not touched (speed set only once, for the 1st)", gameAt(7) == 1 && r.link->counters().aimed == 1);
        CHECK("... and the ball is still kept at 11", myValue() == 11);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
        r.connectAim(); r.connectPts(); r.run(0.4);
        r.link->setPoints(0);                                      // points off while the Aimbot stays on
        r.run(0.4);
        weakThrow(); r.run(5.0); r.settle(0.1);
        CHECK("points turned off, Aimbot still on: the shot is aimed and scores the game's own 3", r.link->counters().aimed == 1 && score() == 3);
        r.link->stop();
    }

    std::printf("== turning the switch off puts the game's number back\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("value", 5, 0);
        r.connectPts(); r.run(0.3);
        CHECK("(on) the held ball has 11", myValue() == 11);
        r.link->setPoints(0);
        r.run(0.3); r.settle(0.1);
        CHECK("(off) the ball has its own 5 again", myValue() == 5);
        CHECK("the facts file says one ball was put back", countNotes("points: switch off - 1 ball put back to the game's own number") == 1);
        CHECK("the menu state is off again", r.link->pointsUiState() == 0);
        r.link->setPoints(4);
        r.run(0.3);
        CHECK("switching on again works (the ball has 4)", myValue() == 4);
        r.link->stop();
    }
    {
        // a value the game has changed since: the point value is not ours any more, so it is left alone when the switch goes off
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("value", 5, 0);
        r.connectPts(); r.run(0.3);
        pts_set("value", 8, 0);                                    // somebody else changes it after our write
        r.link->setPoints(0);
        r.run(0.3);
        CHECK("when the number is not ours any more, switching off leaves it alone (8 stays)", myValue() == 8);
        r.link->stop();
    }

    std::printf("== the slider moves while you hold the ball\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(3);
        r.connectPts(); r.run(0.2);
        CHECK("3 points", myValue() == 3);
        r.link->setPoints(8); r.run(0.1);
        CHECK("moved to 8: the ball has 8 within a tenth of a second", myValue() == 8);
        r.link->setPoints(999); r.run(0.1);
        CHECK("moved to 999", myValue() == 999);
        r.link->setPoints(100000); r.run(0.1);
        CHECK("a silly value is cut to 999", myValue() == 999);
        r.link->setPoints(-4); r.run(0.2);
        CHECK("a negative value means off", r.link->pointsUiState() == 0);
        r.link->stop();
    }

    std::printf("== only YOUR ball: another player's ball is never touched\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_make_other();
        r.connectPts(); r.run(1.0);
        CHECK("your ball has 11, the other ball still has its own 7", myValue() == 11 && otherValue() == 7);
        pts_switch_ball(1);                                         // you pick up the other ball (it is yours now)
        r.run(0.3);
        CHECK("(now yours) the second ball has 11 too, and the first one you let go of is still 11", otherValue() == 11 && myValue() == 11);
        pts_set("value", 4, 0);                                     // the first ball gets its own number back from the game
        r.run(0.5);
        CHECK("the first ball is still kept at 11 while it counts as one of yours (recently held)", myValue() == 11);
        r.run(13.0);                                                // 13 seconds later it is not yours any more
        pts_set("value", 4, 0);
        r.run(0.5);
        CHECK("after 12+ seconds without being held, the first ball is left alone (4 stays)", myValue() == 4 && otherValue() == 11);
        CHECK("two balls were handled in all", r.link->pointsCounters().balls == 2);
        r.link->stop();
    }

    std::printf("== the game's classes are not what we expect: the points part says so and does nothing (the Aimbot is unaffected)\n");
    {
        struct Case { const char* knob; const char* word; };
        const Case cases[] = {{"remove_pointvalue", "_pointValue is missing"}, {"retype_pointvalue", "has type System.Single"}, {"remove_properties", "has no _properties link"}, {"retype_properties", "_properties has type"}};
        for (const Case& c : cases) {
            Rig r; pts_set(c.knob, 1, 0);
            r.start(); r.link->setAsk(true, 50); r.link->setPoints(11);
            pts_set("value", 5, 0);
            r.connectAim(); r.run(1.0);
            weakThrow();
            r.run(5.0); r.settle(0.2);
            char name[200];
            std::snprintf(name, sizeof name, "[%s] the points part is FAILED with a clear reason (%s)", c.knob, c.word);
            CHECK(name, r.link->pointsUiState() == 3 && r.link->pointsHeadline().find("FAILED") == 0 && r.link->pointsHeadline().find(c.word) != std::string::npos);
            std::snprintf(name, sizeof name, "[%s] nothing was written (the number is still what the game wrote, 3) and the facts file has one 'cannot be used' line", c.knob);
            CHECK(name, myValue() == 3 && countNotes("points: cannot be used") == 1);
            std::snprintf(name, sizeof name, "[%s] the Aimbot still aimed and scored the game's own 3", c.knob);
            CHECK(name, r.link->counters().aimed == 1 && score() == 3);
            r.link->stop();
        }
    }

    std::printf("== trouble with the ball's settings object\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("value", 5, 0);
        r.connectPts(); r.run(0.3);
        pts_set("destroy_props", 1, 0);                             // the game destroyed the settings object (new scene)
        pts_set("value", 6, 0);
        r.run(1.0);
        CHECK("a destroyed settings object is not written to (still 6) and nothing crashes", myValue() == 6 && r.link->pointsCounters().readFails >= 1);
        CHECK("... and it is only looked at again about twice a second (not 185 times)", r.link->pointsCounters().readFails <= 4);
        CHECK("... the facts file says so once", countNotes("cannot be read right now") == 1);
        pts_set("destroy_props", 0, 0);
        r.run(1.0);
        CHECK("when the object is alive again the ball is kept at 11 again", myValue() == 11);
        pts_set("props_null", 1, 0);
        r.run(1.0);
        CHECK("an empty _properties link is no problem", r.link->pointsUiState() == 1);
        pts_set("props_null", 0, 0);
        r.run(1.0);
        CHECK("... and it recovers", myValue() == 11);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        r.connectPts(); r.run(0.3);
        pts_set("value", 3, 0);                                     // (the copy on the read-only page must differ from the number we want, or there is nothing to write)
        pts_set("props_readonly", 1, 0);                            // the system refuses writes into the settings object
        r.run(1.0); r.settle(0.1);
        CHECK("when the system refuses the write, it gives up after 5 tries and says FAILED", r.link->pointsUiState() == 3 && r.link->pointsHeadline().find("refused my write") != std::string::npos);
        CHECK("... with exactly 5 refused writes counted, and a line in the facts file", r.link->pointsCounters().writeFails == 5 && countNotes("points: STOPPED: five writes in a row") == 1);
        const unsigned long long fails = r.link->pointsCounters().writeFails;
        r.run(1.0);
        CHECK("... and it does not keep trying", r.link->pointsCounters().writeFails == fails);
        r.link->setPoints(0); pts_set("props_normal", 1, 0); r.run(0.3);
        r.link->setPoints(11); r.run(0.5);
        CHECK("turning the switch off and on again tries again and works", r.link->pointsUiState() == 1 && myValue() == 11);
        r.link->stop();
    }

    std::printf("== not on the game's own thread: nothing happens\n");
    {
        Rig r("UnityMain"); r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("value", 5, 0);
        r.run(1.0);
        CHECK("a doorway call from some other thread is ignored: the ball is untouched", myValue() == 5 && gameAt(6) == 0);
        r.link->stop();
    }

    std::printf("== the facts file stays small\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50); r.link->setPoints(11);
        pts_set("rewrite", 10, 2);
        r.connectPts(); r.run(0.3);
        for (int i = 0; i < 40; ++i) { weakThrow(); r.run(1.0); }
        r.settle(0.2);
        CHECK("40 throws, the game rewriting its number every time: not more than 75 points lines in all", countPrefix("points:") <= 75);
        CHECK("... and the first-look / game-write notes stop at 40", countNotes("the game put its own number") + countNotes("first look at a ball") + countNotes("points: BASKET") <= 41);
        r.link->stop();
    }

    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed ? 1 : 0;
}
