// aim_points.cpp - stage D10: the "Shot points" part of the game link (Troll page).
//
// WHAT THIS DOES (plain words)
//   Every ball in the game (ShovelTools.Basketball) has a settings object (ShovelTools.BasketballProperties) and that object keeps a whole number called
//   _pointValue: how many points a basket with this ball is worth. The game sets it with SetPointValue(int) when a throw is released
//   (2 or 3 outside a game, 1 or 2 in a game - this is what the game's own shot record, ShotData.PointValue, is made from).
//   When the "Shot points" switch is ON, this part keeps that number at the slider's value for the ball YOU control (and for the balls you let go of in the
//   last few seconds), several times per second, on the game's own thread. If the game writes its own number again (it does that when you throw), the next
//   tick puts ours back. When the switch goes OFF, the game's own number is put back where we changed it.
//
// WHAT IS PROVEN AND WHAT IS NOT
//   Proven by the game's own files (the lobby facts files): the two class names, the link _properties in the ball, the field _pointValue (a whole number) in the
//   settings object, and SetPointValue(int) which is a plain store into that field.
//   NOT proven (never run in the real game): that the game's SCORING reads _pointValue at the moment the ball goes in. The game may also copy the number at
//   the moment of the release (then changing it later does nothing) or count points somewhere else. That is why this part writes a full report into the facts
//   file (what number the ball had, when the game wrote its own, what the ball said at each basket) - it is the way the next version gets fixed.
//
// WHAT IT NEVER DOES
//   It never touches a ball that is not the one your ball control object points at (so never another player's ball), never calls into the game's engine
//   (only reads and writes the ball's own memory, through the same checked "safe copy" the movement part uses) and never writes anything else.
#include "aim_link.h"

#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tzaimlink {

namespace {
int rdI(const unsigned char* b, int off) { int v; std::memcpy(&v, b + off, 4); return v; }
uintptr_t rdP(const unsigned char* b, int off) { uint64_t p; std::memcpy(&p, b + off, 8); return static_cast<uintptr_t>(p); }
bool plausiblePtr(uintptr_t p) { return p > 0x10000 && (p & 7) == 0 && p < 0x0000800000000000ULL; }
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) { char b[1200]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }

const size_t kMaxBalls = 4;             // balls of yours kept at once (the one in your hands, and the last few you let go of)
const double kKeepSeconds = 12.0;       // a ball you no longer control is kept this long after you last controlled it (a flight is a few seconds)
const double kRetryReadSeconds = 0.5;   // a ball that cannot be read right now is looked at again after this long
const int kNoteLimit = 40;              // tick-level lines in the facts file (first looks, the game's own writes, baskets)
const int kMaxLines = 70;               // all points lines together
}  // namespace

// ---------------------------------------------------------------- facts file
void AimLink::sayPoints(const char* f, ...) {
    if (!cfg_.note) return;
    char b[1500];
    va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap);
    const int n = ptsNotes_.fetch_add(1);
    if (n >= kMaxLines) return;
    cfg_.note("%s", b);
    if (n + 1 == kMaxLines) cfg_.note("points: (that is enough points lines - the rest are not written, so the facts file stays small)");
}

void AimLink::queuePoints(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMu_);
    if (out_.size() < 64) out_.push_back(line);
}

// ---------------------------------------------------------------- the menu's request
void AimLink::setPoints(int points) {
    if (points < 0) points = 0;
    if (points > 999) points = 999;
    const int was = points_.exchange(points);
    const bool linkOn = linkWanted();                                // the link as a whole runs for the Aimbot, the points OR the hitbox part
    const bool linkWas = on_.exchange(linkOn);
    if (linkOn && !linkWas) initOn();
    if (points != 0 && was == 0) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            nextPointsAt_ = 0; pointsTries_ = 0; pointsDead_ = false; pointsFailWhy_.clear(); ptsLast_.clear();
        }
        ptsWriteDead_.store(false);
        sayPoints("points: switch turned ON: every basket of yours is set to %d points", points);
    } else if (points == 0 && was != 0) {
        sayPoints("points: switch turned OFF (the game's own numbers are put back where I changed them)");
    }
}

// ---------------------------------------------------------------- state for the menu and the facts file
int AimLink::pointsUiState() const {
    if (points_.load() == 0) return 0;
    if (!failReason().empty()) return 3;
    { std::lock_guard<std::mutex> lock(mu_); if (pointsDead_) return 3; }
    if (ptsWriteDead_.load()) return 3;
    if (!layoutReady_.load() || !pointsReady_.load() || !bound_.load()) return 2;
    return 1;
}

std::string AimLink::pointsHeadline() const {
    const int p = points_.load();
    if (p == 0) return "";
    const std::string why = failReason();
    if (!why.empty()) return "FAILED: " + why;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (pointsDead_) return "FAILED: " + pointsFailWhy_;
    }
    if (ptsWriteDead_.load()) return "FAILED: the game's memory refused my write, so nothing was changed";
    if (!layoutReady_.load()) return "looking at the game's code ...";
    if (!pointsReady_.load()) {
        std::lock_guard<std::mutex> lock(mu_);
        return pointsFailWhy_.empty() ? std::string("looking at the ball's point value ...") : "waiting: " + pointsFailWhy_;
    }
    if (!bound_.load()) return "looking for your ball control ...";
    return fmt("connected - your ball is kept at %d point%s", p, p == 1 ? "" : "s");
}

std::string AimLink::pointsLastText() const { std::lock_guard<std::mutex> lock(mu_); return ptsLast_; }

PointsCounters AimLink::pointsCounters() const {
    PointsCounters c;
    c.writes = ptsWrites_.load(); c.gameWrites = ptsGameWrites_.load(); c.baskets = ptsBaskets_.load(); c.balls = ptsBalls_.load();
    c.readFails = ptsReadFails_.load(); c.writeFails = ptsWriteFails_.load();
    return c;
}

std::string AimLink::pointsSummary() const {
    const PointsCounters c = pointsCounters();
    const int ui = pointsUiState();
    const char* state = ui == 1 ? "connected" : (ui == 2 ? "looking" : (ui == 3 ? "FAILED" : "off"));
    const std::string why = ui == 3 ? pointsHeadline() : std::string();
    return fmt("points: link %s%s%s | asked %d | balls handled %llu, number written %llu times, the game put its own number back %llu times, baskets seen %llu, read problems %llu, write problems %llu",
               state, why.empty() ? "" : ": ", why.c_str(), points_.load(), c.balls, c.writes, c.gameWrites, c.baskets, c.readFails, c.writeFails);
}

// ---------------------------------------------------------------- link thread: where the ball keeps its point value
bool AimLink::resolvePointsLayout(std::string* why, bool* transient) {
    *transient = false;
    PointsLayout P;
    const tzscan::FieldInfo* fp = L_.ball.field("_properties");
    if (!fp) { *why = "the ball (" + L_.ball.fullName + ") has no _properties link (the game was updated?)"; return false; }
    if (fp->typeName != "ShovelTools.BasketballProperties") { *why = "the ball's _properties has type " + fp->typeName + ", expected ShovelTools.BasketballProperties"; return false; }
    if (fp->offset < 16 || fp->offset + 8 > L_.ball.size) { *why = "the ball's _properties link lies outside the ball object"; return false; }
    P.bProps = fp->offset;
    P.props = tzscan::findClass(L_.api, "ShovelTools", "BasketballProperties");
    if (!P.props.found) { *why = P.props.error; *transient = P.props.error.find("not ready") != std::string::npos; return false; }
    const tzscan::FieldInfo* fv = P.props.field("_pointValue");
    if (!fv) { *why = "the field _pointValue is missing in " + P.props.fullName + " (the game was updated?)"; return false; }
    if (fv->typeName != "System.Int32") { *why = "the field _pointValue in " + P.props.fullName + " has type " + fv->typeName + ", expected System.Int32"; return false; }
    if (fv->offset < 16 || fv->offset + 4 > P.props.size) { *why = "the field _pointValue lies outside " + P.props.fullName; return false; }
    P.pValue = fv->offset;
    P.ok = true;
    {
        std::lock_guard<std::mutex> lock(mu_);
        P_ = P;
        ptsBallBuf_.assign(static_cast<size_t>(L_.ball.size), 0);
        ptsPropsBuf_.assign(static_cast<size_t>(P.props.size), 0);
    }
    sayPoints("points: found where the ball keeps its point value: %s (size %d) _properties@%d -> %s (size %d) _pointValue@%d (a whole number). Reads and writes go straight to the ball's memory; no engine call.",
              L_.ball.fullName.c_str(), L_.ball.size, P.bProps, P.props.fullName.c_str(), P.props.size, P.pValue);
    return true;
}

// ---------------------------------------------------------------- game thread
bool AimLink::pointsReadBall(PtsBall& b, int* value, bool* shotMade) {
    unsigned char* bb = ptsBallBuf_.data();
    if (!readObj(bb, b.ball, ptsBallBuf_.size()) || !liveObj(bb, L_.ball.klassInv, L_.ball.unityObject)) return false;
    const uintptr_t props = rdP(bb, P_.bProps);
    if (!plausiblePtr(props)) return false;
    unsigned char* pb = ptsPropsBuf_.data();
    if (!readObj(pb, props, ptsPropsBuf_.size()) || !liveObj(pb, P_.props.klassInv, P_.props.unityObject)) return false;
    b.props = props;
    *value = rdI(pb, P_.pValue);
    *shotMade = L_.bShotMade >= 0 && bb[L_.bShotMade] != 0;
    return true;
}

bool AimLink::pointsWrite(const PtsBall& b, int value) {
    if (!plausiblePtr(b.props)) return false;
    unsigned char four[4];
    std::memcpy(four, &value, 4);
    return pipeGame_.put(b.props + static_cast<uintptr_t>(P_.pValue), four, 4);
}

void AimLink::pointsTick(double now, uintptr_t ballPtr, float sinceRelease) {
    if (!pointsReady_.load(std::memory_order_acquire)) return;
    const int want = points_.load(std::memory_order_relaxed);
    if (want <= 0 || ptsWriteDead_.load(std::memory_order_relaxed)) return;
    g_.ptsAsked = want; g_.ptsWasOn = true;

    // 1. the ball your ball control points at right now joins the list (or is marked as "controlled just now")
    if (plausiblePtr(ballPtr)) {
        bool have = false;
        for (PtsBall& b : g_.ptsBalls) if (b.ball == ballPtr) { b.lastControlled = now; have = true; break; }
        if (!have) {
            if (g_.ptsBalls.size() >= kMaxBalls) {                  // make room: drop the ball that was controlled longest ago
                size_t oldest = 0;
                for (size_t i = 1; i < g_.ptsBalls.size(); ++i) if (g_.ptsBalls[i].lastControlled < g_.ptsBalls[oldest].lastControlled) oldest = i;
                g_.ptsBalls.erase(g_.ptsBalls.begin() + static_cast<long>(oldest));
            }
            PtsBall nb; nb.ball = ballPtr; nb.firstSeen = nb.lastControlled = now;
            g_.ptsBalls.push_back(nb);
            ptsBalls_.fetch_add(1);
        }
    }

    // 2. keep every ball of the list at the asked number
    for (size_t i = 0; i < g_.ptsBalls.size();) {
        PtsBall& b = g_.ptsBalls[i];
        if (now - b.lastControlled > kKeepSeconds) { g_.ptsBalls.erase(g_.ptsBalls.begin() + static_cast<long>(i)); continue; }
        if (now < b.retryAt) { ++i; continue; }
        int cur = 0; bool made = false;
        if (!pointsReadBall(b, &cur, &made)) {
            ptsReadFails_.fetch_add(1);
            b.retryAt = now + kRetryReadSeconds;
            if (g_.ptsNotes < kNoteLimit && ++b.readFailNotes <= 1) { ++g_.ptsNotes; queuePoints(fmt("points: a ball of yours (%llx) cannot be read right now (destroyed, or not a ball any more) - looking again in %.1f s", static_cast<unsigned long long>(b.ball), kRetryReadSeconds)); }
            ++i;
            continue;
        }
        if (!b.seenValue) {
            b.seenValue = true; b.firstValue = cur;
            if (g_.ptsNotes < kNoteLimit) { ++g_.ptsNotes; queuePoints(fmt("points: first look at a ball of yours (%llx): its point value is %d (I will keep it at %d)", static_cast<unsigned long long>(b.ball), cur, want)); }
        }
        if (cur != want) {
            // the number is not ours: either nobody set ours yet, or the game wrote its own again (it does that when you throw)
            if (b.writes > 0) {
                ++b.gameWrites; b.lastGame = cur; ptsGameWrites_.fetch_add(1);
                if (g_.ptsNotes < kNoteLimit) {
                    ++g_.ptsNotes;
                    queuePoints(fmt("points: the game put its own number %d into the ball's point value%s%s (it was %d, ours) - I put %d back",
                                    cur, sinceRelease >= 0 ? " " : "", sinceRelease >= 0 ? fmt("%.2f s after you let go", static_cast<double>(sinceRelease)).c_str() : "", b.lastWritten, want));
                }
            }
            b.haveGame = true; b.gameValue = cur;
            if (pointsWrite(b, want)) {
                g_.ptsWriteFailRun = 0; ++b.writes; b.lastWritten = want; ptsWrites_.fetch_add(1);
            } else {
                ptsWriteFails_.fetch_add(1);
                if (++g_.ptsWriteFailRun >= 5) {
                    ptsWriteDead_.store(true);
                    queuePoints("points: STOPPED: five writes in a row into the ball's point value were refused by the system, so nothing is changed");
                    return;
                }
            }
        }
        // 3. a basket: the ball says so (_shotMade). The report says which number the ball carried; whether the game counted it is for you to see on the scoreboard.
        if (made && !b.made) {
            b.made = true; b.madeAt = now;
            ptsBaskets_.fetch_add(1);
            const std::string line = fmt("Basket! The ball carried %d point%s when it went in%s", want, want == 1 ? "" : "s",
                                         b.gameWrites > 0 ? fmt(" (the game wanted %d)", b.lastGame).c_str() : "");
            { std::lock_guard<std::mutex> lock(mu_); ptsLast_ = line; }
            if (g_.ptsNotes < kNoteLimit) { ++g_.ptsNotes; queuePoints(fmt("points: BASKET on ball %llx: the ball's point value was %d (the game's own number for this throw: %s) - check the scoreboard / your score: did it go up by %d?",
                                                                         static_cast<unsigned long long>(b.ball), want, b.gameWrites > 0 ? fmt("%d", b.lastGame).c_str() : "never seen after mine", want)); }
        } else if (!made && b.made) {
            b.made = false;
        }
        ++i;
    }
}

// The switch went off (or the link did): where we changed the number and it is still ours, put the game's own number back.
void AimLink::pointsStandDown() {
    int restored = 0;
    if (pointsReady_.load(std::memory_order_acquire)) {
        for (PtsBall& b : g_.ptsBalls) {
            if (b.writes <= 0 || !b.haveGame) continue;
            int cur = 0; bool made = false;
            if (!pointsReadBall(b, &cur, &made)) continue;
            if (cur == b.lastWritten && cur != b.gameValue && pointsWrite(b, b.gameValue)) ++restored;
        }
    }
    if (!g_.ptsBalls.empty() || g_.ptsWasOn) {
        queuePoints(fmt("points: switch off - %d ball%s put back to the game's own number", restored, restored == 1 ? "" : "s"));
    }
    g_.ptsBalls.clear(); g_.ptsWasOn = false; g_.ptsWriteFailRun = 0; g_.ptsAsked = 0;
}

}  // namespace tzaimlink
