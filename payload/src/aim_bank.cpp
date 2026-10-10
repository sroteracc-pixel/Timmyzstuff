// aim_bank.cpp - the part of the Aimbot link that reads the BACKBOARD, the ball and the bounce from the game for Bank mode (stage D9). See aim_link.h and bank.h.
//
// Nothing here is guessed: every number comes from the game (its own goal objects, its physics colliders and physic materials, its physics settings).
// When one cannot be read, the answer is "Bank unavailable: <the reason>" and the throw is left exactly as the player threw it.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "aim_link.h"

namespace tzaimlink {

namespace {

using tzaim::Vec3;

float rdF(const unsigned char* b, int off) { float f; std::memcpy(&f, b + off, 4); return f; }
int rdI(const unsigned char* b, int off) { int v; std::memcpy(&v, b + off, 4); return v; }
uint64_t rdQ(const unsigned char* b, int off) { uint64_t v; std::memcpy(&v, b + off, 8); return v; }
uintptr_t rdP(const unsigned char* b, int off) { return static_cast<uintptr_t>(rdQ(b, off)); }
Vec3 rdV(const unsigned char* b, int off) { return Vec3{rdF(b, off), rdF(b, off + 4), rdF(b, off + 8)}; }
bool plausiblePtr(uintptr_t p) { return p > 0x10000 && (p & 7) == 0 && p < 0x0000800000000000ULL; }
bool finiteV(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float lenV(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 subV(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
float dotV(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
std::string fv(const Vec3& v) { char b[80]; std::snprintf(b, sizeof b, "(%.3f, %.3f, %.3f)", static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)); return b; }
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) { char b[3000]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }

// the engine's own defaults for a collider that has no physic material (Unity's built-in "Default" material)
const float kDefaultDynamic = 0.6f, kDefaultStatic = 0.6f, kDefaultBounce = 0.0f;

// A required field: right name, right type, inside the object.
bool needField(const tzscan::ClassInfo& ci, const char* name, const char* type, int bytes, bool required, int* out, std::string* why) {
    const tzscan::FieldInfo* fi = ci.field(name);
    if (!fi) { if (required) *why = std::string("the field ") + name + " is missing in " + ci.fullName + " (the game was updated?)"; return !required; }
    if (type && fi->typeName != type) { if (required) *why = std::string("the field ") + name + " in " + ci.fullName + " has type " + fi->typeName + ", expected " + type; return !required; }
    if (fi->offset + bytes > ci.size) { if (required) *why = std::string("the field ") + name + " lies outside " + ci.fullName; return !required; }
    *out = fi->offset;
    return true;
}

// The names of an enum's values in declaration order (their numbers count up from 0 in that order). Empty when the class cannot be read.
std::vector<std::string> enumNames(const tzscan::Api& api, const char* ns, const char* name) {
    std::vector<std::string> r;
    std::string err;
    void* k = tzscan::findClassHandle(api, ns, name, &err);
    if (!k || !api.class_get_fields || !api.field_get_name) return r;
    void* it = nullptr;
    while (void* f = api.class_get_fields(k, &it)) {
        const char* fn = api.field_get_name(f);
        if (!fn || std::strcmp(fn, "value__") == 0) continue;
        r.push_back(fn);
    }
    return r;
}

// The method with this name, this many arguments and this type for its first argument ("System.Type"), looked up in the class and its parents.
void* methodWithParam(const tzscan::Api& api, void* klass, const char* name, unsigned nparams, const char* firstParamType) {
    if (!api.class_get_methods || !api.method_get_name || !api.method_get_param_count || !api.method_get_param || !api.type_get_name) return nullptr;
    for (void* k = klass; k; k = api.class_get_parent ? api.class_get_parent(k) : nullptr) {
        void* it = nullptr;
        while (void* m = api.class_get_methods(k, &it)) {
            const char* mn = api.method_get_name(m);
            if (!mn || std::strcmp(mn, name) != 0 || api.method_get_param_count(m) != nparams) continue;
            void* pt = api.method_get_param(m, 0);
            if (!pt) continue;
            char* tn = api.type_get_name(pt);
            const bool ok = tn && std::strcmp(tn, firstParamType) == 0;
            if (tn && api.il2cpp_free) api.il2cpp_free(tn);
            if (ok) return m;
        }
    }
    return nullptr;
}

// Unity's priority when two materials disagree: Average < Minimum < Multiply < Maximum. `names` = the enum's value names in order (so the numbers are looked up, not remembered).
int combinePriority(const std::vector<std::string>& names, int value) {
    if (value < 0 || value >= static_cast<int>(names.size())) return -1;
    const std::string& n = names[static_cast<size_t>(value)];
    if (n == "Average") return 0;
    if (n == "Minimum") return 1;
    if (n == "Multiply") return 2;
    if (n == "Maximum") return 3;
    return -1;
}
float combineWith(int priority, float a, float b) {
    switch (priority) {
    case 0: return 0.5f * (a + b);
    case 1: return std::min(a, b);
    case 2: return a * b;
    default: return std::max(a, b);
    }
}
const char* priorityName(int p) { return p == 0 ? "average" : (p == 1 ? "minimum" : (p == 2 ? "multiply" : "maximum")); }

}  // namespace

// ---------------------------------------------------------------- calls into the game (game thread)
bool AimLink::callInt(void* method, uintptr_t self, int* out) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    if (threw || !r) return false;
    unsigned char b[4];
    if (!pipeGame_.copy(reinterpret_cast<uintptr_t>(r) + 16, b, 4)) return false;
    *out = rdI(b, 0);
    return true;
}

uintptr_t AimLink::callObjArg(void* method, uintptr_t self, void* arg0) {
    void* args[1] = {arg0};
    bool threw; void* r = invoke(method, self, args, &threw);
    return threw ? 0 : reinterpret_cast<uintptr_t>(r);
}

bool AimLink::callBounds(void* method, uintptr_t self, Vec3* centre, Vec3* extents) {
    bool threw; void* r = invoke(method, self, nullptr, &threw);
    if (threw || !r) return false;
    unsigned char b[24];
    if (!pipeGame_.copy(reinterpret_cast<uintptr_t>(r) + 16, b, 24)) return false;
    *centre = rdV(b, 0); *extents = rdV(b, 12);
    return finiteV(*centre) && finiteV(*extents);
}

// ---------------------------------------------------------------- finding the game's backboard code (link thread, once)
bool AimLink::resolveBankLayout(std::string* why, bool* transient) {
    *transient = false;
    BankLayout B;
    const tzscan::Api& api = L_.api;
    auto fail = [&](const std::string& w, bool tr = false) { *why = w; *transient = tr; return false; };
    auto classOf = [&](const char* ns, const char* name, tzscan::ClassInfo* out) -> bool {
        *out = tzscan::findClass(api, ns, name);
        if (!out->found) { *why = out->error; *transient = out->error.find("not ready") != std::string::npos; return false; }
        return true;
    };
    if (!classOf("ShovelTools", "BasketballProperties", &B.props)) return false;
    if (!classOf("ShovelTools", "BasketballGoal", &B.goal)) return false;
    if (!classOf("ShovelTools", "BasketballGoalManager", &B.gman)) return false;
    std::string w;
    if (!needField(L_.ball, "_properties", "ShovelTools.BasketballProperties", 8, true, &B.bProps, &w)) return fail(w);
    needField(L_.ball, "_assistInGoal", "ShovelTools.BasketballGoal", 8, false, &B.bAssistGoal, &w);
    needField(L_.ball, "_lastBankAssistInTime", "System.Single", 4, false, &B.bAssistTime, &w);
    needField(L_.ball, "_originalAngularDrag", "System.Single", 4, false, &B.bAngDrag, &w);
    if (!needField(B.props, "_basketballCollider", "UnityEngine.SphereCollider", 8, true, &B.pCollider, &w)) return fail(w);
    needField(B.props, "_basketballPhysicMaterial", "UnityEngine.PhysicMaterial", 8, false, &B.pBaseMat, &w);
    needField(B.props, "_runtimePhysicMaterial", "UnityEngine.PhysicMaterial", 8, false, &B.pRuntimeMat, &w);
    needField(B.props, "_runtimeBounciness", "System.Single", 4, false, &B.pRtBounce, &w);
    needField(B.props, "_runtimeStaticFriction", "System.Single", 4, false, &B.pRtStatic, &w);
    needField(B.props, "_runtimeDynamicFriction", "System.Single", 4, false, &B.pRtDyn, &w);
    if (!needField(B.goal, "_backboardCenter", "UnityEngine.Transform", 8, true, &B.gBoardT, &w)) return fail(w);
    needField(B.goal, "_rimCenter", "UnityEngine.Transform", 8, false, &B.gRimT, &w);
    if (!needField(B.goal, "_backboardNormal", "UnityEngine.Vector3", 12, true, &B.gNormal, &w)) return fail(w);
    if (!needField(B.goal, "_backboardSize", "UnityEngine.Vector3", 12, true, &B.gSize, &w)) return fail(w);
    if (!needField(B.goal, "_backboardOffset", "UnityEngine.Vector3", 12, true, &B.gOffset, &w)) return fail(w);
    if (!needField(B.goal, "_rimRadius", "System.Single", 4, true, &B.gRimRadius, &w)) return fail(w);
    if (!needField(B.gman, "_goals", "System.Collections.Generic.List<ShovelTools.BasketballGoal>", 8, true, &B.mgGoals, &w)) return fail(w);

    B.mGoalRim = api.class_get_method_from_name(B.goal.klass(), "GetRimCenter", 0);
    B.mGoalBoard = api.class_get_method_from_name(B.goal.klass(), "GetBackboardCenterPosition", 0);
    B.mGmanInstance = api.class_get_method_from_name(B.gman.klass(), "get_Instance", 0);
    if (!B.mGoalRim || !B.mGoalBoard || !B.mGmanInstance)
        return fail(std::string("the game's goal code lacks ") + (B.mGoalRim ? "" : "BasketballGoal.GetRimCenter ") + (B.mGoalBoard ? "" : "BasketballGoal.GetBackboardCenterPosition ") + (B.mGmanInstance ? "" : "BasketballGoalManager.get_Instance"));

    // the engine's own classes
    std::string err;
    auto cls = [&](const char* name) -> void* { return tzscan::findClassHandle(api, "UnityEngine", name, &err); };
    void* kTransform = cls("Transform"); void* kComponent = cls("Component"); void* kCollider = cls("Collider"); void* kSphere = cls("SphereCollider");
    void* kMat = cls("PhysicMaterial"); void* kRb = L_.rbKlass; void* kPhys = cls("Physics");
    if (!kTransform || !kComponent || !kCollider || !kSphere || !kMat) return fail("the engine classes Transform / Component / Collider / SphereCollider / PhysicMaterial were not found: " + err);
    auto m0 = [&](void* k, const char* name, int argc = 0) -> void* { return k ? api.class_get_method_from_name(k, name, argc) : nullptr; };
    B.mTPos = m0(kTransform, "get_position"); B.mTScale = m0(kTransform, "get_lossyScale");
    B.mTChildCount = m0(kTransform, "get_childCount"); B.mTGetChild = m0(kTransform, "GetChild", 1);
    B.mCTransform = m0(kComponent, "get_transform");
    B.mCBounds = m0(kCollider, "get_bounds"); B.mCMaterial = m0(kCollider, "get_sharedMaterial"); B.mSRadius = m0(kSphere, "get_radius");
    B.mMBounce = m0(kMat, "get_bounciness"); B.mMDyn = m0(kMat, "get_dynamicFriction"); B.mMStat = m0(kMat, "get_staticFriction");
    B.mMBounceCombine = m0(kMat, "get_bounceCombine"); B.mMFrictionCombine = m0(kMat, "get_frictionCombine");
    B.mRAngVel = m0(kRb, "get_angularVelocity"); B.mRAngDrag = m0(kRb, "get_angularDrag"); B.mRMass = m0(kRb, "get_mass"); B.mRInertia = m0(kRb, "get_inertiaTensor");
    B.mRCcd = m0(kRb, "get_collisionDetectionMode"); B.mPBounceThr = m0(kPhys, "get_bounceThreshold");
    B.mGetComp = methodWithParam(api, kComponent, "GetComponent", 1, "System.Type");
    B.mGetCompKids = methodWithParam(api, kComponent, "GetComponentInChildren", 1, "System.Type");
    B.mGetCompParent = methodWithParam(api, kComponent, "GetComponentInParent", 1, "System.Type");
    B.klassCollider = kCollider;
    std::string lacks;
    auto need = [&](void* m, const char* n) { if (!m) lacks += std::string(n) + " "; };
    need(B.mTPos, "Transform.get_position"); need(B.mTScale, "Transform.get_lossyScale"); need(B.mCTransform, "Component.get_transform");
    need(B.mCBounds, "Collider.get_bounds"); need(B.mCMaterial, "Collider.get_sharedMaterial"); need(B.mSRadius, "SphereCollider.get_radius");
    need(B.mMBounce, "PhysicMaterial.get_bounciness"); need(B.mMDyn, "PhysicMaterial.get_dynamicFriction"); need(B.mMBounceCombine, "PhysicMaterial.get_bounceCombine"); need(B.mMFrictionCombine, "PhysicMaterial.get_frictionCombine");
    if (!B.mGetComp && !B.mGetCompKids && !B.mGetCompParent) lacks += "Component.GetComponent(Type) ";
    if (!api.class_get_type || !api.type_get_object) lacks += "il2cpp_class_get_type/il2cpp_type_get_object ";
    if (!lacks.empty()) return fail("the engine functions I need are missing: " + lacks);
    B.combineNames = enumNames(api, "UnityEngine", "PhysicMaterialCombine");
    B.ccdNames = enumNames(api, "UnityEngine", "CollisionDetectionMode");
    if (B.combineNames.empty()) return fail("the engine's PhysicMaterialCombine list could not be read, so the way the ball's and the board's bounce are mixed is unknown");

    B.ok = true;
    {
        std::lock_guard<std::mutex> lock(mu_);
        B_ = B;
    }
    std::string names;
    for (const std::string& n : B.combineNames) names += n + " ";
    char b[1200];
    std::snprintf(b, sizeof b, "aim: BANK: found the game's backboard code. Ball properties (size %d): collider@%d, runtime bounciness@%d | goal (size %d): rim radius@%d, backboard transform@%d, normal@%d, size@%d, offset@%d | goal manager (size %d): goal list@%d | extra engine calls: spin %s, spin drag %s, mass %s, inertia %s, collision mode %s, bounce threshold %s, child walk %s | ways to find the backboard's collider: GetComponent %s, in children %s, in parent %s | material mixing values: %s| collision modes: %zu known",
                  B.props.size, B.pCollider, B.pRtBounce, B.goal.size, B.gRimRadius, B.gBoardT, B.gNormal, B.gSize, B.gOffset, B.gman.size, B.mgGoals,
                  B.mRAngVel ? "yes" : "NO", B.mRAngDrag ? "yes" : "NO", B.mRMass ? "yes" : "NO", B.mRInertia ? "yes" : "NO", B.mRCcd ? "yes" : "NO", B.mPBounceThr ? "yes" : "NO",
                  (B.mTChildCount && B.mTGetChild) ? "yes" : "NO", B.mGetComp ? "yes" : "NO", B.mGetCompKids ? "yes" : "NO", B.mGetCompParent ? "yes" : "NO", names.c_str(), B.ccdNames.size());
    say("%s", b);
    return true;
}

// ---------------------------------------------------------------- the goals and their backboards (game thread)
// All goals the game knows (the managed list of BasketballGoal objects).
bool AimLink::bankListGoals(std::vector<uintptr_t>* out, std::string* why) {
    out->clear();
    const uintptr_t mgr = callObj(B_.mGmanInstance, 0);
    if (!plausiblePtr(mgr)) { *why = "the game has no goal manager right now"; return false; }
    std::vector<unsigned char> mb(static_cast<size_t>(B_.gman.size));
    if (!readObj(mb.data(), mgr, mb.size()) || !liveObj(mb.data(), B_.gman.klassInv, B_.gman.unityObject)) { *why = "the goal manager could not be read"; return false; }
    const uintptr_t list = rdP(mb.data(), B_.mgGoals);
    unsigned char lh[32];
    if (!plausiblePtr(list) || !pipeGame_.copy(list, lh, sizeof lh)) { *why = "the goal list could not be read"; return false; }
    const uintptr_t items = rdP(lh, 16);
    const int size = rdI(lh, 24);
    if (!plausiblePtr(items) || size < 1 || size > 16) { *why = fmt("the goal list looks wrong (%d goals)", size); return false; }
    std::vector<unsigned char> arr(32 + 8 * static_cast<size_t>(size));
    if (!pipeGame_.copy(items, arr.data(), arr.size()) || rdQ(arr.data(), 24) < static_cast<uint64_t>(size)) { *why = "the goal list's array could not be read"; return false; }
    for (int i = 0; i < size; ++i) { const uintptr_t g = rdP(arr.data(), 32 + 8 * i); if (plausiblePtr(g)) out->push_back(g); }
    if (out->empty()) { *why = "the goal list is empty"; return false; }
    return true;
}

// The goal whose ring is the hoop the player aims at (the ball's hoop position and the goal's own GetRimCenter agree).
bool AimLink::bankGoalFor(const Vec3& hoop, uintptr_t* goalOut, Vec3* ringOut, std::string* why) {
    std::vector<uintptr_t> goals;
    if (!bankListGoals(&goals, why)) return false;
    std::vector<unsigned char> gb(static_cast<size_t>(B_.goal.size));
    float best = 1e9f; uintptr_t bestGoal = 0; Vec3 bestRing; std::string seen;
    for (uintptr_t g : goals) {
        if (!readObj(gb.data(), g, gb.size()) || !liveObj(gb.data(), B_.goal.klassInv, B_.goal.unityObject)) continue;
        Vec3 rc;
        if (!callVec3(B_.mGoalRim, g, &rc)) continue;
        const float dh = std::sqrt((rc.x - hoop.x) * (rc.x - hoop.x) + (rc.z - hoop.z) * (rc.z - hoop.z)), dy = std::fabs(rc.y - hoop.y);
        seen += fmt(" [%s]", fv(rc).c_str());
        if (dh < 0.6f && dy < 0.5f && dh + dy < best) { best = dh + dy; bestGoal = g; bestRing = rc; }
    }
    if (!bestGoal) { *why = "no goal of the game has its ring at the hoop you are aiming at (hoop " + fv(hoop) + ", goal rings:" + (seen.empty() ? std::string(" none readable") : seen) + ")"; return false; }
    *goalOut = bestGoal; *ringOut = bestRing;
    return true;
}

// Looks for the collider that is the backboard: first on the backboard's own object, then through the goal's family of objects. A collider is the board when its
// bounding box has the size of the board (_backboardSize) and sits where the board should be. Returns 0 when none is.
uintptr_t AimLink::bankFindCollider(uintptr_t goal, uintptr_t boardT, const Vec3& expectCentre, const Vec3& size, bool* strict, Vec3* cOut, Vec3* eOut, std::string* how) {
    *strict = false;
    const tzscan::Api& api = L_.api;
    void* typeObj = nullptr;
    if (api.class_get_type && api.type_get_object) { void* ty = api.class_get_type(B_.klassCollider); if (ty) typeObj = api.type_get_object(ty); }
    if (!typeObj) { *how = "could not make the Collider type object"; return 0; }
    // the expected half sizes, big to small
    float want[3] = {size.x * 0.5f, size.y * 0.5f, size.z * 0.5f};
    std::sort(want, want + 3, [](float a, float b) { return a > b; });
    uintptr_t bestLoose = 0; float bestLooseD = 1e9f; Vec3 lc, le;
    int tested = 0, withCollider = 0;
    std::string seen;
    auto test = [&](uintptr_t tr, const char* where) -> uintptr_t {
        uintptr_t found = 0;
        for (void* m : {B_.mGetComp, B_.mGetCompKids, B_.mGetCompParent}) {
            if (!m) continue;
            const uintptr_t c = callObjArg(m, tr, typeObj);
            if (!plausiblePtr(c)) continue;
            unsigned char head[24];
            if (!pipeGame_.copy(c, head, sizeof head) || rdQ(head, 16) == 0) continue;       // a destroyed engine object
            found = c; break;
        }
        ++tested;
        if (!found) return 0;
        ++withCollider;
        Vec3 c, e;
        if (!callBounds(B_.mCBounds, found, &c, &e)) return 0;
        float have[3] = {e.x, e.y, e.z};
        std::sort(have, have + 3, [](float a, float b) { return a > b; });
        bool shapeOk = true;
        for (int i = 0; i < 3; ++i) if (std::fabs(have[i] - want[i]) > 0.35f * want[i] + 0.03f) shapeOk = false;
        const float dc = lenV(subV(c, expectCentre));
        if (seen.size() < 600) seen += fmt(" [%s: centre %s half sizes %s, %.2f m from the expected board centre%s]", where, fv(c).c_str(), fv(e).c_str(), static_cast<double>(dc), shapeOk ? ", same size as the board" : "");
        if (shapeOk && dc < 0.35f) { *strict = true; *cOut = c; *eOut = e; return found; }
        if (dc < 0.5f && dc < bestLooseD) { bestLooseD = dc; bestLoose = found; lc = c; le = e; }
        return 0;
    };
    // 1. the backboard's own object (and its children / parents)
    if (plausiblePtr(boardT)) { const uintptr_t r = test(boardT, "backboard object"); if (r) { *how = "collider on the backboard object (shape and place match the board)"; return r; } }
    // 2. the goal's family: climb up two levels, then walk down (breadth first)
    if (B_.mTChildCount && B_.mTGetChild) {
        uintptr_t root = callObj(B_.mCTransform, goal);
        if (plausiblePtr(root)) {
            std::vector<uintptr_t> queue{root};
            for (size_t qi = 0; qi < queue.size() && queue.size() < 300 && tested < 400; ++qi) {
                const uintptr_t tr = queue[qi];
                const uintptr_t r = test(tr, "goal family");
                if (r) { *how = "collider found while walking the goal's objects (shape and place match the board)"; return r; }
                int kids = 0;
                if (!callInt(B_.mTChildCount, tr, &kids) || kids < 0 || kids > 64) continue;
                for (int i = 0; i < kids && queue.size() < 300; ++i) {
                    int idx = i;
                    const uintptr_t ch = callObjArg(B_.mTGetChild, tr, &idx);
                    if (plausiblePtr(ch)) queue.push_back(ch);
                }
            }
        }
    }
    if (bestLoose) { *cOut = lc; *eOut = le; *how = fmt("collider near the board but not the board's size (%d objects tested, %d with a collider)", tested, withCollider); return bestLoose; }
    *how = fmt("no collider of the board's size near it (%d objects tested, %d with a collider):%s", tested, withCollider, seen.c_str());
    return 0;
}

// One physic material: bounciness, frictions and how they are mixed. A collider without a material uses the engine's default material.
bool AimLink::bankMaterial(uintptr_t collider, float* bounce, float* dyn, float* stat, int* bounceCombine, int* frictionCombine, std::string* note) {
    const uintptr_t mat = callObj(B_.mCMaterial, collider);
    if (!mat) {
        *bounce = kDefaultBounce; *dyn = kDefaultDynamic; *stat = kDefaultStatic; *bounceCombine = 0; *frictionCombine = 0;
        *note = "no material set (the engine's default material: bounce 0, friction 0.6, average)";
        return true;
    }
    unsigned char head[24];
    if (!plausiblePtr(mat) || !pipeGame_.copy(mat, head, sizeof head) || rdQ(head, 16) == 0) { *note = "its material object is gone"; return false; }
    float st = 0;
    if (!callFloat(B_.mMBounce, mat, bounce) || !callFloat(B_.mMDyn, mat, dyn)) { *note = "the material's bounciness / friction could not be read"; return false; }
    if (B_.mMStat && callFloat(B_.mMStat, mat, &st)) *stat = st; else *stat = *dyn;
    if (!callInt(B_.mMBounceCombine, mat, bounceCombine) || !callInt(B_.mMFrictionCombine, mat, frictionCombine)) { *note = "the material's mixing rule could not be read"; return false; }
    *note = fmt("material: bounce %.3f, dynamic friction %.3f, static friction %.3f, bounce rule #%d, friction rule #%d", static_cast<double>(*bounce), static_cast<double>(*dyn), static_cast<double>(*stat), *bounceCombine, *frictionCombine);
    return true;
}

// The measured backboard of one goal (cached). Geometry comes from the board's collider box when it matches the game's board data, else from the game's data fields.
bool AimLink::bankBoardFor(uintptr_t goal, const Vec3& ring, BoardInfo** out, std::string* why) {
    for (BoardInfo& b : g_.boards) if (b.goal == goal && b.built) {
        // still the same? (a destroyed / replaced collider must not be used)
        unsigned char head[24];
        Vec3 c, e;
        if (plausiblePtr(b.collider) && pipeGame_.copy(b.collider, head, sizeof head) && rdQ(head, 16) != 0 && callBounds(B_.mCBounds, b.collider, &c, &e) && lenV(subV(c, b.cBounds)) < 0.02f) { *out = &b; return true; }
        b.built = false;
    }
    std::vector<unsigned char> gb(static_cast<size_t>(B_.goal.size));
    if (!readObj(gb.data(), goal, gb.size()) || !liveObj(gb.data(), B_.goal.klassInv, B_.goal.unityObject)) { *why = "the goal object could not be read"; return false; }
    const Vec3 nG = rdV(gb.data(), B_.gNormal), size = rdV(gb.data(), B_.gSize), offset = rdV(gb.data(), B_.gOffset);
    const float rimRadius = rdF(gb.data(), B_.gRimRadius);
    const uintptr_t boardT = rdP(gb.data(), B_.gBoardT);
    if (!finiteV(nG) || !finiteV(size) || !finiteV(offset) || size.x < 0.3f || size.x > 4.0f || size.y < 0.3f || size.y > 4.0f || size.z < 0.001f || size.z > 0.5f) {
        *why = "the goal's backboard numbers are not believable: size " + fv(size) + ", offset " + fv(offset); return false;
    }
    Vec3 bpos;
    const bool haveBpos = callVec3(B_.mGoalBoard, goal, &bpos);
    Vec3 tpos; bool haveT = false;
    if (plausiblePtr(boardT)) haveT = callVec3(B_.mTPos, boardT, &tpos);
    // where the board should be: the game's own GetBackboardCenterPosition, else the backboard transform
    const Vec3 expect = haveBpos ? bpos : (haveT ? tpos : Vec3{ring.x, ring.y + 0.3f, ring.z});
    bool strict = false; Vec3 cc, ce; std::string how;
    const uintptr_t col = bankFindCollider(goal, boardT, expect, size, &strict, &cc, &ce, &how);
    if (!col) { *why = "could not find the backboard's collider: " + how; return false; }
    BoardInfo bi;
    bi.goal = goal; bi.collider = col; bi.cBounds = cc;
    std::string geo;
    Vec3 nFront; Vec3 bc; float halfW = size.x * 0.5f, halfH = size.y * 0.5f, thick = size.z;
    if (strict) {
        // the box: the thinnest horizontal direction is the board's normal
        const bool alongX = ce.x < ce.z;
        const float thin = alongX ? ce.x : ce.z;
        if (ce.y < thin || thin > 0.2f) { *why = "the board's collider box is not a thin upright plate: half sizes " + fv(ce); return false; }
        const float toRing = alongX ? (ring.x - cc.x) : (ring.z - cc.z);
        const float sgn = toRing >= 0 ? 1.0f : -1.0f;
        nFront = alongX ? Vec3{sgn, 0, 0} : Vec3{0, 0, sgn};
        bc = cc; thick = 2.0f * thin; halfH = ce.y; halfW = alongX ? ce.z : ce.x;
        geo = "from the collider box";
    } else {
        // from the game's data: the front direction points from the board towards the ring
        const Vec3 b = haveBpos ? bpos : (haveT ? tpos : expect);
        Vec3 h{ring.x - b.x, 0, ring.z - b.z};
        const float hl = lenV(h);
        if (hl < 0.05f) { *why = "the backboard centre and the ring are in the same place"; return false; }
        nFront = Vec3{h.x / hl, 0, h.z / hl};
        bc = b;
        geo = "from the game's backboard data (the collider found was not the board's size, it is used only for the material)";
    }
    // checks
    const Vec3 fc{bc.x + nFront.x * thick * 0.5f, bc.y, bc.z + nFront.z * thick * 0.5f};
    const float dist = dotV(subV(ring, fc), nFront), up = bc.y - ring.y;
    const float agree = std::fabs(dotV(nFront, nG)) / std::max(0.001f, lenV(nG));
    if (dist < 0.15f || dist > 0.75f) { *why = fmt("the ring is %.2f m in front of the board's face - not believable (expected 0.15 - 0.75). Board centre %s, ring %s, %s", static_cast<double>(dist), fv(bc).c_str(), fv(ring).c_str(), geo.c_str()); return false; }
    if (up < 0.05f || up > 0.8f) { *why = fmt("the board centre is %.2f m above the ring - not believable (expected 0.05 - 0.8)", static_cast<double>(up)); return false; }
    if (agree < 0.9f) { *why = fmt("the board's direction %s does not match the game's _backboardNormal %s", fv(nFront).c_str(), fv(nG).c_str()); return false; }
    bi.centre = fc; bi.n = nFront; bi.halfW = halfW; bi.halfH = halfH; bi.thick = thick;
    bi.how = fmt("%s. %s. Face centre %s, faces %s, half size %.3f x %.3f, thickness %.3f; ring %.3f m in front of the face, board centre %.3f m above the ring; the game's data: GetBackboardCenterPosition %s, backboard object %s, _backboardNormal %s (%s the court), _backboardOffset %s, _backboardSize %s, _rimRadius %.3f",
                 how.c_str(), geo.c_str(), fv(fc).c_str(), fv(nFront).c_str(), static_cast<double>(halfW), static_cast<double>(halfH), static_cast<double>(thick), static_cast<double>(dist), static_cast<double>(up),
                 haveBpos ? fv(bpos).c_str() : "(not readable)", haveT ? fv(tpos).c_str() : "(not readable)", fv(nG).c_str(), dotV(nG, nFront) > 0 ? "pointing TOWARDS" : "pointing AWAY from", fv(offset).c_str(), fv(size).c_str(), static_cast<double>(rimRadius));
    bi.built = true;
    g_.boards.push_back(bi);
    *out = &g_.boards.back();
    return true;
}

// Idle ticks: measure the backboards before the first shot, so a shot does not pay for the search.
void AimLink::bankPrepare(double now) {
    if (now < g_.nextBankPrep) return;
    g_.nextBankPrep = now + 3.0;
    if (g_.boards.size() >= 4) {
        bool all = true; for (const BoardInfo& b : g_.boards) if (!b.built) all = false;
        if (all) { g_.nextBankPrep = now + 20.0; return; }
    }
    std::vector<uintptr_t> goals; std::string why;
    if (!bankListGoals(&goals, &why)) { if (++g_.bankPrepNotes <= 4) queue("aim: BANK: looking for the goals: " + why); return; }
    std::vector<unsigned char> gb(static_cast<size_t>(B_.goal.size));
    for (uintptr_t g : goals) {
        bool have = false; for (const BoardInfo& b : g_.boards) if (b.goal == g && b.built) have = true;
        if (have) continue;
        Vec3 rc;
        if (!readObj(gb.data(), g, gb.size()) || !liveObj(gb.data(), B_.goal.klassInv, B_.goal.unityObject) || !callVec3(B_.mGoalRim, g, &rc)) continue;
        BoardInfo* bi = nullptr; std::string w;
        if (bankBoardFor(g, rc, &bi, &w)) { if (++g_.bankPrepNotes <= 6) queue(fmt("aim: BANK: measured the backboard of the goal with its ring at %s: ", fv(rc).c_str()) + bi->how); }
        else if (++g_.bankPrepNotes <= 6) queue(fmt("aim: BANK: could not measure the backboard of the goal with its ring at %s: ", fv(rc).c_str()) + w);
    }
}

void AimLink::bankUnavailable(Shot& s, const std::string& head, const std::string& reason, const std::string& detail) {
    bankUnavail_.fetch_add(1);
    s.bankOk = false; s.bankWhy = reason; s.why = "Bank unavailable: " + reason;
    queue(head + " | decision: LEFT ALONE - Bank unavailable: " + reason + (detail.empty() ? std::string() : " | " + detail) + " | the throw is left exactly as you threw it (no direct shot)");
    setLast(fmt("shot #%d: Bank unavailable - %s", s.id, reason.c_str()));
}

// ---------------------------------------------------------------- one bank shot (game thread, inside tickWait)
void AimLink::bankShot(Shot& s, const std::string& head, const std::string& hoopTxt, const unsigned char* bb, const Vec3& pos, const Vec3& vel, float elevDeg) {
    (void)hoopTxt; (void)elevDeg;
    if (!bankReady_.load(std::memory_order_acquire)) {
        std::string w; { std::lock_guard<std::mutex> lock(mu_); w = bankFailWhy_; }
        bankUnavailable(s, head, w.empty() ? "the game's backboard code is not ready yet" : w, "");
        return;
    }
    std::string why;
    // 1. which goal, which ring
    uintptr_t goal = 0; Vec3 ring;
    if (!bankGoalFor(s.hoopPos, &goal, &ring, &why)) { bankUnavailable(s, head, "I cannot find that hoop's goal", why); return; }
    s.ring = ring;
    BoardInfo* bi = nullptr;
    if (!bankBoardFor(goal, ring, &bi, &why)) { bankUnavailable(s, head, "I cannot measure the backboard", why); return; }
    // 2. the ball: radius from its collider, bounce from its material
    const uintptr_t props = rdP(bb, B_.bProps);
    std::vector<unsigned char> pb(static_cast<size_t>(B_.props.size));
    if (!plausiblePtr(props) || !readObj(pb.data(), props, pb.size()) || !liveObj(pb.data(), B_.props.klassInv, B_.props.unityObject)) { bankUnavailable(s, head, "I cannot read the ball's physics settings", "the ball has no readable BasketballProperties"); return; }
    const uintptr_t bcol = rdP(pb.data(), B_.pCollider);
    unsigned char ch[24];
    if (!plausiblePtr(bcol) || !pipeGame_.copy(bcol, ch, sizeof ch) || rdQ(ch, 16) == 0) { bankUnavailable(s, head, "I cannot read the ball's size", "the ball's collider is not linked or gone"); return; }
    float rad = 0;
    if (!callFloat(B_.mSRadius, bcol, &rad)) { bankUnavailable(s, head, "I cannot read the ball's size", "SphereCollider.radius failed"); return; }
    Vec3 scale{1, 1, 1};
    const uintptr_t btr = callObj(B_.mCTransform, bcol);
    if (!plausiblePtr(btr) || !callVec3(B_.mTScale, btr, &scale)) { bankUnavailable(s, head, "I cannot read the ball's size", "the collider's scale could not be read"); return; }
    const float smax = std::max(std::fabs(scale.x), std::max(std::fabs(scale.y), std::fabs(scale.z)));
    const float radius = rad * smax;
    float bB, bD, bS; int bBc, bFc; std::string noteBall, noteBoard;
    if (!bankMaterial(bcol, &bB, &bD, &bS, &bBc, &bFc, &noteBall)) { bankUnavailable(s, head, "I cannot read the ball's bounce", "ball " + noteBall); return; }
    float wB, wD, wS; int wBc, wFc;
    if (!bankMaterial(bi->collider, &wB, &wD, &wS, &wBc, &wFc, &noteBoard)) { bankUnavailable(s, head, "I cannot read the backboard's bounce", "board " + noteBoard); return; }
    // 3. mix them the way the engine does
    const int pB = std::max(combinePriority(B_.combineNames, bBc), combinePriority(B_.combineNames, wBc));
    const int pF = std::max(combinePriority(B_.combineNames, bFc), combinePriority(B_.combineNames, wFc));
    if (combinePriority(B_.combineNames, bBc) < 0 || combinePriority(B_.combineNames, wBc) < 0 || combinePriority(B_.combineNames, bFc) < 0 || combinePriority(B_.combineNames, wFc) < 0) {
        bankUnavailable(s, head, "I cannot tell how the bounce is mixed", fmt("unknown mixing rule numbers: ball %d/%d, board %d/%d", bBc, bFc, wBc, wFc)); return;
    }
    const float e = combineWith(pB, bB, wB), mu = combineWith(pF, bD, wD);
    // 4. the rest of the physics (each optional piece says when it fell back)
    std::string extra;
    float thr = 2.0f;
    if (B_.mPBounceThr && callFloat(B_.mPBounceThr, 0, &thr) && thr >= 0 && thr < 50) {} else { thr = 2.0f; extra += " [bounce threshold not read: the engine's default 2 m/s used]"; }
    Vec3 spin{0, 0, 0}; bool spinKnown = false;
    if (B_.mRAngVel && callVec3(B_.mRAngVel, s.rb, &spin) && lenV(spin) < 200.0f) spinKnown = true; else { spin = Vec3{0, 0, 0}; extra += " [spin not read: assumed none]"; }
    float angDrag = 0;
    if (B_.mRAngDrag && callFloat(B_.mRAngDrag, s.rb, &angDrag) && angDrag >= 0 && angDrag < 50) {}
    else if (B_.bAngDrag >= 0 && std::isfinite(rdF(bb, B_.bAngDrag)) && rdF(bb, B_.bAngDrag) >= 0) { angDrag = rdF(bb, B_.bAngDrag); extra += " [spin drag taken from _originalAngularDrag]"; }
    else extra += " [spin drag not read: none assumed]";
    float kappa = 0.4f; std::string kNote = " [spin inertia not read: a solid ball (0.4) assumed]";
    { float mass = 0; Vec3 it;
      if (B_.mRMass && B_.mRInertia && callFloat(B_.mRMass, s.rb, &mass) && callVec3(B_.mRInertia, s.rb, &it) && mass > 0.01f && mass < 20.0f && radius > 0.03f) {
          const float k = ((it.x + it.y + it.z) / 3.0f) / (mass * radius * radius);
          if (k > 0.2f && k < 0.75f) { kappa = k; kNote.clear(); } else kNote = fmt(" [inertia gave an odd spin factor %.2f: a solid ball (0.4) assumed]", static_cast<double>(k));
      } }
    extra += kNote;
    tzbank::ContactModel cm = tzbank::ContactModel::Unknown; std::string cmNote = "touch model: unknown (a shot must work with both)";
    { int mode = -1;
      if (B_.mRCcd && B_.ccdNames.size() > 0 && callInt(B_.mRCcd, s.rb, &mode) && mode >= 0 && mode < static_cast<int>(B_.ccdNames.size())) {
          const std::string& nm = B_.ccdNames[static_cast<size_t>(mode)];
          if (nm == "Discrete") { cm = tzbank::ContactModel::Discrete; cmNote = "collision mode: Discrete"; }
          else if (nm == "Continuous" || nm == "ContinuousDynamic") { cm = tzbank::ContactModel::Continuous; cmNote = "collision mode: " + nm; }
          else cmNote = "collision mode: " + nm + " (treated as unknown)";
      } }
    // the game's own ring size and the ball's hoop position must agree with the goal (otherwise something is not what I think it is)
    const float rimRadius = [&]() { std::vector<unsigned char> gb(static_cast<size_t>(B_.goal.size)); return readObj(gb.data(), goal, gb.size()) ? rdF(gb.data(), B_.gRimRadius) : 0.0f; }();

    tzbank::Request rq;
    rq.scene.flight.gravity = s.model.gravity; rq.scene.flight.drag = s.model.drag; rq.scene.flight.dt = s.model.dt;
    rq.scene.angularDrag = angDrag; rq.scene.radius = radius; rq.scene.kappa = kappa;
    rq.scene.faceCentre = bi->centre; rq.scene.n = bi->n; rq.scene.halfW = bi->halfW; rq.scene.halfH = bi->halfH;
    rq.scene.restitution = e; rq.scene.friction = mu; rq.scene.bounceThreshold = thr;
    rq.scene.ring = ring; rq.scene.ringRadius = rimRadius; rq.scene.ringTube = 0.0f;
    rq.scene.model = cm;
    rq.pos = pos; rq.vel = vel; rq.spin = spin;
    s.scene = rq.scene;
    const std::string inputs = fmt("aim: SHOT #%d bank inputs: ball radius %.4f m (collider %.4f x scale %.3f) | ball %s | board %s | mixed: bounce %.3f (%s of %.3f and %.3f), sliding friction %.3f (%s of %.3f and %.3f) | bounce threshold %.2f m/s | spin %s rad/s%s, spin drag %.3f, spin factor %.3f | %s | ring %s (the ball's own hoop position %s) ring radius %.4f | backboard: ",
                                   s.id, static_cast<double>(radius), static_cast<double>(rad), static_cast<double>(smax), noteBall.c_str(), noteBoard.c_str(), static_cast<double>(e), priorityName(pB), static_cast<double>(bB), static_cast<double>(wB),
                                   static_cast<double>(mu), priorityName(pF), static_cast<double>(bD), static_cast<double>(wD), static_cast<double>(thr), fv(spin).c_str(), spinKnown ? "" : " (assumed)", static_cast<double>(angDrag), static_cast<double>(kappa),
                                   cmNote.c_str(), fv(ring).c_str(), fv(s.hoopPos).c_str(), static_cast<double>(rimRadius)) + bi->how + extra;
    queue(inputs);
    // the game's own numbers for the bounce, as a cross-check (report only)
    if (B_.pRtBounce >= 0) queue(fmt("aim: SHOT #%d bank cross-check: the ball's own runtime settings say bounciness %.3f, static friction %.3f, dynamic friction %.3f (from BasketballProperties)", s.id,
                                      static_cast<double>(rdF(pb.data(), B_.pRtBounce)), B_.pRtStatic >= 0 ? static_cast<double>(rdF(pb.data(), B_.pRtStatic)) : -1.0, B_.pRtDyn >= 0 ? static_cast<double>(rdF(pb.data(), B_.pRtDyn)) : -1.0));
    if (B_.bAssistTime >= 0) s.assistTime0 = rdF(bb, B_.bAssistTime);
    if (B_.bAssistGoal >= 0) s.assistGoal0 = rdP(bb, B_.bAssistGoal);

    s.plan = tzbank::solve(rq);
    s.bankTried = true;
    if (!s.plan.ok) { bankUnavailable(s, head, s.plan.why, s.plan.detail); return; }
    s.bankOk = true;
    s.predStart = vel;
    {   // compare the real flight with the maths at a few steps before the touch and several right after it (that is where a wrong bounce shows)
        const int c = s.plan.sim.contactStep, x = s.plan.sim.crossStep;
        std::vector<int> st{3, 8, 15, 25, 40, 60, c - 3, c - 1, c + 1, c + 2, c + 4, c + 7, c + 11, x - 1};
        std::sort(st.begin(), st.end());
        for (int v : st) if (v >= 3 && v < x && (s.sampleAt.empty() || v > s.sampleAt.back())) s.sampleAt.push_back(v);
    }
    if (!cfg_.allowWrite) {
        s.why = "writing is switched off (test mode)";
        queue(head + " | decision: WOULD BANK (test mode, nothing written) | " + s.plan.detail);
        return;
    }
    const bool ok = callSetVec3(L_.mSetVel, s.rb, s.plan.vel);
    Vec3 rbk; s.haveReadback = ok && callVec3(L_.mGetVel, s.rb, &rbk); if (s.haveReadback) s.readback = rbk;
    if (!ok) {
        refused_.fetch_add(1); s.why = "setting the ball's speed raised an error in the game";
        queue(head + " | decision: LEFT ALONE - " + s.why);
        setLast(fmt("shot #%d: not aimed - the game refused the new speed", s.id));
        return;
    }
    s.applied = true; s.predStart = s.plan.vel; aimed_.fetch_add(1); bankAimed_.fetch_add(1);
    const bool kept = s.haveReadback && lenV(subV(s.readback, s.plan.vel)) < 0.05f;
    queue(head + " | decision: BANK SHOT | " + s.plan.detail + " | was " + fv(vel) + ", set " + fv(s.plan.vel) + ", read back " + (s.haveReadback ? fv(s.readback) : std::string("(failed)")) + (kept ? " = kept" : " = NOT the value we set") +
          fmt(" | predicted: touch after %d steps at %s, ring crossing after %d steps at %s", s.plan.sim.contactStep, fv(s.plan.sim.contactPos).c_str(), s.plan.sim.crossStep, fv(s.plan.sim.crossPos).c_str()));
    setLast(fmt("shot #%d: BANK from %.1f m - hits the board %.2f m off centre", s.id, static_cast<double>(s.plan.distToBoard), static_cast<double>(s.plan.contactA)));
}

// ---------------------------------------------------------------- watching a bank shot
// Every physics step the ball moved: how close it is to the board's front plane, and (around the touch) its speed and spin.
void AimLink::bankWatchStep(Shot& s, int stepNow, const Vec3& pos) {
    if (!s.bankTried || !s.bankOk) return;
    const Vec3 fc = s.scene.faceCentre, n = s.scene.n;
    const float b = dotV(subV(pos, fc), n);
    if (b < s.minB) { s.minB = b; s.minBStep = stepNow; s.minBPos = pos; }
    const int ps = s.plan.sim.contactStep;
    const bool near = (stepNow >= ps - 2 && stepNow <= ps + 6) || b - s.scene.radius < 0.25f;
    if (near && s.trace.size() < 16) {
        Shot::Trace t; t.step = stepNow; t.pos = pos; t.haveSpin = false;
        Vec3 v;
        if (callVec3(L_.mGetVel, s.rb, &v)) t.vel = v;
        Vec3 w;
        if (B_.mRAngVel && callVec3(B_.mRAngVel, s.rb, &w)) { t.spin = w; t.haveSpin = true; }
        s.trace.push_back(t);
    }
}

std::string AimLink::bankResultText(const Shot& s) const {
    if (!s.bankTried || !s.bankOk) return "";
    std::string r = fmt("aim: SHOT #%d bank check: ", s.id);
    const Vec3 fc = s.scene.faceCentre, n = s.scene.n;
    const bool touched = s.minB < 1e8f && s.minB - s.scene.radius < 0.06f;
    if (touched) r += fmt("the ball came closest to the board's front plane at step %d (centre %.3f m from the plane = %.3f m from the board's surface) at %s | predicted touch at step %d at %s%s | ",
                          s.minBStep, static_cast<double>(s.minB), static_cast<double>(s.minB - s.scene.radius), fv(s.minBPos).c_str(), s.plan.sim.contactStep, fv(s.plan.sim.contactPos).c_str(),
                          std::fabs(static_cast<double>(s.minBStep - s.plan.sim.contactStep)) <= 1.0 ? " (same step: the timing model holds)" : " (DIFFERENT step: the timing model is off)");
    else r += fmt("the ball NEVER came near the board (closest centre distance to the front plane %.3f m, ball radius %.3f m) | ", static_cast<double>(s.minB), static_cast<double>(s.scene.radius));
    // measured bounce: the speed along the board's normal just before and just after
    float vIn = 0, vOut = 0; bool haveIn = false, haveOut = false; int stepIn = 0, stepOut = 0; Vec3 tIn, tOut;
    for (size_t i = 0; i + 1 < s.trace.size(); ++i) {
        const float a = dotV(s.trace[i].vel, n), b = dotV(s.trace[i + 1].vel, n);
        if (a < -0.2f && b > 0.2f && !haveIn) { haveIn = haveOut = true; vIn = a; vOut = b; stepIn = s.trace[i].step; stepOut = s.trace[i + 1].step; tIn = s.trace[i].vel; tOut = s.trace[i + 1].vel; }
    }
    if (haveIn) {
        const Vec3 pIn{tIn.x - n.x * vIn, tIn.y - n.y * vIn, tIn.z - n.z * vIn}, pOut{tOut.x - n.x * vOut, tOut.y - n.y * vOut, tOut.z - n.z * vOut};
        r += fmt("MEASURED BOUNCE: speed into the board %.2f m/s (step %d) -> out %.2f m/s (step %d) = bounce %.2f (the plan used %.2f) | speed along the board %.2f -> %.2f m/s | predicted: into %.2f, out %.2f ",
                 static_cast<double>(-vIn), stepIn, static_cast<double>(vOut), stepOut, static_cast<double>(vOut / -vIn), static_cast<double>(s.scene.restitution), static_cast<double>(lenV(pIn)), static_cast<double>(lenV(pOut)),
                 static_cast<double>(s.plan.approach), static_cast<double>(s.scene.restitution * s.plan.approach));
    } else if (!s.trace.empty()) r += "no clear bounce seen in the speed readings | ";
    if (!s.trace.empty()) {
        r += "speed readings near the touch:";
        for (const Shot::Trace& t : s.trace) r += fmt(" [step %d: v %s%s]", t.step, fv(t.vel).c_str(), t.haveSpin ? (" spin " + fv(t.spin)).c_str() : "");
    }
    (void)fc;
    r += fmt(" | the game's own bank assist: _lastBankAssistInTime %.2f -> %.2f, _assistInGoal %s -> %s", static_cast<double>(s.assistTime0), static_cast<double>(s.assistTime1),
             s.assistGoal0 ? "set" : "none", s.assistGoal1 ? "set" : "none");
    return r;
}

}  // namespace tzaimlink
