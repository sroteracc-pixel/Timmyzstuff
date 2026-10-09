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
static void dump() { for (const auto& l : gLines) std::printf("      | %s\n", l.c_str()); }

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
    check("index words: CharacterParameters and PlayerStatusSync match", indexNameMatches("CharacterParameters") && indexNameMatches("PlayerStatusSync"));
    check("index words: ScoreBoard and RemoveItem do not", !indexNameMatches("ScoreBoard") && !indexNameMatches("RemoveItem"));
    check("field words: walkSpeed matches", fieldNameMatches("walkSpeed"));
    check("field words: JumpHeight matches", fieldNameMatches("JumpHeight"));
    check("field words: gravityScale matches", fieldNameMatches("gravityScale"));
    check("field words: title does not match", !fieldNameMatches("title"));

    // ---- load the pretend runtime
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    auto codeAddr = reinterpret_cast<unsigned long long (*)()>(dlsym(lib, "fake_code_address"));
    auto setNull = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_set_domain_null"));
    auto makeLoco = reinterpret_cast<void* (*)(int)>(dlsym(lib, "fake_make_locomotion"));
    auto makeVertical = reinterpret_cast<void* (*)()>(dlsym(lib, "fake_make_vertical"));
    int* attaches = static_cast<int*>(dlsym(lib, "fake_attach_count"));
    int* detaches = static_cast<int*>(dlsym(lib, "fake_detach_count"));
    int* frees = static_cast<int*>(dlsym(lib, "fake_free_count"));
    int* allocs = static_cast<int*>(dlsym(lib, "fake_alloc_count"));
    Dl_info di; dladdr(reinterpret_cast<void*>(codeAddr()), &di);
    const unsigned long long fbase = reinterpret_cast<unsigned long long>(di.dli_fbase);
    char expectJump[96], expectUpdate[96], expectStatic[96];
    std::snprintf(expectJump, sizeof expectJump, "method Jump(1) : System.Void rva=%llx", codeAddr() + 0x1100 - fbase);
    std::snprintf(expectUpdate, sizeof expectUpdate, "method Update(0) : System.Void rva=%llx", codeAddr() + 0x1000 - fbase);
    std::snprintf(expectStatic, sizeof expectStatic, "method get_Instance(0) : System.Void rva=%llx static", codeAddr() + 0x1300 - fbase);

    Api api; std::string missing;
    check("loadApi finds every needed function in the pretend runtime", loadApi(lib, &api, &missing) && missing.empty());
    check("loadApi also found the optional ones", api.field_get_flags && api.method_get_flags && api.il2cpp_free && api.thread_attach && api.thread_detach &&
          api.class_instance_size && api.class_from_type && api.class_is_valuetype && api.class_is_enum && api.class_value_size);

    // ---- a library that is NOT il2cpp (libc): loadApi must fail and say what is missing
    {
        void* libc = dlopen("libc.so.6", RTLD_NOW);
        Api a2; std::string miss2;
        const bool ok2 = libc && loadApi(libc, &a2, &miss2);
        check("loadApi fails on a library without the il2cpp functions", libc && !ok2);
        check("... and lists what is missing", miss2.find("il2cpp_domain_get") != std::string::npos && miss2.find("il2cpp_class_get_methods") != std::string::npos);
    }

    // ---- running copies in ordinary memory: one real, one destroyed, one with an owner lock; and one vertical-motion object
    void* realLoco = makeLoco(0);
    void* deadLoco = makeLoco(1);
    void* lockedLoco = makeLoco(2);
    void* vertical = makeVertical();
    (void)realLoco; (void)deadLoco; (void)lockedLoco; (void)vertical;

    // ---- the real run
    const int attachBefore = *attaches, detachBefore = *detaches, allocBefore = *allocs, freeBefore = *frees;
    Options opt; opt.maxSeconds = 60;
    const Summary s = run(api, opt, logFn);
    dump();
    check("run says ok", s.ok && s.error.empty());
    check("3 assemblies were looked at", s.assemblies == 3);
    check("engine/system assembly is skipped", has("scan: assembly mscorlib classes=1 (engine / system: skipped)"));
    check("the game's own assembly is marked", has("scan: assembly Assembly-CSharp classes=11 (the game's own code)"));
    check("a third-party assembly is only named", has("scan: assembly Photon.Realtime classes=1 (other: names only)") && !has("CLASS Photon.NetworkMoveSync") && !has("index Photon"));

    std::printf("== index\n");
    check("index line for PlayerMovement with parent and counts", has("scan: index Game.PlayerMovement : MonoBehaviour [Assembly-CSharp] fields=5 methods=4"));
    check("index line for MobilePlayerLocomotion", has("scan: index Game.MobilePlayerLocomotion : LocomotionBase [Assembly-CSharp]"));
    check("index lines for the other important classes", has("scan: index Game.MobileVerticalMotion") && has("scan: index Game.CharacterWorldConstraints") && has("scan: index Game.PhysicsBody"));
    check("RemoveItem / Menu / GameSettings are not in the index", !has("index Game.RemoveItem") && !has("index Game.Menu") && !has("index Game.GameSettings"));
    check("the compiler helper <>c__DisplayClass is skipped", !has("DisplayClass"));
    check("7 index lines", s.indexed == 7 && countOf("scan: index ") == 7);

    std::printf("== detail\n");
    check("PlayerMovement is written out with its parent", has("scan: CLASS Game.PlayerMovement : MonoBehaviour   [assembly Assembly-CSharp]"));
    check("its float field walkSpeed shows type and offset", has("field walkSpeed : System.Single @32"));
    check("its CharacterController field is shown", has("field controller : UnityEngine.CharacterController @40"));
    check("a static field is marked static", has("field instanceCount : System.Int32 @0 static"));
    check("method Jump shows its parameter count and the REAL offset inside the library", has(expectJump));
    check("method Update offset", has(expectUpdate));
    check("a static method is marked static", has(expectStatic));
    check("the offsets summary counts them all as inside libil2cpp.so", has("scan: method code places: in-libil2cpp=") && has("other-library=0 outside-libraries=0 none=0"));
    check("RemoveMovementTag (contains Movement) is written out", has("scan: CLASS Game.RemoveMovementTag"));
    check("RemoveItem is NOT written out", !has("CLASS Game.RemoveItem"));
    check("MobilePlayerLocomotion is written out first-class", has("scan: CLASS Game.MobilePlayerLocomotion : LocomotionBase   [assembly Assembly-CSharp]") && has("method SetJumpHeight(1) : System.Void rva="));
    check("an important class is written out in FULL, no 60-field limit (70 fields)", has("field _pad69 : System.Int32 @292") && !has("(more fields not shown)"));
    check("6 classes written out in detail", s.matchedClasses == 6);
    check("a speed field in a non-matching class is a field-hit", has("scan: field-hit Game.GameSettings.speedMultiplier : System.Single @16"));
    check("a 'fly' field in the Menu class is a field-hit", has("scan: field-hit Game.Menu.flyingText"));
    check("a Rigidbody-holding class is a type-hit", has("scan: type-hit Game.PhysicsBody.rb : UnityEngine.Rigidbody @24"));
    check("system object's 'speed' field is NOT reported (assembly skipped)", !has("System.Object") && !has("System.Object.speed"));
    check("thread list is written", has("kinds of threads in the game process"));

    std::printf("== live values\n");
    check("memory search ran and says how much it read", has("scan: memory search read ") && has("regions in "));
    check("the real running copy is found: exactly 1 of the 3 hits looks real", has("scan: live Game.MobilePlayerLocomotion: ") && has("1 look like a real running copy"));
    check("destroyed and owner-locked fakes were rejected (reasons counted)", has("rejected: owner-lock=") && !has("owner-lock=0 destroyed=0"));
    check("the copy's header is written (size, native link set)", has("scan: live Game.MobilePlayerLocomotion #1 size=80 native-link=set in "));
    check("float field of the class itself", has("scan:   live _maxSpeed = 4.25   (Game.MobilePlayerLocomotion : System.Single @32)"));
    check("float field jumpHeight", has("live _jumpHeight = 1.5   ("));
    check("Vector3 field", has("live _velocity = (0.5, 0, -1.25)   ("));
    check("bool field", has("live _enabled = true   ("));
    check("enum field (with the size from the runtime)", has("live _mode = enum 2   ("));
    check("struct field as bytes", has("live _pair = bytes 0102030405060708   ("));
    check("reference field (set / null only)", has("live _target = set   ("));
    check("a field from the PARENT class is included", has("scan:   live _baseAccel = 2.5   (Game.LocomotionBase : System.Single @24)"));
    check("static and constant fields are NOT in the live list", !has("live DefaultSpeed") && !has("live Kind"));
    check("vertical-motion copy found (not a Unity object: no native link check)", has("scan: live Game.MobileVerticalMotion #1 size=24 native-link=n/a in "));
    check("its values", has("live _gravity = -9.81   (") && has("live _jumpSpeed = 3.5   ("));
    check("a class with no running copy says so", has("scan: live Game.PlayerMovement: ") && has(" hit(s), 0 look like a real running copy"));
    check("2 running copies written out in total", s.liveObjects == 2 && s.liveClasses == 4);
    check("memory bytes were counted", s.memoryBytes > 0);
    check("DONE line with counts", has("scan: DONE. assemblies=3") && has("live-copies=2"));
    check("attached to the runtime and detached again (once each)", *attaches - attachBefore == 1 && *detaches - detachBefore == 1);
    check("every type name string was handed back with il2cpp_free (no leak)", *allocs - allocBefore > 20 && (*frees - freeBefore) == (*allocs - allocBefore));

    // ---- the live search turned off
    gLines.clear();
    Options off; off.searchMemory = false;
    const Summary s2 = run(api, off, logFn);
    check("with the memory search off there is no live part", s2.ok && !has("--- live values") && s2.liveObjects == 0 && has("scan: CLASS Game.MobilePlayerLocomotion"));

    // ---- the time limit
    gLines.clear();
    Options zero; zero.maxSeconds = 0;
    run(api, zero, logFn);
    check("a time limit of 0 stops the memory search and says so", has("(STOPPED: time limit)"));

    // ---- missing optional functions: floats still work, strange types are honest about it
    gLines.clear();
    Api lean = api;
    lean.class_instance_size = nullptr; lean.class_from_type = nullptr; lean.class_is_valuetype = nullptr; lean.class_is_enum = nullptr; lean.class_value_size = nullptr;
    lean.field_get_flags = nullptr; lean.il2cpp_free = nullptr;
    const Summary s3 = run(lean, opt, logFn);
    check("without the optional functions the scan still works", s3.ok && has("scan: live Game.MobilePlayerLocomotion #1"));
    check("... plain numbers are still read", has("live _maxSpeed = 4.25   (") && has("live _velocity = (0.5, 0, -1.25)   ("));
    check("... types it cannot understand say so instead of guessing", has("live _mode = (type not understood)   ("));

    // ---- no base address: still resolves through the system (no invented numbers)
    gLines.clear();
    run(api, 0, logFn);
    check("the old two-argument call still works", has("method Jump(1) : System.Void rva="));

    // ---- the runtime is not ready yet (no domain)
    gLines.clear();
    setNull(1);
    const Summary s4 = run(api, opt, logFn);
    check("no domain -> not ok, with a clear reason", !s4.ok && s4.error.find("not ready") != std::string::npos);
    check("... and it printed that reason", has("scan: the game's runtime is not ready yet"));
    check("... and it did not read classes", countOf("scan: CLASS") == 0 && countOf("scan: index") == 0);
    setNull(0);

    // ---- a second real copy (another jump height): only the fields that differ are listed for it
    {
        gLines.clear();
        void* second = makeLoco(3); (void)second;
        const Summary s5 = run(api, opt, logFn);
        check("two real copies are found", s5.ok && has("scan: live Game.MobilePlayerLocomotion: ") && has("2 look like a real running copy"));
        check("copy #2 says it lists only the differences", has("scan: live Game.MobilePlayerLocomotion #2 size=80 native-link=set in ") && has("only the fields that differ from copy #1"));
        check("the differing field (jump 2.25) is written for copy #2", has("live _jumpHeight = 2.25   ("));
        check("the equal field _maxSpeed is written only once (copy #1)", countOf("live _maxSpeed = 4.25") == 1);
    }

    // ---- the copy pipe is only one page: the search must still work (the old code waited forever here)
    {
        gLines.clear();
        Options tiny = opt; tiny.testPipeBytes = 4096;
        const Summary s6 = run(api, tiny, logFn);
        check("a one-page pipe: scan ok, running copies still found", s6.ok && !s6.memoryStuck && has("copy pipe=4096 bytes (chunk 4 KB)") && has("live _maxSpeed = 4.25   ("));
    }

    // ---- LAST: the memory read freezes (this leaves one frozen thread behind, so nothing else may follow except the "still stuck" check)
    {
        gLines.clear();
        Options hang = opt; hang.testHangAfterChunks = 2; hang.stallSeconds = 1;
        const Summary s7 = run(api, hang, logFn);
        check("a frozen read: the scan still ends ok and says the search is stuck", s7.ok && s7.memoryStuck && has("scan: MEMORY SEARCH STUCK and given up on (region ") && has("(live search stuck)"));
        check("... and the class lists were written", has("scan: CLASS Game.MobilePlayerLocomotion") && has("scan: DONE."));
        check("... and no live values were invented", s7.liveObjects == 0 && !has("scan:   live "));
        gLines.clear();
        const Summary s8 = run(api, opt, logFn);
        check("a second scan while the frozen one is still there says so instead of starting another", s8.ok && s8.memoryStuck && has("an earlier memory search is still stuck"));
    }

    std::printf("\npassed: %d  failed: %d\n", pass, failn);
    return failn == 0 ? 0 : 1;
}
