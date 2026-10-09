// PC test of the "Scan game code" reader (il2cpp_scan.cpp) against a PRETEND libil2cpp.so (fake_il2cpp.cpp).
// Proves the reading/printing logic. Does NOT prove anything about the real game.
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "il2cpp_scan.h"

static std::vector<std::string> gLines;
static void logFn(const char* fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    gLines.push_back(b);
}
static int pass = 0, failn = 0;
static void check(const char* what, bool ok) { if (ok) { ++pass; std::printf("  PASS  %s\n", what); } else { ++failn; std::printf("  FAIL  %s\n", what); } }
static bool has(const char* needle) { for (const auto& l : gLines) if (l.find(needle) != std::string::npos) return true; return false; }
static int countOf(const char* needle) { int n = 0; for (const auto& l : gLines) if (l.find(needle) != std::string::npos) ++n; return n; }

int main(int, char** argv) {
    using namespace tzscan;
    // ---- the words
    check("class words: PlayerMovement matches", classNameMatches("PlayerMovement"));
    check("class words: JumpController matches", classNameMatches("JumpController"));
    check("class words: GravityZone matches", classNameMatches("GravityZone"));
    check("class words: RemoveItem does NOT match (remove != move)", !classNameMatches("RemoveItem"));
    check("class words: RemovedFlag does NOT match", !classNameMatches("RemovedFlag"));
    check("class words: RemoveMovementTag still matches", classNameMatches("RemoveMovementTag"));
    check("class words: Menu does not match", !classNameMatches("Menu"));
    check("class words: ScoreBoard does not match", !classNameMatches("ScoreBoard"));
    check("field words: walkSpeed matches", fieldNameMatches("walkSpeed"));
    check("field words: JumpHeight matches", fieldNameMatches("JumpHeight"));
    check("field words: gravityScale matches", fieldNameMatches("gravityScale"));
    check("field words: title does not match", !fieldNameMatches("title"));

    // ---- load the pretend runtime
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    auto fakeBase = reinterpret_cast<unsigned long long (*)()>(dlsym(lib, "fake_base"));
    auto setNull = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_set_domain_null"));
    int* attaches = static_cast<int*>(dlsym(lib, "fake_attach_count"));
    int* detaches = static_cast<int*>(dlsym(lib, "fake_detach_count"));
    int* frees = static_cast<int*>(dlsym(lib, "fake_free_count"));
    int* allocs = static_cast<int*>(dlsym(lib, "fake_alloc_count"));

    Api api; std::string missing;
    check("loadApi finds every needed function in the pretend runtime", loadApi(lib, &api, &missing) && missing.empty());
    check("loadApi also found the optional ones", api.field_get_flags && api.method_get_flags && api.il2cpp_free && api.thread_attach && api.thread_detach);

    // ---- a library that is NOT il2cpp (libc): loadApi must fail and say what is missing
    {
        void* libc = dlopen("libc.so.6", RTLD_NOW);
        Api a2; std::string miss2;
        const bool ok2 = libc && loadApi(libc, &a2, &miss2);
        check("loadApi fails on a library without the il2cpp functions", libc && !ok2);
        check("... and lists what is missing", miss2.find("il2cpp_domain_get") != std::string::npos && miss2.find("il2cpp_class_get_methods") != std::string::npos);
    }

    // ---- the real run
    const uintptr_t base = static_cast<uintptr_t>(fakeBase());
    const Summary s = run(api, base, logFn);
    for (const auto& l : gLines) std::printf("      | %s\n", l.c_str());
    check("run says ok", s.ok && s.error.empty());
    check("3 assemblies were looked at", s.assemblies == 3);
    check("engine/system assembly is skipped", has("scan: assembly mscorlib") && has("engine / system: skipped"));
    check("game assembly is marked fully scanned", has("scan: assembly Assembly-CSharp classes=7 (game code: fully scanned)"));
    check("the other game assembly is names only", has("scan: assembly Photon.Realtime classes=1 (other: names only)"));
    check("PlayerMovement is written out with its parent", has("scan: CLASS Game.PlayerMovement : MonoBehaviour   [assembly Assembly-CSharp]"));
    check("its float field walkSpeed shows type and offset", has("field walkSpeed : System.Single @32"));
    check("its jumpHeight field is shown", has("field jumpHeight : System.Single @36"));
    check("its CharacterController field is shown", has("field controller : UnityEngine.CharacterController @40"));
    check("a static field is marked static", has("field instanceCount : System.Int32 @0 static"));
    check("method Jump shows its parameter count and a rva", has("method Jump(1) : System.Void rva=1100"));
    check("method Update has rva 1000", has("method Update(0) : System.Void rva=1000"));
    check("a static method is marked static", has("method get_Instance(0) : System.Void rva=1300 static"));
    check("RemoveMovementTag (contains Movement) is written out", has("scan: CLASS Game.RemoveMovementTag"));
    check("RemoveItem is NOT written out", !has("CLASS Game.RemoveItem"));
    check("the compiler helper <>c__DisplayClass is skipped", !has("DisplayClass"));
    check("the other-assembly class NetworkMoveSync is written out", has("scan: CLASS Photon.NetworkMoveSync : -   [assembly Photon.Realtime]"));
    check("a speed field in a non-matching class is a field-hit", has("scan: field-hit Game.GameSettings.speedMultiplier : System.Single @16"));
    check("a 'fly' field in the Menu class is a field-hit", has("scan: field-hit Game.Menu.flyingText"));
    check("a Rigidbody-holding class is a type-hit", has("scan: type-hit Game.PhysicsBody.rb : UnityEngine.Rigidbody @24"));
    check("system object's 'speed' field is NOT reported (assembly skipped)", !has("System.Object") && !has("System.Object.speed"));
    check("thread list is written", has("kinds of threads in the game process"));
    check("DONE line with counts", has("scan: DONE. assemblies=3"));
    check("counts: 3 classes written out, 2 field hits, 1 type hit", s.matchedClasses == 3 && s.fieldHits == 2 && s.typeHits == 1);
    check("attached to the runtime and detached again", *attaches == 1 && *detaches == 1);
    check("every type name string was handed back with il2cpp_free (no leak)", *allocs > 10 && *frees == *allocs);

    // ---- no base address: method offsets come out as 0, nothing crashes
    gLines.clear();
    run(api, 0, logFn);
    check("without a base address the rva is 0 (honest, not invented)", has("method Jump(1) : System.Void rva=0"));

    // ---- the runtime is not ready yet (no domain)
    gLines.clear();
    setNull(1);
    const Summary s2 = run(api, base, logFn);
    check("no domain -> not ok, with a clear reason", !s2.ok && s2.error.find("not ready") != std::string::npos);
    check("... and it printed that reason", has("scan: the game's runtime is not ready yet"));
    check("... and it did not attach or read classes", countOf("scan: CLASS") == 0);
    setNull(0);

    std::printf("\npassed: %d  failed: %d\n", pass, failn);
    return failn == 0 ? 0 : 1;
}
