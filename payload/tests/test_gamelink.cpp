// PC test of the game link (game_link.cpp) against the PRETEND game (fake_il2cpp.cpp): finding the live player object, writing
// "original x factor", keeping it that way while a pretend game thread fights back, putting the originals back, and every way it can fail.
// Proves the logic and the safety rules. Does NOT prove that the real game moves differently.
#include <dlfcn.h>
#include <sys/mman.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game_link.h"
#include "movement.h"
#include "safe_copy.h"

using namespace tzgame;
using namespace tzmove;

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)

static void* gLib = nullptr;
static std::atomic<bool> gProviderOk{true};
static bool provider(tzscan::Api* api, std::string* why) {
    if (!gProviderOk.load()) { *why = "pretend: libil2cpp.so is not loaded yet"; return false; }
    return tzscan::loadApi(gLib, api, why);
}
static std::mutex gNoteMu;
static std::vector<std::string> gNotes;
static void note(const char* fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    std::lock_guard<std::mutex> lock(gNoteMu); gNotes.push_back(b);
}
static bool noted(const char* needle) { std::lock_guard<std::mutex> lock(gNoteMu); for (const auto& l : gNotes) if (l.find(needle) != std::string::npos) return true; return false; }
static size_t noteCount() { std::lock_guard<std::mutex> lock(gNoteMu); return gNotes.size(); }

static bool waitFor(const std::function<bool()>& f, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    return f();
}
static void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static float getF(void* o, int off) { float f; std::memcpy(&f, static_cast<unsigned char*>(o) + off, 4); return f; }
static void setF(void* o, int off, float v) { std::memcpy(static_cast<unsigned char*>(o) + off, &v, 4); }
static bool same(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
static unsigned long long counterOf(const PlayerLink& l, const char* label) {          // "writes 12," -> 12
    const std::string s = l.summary(); const size_t p = s.find(label);
    return p == std::string::npos ? ~0ULL : std::strtoull(s.c_str() + p + std::strlen(label), nullptr, 10);
}

enum { FWD_MAX = 684, FWD_ACC = 688, FWD_DEC = 692, LAT_MAX = 696, LAT_ACC = 700, LAT_DEC = 704, BWD_MAX = 708, BWD_ACC = 712, BWD_DEC = 716,
       JUMP_MAX_SPEED = 720, JUMP_MULT = 732, WALK = 744, JOG = 748, SPRINT = 752, GRAV = 524, FINAL_MULT = 464, HMD = 460 };

static Request ask(bool speedOn, float speed, bool jumpOn, float jump, int mode = 0, float pct = 0) {
    Request r; r.speedOn = speedOn; r.speed = speed; r.jumpOn = jumpOn; r.jump = jump; r.gravityMode = mode; r.lowPct = mode == 1 ? pct : 0; r.highPct = mode == 2 ? pct : 0; return r;
}
static bool wantedBy(const Request& r) { return r.speedOn || r.jumpOn || r.gravityMode != 0; }

int main(int, char** argv) {
    gLib = dlopen(argv[1], RTLD_NOW);
    if (!gLib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    auto makePL = reinterpret_cast<void* (*)(int)>(dlsym(gLib, "fake_make_player_locomotion"));
    auto makeLoco = reinterpret_cast<void* (*)(int)>(dlsym(gLib, "fake_make_locomotion"));
    (void)makeLoco;

    std::printf("== the safe write (CopyPipe::put)\n");
    {
        tzscan::CopyPipe p; CHECK("the pipe opens", p.open(0, nullptr));
        float target = 1.0f, v = 2.5f; unsigned char b[4]; std::memcpy(b, &v, 4);
        CHECK("put into our own memory works", p.put(reinterpret_cast<uintptr_t>(&target), b, 4) && target == 2.5f);
        CHECK("put to an address that is not mapped says no (no crash)", !p.put(8, b, 4));
        void* ro = mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK("put into read-only memory says no (no crash) and leaves it alone", ro != MAP_FAILED && !p.put(reinterpret_cast<uintptr_t>(ro), b, 4) && *static_cast<volatile int*>(ro) == 0);
        unsigned char back[4] = {0};
        CHECK("the pipe still works afterwards (nothing stuck inside)", p.copy(reinterpret_cast<uintptr_t>(&target), back, 4) && std::memcmp(back, &v, 4) == 0 && p.trouble() == 0);
        CHECK("put of too many bytes says no", !p.put(reinterpret_cast<uintptr_t>(&target), b, p.chunk() + 1));
    }

    std::printf("== before anything is asked, the link does nothing\n");
    void* garbage = makePL(1); void* inactive = makePL(2);
    unsigned char garbageBefore[1056], inactiveBefore[1056];
    std::memcpy(garbageBefore, garbage, 1056); std::memcpy(inactiveBefore, inactive, 1056);
    LinkConfig cfg; cfg.provider = provider; cfg.note = note; cfg.tickMs = 2; cfg.retrySoonSeconds = 0.2; cfg.minSearchGapSeconds = 0.2; cfg.refreshSeconds = 3600;
    cfg.searchSeconds = 20; cfg.stallSeconds = 5;
    PlayerLink link;
    CHECK("the link starts", link.start(cfg));
    pause(400);
    CHECK("no switch on: no search, idle, not ready", link.searches() == 0 && link.uiState() == 0 && !link.ready() && link.aliveTargets() == 0);

    std::printf("== a switch is turned on, but there is NO active player object yet (only a stale block and an inactive copy)\n");
    gProviderOk = false;
    link.setWanted(true);
    pause(600);
    CHECK("the runtime is not loaded yet: it keeps trying quietly, shows 'looking' (not failed)", link.uiState() == 2 && link.searches() == 0 && link.failReason().empty());
    gProviderOk = true;
    CHECK("then the runtime is there: it searches", waitFor([&] { return link.searches() >= 2; }, 8));
    CHECK("only garbage and an inactive copy exist: not connected, shows 'looking', nothing failed", link.uiState() == 2 && !link.ready() && link.aliveTargets() == 0 && link.failReason().empty());
    CHECK("... and the facts say they were rejected", noted("0 look like the active player"));
    CHECK("neither of those two objects was touched", std::memcmp(garbageBefore, garbage, 1056) == 0 && std::memcmp(inactiveBefore, inactive, 1056) == 0);

    std::printf("== the active player object appears\n");
    void* player = makePL(0);
    unsigned char before[1056]; std::memcpy(before, player, 1056);
    CHECK("the link finds it on its own", waitFor([&] { return link.ready(); }, 8));
    CHECK("exactly one object is linked, state = connected", link.aliveTargets() == 1 && link.uiState() == 1);
    CHECK("the facts say CONNECTED with the class and its field positions", noted("found the class ShovelTools.PlayerLocomotion (object size 1056)") && noted("_forwardMaxSpeed@684") && noted("CONNECTED to 1 object"));
    CHECK("the stale block and the inactive copy were still not touched", std::memcmp(garbageBefore, garbage, 1056) == 0 && std::memcmp(inactiveBefore, inactive, 1056) == 0);
    pause(100);
    CHECK("a switch is 'on' but nothing is asked: still not one byte written", std::memcmp(before, player, 1056) == 0 && counterOf(link, "writes ") == 0);

    Controller ctl; ctl.setAdapter(&link);
    auto step = [&](const Request& r) { link.setWanted(wantedBy(r)); return ctl.update(r); };

    std::printf("== Speed Boost\n");
    Request r = ask(true, 2.0f, false, 1.0f);
    CHECK("Speed 2.0x is applied", step(r) && ctl.status() == Controller::ACTIVE);
    CHECK("forward / lateral / backward max speed = original x 2", same(getF(player, FWD_MAX), 5.0f) && same(getF(player, LAT_MAX), 4.0f) && same(getF(player, BWD_MAX), 1.62f * 2.0f));
    CHECK("... and their accelerations / decelerations too, so the speed is reached just as fast",
          same(getF(player, FWD_ACC), 8.0f) && same(getF(player, FWD_DEC), 8.0f) && same(getF(player, LAT_ACC), 8.0f) && same(getF(player, LAT_DEC), 4.2f * 2.0f) && same(getF(player, BWD_ACC), 8.0f) && same(getF(player, BWD_DEC), 8.0f));
    CHECK("jump height, gravity and the walk / jog / sprint multipliers are not touched",
          same(getF(player, JUMP_MULT), 1.5f) && same(getF(player, GRAV + 4), -0.9f) && same(getF(player, WALK), 1.0f) && same(getF(player, JOG), 1.5f) && same(getF(player, SPRINT), 2.0f) && same(getF(player, JUMP_MAX_SPEED), 0.128f));
    CHECK("not one other byte of the object changed", [&] { unsigned char now[1056]; std::memcpy(now, player, 1056);
        for (int i = 0; i < 1056; ++i) { const bool managed = (i >= 684 && i < 720); if (!managed && now[i] != before[i]) return false; } return true; }());
    CHECK("the facts have the read-back of the first apply", noted("applied speed x2.00") && noted("_forwardMaxSpeed 5 (wanted 5)"));

    std::printf("== the slider moves: always original x factor, never piled up\n");
    r.speed = 3.0f; step(r);
    CHECK("3.0x gives 7.5 (2.5 x 3), not 5 x 3", same(getF(player, FWD_MAX), 7.5f) && same(getF(player, FWD_ACC), 12.0f));
    const long applies = ctl.applyCalls(); const unsigned long long writes = counterOf(link, "writes ");
    for (int i = 0; i < 2000; ++i) step(r);
    pause(150);
    CHECK("the same request 2000 more times: no new apply, no new write, same numbers", ctl.applyCalls() == applies && counterOf(link, "writes ") == writes && same(getF(player, FWD_MAX), 7.5f));
    r.speed = 1.1f; step(r);
    CHECK("back down to 1.1x: 2.5 x 1.1", same(getF(player, FWD_MAX), 2.5f * 1.1f));
    r.speed = 5.0f; step(r);
    CHECK("up to 5.0x: 12.5", same(getF(player, FWD_MAX), 12.5f));

    std::printf("== Jump Boost\n");
    r = ask(true, 2.0f, true, 3.0f); step(r);
    CHECK("jump 3.0x: _jumpHeightMultiplier = 1.5 x 3 = 4.5 (speed stays x2)", same(getF(player, JUMP_MULT), 4.5f) && same(getF(player, FWD_MAX), 5.0f));
    r.jump = 5.0f; step(r);
    CHECK("jump 5.0x: 7.5", same(getF(player, JUMP_MULT), 7.5f));
    r.jumpOn = false; step(r);
    CHECK("jump off: the game's own 1.5 is back, speed still x2", same(getF(player, JUMP_MULT), 1.5f) && same(getF(player, FWD_MAX), 5.0f));

    std::printf("== Low / High Gravity (one at a time)\n");
    r = ask(false, 1.0f, false, 1.0f, 1, 50); step(r);
    CHECK("speed back to normal; Low 50% leaves 50% of gravity: y = -0.9 x 0.5", same(getF(player, FWD_MAX), 2.5f) && same(getF(player, GRAV + 4), -0.9f * 0.5f) && same(getF(player, GRAV), 0.0f) && same(getF(player, GRAV + 8), 0.0f));
    r = ask(false, 1.0f, false, 1.0f, 2, 40); step(r);
    CHECK("switching to High 40% replaces Low: y = -0.9 x 1.4 (never both)", same(getF(player, GRAV + 4), -0.9f * 1.4f));
    r = ask(false, 1.0f, false, 1.0f, 1, 90); step(r);
    CHECK("Low 90%: y = -0.9 x 0.1", same(getF(player, GRAV + 4), -0.9f * (1.0f - 0.9f)));
    r = ask(false, 1.0f, false, 1.0f, 1, 0); step(r);
    CHECK("Low 0% is normal gravity and writes the original back exactly", same(getF(player, GRAV + 4), -0.9f));
    r = ask(false, 1.0f, false, 1.0f, 2, 90); step(r);
    CHECK("High 90%: y = -0.9 x 1.9", same(getF(player, GRAV + 4), -0.9f * 1.9f));

    std::printf("== everything together, then Fly takes over\n");
    r = ask(true, 2.0f, true, 2.0f, 1, 50); step(r);
    CHECK("speed x2, jump x2, gravity x0.5 at once, each exactly once", same(getF(player, FWD_MAX), 5.0f) && same(getF(player, JUMP_MULT), 3.0f) && same(getF(player, GRAV + 4), -0.45f));
    Request flying = r; flying.flyActive = true; step(flying);
    CHECK("Fly active: every boost steps aside (originals back)", same(getF(player, FWD_MAX), 2.5f) && same(getF(player, JUMP_MULT), 1.5f) && same(getF(player, GRAV + 4), -0.9f));
    step(r);
    CHECK("Fly off: the boosts are back", same(getF(player, FWD_MAX), 5.0f) && same(getF(player, JUMP_MULT), 3.0f) && same(getF(player, GRAV + 4), -0.45f));

    std::printf("== the game fights back: it keeps writing its own value (a pretend game thread, 1 write per millisecond)\n");
    r = ask(true, 3.0f, false, 1.0f); step(r);
    {
        std::atomic<bool> run{true}; std::atomic<int> weird{0}, samples{0};
        std::thread game([&] { while (run.load()) { setF(player, FWD_MAX, 2.5f); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } });
        std::thread watcher([&] { while (run.load()) { const float v = getF(player, FWD_MAX); ++samples; if (!(same(v, 2.5f) || same(v, 7.5f))) ++weird; std::this_thread::sleep_for(std::chrono::microseconds(300)); } });
        pause(500);
        run = false; game.join(); watcher.join();
        CHECK("while the game keeps resetting it, the value is only ever 2.5 (the game's) or 7.5 (ours) - never piled up", weird.load() == 0 && samples.load() > 100);
        CHECK("... and the moment the game stops, ours is back within a moment", waitFor([&] { return same(getF(player, FWD_MAX), 7.5f); }, 1));
        CHECK("the link counted the overwrites and said so in the facts", counterOf(link, "game-wrote-back ") > 0 && noted("the game wrote _forwardMaxSpeed back to its own value"));
    }

    std::printf("== the game sets a NEW value of its own (a new setting arrives)\n");
    setF(player, FWD_MAX, 3.0f);
    CHECK("the new value 3.0 becomes the new original: 3.0 x 3 = 9", waitFor([&] { return same(getF(player, FWD_MAX), 9.0f); }, 1));
    r = Request(); step(r);
    CHECK("everything off: the game's NEW value 3.0 is put back (not the old 2.5)", waitFor([&] { return same(getF(player, FWD_MAX), 3.0f); }, 1));
    r = ask(true, 2.0f, false, 1.0f); step(r);
    CHECK("next time: 3.0 x 2 = 6", same(getF(player, FWD_MAX), 6.0f));
    setF(player, FWD_MAX, 100.0f);
    pause(150);
    CHECK("a wildly different value (100, the usual is 2.5) is left alone and reported", same(getF(player, FWD_MAX), 100.0f) && counterOf(link, "odd-values ") >= 1 && noted("far from the usual"));
    setF(player, FWD_MAX, 3.0f);
    CHECK("when the game goes back to 3.0 our x2 follows", waitFor([&] { return same(getF(player, FWD_MAX), 6.0f); }, 1));
    r = Request(); step(r);
    CHECK("all off: back to the game's 3.0", waitFor([&] { return same(getF(player, FWD_MAX), 3.0f); }, 1));

    std::printf("== everything off = every managed number is exactly the game's own again\n");
    {
        bool all = true;
        const int fields[] = {FWD_ACC, FWD_DEC, LAT_MAX, LAT_ACC, LAT_DEC, BWD_MAX, BWD_ACC, BWD_DEC, JUMP_MULT, GRAV, GRAV + 4, GRAV + 8};
        for (int off : fields) { float a, b; std::memcpy(&a, before + off, 4); b = getF(player, off); if (!same(a, b)) { all = false; std::printf("      differs at %d: %g vs %g\n", off, a, b); } }
        CHECK("all other managed fields are bit-for-bit the originals", all);
        CHECK("... and the facts say they were put back", noted("the game's own values were put back") && !noted("SOME COULD NOT BE PUT BACK"));
    }
    CHECK("200 random flips of all the switches later it is still exact", [&] {
        for (int i = 0; i < 200; ++i) { Request q = ask(i % 2 == 0, 1.1f + (i % 40) * 0.1f, i % 3 == 0, 1.1f + (i % 30) * 0.1f, i % 5 == 0 ? 1 : (i % 5 == 1 ? 2 : 0), 5.0f * (i % 19)); step(q); }
        step(Request()); pause(100);
        for (int off : std::initializer_list<int>{FWD_MAX, FWD_ACC, FWD_DEC, LAT_MAX, LAT_ACC, LAT_DEC, BWD_MAX, BWD_ACC, BWD_DEC, JUMP_MULT, GRAV, GRAV + 4, GRAV + 8}) {
            float a = (off == FWD_MAX) ? 3.0f : 0.0f; if (off != FWD_MAX) std::memcpy(&a, before + off, 4);
            if (!same(a, getF(player, off))) return false; }
        return true; }());

    std::printf("== searching again and again never mistakes OUR OWN copies (or leftovers in freed memory) for the game's object\n");
    {
        LinkConfig c7 = cfg; c7.refreshSeconds = 0.3; PlayerLink l7; Controller k7; k7.setAdapter(&l7); l7.start(c7);
        const Request on = ask(true, 2.0f, true, 2.0f, 1, 50);
        l7.setWanted(true);
        CHECK("a second link finds the object and applies", waitFor([&] { l7.setWanted(true); k7.update(on); return k7.status() == Controller::ACTIVE; }, 8));
        const unsigned s0 = l7.searches();
        CHECK("it searches again every 0.3 s while a switch is on", waitFor([&] { k7.update(on); return l7.searches() >= s0 + 6; }, 15));
        CHECK("after 6 more searches it still knows exactly ONE object (no copy of ours, no leftover)", l7.aliveTargets() == 1);
        CHECK("... and the numbers are still exactly original x factor", same(getF(player, FWD_MAX), 6.0f) && same(getF(player, JUMP_MULT), 3.0f));
        l7.setWanted(false); k7.update(Request());
        CHECK("off again: back to the game's own numbers", waitFor([&] { return same(getF(player, FWD_MAX), 3.0f) && same(getF(player, JUMP_MULT), 1.5f); }, 1));
        l7.stop();
    }

    std::printf("== the player object disappears (new scene), then a new one appears\n");
    r = ask(true, 2.0f, false, 1.0f); step(r);
    CHECK("speed x2 is on", same(getF(player, FWD_MAX), 6.0f));
    const uint64_t zero = 0; std::memcpy(static_cast<unsigned char*>(player) + 16, &zero, 8);        // the game destroyed it: the native link is empty
    CHECK("the link notices: not ready, shows 'looking'", waitFor([&] { return !link.ready() && link.uiState() == 2; }, 3));
    CHECK("the facts say it was lost and why", noted("lost the player object") && noted("destroyed"));
    ctl.update(r);
    CHECK("the controller reports no link meanwhile (it does not touch anything)", ctl.status() == Controller::NO_LINK);
    void* player2 = makePL(0);
    CHECK("the new object is found without any button press", waitFor([&] { return link.ready(); }, 8));
    CHECK("... and it gets the speed boost right away (2.5 x 2 = 5)", waitFor([&] { return same(getF(player2, FWD_MAX), 5.0f); }, 2));
    ctl.update(r);
    CHECK("the controller is happy again (ACTIVE)", ctl.status() == Controller::ACTIVE);
    r = Request(); step(r);
    CHECK("all off: the new object has its own 2.5 back", waitFor([&] { return same(getF(player2, FWD_MAX), 2.5f); }, 1));

    std::printf("== head tracking not started -> not the active player\n");
    {
        void* late = makePL(0); static_cast<unsigned char*>(late)[HMD] = 0;       // a fresh copy whose head tracking has not started
        r = ask(true, 2.0f, false, 1.0f); step(r);
        CHECK("it is not picked up", [&] { pause(700); return same(getF(late, FWD_MAX), 2.5f); }());
        r = Request(); step(r);
    }
    link.stop();
    const size_t mainLinkNotes = noteCount();

    std::printf("== failures are reported, never half-done\n");
    {   LinkConfig c2 = cfg; c2.cls = "NoSuchPlayerClass"; PlayerLink l2; l2.start(c2); l2.setWanted(true);
        CHECK("a class that does not exist: FAILED with a clear reason", waitFor([&] { return l2.uiState() == 3; }, 5) && l2.failReason().find("does not exist") != std::string::npos);
        CHECK("... and it does not keep searching", [&] { pause(500); return l2.searches() == 0 && !l2.ready(); }());
        l2.setWanted(false); CHECK("with every switch off the menu goes back to idle", l2.uiState() == 0);
        l2.setWanted(true); CHECK("turning a switch on again retries (and fails the same clear way)", waitFor([&] { return l2.uiState() == 3; }, 5));
        l2.stop(); }
    {   LinkConfig c3 = cfg; c3.ns = "Game"; c3.cls = "MobilePlayerLocomotion"; PlayerLink l3; l3.start(c3); l3.setWanted(true);
        CHECK("a class without the needed fields (the game was updated?): FAILED, names the missing field",
              waitFor([&] { return l3.uiState() == 3; }, 5) && l3.failReason().find("_forwardMaxSpeed is missing") != std::string::npos);
        l3.stop(); }
    {   PlayerLink l4;
        CHECK("the adapter does nothing when it is not linked: capture says no", !l4.captureOriginals() && !l4.ready());
        l4.apply(Effective()); l4.restoreOriginals(); CHECK("... and apply / restore without any object are harmless", true); }

    std::printf("== a memory search that never answers must not freeze anything\n");
    {   LinkConfig c5 = cfg; c5.testHangAfterChunks = 2; c5.stallSeconds = 1; c5.testPollMicros = 20000; PlayerLink l5; l5.start(c5); l5.setWanted(true);
        const auto t0 = std::chrono::steady_clock::now();
        CHECK("the stuck search is given up on and reported as FAILED", waitFor([&] { return l5.uiState() == 3; }, 15) && l5.failReason().find("stopped answering") != std::string::npos);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK("... within a few seconds", took < 8.0);
        CHECK("the menu thread never waited (ready / uiState answer at once)", [&] { const auto a = std::chrono::steady_clock::now(); l5.ready(); l5.uiState(); return std::chrono::steady_clock::now() - a < std::chrono::milliseconds(50); }());
        l5.stop();
        PlayerLink l6; l6.start(cfg); l6.setWanted(true);
        CHECK("a later link, while the stuck search is still there, says so instead of piling on", waitFor([&] { return l6.uiState() == 3; }, 8) && l6.failReason().find("still stuck") != std::string::npos);
        l6.stop(); }

    CHECK("the main link wrote a modest number of lines into the facts (under 120)", mainLinkNotes < 120);
    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
