// movement.cpp - see movement.h.
#include "movement.h"

#include <cmath>

namespace tzmove {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
bool close(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

bool Effective::sameAs(const Effective& o) const {
    return close(speedMul, o.speedMul) && close(jumpHeightMul, o.jumpHeightMul) && close(gravityMul, o.gravityMul);
}

float Effective::jumpSpeedMul() const { return std::sqrt(jumpHeightMul); }

Effective resolve(const Request& r) {
    Effective e;
    if (r.flyActive) return e;                    // Fly wins: every boost steps aside while flying
    if (r.speedOn) e.speedMul = clampf(r.speed, 1.0f, 5.0f);
    if (r.jumpOn) e.jumpHeightMul = clampf(r.jump, 1.0f, 5.0f);
    if (r.gravityMode == 1) e.gravityMul = 1.0f - clampf(r.lowPct, 0.0f, 90.0f) / 100.0f;     // 90% -> 10% of normal gravity left
    else if (r.gravityMode == 2) e.gravityMul = 1.0f + clampf(r.highPct, 0.0f, 90.0f) / 100.0f; // 90% -> 190% of normal gravity
    return e;
}

bool Controller::update(const Request& r) {
    if (!adapter_ || !adapter_->ready()) { status_ = NO_LINK; return false; }
    const Effective want = resolve(r);
    if (want.sameAs(applied_)) { if (status_ != ERROR_) status_ = applied_.isIdentity() ? IDLE : ACTIVE; return false; }   // nothing new: do NOT apply again
    if (want.isIdentity()) {                       // everything off (or Fly took over): put the originals back
        if (haveOriginals_) { adapter_->restoreOriginals(); ++restoreCalls_; }
        haveOriginals_ = false;                    // next time, read the game's values again
        applied_ = Effective();
        status_ = IDLE;
        return true;
    }
    if (!haveOriginals_) {                         // first effect since everything was off: remember the game's own values
        if (failedWith_.sameAs(want)) return false;               // it failed for exactly this request already: do not hammer the game
        if (!adapter_->captureOriginals()) { status_ = ERROR_; failedWith_ = want; return false; }
        haveOriginals_ = true;
    }
    failedWith_ = Effective();
    adapter_->apply(want);                         // absolute: original x factor
    ++applyCalls_;
    applied_ = want;
    status_ = ACTIVE;
    return true;
}

void Controller::shutdown() {
    if (adapter_ && haveOriginals_) { adapter_->restoreOriginals(); ++restoreCalls_; }
    haveOriginals_ = false;
    applied_ = Effective();
    status_ = adapter_ ? IDLE : NO_LINK;
}

}  // namespace tzmove
