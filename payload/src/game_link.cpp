// game_link.cpp - see game_link.h.
#include "game_link.h"

#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace tzgame {

namespace {

enum Group { G_SPEED = 0, G_JUMP = 1, G_GRAVITY = 2 };

// The fields that are changed. `lo`/`hi` = the range a believable value of a REAL player object has (anything else means "this is not the live object").
struct Row { const char* name; const char* type; int group; int comps; float lo, hi; };
const Row kRows[] = {
    {"_forwardMaxSpeed", "System.Single", G_SPEED, 1, 0.01f, 100.0f},
    {"_forwardAcceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_forwardDeceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_lateralMaxSpeed", "System.Single", G_SPEED, 1, 0.01f, 100.0f},
    {"_lateralAcceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_lateralDeceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_backwardMaxSpeed", "System.Single", G_SPEED, 1, 0.01f, 100.0f},
    {"_backwardAcceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_backwardDeceleration", "System.Single", G_SPEED, 1, 0.001f, 1000.0f},
    {"_jumpHeightMultiplier", "System.Single", G_JUMP, 1, 0.01f, 100.0f},
    {"_gravity", "UnityEngine.Vector3", G_GRAVITY, 3, -1000.0f, 1000.0f},
};

const int kMaxNotes = 150;          // link lines in the facts file (so the file stays a size that can be sent)
const unsigned kMaxRebases = 300;   // a field the game keeps changing by itself: after this many "new original" events we leave it alone
const size_t kMaxTargetRecords = 16;

double nowSec() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9; }
float rdF(const unsigned char* b, int off) { float f; std::memcpy(&f, b + off, 4); return f; }

// The game set a value of its own. Believable as a new "original" if it has the same sign and is within 5x of the first value we ever saw
// (a zero first value, like the sideways part of gravity, accepts any small number).
bool saneChange(float cur, float anchor) {
    if (!std::isfinite(cur)) return false;
    if (anchor == 0.0f) return std::fabs(cur) < 1e4f;
    const float ratio = cur / anchor;
    return ratio > 0.2f && ratio < 5.0f;
}

}  // namespace

// A copy of the player object in OUR memory must never look like a running object (the next memory search would find it and think the object is still
// there). So the class pointer in the first 8 bytes is stored inverted, right after the copy.
bool PlayerLink::readObject(uintptr_t addr, unsigned char* out, size_t n) const {
    if (!pipe_.copy(addr, out, n)) return false;
    uint64_t k; std::memcpy(&k, out, 8); k = ~k; std::memcpy(out, &k, 8);
    return true;
}

PlayerLink::PlayerLink() {}
PlayerLink::~PlayerLink() { stop(); }

void PlayerLink::say(const char* fmt, ...) {
    if (!cfg_.note) return;
    if (notes_ >= kMaxNotes) return;
    char b[700];
    va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ++notes_;
    cfg_.note("%s", b);
    if (notes_ == kMaxNotes) cfg_.note("link: (that is enough link lines - the rest are not written, so the facts file stays small)");
}

void PlayerLink::fail(const std::string& why) {        // call with mu_ held
    failed_ = true; failReason_ = why; searching_ = false;
    say("link: FAILED: %s", why.c_str());
}

bool PlayerLink::start(const LinkConfig& cfg) {
    cfg_ = cfg;
    if (started_) return true;
    quit_.store(false);
    pipeOk_ = pipe_.open(0, nullptr);
    if (!pipeOk_) { std::lock_guard<std::mutex> lock(mu_); fail("the safe copy pipe does not work here"); return false; }
    if (pthread_create(&th_, nullptr, threadMain, this) != 0) { std::lock_guard<std::mutex> lock(mu_); fail("could not start the link thread"); return false; }
    started_ = true;
    return true;
}

void PlayerLink::stop() {
    if (!started_) return;
    quit_.store(true);
    pthread_join(th_, nullptr);
    started_ = false;
}

void* PlayerLink::threadMain(void* self) { static_cast<PlayerLink*>(self)->loop(); return nullptr; }

void PlayerLink::setWanted(bool w) {
    std::lock_guard<std::mutex> lock(mu_);
    if (w && !wanted_) {
        failed_ = false; failReason_.clear(); emptySearches_ = 0; nextSearchAt_ = 0;
        if (aliveLocked() == 0 || nowSec() - lastSearchEnd_ > cfg_.refreshSeconds) searchRequested_ = true;     // fresh enough objects are used as they are
        if (++onNotes_ <= 8) say("link: a movement switch was turned on%s", searchRequested_ ? ": looking for the player object" : " (the player object is already linked)");
    } else if (!w && wanted_) {
        if (++offNotes_ <= 8) say("link: all movement switches are off");
    }
    wanted_ = w;
}

int PlayerLink::aliveLocked() const { int n = 0; for (const Target& t : targets_) if (t.alive) ++n; return n; }
int PlayerLink::aliveTargets() const { std::lock_guard<std::mutex> lock(mu_); return aliveLocked(); }

int PlayerLink::uiState() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (failed_ && wanted_) return 3;
    if (aliveLocked() > 0) return 1;
    return wanted_ ? 2 : 0;
}
std::string PlayerLink::failReason() const { std::lock_guard<std::mutex> lock(mu_); return failReason_; }

float PlayerLink::factorFor(int group) const {
    if (!active_) return 1.0f;
    return group == G_SPEED ? want_.speedMul : (group == G_JUMP ? want_.jumpHeightMul : want_.gravityMul);
}

// ---------------------------------------------------------------- the Adapter (called by the movement Controller from the main loop)
bool PlayerLink::ready() { std::lock_guard<std::mutex> lock(mu_); return aliveLocked() > 0; }

bool PlayerLink::captureOriginals() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!layout_.ok) return false;
    int ok = 0;
    for (Target& t : targets_) {
        if (!t.alive) continue;
        if (!readObject(t.addr, buf_.data(), buf_.size())) { kill(t, "its memory can no longer be read"); continue; }
        for (Slot& s : t.slots) {
            const Def& d = layout_.defs[s.def];
            const float cur = rdF(buf_.data(), d.offset);
            if (!std::isfinite(cur)) continue;
            if (s.have && cur == s.last && s.last != s.orig) continue;     // still holds OUR value (a restore did not land): the original we remember is the truth
            s.orig = s.last = cur; if (!s.have) s.anchor = cur;
            s.have = true; s.blocked = false;
        }
        ++ok;
    }
    return ok > 0;
}

void PlayerLink::apply(const tzmove::Effective& e) {
    std::lock_guard<std::mutex> lock(mu_);
    const bool speedChanged = e.speedMul != want_.speedMul;
    want_ = e; active_ = !e.isIdentity();
    if (speedChanged) for (Target& t : targets_) t.peakSpeed = 0;
    tickLocked(nowSec());
    // one read-back so the facts file says whether the writes landed
    for (Target& t : targets_) {
        if (!t.alive || applyNotes_ >= 12) continue;
        ++applyNotes_;
        float fwd = 0, jump = 0, gy = 0, fwdExp = 0, jumpExp = 0, gyExp = 0;
        if (!readObject(t.addr, buf_.data(), buf_.size())) continue;
        for (const Slot& s : t.slots) {
            const Def& d = layout_.defs[s.def];
            const float cur = rdF(buf_.data(), d.offset), exp = s.have ? (factorFor(d.group) == 1.0f ? s.orig : s.orig * factorFor(d.group)) : 0.0f;
            if (!std::strcmp(d.name, "_forwardMaxSpeed")) { fwd = cur; fwdExp = exp; }
            else if (!std::strcmp(d.name, "_jumpHeightMultiplier")) { jump = cur; jumpExp = exp; }
            else if (!std::strcmp(d.name, "_gravity") && d.comp == 1) { gy = cur; gyExp = exp; }
        }
        say("link: applied speed x%.2f, jump height x%.2f, gravity x%.2f. Read-back from the game (object %llx): _forwardMaxSpeed %.4g (wanted %.4g), _jumpHeightMultiplier %.4g (wanted %.4g), _gravity.y %.4g (wanted %.4g)",
            static_cast<double>(e.speedMul), static_cast<double>(e.jumpHeightMul), static_cast<double>(e.gravityMul), static_cast<unsigned long long>(t.addr),
            static_cast<double>(fwd), static_cast<double>(fwdExp), static_cast<double>(jump), static_cast<double>(jumpExp), static_cast<double>(gy), static_cast<double>(gyExp));
        break;
    }
}

void PlayerLink::restoreOriginals() {
    std::lock_guard<std::mutex> lock(mu_);
    want_ = tzmove::Effective(); active_ = false;
    tickLocked(nowSec());
    int back = 0, notBack = 0;
    for (const Target& t : targets_) {
        if (!t.alive) continue;
        for (const Slot& s : t.slots) { if (s.have && s.last != s.orig) ++notBack; else ++back; }
    }
    if (++putBackNotes_ <= 6 || notBack) say("link: the game's own values were put back (%d fields back to normal%s)", back, notBack ? ", SOME COULD NOT BE PUT BACK" : "");
}

// ---------------------------------------------------------------- checking and writing (mu_ held)
void PlayerLink::kill(Target& t, const char* why) {
    if (!t.alive) return;
    t.alive = false; t.died = why; ++lostTotal_;
    if (lostTotal_ <= 12) say("link: lost the player object %llx: %s", static_cast<unsigned long long>(t.addr), why);
}

bool PlayerLink::writeFloat(Target& t, int offset, float v) {
    unsigned char b[4]; std::memcpy(b, &v, 4);
    if (!pipe_.put(t.addr + static_cast<uintptr_t>(offset), b, 4)) { kill(t, "it can no longer be written"); return false; }
    ++t.writes; ++writesTotal_;
    return true;
}

void PlayerLink::processSlot(Target& t, Slot& s, const unsigned char* bytes, float factor) {
    const Def& d = layout_.defs[s.def];
    const float cur = rdF(bytes, d.offset);
    if (!std::isfinite(cur)) { ++t.ignored; ++ignoredTotal_; return; }
    if (!s.have) { s.orig = s.last = s.anchor = cur; s.have = true; }
    const float expected = factor == 1.0f ? s.orig : s.orig * factor;
    if (cur == expected) { s.last = expected; return; }
    if (s.blocked) return;
    if (cur == s.orig || cur == s.last) {
        // our value is not there: the first write, a moved slider, a switch turned off, or the game put its own value back
        if (cur == s.orig && s.last != s.orig && factor != 1.0f) {
            ++t.overwrites; ++overwritesTotal_;
            if (overwritesTotal_ <= 6) say("link: the game wrote %s back to its own value (%.4g) - putting ours (%.4g) back", d.name, static_cast<double>(cur), static_cast<double>(expected));
        }
        if (writeFloat(t, d.offset, expected)) s.last = expected;
        return;
    }
    // neither the game's value nor ours: the game itself set a NEW value (a new setting arrived). That is the new original.
    if (!saneChange(cur, s.anchor)) {
        ++t.ignored; ++ignoredTotal_;
        if (ignoredTotal_ <= 4) say("link: the game set %s to %.4g, far from the usual %.4g - left alone", d.name, static_cast<double>(cur), static_cast<double>(s.anchor));
        return;
    }
    s.orig = cur; ++t.rebases; ++rebasesTotal_;
    if (rebasesTotal_ <= 6) say("link: the game set a new value for %s: %.4g (taking it as the new original)", d.name, static_cast<double>(cur));
    if (++s.rebases > kMaxRebases) { s.blocked = true; say("link: the game keeps changing %s by itself - leaving it alone from now on", d.name); return; }
    const float next = factor == 1.0f ? cur : cur * factor;
    if (next != cur) { if (writeFloat(t, d.offset, next)) s.last = next; }
    else s.last = cur;
}

void PlayerLink::tickLocked(double now) {
    if (!layout_.ok) return;
    const uint64_t wantKlass = layout_.cls.klassInv;          // (the copy keeps the class pointer inverted - see readObject)
    for (Target& t : targets_) {
        if (!t.alive) continue;
        if (!readObject(t.addr, buf_.data(), buf_.size())) { kill(t, "its memory can no longer be read"); continue; }
        uint64_t klassWord, cached;
        std::memcpy(&klassWord, buf_.data(), 8); std::memcpy(&cached, buf_.data() + 16, 8);
        if (klassWord != wantKlass) { kill(t, "the memory now holds something else"); continue; }
        if (layout_.cls.unityObject && cached == 0) { kill(t, "the game destroyed it (new scene?)"); continue; }
        ++t.ticks;
        if (layout_.head >= 0) { if (std::memcmp(t.lastHead, buf_.data() + layout_.head, 12) != 0) { ++t.headMoves; std::memcpy(t.lastHead, buf_.data() + layout_.head, 12); } }
        if (active_) {
            if (layout_.curSpeed >= 0) { const float v = rdF(buf_.data(), layout_.curSpeed); if (std::isfinite(v) && v > t.peakSpeed && v < 1e4f) t.peakSpeed = v; }
            if (layout_.curJumpSpeed >= 0) { const float v = rdF(buf_.data(), layout_.curJumpSpeed); if (std::isfinite(v) && v > t.peakJumpSpeed && v < 1e4f) t.peakJumpSpeed = v; }
        }
        for (Slot& s : t.slots) {
            if (!t.alive) break;
            processSlot(t, s, buf_.data(), factorFor(layout_.defs[s.def].group));
        }
    }
    if (wanted_ && aliveLocked() == 0 && nextSearchAt_ > now + 1.0) nextSearchAt_ = now + 1.0;       // everything is gone: look again soon
}

// ---------------------------------------------------------------- finding the player object
bool PlayerLink::resolveLayout(std::string* why, bool* transient) {
    *transient = false;
    tzscan::Api api; std::string pw;
    if (!cfg_.provider || !cfg_.provider(&api, &pw)) {
        *why = "the game's runtime (libil2cpp.so) is not available yet" + (pw.empty() ? std::string() : ": " + pw);
        *transient = true; return false;
    }
    tzscan::ClassInfo ci = tzscan::findClass(api, cfg_.ns, cfg_.cls);
    if (!ci.found) { *why = ci.error; *transient = ci.error.find("not ready") != std::string::npos; return false; }
    Layout L; L.cls = ci; L.objSize = ci.size;
    for (const Row& r : kRows) {
        const tzscan::FieldInfo* fi = ci.field(r.name);
        if (!fi) { *why = std::string("the field ") + r.name + " is missing in " + ci.fullName + " (the game was updated?)"; return false; }
        if (fi->typeName != r.type) { *why = std::string("the field ") + r.name + " has type " + fi->typeName + ", expected " + r.type; return false; }
        if (fi->offset + 4 * r.comps > ci.size) { *why = std::string("the field ") + r.name + " lies outside the object"; return false; }
        for (int c = 0; c < r.comps; ++c) L.defs.push_back({r.name, c, fi->offset + 4 * c, r.group, r.lo, r.hi});
    }
    auto opt = [&](const char* name, const char* type, int* out) { const tzscan::FieldInfo* fi = ci.field(name); if (fi && fi->typeName == type && fi->offset + 4 <= ci.size) *out = fi->offset; };
    opt("_hmdTrackingInitialized", "System.Boolean", &L.hmdInit);
    opt("_speed", "System.Single", &L.curSpeed);
    opt("_jumpSpeed", "System.Single", &L.curJumpSpeed);
    opt("_prevHmdLocalPosition", "UnityEngine.Vector3", &L.head);
    if (L.head >= 0 && L.head + 12 > ci.size) L.head = -1;
    L.ok = true;
    {
        std::lock_guard<std::mutex> lock(mu_);
        layout_ = L; buf_.assign(static_cast<size_t>(L.objSize), 0);
        std::string where;
        for (const Row& r : kRows) { char b[80]; std::snprintf(b, sizeof b, "%s@%d ", r.name, ci.field(r.name)->offset); where += b; }
        say("link: found the class %s (object size %d). Fields used: %s%s", ci.fullName.c_str(), ci.size, where.c_str(),
            L.hmdInit >= 0 ? "| active-player check: _hmdTrackingInitialized" : "| (no _hmdTrackingInitialized: every sane copy counts as active)");
    }
    return true;
}

int PlayerLink::judge(const std::vector<unsigned char>& b) const {
    if (b.size() < static_cast<size_t>(layout_.objSize)) return 3;
    if (layout_.hmdInit >= 0 && b[static_cast<size_t>(layout_.hmdInit)] != 1) return 1;
    for (const Def& d : layout_.defs) {
        const float v = rdF(b.data(), d.offset);
        if (!std::isfinite(v) || v < d.lo || v > d.hi) return 2;
    }
    return 0;
}

void PlayerLink::install(const std::vector<uintptr_t>& addrs, const std::vector<std::vector<unsigned char>>& bytes, double now) {
    std::lock_guard<std::mutex> lock(mu_);
    for (size_t i = 0; i < addrs.size(); ++i) {
        Target* have = nullptr;
        for (Target& t : targets_) if (t.addr == addrs[i]) { have = &t; break; }
        if (have) {
            if (!have->alive) { have->alive = true; have->died.clear(); say("link: the player object %llx is back", static_cast<unsigned long long>(addrs[i])); }
            continue;
        }
        Target t; t.addr = addrs[i]; t.alive = true;
        for (size_t d = 0; d < layout_.defs.size(); ++d) {
            Slot s; s.def = static_cast<int>(d);
            s.orig = s.last = s.anchor = rdF(bytes[i].data(), layout_.defs[d].offset); s.have = true;
            t.slots.push_back(s);
        }
        if (layout_.head >= 0) std::memcpy(t.lastHead, bytes[i].data() + layout_.head, 12);
        targets_.push_back(std::move(t));
    }
    while (targets_.size() > kMaxTargetRecords) {                 // forget the oldest dead record
        size_t victim = targets_.size();
        for (size_t i = 0; i < targets_.size(); ++i) if (!targets_[i].alive) { victim = i; break; }
        if (victim == targets_.size()) break;
        targets_.erase(targets_.begin() + static_cast<long>(victim));
    }
    if (active_ && aliveLocked() > 0) tickLocked(now);            // a switch is already on: the new object gets it right away
}

void PlayerLink::doSearch() {
    const double t0 = nowSec();
    { std::lock_guard<std::mutex> lock(mu_); searching_ = true; searchRequested_ = false; }
    if (!layout_.ok) {
        std::string why; bool transient = false;
        if (!resolveLayout(&why, &transient)) {
            std::lock_guard<std::mutex> lock(mu_);
            searching_ = false; lastSearchEnd_ = nowSec();
            if (transient) {
                nextSearchAt_ = nowSec() + cfg_.retrySoonSeconds + std::min(12.0, emptySearches_ * 2.0); ++emptySearches_;
                if (emptySearches_ == 1 || emptySearches_ % 10 == 0) say("link: not ready yet (%s) - trying again in a few seconds", why.c_str());
            } else fail(why);
            return;
        }
    }
    searches_.fetch_add(1);
    tzscan::CopySearch cs = tzscan::findCopies(layout_.cls, 64, cfg_.searchSeconds, cfg_.stallSeconds, cfg_.testHangAfterChunks, cfg_.testPollMicros);
    const double t1 = nowSec();
    if (!cs.ok) {
        std::lock_guard<std::mutex> lock(mu_);
        lastSearchEnd_ = t1;
        fail("looking for the player object did not work: " + cs.error);
        return;
    }
    // which copies are the ACTIVE player? (head tracking started + believable numbers)
    int rejHmd = 0, rejNum = 0, rejOther = 0;
    std::vector<std::pair<uintptr_t, size_t>> pass;
    for (size_t i = 0; i < cs.copies.size(); ++i) {
        const int j = judge(cs.copies[i].bytes);
        if (j == 0) pass.push_back({cs.copies[i].addr, i});
        else if (j == 1) ++rejHmd; else if (j == 2) ++rejNum; else ++rejOther;
    }
    std::sort(pass.begin(), pass.end());
    if (static_cast<int>(pass.size()) > cfg_.maxTargets) pass.resize(static_cast<size_t>(cfg_.maxTargets));
    std::vector<uintptr_t> addrs; std::vector<std::vector<unsigned char>> bytes;
    for (const auto& p : pass) { addrs.push_back(p.first); bytes.push_back(cs.copies[p.second].bytes); }
    install(addrs, bytes, t1);
    {
        std::lock_guard<std::mutex> lock(mu_);
        searching_ = false; lastSearchEnd_ = t1;
        const int alive = aliveLocked();
        char b[260];
        std::snprintf(b, sizeof b, "search #%u: %.1f s, %llu MB read, %d copies found, %d look like the active player (not started: %d, odd numbers: %d)",
                      searches_.load(), t1 - t0, cs.bytesRead >> 20, static_cast<int>(cs.copies.size()), static_cast<int>(pass.size()), rejHmd, rejNum + rejOther);
        lastSearchText_ = b;
        if (alive > 0) {
            emptySearches_ = 0; nextRefreshAt_ = t1 + cfg_.refreshSeconds;
            std::string list;
            for (const Target& t : targets_) if (t.alive) { char a[32]; std::snprintf(a, sizeof a, "%llx ", static_cast<unsigned long long>(t.addr)); list += a; }
            say("link: %s -> CONNECTED to %d object(s): %s", b, alive, list.c_str());
        } else {
            nextSearchAt_ = t1 + std::min(15.0, cfg_.retrySoonSeconds + emptySearches_ * 2.0); ++emptySearches_;
            if (emptySearches_ <= 3 || emptySearches_ % 10 == 0) say("link: %s -> no active player yet, trying again in a few seconds", b);
        }
    }
}

void PlayerLink::loop() {
    while (!quit_.load()) {
        const double now = nowSec();
        bool search = false, tick = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const int alive = aliveLocked();
            const bool gapOk = now - lastSearchEnd_ >= cfg_.minSearchGapSeconds;
            if (wanted_ && !failed_ && gapOk) {
                if (searchRequested_) search = true;
                else if (alive == 0 && now >= nextSearchAt_) search = true;
                else if (alive > 0 && active_ && now >= nextRefreshAt_) search = true;
            }
            if (!search && alive > 0) {
                bool dirty = active_;
                if (!dirty) for (const Target& t : targets_) { if (!t.alive) continue; for (const Slot& s : t.slots) if (s.have && s.last != s.orig) dirty = true; }
                tick = dirty;
            }
        }
        if (search) { doSearch(); continue; }
        if (tick) {
            { std::lock_guard<std::mutex> lock(mu_); tickLocked(now); }
            ticks_.fetch_add(1);
            usleep(static_cast<useconds_t>(cfg_.tickMs) * 1000);
            continue;
        }
        usleep(50 * 1000);
    }
}

// ---------------------------------------------------------------- reporting
std::string PlayerLink::summary() const {
    std::lock_guard<std::mutex> lock(mu_);
    char b[900];
    const int alive = aliveLocked();
    const char* state = failed_ ? "FAILED" : (alive > 0 ? "connected" : (wanted_ ? (searching_ ? "searching" : "looking") : "idle"));
    int n = std::snprintf(b, sizeof b, "link: %s%s%s | objects alive %d of %d known | searches %u | writes %llu, game-wrote-back %llu, new-base %llu, odd-values %llu, lost %llu",
                          state, failed_ ? ": " : "", failed_ ? failReason_.c_str() : "", alive, static_cast<int>(targets_.size()), searches_.load(),
                          writesTotal_, overwritesTotal_, rebasesTotal_, ignoredTotal_, lostTotal_);
    for (const Target& t : targets_) {
        if (!t.alive || !layout_.ok) continue;
        float fwdNow = 0, fwdOrig = 0, jNow = 0, jOrig = 0, gNow = 0, gOrig = 0;
        std::vector<unsigned char> tmp(static_cast<size_t>(layout_.objSize));
        const bool got = readObject(t.addr, tmp.data(), tmp.size());
        for (const Slot& s : t.slots) {
            const Def& d = layout_.defs[s.def];
            const float cur = got ? rdF(tmp.data(), d.offset) : 0.0f;
            if (!std::strcmp(d.name, "_forwardMaxSpeed")) { fwdNow = cur; fwdOrig = s.orig; }
            else if (!std::strcmp(d.name, "_jumpHeightMultiplier")) { jNow = cur; jOrig = s.orig; }
            else if (!std::strcmp(d.name, "_gravity") && d.comp == 1) { gNow = cur; gOrig = s.orig; }
        }
        if (n > 0 && n < static_cast<int>(sizeof b) - 200)
            n += std::snprintf(b + n, sizeof b - static_cast<size_t>(n), " | object %llx: forwardMax %.4g (game's own %.4g), jumpMult %.4g (own %.4g), gravityY %.4g (own %.4g), peak speed %.3g, peak jump speed %.3g, head moved %llu times",
                               static_cast<unsigned long long>(t.addr), static_cast<double>(fwdNow), static_cast<double>(fwdOrig), static_cast<double>(jNow), static_cast<double>(jOrig),
                               static_cast<double>(gNow), static_cast<double>(gOrig), static_cast<double>(t.peakSpeed), static_cast<double>(t.peakJumpSpeed), t.headMoves);
        break;                                                      // the first linked object is enough for the summary
    }
    return b;
}

bool PlayerLink::debugValue(const char* field, int component, float* now, float* original) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (!layout_.ok) return false;
    for (const Target& t : targets_) {
        if (!t.alive) continue;
        std::vector<unsigned char> tmp(static_cast<size_t>(layout_.objSize));
        if (!readObject(t.addr, tmp.data(), tmp.size())) return false;
        for (const Slot& s : t.slots) {
            const Def& d = layout_.defs[s.def];
            if (!std::strcmp(d.name, field) && d.comp == component) { if (now) *now = rdF(tmp.data(), d.offset); if (original) *original = s.orig; return true; }
        }
        return false;
    }
    return false;
}

}  // namespace tzgame
