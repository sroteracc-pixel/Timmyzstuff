// A PRETEND libil2cpp.so for the PC test of the "Scan game code" button.
// It exports the same function names the real runtime does and holds a tiny made-up game:
//   Assembly-CSharp: PlayerMovement (walkSpeed, jumpHeight, CharacterController; NO running copy), GameSettings (speedMultiplier),
//                    PhysicsBody (a Rigidbody), RemoveItem (must NOT be picked), RemoveMovementTag (must be picked),
//                    a "<>c__DisplayClass" helper (must be skipped), Menu (not interesting at all),
//                    MobilePlayerLocomotion : LocomotionBase (important; ONE real running copy + two fakes),
//                    MobileVerticalMotion (important; one running copy), CharacterWorldConstraints (important; 70 fields)
//   Photon.Realtime: NetworkMoveSync        mscorlib: System.Object (engine/system: must be skipped)
// Nothing here proves anything about the real game. It only proves our scan code reads and prints correctly.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

namespace {
struct Klass;
struct Type { std::string name; Klass* klass; };
struct Field { std::string name; Type* type; size_t offset; int flags; };
struct Method { uint64_t methodPointer; std::string name; unsigned params; Type* ret; uint32_t flags; };   // first 8 bytes = pointer, like the real MethodInfo
struct Klass { std::string name, ns; Klass* parent; std::vector<Field> fields; std::vector<Method*> methods; int valueSize; bool isValue, isEnum; int instanceSize; };
struct Image { std::string name; std::vector<Klass*> classes; };

Klass kEnumMode{"Mode", "Game", nullptr, {}, {}, 4, true, true, 4};
Klass kPairStruct{"Pair", "Game", nullptr, {}, {}, 8, true, false, 8};
Klass kOther{"OtherThing", "Game", nullptr, {}, {}, 0, false, false, 32};
Type tInt{"System.Int32", nullptr}, tFloat{"System.Single", nullptr}, tVoid{"System.Void", nullptr}, tCC{"UnityEngine.CharacterController", nullptr}, tRB{"UnityEngine.Rigidbody", nullptr},
     tBool{"System.Boolean", nullptr}, tVec3{"UnityEngine.Vector3", nullptr}, tMode{"Game.Mode", &kEnumMode}, tPair{"Game.Pair", &kPairStruct}, tOther{"Game.OtherThing", &kOther},
     tVec2{"UnityEngine.Vector2", nullptr}, tQuat{"UnityEngine.Quaternion", nullptr};
Klass kBehaviour{"MonoBehaviour", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
char gCode[0x4000];                       // the pretend "code": method pointers point into this (inside the pretend library, like the real thing)

Method* mk(const char* n, unsigned p, Type* r, uint64_t rva, uint32_t fl = 0) { return new Method{reinterpret_cast<uint64_t>(gCode) + rva, n, p, r, fl}; }

Klass kMove{"PlayerMovement", "Game", &kBehaviour,
    {{"walkSpeed", &tFloat, 0x20, 0}, {"jumpHeight", &tFloat, 0x24, 0}, {"controller", &tCC, 0x28, 0}, {"gravityScale", &tFloat, 0x30, 0}, {"instanceCount", &tInt, 0x0, 0x10}}, {}, 0, false, false, 64};
Klass kSettings{"GameSettings", "Game", nullptr, {{"speedMultiplier", &tFloat, 0x10, 0}, {"volume", &tFloat, 0x14, 0}}, {}, 0, false, false, 32};
Klass kBody{"PhysicsBody", "Game", &kBehaviour, {{"rb", &tRB, 0x18, 0}, {"mass", &tFloat, 0x20, 0}}, {}, 0, false, false, 48};
Klass kRemoveItem{"RemoveItem", "Game", nullptr, {{"slot", &tInt, 0x10, 0}}, {}, 0, false, false, 32};
Klass kRemoveMove{"RemoveMovementTag", "Game", nullptr, {{"tag", &tInt, 0x10, 0}}, {}, 0, false, false, 32};
Klass kHelper{"<>c__DisplayClass4_0", "Game", nullptr, {{"jumpSpeed", &tFloat, 0x10, 0}}, {}, 0, false, false, 32};
Klass kMenu{"Menu", "Game", nullptr, {{"flyingText", &tInt, 0x10, 0}, {"title", &tInt, 0x14, 0}}, {}, 0, false, false, 32};   // "flyingText" has "fly" in a FIELD name: should show as a field hit
Klass kNetMove{"NetworkMoveSync", "Photon", nullptr, {{"lag", &tFloat, 0x10, 0}}, {}, 0, false, false, 32};
Klass kObject{"Object", "System", nullptr, {{"speed", &tFloat, 0x0, 0}}, {}, 0, false, false, 16};

Klass kLocoBase{"LocomotionBase", "Game", &kBehaviour, {{"_baseAccel", &tFloat, 24, 0}}, {}, 0, false, false, 32};
Klass kLoco{"MobilePlayerLocomotion", "Game", &kLocoBase,
    {{"_maxSpeed", &tFloat, 32, 0}, {"_jumpHeight", &tFloat, 36, 0}, {"_velocity", &tVec3, 40, 0}, {"_enabled", &tBool, 52, 0}, {"_mode", &tMode, 56, 0},
     {"_pair", &tPair, 64, 0}, {"_target", &tOther, 72, 0}, {"DefaultSpeed", &tFloat, 0, 0x10}, {"Kind", &tInt, 0, 0x40}}, {}, 0, false, false, 80};
Klass kVertical{"MobileVerticalMotion", "Game", nullptr, {{"_gravity", &tFloat, 16, 0}, {"_jumpSpeed", &tFloat, 20, 0}}, {}, 0, false, false, 24};
Klass kConstraints{"CharacterWorldConstraints", "Game", nullptr, {}, {}, 0, false, false, 16 + 4 * 70};

// the HEADSET version of the player locomotion (stage D5c / D6): the REAL class, copied from the real facts file - every field name, type and position
// (211 fields, 1056 bytes). The setters are two machine instructions (str s0,[x0,#off] ; ret).
Klass kPlayerLoco{"PlayerLocomotion", "ShovelTools", &kBehaviour,
    {
#include "fake_player_locomotion_fields.inc"
    }, {}, 0, false, false, 1056};

// ---- the "ball and hoops" world (stage D7). Only visible after fake_set_shot_world(1), so the older tests keep seeing the old world.
Klass kComponent{"Component", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
Klass kRigidbody{"Rigidbody", "UnityEngine", &kComponent, {}, {}, 0, false, false, 24};
Klass kTransform{"Transform", "UnityEngine", &kComponent, {}, {}, 0, false, false, 24};
Klass kPhysics{"Physics", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Klass kTime{"Time", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Type tTransform{"UnityEngine.Transform", &kTransform};
Klass kRimSync{"RimSync", "ShovelTools", &kBehaviour, {{"_rimTransform", &tTransform, 24, 0}, {"_bend", &tFloat, 32, 0}, {"_hoopHeight", &tFloat, 36, 0}}, {}, 0, false, false, 48};
Klass kRimBend{"GymClassRimBend", "ShovelTools", &kBehaviour, {{"_rimPivot", &tTransform, 24, 0}, {"_bendAmount", &tFloat, 32, 0}, {"_isNet", &tBool, 36, 0}}, {}, 0, false, false, 48};
Klass kSlider{"GymClassSlider", "ShovelTools", &kBehaviour, {{"_value", &tFloat, 24, 0}}, {}, 0, false, false, 32};
Klass kBall{"BasketballBall", "ShovelTools", &kBehaviour, {{"_body", &tRB, 24, 0}, {"_isHeld", &tBool, 32, 0}, {"_lastShooterId", &tInt, 36, 0}}, {}, 0, false, false, 48};
Klass kShotPrefs{"ShotPreferences", "ShovelTools", nullptr, {{"wristAngle", &tFloat, 16, 0}, {"throwPower", &tFloat, 20, 0}}, {}, 0, false, false, 32};
Klass kPrimary{"PrimaryColor", "ShovelTools", nullptr, {{"r", &tFloat, 16, 0}}, {}, 0, false, false, 32};            // has "rim" inside a word: must NOT match
Klass kFootball{"FootballBall", "ShovelTools", nullptr, {{"x", &tFloat, 16, 0}}, {}, 0, false, false, 32};            // another sport: must NOT match
Klass kBaseball{"BaseballBat", "ShovelTools", nullptr, {{"x", &tFloat, 16, 0}}, {}, 0, false, false, 32};
Klass kGrabBase{"GrabbableBase", "Autohand", &kBehaviour, {{"body", &tRB, 24, 0}, {"heldBy", &tOther, 32, 0}, {"throwPower", &tFloat, 40, 0}}, {}, 0, false, false, 64};
Klass kGrabbable{"Grabbable", "Autohand", &kGrabBase, {{"jointBreakForce", &tFloat, 64, 0}}, {}, 0, false, false, 80};
Klass kHandA{"Hand", "Autohand", &kBehaviour, {{"holdingObj", &tOther, 24, 0}}, {}, 0, false, false, 48};
Klass kMenuHand{"HandMenu", "Autohand", nullptr, {{"x", &tFloat, 16, 0}}, {}, 0, false, false, 32};                 // "Hand..." but not in the explicit list
// added after the REAL class names were checked (stage D7b): the ball's own physics class, an event delegate that must be skipped, a bot command,
// and the network library (Normcore: "Normal.Realtime")
Klass kMulticast{"MulticastDelegate", "System", nullptr, {}, {}, 0, false, false, 64};
Klass kParamMass{"ParameterBasketballMass", "ShovelTools", &kMulticast, {}, {}, 0, false, false, 64};              // an event: 0 fields. Indexed, but never written out and never searched
Klass kBallPhys{"BallPhysics", "ShovelTools", &kBehaviour, {{"_body", &tRB, 24, 0}, {"_drag", &tFloat, 32, 0}, {"_mass", &tFloat, 36, 0}, {"_inHand", &tBool, 40, 0}}, {}, 0, false, false, 48};
Klass kACommand{"ACommand", "ShovelTools", nullptr, {}, {}, 0, false, false, 16};
Klass kShotCmd{"PlayerShotCommand", "ShovelTools", &kACommand, {{"_shooterId", &tInt, 16, 0}}, {}, 0, false, false, 24};
Klass kRealtime{"Realtime", "Normal.Realtime", &kBehaviour, {{"_connected", &tBool, 24, 0}, {"_clientId", &tInt, 28, 0}}, {}, 0, false, false, 48};
Klass kRtView{"RealtimeView", "Normal.Realtime", &kBehaviour, {{"_ownerId", &tInt, 24, 0}, {"_viewId", &tInt, 28, 0}}, {}, 0, false, false, 48};
Klass kRtTransform{"RealtimeTransform", "Normal.Realtime", &kBehaviour, {{"_ownedByMe", &tBool, 24, 0}}, {}, 0, false, false, 48};
Klass kRtOther{"RealtimeVoice", "Normal.Realtime", &kBehaviour, {{"_x", &tInt, 24, 0}}, {}, 0, false, false, 48};     // another network class: not in the list, must not be written
Image iShot{"IRL.GymFake", {&kRimSync, &kRimBend, &kSlider, &kBall, &kShotPrefs, &kPrimary, &kFootball, &kBaseball, &kParamMass, &kBallPhys, &kACommand, &kShotCmd}};
Image iNet{"Normal.Realtime", {&kRealtime, &kRtView, &kRtTransform, &kRtOther}};
Image iAuto{"Autohand.Runtime", {&kGrabBase, &kGrabbable, &kHandA, &kMenuHand}};
Image iPhys{"UnityEngine.PhysicsModule", {&kRigidbody, &kPhysics}};
Image iCoreEng{"UnityEngine.CoreModule", {&kTransform, &kComponent, &kTime}};
bool gShotWorld = false;
void* gAssembliesShot[8] = {nullptr};

Image iGame{"Assembly-CSharp", {&kMove, &kSettings, &kBody, &kRemoveItem, &kRemoveMove, &kHelper, &kMenu, &kLocoBase, &kLoco, &kVertical, &kConstraints, &kPlayerLoco}};
Image iPhoton{"Photon.Realtime", {&kNetMove}};
Image iCore{"mscorlib", {&kObject}};
void* gAssemblies[3] = {&iCore, &iGame, &iPhoton};
int gDomain = 1;
bool gDomainNull = false;
bool gInit = false;
void init() {
    if (gInit) return;
    gInit = true;
    kMove.methods = {mk("Update", 0, &tVoid, 0x1000), mk("Jump", 1, &tVoid, 0x1100), mk("SetSpeed", 1, &tVoid, 0x1200), mk("get_Instance", 0, &tVoid, 0x1300, 0x10)};
    kSettings.methods = {mk("GetSpeed", 0, &tFloat, 0x2000)};
    kNetMove.methods = {mk("OnPhotonSerializeView", 2, &tVoid, 0x3000)};
    kLoco.methods = {mk("SetJumpHeight", 1, &tVoid, 0x3100), mk("SetMaxJumpSpeed", 1, &tVoid, 0x3200)};
    for (int i = 0; i < 70; ++i) kConstraints.fields.push_back({"_pad" + std::to_string(i), &tInt, static_cast<size_t>(16 + 4 * i), 0});
    kConstraints.methods = {mk("Clamp", 1, &tVoid, 0x3300)};
    kRimSync.methods = {mk("Update", 0, &tVoid, 0x3500), mk("get_Bend", 0, &tFloat, 0x3510)};
    kRimBend.methods = {mk("Bend", 1, &tVoid, 0x3520), mk("OnCollisionEnter", 1, &tVoid, 0x3530)};
    kBall.methods = {mk("OnRelease", 1, &tVoid, 0x3540), mk("get_IsHeld", 0, &tBool, 0x3550)};
    kBallPhys.methods = {mk("FixedUpdate", 0, &tVoid, 0x3580), mk("OnThrown", 1, &tVoid, 0x3590)};
    kRealtime.methods = {mk("get_connected", 0, &tBool, 0x35a0)};
    kRtView.methods = {mk("RequestOwnership", 0, &tVoid, 0x35b0), mk("get_isOwnedLocallySelf", 0, &tBool, 0x35c0)};
    kGrabBase.methods = {mk("OnRelease", 1, &tVoid, 0x3560), mk("get_IsHeld", 0, &tBool, 0x3570)};
    kRigidbody.methods = {mk("get_velocity", 0, &tVec3, 0x3600), mk("set_velocity", 1, &tVoid, 0x3610), mk("get_position", 0, &tVec3, 0x3620), mk("get_mass", 0, &tFloat, 0x3630),
                          mk("AddForce", 1, &tVoid, 0x3640), mk("AddForce", 2, &tVoid, 0x3650), mk("MovePosition", 1, &tVoid, 0x3660)};
    kTransform.methods = {mk("get_position", 0, &tVec3, 0x3700), mk("get_forward", 0, &tVec3, 0x3710)};
    kComponent.methods = {mk("get_transform", 0, &tTransform, 0x3720)};
    kPhysics.methods = {mk("get_gravity", 0, &tVec3, 0x3730)};
    kPlayerLoco.methods = {mk("SetForwardMaxSpeed", 1, &tVoid, 0x3400), mk("SetJumpHeightMultiplier", 1, &tVoid, 0x3410), mk("get_CurrentSpeed", 0, &tFloat, 0x3420), mk("Update", 0, &tVoid, 0x3430)};
    const unsigned char setterA[8] = {0x00, 0xac, 0x02, 0xbd, 0xc0, 0x03, 0x5f, 0xd6};     // str s0,[x0,#684] ; ret   (_forwardMaxSpeed)
    const unsigned char setterB[8] = {0x00, 0xdc, 0x02, 0xbd, 0xc0, 0x03, 0x5f, 0xd6};     // str s0,[x0,#732] ; ret   (_jumpHeightMultiplier)
    std::memcpy(gCode + 0x3400, setterA, 8); std::memcpy(gCode + 0x3410, setterB, 8);
}
}  // namespace

extern "C" {
__attribute__((visibility("default"))) int fake_attach_count = 0;
__attribute__((visibility("default"))) int fake_detach_count = 0;
__attribute__((visibility("default"))) int fake_free_count = 0;
__attribute__((visibility("default"))) int fake_alloc_count = 0;
__attribute__((visibility("default"))) void fake_set_domain_null(int v) { gDomainNull = v != 0; }
__attribute__((visibility("default"))) unsigned long long fake_code_address() { return reinterpret_cast<unsigned long long>(gCode); }

// ---- running copies the test creates in ordinary memory (like the game's objects)
__attribute__((visibility("default"))) void* fake_make_locomotion(int kind) {      // 0 = real, 1 = destroyed (native link empty), 2 = has an owner lock, 3 = real but with another jump height
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 80));
    Klass* k = &kLoco; std::memcpy(o, &k, 8);
    const uint64_t cached = kind == 1 ? 0 : reinterpret_cast<uint64_t>(o) + 0x40;
    std::memcpy(o + 16, &cached, 8);
    if (kind == 2) { uint64_t lock = 0x1234500; std::memcpy(o + 8, &lock, 8); }
    const float baseAccel = 2.5f, maxSpeed = 4.25f, jump = kind == 3 ? 2.25f : 1.5f, vel[3] = {0.5f, 0.0f, -1.25f};
    const int mode = 2; const unsigned char pair[8] = {1, 2, 3, 4, 5, 6, 7, 8}; const uint64_t target = reinterpret_cast<uint64_t>(o);
    std::memcpy(o + 24, &baseAccel, 4); std::memcpy(o + 32, &maxSpeed, 4); std::memcpy(o + 36, &jump, 4); std::memcpy(o + 40, vel, 12);
    o[52] = 1; std::memcpy(o + 56, &mode, 4); std::memcpy(o + 64, pair, 8); std::memcpy(o + 72, &target, 8);
    return o;
}
// kind 0 = the ACTIVE player (numbers copied from the real stage D5c file, copy #1), 1 = a stale block full of garbage numbers,
//      2 = an INACTIVE copy (real object, but head tracking never started and the numbers are zero: copy #2 of the real file)
__attribute__((visibility("default"))) void* fake_make_player_locomotion(int kind) {
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 1056));
    Klass* k = &kPlayerLoco; std::memcpy(o, &k, 8);
    const uint64_t cached = reinterpret_cast<uint64_t>(o) + 0x40; std::memcpy(o + 16, &cached, 8);
    auto f = [&](int off, float v) { std::memcpy(o + off, &v, 4); };
    if (kind == 0) {
        f(684, 2.5f); f(688, 4.0f); f(692, 4.0f);            // forward max speed / acceleration / deceleration
        f(696, 2.0f); f(700, 4.0f); f(704, 4.2f);            // lateral
        f(708, 1.62f); f(712, 4.0f); f(716, 4.0f);           // backward
        f(720, 0.128f); f(724, 4.0f); f(728, 4.0f);          // jump (air) speed numbers
        f(732, 1.5f);                                        // _jumpHeightMultiplier
        f(744, 1.0f); f(748, 1.5f); f(752, 2.0f);            // walk / jog / sprint multipliers
        f(524, 0.0f); f(528, -0.9f); f(532, 0.0f);           // _gravity
        f(464, 1.0f);                                        // _finalSpeedMultiplier
        f(412, 0.125f); f(416, 1.6f); f(420, 0.25f);         // _prevHmdLocalPosition (the head)
        o[460] = 1; o[468] = 1; o[469] = 1;                  // head tracking started, locomotion allowed, movement enabled
    } else if (kind == 1) {
        for (int off = 24; off + 8 <= 1056; off += 8) { const uint32_t a = 0x00000079u, b = 0x7fc00000u; std::memcpy(o + off, &a, 4); std::memcpy(o + off + 4, &b, 4); }
        o[460] = 7;
    }
    return o;
}
__attribute__((visibility("default"))) void* fake_make_vertical() {
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 24));
    Klass* k = &kVertical; std::memcpy(o, &k, 8);
    const float g = -9.81f, js = 3.5f; std::memcpy(o + 16, &g, 4); std::memcpy(o + 20, &js, 4);
    return o;
}

__attribute__((visibility("default"))) void* il2cpp_domain_get() { init(); return gDomainNull ? nullptr : &gDomain; }
__attribute__((visibility("default"))) void fake_set_shot_world(int on) {
    gShotWorld = on != 0;
    if (gShotWorld) { void* a[8] = {&iCore, &iGame, &iPhoton, &iShot, &iAuto, &iPhys, &iCoreEng, &iNet}; std::memcpy(gAssembliesShot, a, sizeof a); }
}
// running copies of the ball-and-hoops classes: 0 = RimSync, 1 = GymClassRimBend, 2 = BasketballBall, 3 = BallPhysics, 4 = Realtime, 5 = RealtimeView, 6 = RealtimeTransform.
// `variant` 1 marks the ball as held.
__attribute__((visibility("default"))) void* fake_make_shot_object(int which, int variant) {
    Klass* k = which == 0 ? &kRimSync : (which == 1 ? &kRimBend : (which == 2 ? &kBall : (which == 3 ? &kBallPhys : (which == 4 ? &kRealtime : (which == 5 ? &kRtView : &kRtTransform)))));
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 48));
    std::memcpy(o, &k, 8);
    const uint64_t cached = reinterpret_cast<uint64_t>(o) + 0x40; std::memcpy(o + 16, &cached, 8);
    const uint64_t ref = 0x7a00001000ULL + 16 * variant; std::memcpy(o + 24, &ref, 8);
    if (which == 0) { const float bend = 0.25f * variant, h = 3.05f; std::memcpy(o + 32, &bend, 4); std::memcpy(o + 36, &h, 4); }
    else if (which == 1) { const float bend = 0.5f + variant; std::memcpy(o + 32, &bend, 4); o[36] = 0; }
    else if (which == 2) { o[32] = variant ? 1 : 0; const int id = 7 + variant; std::memcpy(o + 36, &id, 4); }
    else if (which == 3) { const float drag = 0.05f * (variant + 1), mass = 0.62f; std::memcpy(o + 32, &drag, 4); std::memcpy(o + 36, &mass, 4); o[40] = variant ? 1 : 0; }
    else if (which == 4) { o[24] = 1; const int cid = 3; std::memcpy(o + 28, &cid, 4); }                          // connected, client 3
    else if (which == 5) { const int owner = 2 + variant, view = 100 + variant; std::memcpy(o + 24, &owner, 4); std::memcpy(o + 28, &view, 4); }
    else { o[24] = 1; }
    return o;
}
// the pretend icall table: only the Rigidbody velocity getter (with its full signature) is "found"
__attribute__((visibility("default"))) void* il2cpp_resolve_icall(const char* name) {
    if (std::string(name) == "UnityEngine.Rigidbody::get_velocity_Injected(UnityEngine.Vector3&)") return gCode + 0x3800;
    return nullptr;
}
__attribute__((visibility("default"))) int fake_invoke_count = 0;       // how often the scan CALLED into the game through the optional functions (must stay 0)
__attribute__((visibility("default"))) void* il2cpp_runtime_invoke(void*, void*, void**, void**) { ++fake_invoke_count; return nullptr; }
__attribute__((visibility("default"))) void* il2cpp_class_get_method_from_name(void*, const char*, int) { ++fake_invoke_count; return nullptr; }
__attribute__((visibility("default"))) void** il2cpp_domain_get_assemblies(void*, size_t* n) { if (gShotWorld) { *n = 8; return gAssembliesShot; } *n = 3; return gAssemblies; }
__attribute__((visibility("default"))) void* il2cpp_assembly_get_image(void* a) { return a; }
__attribute__((visibility("default"))) const char* il2cpp_image_get_name(void* i) { return static_cast<Image*>(i)->name.c_str(); }
__attribute__((visibility("default"))) size_t il2cpp_image_get_class_count(void* i) { return static_cast<Image*>(i)->classes.size(); }
__attribute__((visibility("default"))) void* il2cpp_image_get_class(void* i, size_t k) { return static_cast<Image*>(i)->classes[k]; }
__attribute__((visibility("default"))) const char* il2cpp_class_get_name(void* k) { return static_cast<Klass*>(k)->name.c_str(); }
__attribute__((visibility("default"))) const char* il2cpp_class_get_namespace(void* k) { return static_cast<Klass*>(k)->ns.c_str(); }
__attribute__((visibility("default"))) void* il2cpp_class_get_parent(void* k) { return static_cast<Klass*>(k)->parent; }
__attribute__((visibility("default"))) void* il2cpp_class_get_fields(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->fields.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return &c->fields[i];
}
__attribute__((visibility("default"))) const char* il2cpp_field_get_name(void* f) { return static_cast<Field*>(f)->name.c_str(); }
__attribute__((visibility("default"))) void* il2cpp_field_get_type(void* f) { return static_cast<Field*>(f)->type; }
__attribute__((visibility("default"))) size_t il2cpp_field_get_offset(void* f) { return static_cast<Field*>(f)->offset; }
__attribute__((visibility("default"))) int il2cpp_field_get_flags(void* f) { return static_cast<Field*>(f)->flags; }
__attribute__((visibility("default"))) void* il2cpp_class_get_methods(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->methods.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return c->methods[i];
}
__attribute__((visibility("default"))) const char* il2cpp_method_get_name(void* m) { return static_cast<Method*>(m)->name.c_str(); }
__attribute__((visibility("default"))) unsigned il2cpp_method_get_param_count(void* m) { return static_cast<Method*>(m)->params; }
__attribute__((visibility("default"))) void* il2cpp_method_get_return_type(void* m) { return static_cast<Method*>(m)->ret; }
__attribute__((visibility("default"))) unsigned il2cpp_method_get_flags(void* m, unsigned* iflags) { if (iflags) *iflags = 0; return static_cast<Method*>(m)->flags; }
__attribute__((visibility("default"))) char* il2cpp_type_get_name(void* t) { ++fake_alloc_count; return strdup(static_cast<Type*>(t)->name.c_str()); }
__attribute__((visibility("default"))) void il2cpp_free(void* p) { ++fake_free_count; free(p); }
__attribute__((visibility("default"))) void* il2cpp_thread_attach(void*) { ++fake_attach_count; return &gDomain; }
__attribute__((visibility("default"))) void il2cpp_thread_detach(void*) { ++fake_detach_count; }
// optional functions for the live values
__attribute__((visibility("default"))) int il2cpp_class_instance_size(void* k) {
    if (std::getenv("FAKE_IL2CPP_HANG_STEP5")) { for (;;) sleep(1000); }       // PC test: a runtime call that never answers
    return static_cast<Klass*>(k)->instanceSize;
}
__attribute__((visibility("default"))) void* il2cpp_class_from_type(void* t) {
    Type* ty = static_cast<Type*>(t);
    return ty->klass ? static_cast<void*>(ty->klass) : static_cast<void*>(&kOther);      // unknown types behave like a reference type
}
__attribute__((visibility("default"))) bool il2cpp_class_is_valuetype(void* k) { return static_cast<Klass*>(k)->isValue; }
__attribute__((visibility("default"))) bool il2cpp_class_is_enum(void* k) { return static_cast<Klass*>(k)->isEnum; }
__attribute__((visibility("default"))) int il2cpp_class_value_size(void* k, unsigned* align) { if (align) *align = 4; return static_cast<Klass*>(k)->valueSize; }
}
