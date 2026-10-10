// PC test of the aim link (aim_link.cpp) against the PRETEND game (fake_il2cpp_aim.cpp): a pretend ball with real physics, a pretend ball control object with release
// timers, pretend engine calls. It proves that the link catches a release, decides, works out the speed, sets it, watches the flight and reports - and that it
// leaves everything alone when it must. It does NOT prove anything about the real game.
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

// ---- the pretend game
static void* gLib = nullptr;
static void (*fake_create)(int) = nullptr;
static void* (*fake_object)(int) = nullptr;
static void (*fake_set)(const char*, double, double, double) = nullptr;
static void (*fake_hold)(double, double, double) = nullptr;
static void (*fake_release)(int, double, double, double) = nullptr;
static void (*fake_step)() = nullptr;
static void (*fake_state)(double*) = nullptr;
static void* (*fake_extra_gm)(int) = nullptr;
static double (*fake_fdt)() = nullptr;

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
static size_t noteCount() { std::lock_guard<std::mutex> lock(gNoteMu); return gNotes.size(); }
static void clearNotes() { std::lock_guard<std::mutex> lock(gNoteMu); gNotes.clear(); }
// the number written just before `after` in `line` ("... = 0.032 m from the hoop centre" -> 0.032)
static double numberBefore(const std::string& line, const char* after) {
    const size_t p = line.find(after);
    if (p == std::string::npos) return -1e9;
    size_t e = p; while (e > 0 && line[e - 1] == ' ') --e;
    size_t b = e; while (b > 0 && (std::isdigit(static_cast<unsigned char>(line[b - 1])) || line[b - 1] == '.' || line[b - 1] == '-')) --b;
    return std::atof(line.substr(b, e - b).c_str());
}

static double gClock = 1000.0;
static double fakeClock() { return gClock; }

struct Rig {
    std::unique_ptr<AimLink> link;
    double physAcc = 0;
    bool yFeed = false, yHeld = false;     // D8c: report the pretend Y button to the link on every tick (like the controller doorway does)
    Config cfg;
    explicit Rig(bool hoops = true, const char* threadName = "", bool allowWrite = true) {
        fake_create(hoops ? 1 : 0);
        clearNotes();
        link.reset(new AimLink());
        cfg.provider = provider; cfg.note = note; cfg.linkTickMs = 2; cfg.gameThreadName = threadName; cfg.clock = fakeClock; cfg.allowWrite = allowWrite;
        cfg.searchSeconds = 20; cfg.stallSeconds = 5; cfg.retrySoonSeconds = 0.05; cfg.minSearchGapSeconds = 0.05;
    }
    bool start() { return link->start(cfg); }
    void tick() {
        const double dt = 1.0 / 185.0;
        gClock += dt; physAcc += dt;
        const double fdt = fake_fdt();
        while (physAcc >= fdt) { fake_step(); physAcc -= fdt; }
        if (yFeed) link->noteY(yHeld, true);
        link->onGameThread();
        usleep(120);
    }
    void run(double seconds) { const int n = static_cast<int>(seconds * 185.0); for (int i = 0; i < n; ++i) tick(); }
    // run until the link is connected (bounded)
    bool connect(double maxSeconds = 8.0) {
        for (int i = 0; i < static_cast<int>(maxSeconds * 185.0); ++i) { tick(); if (link->uiState() == 1) return true; if (link->uiState() == 3) return false; }
        return false;
    }
    bool settle(double seconds) { run(seconds); usleep(30000); return true; }
};

static void state(double* s) { fake_state(s); }
static double stateAt(int i) { double s[10]; state(s); return s[i]; }

// a throw from `from` towards the flat direction of `to` with an upward angle and a speed; `offDeg` turns it sideways
static void throwFrom(double fx, double fy, double fz, double tx, double tz, double elevDeg, double speed, double offDeg, int hand = 2) {
    const double dx = tx - fx, dz = tz - fz;
    double ang = std::atan2(dx, dz) + offDeg * 3.14159265358979 / 180.0;
    const double e = elevDeg * 3.14159265358979 / 180.0;
    fake_hold(fx, fy, fz);
    fake_release(hand, std::sin(ang) * speed * std::cos(e), speed * std::sin(e), std::cos(ang) * speed * std::cos(e));
}

int main(int, char** argv) {
    gLib = dlopen(argv[1], RTLD_NOW);
    if (!gLib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    fake_create = reinterpret_cast<void (*)(int)>(dlsym(gLib, "fake_aim_create"));
    fake_object = reinterpret_cast<void* (*)(int)>(dlsym(gLib, "fake_aim_object"));
    fake_set = reinterpret_cast<void (*)(const char*, double, double, double)>(dlsym(gLib, "fake_aim_set"));
    fake_hold = reinterpret_cast<void (*)(double, double, double)>(dlsym(gLib, "fake_aim_hold"));
    fake_release = reinterpret_cast<void (*)(int, double, double, double)>(dlsym(gLib, "fake_aim_release"));
    fake_step = reinterpret_cast<void (*)()>(dlsym(gLib, "fake_aim_step"));
    fake_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_aim_state"));
    fake_extra_gm = reinterpret_cast<void* (*)(int)>(dlsym(gLib, "fake_aim_extra_gm"));
    fake_fdt = reinterpret_cast<double (*)()>(dlsym(gLib, "fake_aim_fdt"));

    std::printf("== the switch is off: the link touches nothing\n");
    {
        Rig r; r.start(); r.link->setAsk(false, 50);
        r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9, 5);
        r.run(3.0);
        double s[10]; state(s);
        CHECK("not a single call into the game while the switch is off", s[6] == 0);
        CHECK("the menu state is 'off'", r.link->uiState() == 0 && r.link->headline() == "Aimbot is off");
        CHECK("no release was even looked at", r.link->counters().releases == 0);
        r.link->stop();
    }

    std::printf("== switch on: the link connects by asking the game\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        const bool ok = r.connect();
        r.run(0.3);
        CHECK("it connects (state 1)", ok && r.link->uiState() == 1);
        CHECK("the facts file says how it found your ball control (asking the game)", countNotes("by asking the game") == 1);
        CHECK("the facts file lists the classes it found", countNotes("found the game's classes") == 1);
        CHECK("headline says connected", r.link->headline().find("connected") == 0);
        r.link->stop();
    }

    std::printf("== an aimed shot: the ball really goes into the hoop\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);          // thrown 6 degrees off and too weak: it would miss
        r.run(5.0);
        r.settle(0.2);
        double s[10]; state(s);
        const Counters c = r.link->counters();
        CHECK("one release seen, it was a shot, it was aimed", c.releases == 1 && c.shotLike == 1 && c.aimed == 1);
        CHECK("the ball's speed was set exactly once", s[7] == 1);
        CHECK("the pretend game says the shot went in (_shotMade)", s[8] == 1);
        const std::string res = findNote("result");
        CHECK("the report says it came down through the ring height", res.find("came down through the ring height") != std::string::npos);
        const double miss = numberBefore(res, " m from the hoop centre");
        std::printf("       (measured miss: %.4f m)\n", miss);
        CHECK("the ball crossed the ring within 3 cm of its centre", miss >= 0 && miss < 0.03);
        CHECK("the report says the game kept our speed", res.find("our speed was kept by the game") != std::string::npos);
        CHECK("the report says SCORED / YES", res.find("_shotMade: YES") != std::string::npos);
        const std::string dec = findNote("decision: AIM");
        CHECK("the decision line shows what was worked out, set and read back", dec.find("worked out: speed") != std::string::npos && dec.find("read back") != std::string::npos && dec.find("= kept") != std::string::npos);
        CHECK("the decision line shows the physics numbers that were read from the game", dec.find("gravity 9.81") != std::string::npos && dec.find("drag 0.110") != std::string::npos && dec.find("step 0.0139") != std::string::npos);
        CHECK("the decision line says which game mode it is", dec.find("game: state=3") != std::string::npos);
        CHECK("the flight-samples line exists and its errors are tiny", countNotes("flight samples") == 1 && findNote("flight samples").find("err 0.0") != std::string::npos);
        CHECK("the menu text for the last shot says AIMED and SCORED", r.link->lastShotText().find("AIMED") != std::string::npos && r.link->lastShotText().find("SCORED") != std::string::npos);
        r.link->stop();
    }

    std::printf("== the distance cap: farther than the cap = the aimbot does not activate\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 5);              // 5 m cap, the hoop is ~9 m away
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        double s[10]; state(s);
        const Counters c = r.link->counters();
        CHECK("it saw a shot and left it alone: too far", c.shotLike == 1 && c.tooFar == 1 && c.aimed == 0);
        CHECK("the ball's speed was never set", s[7] == 0);
        CHECK("the report says the aimbot does not activate", countNotes("LEFT ALONE (the aimbot does not activate)") == 1);
        CHECK("the menu text says why", r.link->lastShotText().find("farther than your 5 m limit") != std::string::npos);
        CHECK("the unchanged throw is still watched (so the facts show how the game's own ball flies)", countNotes("result") == 1);
        r.link->stop();
    }
    {
        // the cap decision follows the flat distance from the ball to the hoop: 8.99 m away, a 9 m cap = allowed (at the cap is allowed); a 8 m cap = refused
        for (int cap : {9, 8}) {
            Rig r; r.start(); r.link->setAsk(true, static_cast<float>(cap));
            r.connect(); r.run(0.5);
            throwFrom(0, 1.6, 12.66 - 8.99, 0, 12.66, 55, 9.0, 0.0);
            r.run(4.0); r.settle(0.2);
            const Counters c = r.link->counters();
            if (cap == 9) CHECK("8.99 m away with a 9 m cap: aimed", c.aimed == 1 && c.tooFar == 0);
            else CHECK("8.99 m away with an 8 m cap: left alone", c.aimed == 0 && c.tooFar == 1);
            r.link->stop();
        }
    }
    std::printf("== very long shots (Unlimited)\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);               // 50 = Unlimited
        r.connect(); r.run(0.5);
        throwFrom(-44.0, 1.6, 12.66, 0, 12.66, 50, 15.0, 0.0);        // 44 m from the north hoop (from the side: from behind the other hoop its backboard would block the ball), thrown far too weakly
        r.run(8.0); r.settle(0.2);
        double s[10]; state(s);
        CHECK("44 m with Unlimited: aimed and scored", r.link->counters().aimed == 1 && s[8] == 1);
        const double miss = numberBefore(findNote("result"), " m from the hoop centre");
        std::printf("       (44 m shot: miss %.4f m)\n", miss);
        CHECK("... within 3 cm of the ring's centre", miss >= 0 && miss < 0.03);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 40);               // a 40 m cap: the 44 m shot must be left alone
        r.connect(); r.run(0.5);
        throwFrom(-44.0, 1.6, 12.66, 0, 12.66, 50, 15.0, 0.0);       // from the side: the north hoop is 44 m away exactly in line, the south hoop is 30 degrees off
        r.run(8.0); r.settle(0.2);
        CHECK("44 m with a 40 m cap: left alone (too far)", r.link->counters().aimed == 0 && r.link->counters().tooFar == 1);
        r.link->stop();
    }

    std::printf("== throws that are NOT shots, wrong directions, missing information\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 5, 10.0, 0.0);              // a flat pass
        r.run(2.0);
        throwFrom(2, 1.6, 4, 0, 12.66, -60, 3.0, 0.0);            // a drop / dribble push
        r.run(1.0);
        double s[10]; state(s);
        CHECK("a flat pass and a downward push are left alone", s[7] == 0 && r.link->counters().aimed == 0 && r.link->counters().notAShot == 2);
        throwFrom(0, 1.6, 0, 20, 0, 55, 9.0, 0.0);                 // a shot along the x axis: no hoop in that direction
        r.run(3.0); r.settle(0.2);
        CHECK("a shot where no hoop is in the throw direction is left alone", r.link->counters().wrongDirection == 1 && r.link->counters().aimed == 0);
        r.link->stop();
    }
    {
        Rig r(false); r.start(); r.link->setAsk(true, 50);          // the ball knows no hoop positions
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(2.0); r.settle(0.2);
        CHECK("no hoop positions known: nothing is changed, and the report says so", r.link->counters().noHoop == 1 && r.link->counters().aimed == 0 && countNotes("knows no hoop") >= 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        fake_set("unheld_mode", 1, 0, 0);                               // _unheldTime never counts up (the ball "looks held")
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(2.0); r.settle(0.2);
        double s[10]; state(s);
        CHECK("a ball that still looks held is never touched", s[7] == 0 && r.link->counters().aimed == 0);
        r.link->stop();
    }

    std::printf("== left hand, both hands, shot after shot\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(-2, 1.6, 3, 0, 12.66, 55, 9.0, -5.0, 1);       // left hand
        r.run(4.5);
        throwFrom(3, 1.6, 2, 0, 12.66, 60, 10.0, 4.0, 3);        // both hands
        r.run(4.5); r.settle(0.2);
        const Counters c = r.link->counters();
        CHECK("a left-hand release and a two-hand release are both caught and aimed", c.releases == 2 && c.aimed == 2);
        CHECK("... and both went in", c.scored == 2);
        CHECK("shot numbers count up", countNotes("SHOT #1") >= 1 && countNotes("SHOT #2") >= 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(1.0);                                                    // still flying
        throwFrom(-2, 1.6, 3, 0, 12.66, 55, 9.0, -5.0, 1);           // a new release while the first is being watched
        r.run(4.0); r.settle(0.2);
        CHECK("a new release during a flight ends the old report and starts a new shot", r.link->counters().releases == 2 && r.link->counters().aimed == 2 && countNotes("a new release started") >= 1);
        r.link->stop();
    }

    std::printf("== the game fights back / the game's physics is different\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        fake_set("override", 6, 3.0, 4.0);                              // 6 physics steps after the release the game sets its own velocity again
        fake_set("override_vz", 2.0, 0, 0);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(6.5); r.settle(0.2);
        CHECK("the game putting its own speed back is noticed and reported", r.link->counters().overridden == 1 && countNotes("THE GAME CHANGED OUR SPEED") == 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        fake_set("drag", 0.3, 0, 0); fake_set("gravity", 12.0, 0, 0); fake_set("fdt", 0.02, 0, 0);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        double s[10]; state(s);
        const std::string dec = findNote("decision: AIM");
        CHECK("it uses the drag, gravity and step the GAME reports (0.3, 12, 0.02), not its own guesses", dec.find("gravity 12.00") != std::string::npos && dec.find("drag 0.300") != std::string::npos && dec.find("step 0.0200") != std::string::npos);
        CHECK("... and the ball still goes in", s[8] == 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        fake_set("usegravity", 0, 0, 0);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(2.0); r.settle(0.2);
        double s[10]; state(s);
        CHECK("a ball that does not use the engine's gravity is left alone", s[7] == 0 && r.link->counters().refused == 1);
        r.link->stop();
    }

    std::printf("== errors inside the game never crash us\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        fake_set("throw_all", 1, 0, 0);                                 // every call into the game now raises an error
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(2.0); r.settle(0.2);
        double s0[10]; state(s0);
        r.run(2.0);
        double s1[10]; state(s1);
        CHECK("after three errors in a row the link stops touching the game", r.link->uiState() == 3 && r.link->failReason().find("three calls in a row") != std::string::npos);
        CHECK("... and makes no further calls into the game", s1[6] == s0[6]);
        CHECK("the facts file says it stopped", countNotes("STOPPED touching the game") == 1);
        // turning the switch off and on again gives it a fresh start
        fake_set("throw_all", 0, 0, 0);
        r.link->setAsk(false, 50); r.run(0.2); r.link->setAsk(true, 50); r.run(1.0);
        CHECK("switching off and on again clears the stop", r.link->uiState() == 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(0.5);                                                     // the flight is being watched
        fake_set("destroy_rb", 1, 0, 0);                                // the game destroys the ball's physics body
        r.run(2.0); r.settle(0.2);
        CHECK("a physics body destroyed during the flight ends the watching quietly", r.link->uiState() != 3);
        fake_set("destroy_rb", 0, 0, 0);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        fake_set("destroy_bcm", 1, 0, 0);                               // new scene: the ball control object is destroyed
        r.run(0.5);
        CHECK("a destroyed ball control object is noticed (state goes back to looking)", r.link->uiState() == 2 && countNotes("lost your ball control object") == 1);
        fake_set("destroy_bcm", 0, 0, 0);
        r.link->stop();
    }

    std::printf("== the switch goes off in the middle of a shot\n");
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.link->setAsk(false, 50);                                      // off before the link looked at the ball
        r.run(3.0);
        double s[10]; state(s);
        CHECK("switch off right after the release: the speed is never set", s[7] == 0);
        r.link->stop();
    }

    if (!std::getenv("AIMTEST_SKIP_SEARCH")) {      // (the memory search reads arbitrary memory: the address / thread checkers rightly cannot run it)
    std::printf("== finding the ball control by searching memory (when asking the game does not work)\n");
    {
        Rig r; r.start();
        fake_set("get_instance_null", 1, 0, 0);
        r.link->setAsk(true, 50);
        const bool ok = r.connect(12.0);
        r.run(0.3);
        CHECK("it falls back to the memory search and still connects", ok && r.link->uiState() == 1);
        CHECK("the facts file says it was found by the memory search", countNotes("found by the memory search") == 1 && countNotes("usable ball controls") >= 1);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(4.5); r.settle(0.2);
        CHECK("... and the aimed shot works the same", r.link->counters().aimed == 1 && stateAt(8) == 1);
        r.link->stop();
    }
    {
        Rig r; r.start();
        fake_set("get_instance_null", 1, 0, 0);
        fake_extra_gm(0);                                              // a junk copy of the GameManager (no ball control link): must be ignored
        r.link->setAsk(true, 50);
        const bool ok = r.connect(12.0);
        CHECK("a junk GameManager copy is ignored", ok && r.link->uiState() == 1);
        r.link->stop();
    }
    {
        Rig r; r.start();
        fake_set("get_instance_null", 1, 0, 0);
        fake_extra_gm(1);                                              // a second GameManager that points at ANOTHER valid ball control object
        r.link->setAsk(true, 50);
        r.connect(12.0);
        CHECK("two different candidate ball controls: it refuses to guess", r.link->uiState() == 3 && r.link->failReason().find("cannot tell which one is yours") != std::string::npos);
        r.link->stop();
    }

    }

    std::printf("== other threads and the game being different\n");
    {
        Rig r(true, "UnityMain"); r.start(); r.link->setAsk(true, 50);         // only a thread called UnityMain may act; this test thread is called something else
        r.run(2.0); usleep(30000);
        double s[10]; state(s);
        CHECK("a call from a thread that is not the game's script thread does nothing at all", s[6] == 0 && r.link->counters().ticks == 0);
        r.link->stop();
    }
    {
        Rig r(true, "", false); r.start(); r.link->setAsk(true, 50);          // read-only mode
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(4.0); r.settle(0.2);
        CHECK("with writing switched off the speed is never set, but the link says what it WOULD do", stateAt(7) == 0 && countNotes("WOULD AIM") == 1);
        r.link->stop();
    }
    {
        fake_create(1);
        fake_set("break_rigidbody_type", 1, 0, 0);                    // the game was updated: the _rigidbody field now has another type
        clearNotes();
        AimLink link; Config cfg; cfg.provider = provider; cfg.note = note; cfg.linkTickMs = 2; cfg.gameThreadName = ""; cfg.clock = fakeClock;
        link.start(cfg); link.setAsk(true, 50);
        for (int i = 0; i < 300 && link.uiState() != 3; ++i) { gClock += 1.0 / 185; link.onGameThread(); usleep(2000); }
        CHECK("a field with the wrong type stops the link with a clear reason", link.uiState() == 3 && link.failReason().find("_rigidbody") != std::string::npos && link.failReason().find("expected UnityEngine.Rigidbody") != std::string::npos);
        link.stop();
    }
    {
        // the ball control class loses all three release timers (fields 4,5,6): the ball's own _unheldTime still tells us about a release
        Rig r; r.start();
        fake_set("remove_field", 4, 0, 0); fake_set("remove_field", 4, 0, 0); fake_set("remove_field", 4, 0, 0);
        r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(4.5); r.settle(0.2);
        CHECK("without the ball control's timers the release is still noticed (by the ball's own unheld timer) and the shot is aimed", r.link->counters().aimed == 1 && stateAt(8) == 1 && countNotes("noticed by: ball's unheld timer started") >= 1);
        r.link->stop();
    }
    {
        fake_create(1);
        clearNotes();
        fake_set("remove_field", 4, 0, 0); fake_set("remove_field", 4, 0, 0); fake_set("remove_field", 4, 0, 0);     // no timers in the ball control class ...
        fake_set("remove_field", 17, 1, 0);                                                                           // ... and no _unheldTime in the ball
        AimLink link; Config cfg; cfg.provider = provider; cfg.note = note; cfg.linkTickMs = 2; cfg.gameThreadName = ""; cfg.clock = fakeClock;
        link.start(cfg); link.setAsk(true, 50);
        for (int i = 0; i < 300 && link.uiState() != 3; ++i) { gClock += 1.0 / 185; link.onGameThread(); usleep(2000); }
        CHECK("no way at all to notice a release: the link says so and does nothing", link.uiState() == 3 && link.failReason().find("release timers") != std::string::npos);
        link.stop();
    }
    {
        // the timers move but the ball never says "unheld" (a different game): the timers alone are enough, and the ball is not touched while it looks held
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(4.5); r.settle(0.2);
        CHECK("(both ways to notice a release agree: one SHOT, not two)", r.link->counters().releases == 1);
        r.link->stop();
    }

    std::printf("== stage D8c: 'Hold Y to aim' (the Y button is read at the moment of the release)\n");
    {
        // Y held when you let go: aimed
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        r.yHeld = true; r.run(0.1);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        double s[10]; state(s);
        const Counters c = r.link->counters();
        CHECK("Y held: it was a shot, it was aimed and scored, nothing was left alone", c.shotLike == 1 && c.aimed == 1 && c.notHoldingY == 0 && c.yUnknown == 0 && s[7] == 1 && s[8] == 1);
        const std::string dec = findNote("decision: AIM");
        CHECK("the report says Y was held and that the switch is ON", dec.find("Y button: HELD when you let go") != std::string::npos && dec.find("hold Y to aim: ON") != std::string::npos);
        CHECK("the menu text says it was aimed", r.link->lastShotText().find("AIMED") != std::string::npos);
        CHECK("the summary line counts the Y readings", r.link->summary().find("doorway") != std::string::npos && c.yReadsDoorway > 100 && c.yHeldDoorway > 10);
        r.link->stop();
    }
    {
        // Y NOT held: the shot is left alone, the speed is never set, the flight is still watched
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        double s[10]; state(s);
        const Counters c = r.link->counters();
        CHECK("Y not held: it was a shot, it was left alone, the speed was never set", c.shotLike == 1 && c.notHoldingY == 1 && c.aimed == 0 && s[7] == 0);
        const std::string dec = findNote("decision: LEFT ALONE");
        CHECK("the report says Y was not held (never seen held)", dec.find("Y button: NOT held (never seen held in this session)") != std::string::npos && dec.find("the Y button was not held") != std::string::npos);
        CHECK("the menu text says to hold Y", r.link->lastShotText().find("hold Y to aim") != std::string::npos);
        CHECK("the unchanged throw is still watched", countNotes("result") == 1);
        CHECK("the summary line counts it", r.link->summary().find("Y not held 1") != std::string::npos);
        r.link->stop();
    }
    {
        // the switch is OFF: every shot is aimed, Y is only reported
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, false);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        const std::string dec = findNote("decision: AIM");
        CHECK("'hold Y to aim' off: a shot without Y is aimed", r.link->counters().aimed == 1 && r.link->counters().notHoldingY == 0);
        CHECK("... and the report still shows the Y state and that the switch is off", dec.find("Y button: NOT held") != std::string::npos && dec.find("hold Y to aim: off") != std::string::npos);
        r.link->stop();
    }
    {
        // Y let go a moment BEFORE the ball (0.05 s): counts. 0.30 s before: does not.
        for (int pass = 0; pass < 2; ++pass) {
            Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
            r.connect(); r.run(0.5);
            r.yHeld = true; r.run(0.2);
            r.yHeld = false; r.run(pass == 0 ? 0.05 : 0.30);
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            r.run(5.0); r.settle(0.2);
            const Counters c = r.link->counters();
            if (pass == 0) {
                CHECK("Y let go 50 ms before the release: still counts, aimed", c.aimed == 1 && c.notHoldingY == 0);
                CHECK("... and the report says so", findNote("decision: AIM").find("let go") != std::string::npos);
            } else {
                CHECK("Y let go 300 ms before the release: does not count, left alone", c.aimed == 0 && c.notHoldingY == 1);
                CHECK("... and the report says when Y was last seen held", findNote("decision: LEFT ALONE").find("last seen held 0.3 s before") != std::string::npos);
            }
            r.link->stop();
        }
    }
    {
        // Y pressed right after you let go (before the decision, 40 ms): counts
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.tick(); r.tick();
        r.yHeld = true;
        r.run(5.0); r.settle(0.2);
        CHECK("Y pressed right after the release: aimed", r.link->counters().aimed == 1 && r.link->counters().notHoldingY == 0 && findNote("decision: AIM").find("pressed right after") != std::string::npos);
        r.link->stop();
    }
    {
        // the menu's thread is the other reader: it alone is enough
        Rig r; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        for (int i = 0; i < 30; ++i) { r.link->noteY(true, false); r.run(0.02); }
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        for (int i = 0; i < 1000; ++i) { r.link->noteY(true, false); r.run(0.005); }
        r.settle(0.2);
        const Counters c = r.link->counters();
        CHECK("Y read only by the menu's thread: aimed, and its reads are counted separately", c.aimed == 1 && c.yReadsProbe > 100 && c.yHeldProbe > 100 && c.yReadsDoorway == 0);
        r.link->stop();
    }
    {
        // the two readers disagree for the whole shot (the doorway says "no Y", the menu's thread says "Y held"): held wins, in both orders
        for (int order = 0; order < 2; ++order) {
            Rig r; r.yFeed = true; r.yHeld = false; r.start(); r.link->setAsk(true, 50, true);       // the doorway reports "not held" on every tick
            r.connect(); r.run(0.5);
            for (int i = 0; i < 40; ++i) { if (order == 0) r.link->noteY(true, false); r.tick(); if (order == 1) r.link->noteY(true, false); }
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            for (int i = 0; i < 1000; ++i) { if (order == 0) r.link->noteY(true, false); r.tick(); if (order == 1) r.link->noteY(true, false); }
            r.settle(0.2);
            CHECK(order == 0 ? "the doorway says 'no Y' but the menu's thread says 'held' (doorway reads last): aimed" : "... same, the menu's thread reads last: aimed", r.link->counters().aimed == 1 && r.link->counters().notHoldingY == 0);
            r.link->stop();
        }
    }
    {
        // dribbles and drops are not shots: they never count as "Y not held"
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        for (int i = 0; i < 6; ++i) { throwFrom(2, 1.6, 4, 0, 12.66, -60, 2.0, 0.0); r.run(0.6); }
        r.settle(0.3);
        CHECK("dribbles with 'hold Y to aim' ON: not counted as left-alone shots", r.link->counters().releases == 6 && r.link->counters().notHoldingY == 0 && r.link->counters().shotLike == 0);
        r.link->stop();
    }
    {
        // a Y button that is never read must never be guessed: ON = left alone and says why; off = the shot is aimed as usual
        for (int gate = 1; gate >= 0; --gate) {
            Rig r; r.yFeed = false; r.start(); r.link->setAsk(true, 50, gate == 1);       // nobody reports the Y button
            r.connect(); r.run(0.5);
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            r.run(5.0); r.settle(0.2);
            const Counters c = r.link->counters();
            if (gate) {
                CHECK("Y never read, 'hold Y to aim' ON: left alone, says it cannot read the Y button", c.aimed == 0 && c.yUnknown == 1 && c.notHoldingY == 0 && findNote("decision: LEFT ALONE").find("Y button: NOT read") != std::string::npos && r.link->lastShotText().find("cannot read the Y button") != std::string::npos);
            } else {
                CHECK("Y never read, 'hold Y to aim' off: aimed as usual", c.aimed == 1 && c.yUnknown == 0);
            }
            r.link->stop();
        }
    }
    {
        // the readings stop (the menu's thread died?): after one second they no longer count, even though the last one said "held"
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.5);
        r.yHeld = true; r.run(0.2);
        r.yFeed = false; r.run(1.3);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        CHECK("an old 'Y held' reading (1.3 s old) is not trusted: left alone as unreadable", r.link->counters().aimed == 0 && r.link->counters().yUnknown == 1);
        r.link->stop();
    }
    {
        // Y does not override the other rules: too far / wrong direction are still left alone
        Rig r; r.yFeed = true; r.yHeld = true; r.start(); r.link->setAsk(true, 5, true);
        r.connect(); r.run(0.5);
        throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
        r.run(5.0); r.settle(0.2);
        CHECK("Y held but the hoop is farther than the 5 m limit: left alone as too far", r.link->counters().aimed == 0 && r.link->counters().tooFar == 1 && r.link->counters().notHoldingY == 0);
        r.link->stop();
    }
    {
        // the player's jump state no longer matters (it is only reported)
        for (int v : {0, 1}) {
            Rig r; r.yFeed = true; r.yHeld = true; r.start(); r.link->setAsk(true, 50, true);
            r.connect(); r.run(0.5);
            fake_set("vstate", v, 0, 0);
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            r.run(5.0); r.settle(0.2);
            char name[160]; std::snprintf(name, sizeof name, "Y held, player %s: aimed either way", v ? "in the air" : "on the floor");
            CHECK(name, r.link->counters().aimed == 1);
            CHECK("... and the report still shows the jump state", findNote("decision: AIM").find(v ? "IN THE AIR" : "ON THE FLOOR") != std::string::npos);
            r.link->stop();
        }
        for (const char* key : {"loco_link_null", "destroy_loco"}) {
            Rig r; r.yFeed = true; r.yHeld = true; r.start(); r.link->setAsk(true, 50, true);
            r.connect(); r.run(0.5);
            fake_set(key, 1, 0, 0);
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            r.run(5.0); r.settle(0.2);
            char name[160]; std::snprintf(name, sizeof name, "%s: the jump state cannot be read, but with Y held the shot is still aimed", key);
            CHECK(name, r.link->counters().aimed == 1 && r.link->counters().yUnknown == 0 && findNote("decision: AIM").find("jump: NOT read") != std::string::npos);
            r.link->stop();
        }
        for (const char* key : {"break_vert_type", "remove_loco_vert"}) {
            Rig r; fake_set(key, 0, 0, 0); r.yFeed = true; r.yHeld = true; r.start(); r.link->setAsk(true, 50, true);
            const bool ok = r.connect(); r.run(0.5);
            throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0);
            r.run(5.0); r.settle(0.2);
            char name[200]; std::snprintf(name, sizeof name, "%s: it still connects, says the jump state is not readable (report only), and aims with Y held", key);
            CHECK(name, ok && countNotes("jump state NOT readable") == 1 && r.link->counters().aimed == 1);
            r.link->stop();
        }
    }
    {
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(true, 50, true);
        r.connect(); r.run(0.3);
        r.link->setAsk(true, 50, false); r.link->setAsk(true, 50, true);
        CHECK("switching 'hold Y to aim' while the Aimbot is on is written down", countNotes("'hold Y to aim' turned off") == 1 && countNotes("'hold Y to aim' turned ON") == 1);
        r.link->stop();
    }
    {
        // the switch off: the Y readings are ignored (nothing is counted, nothing is stored)
        Rig r; r.yFeed = true; r.yHeld = true; r.start(); r.link->setAsk(false, 50, true);
        r.run(0.5);
        CHECK("Aimbot off: the Y readings are ignored", r.link->counters().yReadsDoorway == 0 && r.link->counters().yReadsProbe == 0);
        r.link->stop();
    }

    std::printf("== the facts file stays small\n");
    {
        Rig r; r.cfg.maxReportLines = 20; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        for (int i = 0; i < 12; ++i) { throwFrom(2, 1.6, 4, 0, 12.66, 55, 9.0, 6.0); r.run(2.5); }
        r.settle(0.3);
        CHECK("no more lines than allowed (plus the one 'enough' line)", noteCount() <= 21 && countNotes("that is enough aim lines") == 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(true, 50);
        r.connect(); r.run(0.5);
        for (int i = 0; i < 30; ++i) { throwFrom(2, 1.6, 4, 0, 12.66, -60, 2.0, 0.0); r.run(0.6); }   // 30 dribble-like releases
        r.settle(0.3);
        CHECK("30 dribble releases: all counted, only a few written down", r.link->counters().releases == 30 && countNotes("nothing to aim") <= 10);
        r.link->stop();
    }

    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
