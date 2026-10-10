// il2cpp_scan.h - a READ-ONLY look at the game's own code, written into the facts file.
//
// WHAT THIS IS (plain words)
//   The game is made with Unity "IL2CPP". The game's runtime library (libil2cpp.so) can tell us the names
//   of the game's classes, their fields (with exact positions) and their methods. This scan asks it and writes down:
//     1. an INDEX line for every class whose name looks like movement / player / character / parameters ...
//     2. the full list of fields and methods of the important classes (MobilePlayerLocomotion and friends)
//     3. (stage D5) the LIVE values: it looks through the game's memory for the running copies ("instances") of those
//        classes and writes down what their fields hold right now (speeds, jump numbers, ...).
//   It changes NOTHING in the game. Every memory read is crash-proof (the kernel says "no" for a bad address).
//   It only runs when you press "Scan game code" on the Movement page.
//
//   Tested only against a pretend runtime on a PC. The first real run (stage D4) worked on the headset.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace tzscan {

typedef void (*LogFn)(const char* fmt, ...);

// The il2cpp functions we use (found by name in libil2cpp.so). The last ones are optional.
struct Api {
    void* (*domain_get)() = nullptr;
    void** (*domain_get_assemblies)(void* domain, size_t* size) = nullptr;
    void* (*assembly_get_image)(void* assembly) = nullptr;
    const char* (*image_get_name)(void* image) = nullptr;
    size_t (*image_get_class_count)(void* image) = nullptr;
    void* (*image_get_class)(void* image, size_t index) = nullptr;
    const char* (*class_get_name)(void* klass) = nullptr;
    const char* (*class_get_namespace)(void* klass) = nullptr;
    void* (*class_get_parent)(void* klass) = nullptr;
    void* (*class_get_fields)(void* klass, void** iter) = nullptr;
    const char* (*field_get_name)(void* field) = nullptr;
    void* (*field_get_type)(void* field) = nullptr;
    size_t (*field_get_offset)(void* field) = nullptr;
    void* (*class_get_methods)(void* klass, void** iter) = nullptr;
    const char* (*method_get_name)(void* method) = nullptr;
    uint32_t (*method_get_param_count)(void* method) = nullptr;
    void* (*method_get_return_type)(void* method) = nullptr;
    char* (*type_get_name)(void* type) = nullptr;
    // optional
    int (*field_get_flags)(void* field) = nullptr;
    uint32_t (*method_get_flags)(void* method, uint32_t* iflags) = nullptr;
    void (*il2cpp_free)(void* p) = nullptr;
    void* (*thread_attach)(void* domain) = nullptr;
    void (*thread_detach)(void* thread) = nullptr;
    // optional, for the live values (without them the live part still works for the common number types)
    int32_t (*class_instance_size)(void* klass) = nullptr;
    void* (*class_from_type)(void* type) = nullptr;
    bool (*class_is_valuetype)(void* klass) = nullptr;
    bool (*class_is_enum)(void* klass) = nullptr;
    int32_t (*class_value_size)(void* klass, uint32_t* align) = nullptr;
    // optional, stage D7: only LOOKED FOR and reported (the ball scan says whether they exist); nothing calls them yet
    void* (*runtime_invoke)(void* method, void* obj, void** params, void** exc) = nullptr;
    void* (*class_get_method_from_name)(void* klass, const char* name, int argsCount) = nullptr;
    void* (*resolve_icall)(const char* name) = nullptr;
    // optional, stage D8: "is the calling thread known to the game's runtime?" (null result = no, the thread must not call into the game)
    void* (*thread_current)() = nullptr;
};

// Looks the functions up in an already loaded libil2cpp.so. `missing` lists required ones that were not found.
bool loadApi(void* lib, Api* out, std::string* missing);

// What the report is about. Movement = the headset movement classes (stages D4 - D6). Shot = balls, hoops, rims and shots (stage D7, for the Aimbot).
enum class Topic { Movement, Shot };

struct Options {
    Topic topic = Topic::Movement;
    uintptr_t libBase = 0;        // where libil2cpp.so is mapped (only used when the system cannot tell which library an address is in)
    bool searchMemory = true;     // look for the running copies of the important classes and print their field values
    int maxSeconds = 90;          // the memory search stops after this long
    bool brief = false;           // short report: only the important classes (full detail) + the live values; no index, no single-line hits
    int stallSeconds = 10;        // ... and is given up on when it makes no progress at all for this long (a stuck read)
    // ball-and-hoops report only (stage D7c):
    int liveDelaySeconds = 0;     // wait this long before looking at the running objects (time to close the menu, pick up a ball and shoot)
    bool shotIndex = false;       // write the (long) class-name index; the earlier report already has it
    bool listAssemblies = true;   // one line per assembly
    // the size budget of the report (the facts file that reaches the person is cut at about 265 KB). maxBytes = 0: no byte budget.
    int maxLines = 7000, reservedLines = 80;
    size_t maxBytes = 0, reservedBytes = 0;
    // only for the PC tests (0 = off): pretend the memory read gets stuck after this many chunks / make the copy pipe this small
    int testHangAfterChunks = 0;
    size_t testPipeBytes = 0;
};

struct Summary {
    bool ok = false;
    int assemblies = 0, classes = 0, matchedClasses = 0, fieldHits = 0, typeHits = 0;
    int indexed = 0;              // index lines written
    int liveClasses = 0;          // important classes that were looked for in memory
    int liveObjects = 0;          // running copies found (and printed)
    unsigned long long memoryBytes = 0;   // how much memory was read during the search
    bool memoryStuck = false;     // the memory search got stuck and was given up on (the class lists are still good)
    std::string error;
};

// Which step the scan is in right now (a short text; "finished" at the end). Safe to call from another thread.
const char* currentStep();

// Runs the scan.
Summary run(const Api& api, const Options& options, LogFn log);
Summary run(const Api& api, uintptr_t libBase, LogFn log);      // same, with default options

// ---- stage D6: finding ONE class and its running copies again, quietly (nothing is written to the facts file)
struct FieldInfo { std::string name, typeName; int offset = 0; };       // offset counts from the start of the object (the 16-byte header is included)
struct ClassInfo {
    bool found = false;
    std::string fullName, error;
    uint64_t klassInv = 0;            // the class pointer, inverted, so that a copy of it in our own memory never looks like a running object
    int size = 0;                     // size of one object, header included
    bool unityObject = false;
    std::vector<FieldInfo> fields;    // the class's OWN instance fields (not static, not inherited)
    const FieldInfo* field(const char* name) const;
    void* klass() const { return reinterpret_cast<void*>(static_cast<uintptr_t>(~klassInv)); }     // the class pointer (stage D8: needed to look up methods)
};
// Looks the class up by namespace + name in the game's own code. The calling thread is attached to the runtime for the call.
ClassInfo findClass(const Api& api, const char* ns, const char* name);

// stage D8: the raw class handle of ANY class (the engine's UnityEngine.Rigidbody too) by namespace + name; nullptr (and `error` filled) when there is none.
void* findClassHandle(const Api& api, const char* ns, const char* name, std::string* error);

struct Copy { uintptr_t addr = 0; std::vector<unsigned char> bytes; };
struct CopySearch {
    bool ok = false;                  // the search ran to its end
    bool stuck = false;               // it stopped answering and was given up on (or an earlier one still is)
    bool busy = false;                // ... because an earlier search is still stuck, so nothing was started
    std::string error;
    std::vector<Copy> copies;         // running copies that look like real objects (owner lock free, native link set), at most maxCopies
    int hits = 0, accepted = 0;
    unsigned long long bytesRead = 0; double seconds = 0;
};
// Looks through the game's memory for the running copies of `cls` (on its own watched thread: this call always comes back).
CopySearch findCopies(const ClassInfo& cls, int maxCopies, int maxSeconds, int stallSeconds, int testHangAfterChunks = 0, int testPollMicros = 0);

// The words used to pick classes / fields (exposed for the test).
bool classNameMatches(const std::string& name);       // movement-ish class names
bool indexNameMatches(const std::string& name);       // broader: also player / character / parameters ...
bool fieldNameMatches(const std::string& name);
bool shotNameMatches(const std::string& name);        // stage D7: classes about balls, hoops, rims, shots, throws, grabbing
bool shotTargetName(const std::string& name);                                 // is this one of the exact ball / hoop / shot-assist classes that are written out in full?
bool shotLiveWanted(const std::string& name, bool ours, const std::string& parent);   // is a running copy of this class searched for?

}  // namespace tzscan
