// A PRETEND libil2cpp.so for the PC test of the "Scan game code" button.
// It exports the same function names the real runtime does and holds a tiny made-up game:
//   Assembly-CSharp: PlayerMovement (walkSpeed, jumpHeight, CharacterController), GameSettings (speedMultiplier),
//                    PhysicsBody (a Rigidbody), RemoveItem (must NOT be picked), RemoveMovementTag (must be picked),
//                    a "<>c__DisplayClass" helper (must be skipped), Menu (not interesting at all)
//   Photon.Realtime: NetworkMoveSync        mscorlib: System.Object (engine/system: must be skipped)
// Nothing here proves anything about the real game. It only proves our scan code reads and prints correctly.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Type { std::string name; };
struct Field { std::string name; Type* type; size_t offset; int flags; };
struct Method { uint64_t methodPointer; std::string name; unsigned params; Type* ret; uint32_t flags; };   // first 8 bytes = pointer, like the real MethodInfo
struct Klass { std::string name, ns; Klass* parent; std::vector<Field> fields; std::vector<Method*> methods; };
struct Image { std::string name; std::vector<Klass*> classes; };

Type tInt{"System.Int32"}, tFloat{"System.Single"}, tVoid{"System.Void"}, tCC{"UnityEngine.CharacterController"}, tRB{"UnityEngine.Rigidbody"}, tBool{"System.Boolean"};
Klass kBehaviour{"MonoBehaviour", "UnityEngine", nullptr, {}, {}};
const uint64_t kBase = 0x10000000;       // pretend "where libil2cpp.so is mapped"

Method* mk(const char* n, unsigned p, Type* r, uint64_t rva, uint32_t fl = 0) { return new Method{kBase + rva, n, p, r, fl}; }

Klass kMove{"PlayerMovement", "Game", &kBehaviour,
    {{"walkSpeed", &tFloat, 0x20, 0}, {"jumpHeight", &tFloat, 0x24, 0}, {"controller", &tCC, 0x28, 0}, {"gravityScale", &tFloat, 0x30, 0}, {"instanceCount", &tInt, 0x0, 0x10}}, {}};
Klass kSettings{"GameSettings", "Game", nullptr, {{"speedMultiplier", &tFloat, 0x10, 0}, {"volume", &tFloat, 0x14, 0}}, {}};
Klass kBody{"PhysicsBody", "Game", &kBehaviour, {{"rb", &tRB, 0x18, 0}, {"mass", &tFloat, 0x20, 0}}, {}};
Klass kRemoveItem{"RemoveItem", "Game", nullptr, {{"slot", &tInt, 0x10, 0}}, {}};
Klass kRemoveMove{"RemoveMovementTag", "Game", nullptr, {{"tag", &tInt, 0x10, 0}}, {}};
Klass kHelper{"<>c__DisplayClass4_0", "Game", nullptr, {{"jumpSpeed", &tFloat, 0x10, 0}}, {}};
Klass kMenu{"Menu", "Game", nullptr, {{"flyingText", &tInt, 0x10, 0}, {"title", &tInt, 0x14, 0}}, {}};   // "flyingText" has "fly" in a FIELD name: should show as a field hit
Klass kNetMove{"NetworkMoveSync", "Photon", nullptr, {{"lag", &tFloat, 0x10, 0}}, {}};
Klass kObject{"Object", "System", nullptr, {{"speed", &tFloat, 0x0, 0}}, {}};

Image iGame{"Assembly-CSharp", {&kMove, &kSettings, &kBody, &kRemoveItem, &kRemoveMove, &kHelper, &kMenu}};
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
}
}  // namespace

extern "C" {
__attribute__((visibility("default"))) int fake_attach_count = 0;
__attribute__((visibility("default"))) int fake_detach_count = 0;
__attribute__((visibility("default"))) int fake_free_count = 0;
__attribute__((visibility("default"))) int fake_alloc_count = 0;
__attribute__((visibility("default"))) void fake_set_domain_null(int v) { gDomainNull = v != 0; }
__attribute__((visibility("default"))) unsigned long long fake_base() { return kBase; }

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
}
