// aim_link.cpp - see aim_link.h.
#include "aim_link.h"

#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace tzaimlink {

namespace {

using tzaim::Vec3;

double nowSec() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9; }
float rdF(const unsigned char* b, int off) { float f; std::memcpy(&f, b + off, 4); return f; }
int rdI(const unsigned char* b, int off) { int v; std::memcpy(&v, b + off, 4); return v; }
uintptr_t rdP(const unsigned char* b, int off) { uint64_t p; std::memcpy(&p, b + off, 8); return static_cast<uintptr_t>(p); }
Vec3 rdV(const unsigned char* b, int off) { return Vec3{rdF(b, off), rdF(b, off + 4), rdF(b, off + 8)}; }
bool plausiblePtr(uintptr_t p) { return p > 0x10000 && (p & 7) == 0 && p < 0x0000800000000000ULL; }
bool finiteV(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float lenV(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
float distV(const Vec3& a, const Vec3& b) { return lenV(Vec3{a.x - b.x, a.y - b.y, a.z - b.z}); }
std::string fv(const Vec3& v) { char b[80]; std::snprintf(b, sizeof b, "(%.2f, %.2f, %.2f)", static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)); return b; }
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) { char b[1200]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }

std::string handText(int h) {
    std::string r;
    auto add = [&](const char* t) { if (!r.empty()) r += " + "; r += t; };
    if (h & 1) add("left hand timer");
    if (h & 2) add("right hand timer");
    if (h & 4) add("release time moved");
    if (h & 8) add("ball's unheld timer started");
    return r.empty() ? std::string("?") : r;
}
const char* const kHoopNames[6] = {"north hoop", "south hoop", "north hoop 1", "south hoop 1", "north hoop 2", "south hoop 2"};
const int kSampleSteps[] = {3, 6, 10, 15, 22, 30, 40, 52, 66, 82, 100, 120, 145, 170, 200};
const int kNumSamples = static_cast<int>(sizeof kSampleSteps / sizeof kSampleSteps[0]);

// A copy of a game object in OUR memory must never look like a running object (a later memory search would find it and think it is alive). So the
// class pointer in the first 8 bytes is stored inverted, right after the copy.
bool readObject(tzscan::CopyPipe& pipe, unsigned char* out, uintptr_t addr, size_t n) {
    if (!plausiblePtr(addr)) return false;
    if (!pipe.copy(addr, out, n)) return false;
    uint64_t k; std::memcpy(&k, out, 8); k = ~k; std::memcpy(out, &k, 8);
    return true;
}

}  // namespace

AimLink::AimLink() {}
AimLink::~AimLink() { stop(); }

// ---------------------------------------------------------------- writing to the facts file
void AimLink::say(const char* f, ...) {
    if (!cfg_.note) return;
    char b[3000];
    va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap);
    const int n = notes_.fetch_add(1);
    if (n >= cfg_.maxReportLines) return;
    cfg_.note("%s", b);
    if (n + 1 == cfg_.maxReportLines) cfg_.note("aim: (that is enough aim lines - the rest are not written, so the facts file stays small)");
}

void AimLink::queue(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMu_);
    if (out_.size() < 64) out_.push_back(line);
}

void AimLink::flush() {
    std::vector<std::string> v;
    { std::lock_guard<std::mutex> lock(outMu_); v.swap(out_); }
    for (const std::string& s : v) {
        if (s.compare(0, 7, "points:") == 0) sayPoints("%s", s.c_str());        // stage D10: the points lines have their own budget
        else if (s.compare(0, 7, "hitbox:") == 0) sayHitbox("%s", s.c_str());   // stage D11: so do the hitbox lines
        else say("%s", s.c_str());
    }
}

void AimLink::setLast(const std::string& text) { std::lock_guard<std::mutex> lock(mu_); lastText_ = text; }

void AimLink::fail(const std::string& why) {
    { std::lock_guard<std::mutex> lock(mu_); failed_ = true; failReason_ = why; }
    say("aim: link FAILED: %s", why.c_str());
}

void AimLink::killEngine(const std::string& why) {
    bool first = false;
    { std::lock_guard<std::mutex> lock(mu_); if (engineDeadWhy_.empty()) { engineDeadWhy_ = why; first = true; } }
    engineDead_.store(true);
    if (first) queue("aim: STOPPED touching the game: " + why);
}

// ---------------------------------------------------------------- start / stop / the menu's request
bool AimLink::start(const Config& cfg) {
    cfg_ = cfg;
    if (started_) return true;
    quit_.store(false);
    pipesOk_ = pipeGame_.open(0, nullptr) && pipeLink_.open(0, nullptr);
    if (!pipesOk_) { fail("the safe copy pipe does not work here"); return false; }
    if (pthread_create(&th_, nullptr, threadMain, this) != 0) { fail("could not start the aim link thread"); return false; }
    started_ = true;
    return true;
}

void AimLink::stop() {
    if (!started_) return;
    quit_.store(true);
    pthread_join(th_, nullptr);
    started_ = false;
}

void* AimLink::threadMain(void* self) { static_cast<AimLink*>(self)->loop(); return nullptr; }

void AimLink::setAsk(bool on, float capM, bool holdY) { setAsk(on ? 1 : 0, capM, holdY); }

// The link as a whole (aim OR points) just came to life: forget old failures and look again.
void AimLink::initOn() {
    onSinceAtomic_.store(nowSec());
    {
        std::lock_guard<std::mutex> lock(mu_);
        failed_ = false; failReason_.clear(); engineDeadWhy_.clear();
        if (!layoutReady_.load()) { nextLayoutAt_ = 0; layoutTries_ = 0; }
        nextSearchAt_ = 0;
    }
    if (engineDead_.exchange(false)) resetExc_.store(true);
    if (!bound_.load()) { fastFailed_.store(false); needBcm_.store(false); }
}

void AimLink::setAsk(int mode, float capM, bool holdY) {
    if (mode < 0 || mode > 2) mode = 0;
    capM_.store(capM);
    const bool yWas = holdY_.exchange(holdY);
    const int modeWas = mode_.exchange(mode);
    const bool linkOn = linkWanted();                              // stage D10 / D11: the "Shot points" and hitbox parts also need the link
    const bool linkWas = on_.exchange(linkOn);
    const char* name = mode == 2 ? "BANK" : "Direct";
    if (linkOn && !linkWas) initOn();
    if (mode != 0 && modeWas == 0) {
        if (mode == 2 && !bankReady_.load()) { std::lock_guard<std::mutex> lock(mu_); nextBankLayoutAt_ = 0; bankLayoutTries_ = 0; bankFailWhy_.clear(); }
        say("aim: switch turned ON (%s mode), max shot distance %s, hold Y to aim: %s", name, tzaim::capLabel(capM).c_str(), holdY ? "ON" : "off");
    } else if (mode == 0 && modeWas != 0) {
        say("aim: switch turned OFF");
    } else if (mode != 0 && modeWas != mode) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (mode == 2 && !bankReady_.load()) { nextBankLayoutAt_ = 0; bankLayoutTries_ = 0; bankFailWhy_.clear(); }
            lastText_.clear();
        }
        say("aim: mode changed to %s", name);
    } else if (mode != 0 && yWas != holdY) {
        say("aim: 'hold Y to aim' turned %s", holdY ? "ON" : "off");
    }
}

// The real Y button. Two places call this (see the header). Nothing here touches the game.
void AimLink::noteY(bool held, bool fromDoorway) {
    if (!on_.load(std::memory_order_relaxed)) return;
    const double t = tnow();
    const int i = fromDoorway ? 1 : 0;
    (fromDoorway ? yReadsDoor_ : yReads_).fetch_add(1, std::memory_order_relaxed);
    if (held) (fromDoorway ? yHeldDoor_ : yHeld_).fetch_add(1, std::memory_order_relaxed);
    yNow_[i].store(held, std::memory_order_relaxed);
    ySeen_[i].store(t, std::memory_order_relaxed);
    if (held) yHeldAt_.store(t, std::memory_order_relaxed);
}

bool AimLink::yHeldNow(double now) const {
    for (int i = 0; i < 2; ++i) if (yNow_[i].load(std::memory_order_relaxed) && now - ySeen_[i].load(std::memory_order_relaxed) <= 0.15) return true;
    return false;
}

bool AimLink::yReadable(double now) const {
    for (int i = 0; i < 2; ++i) if (now - ySeen_[i].load(std::memory_order_relaxed) <= 1.0) return true;     // Y is read many times a second; nothing for a whole second = it cannot be read
    return false;
}

// ---------------------------------------------------------------- the menu texts
int AimLink::uiState() const {
    if (!on_.load() || mode_.load() == 0) return 0;          // (stage D10: the link also runs for "Shot points" alone; that is not the Aimbot's state)
    if (!failReason().empty()) return 3;
    return bound_.load() ? 1 : 2;
}

std::string AimLink::failReason() const {
    if (!on_.load()) return "";
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (failed_) return failReason_;
        if (!engineDeadWhy_.empty()) return engineDeadWhy_;
    }
    const double since = onSinceAtomic_.load();
    if (since > 0 && nowSec() - since > 10.0 && layoutReady_.load()) {
        if (calls_.load() == 0) return "the game never called the controller doorway, so the aimbot cannot see your throws";
        if (ticks_.load() == 0) return std::string("the controller doorway is called from another thread than ") + (cfg_.gameThreadName ? cfg_.gameThreadName : "?") + ", so the aimbot stays out of the game";
    }
    return "";
}

std::string AimLink::headline() const {
    if (!on_.load() || mode_.load() == 0) return "Aimbot is off";
    const std::string why = failReason();
    if (!why.empty()) return "FAILED: " + why;
    if (!layoutReady_.load()) return "looking at the game's code ...";
    if (!bound_.load()) return "looking for your ball control ...";
    if (mode_.load() == 2) {
        if (bankReady_.load()) return "Bank: connected - waiting for your shot";
        std::lock_guard<std::mutex> lock(mu_);
        if (!bankFailWhy_.empty()) return "Bank unavailable: " + bankFailWhy_;
        return "Bank: looking at the game's backboard code ...";
    }
    return "connected - waiting for your shot";
}

std::string AimLink::lastShotText() const { std::lock_guard<std::mutex> lock(mu_); return lastText_; }

Counters AimLink::counters() const {
    Counters c;
    c.ticks = ticks_.load(); c.releases = releases_.load(); c.shotLike = shotLike_.load(); c.aimed = aimed_.load();
    c.notHoldingY = notHoldingY_.load(); c.yUnknown = yUnknown_.load();
    c.yReadsProbe = yReads_.load(); c.yHeldProbe = yHeld_.load(); c.yReadsDoorway = yReadsDoor_.load(); c.yHeldDoorway = yHeldDoor_.load();
    c.tooFar = tooFar_.load(); c.wrongDirection = wrongDir_.load(); c.noHoop = noHoop_.load(); c.notAShot = notShot_.load(); c.noSolution = noSolution_.load(); c.refused = refused_.load();
    c.errors = errors_.load(); c.overridden = overridden_.load(); c.scored = scored_.load(); c.missed = missed_.load();
    c.bankAimed = bankAimed_.load(); c.bankUnavailable = bankUnavail_.load(); c.bankScored = bankScored_.load(); c.bankMissed = bankMissed_.load();
    return c;
}

std::string AimLink::summary() const {
    const Counters c = counters();
    const char* state = "off";
    const int ui = uiState();
    if (ui == 1) state = "connected"; else if (ui == 2) state = "looking"; else if (ui == 3) state = "FAILED";
    std::string why = ui == 3 ? failReason() : std::string();
    const char* modeName = mode_.load() == 2 ? "BANK" : (mode_.load() == 1 ? "Direct" : "off");
    // (the original D8c fields keep their order; the stage D9 fields are at the end)
    return fmt("aim: link %s%s%s | doorway calls %llu, used %llu | releases %llu, shot-like %llu, AIMED %llu (scored %llu, missed %llu, game changed our speed %llu) | left alone: too far %llu, wrong direction %llu, Y not held %llu, Y unreadable %llu, no hoop %llu, no solution %llu, not a shot %llu, other %llu | engine errors %llu | Y button reads: menu thread %llu (held in %llu), doorway %llu (held in %llu) | mode %s | BANK shots %llu (scored %llu, missed %llu), Bank unavailable %llu",
               state, why.empty() ? "" : ": ", why.c_str(), calls_.load(), c.ticks, c.releases, c.shotLike, c.aimed, c.scored, c.missed, c.overridden,
               c.tooFar, c.wrongDirection, c.notHoldingY, c.yUnknown, c.noHoop, c.noSolution, c.notAShot, c.refused, c.errors,
               c.yReadsProbe, c.yHeldProbe, c.yReadsDoorway, c.yHeldDoorway, modeName, c.bankAimed, c.bankScored, c.bankMissed, c.bankUnavailable);
}

// ---------------------------------------------------------------- the link thread: layout and the memory search
void AimLink::loop() {
    while (!quit_.load()) {
        flush();
        const double now = nowSec();
        if (on_.load()) {
            bool doLayout = false, doSrch = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!layoutReady_.load() && !failed_ && now >= nextLayoutAt_) doLayout = true;
                else if (layoutReady_.load() && !failed_ && needBcm_.load() && fastFailed_.load() && !bound_.load() && now >= nextSearchAt_ && now - lastSearchEnd_ >= cfg_.minSearchGapSeconds) doSrch = true;
            }
            bool doBank = false, doPoints = false, doHitbox = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (layoutReady_.load() && !failed_ && mode_.load() == 2 && !bankReady_.load() && now >= nextBankLayoutAt_) doBank = true;
                if (layoutReady_.load() && !failed_ && points_.load() != 0 && !pointsReady_.load() && !pointsDead_ && now >= nextPointsAt_) doPoints = true;
                if (layoutReady_.load() && !failed_ && (hitboxX10_.load() != 0 || hitboxSee_.load()) && !hitboxReady_.load() && !hitboxDead_.load() && now >= nextHitboxAt_) doHitbox = true;
            }
            if (doHitbox) {
                std::string why; bool transient = false;
                if (resolveHitboxLayout(&why, &transient)) { hitboxReady_.store(true, std::memory_order_release); std::lock_guard<std::mutex> lock(mu_); hitboxFailWhy_.clear(); }
                else {
                    ++hitboxTries_;
                    if (transient) {
                        { std::lock_guard<std::mutex> lock(mu_); hitboxFailWhy_ = why; nextHitboxAt_ = nowSec() + cfg_.retrySoonSeconds + std::min(12.0, hitboxTries_ * 2.0); }
                        if (hitboxTries_ == 1 || hitboxTries_ % 10 == 0) queueHitbox("hitbox: not ready yet (" + why + ") - trying again in a few seconds");
                    } else { hbDie(why); queueHitbox("hitbox: cannot be used: " + why); }
                }
            }
            if (doPoints) {
                std::string why; bool transient = false;
                if (resolvePointsLayout(&why, &transient)) { pointsReady_.store(true, std::memory_order_release); std::lock_guard<std::mutex> lock(mu_); pointsFailWhy_.clear(); }
                else {
                    std::lock_guard<std::mutex> lock(mu_);
                    ++pointsTries_;
                    pointsFailWhy_ = why;
                    if (transient) { nextPointsAt_ = nowSec() + cfg_.retrySoonSeconds + std::min(12.0, pointsTries_ * 2.0); if (pointsTries_ == 1 || pointsTries_ % 10 == 0) queuePoints("points: not ready yet (" + why + ") - trying again in a few seconds"); }
                    else { pointsDead_ = true; queuePoints("points: cannot be used: " + why); }
                }
            }
            if (doBank) {
                std::string why; bool transient = false;
                if (resolveBankLayout(&why, &transient)) { bankReady_.store(true, std::memory_order_release); std::lock_guard<std::mutex> lock(mu_); bankFailWhy_.clear(); }
                else {
                    std::lock_guard<std::mutex> lock(mu_);
                    ++bankLayoutTries_;
                    bankFailWhy_ = why;
                    if (transient) { nextBankLayoutAt_ = nowSec() + cfg_.retrySoonSeconds + std::min(12.0, bankLayoutTries_ * 2.0); if (bankLayoutTries_ == 1 || bankLayoutTries_ % 10 == 0) queue("aim: BANK not ready yet (" + why + ") - trying again in a few seconds"); }
                    else { nextBankLayoutAt_ = 1e18; queue("aim: BANK cannot be used: " + why); }
                }
            }
            if (doLayout) {
                std::string why; bool transient = false;
                if (resolveLayout(&why, &transient)) { /* published inside */ }
                else {
                    std::lock_guard<std::mutex> lock(mu_);
                    ++layoutTries_;
                    if (transient) {
                        nextLayoutAt_ = nowSec() + cfg_.retrySoonSeconds + std::min(12.0, layoutTries_ * 2.0);
                        if (layoutTries_ == 1 || layoutTries_ % 10 == 0) { lastText_.clear(); queue("aim: not ready yet (" + why + ") - trying again in a few seconds"); }
                    } else { failed_ = true; failReason_ = why; queue("aim: link FAILED: " + why); }
                }
            } else if (doSrch) {
                needBcm_.store(false);
                doSearch();
            }
        }
        usleep(static_cast<useconds_t>(cfg_.linkTickMs) * 1000);
    }
    flush();
}

bool AimLink::resolveLayout(std::string* why, bool* transient) {
    *transient = false;
    Layout L; std::string pw;
    if (!cfg_.provider || !cfg_.provider(&L.api, &pw)) {
        *why = "the game's runtime (libil2cpp.so) is not available yet" + (pw.empty() ? std::string() : ": " + pw);
        *transient = true; return false;
    }
    if (!L.api.runtime_invoke || !L.api.class_get_method_from_name) {
        *why = "this game's runtime has no il2cpp_runtime_invoke / il2cpp_class_get_method_from_name, so the ball's speed can neither be read nor set";
        return false;
    }
    L.ball = tzscan::findClass(L.api, "ShovelTools", "Basketball");
    if (!L.ball.found) { *why = L.ball.error; *transient = L.ball.error.find("not ready") != std::string::npos; return false; }
    L.bcm = tzscan::findClass(L.api, "ShovelTools", "BallControlManager");
    if (!L.bcm.found) { *why = L.bcm.error; *transient = L.bcm.error.find("not ready") != std::string::npos; return false; }
    L.gm = tzscan::findClass(L.api, "ShovelTools", "GameManager");
    if (!L.gm.found) { *why = L.gm.error; *transient = L.gm.error.find("not ready") != std::string::npos; return false; }

    // a field must have this name AND this type AND lie inside the object; otherwise the game was updated and we stop instead of guessing
    auto field = [&](const tzscan::ClassInfo& ci, const char* name, const char* type, int bytes, bool required, int* out) -> bool {
        const tzscan::FieldInfo* fi = ci.field(name);
        if (!fi) { if (required) *why = std::string("the field ") + name + " is missing in " + ci.fullName + " (the game was updated?)"; return !required; }
        if (type && fi->typeName != type) { if (required) *why = std::string("the field ") + name + " in " + ci.fullName + " has type " + fi->typeName + ", expected " + type; return !required; }
        if (fi->offset + bytes > ci.size) { if (required) *why = std::string("the field ") + name + " lies outside " + ci.fullName; return !required; }
        *out = fi->offset; return true;
    };
    if (!field(L.ball, "_rigidbody", "UnityEngine.Rigidbody", 8, true, &L.bRigid)) return false;
    static const char* const hoopNames[6] = {"_northHoopPosition", "_southHoopPosition", "_northHoop01Position", "_southHoop01Position", "_northHoop02Position", "_southHoop02Position"};
    for (int i = 0; i < 6; ++i) if (!field(L.ball, hoopNames[i], "UnityEngine.Vector3", 12, i < 2, &L.bHoop[i])) return false;
    field(L.ball, "_wasShot", "System.Boolean", 1, false, &L.bWasShot);
    field(L.ball, "_shotMade", "System.Boolean", 1, false, &L.bShotMade);
    field(L.ball, "_unheldTime", "System.Single", 4, false, &L.bUnheld);
    field(L.ball, "_hoopsLength", "System.Int32", 4, false, &L.bHoopsLen);
    field(L.ball, "_originalDrag", "System.Single", 4, false, &L.bOrigDrag);
    field(L.ball, "_currentBallPosition", "UnityEngine.Vector3", 12, false, &L.bCurPos);

    if (!field(L.bcm, "_basketball", "ShovelTools.Basketball", 8, true, &L.cBall)) return false;
    field(L.bcm, "_releasedLeftTimer", "System.Single", 4, false, &L.cTimerL);
    field(L.bcm, "_releasedRightTimer", "System.Single", 4, false, &L.cTimerR);
    field(L.bcm, "lastReleaseTime", "System.Single", 4, false, &L.cLastRel);
    field(L.bcm, "_lastRawThrowVelocity", "UnityEngine.Vector3", 12, false, &L.cRaw);
    if (L.cTimerL < 0 && L.cTimerR < 0 && L.cLastRel < 0 && L.bUnheld < 0) { *why = "the game has none of the release timers (_releasedLeftTimer, _releasedRightTimer, lastReleaseTime, _unheldTime): the game was updated?"; return false; }

    // stage D8b: the player's jump state (optional; since stage D8c it is only written into the report - the Aimbot no longer depends on it).
    // From the game's own files: BallControlManager._playerLocomotion (a link at +144) -> ShovelTools.PlayerLocomotion._locomotionVerticalState (a number at +828).
    // Its type has four values named FLOOR, JUMPING, FALLING, GRABBING. Only "0 = FLOOR" is relied on (a 0 was read in an earlier scan; the first name of an enum is 0).
    field(L.bcm, "_playerLocomotion", "ShovelTools.PlayerLocomotion", 8, false, &L.cLoco);
    L.loco = tzscan::findClass(L.api, "ShovelTools", "PlayerLocomotion");
    if (!L.loco.found) L.jumpWhy = "the class ShovelTools.PlayerLocomotion was not found";
    else if (L.cLoco < 0) L.jumpWhy = "your ball control has no _playerLocomotion link (the game was updated?)";
    else {
        std::string keep = *why;
        if (!field(L.loco, "_locomotionVerticalState", "ShovelTools.PlayerLocomotion.LocomotionVerticalState", 4, true, &L.lVert)) { L.jumpWhy = *why; L.lVert = -1; }
        *why = keep;
        field(L.loco, "_isLeftJumpPressed", "System.Boolean", 1, false, &L.lJumpL);
        field(L.loco, "_isRightJumpPressed", "System.Boolean", 1, false, &L.lJumpR);
        field(L.loco, "_locomotionState", nullptr, 4, false, &L.lState);
        if (L.lVert >= 0) {
            L.jumpOk = true;
            int mx = 24; for (int off : {L.lVert + 4, L.lJumpL + 1, L.lJumpR + 1, L.lState + 4}) if (off > mx) mx = off;
            L.lRead = mx;
        }
    }

    field(L.gm, "_playerBallControlManager", "ShovelTools.BallControlManager", 8, false, &L.gBcm);
    field(L.gm, "_gameState", nullptr, 4, false, &L.gState);
    field(L.gm, "_isInCompetitionMode", "System.Boolean", 1, false, &L.gComp);
    field(L.gm, "_isGMMode", "System.Boolean", 1, false, &L.gGm);
    field(L.gm, "_isSolo", "System.Boolean", 1, false, &L.gSolo);
    field(L.gm, "_isNBA", "System.Boolean", 1, false, &L.gNba);

    // the engine's own functions (names and argument counts were all seen in the stage D7c facts file)
    std::string err;
    L.rbKlass = tzscan::findClassHandle(L.api, "UnityEngine", "Rigidbody", &err);
    if (!L.rbKlass) { *why = "the engine class UnityEngine.Rigidbody was not found: " + err; return false; }
    L.mGetVel = L.api.class_get_method_from_name(L.rbKlass, "get_velocity", 0);
    L.mSetVel = L.api.class_get_method_from_name(L.rbKlass, "set_velocity", 1);
    L.mGetPos = L.api.class_get_method_from_name(L.rbKlass, "get_position", 0);
    L.mGetDrag = L.api.class_get_method_from_name(L.rbKlass, "get_drag", 0);
    L.mGetUseGravity = L.api.class_get_method_from_name(L.rbKlass, "get_useGravity", 0);
    if (!L.mGetVel || !L.mSetVel || !L.mGetPos) { *why = "the engine has no Rigidbody get_velocity / set_velocity / get_position (found: " + std::string(L.mGetVel ? "get_velocity " : "") + (L.mSetVel ? "set_velocity " : "") + (L.mGetPos ? "get_position" : "") + ")"; return false; }
    if (void* phys = tzscan::findClassHandle(L.api, "UnityEngine", "Physics", &err)) L.mGetGravity = L.api.class_get_method_from_name(phys, "get_gravity", 0);
    if (void* tm = tzscan::findClassHandle(L.api, "UnityEngine", "Time", &err)) L.mGetFixedDt = L.api.class_get_method_from_name(tm, "get_fixedDeltaTime", 0);
    L.mGmInstance = L.api.class_get_method_from_name(L.gm.klass(), "get_Instance", 0);
    L.mGmOwned = L.api.class_get_method_from_name(L.gm.klass(), "GetOwnedBallControlManager", 0);

    L.ok = true;
    {
        std::lock_guard<std::mutex> lock(mu_);
        L_ = L;
        bcmBuf_.assign(static_cast<size_t>(L.bcm.size), 0);
        ballBuf_.assign(static_cast<size_t>(L.ball.size), 0);
        gmBuf_.assign(static_cast<size_t>(L.gm.size), 0);
        locoBuf_.assign(static_cast<size_t>(L.lRead > 0 ? L.lRead : 24), 0);
    }
    char b[900];
    std::snprintf(b, sizeof b, "aim: found the game's classes. Ball %s (size %d): _rigidbody@%d, north hoop@%d, south hoop@%d, _wasShot@%d, _shotMade@%d, _unheldTime@%d | ball control (size %d): _basketball@%d, left timer@%d, right timer@%d, lastReleaseTime@%d | GameManager (size %d): _playerBallControlManager@%d | engine: get_velocity %s, set_velocity %s, get_position %s, get_drag %s, get_useGravity %s, Physics.get_gravity %s, Time.get_fixedDeltaTime %s | ways to find your ball control: GameManager.get_Instance %s + GetOwnedBallControlManager %s, memory search %s",
                  L.ball.fullName.c_str(), L.ball.size, L.bRigid, L.bHoop[0], L.bHoop[1], L.bWasShot, L.bShotMade, L.bUnheld, L.bcm.size, L.cBall, L.cTimerL, L.cTimerR, L.cLastRel, L.gm.size, L.gBcm,
                  L.mGetVel ? "yes" : "NO", L.mSetVel ? "yes" : "NO", L.mGetPos ? "yes" : "NO", L.mGetDrag ? "yes" : "NO", L.mGetUseGravity ? "yes" : "NO", L.mGetGravity ? "yes" : "NO", L.mGetFixedDt ? "yes" : "NO",
                  L.mGmInstance ? "yes" : "NO", L.mGmOwned ? "yes" : "NO", L.gBcm >= 0 ? "possible" : "NOT possible");
    say("%s", b);
    if (L.jumpOk) say("aim: jump state can be read (report only, the Aimbot does not use it): ball control _playerLocomotion@%d -> player object (size %d) _locomotionVerticalState@%d (read %d bytes).", L.cLoco, L.loco.size, L.lVert, L.lRead);
    else say("aim: jump state NOT readable (report only, the Aimbot does not use it): %s.", L.jumpWhy.c_str());
    layoutReady_.store(true, std::memory_order_release);
    return true;
}

// Fallback way to find your ball control object: look through memory for the GameManager and read the link _playerBallControlManager from it.
void AimLink::doSearch() {
    const double t0 = nowSec();
    if (L_.gBcm < 0) { fail("no way to find your ball control: GameManager.get_Instance did not work and the field _playerBallControlManager does not exist"); return; }
    ++searches_;
    tzscan::CopySearch cs = tzscan::findCopies(L_.gm, 32, cfg_.searchSeconds, cfg_.stallSeconds, cfg_.testHangAfterChunks, cfg_.testPollMicros);
    const double t1 = nowSec();
    {
        std::lock_guard<std::mutex> lock(mu_);
        lastSearchEnd_ = t1;
    }
    if (!cs.ok) { fail("looking for the GameManager in memory did not work: " + cs.error); return; }
    std::vector<std::pair<uintptr_t, uintptr_t>> cands;        // (ball control pointer, the GameManager copy it came from)
    int junk = 0;
    for (const tzscan::Copy& c : cs.copies) {
        if (c.bytes.size() < static_cast<size_t>(L_.gm.size)) { ++junk; continue; }
        const uintptr_t p = rdP(c.bytes.data(), L_.gBcm);
        if (!plausiblePtr(p)) { ++junk; continue; }
        unsigned char head[24];
        if (!pipeLink_.copy(p, head, sizeof head)) { ++junk; continue; }
        uint64_t klass, cached; std::memcpy(&klass, head, 8); std::memcpy(&cached, head + 16, 8); std::memset(head, 0, 8);
        if (klass != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(L_.bcm.klass())) || cached == 0) { ++junk; continue; }
        bool have = false;
        for (const auto& x : cands) if (x.first == p) have = true;
        if (!have) cands.push_back({p, c.addr});
    }
    char b[300];
    std::snprintf(b, sizeof b, "aim: search #%d: %.1f s, %llu MB read, %d GameManager copies, %d usable ball controls (%d copies had no valid link)",
                  searches_, t1 - t0, cs.bytesRead >> 20, static_cast<int>(cs.copies.size()), static_cast<int>(cands.size()), junk);
    if (cands.size() == 1) {
        say("%s -> using %llx", b, static_cast<unsigned long long>(cands[0].first));
        gmFound_.store(cands[0].second); bcmFound_.store(cands[0].first);
        bcmGen_.fetch_add(1, std::memory_order_release);
        emptySearches_ = 0;
    } else if (cands.size() > 1) {
        fail(std::string(b) + " -> more than one ball control candidate: I cannot tell which one is yours");
    } else {
        ++emptySearches_;
        std::lock_guard<std::mutex> lock(mu_);
        nextSearchAt_ = t1 + std::min(15.0, cfg_.retrySoonSeconds + emptySearches_ * 2.0);
        if (emptySearches_ <= 3 || emptySearches_ % 10 == 0) { lastText_.clear(); queue(std::string(b) + " -> nothing yet, trying again in a few seconds"); }
        needBcm_.store(true);
    }
}

// ---------------------------------------------------------------- the game thread
double AimLink::tnow() const { return cfg_.clock ? cfg_.clock() : nowSec(); }

bool AimLink::gameThreadOk() const {
    if (cfg_.gameThreadName && cfg_.gameThreadName[0]) {
        char name[32] = {0};
        prctl(PR_GET_NAME, reinterpret_cast<unsigned long>(name), 0, 0, 0);
        if (std::strncmp(name, cfg_.gameThreadName, 15) != 0) return false;
    }
    if (L_.api.thread_current && !L_.api.thread_current()) return false;         // a thread the game's runtime does not know must never call into the game
    return true;
}

bool AimLink::readObj(unsigned char* out, uintptr_t addr, size_t n) { return readObject(pipeGame_, out, addr, n); }

bool AimLink::liveObj(const unsigned char* obj, uint64_t klassInv, bool unityObject) const {
    uint64_t k, cached;
    std::memcpy(&k, obj, 8); std::memcpy(&cached, obj + 16, 8);
    if (k != klassInv) return false;
    return !unityObject || cached != 0;
}

bool AimLink::rigidOk(uintptr_t rb) {
    unsigned char head[24];
    if (!plausiblePtr(rb) || !pipeGame_.copy(rb, head, sizeof head)) return false;
    uint64_t k, cached; std::memcpy(&k, head, 8); std::memcpy(&cached, head + 16, 8); std::memset(head, 0, 8);
    return k == static_cast<uint64_t>(reinterpret_cast<uintptr_t>(L_.rbKlass)) && cached != 0;
}

// ---- calling into the game (game thread only). Every call is checked: an error thrown inside the game is caught here, and three in a row stop everything.
void* AimLink::invoke(void* method, uintptr_t self, void** args, bool* threw) {
    *threw = false;
    if (!method || !L_.api.runtime_invoke) { *threw = true; return nullptr; }
    void* exc = nullptr;
    void* r = L_.api.runtime_invoke(method, reinterpret_cast<void*>(self), args, &exc);
    if (exc) {
        errors_.fetch_add(1); ++g_.excRun; *threw = true;
        if (g_.excRun >= 3) killEngine("the game's engine reported errors on three calls in a row (calling it from here does not work)");
        return nullptr;
    }
    g_.excRun = 0;
    return r;
}

bool AimLink::callVec3(void* method, uintptr_t self, Vec3* out) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    if (threw || !r) return false;
    unsigned char b[12];
    if (!pipeGame_.copy(reinterpret_cast<uintptr_t>(r) + 16, b, 12)) return false;       // the result is a "boxed" value: the number starts after the 16-byte object header
    *out = rdV(b, 0);
    return finiteV(*out);
}

bool AimLink::callFloat(void* method, uintptr_t self, float* out) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    if (threw || !r) return false;
    unsigned char b[4];
    if (!pipeGame_.copy(reinterpret_cast<uintptr_t>(r) + 16, b, 4)) return false;
    *out = rdF(b, 0);
    return std::isfinite(*out);
}

bool AimLink::callBool(void* method, uintptr_t self, bool* out) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    if (threw || !r) return false;
    unsigned char b[1];
    if (!pipeGame_.copy(reinterpret_cast<uintptr_t>(r) + 16, b, 1)) return false;
    *out = b[0] != 0;
    return true;
}

uintptr_t AimLink::callObj(void* method, uintptr_t self) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    return threw ? 0 : reinterpret_cast<uintptr_t>(r);
}

bool AimLink::callSetVec3(void* method, uintptr_t self, const Vec3& v) {
    Vec3 tmp = v; void* args[1] = {&tmp};
    bool threw; invoke(method, self, args, &threw);
    return !threw;
}

std::string AimLink::gameFlags() {
    if (!g_.gm || L_.gm.size <= 0) return "game: GameManager not known";
    if (!readObj(gmBuf_.data(), g_.gm, static_cast<size_t>(L_.gm.size)) || !liveObj(gmBuf_.data(), L_.gm.klassInv, L_.gm.unityObject)) return "game: GameManager unreadable";
    const unsigned char* b = gmBuf_.data();
    auto flag = [&](int off) { return off >= 0 ? std::string(b[off] ? "1" : "0") : std::string("?"); };
    return fmt("game: state=%d inCompetition=%s gmMode=%s solo=%s nba=%s", L_.gState >= 0 ? rdI(b, L_.gState) : -1, flag(L_.gComp).c_str(), flag(L_.gGm).c_str(), flag(L_.gSolo).c_str(), flag(L_.gNba).c_str());
}

// ---- finding your ball control object
void AimLink::dropBcm(const char* why) {
    if (g_.phase != IDLE) abandon("your ball control object went away");
    g_.bcm = 0; g_.haveTimers = false; g_.fastTries = 0; g_.lastBall = 0;
    bound_.store(false); fastFailed_.store(false);
    if (++g_.bindNotes <= 8) queue(std::string("aim: lost your ball control object: ") + why);
}

void AimLink::fastResolve(double now) {
    (void)now;
    if (!L_.mGmInstance || !L_.mGmOwned) { fastFailed_.store(true); queue("aim: GameManager.get_Instance / GetOwnedBallControlManager not available: will use the memory search"); return; }
    const uintptr_t gm = callObj(L_.mGmInstance, 0);
    if (!plausiblePtr(gm) || !readObj(gmBuf_.data(), gm, static_cast<size_t>(L_.gm.size)) || !liveObj(gmBuf_.data(), L_.gm.klassInv, L_.gm.unityObject)) {
        if (g_.fastTries >= 3) { fastFailed_.store(true); queue("aim: GameManager.get_Instance() gave no usable GameManager (3 tries): will use the memory search"); }
        return;
    }
    const uintptr_t bcm = callObj(L_.mGmOwned, gm);
    if (!plausiblePtr(bcm) || !readObj(bcmBuf_.data(), bcm, static_cast<size_t>(L_.bcm.size)) || !liveObj(bcmBuf_.data(), L_.bcm.klassInv, L_.bcm.unityObject)) {
        if (g_.fastTries >= 3) { fastFailed_.store(true); queue("aim: GetOwnedBallControlManager() gave no usable ball control (3 tries): will use the memory search"); }
        return;
    }
    g_.gm = gm; g_.bcm = bcm; g_.haveTimers = false;
    bound_.store(true);
    queue(fmt("aim: bound to your ball control object %llx (GameManager %llx) by asking the game: GameManager.get_Instance().GetOwnedBallControlManager()", static_cast<unsigned long long>(bcm), static_cast<unsigned long long>(gm)));
}

bool AimLink::ensureBcm(double now) {
    if (g_.bcm) return true;
    const unsigned gen = bcmGen_.load(std::memory_order_acquire);
    if (gen != g_.seenGen) {
        g_.seenGen = gen;
        const uintptr_t p = bcmFound_.load();
        if (p && readObj(bcmBuf_.data(), p, static_cast<size_t>(L_.bcm.size)) && liveObj(bcmBuf_.data(), L_.bcm.klassInv, L_.bcm.unityObject)) {
            g_.bcm = p; g_.gm = gmFound_.load(); g_.haveTimers = false;
            bound_.store(true);
            queue(fmt("aim: bound to your ball control object %llx (found by the memory search)", static_cast<unsigned long long>(p)));
            return true;
        }
    }
    if (now < g_.nextResolveAt) return false;
    g_.nextResolveAt = now + 1.0;
    if (!fastFailed_.load()) { ++g_.fastTries; fastResolve(now); if (g_.bcm) return true; }
    if (fastFailed_.load()) needBcm_.store(true);
    return false;
}

// ---- the main tick
void AimLink::onGameThread() {
    if (!started_) return;
    struct Guard { bool& f; explicit Guard(bool& x) : f(x) { f = true; } ~Guard() { f = false; } };
    if (!on_.load(std::memory_order_relaxed)) {
        if (g_.phase != IDLE && !g_.inTick) abandon("the switch was turned off");
        if (!g_.ptsBalls.empty() && !g_.inTick && layoutReady_.load(std::memory_order_acquire) && gameThreadOk()) pointsStandDown();      // stage D10: the points switch went off too
        if (g_.hbOn && !g_.inTick && layoutReady_.load(std::memory_order_acquire) && gameThreadOk()) { Guard guard(g_.inTick); hitboxStandDown("every switch is off"); }       // stage D11: put the hitboxes back
        g_.haveTimers = false;
        return;
    }
    calls_.fetch_add(1, std::memory_order_relaxed);
    if (!layoutReady_.load(std::memory_order_acquire)) return;
    if (g_.inTick) return;                                     // the game called us from inside one of our own calls
    if (!gameThreadOk()) return;
    if (resetExc_.exchange(false)) g_.excRun = 0;
    if (engineDead_.load()) return;
    Guard guard(g_.inTick);
    ticks_.fetch_add(1, std::memory_order_relaxed);
    const double now = tnow();

    if (!ensureBcm(now)) return;
    unsigned char* b = bcmBuf_.data();
    if (!readObj(b, g_.bcm, static_cast<size_t>(L_.bcm.size))) { dropBcm("its memory can no longer be read"); return; }
    if (!liveObj(b, L_.bcm.klassInv, L_.bcm.unityObject)) { dropBcm("the game destroyed it (new scene?)"); return; }

    const float tl = L_.cTimerL >= 0 ? rdF(b, L_.cTimerL) : std::nanf("");
    const float tr = L_.cTimerR >= 0 ? rdF(b, L_.cTimerR) : std::nanf("");
    const float lr = L_.cLastRel >= 0 ? rdF(b, L_.cLastRel) : std::nanf("");
    const uintptr_t ballPtr = rdP(b, L_.cBall);
    if (plausiblePtr(ballPtr)) { g_.lastBall = ballPtr; g_.lastBallT = now; }

    int hand = 0;
    if (g_.haveTimers) {
        if (std::isfinite(tl) && std::isfinite(g_.prevL) && tl < g_.prevL - 0.02f) hand |= 1;           // a timer that jumps back to zero = you let go
        if (std::isfinite(tr) && std::isfinite(g_.prevR) && tr < g_.prevR - 0.02f) hand |= 2;
        if (!hand && std::isfinite(lr) && std::isfinite(g_.prevLast) && lr != g_.prevLast) hand = 4;       // only the time of the last release moved
    }
    g_.prevL = tl; g_.prevR = tr; g_.prevLast = lr; g_.haveTimers = true;
    // a second, independent way to notice a release: the ball you control starts counting "seconds since it was let go" (_unheldTime)
    if (L_.bUnheld >= 0) {
        const uintptr_t wb = plausiblePtr(ballPtr) ? ballPtr : (now - g_.lastBallT < 1.0 ? g_.lastBall : 0);
        if (wb && readObj(ballBuf_.data(), wb, static_cast<size_t>(L_.ball.size)) && liveObj(ballBuf_.data(), L_.ball.klassInv, L_.ball.unityObject)) {
            const float u = rdF(ballBuf_.data(), L_.bUnheld);
            if (wb == g_.prevUnheldBall && std::isfinite(u) && std::isfinite(g_.prevUnheld) && g_.prevUnheld <= 0.005f && u > 0.005f && u < 0.5f) hand |= 8;
            g_.prevUnheld = u; g_.prevUnheldBall = wb;
        } else g_.prevUnheldBall = 0;
    }
    const int aimMode = mode_.load(std::memory_order_relaxed);           // stage D10: the link also runs for "Shot points" alone; then it must not touch your throws
    if (hand && aimMode != 0 && now - g_.lastEdgeT > 0.15) { g_.lastEdgeT = now; startShot(now, hand, ballPtr); }
    if (aimMode == 0 && g_.phase != IDLE) abandon("the Aimbot was turned off");

    if (g_.phase == WAIT) tickWait(now);
    else if (g_.phase == WATCH) tickWatch(now);
    else if (aimMode == 2 && bankReady_.load(std::memory_order_acquire)) bankPrepare(now);       // idle: measure the backboards ahead of the first shot

    // stage D10 ("Shot points"): keep your ball at the asked number of points. (Reads and writes the ball's own memory only; no engine call.)
    if (points_.load(std::memory_order_relaxed) != 0) {
        float since = -1.0f;                                              // seconds since you let go of a ball (the smaller of the two release timers), or -1
        if (std::isfinite(tl) && tl >= 0) since = tl;
        if (std::isfinite(tr) && tr >= 0 && (since < 0 || tr < since)) since = tr;
        pointsTick(now, ballPtr, since);
    } else if (!g_.ptsBalls.empty()) {
        pointsStandDown();
    }

    // stage D11 ("Hitbox expander" + "See hitbox"): your hands' hitboxes. (Calls a few engine functions, at most ten times a second, only when something changed.)
    hitboxTick(now);
}

void AimLink::startShot(double now, int hand, uintptr_t ballPtr) {
    if (g_.phase != IDLE) finishShot("a new release started", now);
    g_.shot = Shot();
    Shot& s = g_.shot;
    s.id = shotSeq_.fetch_add(1) + 1; s.hand = hand; s.tEdge = now; s.mode = mode_.load() == 2 ? 2 : 1;
    s.ball = plausiblePtr(ballPtr) ? ballPtr : (now - g_.lastBallT < 2.0 ? g_.lastBall : 0);
    releases_.fetch_add(1);
    readY(s, now);                                                  // the Y button at THIS moment (the moment of the release)
    readJump(s);                                                    // the player's jump state at this moment (for the report only)
    g_.phase = WAIT;
}

// The Y button at the release (the latest reading from the menu's thread or the doorway). Reads our own memory only.
void AimLink::readY(Shot& s, double now) {
    s.holdY = holdY_.load();
    s.yKnown = yReadable(now);
    s.yHeldAtRelease = false; s.yAgo = -1; s.yRecent = false; s.yAtDecision = false;
    if (!s.yKnown) return;
    s.yHeldAtRelease = yHeldNow(now);
    const double heldAt = yHeldAt_.load();
    if (!s.yHeldAtRelease && heldAt > -1e8) { s.yAgo = std::max(0.0, now - heldAt); s.yRecent = s.yAgo <= cfg_.yGraceSeconds; }
}

std::string AimLink::yText(const Shot& s, bool readable) const {
    if (!readable) return "Y button: NOT read (no reading in the last second)";
    if (s.yHeldAtRelease) return "Y button: HELD when you let go";
    if (s.yRecent) return fmt("Y button: let go %.0f ms before you released the ball (counts as held)", s.yAgo * 1000.0);
    if (s.yAtDecision) return "Y button: pressed right after you let go (counts as held)";
    if (s.yAgo >= 0) return fmt("Y button: NOT held (last seen held %.1f s before the release)", s.yAgo);
    return "Y button: NOT held (never seen held in this session)";
}

// The player's vertical state at the release: 0 = on the floor, any other number = in the air. Reads memory only (no game call). Report only.
void AimLink::readJump(Shot& s) {
    s.vKnown = false; s.vState = -1; s.jumpPressed = 0; s.locoState = -1;
    if (!L_.jumpOk) { s.vWhy = L_.jumpWhy.empty() ? "this game has no jump state I can read" : L_.jumpWhy; return; }
    const uintptr_t lp = rdP(bcmBuf_.data(), L_.cLoco);
    if (!plausiblePtr(lp)) { s.vWhy = "your ball control has no player linked"; return; }
    unsigned char* b = locoBuf_.data();
    if (!readObj(b, lp, static_cast<size_t>(L_.lRead)) || !liveObj(b, L_.loco.klassInv, L_.loco.unityObject)) { s.vWhy = "the player object could not be read (gone?)"; return; }
    const int v = rdI(b, L_.lVert);
    s.vState = v;
    if (L_.lJumpL >= 0 && b[L_.lJumpL]) s.jumpPressed |= 1;
    if (L_.lJumpR >= 0 && b[L_.lJumpR]) s.jumpPressed |= 2;
    if (L_.lState >= 0) s.locoState = rdI(b, L_.lState);
    if (v < 0 || v > 3) { s.vWhy = fmt("the vertical state number %d is outside 0..3 (the game has four values)", v); return; }       // not believable: do not guess
    s.vKnown = true;
}

std::string AimLink::jumpText(const Shot& s) const {
    if (!s.vKnown) return fmt("jump: NOT read (%s)", s.vWhy.c_str());
    return fmt("jump: vertical state %d = %s (jump button left %s, right %s; locomotion state %d)", s.vState, s.vState == 0 ? "ON THE FLOOR" : "IN THE AIR",
               (s.jumpPressed & 1) ? "pressed" : "no", (s.jumpPressed & 2) ? "pressed" : "no", s.locoState);
}

void AimLink::abandon(const char* why) {
    if (g_.phase == IDLE) return;
    if (g_.phase == WATCH) queue(fmt("aim: shot #%d: stopped watching: %s", g_.shot.id, why));
    g_.phase = IDLE;
}

void AimLink::tickWait(double now) {
    Shot& s = g_.shot;
    const double dt = now - s.tEdge;
    if (dt < cfg_.releaseDelaySeconds) return;
    if (dt > cfg_.releaseWindowSeconds) { if (s.why.empty()) s.why = "no shot-like speed within the time window"; finishShot("window over", now); return; }
    if (!s.ball) { s.why = "your ball control had no ball linked"; finishShot("no ball", now); return; }
    if (!readObj(ballBuf_.data(), s.ball, static_cast<size_t>(L_.ball.size)) || !liveObj(ballBuf_.data(), L_.ball.klassInv, L_.ball.unityObject)) {
        s.why = "the ball object was gone"; finishShot("ball gone", now); return;
    }
    const unsigned char* bb = ballBuf_.data();
    const uintptr_t rb = rdP(bb, L_.bRigid);
    if (!rigidOk(rb)) { s.why = "the ball has no usable physics body"; finishShot("no rigidbody", now); return; }
    s.rb = rb;

    Vec3 pos, vel;
    if (!callVec3(L_.mGetPos, rb, &pos) || !callVec3(L_.mGetVel, rb, &vel)) {
        if (engineDead_.load()) { s.why = "the engine calls fail"; finishShot("engine", now); }
        return;                                                   // else try again on the next tick
    }
    if (lenV(pos) > 5000.0f || lenV(vel) > 300.0f) { killEngine("the engine gave a ball position / speed that is not believable: " + fv(pos) + " " + fv(vel)); s.why = "unbelievable engine numbers"; finishShot("engine", now); return; }
    if (!g_.engineChecked) {
        g_.engineChecked = true;
        const Vec3 mp = L_.bCurPos >= 0 ? rdV(bb, L_.bCurPos) : Vec3();
        queue(fmt("aim: engine check (first release): body position %s, body speed %s, the ball's own _currentBallPosition %s -> %s",
                  fv(pos).c_str(), fv(vel).c_str(), fv(mp).c_str(), L_.bCurPos >= 0 ? (distV(pos, mp) < 3.0f ? "they agree (within 3 m)" : "they DO NOT agree") : "no managed position to compare"));
    }
    s.pos0 = pos; s.vel0 = vel;
    s.unheld = L_.bUnheld >= 0 ? rdF(bb, L_.bUnheld) : -1.0f;

    // the hoops this ball knows (rings only: believable heights; unset extra hoops are skipped)
    s.nHoops = 0;
    for (int i = 0; i < 6; ++i) {
        if (L_.bHoop[i] < 0) continue;
        const Vec3 h = rdV(bb, L_.bHoop[i]);
        if (!finiteV(h) || h.y < 0.5f || h.y > 8.0f || (std::fabs(h.x) + std::fabs(h.z) < 0.01f)) continue;
        s.hoops[s.nHoops] = h; s.hoopSlot[s.nHoops] = i; ++s.nHoops;
    }
    const float cap = capM_.load();
    s.capM = cap;
    // the Y button again, now that the decision is made: pressing it right after you let go also counts
    const bool yFreshNow = yReadable(now);
    s.yAtDecision = yFreshNow && yHeldNow(now);
    const bool yCanRead = s.yKnown || yFreshNow;
    const bool yPass = s.yHeldAtRelease || s.yRecent || s.yAtDecision;
    tzaim::Shot shot; shot.pos = pos; shot.vel = vel;
    const tzaim::Decision d = tzaim::decide(true, cap, shot, s.hoops, s.nHoops, tzaim::Rules());
    s.dec = d;
    const float speed = lenV(vel), elev = d.elevationDeg;
    if (d.verdict == tzaim::Verdict::NotAShot) { s.why = fmt("not a shot (speed %.1f m/s, %.0f degrees upward)", static_cast<double>(speed), static_cast<double>(elev)); return; }       // keep waiting: the throw may not be applied yet
    if (s.unheld >= 0.0f && s.unheld <= 0.005f) { s.why = fmt("the ball still looks held (_unheldTime %.3f)", static_cast<double>(s.unheld)); return; }
    shotLike_.fetch_add(1);
    s.decided = true;
    if (d.verdict == tzaim::Verdict::NoHoop) { s.why = "this ball knows no hoop position"; noHoop_.fetch_add(1); queue(fmt("aim: SHOT #%d: %s", s.id, s.why.c_str())); setLast(fmt("last shot #%d: no hoop known", s.id)); g_.phase = IDLE; return; }

    // the game's physics numbers
    float gmag = 9.81f, drag = 0.11f, fdt = 0.02f; bool useGrav = true; std::string physNote;
    Vec3 gv;
    if (L_.mGetGravity && callVec3(L_.mGetGravity, 0, &gv) && std::fabs(gv.y) > 0.5f && std::fabs(gv.y) < 100.0f) gmag = std::fabs(gv.y); else physNote += " [gravity not read: 9.81 assumed]";
    float dr = 0;
    if (L_.mGetDrag && callFloat(L_.mGetDrag, rb, &dr) && dr >= 0.0f && dr < 50.0f) drag = dr;
    else if (L_.bOrigDrag >= 0 && std::isfinite(rdF(bb, L_.bOrigDrag)) && rdF(bb, L_.bOrigDrag) >= 0.0f) { drag = rdF(bb, L_.bOrigDrag); physNote += " [drag taken from _originalDrag]"; }
    else physNote += " [drag not read: 0.11 assumed]";
    float fd = 0;
    if (L_.mGetFixedDt && callFloat(L_.mGetFixedDt, 0, &fd) && fd > 0.002f && fd < 0.1f) fdt = fd; else physNote += " [physics step not read: 0.02 assumed]";
    bool ug = true;
    if (L_.mGetUseGravity && callBool(L_.mGetUseGravity, rb, &ug)) useGrav = ug;
    s.model.gravity = gmag; s.model.drag = drag; s.model.dt = fdt;

    std::string head = fmt("aim: SHOT #%d (noticed by: %s) %.0f ms after the release | ball %s moving %.1f m/s, %.0f deg upward, _unheldTime %.2f | physics: gravity %.2f, drag %.3f, step %.4f s, uses gravity %s%s | %s | %s | %s | hold Y to aim: %s | hoops known: %d | max shot distance %s",
                          s.id, handText(s.hand).c_str(), dt * 1000.0, fv(pos).c_str(), static_cast<double>(speed),
                          static_cast<double>(elev), static_cast<double>(s.unheld), static_cast<double>(gmag), static_cast<double>(drag), static_cast<double>(fdt), useGrav ? "yes" : "NO", physNote.c_str(),
                          gameFlags().c_str(), yText(s, yCanRead).c_str(), jumpText(s).c_str(), s.holdY ? "ON" : "off", s.nHoops, tzaim::capLabel(cap).c_str());
    if (s.mode == 2) head += " | mode: BANK (hit the backboard first)";
    s.hoopPos = s.hoops[d.hoop];
    const std::string hoopTxt = fmt("%s at %s, %.1f m away along the floor, %.0f degrees off your throw direction", kHoopNames[s.hoopSlot[d.hoop]], fv(s.hoopPos).c_str(), static_cast<double>(d.distanceM), static_cast<double>(d.angleDeg));
    s.predStart = vel;

    if (d.verdict == tzaim::Verdict::TooFar) {
        tooFar_.fetch_add(1); s.why = fmt("hoop is %.1f m away, farther than your %s limit", static_cast<double>(d.distanceM), tzaim::capLabel(cap).c_str());
        queue(head + " | decision: LEFT ALONE (the aimbot does not activate) - " + s.why + " (" + hoopTxt + ")");
        setLast(fmt("shot #%d: not aimed - %.0f m is farther than your %s limit", s.id, static_cast<double>(d.distanceM), tzaim::capLabel(cap).c_str()));
    } else if (d.verdict == tzaim::Verdict::WrongDirection) {
        wrongDir_.fetch_add(1); s.why = "no hoop within 40 degrees of your throw direction";
        queue(head + " | decision: LEFT ALONE - " + s.why + " (closest: " + hoopTxt + ")");
        setLast(fmt("shot #%d: not aimed - no hoop in your throw direction", s.id));
    } else if (s.holdY && !yCanRead) {
        yUnknown_.fetch_add(1); s.why = "'hold Y to aim' is ON but the Y button could not be read";
        queue(head + " | decision: LEFT ALONE - " + s.why);
        setLast(fmt("shot #%d: not aimed - cannot read the Y button", s.id));
    } else if (s.holdY && !yPass) {
        notHoldingY_.fetch_add(1); s.why = "the Y button was not held when you let go and 'hold Y to aim' is ON";
        queue(head + " | decision: LEFT ALONE - " + s.why + " (" + hoopTxt + ")");
        setLast(fmt("shot #%d: not aimed - hold Y to aim", s.id));
    } else if (!useGrav) {
        refused_.fetch_add(1); s.why = "the ball does not use the engine's gravity, so the flight cannot be worked out";
        queue(head + " | decision: LEFT ALONE - " + s.why);
        setLast(fmt("shot #%d: not aimed - unusual ball physics", s.id));
    } else if (s.mode == 2) {
        bankShot(s, head, hoopTxt, bb, pos, vel, elev);                    // stage D9: Bank mode (never falls back to the direct solver)
    } else {
        tzaim::SolveParams sp; sp.minEntryDeg = 45.0f; sp.preferredLaunchDeg = elev; sp.minLaunchDeg = 35.0f; sp.maxLaunchDeg = 80.0f; sp.maxSpeed = 60.0f;
        s.sol = tzaim::solveFlight(pos, s.hoopPos, sp, s.model);
        s.solved = true;
        if (!s.sol.ok) {
            noSolution_.fetch_add(1); s.why = std::string("no launch speed found: ") + s.sol.why;
            queue(head + " | decision: LEFT ALONE - " + s.why + " (" + hoopTxt + ")");
            setLast(fmt("shot #%d: not aimed - %s", s.id, s.sol.why));
        } else if (!cfg_.allowWrite) {
            s.why = "writing is switched off (test mode)";
            queue(head + " | decision: WOULD AIM (test mode, nothing written) at " + hoopTxt + " | speed " + fmt("%.2f, launch %.1f deg, entry %.1f deg, flight %.2f s", static_cast<double>(s.sol.speed), static_cast<double>(s.sol.launchDeg), static_cast<double>(s.sol.entryDeg), static_cast<double>(s.sol.flightSeconds)));
        } else {
            // THE CHANGE: the new launch speed goes onto the ball's physics body.
            const bool ok = callSetVec3(L_.mSetVel, rb, s.sol.vel);
            Vec3 rbk; s.haveReadback = ok && callVec3(L_.mGetVel, rb, &rbk); if (s.haveReadback) s.readback = rbk;
            if (!ok) {
                refused_.fetch_add(1); s.why = "setting the ball's speed raised an error in the game";
                queue(head + " | decision: LEFT ALONE - " + s.why);
                setLast(fmt("shot #%d: not aimed - the game refused the new speed", s.id));
            } else {
                s.applied = true; s.predStart = s.sol.vel; aimed_.fetch_add(1);
                const bool kept = s.haveReadback && distV(s.readback, s.sol.vel) < 0.05f;
                queue(head + " | decision: AIM at " + hoopTxt + " | worked out: speed " + fmt("%.2f m/s, launch %.1f deg, entry angle %.1f deg, flight %.2f s", static_cast<double>(s.sol.speed), static_cast<double>(s.sol.launchDeg), static_cast<double>(s.sol.entryDeg), static_cast<double>(s.sol.flightSeconds)) +
                      " | was " + fv(vel) + ", set " + fv(s.sol.vel) + ", read back " + (s.haveReadback ? fv(s.readback) : std::string("(failed)")) + (kept ? " = kept" : " = NOT the value we set"));
                setLast(fmt("shot #%d: AIMED at the %s from %.1f m", s.id, kHoopNames[s.hoopSlot[d.hoop]], static_cast<double>(d.distanceM)));
            }
        }
    }
    // from here on: watch the flight (also when the throw was left alone: that shows how this game's own ball flies)
    s.tApply = now; s.havePrev = true; s.prevPos = s.pos0; s.steps = 0;
    g_.phase = WATCH;
}

void AimLink::tickWatch(double now) {
    Shot& s = g_.shot;
    const double dt = now - s.tApply;
    if (!rigidOk(s.rb)) { finishShot("the ball's physics body is gone", now); return; }
    Vec3 pos;
    if (!callVec3(L_.mGetPos, s.rb, &pos)) { if (engineDead_.load()) finishShot("engine calls fail", now); return; }
    const bool moved = std::memcmp(&pos, &s.prevPos, sizeof pos) != 0;                // the physics body only moves once per physics step
    if (moved) {
        ++s.steps;
        if (s.mode == 2) bankWatchStep(s, s.steps, pos);                       // stage D9: closest approach to the board, speeds around the touch
        if (!s.crossed && s.prevPos.y >= s.hoopPos.y && pos.y < s.hoopPos.y) {          // came down through the ring's height
            const float k = (s.prevPos.y - s.hoopPos.y) / (s.prevPos.y - pos.y);
            s.crossPos = Vec3{s.prevPos.x + (pos.x - s.prevPos.x) * k, s.hoopPos.y, s.prevPos.z + (pos.z - s.prevPos.z) * k};
            s.crossMiss = std::sqrt((s.crossPos.x - s.hoopPos.x) * (s.crossPos.x - s.hoopPos.x) + (s.crossPos.z - s.hoopPos.z) * (s.crossPos.z - s.hoopPos.z));
            s.crossT = static_cast<float>(dt); s.crossed = true;
        }
        const float dd = distV(pos, s.hoopPos);
        if (dd < s.closest) { s.closest = dd; s.closestStep = s.steps; }
        const int nSamples = s.sampleAt.empty() ? kNumSamples : static_cast<int>(s.sampleAt.size());
        auto sampleStep = [&](int i) { return s.sampleAt.empty() ? kSampleSteps[i] : s.sampleAt[static_cast<size_t>(i)]; };
        if (s.nextSample < nSamples && s.steps >= sampleStep(s.nextSample)) {
            Sample sm; sm.steps = s.steps; sm.pos = pos;
            Vec3 v;
            if (callVec3(L_.mGetVel, s.rb, &v)) sm.vel = v;
            sm.pred = tzaim::stepsAfter(s.pos0, s.predStart, s.steps, s.model);
            if (s.mode == 2 && s.applied && s.bankOk && s.steps >= s.plan.sim.contactStep && static_cast<size_t>(s.steps) < s.plan.sim.path.size() && s.steps >= 1) {
                // after the touch with the board the prediction is the bank maths' own flight (position from its path, speed from two steps of it)
                const Vec3 a = s.plan.sim.path[static_cast<size_t>(s.steps) - 1], b = s.plan.sim.path[static_cast<size_t>(s.steps)];
                sm.pred.pos = b; sm.pred.vel = Vec3{(b.x - a.x) / s.model.dt, (b.y - a.y) / s.model.dt, (b.z - a.z) / s.model.dt};
            }
            if (s.applied && s.samples.empty()) {
                const float diff = distV(sm.vel, sm.pred.vel);
                s.overrideDiff = diff;
                if (diff > std::max(0.6f, 0.08f * lenV(s.sol.vel))) { s.overridden = true; overridden_.fetch_add(1); }
            }
            s.samples.push_back(sm); ++s.nextSample;
        }
    }
    s.prevPos = pos; s.havePrev = true;
    if (!std::isfinite(pos.y) || pos.y < -20.0f) { finishShot("the ball fell out of the world", now); return; }
    if (s.crossed && dt > s.crossT + 0.45) { finishShot("flight over", now); return; }
    if (dt > cfg_.watchSeconds) { finishShot("watch time over", now); return; }
}

void AimLink::finishShot(const char* why, double now) {
    (void)now;
    Shot& s = g_.shot;
    if (g_.phase == WAIT && !s.decided) {
        // a release that never looked like a shot: counted, and the first few are written down (dribbles would otherwise fill the file)
        const bool notAShot = s.why.rfind("not a shot", 0) == 0 || s.why.rfind("no shot-like", 0) == 0;
        if (notAShot) notShot_.fetch_add(1); else refused_.fetch_add(1);
        if (++g_.releaseNotes <= 10)
            queue(fmt("aim: release #%d (noticed by: %s) %.0f ms: nothing to aim - %s | last seen: ball speed %s", s.id, handText(s.hand).c_str(), (now - s.tEdge) * 1000.0, s.why.c_str(), fv(s.vel0).c_str()));
        setLast(fmt("release #%d: nothing to aim (%s)", s.id, s.why.c_str()));
        g_.phase = IDLE;
        return;
    }
    if (g_.phase == WATCH) {
        // what the game itself says about the shot
        s.madeFlag = -1;
        if (L_.bShotMade >= 0 && readObj(ballBuf_.data(), s.ball, static_cast<size_t>(L_.ball.size)) && liveObj(ballBuf_.data(), L_.ball.klassInv, L_.ball.unityObject)) s.madeFlag = ballBuf_[static_cast<size_t>(L_.bShotMade)] ? 1 : 0;
        if (s.madeFlag == 1) scored_.fetch_add(1); else if (s.madeFlag == 0 && s.crossed) missed_.fetch_add(1);
        if (s.mode == 2 && s.applied) {
            if (s.madeFlag == 1) bankScored_.fetch_add(1); else if (s.madeFlag == 0 && s.crossed) bankMissed_.fetch_add(1);
            if (B_.bAssistTime >= 0) s.assistTime1 = rdF(ballBuf_.data(), B_.bAssistTime);
            if (B_.bAssistGoal >= 0) s.assistGoal1 = rdP(ballBuf_.data(), B_.bAssistGoal);
        }
        std::string r = fmt("aim: SHOT #%d result (%s): watched %.1f s = %d physics steps | ", s.id, why, now - s.tApply, s.steps);
        if (s.crossed) r += fmt("came down through the ring height after %.2f s at %s = %.3f m from the hoop centre (ring radius %.4f m, ball radius about 0.12 m) | ", static_cast<double>(s.crossT), fv(s.crossPos).c_str(), static_cast<double>(s.crossMiss), 0.2286);
        else r += "never came down through the ring height | ";
        r += fmt("closest to the hoop centre: %.3f m (step %d) | the game's _shotMade: %s", static_cast<double>(s.closest), s.closestStep, s.madeFlag == 1 ? "YES" : (s.madeFlag == 0 ? "no" : "unknown"));
        if (s.applied) r += s.overridden ? fmt(" | THE GAME CHANGED OUR SPEED (difference %.2f m/s at step %d)", static_cast<double>(s.overrideDiff), s.samples.empty() ? 0 : s.samples[0].steps) : " | our speed was kept by the game";
        queue(r);
        if (!s.samples.empty()) {
            std::string sl = fmt("aim: SHOT #%d flight samples (real vs %s; error = distance between the real ball and the prediction):", s.id, s.applied ? "what the maths predicted for our speed" : "the same maths started from the player's own throw");
            for (const Sample& sm : s.samples)
                sl += fmt(" [step %d: pos %s err %.2f m, vel %s err %.2f m/s]", sm.steps, fv(sm.pos).c_str(), static_cast<double>(distV(sm.pos, sm.pred.pos)), fv(sm.vel).c_str(), static_cast<double>(distV(sm.vel, sm.pred.vel)));
            queue(sl);
        }
        if (s.mode == 2) { const std::string bt = bankResultText(s); if (!bt.empty()) queue(bt); }
        const char* ver = s.madeFlag == 1 ? "SCORED" : (s.madeFlag == 0 ? "missed" : "result unknown");
        if (s.applied) setLast(fmt("shot #%d: %s from %.0f m - %s, %.2f m off centre", s.id, s.mode == 2 ? "BANK" : "AIMED", static_cast<double>(s.dec.distanceM), ver, static_cast<double>(s.crossed ? s.crossMiss : s.closest)));
    }
    g_.phase = IDLE;
}

}  // namespace tzaimlink
