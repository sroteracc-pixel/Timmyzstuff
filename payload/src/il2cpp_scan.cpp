// il2cpp_scan.cpp - see il2cpp_scan.h.
#include "il2cpp_scan.h"
#include "safe_copy.h"

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace tzscan {

namespace {

// ---- limits (the facts file must stay a size you can send)
const int kMaxLines = 7000;
const int kReservedLines = 80;       // always kept free for the important summary lines (live values, stuck / done messages)
const int kMaxPriorityDetail = 40;     // important classes written out in full (no field / method limit)
const int kMaxOtherDetail = 70;        // other movement-ish classes written out (with limits)
const int kMaxFieldsPerClass = 60;
const int kMaxMethodsPerClass = 90;
const int kMaxFieldsPriority = 400;
const int kMaxMethodsPriority = 600;
const int kMaxIndexLines = 1500;       // one line per class, names only
const int kMaxFieldHits = 400;         // single "this field looks interesting" lines
const int kMaxTypeHits = 150;          // "this class holds a Rigidbody / CharacterController" lines
const int kMaxLiveTargets = 12;        // classes looked for in memory
const int kMaxLivePrinted = 2;         // running copies written out per class (the ones that look most like a real, healthy object)
const int kMaxLiveRead = 64;           // running copies read (then ranked) per class
const int kMaxLiveFields = 300;
const int kMaxCandidates = 20000;
const size_t kChunk = 65536;

// ---- the classes we care most about (found by the stage D4 scan of the real game)
const char* const kPriorityClasses[] = {"MobilePlayerLocomotion", "MobileVerticalMotion", "CharacterVerticalState", "CharacterWorldConstraints",
                                        "PlayerStatusSync", "CollisionVolume", "BodyCollider", nullptr};
const char* const kLiveClasses[] = {"MobilePlayerLocomotion", "MobileVerticalMotion", "CharacterWorldConstraints", "SimpleCapsuleWithStickMovement",
                                    "PlayerMovement", "Movement", "LocomotionController", "PlayerStatusSync", "BodyCollider", nullptr};
// Stage D5c ("brief" scan): the second real scan showed that the HEADSET version uses ShovelTools.PlayerLocomotion (211 fields), while
// MobilePlayerLocomotion is the phone / PC version. These are written out in full and looked for in memory.
const char* const kPriorityBrief[] = {"PlayerLocomotion", "ClientBotLocomotionParameters", "ClientBotLocomotion", "MotionHandoff", "MotionMechanics",
                                      "LocomotionAnimation", "SetJumpHeight", "WorldSpaceGravity", "CustomCenterOfGravity", nullptr};
const char* const kLiveBrief[] = {"PlayerLocomotion", "ClientBotLocomotionParameters", "LocomotionManager", "SetJumpHeight", "WorldSpaceGravity",
                                  "CustomCenterOfGravity", "MotionHandoff", "MotionMechanics", "PlanarLocomotion", "LocomotionAnimation", nullptr};

std::string lowerOf(const char* s) {
    std::string r = s ? s : "";
    for (char& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}
bool hasAny(const std::string& hay, const char* const* words) {
    for (int i = 0; words[i]; ++i) if (hay.find(words[i]) != std::string::npos) return true;
    return false;
}
bool inList(const std::string& name, const char* const* list) {
    for (int i = 0; list[i]; ++i) if (name == list[i]) return true;
    return false;
}
bool startsWith(const std::string& s, const char* prefix) { return s.compare(0, std::strlen(prefix), prefix) == 0; }

// Assemblies we do not need to look through (the engine and the .NET base library).
bool skipAssembly(const std::string& n) {
    static const char* prefixes[] = {"mscorlib", "System", "Mono.", "netstandard", "UnityEngine", "Unity.TextMeshPro", "Unity.Mathematics", "Unity.Burst",
                                     "Unity.Collections", "Unity.Jobs", "Unity.Timeline", "Newtonsoft", "I18N", "Unity.Serialization", "nunit", "ICSharpCode", nullptr};
    for (int i = 0; prefixes[i]; ++i) if (startsWith(n, prefixes[i])) return true;
    return false;
}
// The game's OWN code (the stage D4 scan showed: Assembly-CSharp.dll, IRL.*.dll, com.irl.*.dll).
bool oursAssembly(const std::string& n) { return startsWith(n, "Assembly-CSharp") || startsWith(n, "IRL.") || startsWith(n, "com.irl."); }

// ---- stage D7: which classes are about balls, hoops, rims and shots?
std::vector<std::string> camelTokens(const std::string& name) {
    std::vector<std::string> t; std::string cur;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool upper = c >= 'A' && c <= 'Z', alpha = upper || (c >= 'a' && c <= 'z');
        const bool prevLower = i > 0 && name[i - 1] >= 'a' && name[i - 1] <= 'z';
        if (!alpha || (upper && prevLower)) { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
        if (alpha) cur += static_cast<char>(upper ? c - 'A' + 'a' : c);
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

}  // namespace

bool shotNameMatches(const std::string& name) {
    const std::string l = lowerOf(name.c_str());
    static const char* otherSports[] = {"football", "baseball", "softball", "soccer", "paintball", "dodgeball", "volleyball", "tennis", "boxing", "golf", nullptr};
    if (hasAny(l, otherSports) && l.find("basket") == std::string::npos) return false;          // the other sports are not what we look for
    static const char* longWords[] = {"basket", "hoop", "backboard", "swish", "dunk", "shoot", "throw", "rebound", "grabbable", "court", nullptr};
    if (hasAny(l, longWords)) return true;
    static const char* tokenWords[] = {"ball", "rim", "goal", "shot", "score", "net", "release", "pass", "catch", "aim", nullptr};      // short words count only as a whole part of the name ("Primary" is not "rim")
    for (const std::string& t : camelTokens(name)) if (inList(t, tokenWords)) return true;
    return false;
}

namespace {

class Out {
public:
    // maxBytes = 0: no byte budget. Otherwise the ordinary lines stop at (maxBytes - reservedBytes) and the important ones at maxBytes
    // (the facts file that reaches the person must stay below a size limit).
    Out(LogFn log, int maxLines = kMaxLines, int reservedLines = kReservedLines, size_t maxBytes = 0, size_t reservedBytes = 0)
        : log_(log), maxLines_(maxLines), reservedLines_(reservedLines), maxBytes_(maxBytes), reservedBytes_(reservedBytes) {}
    // an ordinary line: dropped once the file is nearly full
    void line(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (full()) return;
        char b[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (maxBytes_ && bytes_ + std::strlen(b) + 1 + reservedBytes_ > maxBytes_) return;
        put(b);
    }
    // an important line (headers, summaries, "stuck", "DONE"): still written when the ordinary lines have used up their room
    void key(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (lines_ >= maxLines_ || (maxBytes_ && bytes_ >= maxBytes_)) return;
        char b[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (maxBytes_ && bytes_ + std::strlen(b) + 1 > maxBytes_) return;           // never go over the byte budget, not even by one line
        put(b);
    }
    // the closing line ("DONE" / "STUCK"): always written, even when the budget is used up (it may go over by a small, fixed margin)
    void last(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        put(b);
    }
    bool full() const { return lines_ >= maxLines_ - reservedLines_ || (maxBytes_ && bytes_ + reservedBytes_ >= maxBytes_); }
private:
    void put(const char* b) { log_("%s", b); ++lines_; bytes_ += std::strlen(b) + 1; }       // never use game text as a format string
    LogFn log_; int lines_ = 0; int maxLines_, reservedLines_; size_t bytes_ = 0, maxBytes_, reservedBytes_;
};

std::string typeName(const Api& api, void* type) {
    if (!type || !api.type_get_name) return "?";
    char* s = api.type_get_name(type);
    if (!s) return "?";
    std::string r = s;
    if (api.il2cpp_free) api.il2cpp_free(s);
    return r;
}

// thread names (so a later step knows which thread is the game's main thread)
void listThreads(Out& out) {
    std::map<std::string, std::pair<int, int>> names;       // name -> (count, first tid)
    DIR* d = opendir("/proc/self/task");
    if (!d) { out.line("scan: could not list threads"); return; }
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[320]; std::snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
        FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char nm[64] = {0};
        if (std::fgets(nm, sizeof nm, f)) { size_t n = std::strlen(nm); while (n && (nm[n - 1] == '\n' || nm[n - 1] == '\r')) nm[--n] = 0; }
        std::fclose(f);
        auto& slot = names[nm];
        if (slot.first == 0) slot.second = std::atoi(e->d_name);
        ++slot.first;
    }
    closedir(d);
    out.line("scan: %d kinds of threads in the game process (name x count, first thread id):", static_cast<int>(names.size()));
    std::string line;
    for (const auto& kv : names) {
        char b[96]; std::snprintf(b, sizeof b, "%s x%d (%d)  ", kv.first.c_str(), kv.second.first, kv.second.second);
        line += b;
        if (line.size() > 150) { out.line("scan: threads: %s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) out.line("scan: threads: %s", line.c_str());
}

// ---- crash-proof memory reads: see safe_copy.h
CopyPipe& pointerPipe() {                  // the small reads of the code step (8 bytes at a time); the memory search has its own pipe
    static CopyPipe p; static bool tried = false, ok = false;
    if (!tried) { tried = true; ok = p.open(0, nullptr); }
    static CopyPipe none;                  // never opened: every copy on it just says "no"
    return ok ? p : none;
}
bool readPtr(uintptr_t from, uint64_t* out) { return pointerPipe().copy(from, reinterpret_cast<unsigned char*>(out), 8); }

// ---- where does a method's code live? (the system knows which library an address is in)
enum PtrKind { P_NONE = 0, P_IL2CPP = 1, P_OTHERLIB = 2, P_OUTSIDE = 3 };
PtrKind classifyPointer(uint64_t ptr, uintptr_t libBase, unsigned long* rva, std::string* lib) {
    *rva = 0; lib->clear();
    if (ptr == 0) return P_NONE;
    Dl_info di; std::memset(&di, 0, sizeof di);
    if (dladdr(reinterpret_cast<void*>(ptr), &di) && di.dli_fbase && ptr >= reinterpret_cast<uint64_t>(di.dli_fbase)) {
        *rva = static_cast<unsigned long>(ptr - reinterpret_cast<uint64_t>(di.dli_fbase));
        const char* f = di.dli_fname ? di.dli_fname : "";
        const char* slash = std::strrchr(f, '/');
        *lib = slash ? slash + 1 : f;
        return *lib == "libil2cpp.so" ? P_IL2CPP : P_OTHERLIB;
    }
    if (libBase && ptr > libBase && ptr - libBase < (1ULL << 31)) { *rva = static_cast<unsigned long>(ptr - libBase); *lib = "libil2cpp.so"; return P_IL2CPP; }
    return P_OUTSIDE;
}

// ---- what a field holds (decided while the il2cpp runtime can still be asked, used later when printing live values)
enum Kind { K_UNKNOWN, K_FLOAT, K_DOUBLE, K_I8, K_U8, K_I16, K_U16, K_I32, K_U32, K_I64, K_U64, K_BOOL, K_VEC2, K_VEC3, K_VEC4, K_ENUM, K_STRUCT, K_REF };
struct FieldPlan { std::string name, typeName, owner; int offset = 0; Kind kind = K_UNKNOWN; int size = 0; };

Kind kindOf(const Api& api, void* type, const std::string& tn, int* size) {
    struct Known { const char* name; Kind kind; int size; };
    static const Known known[] = {
        {"System.Single", K_FLOAT, 4}, {"System.Double", K_DOUBLE, 8}, {"System.SByte", K_I8, 1}, {"System.Byte", K_U8, 1}, {"System.Int16", K_I16, 2},
        {"System.UInt16", K_U16, 2}, {"System.Int32", K_I32, 4}, {"System.UInt32", K_U32, 4}, {"System.Int64", K_I64, 8}, {"System.UInt64", K_U64, 8},
        {"System.Boolean", K_BOOL, 1}, {"UnityEngine.Vector2", K_VEC2, 8}, {"UnityEngine.Vector3", K_VEC3, 12}, {"UnityEngine.Vector4", K_VEC4, 16},
        {"UnityEngine.Quaternion", K_VEC4, 16}, {"UnityEngine.Color", K_VEC4, 16}, {nullptr, K_UNKNOWN, 0}};
    for (int i = 0; known[i].name; ++i) if (tn == known[i].name) { *size = known[i].size; return known[i].kind; }
    if (api.class_from_type && api.class_is_valuetype) {
        void* k = api.class_from_type(type);
        if (k) {
            if (!api.class_is_valuetype(k)) { *size = 8; return K_REF; }
            uint32_t align = 0;
            const int32_t sz = api.class_value_size ? api.class_value_size(k, &align) : 0;
            if (api.class_is_enum && api.class_is_enum(k) && (sz == 1 || sz == 2 || sz == 4 || sz == 8)) { *size = sz; return K_ENUM; }
            if (sz > 0 && sz <= 64) { *size = sz; return K_STRUCT; }
        }
    }
    *size = 0;
    return K_UNKNOWN;
}

std::string fmtFloat(float f) { char b[40]; if (std::isfinite(f)) std::snprintf(b, sizeof b, "%.4g", static_cast<double>(f)); else std::snprintf(b, sizeof b, "nan/inf"); return b; }

std::string formatValue(const FieldPlan& p, const unsigned char* buf, size_t avail) {
    if (p.kind == K_UNKNOWN) return "(type not understood)";
    if (p.size <= 0 || p.offset < 0 || static_cast<size_t>(p.offset) + static_cast<size_t>(p.size) > avail) return "(outside the copy)";
    const unsigned char* q = buf + p.offset;
    char b[160];
    switch (p.kind) {
        case K_FLOAT: { float f; std::memcpy(&f, q, 4); return fmtFloat(f); }
        case K_DOUBLE: { double d; std::memcpy(&d, q, 8); std::snprintf(b, sizeof b, "%.6g", d); return b; }
        case K_I8: std::snprintf(b, sizeof b, "%d", static_cast<int>(static_cast<int8_t>(q[0]))); return b;
        case K_U8: std::snprintf(b, sizeof b, "%u", static_cast<unsigned>(q[0])); return b;
        case K_I16: { int16_t v; std::memcpy(&v, q, 2); std::snprintf(b, sizeof b, "%d", static_cast<int>(v)); return b; }
        case K_U16: { uint16_t v; std::memcpy(&v, q, 2); std::snprintf(b, sizeof b, "%u", static_cast<unsigned>(v)); return b; }
        case K_I32: { int32_t v; std::memcpy(&v, q, 4); std::snprintf(b, sizeof b, "%d", static_cast<int>(v)); return b; }
        case K_U32: { uint32_t v; std::memcpy(&v, q, 4); std::snprintf(b, sizeof b, "%u", static_cast<unsigned>(v)); return b; }
        case K_I64: { int64_t v; std::memcpy(&v, q, 8); std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v)); return b; }
        case K_U64: { uint64_t v; std::memcpy(&v, q, 8); std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(v)); return b; }
        case K_BOOL: return q[0] == 0 ? "false" : (q[0] == 1 ? "true" : "?");
        case K_VEC2: case K_VEC3: case K_VEC4: {
            const int n = p.kind == K_VEC2 ? 2 : (p.kind == K_VEC3 ? 3 : 4);
            std::string s = "(";
            for (int i = 0; i < n; ++i) { float f; std::memcpy(&f, q + 4 * i, 4); if (i) s += ", "; s += fmtFloat(f); }
            return s + ")";
        }
        case K_ENUM: {
            long long v = 0;
            if (p.size == 1) v = static_cast<int8_t>(q[0]);
            else if (p.size == 2) { int16_t x; std::memcpy(&x, q, 2); v = x; }
            else if (p.size == 4) { int32_t x; std::memcpy(&x, q, 4); v = x; }
            else { int64_t x; std::memcpy(&x, q, 8); v = x; }
            std::snprintf(b, sizeof b, "enum %lld", v); return b;
        }
        case K_STRUCT: {
            std::string s = "bytes ";
            const int n = p.size < 16 ? p.size : 16;
            for (int i = 0; i < n; ++i) { std::snprintf(b, sizeof b, "%02x", static_cast<unsigned>(q[i])); s += b; }
            if (p.size > 16) s += "...";
            return s;
        }
        case K_REF: { uint64_t v; std::memcpy(&v, q, 8); return v == 0 ? "null" : "set"; }
        default: return "(type not understood)";
    }
}

// ---- reading the list of memory regions
struct MapEntry { uintptr_t start = 0, end = 0; bool rw = false; std::string path; };
std::vector<MapEntry> readMaps() {
    std::vector<MapEntry> out;
    FILE* m = std::fopen("/proc/self/maps", "r");
    if (!m) return out;
    char line[1024];
    while (std::fgets(line, sizeof line, m)) {
        unsigned long a = 0, b = 0; char perms[8] = {0}; int pos = 0;
        if (std::sscanf(line, "%lx-%lx %7s %*s %*s %*s %n", &a, &b, perms, &pos) < 3) continue;
        MapEntry e; e.start = a; e.end = b; e.rw = (perms[0] == 'r' && perms[1] == 'w');
        e.path = pos > 0 ? std::string(line + pos) : std::string();
        while (!e.path.empty() && (e.path.back() == '\n' || e.path.back() == '\r' || e.path.back() == ' ')) e.path.pop_back();
        out.push_back(e);
    }
    std::fclose(m);
    return out;
}
// The game's running objects live in ordinary (anonymous) memory: not in files, graphics memory or thread stacks.
bool regionWanted(const MapEntry& r) {
    if (!r.rw || r.end - r.start > (1UL << 30)) return false;
    if (r.path.empty() || r.path == "[heap]") return true;
    if (startsWith(r.path, "[anon:")) {
        if (r.path.find("stack") != std::string::npos || r.path.find("guard") != std::string::npos || r.path.find(".bss") != std::string::npos ||
            r.path.find("signal") != std::string::npos || r.path.find("dalvik") != std::string::npos) return false;     // (the Java heap holds no Unity objects)
        return true;
    }
    return false;
}
double nowSeconds() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9; }

alignas(8) unsigned char gChunk[kChunk];      // the copy buffer for the memory search (its own memory is never searched)
std::atomic<bool> gSearchBusy{false};         // one memory search at a time (a stuck one keeps this set and keeps using gChunk)
std::atomic<unsigned long long> gBeat{0};     // ticks once for every bit of progress ANY search makes: a second search waits while this moves, and gives up when it stops
std::atomic<const char*> gStep{"not started"};

// the class pointer is kept inverted, so a leftover copy in freed memory can never look like a running object
struct ClassRef {
    uintptr_t inv = 0; int asmIndex = 0; std::string name, full;
    void set(void* k) { inv = ~reinterpret_cast<uintptr_t>(k); }
    void* klass() const { return reinterpret_cast<void*>(~inv); }
};
struct AsmRef { std::string name; bool skip, ours; };
struct LivePlan { uint64_t klassInv; std::string full; bool unityObject; int size; std::vector<FieldPlan> fields; bool filterShot = false; };   // klassInv = ~klass, so our own lists never look like a live object
struct LiveObject { int plan; uintptr_t addr; std::string where; std::vector<unsigned char> bytes; };

// ---- the memory search runs on its OWN thread, so a read that never returns cannot freeze the scan:
// the scan thread watches a heartbeat and gives up on the search if it stops.
struct PlanLite { uint64_t klassInv; bool unityObject; int size; };
struct Cand { int plan; uintptr_t addr; int region; };
struct SearchJob {
    // inputs (set before the thread starts, never changed afterwards)
    std::vector<PlanLite> plans;
    std::vector<MapEntry> maps;
    int maxSeconds = 90, testHangAfterChunks = 0; size_t testPipeBytes = 0; int testPollMicros = 0;
    // progress (the search thread writes, the scan thread reads)
    std::atomic<unsigned> heartbeat{0};
    std::atomic<unsigned long> region{0}, regionsRead{0};
    std::atomic<unsigned long long> bytes{0};
    std::atomic<int> stage{0};                 // 0 starting, 1 reading memory, 2 checking the hits, 3 finished
    std::atomic<bool> done{false};
    // results (only to be looked at after done == true)
    bool timedOut = false, pipeFailed = false; size_t chunkBytes = 0, pipeBytes = 0; int pipeTrouble = 0, pipeErrno = 0;
    int cands = 0, rejMonitor = 0, rejCached = 0, rejUnreadable = 0; double seconds = 0;
    std::vector<int> candidatesPer, acceptedPer;
    std::vector<LiveObject> found;
};

bool isUnityObjectClass(const Api& api, void* klass) {
    int guard = 0;
    for (void* p = api.class_get_parent(klass); p && guard < 32; p = api.class_get_parent(p), ++guard) {
        const char* n = api.class_get_name(p);
        const char* ns = api.class_get_namespace ? api.class_get_namespace(p) : nullptr;
        if (!n) break;
        const std::string name = n;
        if (name == "MonoBehaviour" || name == "Behaviour" || name == "Component" || name == "ScriptableObject") return true;
        if (name == "Object" && ns && std::string(ns) == "UnityEngine") return true;
    }
    return false;
}

void addInstanceFields(const Api& api, void* klass, const std::string& owner, std::vector<FieldPlan>* plan) {
    void* it = nullptr; int n = 0;
    while (void* f = api.class_get_fields(klass, &it)) {
        if (++n > 400 || static_cast<int>(plan->size()) >= kMaxLiveFields) break;
        const int flags = api.field_get_flags ? api.field_get_flags(f) : 0;
        if (flags & (0x10 | 0x40)) continue;                              // static / constant: not stored in the object
        const int off = static_cast<int>(api.field_get_offset(f));
        if (off < 16) continue;                                           // instance fields always come after the 16-byte object header
        FieldPlan p; const char* fn = api.field_get_name(f);
        p.name = fn ? fn : "?"; p.owner = owner; p.offset = off;
        void* type = api.field_get_type(f);
        p.typeName = typeName(api, type);
        p.kind = kindOf(api, type, p.typeName, &p.size);
        plan->push_back(p);
    }
}


// How healthy does a copy look? A real running object has sensible numbers; a freed or half-overwritten block has garbage
// (tiny denormal floats, booleans that are neither 0 nor 1, pointers that are not 8-aligned ...). Used to pick the copies worth writing out.
struct Sanity { int ok = 0, total = 0, nonzero = 0; int percent() const { return total ? (100 * ok) / total : 0; } };
Sanity sanityOf(const LivePlan& p, const std::vector<unsigned char>& bytes) {
    Sanity r;
    auto saneF = [](float v) { if (!std::isfinite(v)) return false; const float a = std::fabs(v); return a == 0.0f || (a >= 1e-6f && a <= 1e7f); };
    for (const FieldPlan& f : p.fields) {
        if (f.kind == K_UNKNOWN || f.size <= 0 || f.offset < 0 || static_cast<size_t>(f.offset) + static_cast<size_t>(f.size) > bytes.size()) continue;
        const unsigned char* q = bytes.data() + f.offset;
        bool ok = true, nz = false;
        switch (f.kind) {
            case K_FLOAT: { float v; std::memcpy(&v, q, 4); ok = saneF(v); nz = v != 0.0f; break; }
            case K_VEC2: case K_VEC3: case K_VEC4: {
                const int n = f.kind == K_VEC2 ? 2 : (f.kind == K_VEC3 ? 3 : 4);
                for (int i = 0; i < n; ++i) { float v; std::memcpy(&v, q + 4 * i, 4); if (!saneF(v)) ok = false; if (v != 0.0f) nz = true; }
                break;
            }
            case K_DOUBLE: { double v; std::memcpy(&v, q, 8); ok = std::isfinite(v) && std::fabs(v) < 1e12; nz = v != 0.0; break; }
            case K_BOOL: ok = q[0] <= 1; nz = q[0] != 0; break;
            case K_ENUM: {
                long long v = 0;
                if (f.size == 1) v = static_cast<int8_t>(q[0]); else if (f.size == 2) { int16_t x; std::memcpy(&x, q, 2); v = x; }
                else if (f.size == 4) { int32_t x; std::memcpy(&x, q, 4); v = x; } else { int64_t x; std::memcpy(&x, q, 8); v = x; }
                ok = v > -100000 && v < 100000; nz = v != 0; break;
            }
            case K_REF: { uint64_t v; std::memcpy(&v, q, 8); ok = v == 0 || ((v & 7) == 0 && v >= 0x10000 && v < (1ULL << 47)); nz = v != 0; break; }
            default: continue;                                  // whole numbers and structs: any value can be right, so they do not vote
        }
        ++r.total; if (ok) ++r.ok; if (nz) ++r.nonzero;
    }
    return r;
}


// The memory search itself. It runs on its own thread (see run()). Everything it touches is in `j` or in gChunk.
void searchMemory(SearchJob& j) {
    struct Finish { SearchJob& j; double t0; ~Finish() { j.seconds = nowSeconds() - t0; j.stage.store(3); j.done.store(true); gSearchBusy.store(false); } } finish{j, nowSeconds()};
    const double t0 = finish.t0;
    j.candidatesPer.assign(j.plans.size(), 0); j.acceptedPer.assign(j.plans.size(), 0);
    CopyPipe pipe;
    if (!pipe.open(j.testPipeBytes, gChunk)) { j.pipeFailed = true; j.pipeErrno = pipe.lastErrno(); return; }
    j.chunkBytes = pipe.chunk(); j.pipeBytes = pipe.pipeBytes();

    uint32_t hi[kMaxLiveTargets]; int nhi = 0;
    for (const PlanLite& p : j.plans) { if (nhi < kMaxLiveTargets) hi[nhi++] = static_cast<uint32_t>((~p.klassInv) >> 32); }
    std::vector<Cand> cands;
    const uintptr_t bufLo = reinterpret_cast<uintptr_t>(gChunk);
    unsigned chunks = 0;
    j.stage.store(1);

    auto scanBytes = [&](const unsigned char* buf, size_t n, uintptr_t base, int region) {
        const uint64_t* words = reinterpret_cast<const uint64_t*>(buf);
        for (size_t o = 0; o < n / 8; ++o) {
            const uint64_t v = words[o];
            const uint32_t top = static_cast<uint32_t>(v >> 32);
            bool near = false;
            for (int h = 0; h < nhi; ++h) if (hi[h] == top) { near = true; break; }
            if (!near) continue;
            for (size_t t = 0; t < j.plans.size(); ++t)
                if (~v == j.plans[t].klassInv && static_cast<int>(cands.size()) < kMaxCandidates) cands.push_back({static_cast<int>(t), base + o * 8, region});
        }
    };

    for (size_t ri = 0; ri < j.maps.size() && !j.timedOut; ++ri) {
        const MapEntry& r = j.maps[ri];
        if (!regionWanted(r) || (bufLo >= r.start && bufLo < r.end)) continue;
        j.region.store(ri); j.regionsRead.fetch_add(1);
        for (uintptr_t c = r.start; c < r.end; c += pipe.chunk()) {
            if (nowSeconds() - t0 >= j.maxSeconds) { j.timedOut = true; break; }
            j.heartbeat.fetch_add(1); gBeat.fetch_add(1);
            if (j.testHangAfterChunks > 0 && static_cast<int>(chunks) >= j.testHangAfterChunks) { for (;;) sleep(1000); }      // PC test only: a read that never returns
            const size_t n = r.end - c < pipe.chunk() ? static_cast<size_t>(r.end - c) : pipe.chunk();
            if (pipe.copy(c, gChunk, n)) { j.bytes.fetch_add(n); scanBytes(gChunk, n, c, static_cast<int>(ri)); }
            else if (n > 4096) {                                  // part of the chunk can not be read: keep the pages that can
                for (size_t p = 0; p < n; p += 4096) {
                    const size_t m = n - p < 4096 ? n - p : 4096;
                    if (pipe.copy(c + p, gChunk + p, m)) { j.bytes.fetch_add(m); scanBytes(gChunk + p, m, c + p, static_cast<int>(ri)); }
                }
            }
            if ((++chunks & 31) == 0) usleep(1000);              // be gentle: the game keeps running while we read
        }
    }

    j.stage.store(2);
    j.cands = static_cast<int>(cands.size());
    for (const Cand& cd : cands) {
        j.heartbeat.fetch_add(1); gBeat.fetch_add(1);
        ++j.candidatesPer[cd.plan];
        const PlanLite& p = j.plans[cd.plan];
        unsigned char hdr[24];
        if (!pipe.copy(cd.addr, hdr, sizeof hdr)) { ++j.rejUnreadable; continue; }
        { uint64_t k; std::memcpy(&k, hdr, 8); k = ~k; std::memcpy(hdr, &k, 8); }          // our own copy must never look like a running object to a later search
        uint64_t monitor, cached; std::memcpy(&monitor, hdr + 8, 8); std::memcpy(&cached, hdr + 16, 8);
        if (monitor != 0) { ++j.rejMonitor; continue; }
        if (p.unityObject && (cached == 0 || (cached & 7) != 0)) { ++j.rejCached; continue; }     // a destroyed or fake object
        ++j.acceptedPer[cd.plan];
        if (j.acceptedPer[cd.plan] > kMaxLiveRead) continue;
        LiveObject lo; lo.plan = cd.plan; lo.addr = cd.addr;
        size_t got = 0;                                       // the whole object if it can be read, else a smaller front part
        for (size_t want = std::min(static_cast<size_t>(p.size), pipe.chunk()); want >= 16 && got == 0; want /= 2) {
            lo.bytes.assign(want, 0);
            if (pipe.copy(cd.addr, lo.bytes.data(), want)) got = want;
        }
        if (got == 0) { --j.acceptedPer[cd.plan]; ++j.rejUnreadable; continue; }
        { uint64_t k; std::memcpy(&k, lo.bytes.data(), 8); k = ~k; std::memcpy(lo.bytes.data(), &k, 8); }     // the first 8 bytes (the class pointer) are kept inverted: a copy of ours is never mistaken for the game's object
        const MapEntry& reg = j.maps[cd.region];
        lo.where = reg.path.empty() ? "anonymous memory" : reg.path;
        j.found.push_back(lo);
    }
    j.pipeTrouble = pipe.trouble(); j.pipeErrno = pipe.lastErrno();
}

void* searchThread(void* arg) {
    std::shared_ptr<SearchJob>* holder = static_cast<std::shared_ptr<SearchJob>*>(arg);
    std::shared_ptr<SearchJob> job = *holder;
    delete holder;
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);       // be polite: the game's own threads always go first (affects only this thread; errors do not matter)
    searchMemory(*job);
    return nullptr;
}

std::string describeRegion(const SearchJob& j, unsigned long ri) {
    if (ri >= j.maps.size()) return "unknown region";
    const MapEntry& r = j.maps[ri];
    char b[300];
    std::snprintf(b, sizeof b, "region %lu of %lu: %llu KB, %s", ri + 1, static_cast<unsigned long>(j.maps.size()), static_cast<unsigned long long>((r.end - r.start) >> 10),
                  r.path.empty() ? "anonymous memory" : r.path.c_str());
    return b;
}

enum JobResult { JOB_DONE, JOB_STUCK, JOB_BUSY, JOB_NOTHREAD };

// Starts the memory search on its own thread and waits for it, watching the heartbeat. `talk` (optional) gets a "still working" line every 10 s.
// JOB_STUCK: it stopped making progress (or ran far over its time) and was given up on; *stuckWhere says where. The thread is left alone (it keeps
// gSearchBusy set while it is stuck, so no second search starts on top of it).
JobResult runJob(const std::shared_ptr<SearchJob>& job, int stallSeconds, Out* talk, std::string* stuckWhere) {
    {   // one search at a time. If another one is running (the Scan button and the movement link can ask at the same moment) wait for it -
        // unless it makes no progress for stallSeconds: then it is stuck, and we say so instead of piling on.
        bool expected = false;
        if (!gSearchBusy.compare_exchange_strong(expected, true)) {
            const double waitStart = nowSeconds();
            double lastMoveAt = waitStart; unsigned long long lastBeat = gBeat.load();
            for (;;) {
                usleep(50000);
                expected = false;
                if (gSearchBusy.compare_exchange_strong(expected, true)) break;
                const double now = nowSeconds(); const unsigned long long b = gBeat.load();
                if (b != lastBeat) { lastBeat = b; lastMoveAt = now; }
                if (now - lastMoveAt >= stallSeconds || now - waitStart >= 150.0) return JOB_BUSY;
            }
        }
    }
    pthread_t th; auto* holder = new std::shared_ptr<SearchJob>(job);
    if (pthread_create(&th, nullptr, searchThread, holder) != 0) { delete holder; gSearchBusy.store(false); return JOB_NOTHREAD; }
    pthread_detach(th);
    const double tStart = nowSeconds();
    double lastBeatAt = tStart, lastTalk = tStart; unsigned lastBeat = job->heartbeat.load();
    while (!job->done.load()) {
        usleep(job->testPollMicros > 0 ? job->testPollMicros : 100000);
        const double now = nowSeconds();
        const unsigned hb = job->heartbeat.load();
        if (hb != lastBeat) { lastBeat = hb; lastBeatAt = now; }
        if (talk && now - lastTalk >= 10.0) {
            lastTalk = now;
            talk->key("scan: memory search still working: %llu MB read so far, %s", job->bytes.load() >> 20, describeRegion(*job, job->region.load()).c_str());
        }
        if (now - lastBeatAt >= stallSeconds || now - tStart >= job->maxSeconds + 30.0) {
            char b[400]; std::snprintf(b, sizeof b, "%s, step %d, after %llu MB", describeRegion(*job, job->region.load()).c_str(), job->stage.load(), job->bytes.load() >> 20);
            if (stuckWhere) *stuckWhere = b;
            return JOB_STUCK;
        }
    }
    return JOB_DONE;
}

}  // namespace

bool classNameMatches(const std::string& name) {
    static const char* words[] = {"move", "locomo", "jump", "gravit", "speed", "fly", "walk", "sprint", "climb", "charactercontroller", "velocity", "teleport", "boost", "stamina", nullptr};
    std::string l = lowerOf(name.c_str());
    // "Remove" contains "move" but is not movement ("RemoveItem"). Take every "remove" out first,
    // so "RemoveItem" does not match but "RemoveMovement" still does.
    for (size_t p = l.find("remove"); p != std::string::npos; p = l.find("remove")) l.erase(p, 6);
    return hasAny(l, words);
}

bool indexNameMatches(const std::string& name) {
    static const char* words[] = {"character", "vertical", "motion", "player", "parameter", "constraint", "mobile", "physics", "bodycollider", nullptr};
    return classNameMatches(name) || hasAny(lowerOf(name.c_str()), words);
}

bool fieldNameMatches(const std::string& name) {
    static const char* words[] = {"speed", "jump", "gravit", "velocity", "sprint", "fly", "stamina", nullptr};
    return hasAny(lowerOf(name.c_str()), words);
}

bool loadApi(void* lib, Api* out, std::string* missing) {
    bool ok = true;
    auto need = [&](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(dlsym(lib, name));
        if (!slot) { ok = false; if (missing) { *missing += name; *missing += " "; } }
    };
    auto want = [&](auto& slot, const char* name) { slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(dlsym(lib, name)); };
    need(out->domain_get, "il2cpp_domain_get");
    need(out->domain_get_assemblies, "il2cpp_domain_get_assemblies");
    need(out->assembly_get_image, "il2cpp_assembly_get_image");
    need(out->image_get_name, "il2cpp_image_get_name");
    need(out->image_get_class_count, "il2cpp_image_get_class_count");
    need(out->image_get_class, "il2cpp_image_get_class");
    need(out->class_get_name, "il2cpp_class_get_name");
    need(out->class_get_namespace, "il2cpp_class_get_namespace");
    need(out->class_get_parent, "il2cpp_class_get_parent");
    need(out->class_get_fields, "il2cpp_class_get_fields");
    need(out->field_get_name, "il2cpp_field_get_name");
    need(out->field_get_type, "il2cpp_field_get_type");
    need(out->field_get_offset, "il2cpp_field_get_offset");
    need(out->class_get_methods, "il2cpp_class_get_methods");
    need(out->method_get_name, "il2cpp_method_get_name");
    need(out->method_get_param_count, "il2cpp_method_get_param_count");
    need(out->method_get_return_type, "il2cpp_method_get_return_type");
    need(out->type_get_name, "il2cpp_type_get_name");
    want(out->field_get_flags, "il2cpp_field_get_flags");
    want(out->method_get_flags, "il2cpp_method_get_flags");
    want(out->il2cpp_free, "il2cpp_free");
    want(out->thread_attach, "il2cpp_thread_attach");
    want(out->thread_detach, "il2cpp_thread_detach");
    want(out->class_instance_size, "il2cpp_class_instance_size");
    want(out->class_from_type, "il2cpp_class_from_type");
    want(out->class_is_valuetype, "il2cpp_class_is_valuetype");
    want(out->class_is_enum, "il2cpp_class_is_enum");
    want(out->class_value_size, "il2cpp_class_value_size");
    want(out->runtime_invoke, "il2cpp_runtime_invoke");
    want(out->class_get_method_from_name, "il2cpp_class_get_method_from_name");
    want(out->resolve_icall, "il2cpp_resolve_icall");
    want(out->thread_current, "il2cpp_thread_current");
    return ok;
}

const char* currentStep() { return gStep.load(); }

// ---- stage D6: finding one class and its running copies again, quietly (nothing is written to the facts file) ------------------------------
const FieldInfo* ClassInfo::field(const char* n) const {
    for (const FieldInfo& f : fields) if (f.name == n) return &f;
    return nullptr;
}

ClassInfo findClass(const Api& api, const char* ns, const char* name) {
    ClassInfo ci;
    const std::string wantNs = ns ? ns : "", wantName = name ? name : "";
    ci.fullName = wantNs.empty() ? wantName : wantNs + "." + wantName;
    void* domain = api.domain_get ? api.domain_get() : nullptr;
    if (!domain) { ci.error = "the game's runtime is not ready yet (no domain)"; return ci; }
    void* thread = api.thread_attach ? api.thread_attach(domain) : nullptr;
    struct Detach { const Api& a; void* t; ~Detach() { if (t && a.thread_detach) a.thread_detach(t); } } detach{api, thread};
    size_t asmCount = 0;
    void** assemblies = api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies || asmCount == 0) { ci.error = "no assemblies reported"; return ci; }
    void* klass = nullptr;
    for (int pass = 0; pass < 2 && !klass; ++pass) {                       // the game's own code first, then anything that is not engine / system
        for (size_t a = 0; a < asmCount && !klass; ++a) {
            void* image = api.assembly_get_image(assemblies[a]);
            if (!image) continue;
            const char* rawName = api.image_get_name(image);
            const std::string an = rawName ? rawName : "";
            if (skipAssembly(an) || (pass == 0 && !oursAssembly(an))) continue;
            const size_t count = api.image_get_class_count(image);
            for (size_t i = 0; i < count; ++i) {
                void* k = api.image_get_class(image, i);
                if (!k) continue;
                const char* cn = api.class_get_name(k);
                if (!cn || wantName != cn) continue;
                const char* kns = api.class_get_namespace(k);
                if (wantNs != (kns ? kns : "")) continue;
                klass = k; break;
            }
        }
    }
    if (!klass) { ci.error = "the class " + ci.fullName + " does not exist in this game"; return ci; }
    const int32_t isz = api.class_instance_size ? api.class_instance_size(klass) : 0;
    if (isz < 16 || isz > 8192) { ci.error = "the size of " + ci.fullName + " could not be read"; return ci; }
    ci.klassInv = ~static_cast<uint64_t>(reinterpret_cast<uintptr_t>(klass));
    ci.size = isz;
    ci.unityObject = isUnityObjectClass(api, klass);
    void* it = nullptr; int n = 0;
    while (void* f = api.class_get_fields(klass, &it)) {
        if (++n > 600) break;
        const int flags = api.field_get_flags ? api.field_get_flags(f) : 0;
        if (flags & (0x10 | 0x40)) continue;                              // static / constant: not stored in the object
        const int off = static_cast<int>(api.field_get_offset(f));
        if (off < 16 || off >= isz) continue;
        FieldInfo fi; const char* fn = api.field_get_name(f);
        fi.name = fn ? fn : "?"; fi.offset = off; fi.typeName = typeName(api, api.field_get_type(f));
        ci.fields.push_back(fi);
    }
    ci.found = true;
    return ci;
}

void* findClassHandle(const Api& api, const char* ns, const char* name, std::string* error) {
    const std::string wantNs = ns ? ns : "", wantName = name ? name : "";
    const std::string full = wantNs.empty() ? wantName : wantNs + "." + wantName;
    void* domain = api.domain_get ? api.domain_get() : nullptr;
    if (!domain) { if (error) *error = "the game's runtime is not ready yet (no domain)"; return nullptr; }
    void* thread = api.thread_attach ? api.thread_attach(domain) : nullptr;
    struct Detach { const Api& a; void* t; ~Detach() { if (t && a.thread_detach) a.thread_detach(t); } } detach{api, thread};
    size_t asmCount = 0;
    void** assemblies = api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies || asmCount == 0) { if (error) *error = "no assemblies reported"; return nullptr; }
    for (size_t a = 0; a < asmCount; ++a) {
        void* image = api.assembly_get_image(assemblies[a]);
        if (!image) continue;
        const size_t count = api.image_get_class_count(image);
        for (size_t i = 0; i < count; ++i) {
            void* k = api.image_get_class(image, i);
            if (!k) continue;
            const char* cn = api.class_get_name(k);
            if (!cn || wantName != cn) continue;
            const char* kns = api.class_get_namespace(k);
            if (wantNs != (kns ? kns : "")) continue;
            return k;
        }
    }
    if (error) *error = "the class " + full + " does not exist in this game";
    return nullptr;
}

CopySearch findCopies(const ClassInfo& cls, int maxCopies, int maxSeconds, int stallSeconds, int testHangAfterChunks, int testPollMicros) {
    CopySearch r;
    if (!cls.found || cls.size < 16) { r.error = "no class to look for"; return r; }
    auto job = std::make_shared<SearchJob>();
    job->plans.push_back({cls.klassInv, cls.unityObject, cls.size});
    job->maps = readMaps();
    job->maxSeconds = maxSeconds; job->testHangAfterChunks = testHangAfterChunks; job->testPollMicros = testPollMicros;
    std::string where;
    switch (runJob(job, stallSeconds, nullptr, &where)) {
        case JOB_DONE: break;
        case JOB_STUCK: r.stuck = true; r.error = "the memory search stopped answering (" + where + ")"; return r;
        case JOB_BUSY: r.busy = true; r.stuck = true; r.error = "an earlier memory search is still stuck in this game session"; return r;
        case JOB_NOTHREAD: r.error = "could not start the memory search thread"; return r;
    }
    r.bytesRead = job->bytes.load(); r.seconds = job->seconds;
    if (job->pipeFailed) { r.error = "the copy pipe does not work here (error " + std::to_string(job->pipeErrno) + ")"; return r; }
    r.ok = true;
    r.hits = job->cands; r.accepted = job->acceptedPer.empty() ? 0 : job->acceptedPer[0];
    for (const LiveObject& lo : job->found) {
        if (static_cast<int>(r.copies.size()) >= maxCopies) break;
        Copy c; c.addr = lo.addr; c.bytes = lo.bytes;
        r.copies.push_back(std::move(c));
    }
    return r;
}

// ---- stage D7: the "ball and hoops" report -----------------------------------------------------------------------------
namespace {
const int kMaxShotIndex = 500;         // index lines (names only)
const int kMaxShotDetail = 45;         // ball / hoop / shot-assist classes written out in full
const int kMaxShotLive = 12;           // classes looked for in memory

// ---- The exact classes. The stage D7b scan of the real game (lobby) listed every class with a ball / hoop / shot-like name (311 of them) and showed that most
// are noise (audio, tether ball, shaders, bots ...). These are the ones that matter for an aimbot: the ball, the game's OWN shot assist, the hoops and the
// game / match state. Found by exact name (in the game's own code); a name that is not there is reported.
struct ShotTarget { const char* name; int maxFields; int maxMethods; };        // 0 = the usual limit for important classes
const ShotTarget kShotTargets[] = {
    {"Basketball", 0, 0},                                                                            // the ball script (101 fields)
    {"BasketballShotAssist", 0, 0}, {"ShotAssistParams", 0, 0}, {"PredictedShotResult", 0, 0}, {"BankShotCandidate", 0, 0}, {"RimTarget", 0, 0},     // the game's own aim help
    {"ShotData", 0, 0}, {"BasketballAssist", 0, 0}, {"ThrowAssist", 0, 0},
    {"BasketballGoal", 0, 0}, {"BasketballGoalManager", 0, 0}, {"HoopManager", 0, 0}, {"NetRimReference", 0, 0},                                   // the hoops
    {"BasketballGameContext", 0, 60}, {"GameManager", 120, 120},                                     // lobby or official match?
    {"BallControl", 0, 0}, {"BallControlManager", 0, 80}, {"BasketballProperties", 0, 0}, {"ShootGesture", 0, 0}, {"ShootGameBall", 0, 0},
    {"BallPhysicsUtilities", 0, 0}, {"ReleasedBallCommand", 0, 0}, {"ShotManager", 0, 0}, {"ShotDetectionHelper", 0, 0}, {"SteveBallSync", 0, 0}, {"BallController", 0, 0},
    {nullptr, 0, 0}};
// The network library of the game (Normcore: "Normal.Realtime.dll"). Written out with limits (who owns an object, how to ask for ownership).
const char* const kNetClasses[] = {"Normal.Realtime.RealtimeView", "Normal.Realtime.RealtimeTransform", nullptr};
// Classes whose running copies are looked for in memory, most important first (at most kMaxLiveTargets).
const char* const kShotLive[] = {"Basketball", "BasketballStateSync", "GameManager", "BasketballGameContext", "BasketballGoal", "BasketballShotAssist", "ShotAssistParams",
                                 "ThrowAssist", "BasketballAssist", "HoopManager", "BallControl", "BallControlManager", nullptr};

// Classes that can never have a useful running copy: event delegates, enums, structs (they live inside other objects).
bool shotUselessParent(const std::string& parent) { return parent == "MulticastDelegate" || parent == "Delegate" || parent == "Enum" || parent == "ValueType"; }
bool shotIsTarget(const std::string& name) { for (const ShotTarget* t = kShotTargets; t->name; ++t) if (name == t->name) return true; return false; }
// is a running copy of this class searched for?  (only the game's own code; never delegates / enums / structs)
bool shotLiveName(const std::string& name, bool ours, const std::string& parent) { return ours && !shotUselessParent(parent) && inList(name, kShotLive); }
bool shotFieldMatches(const std::string& name) {
    static const char* words[] = {"ball", "rim", "grab", "hold", "shot", "throw", "hoop", "wrist", "power", "hand", "release", "goal", "pass", "aim", nullptr};
    return hasAny(lowerOf(name.c_str()), words);
}

void* findEngineClass(const Api& api, void** assemblies, size_t asmCount, const char* ns, const char* name, std::string* asmName) {
    for (size_t a = 0; a < asmCount; ++a) {
        void* image = api.assembly_get_image(assemblies[a]);
        if (!image) continue;
        const char* rn = api.image_get_name(image);
        const std::string an = rn ? rn : "";
        if (!startsWith(an, "UnityEngine")) continue;
        const size_t count = api.image_get_class_count(image);
        for (size_t i = 0; i < count; ++i) {
            void* k = api.image_get_class(image, i);
            if (!k) continue;
            const char* cn = api.class_get_name(k);
            if (!cn || std::strcmp(cn, name) != 0) continue;
            const char* kns = api.class_get_namespace(k);
            if (std::strcmp(kns ? kns : "", ns) != 0) continue;
            if (asmName) *asmName = an;
            return k;
        }
    }
    return nullptr;
}

std::string methodPlace(uintptr_t libBase, void* method) {
    uint64_t ptr = 0;
    if (!readPtr(reinterpret_cast<uintptr_t>(method), &ptr)) return "rva=none";
    unsigned long rva = 0; std::string lib;
    const PtrKind kind = classifyPointer(ptr, libBase, &rva, &lib);
    char b[96];
    if (kind == P_IL2CPP) std::snprintf(b, sizeof b, "rva=%lx", rva);
    else if (kind == P_OTHERLIB) std::snprintf(b, sizeof b, "rva=%lx lib=%s", rva, lib.c_str());
    else if (kind == P_OUTSIDE) std::snprintf(b, sizeof b, "rva=outside-libraries");
    else std::snprintf(b, sizeof b, "rva=none");
    return b;
}

// Only LOOKS things up. Nothing in the game is called, so this cannot change anything.
void shotEngineReport(const Api& api, void** assemblies, size_t asmCount, uintptr_t libBase, Out& out) {
    out.key("--- engine functions the Aimbot would need (looked up only: nothing is called) ---");
    out.key("scan: runtime functions: il2cpp_runtime_invoke=%s il2cpp_class_get_method_from_name=%s il2cpp_resolve_icall=%s il2cpp_thread_attach=%s",
            api.runtime_invoke ? "yes" : "NO", api.class_get_method_from_name ? "yes" : "NO", api.resolve_icall ? "yes" : "NO", api.thread_attach ? "yes" : "NO");
    struct Want { const char* ns; const char* cls; const char* methods[12]; };
    static const Want wants[] = {
        {"UnityEngine", "Rigidbody", {"get_velocity", "set_velocity", "get_position", "get_angularVelocity", "set_angularVelocity", "get_useGravity", "get_drag", "get_mass", "get_isKinematic", "AddForce", nullptr}},
        {"UnityEngine", "Transform", {"get_position", "get_localPosition", "get_forward", "get_parent", nullptr}},
        {"UnityEngine", "Component", {"get_transform", "get_gameObject", nullptr}},
        {"UnityEngine", "Physics", {"get_gravity", nullptr}},
        {"UnityEngine", "Time", {"get_deltaTime", "get_fixedDeltaTime", nullptr}},
    };
    for (const Want& w : wants) {
        std::string asmName;
        void* k = findEngineClass(api, assemblies, asmCount, w.ns, w.cls, &asmName);
        if (!k) { out.key("scan: engine class %s.%s NOT FOUND", w.ns, w.cls); continue; }
        out.key("scan: engine class %s.%s found [assembly %s]", w.ns, w.cls, asmName.c_str());
        void* it = nullptr; int n = 0;
        while (void* m = api.class_get_methods(k, &it)) {
            if (++n > 3000) break;
            const char* mn = api.method_get_name(m);
            if (!mn || !inList(mn, w.methods)) continue;
            out.key("scan:   method %s(%u) : %s %s", mn, api.method_get_param_count(m), typeName(api, api.method_get_return_type(m)).c_str(), methodPlace(libBase, m).c_str());
        }
    }
    if (api.resolve_icall) {
        static const char* const icalls[] = {
            "UnityEngine.Rigidbody::get_velocity_Injected(UnityEngine.Vector3&)", "UnityEngine.Rigidbody::get_velocity_Injected",
            "UnityEngine.Rigidbody::set_velocity_Injected(UnityEngine.Vector3&)", "UnityEngine.Rigidbody::set_velocity_Injected",
            "UnityEngine.Rigidbody::get_position_Injected(UnityEngine.Vector3&)", "UnityEngine.Rigidbody::get_position_Injected",
            "UnityEngine.Transform::get_position_Injected(UnityEngine.Vector3&)", "UnityEngine.Transform::get_position_Injected",
            "UnityEngine.Physics::get_gravity_Injected(UnityEngine.Vector3&)", "UnityEngine.Physics::get_gravity_Injected", nullptr};
        for (int i = 0; icalls[i]; ++i) {
            void* f = api.resolve_icall(icalls[i]);
            unsigned long rva = 0; std::string lib;
            const bool got = f && classifyPointer(reinterpret_cast<uint64_t>(f), libBase, &rva, &lib) != P_NONE;
            if (got) out.key("scan: icall %s -> found (rva=%lx lib=%s)", icalls[i], rva, lib.c_str());
            else out.key("scan: icall %s -> not found", icalls[i]);
        }
    }
}
}  // namespace

// exposed for the test (the real class names from the earlier facts files are checked against these)
bool shotTargetName(const std::string& name) { return shotIsTarget(name); }
bool shotLiveWanted(const std::string& name, bool ours, const std::string& parent) { return shotLiveName(name, ours, parent); }

Summary run(const Api& api, uintptr_t libBase, LogFn log) {
    Options o; o.libBase = libBase;
    return run(api, o, log);
}

Summary run(const Api& api, const Options& opt, LogFn log) {
    Summary sum;
    Out out(log, opt.maxLines, opt.reservedLines, opt.maxBytes, opt.reservedBytes);
    const bool shot = opt.topic == Topic::Shot;
    gStep.store("starting");
    out.line("--- game code scan (read-only) ---");
    if (shot) out.line("scan: BALL AND HOOPS scan (stage D7): the ball, the hoops, the game's own shot assist, the game / match state, and the engine functions a ball needs");
    listThreads(out);
    void* domain = api.domain_get();
    if (!domain) { sum.error = "the game's runtime is not ready yet (no domain)"; out.line("scan: %s", sum.error.c_str()); return sum; }
    void* thread = api.thread_attach ? api.thread_attach(domain) : nullptr;
    size_t asmCount = 0;
    void** assemblies = api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies || asmCount == 0) { sum.error = "no assemblies reported"; out.line("scan: %s", sum.error.c_str()); if (thread && api.thread_detach) api.thread_detach(thread); return sum; }

    // ---- 1. list every assembly, remember every class of the ones we care about
    gStep.store("1 of 6: listing the assemblies and classes");
    std::vector<AsmRef> asms;
    std::vector<ClassRef> classes;
    for (size_t a = 0; a < asmCount; ++a) {
        void* image = api.assembly_get_image(assemblies[a]);
        if (!image) continue;
        const char* rawName = api.image_get_name(image);
        AsmRef ar; ar.name = rawName ? rawName : "?"; ar.skip = skipAssembly(ar.name); ar.ours = oursAssembly(ar.name);
        const size_t count = api.image_get_class_count(image);
        ++sum.assemblies;
        if (opt.listAssemblies) out.line("scan: assembly %s classes=%d %s", ar.name.c_str(), static_cast<int>(count), ar.skip ? "(engine / system: skipped)" : (ar.ours ? "(the game's own code)" : "(other: names only)"));
        asms.push_back(ar);
        if (ar.skip) continue;
        const int asmIndex = static_cast<int>(asms.size()) - 1;
        for (size_t i = 0; i < count; ++i) {
            void* klass = api.image_get_class(image, i);
            if (!klass) continue;
            const char* cn = api.class_get_name(klass);
            if (!cn) continue;
            ++sum.classes;
            ClassRef cr; cr.set(klass); cr.asmIndex = asmIndex; cr.name = cn;
            if (cr.name.find('<') != std::string::npos || cr.name.find('>') != std::string::npos) continue;    // compiler-made helper classes
            const char* ns = api.class_get_namespace(klass);
            cr.full = (ns && *ns) ? std::string(ns) + "." + cr.name : cr.name;
            classes.push_back(cr);
        }
    }

    // ---- 2. index: one line per class that looks like movement / player / character / parameters ...
    int indexLines = 0;
    gStep.store("2 of 6: class-name index");
    if (opt.brief && !shot) out.line("scan: BRIEF scan: the class index and the single-line speed / jump / gravity hits are left out (the earlier scans already wrote them)");
    if (shot && !opt.shotIndex) out.line("scan: (the class-name index is left out of this report: the earlier ball-and-hoops file already has it)");
    else out.line(shot ? "--- index of class names about balls, hoops, rims, shots, throws and grabbing (the game's own code + Autohand) ---" : "--- index of class names (the game's own code + Normal.*) ---");
    for (const ClassRef& c : classes) {
        if (opt.brief && !shot) break;                     // brief scan: the index was already written by the earlier scans
        if (shot && !opt.shotIndex) break;
        const AsmRef& ar = asms[c.asmIndex];
        if (shot) {
            if (!(ar.ours || startsWith(c.full, "Autohand.")) || !shotNameMatches(c.name)) continue;
            if (indexLines >= kMaxShotIndex) { out.line("scan: (index stopped at %d lines)", kMaxShotIndex); break; }
        } else {
            if (!(ar.ours || startsWith(ar.name, "Normal."))) continue;
            if (!indexNameMatches(c.name)) continue;
            if (indexLines >= kMaxIndexLines) { out.line("scan: (index stopped at %d lines)", kMaxIndexLines); break; }
        }
        void* parent = api.class_get_parent(c.klass());
        const char* pn = parent ? api.class_get_name(parent) : nullptr;
        int nf = 0, nm = 0;
        { void* it = nullptr; while (api.class_get_fields(c.klass(), &it)) if (++nf > 2000) break; }
        { void* it = nullptr; while (api.class_get_methods(c.klass(), &it)) if (++nm > 2000) break; }
        out.line("scan: index %s : %s [%s] fields=%d methods=%d", c.full.c_str(), pn ? pn : "-", ar.name.c_str(), nf, nm);
        ++indexLines; ++sum.indexed;
    }

    // ---- 3. full detail: important classes first (no limits), then the other movement-ish ones
    gStep.store("3 of 6: fields and methods of the important classes");
    out.line("scan: step 3 of 6: fields and methods of the important classes");
    std::vector<char> detailed(classes.size(), 0);
    int priorityCount = 0, otherCount = 0;
    unsigned noneCount = 0, ilCount = 0, otherLibCount = 0, outsideCount = 0;
    auto detail = [&](size_t idx, bool priority, int capF = 0, int capM = 0) {
        const ClassRef& c = classes[idx];
        const AsmRef& ar = asms[c.asmIndex];
        void* parent = api.class_get_parent(c.klass());
        const char* pn = parent ? api.class_get_name(parent) : nullptr;
        out.line("scan: CLASS %s : %s   [assembly %s]", c.full.c_str(), pn ? pn : "-", ar.name.c_str());
        const int maxF = capF > 0 ? capF : (priority ? kMaxFieldsPriority : kMaxFieldsPerClass), maxM = capM > 0 ? capM : (priority ? kMaxMethodsPriority : kMaxMethodsPerClass);
        void* it = nullptr; int nf = 0;
        while (void* f = api.class_get_fields(c.klass(), &it)) {
            if (++nf > maxF) { out.line("scan:   (more fields not shown)"); break; }
            const char* fnm = api.field_get_name(f);
            const int flags = api.field_get_flags ? api.field_get_flags(f) : 0;
            out.line("scan:   field %s : %s @%d%s", fnm ? fnm : "?", typeName(api, api.field_get_type(f)).c_str(), static_cast<int>(api.field_get_offset(f)), (flags & 0x10) ? " static" : "");
        }
        it = nullptr; int nm = 0;
        while (void* m = api.class_get_methods(c.klass(), &it)) {
            if (++nm > maxM) { out.line("scan:   (more methods not shown)"); break; }
            const char* mn = api.method_get_name(m);
            uint32_t iflags = 0;
            const uint32_t mflags = api.method_get_flags ? api.method_get_flags(m, &iflags) : 0;
            uint64_t ptr = 0;
            const bool readable = readPtr(reinterpret_cast<uintptr_t>(m), &ptr);
            unsigned long rva = 0; std::string lib;
            const PtrKind kind = readable ? classifyPointer(ptr, opt.libBase, &rva, &lib) : P_NONE;
            char where[96];
            if (kind == P_IL2CPP) { std::snprintf(where, sizeof where, "rva=%lx", rva); ++ilCount; }
            else if (kind == P_OTHERLIB) { std::snprintf(where, sizeof where, "rva=%lx lib=%s", rva, lib.c_str()); ++otherLibCount; }
            else if (kind == P_OUTSIDE) { std::snprintf(where, sizeof where, "rva=outside-libraries"); ++outsideCount; }
            else { std::snprintf(where, sizeof where, "rva=none"); ++noneCount; }
            // The first bytes of the code of the small Set* / set_* / get_* / On* methods of the important classes: a "Set" method that just stores
            // its argument is two machine instructions, and they say which field it writes (decoded offline, never run).
            char codeText[48] = "";
            if (priority && kind == P_IL2CPP && api.method_get_param_count(m) <= 1 && mn &&
                (startsWith(mn, "Set") || startsWith(mn, "set_") || startsWith(mn, "get_") || startsWith(mn, "On"))) {
                unsigned char cb[16];
                if (pointerPipe().copy(static_cast<uintptr_t>(ptr), cb, sizeof cb)) {
                    std::snprintf(codeText, sizeof codeText, " code=");
                    for (int bi = 0; bi < 16; ++bi) { char two[4]; std::snprintf(two, sizeof two, "%02x", static_cast<unsigned>(cb[bi])); std::strcat(codeText, two); }
                }
            }
            out.line("scan:   method %s(%u) : %s %s%s%s", mn ? mn : "?", api.method_get_param_count(m), typeName(api, api.method_get_return_type(m)).c_str(), where, codeText, (mflags & 0x10) ? " static" : "");
        }
        detailed[idx] = 1; ++sum.matchedClasses;
        if (priority) ++priorityCount; else ++otherCount;
    };
    const char* const* priorityNames = opt.brief ? kPriorityBrief : kPriorityClasses;
    if (shot) {       // the exact ball / hoop / shot-assist classes, in the order of kShotTargets, then the network classes
        int shotCount = 0;
        std::string missingNames;
        for (const ShotTarget* t = kShotTargets; t->name && shotCount < kMaxShotDetail; ++t) {
            int found = 0;
            for (size_t i = 0; i < classes.size() && found < 2 && shotCount < kMaxShotDetail; ++i) {      // (two classes can share a name, for example ShotData)
                const ClassRef& c = classes[i];
                if (detailed[i] || !asms[c.asmIndex].ours || c.name != t->name) continue;
                detail(i, true, t->maxFields, t->maxMethods);
                ++found; ++shotCount;
            }
            if (!found) { missingNames += t->name; missingNames += " "; }
        }
        for (const char* const* n = kNetClasses; *n && shotCount < kMaxShotDetail; ++n) {
            bool found = false;
            for (size_t i = 0; i < classes.size(); ++i) if (!detailed[i] && classes[i].full == *n) { detail(i, true, 40, 90); ++shotCount; found = true; break; }
            if (!found) missingNames += std::string(*n) + " ";
        }
        out.key("scan: classes asked for by name but NOT found in this game: %s", missingNames.empty() ? "(none)" : missingNames.c_str());
    }
    for (int pass = 0; pass < (opt.brief || shot ? (shot ? 0 : 1) : 2); ++pass) {               // important classes: the named ones first, then anything vertical / jump / gravity / parameters
        for (size_t i = 0; i < classes.size(); ++i) {
            const ClassRef& c = classes[i];
            if (detailed[i] || !asms[c.asmIndex].ours || priorityCount >= kMaxPriorityDetail) continue;
            const std::string low = lowerOf(c.name.c_str());
            const bool named = inList(c.name, priorityNames);
            const bool strong = low.find("vertical") != std::string::npos || low.find("parameters") != std::string::npos ||
                                low.find("jump") != std::string::npos || low.find("gravit") != std::string::npos;
            if (pass == 0 ? !named : !strong) continue;
            detail(i, true);
        }
    }
    for (size_t i = 0; i < classes.size(); ++i) {        // other movement-ish classes (the Oculus teleport samples were already listed by stage D4)
        if (opt.brief || shot) break;
        const ClassRef& c = classes[i];
        if (detailed[i] || !asms[c.asmIndex].ours || otherCount >= kMaxOtherDetail) continue;
        if (!classNameMatches(c.name) || startsWith(c.name, "Teleport") || startsWith(c.full, "OculusSampleFramework")) continue;
        detail(i, false);
    }
    out.line("scan: method code places: in-libil2cpp=%u other-library=%u outside-libraries=%u none=%u", ilCount, otherLibCount, outsideCount, noneCount);

    // ---- 4. single lines for speed / jump / gravity fields and for classes that hold a Rigidbody / CharacterController
    gStep.store("4 of 6: speed / jump / gravity field lines");
    out.line("scan: step 4 of 6: single lines for speed / jump / gravity fields");
    int fieldHits = 0, typeHits = 0;
    for (size_t i = 0; i < classes.size(); ++i) {
        if (opt.brief || shot) break;                      // brief scan: these single lines were already written by the earlier scans
        const ClassRef& c = classes[i];
        if (!asms[c.asmIndex].ours || detailed[i]) continue;
        void* it = nullptr; int nf = 0;
        while (void* f = api.class_get_fields(c.klass(), &it)) {
            if (++nf > 400) break;
            const char* fnm = api.field_get_name(f);
            const std::string fname = fnm ? fnm : "";
            if (fieldHits < kMaxFieldHits && fieldNameMatches(fname)) {
                ++fieldHits; ++sum.fieldHits;
                out.line("scan: field-hit %s.%s : %s @%d", c.full.c_str(), fname.c_str(), typeName(api, api.field_get_type(f)).c_str(), static_cast<int>(api.field_get_offset(f)));
            } else if (typeHits < kMaxTypeHits) {
                const std::string tn = typeName(api, api.field_get_type(f));
                if (tn.find("CharacterController") != std::string::npos || (tn.find("Rigidbody") != std::string::npos && fname.find("ball") == std::string::npos && lowerOf(c.name.c_str()).find("ball") == std::string::npos)) {
                    ++typeHits; ++sum.typeHits;
                    out.line("scan: type-hit %s.%s : %s @%d", c.full.c_str(), fname.c_str(), tn.c_str(), static_cast<int>(api.field_get_offset(f)));
                }
            }
        }
    }

    out.line("scan: step 4 done: field-hits=%d type-hits=%d", sum.fieldHits, sum.typeHits);
    if (shot) shotEngineReport(api, assemblies, asmCount, opt.libBase, out);

    // ---- 5. plan the live search while the runtime can still be asked (field names, types, positions)
    gStep.store("5 of 6: planning the live search");
    out.line("scan: step 5 of 6: planning the live search");
    std::vector<LivePlan> plans;
    if (opt.searchMemory) {
        std::vector<const ClassRef*> wanted;                 // which classes get a live search (in the shot scan: in the order of kShotLive)
        if (shot) {
            for (const char* const* n = kShotLive; *n; ++n)
                for (const ClassRef& c : classes) {
                    if (c.name != *n || !asms[c.asmIndex].ours) continue;
                    void* par = api.class_get_parent(c.klass()); const char* pnm = par ? api.class_get_name(par) : nullptr;
                    if (shotLiveName(c.name, true, pnm ? pnm : "")) { wanted.push_back(&c); break; }
                }
        } else {
            for (const ClassRef& c : classes) if (asms[c.asmIndex].ours && inList(c.name, opt.brief ? kLiveBrief : kLiveClasses)) wanted.push_back(&c);
        }
        for (const ClassRef* cp : wanted) {
            const ClassRef& c = *cp;
            if (static_cast<int>(plans.size()) >= (shot ? kMaxShotLive : kMaxLiveTargets)) break;
            LivePlan p; p.klassInv = ~static_cast<uint64_t>(reinterpret_cast<uintptr_t>(c.klass())); p.full = c.full;
            p.filterShot = false;
            p.unityObject = isUnityObjectClass(api, c.klass());
            const int32_t isz = api.class_instance_size ? api.class_instance_size(c.klass()) : 0;
            p.size = (isz >= 16 && isz <= 4096) ? isz : 512;
            // the class's own fields, then the fields of the game's own parent classes
            int guard = 0;
            for (void* k = c.klass(); k && guard < 8 && static_cast<int>(p.fields.size()) < kMaxLiveFields; k = api.class_get_parent(k), ++guard) {
                const char* kn = api.class_get_name(k);
                const char* kns = api.class_get_namespace(k);
                if (!kn) break;
                const std::string name = kn, ns = kns ? kns : "";
                if (k != c.klass() && (name == "MonoBehaviour" || name == "Behaviour" || name == "Component" || name == "Object" || name == "ScriptableObject" || startsWith(ns, "UnityEngine") || startsWith(ns, "System"))) break;
                addInstanceFields(api, k, ns.empty() ? name : ns + "." + name, &p.fields);
            }
            plans.push_back(p);
        }
    }
    { std::string names; for (const LivePlan& p : plans) { names += p.full; names += "(" + std::to_string(p.fields.size()) + " fields)  "; }
      out.key("scan: step 5 done: %d class(es) planned for the live search: %s", static_cast<int>(plans.size()), names.c_str()); }
    std::vector<ClassRef>().swap(classes);            // free our own copies of the class pointers before the memory search
    if (thread && api.thread_detach) api.thread_detach(thread);       // the memory search needs no il2cpp calls
    if (shot && opt.searchMemory && !plans.empty() && opt.liveDelaySeconds > 0) {
        // Time for the person to close the menu, pick up a ball and take a shot: the running ball only has useful values then.
        out.key("scan: waiting %d seconds before looking at the running objects, so you can close the menu, pick up a ball and take a shot, then hold a ball again", opt.liveDelaySeconds);
        gStep.store("5b: waiting for you to pick up a ball");
        for (int i = 0; i < opt.liveDelaySeconds; ++i) usleep(1000000);
    }

    // ---- 6. look through the game's memory for the running copies and write down their field values
    // The search runs on its own thread; this thread only watches it. If it makes no progress for a while, we give up on it.
    std::shared_ptr<SearchJob> job;
    bool stuck = false; std::string stuckWhere;
    if (opt.searchMemory && !plans.empty()) {
        sum.liveClasses = static_cast<int>(plans.size());
        gStep.store("6 of 6: reading the game's memory");
        out.key("scan: step 6 of 6: looking through the game's memory (read-only; the game keeps running)");
        job = std::make_shared<SearchJob>();
        for (const LivePlan& p : plans) job->plans.push_back({p.klassInv, p.unityObject, p.size});
        job->maps = readMaps();
        job->maxSeconds = opt.maxSeconds; job->testHangAfterChunks = opt.testHangAfterChunks; job->testPipeBytes = opt.testPipeBytes;
        std::string where;
        switch (runJob(job, opt.stallSeconds, &out, &where)) {
            case JOB_DONE: break;
            case JOB_STUCK: stuck = true; stuckWhere = where; break;
            case JOB_BUSY:
                out.key("scan: an earlier memory search is still stuck in this game session, so no new one is started (close and reopen the game to try the live values again)");
                stuck = true; stuckWhere = "an earlier search is still stuck"; job.reset(); break;
            case JOB_NOTHREAD:
                out.key("scan: could not start the memory search thread");
                stuck = true; stuckWhere = "could not start the memory search thread"; job.reset(); break;
        }
    }
    if (job && !stuck) {
        sum.memoryBytes = job->bytes.load();
        if (job->pipeFailed) {
            out.key("scan: the memory search could not start: its copy pipe does not work here (error %d). The class lists above are still good, but there are no live values.", job->pipeErrno);
        } else {
            out.key("--- live values (the running copies of the important classes) ---");
            out.key("scan: memory search read %llu KB in %lu regions in %.1f s%s; copy pipe=%lu bytes (chunk %lu KB); hits=%d (rejected: owner-lock=%d destroyed=%d unreadable=%d); copy trouble=%d%s",
                     static_cast<unsigned long long>(job->bytes.load() >> 10), job->regionsRead.load(), job->seconds, job->timedOut ? " (STOPPED: time limit)" : "",
                     static_cast<unsigned long>(job->pipeBytes), static_cast<unsigned long>(job->chunkBytes >> 10), job->cands, job->rejMonitor, job->rejCached, job->rejUnreadable,
                     job->pipeTrouble, job->pipeTrouble ? " (the copy pipe itself misbehaved: some memory was not looked at)" : "");
            // rank the copies of each class by how healthy they look, write out only the best few
            struct Ranked { size_t idx; Sanity sanity; };
            const int maxPrint = shot ? 4 : kMaxLivePrinted;
            std::vector<std::vector<Ranked>> perPlan(plans.size());
            for (size_t i = 0; i < job->found.size(); ++i) perPlan[job->found[i].plan].push_back({i, sanityOf(plans[job->found[i].plan], job->found[i].bytes)});
            for (auto& v : perPlan)
                std::stable_sort(v.begin(), v.end(), [](const Ranked& x, const Ranked& y) {
                    if (x.sanity.percent() != y.sanity.percent()) return x.sanity.percent() > y.sanity.percent();
                    return x.sanity.nonzero > y.sanity.nonzero; });
            for (size_t t = 0; t < plans.size(); ++t) {
                char best[40] = "";
                if (!perPlan[t].empty()) std::snprintf(best, sizeof best, ", best sanity=%d%%", perPlan[t][0].sanity.percent());
                out.key("scan: live %s: %d hit(s), %d look like a real running copy%s%s", plans[t].full.c_str(), job->candidatesPer[t], job->acceptedPer[t],
                        job->acceptedPer[t] > maxPrint ? " (best few written out)" : "", best);
            }
            for (size_t t = 0; t < plans.size(); ++t) {
                const LivePlan& p = plans[t];
                std::vector<std::string> firstValues;                                  // the values of the first written copy of this class
                for (size_t rank = 0; rank < perPlan[t].size() && static_cast<int>(rank) < maxPrint; ++rank) {
                    const LiveObject& lo = job->found[perPlan[t][rank].idx];
                    const int ordinal = static_cast<int>(rank) + 1;
                    char cachedText[40] = "n/a";
                    if (p.unityObject) { uint64_t cp; std::memcpy(&cp, lo.bytes.data() + 16, 8); std::snprintf(cachedText, sizeof cachedText, "%s", cp ? "set" : "empty"); }
                    out.key("scan: live %s #%d size=%d native-link=%s in %s  sanity=%d%%%s", p.full.c_str(), ordinal, p.size, cachedText, lo.where.c_str(), perPlan[t][rank].sanity.percent(),
                            ordinal > 1 ? "   (only the fields that differ from copy #1 are listed)" : "");
                    for (size_t fi = 0; fi < p.fields.size(); ++fi) {
                        const FieldPlan& f = p.fields[fi];
                        if (p.filterShot && !shotFieldMatches(f.name)) { if (ordinal == 1) firstValues.push_back(std::string()); continue; }     // the player has 211 fields: only the ball / rim / grab ones are written
                        const std::string v = formatValue(f, lo.bytes.data(), lo.bytes.size());
                        if (ordinal == 1) firstValues.push_back(v);
                        else if (fi < firstValues.size() && firstValues[fi] == v) continue;
                        out.key("scan:   live %s = %s   (%s : %s @%d)", f.name.c_str(), v.c_str(), f.owner.c_str(), f.typeName.c_str(), f.offset);
                    }
                    ++sum.liveObjects;
                }
            }
        }
    }
    if (stuck) {
        sum.memoryStuck = true;
        out.last("scan: MEMORY SEARCH STUCK and given up on (%s). The class, field and method lists above are complete; only the live values are missing. The game is not affected.", stuckWhere.c_str());
    }

    gStep.store("finished");
    out.last("scan: DONE. assemblies=%d classes=%d index=%d detailed=%d field-hits=%d type-hits=%d live-copies=%d%s%s", sum.assemblies, sum.classes, sum.indexed, sum.matchedClasses, sum.fieldHits, sum.typeHits, sum.liveObjects,
             sum.memoryStuck ? " (live search stuck)" : "", out.full() ? " (output was cut at the line limit)" : "");
    sum.ok = true;
    return sum;
}

}  // namespace tzscan
