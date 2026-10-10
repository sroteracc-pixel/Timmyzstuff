// A PRETEND libil2cpp.so + a pretend basketball world for the PC test of the aim link (stage D8).
// Class names, field names, types and positions are copied from the real stage D7c lobby facts file (ShovelTools.Basketball, ShovelTools.BallControlManager,
// ShovelTools.GameManager); the engine functions have the real names and argument counts (UnityEngine.Rigidbody get_velocity / set_velocity / get_position ...).
// The pretend ball is a real little physics simulation (Unity's step order: gravity, then drag, then move), so the test can check where a shot REALLY ends up.
// Nothing here proves anything about the real game. It only proves our code does what it should against a game that behaves as I read it.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
struct Klass;
struct Type { std::string name; Klass* klass; };
struct Field { std::string name; Type* type; size_t offset; int flags; };
struct Method { uint64_t methodPointer; std::string name; unsigned params; Type* ret; uint32_t flags; int id; };
struct Klass { std::string name, ns; Klass* parent; std::vector<Field> fields; std::vector<Method*> methods; int valueSize; bool isValue, isEnum; int instanceSize; };
struct Image { std::string name; std::vector<Klass*> classes; };

enum { M_NONE = 0, M_RB_GETVEL, M_RB_SETVEL, M_RB_GETPOS, M_RB_GETDRAG, M_RB_GETUSEGRAV, M_PH_GETGRAV, M_TM_FIXEDDT, M_GM_INSTANCE, M_GM_OWNED };

Klass kEnumState{"EGameState", "ShovelTools.GameManager", nullptr, {}, {}, 4, true, true, 4};
Klass kOther{"OtherThing", "Game", nullptr, {}, {}, 0, false, false, 32};
Type tInt{"System.Int32", nullptr}, tFloat{"System.Single", nullptr}, tBool{"System.Boolean", nullptr}, tVec3{"UnityEngine.Vector3", nullptr}, tVoid{"System.Void", nullptr},
     tState{"ShovelTools.GameManager.EGameState", &kEnumState}, tOther{"Game.OtherThing", &kOther};
Klass kBehaviour{"MonoBehaviour", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
Klass kComponent{"Component", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
Klass kRigidbody{"Rigidbody", "UnityEngine", &kComponent, {}, {}, 0, false, false, 24};
Klass kPhysics{"Physics", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Klass kTime{"Time", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Type tRB{"UnityEngine.Rigidbody", &kRigidbody};
Klass kBall, kBcm, kGm, kLoco;
Klass kEnumVert{"LocomotionVerticalState", "", nullptr, {}, {}, 4, true, true, 4};      // the player's vertical state (FLOOR, JUMPING, FALLING, GRABBING): a nested enum, so no namespace
Type tBall{"ShovelTools.Basketball", &kBall}, tBcm{"ShovelTools.BallControlManager", &kBcm}, tLoco{"ShovelTools.PlayerLocomotion", &kLoco},
     tVert{"ShovelTools.PlayerLocomotion.LocomotionVerticalState", &kEnumVert};

char gCode[0x1000];
Method* mk(const char* n, unsigned p, Type* r, int id, uint32_t fl = 0) { return new Method{reinterpret_cast<uint64_t>(gCode) + 0x100 + static_cast<uint64_t>(id) * 16, n, p, r, fl, id}; }

Image iGame{"Assembly-CSharp", {&kBall, &kBcm, &kGm, &kLoco, &kEnumVert}};
Image iPhys{"UnityEngine.PhysicsModule", {&kRigidbody, &kPhysics}};
Image iCore{"UnityEngine.CoreModule", {&kComponent, &kTime}};
Image iMscorlib{"mscorlib", {}};
void* gAssemblies[4] = {&iMscorlib, &iGame, &iPhys, &iCore};
int gDomain = 1;
bool gInit = false;

void buildFields() {       // called for every new scenario, so a scenario that breaks a class does not spoil the next one
    kBall.fields = {{"_currFrameVelocity", &tVec3, 24, 0}, {"playerNetworked", &tOther, 64, 0}, {"_rigidbody", &tRB, 72, 0}, {"_wasShot", &tBool, 80, 0}, {"_shotMade", &tBool, 81, 0},
         {"_ballControlManager", &tBcm, 144, 0}, {"_hoopsLength", &tInt, 164, 0}, {"_northHoopPosition", &tVec3, 180, 0}, {"_southHoopPosition", &tVec3, 192, 0},
         {"_northHoop01Position", &tVec3, 204, 0}, {"_southHoop01Position", &tVec3, 216, 0}, {"_northHoop02Position", &tVec3, 228, 0}, {"_southHoop02Position", &tVec3, 240, 0},
         {"_hoopRadius", &tFloat, 304, 0}, {"_currentBallPosition", &tVec3, 316, 0}, {"_dragValuesWereCleared", &tBool, 348, 0}, {"_originalDrag", &tFloat, 352, 0},
         {"_unheldTime", &tFloat, 388, 0}, {"_basketballState", &tInt, 396, 0}, {"_NEGATIVE_VELOCITY_Y", &tFloat, 0, 0x10}};
    kBcm.fields = {{"_leftBallControl", &tOther, 40, 0}, {"_isAI", &tBool, 290, 0}, {"_basketballRigidbody", &tRB, 152, 0}, {"_basketball", &tBall, 168, 0},
         {"_releasedLeftTimer", &tFloat, 200, 0}, {"_releasedRightTimer", &tFloat, 204, 0}, {"lastReleaseTime", &tFloat, 384, 0}, {"_lastRawThrowVelocity", &tVec3, 388, 0},
         {"_playerLocomotion", &tLoco, 144, 0}};      // (last in the list, so the tests that remove fields by number keep working)
    kLoco.fields = {{"_isLeftJumpPressed", &tBool, 240, 0}, {"_isRightJumpPressed", &tBool, 241, 0}, {"_locomotionState", &tInt, 812, 0}, {"_locomotionVerticalState", &tVert, 828, 0}};
    kGm.fields = {{"User", &tOther, 392, 0}, {"_playerBallControlManager", &tBcm, 408, 0}, {"_isInCompetitionMode", &tBool, 480, 0}, {"_isGMMode", &tBool, 760, 0}, {"_isSolo", &tBool, 761, 0},
         {"_isNBA", &tBool, 762, 0}, {"_gameState", &tState, 856, 0}};
}

void init() {
    if (gInit) return;
    gInit = true;
    kBall.name = "Basketball"; kBall.ns = "ShovelTools"; kBall.parent = &kBehaviour; kBall.instanceSize = 408;
    kBcm.name = "BallControlManager"; kBcm.ns = "ShovelTools"; kBcm.parent = &kBehaviour; kBcm.instanceSize = 424;
    kGm.name = "GameManager"; kGm.ns = "ShovelTools"; kGm.parent = &kBehaviour; kGm.instanceSize = 864;
    kLoco.name = "PlayerLocomotion"; kLoco.ns = "ShovelTools"; kLoco.parent = &kBehaviour; kLoco.instanceSize = 1056;
    buildFields();
    kRigidbody.methods = {mk("get_velocity", 0, &tVec3, M_RB_GETVEL), mk("set_velocity", 1, &tVoid, M_RB_SETVEL), mk("get_position", 0, &tVec3, M_RB_GETPOS),
                          mk("get_drag", 0, &tFloat, M_RB_GETDRAG), mk("get_useGravity", 0, &tBool, M_RB_GETUSEGRAV)};
    kPhysics.methods = {mk("get_gravity", 0, &tVec3, M_PH_GETGRAV, 0x10)};
    kTime.methods = {mk("get_fixedDeltaTime", 0, &tFloat, M_TM_FIXEDDT, 0x10)};
    kGm.methods = {mk("get_Instance", 0, &tVoid, M_GM_INSTANCE, 0x10), mk("GetOwnedBallControlManager", 0, &tVoid, M_GM_OWNED)};
}

// ---------------------------------------------------------------- the pretend world
struct World {
    unsigned char *gm = nullptr, *bcm = nullptr, *ball = nullptr, *rb = nullptr, *loco = nullptr;
    double p[3] = {0, 1.6, 0}, v[3] = {0, 0, 0};
    double drag = 0.11, gravity = 9.81, fdt = 1.0 / 72.0;
    bool useGravity = true, held = true, released = false;
    bool getInstanceNull = false, getOwnedNull = false, throwAll = false, rbDestroyed = false;
    long invokes = 0, setVelCalls = 0, steps = 0, stepsSinceRelease = 0;
    int overrideAtStep = -1; double ov[3] = {0, 0, 0};
    double time = 0;
    int unheldMode = 0;            // 1 = _unheldTime never counts up (a game that does not use it as I read it)
};
World W;
std::vector<int> gAllocatedSize;

void setF(unsigned char* o, int off, float v) { std::memcpy(o + off, &v, 4); }
float getF(const unsigned char* o, int off) { float v; std::memcpy(&v, o + off, 4); return v; }
void setV(unsigned char* o, int off, double x, double y, double z) { setF(o, off, static_cast<float>(x)); setF(o, off + 4, static_cast<float>(y)); setF(o, off + 8, static_cast<float>(z)); }
std::vector<unsigned char*> gAllocated;          // every pretend object, so the next scenario can wipe them (a stale one would look like a second running copy)
unsigned char* makeObject(Klass* k, int size) {
    unsigned char* o = static_cast<unsigned char*>(calloc(1, static_cast<size_t>(size)));
    gAllocated.push_back(o); gAllocatedSize.push_back(size);
    std::memcpy(o, &k, 8);
    const uint64_t cached = reinterpret_cast<uint64_t>(o) + 0x40; std::memcpy(o + 16, &cached, 8);
    return o;
}

unsigned char gBox[16][48];
int gBoxNext = 0;
unsigned char* box() { unsigned char* b = gBox[gBoxNext++ & 15]; std::memset(b, 0, 48); return b; }
}  // namespace

extern "C" {
#define EXPORT __attribute__((visibility("default")))

// ---- control of the pretend world (used by the test)
EXPORT void fake_aim_create(int withHoops) {
    init();
    buildFields();
    for (size_t i = 0; i < gAllocated.size(); ++i) { std::memset(gAllocated[i], 0, static_cast<size_t>(gAllocatedSize[i])); free(gAllocated[i]); }
    gAllocated.clear(); gAllocatedSize.clear();
    W = World();
    W.gm = makeObject(&kGm, 864); W.bcm = makeObject(&kBcm, 424); W.ball = makeObject(&kBall, 408); W.rb = makeObject(&kRigidbody, 24); W.loco = makeObject(&kLoco, 1056);
    const uint64_t pRb = reinterpret_cast<uint64_t>(W.rb), pBall = reinterpret_cast<uint64_t>(W.ball), pBcm = reinterpret_cast<uint64_t>(W.bcm);
    std::memcpy(W.ball + 72, &pRb, 8);
    std::memcpy(W.bcm + 168, &pBall, 8); std::memcpy(W.bcm + 152, &pRb, 8);
    { const uint64_t pLoco = reinterpret_cast<uint64_t>(W.loco); std::memcpy(W.bcm + 144, &pLoco, 8); }
    std::memcpy(W.gm + 408, &pBcm, 8);
    std::memcpy(W.ball + 144, &pBcm, 8);
    if (withHoops) { setV(W.ball, 180, 0, 3.1, 12.66); setV(W.ball, 192, 0, 3.1, -12.66); }
    const int hl = 1; std::memcpy(W.ball + 164, &hl, 4);
    setF(W.ball, 304, 0.2286f); setF(W.ball, 352, 0.11f);
    setF(W.bcm, 200, 12.33f); setF(W.bcm, 204, 5.0f);
    const int st = 3; std::memcpy(W.gm + 856, &st, 4); W.gm[760] = 1;
}
EXPORT void* fake_aim_object(int which) { return which == 0 ? W.gm : (which == 1 ? W.bcm : (which == 2 ? W.ball : W.rb)); }
EXPORT void fake_aim_set(const char* key, double a, double b, double c) {
    const std::string k = key;
    if (k == "drag") W.drag = a; else if (k == "gravity") W.gravity = a; else if (k == "fdt") W.fdt = a; else if (k == "usegravity") W.useGravity = a != 0;
    else if (k == "get_instance_null") W.getInstanceNull = a != 0; else if (k == "get_owned_null") W.getOwnedNull = a != 0; else if (k == "throw_all") W.throwAll = a != 0;
    else if (k == "override") { W.overrideAtStep = static_cast<int>(a); W.ov[0] = b; W.ov[1] = c; W.ov[2] = 0; }
    else if (k == "override_vz") W.ov[2] = a;
    else if (k == "unheld_mode") W.unheldMode = static_cast<int>(a);
    else if (k == "destroy_rb") { W.rbDestroyed = a != 0; const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.rb) + 0x40; std::memcpy(W.rb + 16, &z, 8); }
    else if (k == "destroy_bcm") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.bcm) + 0x40; std::memcpy(W.bcm + 16, &z, 8); }
    else if (k == "vstate") { const int v = static_cast<int>(a); std::memcpy(W.loco + 828, &v, 4); }                       // the player's vertical state: 0 floor, 1 jumping, 2 falling, 3 grabbing
    else if (k == "locostate") { const int v = static_cast<int>(a); std::memcpy(W.loco + 812, &v, 4); }
    else if (k == "jumpbtn") { W.loco[240] = a != 0; W.loco[241] = b != 0; }
    else if (k == "loco_link_null") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.loco); std::memcpy(W.bcm + 144, &z, 8); }
    else if (k == "destroy_loco") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.loco) + 0x40; std::memcpy(W.loco + 16, &z, 8); }
    else if (k == "break_vert_type") { for (Field& f : kLoco.fields) if (f.name == "_locomotionVerticalState") f.type = &tInt; }
    else if (k == "remove_loco_vert") { for (size_t i = 0; i < kLoco.fields.size(); ++i) if (kLoco.fields[i].name == "_locomotionVerticalState") { kLoco.fields.erase(kLoco.fields.begin() + static_cast<long>(i)); break; } }
    else if (k == "hoops_zero") { setV(W.ball, 180, 0, 0, 0); setV(W.ball, 192, 0, 0, 0); }
    else if (k == "break_rigidbody_type") { for (Field& f : kBall.fields) if (f.name == "_rigidbody") f.type = &tInt; }
    else if (k == "remove_field") { /* a = index of the field, b = 0: ball control class, 1: ball class */
        std::vector<Field>& fl = b == 0 ? kBcm.fields : kBall.fields;
        if (a >= 0 && a < static_cast<double>(fl.size())) fl.erase(fl.begin() + static_cast<long>(a)); }
    (void)b; (void)c;
}
EXPORT void fake_aim_hold(double x, double y, double z) {
    W.p[0] = x; W.p[1] = y; W.p[2] = z; W.v[0] = W.v[1] = W.v[2] = 0; W.held = true; W.released = false;
    setF(W.ball, 388, 0.0f); W.ball[80] = 0; W.ball[81] = 0;
    setV(W.ball, 316, x, y, z);
}
// you let go: hand 1 = left, 2 = right, 3 = both. `throwVelocity` is what the player's throw gives the ball.
EXPORT void fake_aim_release(int hand, double vx, double vy, double vz) {
    W.held = false; W.released = true; W.stepsSinceRelease = 0;
    W.v[0] = vx; W.v[1] = vy; W.v[2] = vz;
    if (hand & 1) setF(W.bcm, 200, 0.0f);
    if (hand & 2) setF(W.bcm, 204, 0.0f);
    setF(W.bcm, 384, static_cast<float>(W.time));
    setV(W.bcm, 388, vx, vy, vz);
    W.ball[80] = 1;
}
// one physics step (Unity's order: gravity, then drag, then move)
EXPORT void fake_aim_step() {
    ++W.steps; W.time += W.fdt;
    setF(W.bcm, 200, getF(W.bcm, 200) + static_cast<float>(W.fdt));
    setF(W.bcm, 204, getF(W.bcm, 204) + static_cast<float>(W.fdt));
    if (W.held) return;
    ++W.stepsSinceRelease;
    if (W.overrideAtStep >= 0 && W.stepsSinceRelease == W.overrideAtStep) { W.v[0] = W.ov[0]; W.v[1] = W.ov[1]; W.v[2] = W.ov[2]; }
    const double qy = W.p[1];
    if (W.useGravity) W.v[1] -= W.gravity * W.fdt;
    const double damp = 1.0 - W.drag * W.fdt;
    for (int i = 0; i < 3; ++i) { W.v[i] *= damp; W.p[i] += W.v[i] * W.fdt; }
    if (W.p[1] < 0.12) { W.p[1] = 0.12; W.v[1] = 0; W.v[0] *= 0.5; W.v[2] *= 0.5; }       // the floor
    if (W.unheldMode == 0) setF(W.ball, 388, getF(W.ball, 388) + static_cast<float>(W.fdt));
    setV(W.ball, 316, W.p[0], W.p[1], W.p[2]);
    // the game's own scoring: the ball comes down through a ring (inside the ring radius)
    for (int h = 0; h < 2; ++h) {
        const double hz = h == 0 ? 12.66 : -12.66;
        if (qy >= 3.1 && W.p[1] < 3.1 && W.v[1] < 0) {
            const double k = (qy - 3.1) / (qy - W.p[1]);
            const double cx = W.p[0] - W.v[0] * W.fdt * (1 - k), cz = W.p[2] - W.v[2] * W.fdt * (1 - k);
            if (std::sqrt(cx * cx + (cz - hz) * (cz - hz)) < 0.2286) W.ball[81] = 1;
        }
    }
}
EXPORT void fake_aim_state(double* out) { for (int i = 0; i < 3; ++i) { out[i] = W.p[i]; out[3 + i] = W.v[i]; } out[6] = static_cast<double>(W.invokes); out[7] = static_cast<double>(W.setVelCalls); out[8] = W.ball[81]; out[9] = static_cast<double>(W.steps); }
EXPORT double fake_aim_fdt() { return W.fdt; }
// extra GameManager copies (for the memory-search tests): kind 0 = a copy whose ball control link is empty (junk), 1 = a copy that points at ANOTHER valid ball control object
EXPORT void* fake_aim_extra_gm(int kind) {
    unsigned char* g = makeObject(&kGm, 864);
    if (kind == 1) { unsigned char* b2 = makeObject(&kBcm, 424); const uint64_t p = reinterpret_cast<uint64_t>(b2); std::memcpy(g + 408, &p, 8); }
    return g;
}

// ---- the runtime functions the link uses
EXPORT void* il2cpp_domain_get() { init(); return &gDomain; }
EXPORT void** il2cpp_domain_get_assemblies(void*, size_t* n) { *n = 4; return gAssemblies; }
EXPORT void* il2cpp_assembly_get_image(void* a) { return a; }
EXPORT const char* il2cpp_image_get_name(void* i) { return static_cast<Image*>(i)->name.c_str(); }
EXPORT size_t il2cpp_image_get_class_count(void* i) { return static_cast<Image*>(i)->classes.size(); }
EXPORT void* il2cpp_image_get_class(void* i, size_t k) { return static_cast<Image*>(i)->classes[k]; }
EXPORT const char* il2cpp_class_get_name(void* k) { return static_cast<Klass*>(k)->name.c_str(); }
EXPORT const char* il2cpp_class_get_namespace(void* k) { return static_cast<Klass*>(k)->ns.c_str(); }
EXPORT void* il2cpp_class_get_parent(void* k) { return static_cast<Klass*>(k)->parent; }
EXPORT void* il2cpp_class_get_fields(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->fields.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return &c->fields[i];
}
EXPORT const char* il2cpp_field_get_name(void* f) { return static_cast<Field*>(f)->name.c_str(); }
EXPORT void* il2cpp_field_get_type(void* f) { return static_cast<Field*>(f)->type; }
EXPORT size_t il2cpp_field_get_offset(void* f) { return static_cast<Field*>(f)->offset; }
EXPORT int il2cpp_field_get_flags(void* f) { return static_cast<Field*>(f)->flags; }
EXPORT void* il2cpp_class_get_methods(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->methods.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return c->methods[i];
}
EXPORT const char* il2cpp_method_get_name(void* m) { return static_cast<Method*>(m)->name.c_str(); }
EXPORT unsigned il2cpp_method_get_param_count(void* m) { return static_cast<Method*>(m)->params; }
EXPORT void* il2cpp_method_get_return_type(void* m) { return static_cast<Method*>(m)->ret; }
EXPORT char* il2cpp_type_get_name(void* t) { return strdup(static_cast<Type*>(t)->name.c_str()); }
EXPORT void il2cpp_free(void* p) { free(p); }
EXPORT void* il2cpp_thread_attach(void*) { return &gDomain; }
EXPORT void il2cpp_thread_detach(void*) {}
EXPORT int il2cpp_class_instance_size(void* k) { return static_cast<Klass*>(k)->instanceSize; }
EXPORT void* il2cpp_class_from_type(void* t) { Type* ty = static_cast<Type*>(t); return ty->klass ? static_cast<void*>(ty->klass) : static_cast<void*>(&kOther); }
EXPORT bool il2cpp_class_is_valuetype(void* k) { return static_cast<Klass*>(k)->isValue; }
EXPORT bool il2cpp_class_is_enum(void* k) { return static_cast<Klass*>(k)->isEnum; }
EXPORT int il2cpp_class_value_size(void* k, unsigned* align) { if (align) *align = 4; return static_cast<Klass*>(k)->valueSize; }
EXPORT void* il2cpp_thread_current() { return &gDomain; }
EXPORT void* il2cpp_resolve_icall(const char*) { return nullptr; }

EXPORT void* il2cpp_class_get_method_from_name(void* k, const char* name, int argc) {
    Klass* c = static_cast<Klass*>(k);
    for (Method* m : c->methods) if (m->name == name && static_cast<int>(m->params) == argc) return m;
    return nullptr;
}

EXPORT void* il2cpp_runtime_invoke(void* method, void* obj, void** params, void** exc) {
    ++W.invokes;
    Method* m = static_cast<Method*>(method);
    if (W.throwAll) { if (exc) *exc = &gDomain; return nullptr; }
    auto bad = [&]() -> void* { if (exc) *exc = &gDomain; return nullptr; };
    auto boxVec = [&](double x, double y, double z) -> void* { unsigned char* b = box(); setV(b, 16, x, y, z); return b; };
    switch (m->id) {
    case M_RB_GETVEL: if (obj != W.rb || W.rbDestroyed) return bad(); return boxVec(W.v[0], W.v[1], W.v[2]);
    case M_RB_GETPOS: if (obj != W.rb || W.rbDestroyed) return bad(); return boxVec(W.p[0], W.p[1], W.p[2]);
    case M_RB_SETVEL: {
        if (obj != W.rb || W.rbDestroyed || !params) return bad();
        const float* f = static_cast<const float*>(params[0]);
        W.v[0] = f[0]; W.v[1] = f[1]; W.v[2] = f[2]; ++W.setVelCalls;
        return nullptr;
    }
    case M_RB_GETDRAG: { if (obj != W.rb) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(W.drag)); return b; }
    case M_RB_GETUSEGRAV: { if (obj != W.rb) return bad(); unsigned char* b = box(); b[16] = W.useGravity ? 1 : 0; return b; }
    case M_PH_GETGRAV: return boxVec(0, -W.gravity, 0);
    case M_TM_FIXEDDT: { unsigned char* b = box(); setF(b, 16, static_cast<float>(W.fdt)); return b; }
    case M_GM_INSTANCE: return W.getInstanceNull ? nullptr : W.gm;
    case M_GM_OWNED: if (obj != W.gm) return bad(); return W.getOwnedNull ? nullptr : W.bcm;
    }
    return bad();
}
}
