// aim_hitbox.cpp - stage D11b: the "Hitbox expander" part of the game link (Troll page).
//
// WHAT THIS DOES (plain words)
//   Your hands are made of "hitboxes" (the game's word is "hand colliders": invisible solid shapes). When a hitbox touches the ball, the game's physics reacts:
//   the ball bounces off your hand, and the game plays the hand-touch haptics (Basketball.CheckHandCollision). In stage D11 the expander only made these hitboxes
//   bigger - and the test showed the ball could be TOUCHED from further away (you felt the vibration) but not GRABBED: the game grabs with other numbers.
//   Stage D11b therefore makes two things bigger by the slider's factor (1.0x = the normal size ... 10.0x):
//     (1) the hitboxes of YOUR two hands (as before), and
//     (2) the game's own grab-reach numbers of your two hands (all from the game's own files; each one that exists is used):
//           Autohand.Hand.reachDistance and palmRadius (floats), ShovelTools.BallControl._gravityDistance (a float: how close the ball has to be before the game pulls
//           it into the hand) and BallControl._grabVolume (a Transform: the game's grab volume on the hand; its local scale is multiplied).
//   The game's own values are remembered and put back when the switch goes OFF (or the slider goes back to 1.0x).
//
// HOW IT FINDS YOUR HANDS (all names are from the game's own files, the stage D7c / D10 scans)
//   your ball control manager (BallControlManager, the same object the Aimbot uses) -> _leftBallControl / _rightBallControl (BallControl) -> _hand (Autohand.Hand)
//   -> _handColliders (a list of UnityEngine.Collider). Each collider is a BoxCollider, SphereCollider or CapsuleCollider; the engine's own get_ / set_ calls
//   read and change its size (box: size; sphere: radius; capsule: radius and height). Other players' hands are never touched: only the two hands that
//   belong to your ball control manager are.
//
// WHAT IS PROVEN AND WHAT IS NOT
//   Proven by the game's own files: all the class and field names above (with their types and positions). NOT proven (never run in the real game): which of the four
//   grab-reach numbers the game's grab really uses (so all of them are scaled together and the facts file says what each one was and what it was set to); whether
//   the game puts its own numbers back (the sizes are checked ten times a second and every change by the game is counted and written down); whether other players
//   see the bigger hands (the game may decide a steal on the other player's side); whether very big hitboxes make your hand physics jump around.
//
// WHAT IT NEVER DOES
//   It never touches anything but the hitboxes and the grab-reach numbers of your two hands. It only makes engine calls when something changed (at most ten
//   looks a second), stops on its own after 12 refused calls in a row, and puts the game's own values back when it is switched off.
#include "aim_link.h"

#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tzaimlink {

namespace {
using tzaim::Vec3;
uint64_t rdQ(const unsigned char* b, int off) { uint64_t v; std::memcpy(&v, b + off, 8); return v; }
uintptr_t rdP(const unsigned char* b, int off) { return static_cast<uintptr_t>(rdQ(b, off)); }
bool plausiblePtr(uintptr_t p) { return p > 0x10000 && (p & 7) == 0 && p < 0x0000800000000000ULL; }
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) { char b[1500]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }

const size_t kMaxCols = 64;             // hitboxes handled (two hands have far fewer than this)
const int kMaxLines = 60;               // all hitbox lines together in the facts file
const int kNoteLimit = 30;              // game-thread lines (census, resets, display)
const double kPace = 0.10;              // the game thread looks at most this often (seconds)
const double kCensusEvery = 2.0;        // the list of hitboxes is rebuilt (and the sizes checked) this often
const float kMaxGrabValue = 1000.0f;    // a grab-reach value (or Transform scale) that would get bigger than this is left alone
const float kMaxWorldSize = 50.0f;      // a hitbox that would get bigger than this (metres) is left alone

const char* kindName(int k) { return k == 1 ? "BoxCollider" : (k == 2 ? "SphereCollider" : (k == 3 ? "CapsuleCollider" : "?")); }
int kindComps(int k) { return k == 1 ? 3 : (k == 3 ? 2 : 1); }       // numbers a hitbox of this kind has (box: x y z; sphere: radius; capsule: radius, height)
bool nearV(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i) if (std::fabs(a[i] - b[i]) > 1e-4f + 1e-3f * std::fabs(b[i])) return false;
    return true;
}
std::string sizeText(int kind, const float* v) {
    if (kind == 1) return fmt("size (%.3f, %.3f, %.3f)", static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]));
    if (kind == 3) return fmt("radius %.3f height %.3f", static_cast<double>(v[0]), static_cast<double>(v[1]));
    return fmt("radius %.3f", static_cast<double>(v[0]));
}

// A required field: right name, right type, inside the object.
bool needField(const tzscan::ClassInfo& ci, const char* name, const char* type, int bytes, bool required, int* out, std::string* why) {
    const tzscan::FieldInfo* fi = ci.field(name);
    if (!fi) { if (required) *why = std::string("the field ") + name + " is missing in " + ci.fullName + " (the game was updated?)"; return !required; }
    if (type && fi->typeName != type) { if (required) *why = std::string("the field ") + name + " in " + ci.fullName + " has type " + fi->typeName + ", expected " + type; return !required; }
    if (fi->offset < 16 || fi->offset + bytes > ci.size) { if (required) *why = std::string("the field ") + name + " lies outside " + ci.fullName; return !required; }
    *out = fi->offset;
    return true;
}
}  // namespace

// ---------------------------------------------------------------- facts file
void AimLink::sayHitbox(const char* f, ...) {
    if (!cfg_.note) return;
    char b[1800];
    va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap);
    const int n = hbNotes_.fetch_add(1);
    if (n >= kMaxLines) return;
    cfg_.note("%s", b);
    if (n + 1 == kMaxLines) cfg_.note("hitbox: (that is enough hitbox lines - the rest are not written, so the facts file stays small)");
}

void AimLink::queueHitbox(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMu_);
    if (out_.size() < 64) out_.push_back(line);
}

bool AimLink::linkWanted() const {
    return mode_.load() != 0 || points_.load() != 0 || hitboxX10_.load() != 0;
}

// ---------------------------------------------------------------- the menu's request
void AimLink::setHitbox(bool expand, float mul) {
    int x10 = 0;
    if (expand) {
        if (!(mul >= 1.0f)) mul = 1.0f;                    // (also catches NaN)
        if (mul > 10.0f) mul = 10.0f;
        x10 = static_cast<int>(std::lround(mul * 10.0f));
    }
    const int was = hitboxX10_.exchange(x10);
    const bool linkOn = linkWanted();
    const bool linkWas = on_.exchange(linkOn);
    if (linkOn && !linkWas) initOn();
    if (x10 != 0 && was == 0) {
        std::lock_guard<std::mutex> lock(mu_);
        nextHitboxAt_ = 0; hitboxTries_ = 0; hitboxFailWhy_.clear(); hitboxDeadWhy_.clear(); hbInfo_.clear();
        hitboxDead_.store(false);
    }
    if (x10 != 0 && was == 0) sayHitbox("hitbox: Hitbox expander turned ON at %.1fx: every hitbox and every grab-reach number of your two hands is made that much bigger (the game's own values are put back when it goes off)", static_cast<double>(x10) / 10.0);
    else if (x10 == 0 && was != 0) sayHitbox("hitbox: Hitbox expander turned OFF (the game's own values are put back)");
}

// ---------------------------------------------------------------- state for the menu and the facts file
int AimLink::hitboxUiState() const {
    const int x10 = hitboxX10_.load();
    if (x10 == 0) return 0;
    if (!failReason().empty()) return 3;
    if (hitboxDead_.load()) return 3;
    if (!layoutReady_.load() || !hitboxReady_.load() || !bound_.load()) return 2;
    if (hbHands_.load() == 0) return 2;
    if (hbBoxCount_.load() > 0 && hbUsable_.load() == 0 && hbTwUsable_.load() == 0) return 3;       // hitboxes were found but none of them can be resized, and no grab-reach value can be changed
    if (hbBoxCount_.load() == 0 && hbTwUsable_.load() == 0) return 2;                                 // the hands have no hitboxes yet (the game makes them a moment after the hands)
    return 1;
}

std::string AimLink::hitboxHeadline() const {
    if (hitboxX10_.load() == 0) return "";
    const std::string why = failReason();
    if (!why.empty()) return "FAILED: " + why;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (hitboxDead_.load()) return "FAILED: " + hitboxDeadWhy_;
    }
    if (!layoutReady_.load()) return "looking at the game's code ...";
    if (!hitboxReady_.load()) {
        std::lock_guard<std::mutex> lock(mu_);
        return hitboxFailWhy_.empty() ? std::string("looking at the hands' code ...") : "waiting: " + hitboxFailWhy_;
    }
    if (!bound_.load()) return "looking for your ball control ...";
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!hbInfo_.empty()) return hbInfo_;
    }
    return "looking for your hands ...";
}

HitboxCounters AimLink::hitboxCounters() const {
    HitboxCounters c;
    c.censuses = hbCensuses_.load(); c.resized = hbResized_.load(); c.restored = hbRestored_.load(); c.gameResets = hbGameResets_.load();
    c.readFails = hbReadFails_.load(); c.writeFails = hbWriteFails_.load();
    c.grabSet = hbTwSet_.load(); c.grabRestored = hbTwRestored_.load(); c.grabResets = hbTwResets_.load(); c.grabReadFails = hbTwReadFails_.load(); c.grabWriteFails = hbTwWriteFails_.load(); c.grabValues = hbTwUsable_.load();
    c.hands = hbHands_.load(); c.hitboxes = hbBoxCount_.load(); c.resizable = hbUsable_.load();
    c.boxes = hbBoxes_.load(); c.spheres = hbSpheres_.load(); c.capsules = hbCapsules_.load(); c.meshes = hbMeshes_.load(); c.others = hbOthers_.load();
    return c;
}

std::string AimLink::hitboxSummary() const {
    const HitboxCounters c = hitboxCounters();
    const int ui = hitboxUiState();
    const char* state = ui == 1 ? "connected" : (ui == 2 ? "looking" : (ui == 3 ? "FAILED" : "off"));
    const std::string why = ui == 3 ? hitboxHeadline() : std::string();
    const int x10 = hitboxX10_.load();
    return fmt("hitbox: link %s%s%s | asked: expander %s | hands %d, hitboxes %d (can be resized %d: box %d, sphere %d, capsule %d; other kinds %d, mesh %d) | sizes set %llu, put back %llu, the game put a size back after mine %llu times | could not read %llu, could not set %llu | GRAB-REACH values: %d can be changed, set %llu times, put back %llu times, the game put its own value back after mine %llu times, could not read %llu, could not set %llu | lists made %llu",
               state, why.empty() ? "" : ": ", why.c_str(), x10 == 0 ? "off" : fmt("%.1fx", static_cast<double>(x10) / 10.0).c_str(),
               c.hands, c.hitboxes, c.resizable, c.boxes, c.spheres, c.capsules, c.others, c.meshes, c.resized, c.restored, c.gameResets, c.readFails, c.writeFails,
               c.grabValues, c.grabSet, c.grabRestored, c.grabResets, c.grabReadFails, c.grabWriteFails, c.censuses);
}

void AimLink::hbSetInfo(const std::string& text) {
    std::lock_guard<std::mutex> lock(mu_);
    hbInfo_ = text;
}

void AimLink::hbDie(const std::string& why) {
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!hitboxDead_.load()) { hitboxDeadWhy_ = why; first = true; }
        hitboxDead_.store(true);
    }
    if (first) queueHitbox("hitbox: STOPPED: " + why);
}

// ---------------------------------------------------------------- link thread: where the hands keep their hitboxes
bool AimLink::resolveHitboxLayout(std::string* why, bool* transient) {
    *transient = false;
    HitboxLayout H;
    const tzscan::Api& api = L_.api;
    auto fail = [&](const std::string& w, bool tr = false) { *why = w; *transient = tr; return false; };
    auto classOf = [&](const char* ns, const char* name, tzscan::ClassInfo* out) -> bool {
        *out = tzscan::findClass(api, ns, name);
        if (!out->found) { *why = out->error; *transient = out->error.find("not ready") != std::string::npos; return false; }
        return true;
    };
    if (!classOf("ShovelTools", "BallControl", &H.bc)) return false;
    if (!classOf("Autohand", "Hand", &H.hand)) return false;
    std::string w;
    if (!needField(L_.bcm, "_leftBallControl", "ShovelTools.BallControl", 8, true, &H.cL, &w)) return fail(w);
    if (!needField(L_.bcm, "_rightBallControl", "ShovelTools.BallControl", 8, true, &H.cR, &w)) return fail(w);
    if (!needField(H.bc, "_hand", "Autohand.Hand", 8, true, &H.bcHand, &w)) return fail(w);
    if (!needField(H.hand, "_handColliders", "UnityEngine.Collider[]", 8, true, &H.hColl, &w)) return fail(w);
    needField(H.hand, "_hasAuthority", "System.Boolean", 1, false, &H.hAuth, &w);
    // the grab-reach values: every one that exists in this game is used (which one the game's grab really looks at is not known - they are scaled together)
    struct TwDef { const char* name; int owner; const char* field; const char* type; int kind; int bytes; };
    static const TwDef defs[4] = {
        {"Hand.reachDistance", 0, "reachDistance", "System.Single", 0, 4},
        {"Hand.palmRadius", 0, "palmRadius", "System.Single", 0, 4},
        {"BallControl._gravityDistance", 1, "_gravityDistance", "System.Single", 0, 4},
        {"BallControl._grabVolume", 1, "_grabVolume", "UnityEngine.Transform", 1, 8},
    };
    std::string twMissing;
    for (const TwDef& d : defs) {
        int off = -1; std::string ww;
        needField(d.owner == 0 ? H.hand : H.bc, d.field, d.type, d.bytes, false, &off, &ww);
        if (off < 0) { twMissing += std::string(twMissing.empty() ? "" : ", ") + d.name; continue; }
        HitboxLayout::Tweak& t = H.tw[H.nTw++];
        t.name = d.name; t.owner = d.owner; t.off = off; t.type = d.kind;
    }

    // the engine's own classes and calls
    std::string err;
    auto cls = [&](const char* name) -> void* { return tzscan::findClassHandle(api, "UnityEngine", name, &err); };
    void* kObject = cls("Object"); void* kBox = cls("BoxCollider"); void* kSph = cls("SphereCollider"); void* kCap = cls("CapsuleCollider"); void* kMesh = cls("MeshCollider");
    if (!kBox && !kSph && !kCap) return fail("the engine classes BoxCollider / SphereCollider / CapsuleCollider were not found: " + err);
    H.kBox = reinterpret_cast<uint64_t>(kBox); H.kSphere = reinterpret_cast<uint64_t>(kSph); H.kCapsule = reinterpret_cast<uint64_t>(kCap); H.kMesh = reinterpret_cast<uint64_t>(kMesh);
    auto m0 = [&](void* k, const char* name, int argc) -> void* { return (k && api.class_get_method_from_name) ? api.class_get_method_from_name(k, name, argc) : nullptr; };
    H.gBox = m0(kBox, "get_size", 0);      H.sBox = m0(kBox, "set_size", 1);
    H.gSph = m0(kSph, "get_radius", 0);    H.sSph = m0(kSph, "set_radius", 1);
    H.gCapR = m0(kCap, "get_radius", 0);   H.sCapR = m0(kCap, "set_radius", 1);
    H.gCapH = m0(kCap, "get_height", 0);   H.sCapH = m0(kCap, "set_height", 1);
    H.mName = m0(kObject, "get_name", 0);
    void* kTransform = cls("Transform");
    H.mTGetLS = m0(kTransform, "get_localScale", 0); H.mTSetLS = m0(kTransform, "set_localScale", 1);
    // The engine's own internal calls: only looked up for the calls above that the game's code does not have (stripped).
    auto ic = [&](const char* a, const char* b) -> void* {
        if (!api.resolve_icall) return nullptr;
        void* f = api.resolve_icall(a);
        if (!f && b) f = api.resolve_icall(b);
        return f;
    };
    if (kBox && !H.gBox) H.iGBox = ic("UnityEngine.BoxCollider::get_size_Injected(UnityEngine.Vector3&)", "UnityEngine.BoxCollider::get_size_Injected");
    if (kBox && !H.sBox) H.iSBox = ic("UnityEngine.BoxCollider::set_size_Injected(UnityEngine.Vector3&)", "UnityEngine.BoxCollider::set_size_Injected");
    if (kSph && !H.gSph) H.iGSph = ic("UnityEngine.SphereCollider::get_radius", nullptr);
    if (kSph && !H.sSph) H.iSSph = ic("UnityEngine.SphereCollider::set_radius", nullptr);
    if (kCap && !H.gCapR) H.iGCapR = ic("UnityEngine.CapsuleCollider::get_radius", nullptr);
    if (kCap && !H.sCapR) H.iSCapR = ic("UnityEngine.CapsuleCollider::set_radius", nullptr);
    if (kCap && !H.gCapH) H.iGCapH = ic("UnityEngine.CapsuleCollider::get_height", nullptr);
    if (kCap && !H.sCapH) H.iSCapH = ic("UnityEngine.CapsuleCollider::set_height", nullptr);
    bool needScale = false;
    for (int i = 0; i < H.nTw; ++i) if (H.tw[i].type == 1) needScale = true;
    if (needScale && !H.mTGetLS) H.iTGetLS = ic("UnityEngine.Transform::get_localScale_Injected(UnityEngine.Vector3&)", "UnityEngine.Transform::get_localScale_Injected");
    if (needScale && !H.mTSetLS) H.iTSetLS = ic("UnityEngine.Transform::set_localScale_Injected(UnityEngine.Vector3&)", "UnityEngine.Transform::set_localScale_Injected");
    std::string scaleNote;
    if (needScale && !((H.mTGetLS || H.iTGetLS) && (H.mTSetLS || H.iTSetLS))) {      // the grab volume cannot be read / set: drop it, the rest still works
        int k = 0;
        for (int i = 0; i < H.nTw; ++i) if (H.tw[i].type != 1) H.tw[k++] = H.tw[i];
        scaleNote = std::string("BallControl._grabVolume left alone: neither the game nor the engine has Transform.get_localScale / set_localScale for me (") + (kTransform ? "class found" : "no Transform class") + ")";
        H.nTw = k;
    }
    auto yn = [](bool b) { return b ? "yes" : "NO"; };
    auto have = [](void* m, void* i) { return m ? "game call" : (i ? "engine call" : "NO"); };
    H.callList = fmt("Box size read %s / set %s, Sphere radius read %s / set %s, Capsule radius read %s / set %s, Capsule height read %s / set %s, Object.get_name %s, MeshCollider class %s",
                     kBox ? have(H.gBox, H.iGBox) : "no class", kBox ? have(H.sBox, H.iSBox) : "no class", kSph ? have(H.gSph, H.iGSph) : "no class", kSph ? have(H.sSph, H.iSSph) : "no class",
                     kCap ? have(H.gCapR, H.iGCapR) : "no class", kCap ? have(H.sCapR, H.iSCapR) : "no class", kCap ? have(H.gCapH, H.iGCapH) : "no class", kCap ? have(H.sCapH, H.iSCapH) : "no class",
                     yn(H.mName != nullptr), yn(kMesh != nullptr));
    const bool boxOk = kBox && (H.gBox || H.iGBox) && (H.sBox || H.iSBox);
    const bool sphOk = kSph && (H.gSph || H.iGSph) && (H.sSph || H.iSSph);
    const bool capOk = kCap && (H.gCapR || H.iGCapR) && (H.sCapR || H.iSCapR) && (H.gCapH || H.iGCapH) && (H.sCapH || H.iSCapH);
    H.resizeOk = boxOk || sphOk || capOk;

    if (!H.resizeOk && H.nTw == 0)
        return fail("neither the hitbox sizes can be changed (" + H.callList + ") nor does this game have any grab-reach value I know (" + twMissing + ")");
    H.ok = true;
    {
        std::lock_guard<std::mutex> lock(mu_);
        H_ = H;
        hbBcBuf_.assign(static_cast<size_t>(H.bc.size), 0);
        hbHandBuf_.assign(static_cast<size_t>(H.hand.size), 0);
    }
    std::string twList;
    for (int i = 0; i < H.nTw; ++i) twList += fmt("%s%s@%d(%s)", twList.empty() ? "" : ", ", H.tw[i].name, H.tw[i].off, H.tw[i].owner == 0 ? "Hand" : "BallControl");
    sayHitbox("hitbox: found where your hands keep their hitboxes: ball control manager (size %d) _leftBallControl@%d / _rightBallControl@%d -> %s (size %d) _hand@%d -> %s (size %d) _handColliders@%d%s | %s",
              L_.bcm.size, H.cL, H.cR, H.bc.fullName.c_str(), H.bc.size, H.bcHand, H.hand.fullName.c_str(), H.hand.size, H.hColl, H.hAuth >= 0 ? fmt(" (+ _hasAuthority@%d for the report)", H.hAuth).c_str() : "",
              H.callList.c_str());
    sayHitbox("hitbox: GRAB-REACH values found (made bigger together with the hitboxes): %s | not in this game: %s%s%s | Transform scale call: %s",
              twList.empty() ? "none" : twList.c_str(), twMissing.empty() ? "none" : twMissing.c_str(), scaleNote.empty() ? "" : " | ", scaleNote.c_str(),
              (H.mTGetLS && H.mTSetLS) ? "game call" : ((H.iTGetLS && H.iTSetLS) ? "engine call" : "not needed / NO"));
    if (!H.resizeOk) sayHitbox("hitbox: WARNING: no kind of hitbox can be resized (%s) - only the grab-reach values can change", H.callList.c_str());
    return true;
}

// ---------------------------------------------------------------- game thread: small pieces
void AimLink::hbNoteErr(bool threw) {
    if (!threw) { g_.hbErrRun = 0; return; }
    if (++g_.hbErrRun >= 12) hbDie("the game's engine refused 12 hitbox calls in a row (reading or setting a hand hitbox does not work from here)");
}

std::string AimLink::hbNameOf(uintptr_t obj) {
    if (!H_.mName || !plausiblePtr(obj)) return "";
    const unsigned long long e0 = errors_.load();
    const uintptr_t str = callObj(H_.mName, obj);
    g_.excRun = 0;
    if (errors_.load() != e0 || !plausiblePtr(str)) return "";
    unsigned char h[24];
    if (!pipeGame_.copy(str, h, sizeof h)) return "";
    int len; std::memcpy(&len, h + 16, 4);
    if (len < 0 || len > 400) return "";
    const int n = std::min(len, 32);
    std::vector<unsigned char> ch(static_cast<size_t>(n) * 2 + 2);
    if (n > 0 && !pipeGame_.copy(str + 20, ch.data(), static_cast<size_t>(n) * 2)) return "";
    std::string r;
    for (int i = 0; i < n; ++i) { const unsigned c = ch[static_cast<size_t>(2 * i)] | (static_cast<unsigned>(ch[static_cast<size_t>(2 * i + 1)]) << 8); r += (c >= 32 && c < 127) ? static_cast<char>(c) : '?'; }
    if (len > n) r += "...";
    return r;
}

// Your two BallControl objects, their hands and the hands' hitbox lists. True when at least one hand was found.
bool AimLink::hbFindHands(uintptr_t bc[2], uintptr_t hand[2], uintptr_t arr[2], int* auth) {
    bc[0] = bc[1] = hand[0] = hand[1] = arr[0] = arr[1] = 0; auth[0] = auth[1] = -1;
    const unsigned char* b = bcmBuf_.data();
    const int offs[2] = {H_.cL, H_.cR};
    int found = 0;
    for (int i = 0; i < 2; ++i) {
        const uintptr_t p = rdP(b, offs[i]);
        if (!plausiblePtr(p)) continue;
        if (!readObj(hbBcBuf_.data(), p, hbBcBuf_.size()) || !liveObj(hbBcBuf_.data(), H_.bc.klassInv, H_.bc.unityObject)) continue;
        const uintptr_t h = rdP(hbBcBuf_.data(), H_.bcHand);
        if (!plausiblePtr(h)) continue;
        if (!readObj(hbHandBuf_.data(), h, hbHandBuf_.size()) || !liveObj(hbHandBuf_.data(), H_.hand.klassInv, H_.hand.unityObject)) continue;
        bc[i] = p; hand[i] = h; arr[i] = rdP(hbHandBuf_.data(), H_.hColl);
        if (H_.hAuth >= 0) auth[i] = hbHandBuf_[static_cast<size_t>(H_.hAuth)] ? 1 : 0;
        ++found;
    }
    return found > 0;
}

// The hitbox's current size. Box: x y z; sphere: radius; capsule: radius, height. The engine's own call (or, when the game's code lacks it, the engine's internal one).
bool AimLink::hbGet(const HbCol& c, float v[3]) {
    v[0] = v[1] = v[2] = 0;
    const unsigned long long e0 = errors_.load();
    bool ok = false;
    auto getF = [&](void* m, void* icall, float* out) -> bool {
        if (m) { return callFloat(m, c.addr, out); }
        if (icall) { *out = reinterpret_cast<float (*)(void*)>(icall)(reinterpret_cast<void*>(c.addr)); return std::isfinite(*out); }
        return false;
    };
    if (c.kind == 1) {
        Vec3 s{0, 0, 0};
        if (H_.gBox) ok = callVec3(H_.gBox, c.addr, &s);
        else if (H_.iGBox) { reinterpret_cast<void (*)(void*, Vec3*)>(H_.iGBox)(reinterpret_cast<void*>(c.addr), &s); ok = std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z); }
        if (ok) { v[0] = s.x; v[1] = s.y; v[2] = s.z; }
    } else if (c.kind == 2) {
        ok = getF(H_.gSph, H_.iGSph, &v[0]);
    } else if (c.kind == 3) {
        ok = getF(H_.gCapR, H_.iGCapR, &v[0]) && getF(H_.gCapH, H_.iGCapH, &v[1]);
    }
    g_.excRun = 0;
    hbNoteErr(errors_.load() != e0);
    return ok;
}

bool AimLink::hbSet(const HbCol& c, const float v[3]) {
    for (int i = 0; i < kindComps(c.kind); ++i) if (!std::isfinite(v[i]) || v[i] <= 0.0f || v[i] > kMaxWorldSize) return false;
    const unsigned long long e0 = errors_.load();
    bool ok = false;
    auto setF = [&](void* m, void* icall, float val) -> bool {
        if (m) { float t = val; void* args[1] = {&t}; bool threw; invoke(m, c.addr, args, &threw); return !threw; }
        if (icall) { reinterpret_cast<void (*)(void*, float)>(icall)(reinterpret_cast<void*>(c.addr), val); return true; }
        return false;
    };
    if (c.kind == 1) {
        Vec3 s{v[0], v[1], v[2]};
        if (H_.sBox) { void* args[1] = {&s}; bool threw; invoke(H_.sBox, c.addr, args, &threw); ok = !threw; }
        else if (H_.iSBox) { reinterpret_cast<void (*)(void*, Vec3*)>(H_.iSBox)(reinterpret_cast<void*>(c.addr), &s); ok = true; }
    } else if (c.kind == 2) {
        ok = setF(H_.sSph, H_.iSSph, v[0]);
    } else if (c.kind == 3) {
        ok = setF(H_.sCapR, H_.iSCapR, v[0]) && setF(H_.sCapH, H_.iSCapH, v[1]);
    }
    g_.excRun = 0;
    hbNoteErr(errors_.load() != e0);
    return ok;
}

// ---------------------------------------------------------------- game thread: the grab-reach values
// A float lives inside the Hand / BallControl object itself (read and written as 4 bytes, checked by the system); a Transform is another engine object (its
// local scale is read and set with the engine's own calls).
bool AimLink::hbTwRead(int t, const HbTw& r, float v[3]) {
    v[0] = v[1] = v[2] = 0;
    const HitboxLayout::Tweak& T = H_.tw[t];
    if (T.type == 0) {
        unsigned char b[4];
        if (!pipeGame_.copy(r.owner + static_cast<uintptr_t>(T.off), b, 4)) return false;
        std::memcpy(&v[0], b, 4);
        return std::isfinite(v[0]);
    }
    const unsigned long long e0 = errors_.load();
    Vec3 s{0, 0, 0};
    bool ok = false;
    if (H_.mTGetLS) ok = callVec3(H_.mTGetLS, r.obj, &s);
    else if (H_.iTGetLS) { reinterpret_cast<void (*)(void*, Vec3*)>(H_.iTGetLS)(reinterpret_cast<void*>(r.obj), &s); ok = std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z); }
    g_.excRun = 0;
    hbNoteErr(errors_.load() != e0);
    if (ok) { v[0] = s.x; v[1] = s.y; v[2] = s.z; }
    return ok;
}

bool AimLink::hbTwWrite(int t, const HbTw& r, const float v[3]) {
    const HitboxLayout::Tweak& T = H_.tw[t];
    const int n = T.type == 0 ? 1 : 3;
    for (int i = 0; i < n; ++i) if (!std::isfinite(v[i]) || v[i] <= 0.0f || v[i] > kMaxGrabValue) return false;
    if (T.type == 0) {
        unsigned char b[4], back[4];
        std::memcpy(b, &v[0], 4);
        if (!pipeGame_.put(r.owner + static_cast<uintptr_t>(T.off), b, 4)) return false;
        return pipeGame_.copy(r.owner + static_cast<uintptr_t>(T.off), back, 4) && std::memcmp(b, back, 4) == 0;
    }
    const unsigned long long e0 = errors_.load();
    Vec3 s{v[0], v[1], v[2]};
    bool ok = false;
    if (H_.mTSetLS) ok = callSetVec3(H_.mTSetLS, r.obj, s);
    else if (H_.iTSetLS) { reinterpret_cast<void (*)(void*, Vec3*)>(H_.iTSetLS)(reinterpret_cast<void*>(r.obj), &s); ok = true; }
    g_.excRun = 0;
    hbNoteErr(errors_.load() != e0);
    return ok;
}

// Are the owner (Hand / BallControl) and, for a Transform, the Transform itself still the running objects we saw?
bool AimLink::hbTwAlive(int t, const HbTw& r) {
    const HitboxLayout::Tweak& T = H_.tw[t];
    if (!r.owner) return false;
    unsigned char* buf = T.owner == 0 ? hbHandBuf_.data() : hbBcBuf_.data();
    const size_t sz = T.owner == 0 ? hbHandBuf_.size() : hbBcBuf_.size();
    const tzscan::ClassInfo& ci = T.owner == 0 ? H_.hand : H_.bc;
    if (!readObj(buf, r.owner, sz) || !liveObj(buf, ci.klassInv, ci.unityObject)) return false;
    if (T.type == 1) {
        unsigned char head[24];
        if (!plausiblePtr(r.obj) || !pipeGame_.copy(r.obj, head, sizeof head)) return false;
        if (rdQ(head, 0) != r.klass || rdQ(head, 16) == 0) return false;
        uintptr_t cur;
        if (!pipeGame_.copy(r.owner + static_cast<uintptr_t>(T.off), reinterpret_cast<unsigned char*>(&cur), 8) || cur != r.obj) return false;       // the field points somewhere else now
    }
    return true;
}

bool AimLink::hbTwPutBack(int t, HbTw& r) {
    if (!r.applied) return false;
    r.applied = false;
    const int n = H_.tw[t].type == 0 ? 1 : 3;
    if (!hbTwAlive(t, r)) return false;
    float cur[3];
    if (!hbTwRead(t, r, cur) || !nearV(cur, r.last, n)) return false;                // the game changed it since: it is the game's now
    if (!hbTwWrite(t, r, r.orig)) return false;
    std::memcpy(r.last, r.orig, sizeof r.last);
    hbTwRestored_.fetch_add(1);
    return true;
}

void AimLink::hbTwReleaseAll(int* back) {
    int n = 0;
    for (int i = 0; i < 2; ++i) for (int t = 0; t < H_.nTw; ++t) {
        if (hbTwPutBack(t, g_.hbTw[i][t])) ++n;
        g_.hbTw[i][t] = HbTw();
    }
    g_.hbTwAppliedX10 = 0; g_.hbTwWriteFailRun = 0;
    hbTwUsable_.store(0);
    if (back) *back = n;
}

// Read, check and set the grab-reach values of both hands. `bc` / `hand` were found (and checked alive) a moment ago by hbFindHands; 0 = that hand is not there.
void AimLink::hbTwTick(const uintptr_t bc[2], const uintptr_t hand[2], int x10, float mul, int* setNow, int* failedNow) {
    *setNow = *failedNow = 0;
    int usable = 0, resetN = 0, newOwnN = 0;
    std::string example, first[2];
    bool anyFirst = false;
    for (int i = 0; i < 2; ++i) for (int t = 0; t < H_.nTw; ++t) {
        HbTw& r = g_.hbTw[i][t];
        const HitboxLayout::Tweak& T = H_.tw[t];
        const bool isF = T.type == 0;
        const int n = isF ? 1 : 3;
        const uintptr_t owner = T.owner == 0 ? hand[i] : bc[i];
        if (r.owner && r.owner != owner) { hbTwPutBack(t, r); r = HbTw(); }                  // the hand is gone or is another object: what we changed went with it (or is put back if it still lives)
        if (!owner) continue;
        if (!r.owner) r.owner = owner;
        if (r.dead) continue;
        if (!isF) {                                                                          // the Transform the field points to
            uintptr_t p = 0;
            if (!pipeGame_.copy(owner + static_cast<uintptr_t>(T.off), reinterpret_cast<unsigned char*>(&p), 8)) { hbTwReadFails_.fetch_add(1); continue; }
            if (!plausiblePtr(p)) continue;                                                  // the field is empty right now: keep what we know (the game may fill it again)
            if (p != r.obj) {                                                                // the game gave the hand ANOTHER grab volume: it starts from its own scale
                if (r.obj) { hbTwPutBack(t, r); r.haveOrig = false; r.applied = false; r.fails = 0; }
                r.obj = 0; r.klass = 0;
                unsigned char head[24];
                if (!pipeGame_.copy(p, head, sizeof head) || rdQ(head, 16) == 0 || rdQ(head, 0) == 0) continue;     // not a running object: nothing to scale (yet)
                r.obj = p; r.klass = rdQ(head, 0);
            }
            if (!r.obj) continue;
            unsigned char head[24];
            if (!pipeGame_.copy(r.obj, head, sizeof head) || rdQ(head, 16) == 0) continue;      // the game destroyed it (the engine would throw): wait until the field points to another one
        }
        float cur[3];
        if (!hbTwRead(t, r, cur)) {
            hbTwReadFails_.fetch_add(1);
            if (++r.fails >= 3) { r.dead = true; if (g_.hbTwNotes < 16) { ++g_.hbTwNotes; queueHitbox(fmt("hitbox: grab-reach value %s (%s hand) cannot be read - left alone", T.name, i == 0 ? "left" : "right")); } }
            continue;
        }
        bool sane = true;
        for (int k = 0; k < n; ++k) if (!(cur[k] > 0.0f) || cur[k] > kMaxGrabValue) sane = false;
        if (!r.haveOrig) {
            if (!sane) {                                                                     // 0 stays 0 whatever it is multiplied by
                r.dead = true;
                if (g_.hbTwNotes < 16) { ++g_.hbTwNotes; queueHitbox(fmt("hitbox: grab-reach value %s (%s hand) is %.3f - multiplying that changes nothing, so it is left alone", T.name, i == 0 ? "left" : "right", static_cast<double>(cur[0]))); }
                continue;
            }
            std::memcpy(r.orig, cur, sizeof r.orig); std::memcpy(r.last, cur, sizeof r.last); r.haveOrig = true; r.fails = 0;
            first[i] += isF ? fmt("%s%s %.3f", first[i].empty() ? "" : ", ", T.name, static_cast<double>(cur[0]))
                            : fmt("%s%s scale (%.3f, %.3f, %.3f)", first[i].empty() ? "" : ", ", T.name, static_cast<double>(cur[0]), static_cast<double>(cur[1]), static_cast<double>(cur[2]));
            anyFirst = true;
        } else if (r.applied) {
            if (!nearV(cur, r.last, n)) {                                                    // the game changed it after we set it
                hbTwResets_.fetch_add(1); ++resetN;
                bool newOwn = false;
                for (int k = 0; k < n; ++k) if (!nearV(cur + k, r.last + k, 1) && !nearV(cur + k, r.orig + k, 1)) { r.orig[k] = cur[k]; newOwn = true; }      // a part the game changed to a NEW own value: scale from that one from now on (a part it did not touch stays ours)
                if (newOwn) ++newOwnN;
                r.applied = false;
                if (example.empty()) example = fmt("%s (%s hand) is now %.3f, I had set %.3f", T.name, i == 0 ? "left" : "right", static_cast<double>(cur[0]), static_cast<double>(r.last[0]));
            }
        } else {
            std::memcpy(r.orig, cur, sizeof r.orig); std::memcpy(r.last, cur, sizeof r.last);       // not changed by us: whatever the game has now is its own value
        }
        ++usable;
        if (x10 == 0) continue;
        float target[3] = {r.orig[0], r.orig[1], r.orig[2]};
        for (int k = 0; k < n; ++k) target[k] = r.orig[k] * mul;
        if (!r.applied && nearV(target, r.orig, n)) continue;                                 // 1.0x and nothing was changed: nothing to do
        if (r.applied && nearV(target, r.last, n) && nearV(cur, target, n)) continue;
        if (hbTwWrite(t, r, target)) {
            std::memcpy(r.last, target, sizeof r.last);
            r.applied = !nearV(target, r.orig, n);
            ++*setNow; g_.hbTwWriteFailRun = 0;
            if (r.applied) hbTwSet_.fetch_add(1); else hbTwRestored_.fetch_add(1);
        } else {
            ++*failedNow; hbTwWriteFails_.fetch_add(1);
            if (++r.fails >= 3) {
                r.dead = true;
                if (g_.hbTwNotes < 16) { ++g_.hbTwNotes; queueHitbox(fmt("hitbox: grab-reach value %s (%s hand) could not be set 3 times - left alone", T.name, i == 0 ? "left" : "right")); }
            }
            if (++g_.hbTwWriteFailRun >= 12) { hbDie("the system refused 12 grab-reach changes in a row, so nothing is changed"); return; }
        }
    }
    hbTwUsable_.store(usable);
    if (anyFirst && g_.hbTwNotes < 16) {
        ++g_.hbTwNotes;
        queueHitbox(fmt("hitbox: the game's own grab-reach values - left hand: %s | right hand: %s", first[0].empty() ? "(none)" : first[0].c_str(), first[1].empty() ? "(none)" : first[1].c_str()));
    }
    if (resetN && g_.hbTwNotes < 16 && g_.hbResetNotes < 6) {
        ++g_.hbTwNotes; ++g_.hbResetNotes;
        queueHitbox(fmt("hitbox: the game changed %d of your grab-reach values since I set them (%d back to its own value, %d to a NEW own value) - e.g. %s - I set mine again", resetN, resetN - newOwnN, newOwnN, example.c_str()));
    }
}

// Is this hitbox still a running engine object of the kind we think it is?
static bool hbAliveHead(const unsigned char* head, uint64_t klass) { return klass != 0 && rdQ(head, 0) == klass && rdQ(head, 16) != 0; }

// Give one hitbox the game's own size back - but only when it is still a running engine object AND still at the size we set (if the game changed it since, it is the game's now).
bool AimLink::hbPutBack(HbCol& c) {
    if (!c.applied) return false;
    c.applied = false;
    unsigned char head[24];
    if (!pipeGame_.copy(c.addr, head, sizeof head) || !hbAliveHead(head, c.kind == 1 ? H_.kBox : (c.kind == 2 ? H_.kSphere : H_.kCapsule))) return false;
    float cur[3];
    if (!hbGet(c, cur) || !nearV(cur, c.last, kindComps(c.kind))) return false;
    if (!hbSet(c, c.orig)) return false;
    std::memcpy(c.last, c.orig, sizeof c.last);
    hbRestored_.fetch_add(1);
    return true;
}

// ---------------------------------------------------------------- game thread: the list of hitboxes
bool AimLink::hbCensus(const uintptr_t arr[2], bool log) {
    hbCensuses_.fetch_add(1);
    std::vector<HbCol> fresh;
    int perHand[2] = {0, 0}, boxes = 0, spheres = 0, capsules = 0, meshes = 0, others = 0, total = 0;
    std::string seen;
    for (int h = 0; h < 2; ++h) {
        if (!plausiblePtr(arr[h])) continue;
        std::vector<uintptr_t> items; size_t len = 0;
        if (!readObjArray(arr[h], kMaxCols, &items, &len)) continue;
        if (len > kMaxCols) { if (g_.hbNotes < kNoteLimit) { ++g_.hbNotes; queueHitbox(fmt("hitbox: a hand lists %zu hitboxes - more than the %zu I handle, so that hand is left alone", len, kMaxCols)); } continue; }
        for (uintptr_t p : items) {
            bool dup = false;
            for (const HbCol& f : fresh) if (f.addr == p) { dup = true; break; }
            if (dup) continue;
            unsigned char head[24];
            if (!pipeGame_.copy(p, head, sizeof head)) continue;
            const uint64_t k = rdQ(head, 0);
            if (rdQ(head, 16) == 0) continue;                                     // destroyed by the game
            int kind = 0;
            if (H_.kBox && k == H_.kBox) kind = 1; else if (H_.kSphere && k == H_.kSphere) kind = 2; else if (H_.kCapsule && k == H_.kCapsule) kind = 3;
            ++total; ++perHand[h];
            const bool resizable = (kind == 1 && (H_.gBox || H_.iGBox) && (H_.sBox || H_.iSBox)) || (kind == 2 && (H_.gSph || H_.iGSph) && (H_.sSph || H_.iSSph)) ||
                                   (kind == 3 && (H_.gCapR || H_.iGCapR) && (H_.sCapR || H_.iSCapR) && (H_.gCapH || H_.iGCapH) && (H_.sCapH || H_.iSCapH));
            if (kind == 1) ++boxes; else if (kind == 2) ++spheres; else if (kind == 3) ++capsules;
            else if (H_.kMesh && k == H_.kMesh) ++meshes; else ++others;
            if (!resizable || fresh.size() >= kMaxCols) continue;
            HbCol rec; rec.addr = p; rec.kind = kind; rec.usable = true;
            for (const HbCol& old : g_.hbCols) if (old.addr == p) { rec = old; break; }       // keep what is known about a hitbox that was already in the list
            fresh.push_back(rec);
        }
    }
    g_.hbCols.swap(fresh);                       // (`fresh` now holds the OLD list)
    {
        int gone = 0;
        for (HbCol& o : fresh) {
            if (!o.applied) continue;
            bool still = false;
            for (const HbCol& n : g_.hbCols) if (n.addr == o.addr) { still = true; break; }
            if (!still && hbPutBack(o)) ++gone;
        }
        if (gone && g_.hbNotes < kNoteLimit) { ++g_.hbNotes; queueHitbox(fmt("hitbox: %d hitbox%s left the lists of your hands' hitboxes - put back to the game's own size", gone, gone == 1 ? "" : "es")); }
    }
    int usable = 0;
    for (const HbCol& c : g_.hbCols) if (c.usable) ++usable;
    hbBoxCount_.store(total); hbUsable_.store(usable); hbBoxes_.store(boxes); hbSpheres_.store(spheres); hbCapsules_.store(capsules); hbMeshes_.store(meshes); hbOthers_.store(others);
    if (log) {
        ++g_.hbCensusLogs;
        std::string list; int shown = 0;
        for (const HbCol& c : g_.hbCols) {
            if (++shown > 14) { list += " ..."; break; }
            float v[3]; const bool got = hbGet(c, v);
            list += fmt(" [%s '%s' %s]", kindName(c.kind), hbNameOf(c.addr).c_str(), got ? sizeText(c.kind, v).c_str() : "size unreadable");
        }
        queueHitbox(fmt("hitbox: your hands have %d hitbox%s (left hand %d, right hand %d): %d can be resized (box %d, sphere %d, capsule %d), mesh %d, other kinds %d.%s",
                        total, total == 1 ? "" : "es", perHand[0], perHand[1], usable, boxes, spheres, capsules, meshes, others, list.c_str()));
    }
    return true;
}

// ---------------------------------------------------------------- game thread: apply / restore / check
namespace {
void scaled(int kind, const float* orig, float mul, float* out) {
    out[0] = orig[0]; out[1] = orig[1]; out[2] = orig[2];
    for (int i = 0; i < kindComps(kind); ++i) out[i] = orig[i] * mul;
}
}  // namespace

void AimLink::hitboxTick(double now) {
    if (!hitboxReady_.load(std::memory_order_acquire)) return;
    if (hitboxDead_.load(std::memory_order_relaxed)) { if (g_.hbOn) hitboxStandDown("the hitbox part stopped"); return; }       // whatever was changed before it stopped is put back
    const int x10 = hitboxX10_.load(std::memory_order_relaxed);
    if (x10 == 0) { if (g_.hbOn) hitboxStandDown("the switch went off"); return; }
    if (now < g_.hbNext) return;
    g_.hbNext = now + kPace;
    g_.hbOn = true;
    if (x10 != g_.hbLastX10) { g_.hbLastX10 = x10; g_.hbChangedAt = now; }
    const float mul = static_cast<float>(x10) / 10.0f;

    // 1. your two hands
    uintptr_t bc[2], hand[2], arr[2]; int auth[2];
    if (!hbFindHands(bc, hand, arr, auth)) {
        if (g_.hbBc[0] || g_.hbBc[1]) {                          // the hands we knew are gone (new scene / new avatar): what we changed went with them
            for (HbCol& c : g_.hbCols) hbPutBack(c);           // (a hitbox the game already destroyed is skipped)
            hbTwReleaseAll(nullptr);                           // (so are grab-reach values of objects that are gone)
            g_.hbCols.clear(); g_.hbAppliedX10 = 0;
            g_.hbBc[0] = g_.hbBc[1] = g_.hbHand[0] = g_.hbHand[1] = g_.hbArr[0] = g_.hbArr[1] = 0;
            if (g_.hbNotes < kNoteLimit) { ++g_.hbNotes; queueHitbox("hitbox: your hands are gone (new scene or avatar?) - looking for them again"); }
        }
        hbHands_.store(0); hbBoxCount_.store(0); hbUsable_.store(0); hbTwUsable_.store(0);
        if (g_.hbInfoText != "looking for your hands ...") { g_.hbInfoText = "looking for your hands ..."; hbSetInfo(g_.hbInfoText); }
        return;
    }
    hbHands_.store((bc[0] ? 1 : 0) + (bc[1] ? 1 : 0));
    const bool changed = bc[0] != g_.hbBc[0] || bc[1] != g_.hbBc[1] || hand[0] != g_.hbHand[0] || hand[1] != g_.hbHand[1] || arr[0] != g_.hbArr[0] || arr[1] != g_.hbArr[1];
    bool verifyDue = false;
    if (changed || now >= g_.hbCensusAt) {
        const bool log = changed && g_.hbCensusLogs < 4;
        if (changed && g_.hbNotes < kNoteLimit) {
            ++g_.hbNotes;
            queueHitbox(fmt("hitbox: found your hands: left BallControl %llx hand %llx (authority %d), right BallControl %llx hand %llx (authority %d) - the game's own hitbox lists %llx / %llx",
                            static_cast<unsigned long long>(bc[0]), static_cast<unsigned long long>(hand[0]), auth[0], static_cast<unsigned long long>(bc[1]), static_cast<unsigned long long>(hand[1]), auth[1],
                            static_cast<unsigned long long>(arr[0]), static_cast<unsigned long long>(arr[1])));
        }
        hbCensus(arr, log);
        for (int i = 0; i < 2; ++i) { g_.hbBc[i] = bc[i]; g_.hbHand[i] = hand[i]; g_.hbArr[i] = arr[i]; }
        g_.hbCensusAt = now + kCensusEvery;
        verifyDue = !changed;
    }

    // 2. the sizes: check what the game did (every two seconds), then set / restore
    bool redo = false;
    if (verifyDue) {
        int resetN = 0, newOwnN = 0; std::string example;
        for (HbCol& c : g_.hbCols) {
            if (!c.usable || !c.applied || !c.haveOrig) continue;
            unsigned char head[24];
            if (!pipeGame_.copy(c.addr, head, sizeof head) || !hbAliveHead(head, c.kind == 1 ? H_.kBox : (c.kind == 2 ? H_.kSphere : H_.kCapsule))) continue;
            float cur[3];
            if (!hbGet(c, cur)) continue;
            const int n = kindComps(c.kind);
            if (nearV(cur, c.last, n)) continue;                                   // still ours
            hbGameResets_.fetch_add(1); redo = true; ++resetN;
            bool newOwn = false;
            for (int k = 0; k < n; ++k) if (!nearV(cur + k, c.last + k, 1) && !nearV(cur + k, c.orig + k, 1)) { c.orig[k] = cur[k]; newOwn = true; }      // the game gave this part a NEW own size: that is the one to scale from now on (a part it did not touch stays ours)
            if (newOwn) ++newOwnN;
            c.applied = false;
            if (example.empty()) example = fmt("'%s' is now %s, I had set %s", hbNameOf(c.addr).c_str(), sizeText(c.kind, cur).c_str(), sizeText(c.kind, c.last).c_str());
        }
        if (resetN && g_.hbResetNotes < 6 && g_.hbNotes < kNoteLimit) {                // one line per check, not one per hitbox
            ++g_.hbResetNotes; ++g_.hbNotes;
            queueHitbox(fmt("hitbox: the game changed %d of your hitboxes since I set them (%d back to its own size, %d to a NEW own size) - e.g. %s - I set mine again", resetN, resetN - newOwnN, newOwnN, example.c_str()));
        }
    }
    if (x10 != 0) {
        bool need = redo || x10 != g_.hbAppliedX10;
        if (!need) for (const HbCol& c : g_.hbCols) if (c.usable && (!c.haveOrig || (x10 != 10 && !c.applied))) { need = true; break; }
        if (need) {
            int changedNow = 0, failedNow = 0;
            for (HbCol& c : g_.hbCols) {
                if (!c.usable) continue;
                unsigned char head[24];
                if (!pipeGame_.copy(c.addr, head, sizeof head) || !hbAliveHead(head, c.kind == 1 ? H_.kBox : (c.kind == 2 ? H_.kSphere : H_.kCapsule))) continue;
                const int n = kindComps(c.kind);
                if (!c.haveOrig) {
                    float cur[3];
                    if (!hbGet(c, cur)) { hbReadFails_.fetch_add(1); if (++c.fails >= 3) c.usable = false; continue; }
                    std::memcpy(c.orig, cur, sizeof c.orig); std::memcpy(c.last, cur, sizeof c.last); c.haveOrig = true;
                }
                float target[3]; scaled(c.kind, c.orig, mul, target);
                if (!c.applied && nearV(target, c.orig, n)) continue;                // 1.0x and nothing was changed: nothing to do
                if (nearV(target, c.last, n) && c.applied) continue;                 // already at the asked size
                if (hbSet(c, target)) {
                    std::memcpy(c.last, target, sizeof c.last);
                    const bool back = nearV(target, c.orig, n);
                    c.applied = !back; ++changedNow; g_.hbWriteFailRun = 0;
                    if (back) hbRestored_.fetch_add(1); else hbResized_.fetch_add(1);
                } else {
                    ++failedNow; hbWriteFails_.fetch_add(1);
                    if (++c.fails >= 3) c.usable = false;
                    if (++g_.hbWriteFailRun >= 12) { hbDie("the engine refused 12 hitbox size changes in a row, so nothing is changed"); return; }
                }
            }
            g_.hbAppliedX10 = x10;
            int usable = 0; for (const HbCol& c : g_.hbCols) if (c.usable) ++usable;
            hbUsable_.store(usable);
            if (g_.hbNotes < kNoteLimit && (changedNow || failedNow)) {
                ++g_.hbNotes;
                queueHitbox(fmt("hitbox: set %d hitbox%s of your hands to %.1fx their own size%s", changedNow, changedNow == 1 ? "" : "es", static_cast<double>(mul), failedNow ? fmt(" (%d could not be set)", failedNow).c_str() : ""));
            }
        }
    }

    // 3. the grab-reach values (read and set every look: they are plain numbers, cheap to check)
    {
        int setNow = 0, failedNow = 0;
        hbTwTick(bc, hand, x10, mul, &setNow, &failedNow);
        if (hitboxDead_.load(std::memory_order_relaxed)) return;
        g_.hbTwAppliedX10 = x10;
        if (g_.hbTwNotes < 16 && (setNow || failedNow)) {
            ++g_.hbTwNotes;
            queueHitbox(fmt("hitbox: set %d grab-reach value%s of your hands to %.1fx their own value%s", setNow, setNow == 1 ? "" : "s", static_cast<double>(mul), failedNow ? fmt(" (%d could not be set)", failedNow).c_str() : ""));
        }
    }

    // 4. the menu's status text (only when it changed)
    std::string info;
    const int hands = (bc[0] ? 1 : 0) + (bc[1] ? 1 : 0);
    const int total = hbBoxCount_.load(), usable = hbUsable_.load(), grab = hbTwUsable_.load();
    if (total == 0 && grab == 0) info = fmt("found %d hand%s, but they have no hitboxes yet - waiting", hands, hands == 1 ? "" : "s");
    else if (usable == 0 && grab == 0) info = fmt("your hands have %d hitbox%s but none of them can be resized (box %d, sphere %d, capsule %d, mesh %d, other %d)", total, total == 1 ? "" : "es", hbBoxes_.load(), hbSpheres_.load(), hbCapsules_.load(), hbMeshes_.load(), hbOthers_.load());
    else {
        std::string what = fmt("%d hitbox%s", usable, usable == 1 ? "" : "es");
        what += grab > 0 ? fmt(" + %d grab value%s", grab, grab == 1 ? "" : "s") : std::string(" (no grab values found)");
        if (x10 == 10) info = fmt("connected: %d hand%s, %s at the normal size (1.0x)", hands, hands == 1 ? "" : "s", what.c_str());
        else info = fmt("connected: %d hand%s, %s made %.1fx bigger", hands, hands == 1 ? "" : "s", what.c_str(), static_cast<double>(mul));
    }
    if (info != g_.hbInfoText) { g_.hbInfoText = info; hbSetInfo(info); }
}

// The switch went off (or the link did): put every hitbox and grab-reach value we changed back to the game's own.
void AimLink::hitboxStandDown(const char* why) {
    int back = 0, grabBack = 0;
    for (HbCol& c : g_.hbCols) if (hbPutBack(c)) ++back;
    hbTwReleaseAll(&grabBack);
    queueHitbox(fmt("hitbox: switched off (%s) - %d hitbox%s and %d grab-reach value%s put back to the game's own", why, back, back == 1 ? "" : "es", grabBack, grabBack == 1 ? "" : "s"));
    g_.hbCols.clear(); g_.hbAppliedX10 = 0; g_.hbLastX10 = 0; g_.hbOn = false; g_.hbWriteFailRun = 0;
    g_.hbBc[0] = g_.hbBc[1] = g_.hbHand[0] = g_.hbHand[1] = g_.hbArr[0] = g_.hbArr[1] = 0;
    g_.hbInfoText.clear();
    hbHands_.store(0); hbBoxCount_.store(0); hbUsable_.store(0); hbTwUsable_.store(0);
    hbSetInfo("");
}

}  // namespace tzaimlink
