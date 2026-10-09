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
     tBool{"System.Boolean", nullptr}, tVec3{"UnityEngine.Vector3", nullptr}, tMode{"Game.Mode", &kEnumMode}, tPair{"Game.Pair", &kPairStruct}, tOther{"Game.OtherThing", &kOther};
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

// the HEADSET version of the player locomotion (stage D5c): setters are two machine instructions (str s0,[x0,#off] ; ret)
Klass kPlayerLoco{"PlayerLocomotion", "ShovelTools", &kBehaviour,
    {{"_forwardMaxSpeed", &tFloat, 24, 0}, {"_jumpHeightMultiplier", &tFloat, 28, 0}, {"_walkSpeedMultiplier", &tFloat, 32, 0}, {"_flag", &tBool, 36, 0}}, {}, 0, false, false, 48};

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
    kPlayerLoco.methods = {mk("SetForwardMaxSpeed", 1, &tVoid, 0x3400), mk("SetJumpHeightMultiplier", 1, &tVoid, 0x3410), mk("get_CurrentSpeed", 0, &tFloat, 0x3420), mk("Update", 0, &tVoid, 0x3430)};
    const unsigned char setterA[8] = {0x00, 0x18, 0x00, 0xbd, 0xc0, 0x03, 0x5f, 0xd6};     // str s0,[x0,#24] ; ret
    const unsigned char setterB[8] = {0x00, 0x1c, 0x00, 0xbd, 0xc0, 0x03, 0x5f, 0xd6};     // str s0,[x0,#28] ; ret
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
__attribute__((visibility("default"))) void* fake_make_player_locomotion(int kind) {     // 0 = healthy numbers, 1 = garbage numbers (a stale block)
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 48));
    Klass* k = &kPlayerLoco; std::memcpy(o, &k, 8);
    const uint64_t cached = reinterpret_cast<uint64_t>(o) + 0x40; std::memcpy(o + 16, &cached, 8);
    if (kind == 0) { const float a = 4.5f, b = 1.25f, c = 1.0f; std::memcpy(o + 24, &a, 4); std::memcpy(o + 28, &b, 4); std::memcpy(o + 32, &c, 4); o[36] = 1; }
    else { const uint32_t a = 0x00000079u, b = 0x7fc00000u, c = 0x4e8c8c8cu; std::memcpy(o + 24, &a, 4); std::memcpy(o + 28, &b, 4); std::memcpy(o + 32, &c, 4); o[36] = 7; }
    return o;
}
__attribute__((visibility("default"))) void* fake_make_vertical() {
    unsigned char* o = static_cast<unsigned char*>(calloc(1, 24));
    Klass* k = &kVertical; std::memcpy(o, &k, 8);
    const float g = -9.81f, js = 3.5f; std::memcpy(o + 16, &g, 4); std::memcpy(o + 20, &js, 4);
    return o;
}

__attribute__((visibility("default"))) void* il2cpp_domain_get() { init(); return gDomainNull ? nullptr : &gDomain; }
__attribute__((visibility("default"))) void** il2cpp_domain_get_assemblies(void*, size_t* n) { *n = 3; return gAssemblies; }
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
