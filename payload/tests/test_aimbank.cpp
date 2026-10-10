// PC test of the Aimbot's BANK mode (stage D9) against the PRETEND game (fake_il2cpp_aim.cpp): two pretend backboards with a collider and a physic material, a ball with
// a collider, a size and a bounce, and a ball that really bounces off the board (rigid-body maths, written differently from bank.cpp). It proves that the link reads
// what it must, works out a bank shot, sets it, and that the ball REALLY goes in the pretend ring after hitting the pretend board - and that every case that is not
// possible says "Bank unavailable" and leaves the throw alone. It does NOT prove anything about the real game.
#include <dlfcn.h>
#include <unistd.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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


static void (*fake_bank_set)(const char*, double, double, double) = nullptr;
static void (*fake_bank_hide)(const char*) = nullptr;
static void (*fake_bank_state)(double*) = nullptr;
static void (*fake_bank_field_fn)(const char*, const char*, int) = nullptr;
static void fake_bank_field(const char* k, const char* f, int a) { fake_bank_field_fn(k, f, a); }

// the pretend board's readings: 0 board touches, 1 pole touches, 2 steps near the rim, 3 step of the first touch, 4-6 where, 7 speed along the board's normal before, 8 after, 9 bounce used, 10 friction used, 11 approach speed, 12 ball radius
struct BankReading { double v[16]; };
static BankReading bankNow() { BankReading b; std::memset(b.v, 0, sizeof b.v); fake_bank_state(b.v); return b; }

struct Outcome {
    bool banked = false, unavailable = false, direct = false, scored = false, setCalled = false, resultSeen = false;
    int boardTouches = 0, poleTouches = 0, rimSteps = 0;
    BankReading b;
};
static const double kNorthZ = 12.66;
// One throw from (x, z) towards the hoop at hoopZ, then wait for the end of the flight. Default throw: a weak, slightly crooked throw (like a real one).
static Outcome shootOnce(Rig& r, double x, double z, double hoopZ = kNorthZ, double elev = 55, double speed = 9.0, double offDeg = 4.0, double y = 1.6) {
    const long before = static_cast<long>(stateAt(7));
    clearNotes();
    throwFrom(x, y, z, 0, hoopZ, elev, speed, offDeg);
    for (int i = 0; i < 40 && countNotes("result (") == 0 && countNotes("nothing to aim") == 0; ++i) r.run(0.25);
    r.settle(0.3);
    Outcome o;
    double s[10]; state(s);
    o.b = bankNow();
    o.setCalled = static_cast<long>(s[7]) > before;
    o.scored = s[8] == 1;
    o.boardTouches = static_cast<int>(o.b.v[0]); o.poleTouches = static_cast<int>(o.b.v[1]); o.rimSteps = static_cast<int>(o.b.v[2]);
    o.banked = countNotes("decision: BANK SHOT") >= 1;
    o.unavailable = countNotes("decision: LEFT ALONE - Bank unavailable") >= 1;
    o.direct = countNotes("decision: AIM") >= 1 || countNotes("WOULD AIM") >= 1;
    o.resultSeen = countNotes("result (") >= 1;
    return o;
}
// "Bank unavailable": nothing was changed, no direct shot was taken, the menu says so
static bool leftAloneAsUnavailable(const Outcome& o, Rig& r, const char* reasonPart = nullptr) {
    bool ok = o.unavailable && !o.banked && !o.direct && !o.setCalled && r.link->lastShotText().find("Bank unavailable") != std::string::npos;
    if (reasonPart) ok = ok && findNote("decision: LEFT ALONE - Bank unavailable").find(reasonPart) != std::string::npos;
    return ok;
}
// a bank shot that did what it promised: touched the board once, never the pole, dropped in without touching the rim, and the game counted it
static bool goodBank(const Outcome& o) { return o.banked && !o.direct && o.setCalled && o.scored && o.boardTouches == 1 && o.poleTouches == 0 && o.rimSteps == 0; }
static const char* kinds(const Outcome& o) { return o.banked ? (goodBank(o) ? "BANK, scored" : "BANK, NOT GOOD") : (o.unavailable ? "unavailable" : (o.direct ? "DIRECT" : "nothing")); }

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
    fake_bank_set = reinterpret_cast<void (*)(const char*, double, double, double)>(dlsym(gLib, "fake_bank_set"));
    fake_bank_hide = reinterpret_cast<void (*)(const char*)>(dlsym(gLib, "fake_bank_hide"));
    fake_bank_state = reinterpret_cast<void (*)(double*)>(dlsym(gLib, "fake_bank_state"));
    fake_bank_field_fn = reinterpret_cast<void (*)(const char*, const char*, int)>(dlsym(gLib, "fake_bank_field"));
    if (!fake_bank_set || !fake_bank_hide || !fake_bank_state) { std::printf("the pretend runtime has no backboards\n"); return 2; }
    const bool verbose = std::getenv("AIMTEST_VERBOSE") != nullptr;

    std::printf("== modes: only one at a time\n");
    {
        Rig r; r.start();
        r.link->setAsk(true, 50, false);
        CHECK("the old on/off call means Direct mode", r.link->mode() == 1);
        r.link->setAsk(2, 50, false);
        CHECK("mode 2 = Bank", r.link->mode() == 2);
        r.link->setAsk(1, 50, false);
        CHECK("mode 1 = Direct", r.link->mode() == 1);
        r.link->setAsk(0, 50, false);
        CHECK("mode 0 = off", r.link->mode() == 0 && r.link->uiState() == 0);
        r.link->setAsk(7, 50, false);
        CHECK("a number that is not a mode means off", r.link->mode() == 0);
        r.link->setAsk(2, 50, false); r.link->setAsk(1, 50, false); r.run(0.3);
        CHECK("every switch between the modes is written down (Direct->Bank once, Bank->Direct twice, switch ON in Bank mode once)", countNotes("mode changed to BANK") == 1 && countNotes("mode changed to Direct") == 2 && countNotes("switch turned ON (BANK mode)") == 1);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        const bool ok = r.connect(); r.run(1.0);
        CHECK("Bank mode connects", ok && r.link->uiState() == 1);
        CHECK("the headline says the bank code is connected", r.link->headline() == "Bank: connected - waiting for your shot");
        const std::string f = findNote("BANK: found the game's backboard code");
        CHECK("the facts file says it found the game's backboard code, with the positions of the fields", !f.empty() && f.find("collider@96") != std::string::npos && f.find("goal list@24") != std::string::npos && f.find("Maximum") != std::string::npos);
        CHECK("the facts file lists the way the engine's bounce mixing is numbered (read from the game, not remembered)", f.find("Average Multiply Minimum Maximum") != std::string::npos);
        CHECK("the backboards were measured while idle (before any shot)", countNotes("BANK: measured the backboard of the goal with its ring at (0.000, 3.100, 12.660)") == 1 && countNotes("BANK: measured the backboard of the goal with its ring at (0.000, 3.100, -12.660)") == 1);
        CHECK("... the measurement says where the numbers came from", findNote("measured the backboard").find("from the collider box") != std::string::npos && findNote("measured the backboard").find("Face centre (0.000, 3.450, 13.040)") != std::string::npos);
        r.link->stop();
    }

    std::printf("== one bank shot, straight on: it must hit the board and drop in\n");
    {
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        const Outcome o = shootOnce(r, 0, 6.0);
        const Counters c = r.link->counters();
        CHECK("a bank shot was worked out and set (and not a direct shot)", o.banked && !o.direct && o.setCalled);
        CHECK("the ball's speed was set exactly once", stateAt(7) == 1);
        CHECK("the ball touched the board once, never the pole, never the rim", o.boardTouches == 1 && o.poleTouches == 0 && o.rimSteps == 0);
        CHECK("the pretend game says the shot went in (_shotMade)", o.scored);
        CHECK("counters: one release, aimed, one bank shot, scored, nothing unavailable", c.releases == 1 && c.aimed == 1 && c.bankAimed == 1 && c.bankScored == 1 && c.bankUnavailable == 0 && c.bankMissed == 0 && c.scored == 1);
        const std::string in = findNote("bank inputs");
        CHECK("the facts file shows the ball radius that was read (0.5 x scale 0.24 = 0.12 m)", in.find("ball radius 0.1200 m (collider 0.5000 x scale 0.240)") != std::string::npos);
        CHECK("... both materials", in.find("ball material: bounce 0.800, dynamic friction 0.500") != std::string::npos && in.find("board material: bounce 0.600, dynamic friction 0.400") != std::string::npos);
        CHECK("... how the engine mixes them (bounce: the board says Maximum, so 0.8; friction: average, 0.45)", in.find("mixed: bounce 0.800 (maximum of 0.800 and 0.600), sliding friction 0.450 (average of 0.500 and 0.400)") != std::string::npos);
        CHECK("... the engine's bounce threshold, spin, spin drag and spin factor", in.find("bounce threshold 2.00 m/s") != std::string::npos && in.find("spin drag 0.050, spin factor 0.400") != std::string::npos);
        CHECK("... the collision mode, the ring and the board (face centre, direction, size)", in.find("collision mode: Discrete") != std::string::npos && in.find("ring (0.000, 3.100, 12.660)") != std::string::npos &&
              in.find("Face centre (0.000, 3.450, 13.040), faces (0.000, 0.000, -1.000), half size 0.915 x 0.535, thickness 0.050") != std::string::npos);
        CHECK("... where the board's numbers came from, and the game's own data next to them", in.find("collider on the backboard object (shape and place match the board)") != std::string::npos && in.find("_backboardSize (1.830, 1.070, 0.050)") != std::string::npos && in.find("pointing TOWARDS the court") != std::string::npos);
        CHECK("... the cross-check against the ball's own runtime settings", countNotes("bank cross-check: the ball's own runtime settings say bounciness 0.800, static friction 0.500, dynamic friction 0.500") == 1);
        const std::string dec = findNote("decision: BANK SHOT");
        CHECK("the decision line says what it will do: where on the board, how hard, how solid", dec.find("touches the board") != std::string::npos && dec.find("then enters the ring at") != std::string::npos && dec.find("bounce held in") != std::string::npos && dec.find("= kept") != std::string::npos);
        CHECK("the decision line says the throw became a bank shot, with the predicted touch and ring crossing", dec.find("predicted: touch after") != std::string::npos && dec.find("ring crossing after") != std::string::npos);
        const std::string chk = findNote("bank check");
        CHECK("the report says the ball touched the board at the predicted step (the timing model holds)", chk.find("(same step: the timing model holds)") != std::string::npos);
        const double bounce = numberBefore(chk, " (the plan used");
        CHECK("... and it MEASURED the bounce from the ball's speeds (0.80)", bounce > 0.77 && bounce < 0.83);
        CHECK("the flight samples include steps right after the touch, and their errors are tiny", findNote("flight samples").find("err 0.0") != std::string::npos && findNote("flight samples").find("err 0.1") == std::string::npos && findNote("flight samples").find("err 1.") == std::string::npos);
        CHECK("the result says the game kept our speed and the shot counted", findNote("result (").find("our speed was kept by the game") != std::string::npos && findNote("result (").find("_shotMade: YES") != std::string::npos);
        CHECK("the menu text says BANK and SCORED", r.link->lastShotText().find("BANK") != std::string::npos && r.link->lastShotText().find("SCORED") != std::string::npos);
        CHECK("the summary line counts bank shots", r.link->summary().find("mode BANK | BANK shots 1 (scored 1, missed 0), Bank unavailable 0") != std::string::npos);
        r.link->stop();
    }

    std::printf("== bank shots from many spots (both hoops)\n");
    {
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        struct Spot { double x, z, hoopZ; };
        const Spot spots[] = {{0, 3, kNorthZ}, {-5, 3, kNorthZ}, {5, 3, kNorthZ}, {-8, 6, kNorthZ}, {0, 6, kNorthZ}, {8, 6, kNorthZ}, {-8, 9, kNorthZ}, {-4, 9, kNorthZ}, {0, 9, kNorthZ}, {4, 9, kNorthZ}, {8, 9, kNorthZ},
                              {-2.5, 11, kNorthZ}, {0, 11, kNorthZ}, {2.5, 11, kNorthZ}, {0, -6, -kNorthZ}, {4, -9, -kNorthZ}, {-5, -3, -kNorthZ}, {7, -7, -kNorthZ}, {-3, -10.5, -kNorthZ}};
        int good = 0, total = 0, bad = 0;
        for (const Spot& sp : spots) {
            const Outcome o = shootOnce(r, sp.x, sp.z, sp.hoopZ);
            ++total;
            const bool g = goodBank(o);
            if (g) ++good;
            if (!g && !leftAloneAsUnavailable(o, r)) ++bad;
            if (verbose || !g) std::printf("       x %5.1f z %5.1f -> %s (board %d, pole %d, rim %d)\n", sp.x, sp.z, kinds(o), o.boardTouches, o.poleTouches, o.rimSteps);
        }
        CHECK("every spot is either a bank shot that scored (one board touch, no pole, no rim) or a clean 'Bank unavailable' - never anything else", bad == 0);
        CHECK("at least 17 of the 19 spots had a bank shot, and all of those scored", good >= 17);
        const Counters c = r.link->counters();
        CHECK("the counters add up", c.releases == static_cast<unsigned long long>(total) && c.bankAimed == static_cast<unsigned long long>(good) && c.bankScored == static_cast<unsigned long long>(good) && c.bankAimed + c.bankUnavailable == static_cast<unsigned long long>(total) && c.bankMissed == 0);
        CHECK("not a single direct shot was taken in Bank mode", countNotes("decision: AIM") == 0);
        r.link->stop();
    }

    std::printf("== places where a bank shot is not possible: 'Bank unavailable', the throw is left alone, no direct shot\n");
    {
        struct Case { const char* name; double x, z, hoopZ; const char* reason; };
        const Case cases[] = {
            {"far away (12.8 m from the board): no safe bank shot", 0, 0, kNorthZ, "no safe bank shot from this spot"},
            {"1 m from the board: too close", -2.5, 12.0, kNorthZ, "too close to the backboard"},
            {"almost on the line of the board: angle too sideways", -8, 11, kNorthZ, "angle too sideways to reach the board"},
            {"behind the backboard", -1.0, 14.3, kNorthZ, "you are behind the backboard"},
        };
        for (const Case& cs : cases) {
            Rig r; r.start(); r.link->setAsk(2, 50, false);
            r.connect(); r.run(1.0);
            const Outcome o = shootOnce(r, cs.x, cs.z, cs.hoopZ);
            char nm[200];
            std::snprintf(nm, sizeof nm, "%s: left alone as 'Bank unavailable', speed never set, no direct shot", cs.name);
            CHECK(nm, leftAloneAsUnavailable(o, r, cs.reason));
            const Counters c = r.link->counters();
            std::snprintf(nm, sizeof nm, "%s: counted as unavailable, not aimed", cs.name);
            CHECK(nm, c.bankUnavailable == 1 && c.aimed == 0 && c.bankAimed == 0 && stateAt(7) == 0);
            std::snprintf(nm, sizeof nm, "%s: the menu text says why", cs.name);
            CHECK(nm, r.link->lastShotText().find(std::string("Bank unavailable - ") + cs.reason) != std::string::npos);
            std::snprintf(nm, sizeof nm, "%s: the facts file says the throw was left exactly as thrown", cs.name);
            CHECK(nm, findNote("decision: LEFT ALONE - Bank unavailable").find("the throw is left exactly as you threw it (no direct shot)") != std::string::npos);
            r.link->stop();
        }
    }

    std::printf("== the game's own numbers decide the shot (bounce, friction, mixing rules, ball size, spin, touch model)\n");
    {
        struct Var { const char* name; std::function<void()> setup; const char* note; double expectE; int minGood; };
        const Var vars[] = {
            {"nothing changed (reference)", [] {}, "mixed: bounce 0.800 (maximum", 0.80, 3},
            {"ball bounce 0.7, board 0.5 (the board says Maximum -> 0.7)", [] { fake_bank_set("ball_e", 0.7, 0, 0); fake_bank_set("board_e", 0.5, 0, 0); }, "mixed: bounce 0.700 (maximum", 0.70, 3},
            {"board mixing = Multiply (0.8 x 0.6 = 0.48)", [] { fake_bank_set("board_bcomb", 1, 0, 0); }, "mixed: bounce 0.480 (multiply", 0.48, 2},
            {"board mixing = Minimum (0.6)", [] { fake_bank_set("board_bcomb", 2, 0, 0); }, "mixed: bounce 0.600 (minimum", 0.60, 3},
            {"both Average (0.7)", [] { fake_bank_set("board_bcomb", 0, 0, 0); }, "mixed: bounce 0.700 (average", 0.70, 3},
            {"low friction 0.2 (board Multiply -> 0.5 x 0.2 = 0.1)", [] { fake_bank_set("board_mu", 0.2, 0, 0); fake_bank_set("board_fcomb", 1, 0, 0); }, "sliding friction 0.100 (multiply", -1, 3},
            {"high friction 1.2 both", [] { fake_bank_set("board_mu", 1.2, 0, 0); fake_bank_set("ball_mu", 1.2, 0, 0); }, "sliding friction 1.200", -1, 3},
            {"the engine's touch model is Continuous", [] { fake_bank_set("ccd", 1, 0, 0); }, "collision mode: Continuous", 0.80, 3},
            {"ContinuousDynamic", [] { fake_bank_set("ccd", 2, 0, 0); }, "collision mode: ContinuousDynamic", 0.80, 3},
            {"ContinuousSpeculative (treated as unknown: must work either way)", [] { fake_bank_set("ccd", 3, 0, 0); }, "(treated as unknown)", 0.80, 2},
            {"bigger ball (scale 0.30 -> radius 0.15)", [] { fake_bank_set("ball_scale", 0.30, 0, 0); }, "ball radius 0.1500 m", 0.80, 2},
            {"smaller ball (scale 0.20 -> radius 0.10)", [] { fake_bank_set("ball_scale", 0.20, 0, 0); }, "ball radius 0.1000 m", 0.80, 3},
            {"hollow ball (spin factor 0.667 from the inertia)", [] { fake_bank_set("kappa", 0.667, 0, 0); }, "spin factor 0.667", 0.80, 3},
            {"backspin thrown with the ball (-25 rad/s about x)", [] { fake_bank_set("release_spin", -25, 0, 0); }, "spin (-", 0.80, 3},
            {"topspin thrown with the ball (+25 rad/s about x)", [] { fake_bank_set("release_spin", 25, 0, 0); }, "spin (2", 0.80, 3},
            {"sidespin (20 rad/s about y)", [] { fake_bank_set("release_spin", 0, 20, 0); }, "spin (0.0", 0.80, 3},
            {"strong spin drag (0.5)", [] { fake_bank_set("ang_drag", 0.5, 0, 0); fake_bank_set("release_spin", -25, 0, 0); }, "spin drag 0.500", 0.80, 3},
            {"bounce threshold 3 m/s", [] { fake_bank_set("bounce_threshold", 3.0, 0, 0); }, "bounce threshold 3.00 m/s", 0.80, 2},
            {"the board's collider sits on a child object of the backboard object", [] { fake_bank_set("rebuild", 1, 0, 0); }, "collider on the backboard object", 0.80, 3},
            {"the board's collider sits on a different object of the goal (found by walking the goal's objects)", [] { fake_bank_set("rebuild", 2, 0, 0); }, "collider found while walking the goal's objects", 0.80, 3},
            {"the game's board direction points away from the court (flipped sign)", [] { fake_bank_set("flip_normal", 0, 0, 0); }, "pointing AWAY from the court", 0.80, 3},
            {"the game's backboard data is 0.3 m off from the collider (the collider wins)", [] { fake_bank_set("board_data_dy", 0.3, 0, 0); }, "from the collider box", 0.80, 3},
            {"no material on the board (the engine's default: bounce 0, friction 0.6)", [] { fake_bank_set("board_no_material", 0, 0, 0); }, "no material set (the engine's default material", 0.40, 0},
        };
        for (const Var& v : vars) {
            Rig r;
            v.setup();
            r.start(); r.link->setAsk(2, 50, false);
            r.connect(); r.run(1.0);
            const double spots[3][2] = {{0, 6}, {-5, 9}, {5, 3}};
            int good = 0, bad = 0, banks = 0;
            bool noteOk = false, eOk = v.expectE < 0;
            for (const auto& sp : spots) {
                const Outcome o = shootOnce(r, sp[0], sp[1]);
                if (goodBank(o)) ++good;
                if (o.banked) ++banks;
                if (!goodBank(o) && !leftAloneAsUnavailable(o, r)) ++bad;
                if (o.banked || o.unavailable) {
                    if (findNote("bank inputs").find(v.note) != std::string::npos) noteOk = true;
                    if (o.banked && v.expectE >= 0) { const double m = numberBefore(findNote("bank check"), " (the plan used"); if (std::fabs(m - v.expectE) < 0.04) eOk = true; }
                }
                if (verbose || (!goodBank(o) && !o.unavailable)) std::printf("       %s at (%.0f, %.0f): %s\n", v.name, sp[0], sp[1], kinds(o));
            }
            char nm[260];
            std::snprintf(nm, sizeof nm, "%s: every shot is a bank shot that scored or a clean 'Bank unavailable'", v.name);
            CHECK(nm, bad == 0);
            std::snprintf(nm, sizeof nm, "%s: at least %d of 3 spots had a bank shot, and all scored", v.name, v.minGood);
            CHECK(nm, good >= v.minGood && good == banks);
            std::snprintf(nm, sizeof nm, "%s: the facts file shows the number that was read", v.name);
            CHECK(nm, noteOk);
            if (v.expectE >= 0 && v.minGood > 0) { std::snprintf(nm, sizeof nm, "%s: the bounce MEASURED on the pretend ball matches the one the plan used", v.name); CHECK(nm, eOk); }
            r.link->stop();
        }
    }

    std::printf("== things the game may not give us: every one says 'Bank unavailable: <why>' and leaves the throw alone\n");
    {
        struct Broken { const char* name; std::function<void()> setup; const char* reason; const char* detail; bool atStart; };
        const Broken bs[] = {
            {"no collider on the backboard", [] { fake_bank_set("no_board_collider", 0, 0, 0); }, "I cannot measure the backboard", "no collider of the board's size near it", false},
            {"the board's collider is destroyed", [] { fake_bank_set("destroy_board_collider", 0, 0, 0); }, "I cannot measure the backboard", "no collider", false},
            {"the board's collider is far too big (2x)", [] { fake_bank_set("wrong_size_collider", 2.0, 0, 0); }, "", "", false},
            {"the game's board data is 0.6 m away from the real collider", [] { fake_bank_set("board_data_dy", 0.6, 0, 0); }, "I cannot measure the backboard", "no collider of the board's size near it", false},
            {"the game's board size is nonsense (0.1 x 0.1 x 0.01)", [] { fake_bank_set("board_size_data", 0.1, 0.1, 0.01); }, "I cannot measure the backboard", "not believable", false},
            {"the board's material object is gone", [] { fake_bank_set("board_material_gone", 0, 0, 0); }, "I cannot read the backboard's bounce", "its material object is gone", false},
            {"the ball's collider is not linked", [] { fake_bank_set("ball_collider_null", 0, 0, 0); }, "I cannot read the ball's size", "not linked or gone", false},
            {"the ball's collider is destroyed", [] { fake_bank_set("destroy_ball_collider", 0, 0, 0); }, "I cannot read the ball's size", "not linked or gone", false},
            {"the ball has no BasketballProperties", [] { fake_bank_set("props_null", 0, 0, 0); }, "I cannot read the ball's physics settings", "", false},
            {"the ball is absurdly small (radius 0.5 cm)", [] { fake_bank_set("ball_scale", 0.01, 0, 0); }, "a ball or board number from the game is missing or not believable", "radius 0.005", false},
            {"the game has no goal manager", [] { fake_bank_set("hide_manager", 1, 0, 0); }, "I cannot find that hoop's goal", "no goal manager", false},
            {"the goal list is empty (0 goals)", [] { fake_bank_set("goal_list_size", 0, 0, 0); }, "I cannot find that hoop's goal", "looks wrong (0 goals)", false},
            {"the goal manager has no list", [] { fake_bank_set("goal_list_null", 0, 0, 0); }, "I cannot find that hoop's goal", "goal list could not be read", false},
            {"no goal has its ring where the ball's hoop is (1 m off)", [] { fake_bank_set("ring_data_dy", 1.0, 0, 0); }, "I cannot find that hoop's goal", "no goal of the game has its ring at the hoop you are aiming at", false},
            {"the game lost the field BasketballGoal._backboardCenter", [] { fake_bank_field("BasketballGoal", "_backboardCenter", 0); }, "", "the field _backboardCenter is missing in ShovelTools.BasketballGoal", true},
            {"BasketballGoal._backboardNormal has another type", [] { fake_bank_field("BasketballGoal", "_backboardNormal", 1); }, "", "has type System.Int32, expected UnityEngine.Vector3", true},
            {"the game lost BasketballGoal._rimRadius", [] { fake_bank_field("BasketballGoal", "_rimRadius", 0); }, "", "_rimRadius is missing", true},
            {"the game lost BasketballProperties._basketballCollider", [] { fake_bank_field("BasketballProperties", "_basketballCollider", 0); }, "", "_basketballCollider is missing", true},
            {"the game lost Basketball._properties", [] { fake_bank_field("Basketball", "_properties", 0); }, "", "_properties is missing", true},
            {"the game lost BasketballGoalManager._goals", [] { fake_bank_field("BasketballGoalManager", "_goals", 0); }, "", "_goals is missing", true},
            {"the class BasketballGoalManager does not exist", [] { fake_bank_field("BasketballGoalManager", "", 2); }, "", "BasketballGoalManager", true},
            {"the engine has no PhysicMaterial.get_bounciness", [] { fake_bank_hide("get_bounciness"); }, "", "PhysicMaterial.get_bounciness", true},
            {"the engine has no Collider.get_bounds", [] { fake_bank_hide("get_bounds"); }, "", "Collider.get_bounds", true},
            {"the engine has no Collider.get_sharedMaterial", [] { fake_bank_hide("get_sharedMaterial"); }, "", "Collider.get_sharedMaterial", true},
            {"the engine has no SphereCollider.get_radius", [] { fake_bank_hide("get_radius"); }, "", "SphereCollider.get_radius", true},
            {"the engine has none of the three GetComponent calls", [] { fake_bank_hide("GetComponent"); fake_bank_hide("GetComponentInChildren"); fake_bank_hide("GetComponentInParent"); }, "", "Component.GetComponent(Type)", true},
            {"the engine has no PhysicMaterial.get_bounceCombine", [] { fake_bank_hide("get_bounceCombine"); }, "", "PhysicMaterial.get_bounceCombine", true},
            {"the engine's PhysicMaterialCombine list cannot be read", [] { fake_bank_field("PhysicMaterialCombine", "", 2); }, "", "PhysicMaterialCombine", true},
        };
        for (const Broken& b : bs) {
            Rig r;
            b.setup();
            r.start(); r.link->setAsk(2, 50, false);
            r.connect(); r.run(1.2);
            const Outcome o = shootOnce(r, 0, 6.0);
            char nm[260];
            std::snprintf(nm, sizeof nm, "%s: 'Bank unavailable', throw left alone, no direct shot, nothing set", b.name);
            const bool unav = o.unavailable && !o.banked && !o.direct && !o.setCalled && r.link->lastShotText().find("Bank unavailable") != std::string::npos;
            if (std::string(b.reason).empty() && std::string(b.detail).empty()) {
                // (the 2x collider: either a plan from the game's own board numbers that scored, or a clean refusal - never a silent change)
                CHECK(nm, goodBank(o) || unav || (o.banked && !o.direct));
                std::snprintf(nm, sizeof nm, "%s: the facts file says the collider is not the board's size", b.name);
                CHECK(nm, findNote("bank inputs").find("not the board's size") != std::string::npos || findNote("Bank unavailable").find("board's size") != std::string::npos);
                if (verbose) std::printf("       -> %s\n", kinds(o));
            } else {
                CHECK(nm, unav);
                std::snprintf(nm, sizeof nm, "%s: the reason is written in the menu text and the facts file", b.name);
                const std::string text = r.link->lastShotText() + " | " + findNote("decision: LEFT ALONE - Bank unavailable") + " | " + r.link->headline() + " | " + findNote("BANK cannot be used");
                CHECK(nm, text.find(b.reason[0] ? b.reason : b.detail) != std::string::npos && (b.detail[0] == 0 || text.find(b.detail) != std::string::npos));
            }
            if (b.atStart) {
                std::snprintf(nm, sizeof nm, "%s: the menu headline says 'Bank unavailable' straight away (before any shot)", b.name);
                CHECK(nm, r.link->headline().rfind("Bank unavailable: ", 0) == 0);
            }
            r.link->stop();
        }
    }
    {
        // a board that was fine for the first shot and is gone for the second: no stale numbers are used
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        const Outcome a = shootOnce(r, 0, 6.0);
        fake_bank_set("destroy_board_collider", 0, 0, 0);
        const Outcome b = shootOnce(r, 0, 6.0);
        CHECK("first shot fine, then the backboard's collider is destroyed: the second is 'Bank unavailable' (no stale board data)", goodBank(a) && leftAloneAsUnavailable(b, r, "I cannot measure the backboard"));
        r.link->stop();
    }

    std::printf("== optional engine calls missing: the shot still works, the facts file says what was assumed\n");
    {
        struct Opt { const char* method; const char* note; };
        const Opt opts[] = {{"get_angularVelocity", "[spin not read: assumed none]"}, {"get_angularDrag", "[spin drag taken from _originalAngularDrag]"}, {"get_inertiaTensor", "[spin inertia not read: a solid ball (0.4) assumed]"},
                            {"get_collisionDetectionMode", "touch model: unknown (a shot must work with both)"}, {"get_bounceThreshold", "[bounce threshold not read: the engine's default 2 m/s used]"},
                            {"GetComponent", "GetComponent NO, in children yes"}, {"GetComponentInChildren", "in children NO"}};
        for (const Opt& o : opts) {
            Rig r; fake_bank_hide(o.method); r.start(); r.link->setAsk(2, 50, false);
            r.connect(); r.run(1.0);
            const std::string found = findNote("BANK: found the game's backboard code");
            int good = 0, bad = 0;
            for (const auto& sp : {std::pair<double, double>{0, 6}, {-5, 9}, {5, 3}}) {
                const Outcome out = shootOnce(r, sp.first, sp.second);
                if (goodBank(out)) ++good;
                if (!goodBank(out) && !leftAloneAsUnavailable(out, r)) ++bad;
            }
            char nm[200];
            std::snprintf(nm, sizeof nm, "%s hidden: bank shots still scored (or were refused cleanly)", o.method);
            CHECK(nm, good >= 2 && bad == 0);
            std::snprintf(nm, sizeof nm, "%s hidden: the facts file says what was assumed", o.method);
            CHECK(nm, (findNote("bank inputs") + found).find(o.note) != std::string::npos || (std::string(o.method) == "get_angularDrag" && findNote("bank inputs").find("spin drag 0.050") != std::string::npos));
            r.link->stop();
        }
    }

    std::printf("== only one mode: Direct stays Direct, Bank never turns into Direct\n");
    {
        Rig r; r.start(); r.link->setAsk(1, 50, false);
        r.connect(); r.run(1.0);
        const Outcome o = shootOnce(r, 2, 4);
        const Counters c = r.link->counters();
        CHECK("Direct mode: a direct shot, scored, no bank code ran", o.direct && !o.banked && !o.unavailable && o.scored && c.aimed == 1 && c.bankAimed == 0 && c.bankUnavailable == 0);
        CHECK("... the bank code was never even looked up (no bank lines in the facts file)", countNotes("BANK") == 0 && countNotes("bank inputs") == 0);
        CHECK("... the board was not needed (Direct shots do not touch the board: the pretend board was not hit)", o.boardTouches == 0);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(1, 50, false);
        r.connect(); r.run(1.0);
        const Outcome d1 = shootOnce(r, 0, 6.0);
        r.link->setAsk(2, 50, false); r.run(1.0);
        const Outcome b1 = shootOnce(r, 0, 6.0);
        r.link->setAsk(1, 50, false); r.run(0.3);
        const Outcome d2 = shootOnce(r, 0, 6.0);
        r.link->setAsk(2, 50, false); r.run(0.3);
        const Outcome b2 = shootOnce(r, 0, 6.0);
        CHECK("Direct -> Bank -> Direct -> Bank: each shot uses the mode that was on when it was thrown", d1.direct && !d1.banked && goodBank(b1) && d2.direct && !d2.banked && goodBank(b2));
        const Counters c = r.link->counters();
        CHECK("... counters: 4 aimed shots, 2 of them bank", c.aimed == 4 && c.bankAimed == 2 && c.bankUnavailable == 0);
        r.link->stop();
    }
    {
        // turning Bank on while a direct-only link is already connected: the bank code is found later, once
        Rig r; r.start(); r.link->setAsk(1, 50, false);
        r.connect(); r.run(0.5);
        CHECK("Direct mode connected: the headline does not mention the bank", r.link->headline() == "connected - waiting for your shot");
        r.link->setAsk(2, 50, false); r.run(1.0);
        CHECK("... then Bank is switched on: the headline says the bank code is connected", r.link->headline() == "Bank: connected - waiting for your shot");
        r.link->setAsk(1, 50, false); r.run(0.2);
        CHECK("... and back to Direct", r.link->headline() == "connected - waiting for your shot");
        r.link->stop();
    }

    std::printf("== the other rules still apply in Bank mode\n");
    {
        Rig r; r.start(); r.link->setAsk(2, 5, false);
        r.connect(); r.run(1.0);
        const Outcome o = shootOnce(r, 0, 6.0);
        CHECK("distance limit 5 m, the hoop is 6.7 m away: left alone as too far (not 'Bank unavailable')", !o.banked && !o.setCalled && !o.unavailable && r.link->counters().tooFar == 1 && r.link->counters().bankUnavailable == 0);
        CHECK("... the menu says the limit", r.link->lastShotText().find("farther than your 5 m limit") != std::string::npos);
        r.link->stop();
    }
    {
        Rig r; r.yFeed = true; r.start(); r.link->setAsk(2, 50, true);
        r.connect(); r.run(1.0);
        r.yHeld = false;
        const Outcome o = shootOnce(r, 0, 6.0);
        CHECK("'Hold Y to aim' ON and Y not held: nothing happens", !o.banked && !o.unavailable && !o.setCalled && r.link->counters().notHoldingY == 1);
        CHECK("... the menu says to hold Y", r.link->lastShotText().find("hold Y to aim") != std::string::npos);
        r.yHeld = true; r.run(0.3);
        const Outcome p = shootOnce(r, 0, 6.0);
        CHECK("... with Y held: the bank shot is made and scores", goodBank(p));
        r.link->stop();
    }
    {
        Rig r(true, "", false); r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        const Outcome o = shootOnce(r, 0, 6.0);
        CHECK("test mode (writing switched off): says WOULD BANK and changes nothing", countNotes("decision: WOULD BANK (test mode, nothing written)") == 1 && !o.setCalled && r.link->counters().bankAimed == 0);
        r.link->stop();
    }
    {
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        for (int i = 0; i < 4; ++i) { throwFrom(0, 1.6, 6.0, 0, kNorthZ, -60, 2.0, 0.0); r.run(0.6); }
        r.settle(0.3);
        CHECK("dribbles in Bank mode: counted, not shots, no bank calculation, no 'Bank unavailable'", r.link->counters().releases == 4 && r.link->counters().shotLike == 0 && r.link->counters().bankUnavailable == 0 && countNotes("bank inputs") == 0);
        r.link->stop();
    }
    {
        // five shots in a row from different places in one session
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        int good = 0;
        const double spots[5][2] = {{0, 4}, {3, 7}, {-3, 7}, {6, 5}, {-6, 5}};
        for (const auto& sp : spots) if (goodBank(shootOnce(r, sp[0], sp[1]))) ++good;
        const Counters c = r.link->counters();
        CHECK("five bank shots in a row: all scored, all counted", good == 5 && c.bankAimed == 5 && c.bankScored == 5 && c.releases == 5);
        r.link->stop();
    }
    {
        // the switch is turned off while Bank mode was on: nothing is touched any more
        Rig r; r.start(); r.link->setAsk(2, 50, false);
        r.connect(); r.run(1.0);
        r.link->setAsk(0, 50, false); r.run(0.3);
        const double calls0 = stateAt(6);
        throwFrom(0, 1.6, 6.0, 0, kNorthZ, 55, 9.0, 4.0);
        r.run(3.0);
        CHECK("switch off after Bank: not a single call into the game, headline says off", stateAt(6) == calls0 && r.link->headline() == "Aimbot is off" && r.link->counters().releases == 0);
        r.link->stop();
    }

    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
