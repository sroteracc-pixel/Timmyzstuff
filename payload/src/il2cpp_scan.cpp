// il2cpp_scan.cpp - see il2cpp_scan.h.
#include "il2cpp_scan.h"

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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
const int kMaxLivePrinted = 3;         // running copies written out per class
const int kMaxLiveFields = 160;
const int kMaxCandidates = 20000;
const size_t kChunk = 65536;

// ---- the classes we care most about (found by the stage D4 scan of the real game)
const char* const kPriorityClasses[] = {"MobilePlayerLocomotion", "MobileVerticalMotion", "CharacterVerticalState", "CharacterWorldConstraints",
                                        "PlayerStatusSync", "CollisionVolume", "BodyCollider", nullptr};
const char* const kLiveClasses[] = {"MobilePlayerLocomotion", "MobileVerticalMotion", "CharacterWorldConstraints", "SimpleCapsuleWithStickMovement",
                                    "PlayerMovement", "Movement", "LocomotionController", "PlayerStatusSync", "BodyCollider", nullptr};

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

class Out {
public:
    explicit Out(LogFn log) : log_(log) {}
    // an ordinary line: dropped once the file is nearly full
    void line(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (lines_ >= kMaxLines - kReservedLines) return;
        char b[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        put(b);
    }
    // an important line (headers, summaries, "stuck", "DONE"): still written when the ordinary lines have used up their room
    void key(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (lines_ >= kMaxLines) return;
        char b[1024];
        va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        put(b);
    }
    bool full() const { return lines_ >= kMaxLines - kReservedLines; }
private:
    void put(const char* b) { log_("%s", b); ++lines_; }       // never use game text as a format string
    LogFn log_; int lines_ = 0;
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

// ---- crash-proof memory reads (the kernel says "no" for a bad address instead of the program crashing)
// The memory is written into a pipe (the kernel checks every address) and read back out of it.
// IMPORTANT (found with the second real scan, which hung): a pipe can be SMALLER than we ask for. A write that is bigger than
// the pipe waits for a reader that never comes, and the thread is stuck for good. So: both ends never wait (O_NONBLOCK),
// the real pipe size is asked for and never exceeded, and the pipe is tested once before it is used.
class CopyPipe {
public:
    // `scratch` (optional, kChunk bytes) lets open() also test a full-size copy.
    bool open(size_t askedBytes, unsigned char* scratch) {
        int p[2];
        if (pipe(p) != 0) { err_ = errno; return false; }
        rd_ = p[0]; wr_ = p[1];
        fcntl(rd_, F_SETFL, fcntl(rd_, F_GETFL, 0) | O_NONBLOCK);
        fcntl(wr_, F_SETFL, fcntl(wr_, F_GETFL, 0) | O_NONBLOCK);
#ifdef F_SETPIPE_SZ
        fcntl(wr_, F_SETPIPE_SZ, static_cast<int>(askedBytes ? askedBytes : (1u << 17)));     // may be refused: then we use what we got
#else
        (void)askedBytes;
#endif
        long sz = 0;
#ifdef F_GETPIPE_SZ
        sz = fcntl(wr_, F_GETPIPE_SZ);
#endif
        if (sz < 4096) sz = 4096;                                      // unknown: assume one page
        pipeBytes_ = static_cast<size_t>(sz);
        cap_ = pipeBytes_ < kChunk ? pipeBytes_ : kChunk;
        cap_ &= ~static_cast<size_t>(4095);
        if (cap_ < 4096) cap_ = 4096;
        // self-test with our own memory: a small copy that must come back unchanged, then (if given) a full-size one
        unsigned char a[256], b[256];
        for (int i = 0; i < 256; ++i) { a[i] = static_cast<unsigned char>(i * 7 + 3); b[i] = 0; }
        bool ok = copy(reinterpret_cast<uintptr_t>(a), b, sizeof a) && std::memcmp(a, b, sizeof a) == 0;
        if (ok && scratch) ok = copy(reinterpret_cast<uintptr_t>(scratch), scratch, cap_);
        trouble_ = 0;                                                  // the self-test does not count
        if (!ok) { close(); return false; }
        return true;
    }
    void close() { if (rd_ >= 0) ::close(rd_); if (wr_ >= 0) ::close(wr_); rd_ = wr_ = -1; }
    size_t chunk() const { return cap_; }               // the biggest copy that fits the pipe
    size_t pipeBytes() const { return pipeBytes_; }
    int trouble() const { return trouble_; }            // copies that failed for a reason other than "bad address"
    int lastErrno() const { return err_; }

    // Copies n bytes from address `from` to `to`. False if any byte could not be read (nothing is promised about `to` then).
    bool copy(uintptr_t from, unsigned char* to, size_t n) {
        std::lock_guard<std::mutex> lock(mu_);
        if (rd_ < 0 || n == 0 || n > cap_) return false;
        ssize_t w;
        do { w = write(wr_, reinterpret_cast<const void*>(from), n); } while (w < 0 && errno == EINTR);
        if (w != static_cast<ssize_t>(n)) {
            if (w < 0 && errno != EFAULT) { ++trouble_; err_ = errno; }
            drain();                                                    // something in the range could not be read: throw away what was copied
            return false;
        }
        size_t got = 0;
        while (got < n) {
            const ssize_t r = read(rd_, to + got, n - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) { ++trouble_; err_ = r < 0 ? errno : 0; drain(); return false; }
            got += static_cast<size_t>(r);
        }
        return true;
    }
private:
    void drain() { unsigned char d[4096]; for (int i = 0; i < 64; ++i) { const ssize_t r = read(rd_, d, sizeof d); if (r <= 0) break; } }
    int rd_ = -1, wr_ = -1; size_t cap_ = 4096, pipeBytes_ = 4096; int trouble_ = 0, err_ = 0; std::mutex mu_;
};

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
std::atomic<const char*> gStep{"not started"};

// the class pointer is kept inverted, so a leftover copy in freed memory can never look like a running object
struct ClassRef {
    uintptr_t inv = 0; int asmIndex = 0; std::string name, full;
    void set(void* k) { inv = ~reinterpret_cast<uintptr_t>(k); }
    void* klass() const { return reinterpret_cast<void*>(~inv); }
};
struct AsmRef { std::string name; bool skip, ours; };
struct LivePlan { uint64_t klassInv; std::string full; bool unityObject; int size; std::vector<FieldPlan> fields; };   // klassInv = ~klass, so our own lists never look like a live object
struct LiveObject { int plan; uintptr_t addr; std::string where; std::vector<unsigned char> bytes; };

// ---- the memory search runs on its OWN thread, so a read that never returns cannot freeze the scan:
// the scan thread watches a heartbeat and gives up on the search if it stops.
struct PlanLite { uint64_t klassInv; bool unityObject; int size; };
struct Cand { int plan; uintptr_t addr; int region; };
struct SearchJob {
    // inputs (set before the thread starts, never changed afterwards)
    std::vector<PlanLite> plans;
    std::vector<MapEntry> maps;
    int maxSeconds = 90, testHangAfterChunks = 0; size_t testPipeBytes = 0;
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
            j.heartbeat.fetch_add(1);
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
        j.heartbeat.fetch_add(1);
        ++j.candidatesPer[cd.plan];
        const PlanLite& p = j.plans[cd.plan];
        unsigned char hdr[24];
        if (!pipe.copy(cd.addr, hdr, sizeof hdr)) { ++j.rejUnreadable; continue; }
        uint64_t monitor, cached; std::memcpy(&monitor, hdr + 8, 8); std::memcpy(&cached, hdr + 16, 8);
        if (monitor != 0) { ++j.rejMonitor; continue; }
        if (p.unityObject && (cached == 0 || (cached & 7) != 0)) { ++j.rejCached; continue; }     // a destroyed or fake object
        ++j.acceptedPer[cd.plan];
        if (j.acceptedPer[cd.plan] > kMaxLivePrinted) continue;
        LiveObject lo; lo.plan = cd.plan; lo.addr = cd.addr;
        size_t got = 0;                                       // the whole object if it can be read, else a smaller front part
        for (size_t want = std::min(static_cast<size_t>(p.size), pipe.chunk()); want >= 16 && got == 0; want /= 2) {
            lo.bytes.assign(want, 0);
            if (pipe.copy(cd.addr, lo.bytes.data(), want)) got = want;
        }
        if (got == 0) { --j.acceptedPer[cd.plan]; ++j.rejUnreadable; continue; }
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
    return ok;
}

const char* currentStep() { return gStep.load(); }

Summary run(const Api& api, uintptr_t libBase, LogFn log) {
    Options o; o.libBase = libBase;
    return run(api, o, log);
}

Summary run(const Api& api, const Options& opt, LogFn log) {
    Summary sum;
    Out out(log);
    gStep.store("starting");
    out.line("--- game code scan (read-only) ---");
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
        out.line("scan: assembly %s classes=%d %s", ar.name.c_str(), static_cast<int>(count), ar.skip ? "(engine / system: skipped)" : (ar.ours ? "(the game's own code)" : "(other: names only)"));
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
    out.line("--- index of class names (the game's own code + Normal.*) ---");
    for (const ClassRef& c : classes) {
        const AsmRef& ar = asms[c.asmIndex];
        if (!(ar.ours || startsWith(ar.name, "Normal."))) continue;
        if (!indexNameMatches(c.name)) continue;
        if (indexLines >= kMaxIndexLines) { out.line("scan: (index stopped at %d lines)", kMaxIndexLines); break; }
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
    auto detail = [&](size_t idx, bool priority) {
        const ClassRef& c = classes[idx];
        const AsmRef& ar = asms[c.asmIndex];
        void* parent = api.class_get_parent(c.klass());
        const char* pn = parent ? api.class_get_name(parent) : nullptr;
        out.line("scan: CLASS %s : %s   [assembly %s]", c.full.c_str(), pn ? pn : "-", ar.name.c_str());
        const int maxF = priority ? kMaxFieldsPriority : kMaxFieldsPerClass, maxM = priority ? kMaxMethodsPriority : kMaxMethodsPerClass;
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
            out.line("scan:   method %s(%u) : %s %s%s", mn ? mn : "?", api.method_get_param_count(m), typeName(api, api.method_get_return_type(m)).c_str(), where, (mflags & 0x10) ? " static" : "");
        }
        detailed[idx] = 1; ++sum.matchedClasses;
        if (priority) ++priorityCount; else ++otherCount;
    };
    for (int pass = 0; pass < 2; ++pass) {               // important classes: the named ones first, then anything vertical / jump / gravity / parameters
        for (size_t i = 0; i < classes.size(); ++i) {
            const ClassRef& c = classes[i];
            if (detailed[i] || !asms[c.asmIndex].ours || priorityCount >= kMaxPriorityDetail) continue;
            const std::string low = lowerOf(c.name.c_str());
            const bool named = inList(c.name, kPriorityClasses);
            const bool strong = low.find("vertical") != std::string::npos || low.find("parameters") != std::string::npos ||
                                low.find("jump") != std::string::npos || low.find("gravit") != std::string::npos;
            if (pass == 0 ? !named : !strong) continue;
            detail(i, true);
        }
    }
    for (size_t i = 0; i < classes.size(); ++i) {        // other movement-ish classes (the Oculus teleport samples were already listed by stage D4)
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

    // ---- 5. plan the live search while the runtime can still be asked (field names, types, positions)
    gStep.store("5 of 6: planning the live search");
    out.line("scan: step 5 of 6: planning the live search");
    std::vector<LivePlan> plans;
    if (opt.searchMemory) {
        for (const ClassRef& c : classes) {
            if (static_cast<int>(plans.size()) >= kMaxLiveTargets) break;
            if (!asms[c.asmIndex].ours || !inList(c.name, kLiveClasses)) continue;
            LivePlan p; p.klassInv = ~static_cast<uint64_t>(reinterpret_cast<uintptr_t>(c.klass())); p.full = c.full;
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
        bool expected = false;
        if (!gSearchBusy.compare_exchange_strong(expected, true)) {
            out.key("scan: an earlier memory search is still stuck in this game session, so no new one is started (close and reopen the game to try the live values again)");
            stuck = true; stuckWhere = "an earlier search is still stuck"; job.reset();
        } else {
            pthread_t th; auto* holder = new std::shared_ptr<SearchJob>(job);
            if (pthread_create(&th, nullptr, searchThread, holder) != 0) {
                delete holder; gSearchBusy.store(false);
                out.key("scan: could not start the memory search thread");
                stuck = true; stuckWhere = "could not start the memory search thread"; job.reset();
            } else {
                pthread_detach(th);
                const double tStart = nowSeconds();
                double lastBeatAt = tStart, lastTalk = tStart; unsigned lastBeat = job->heartbeat.load();
                while (!job->done.load()) {
                    usleep(100000);
                    const double now = nowSeconds();
                    const unsigned hb = job->heartbeat.load();
                    if (hb != lastBeat) { lastBeat = hb; lastBeatAt = now; }
                    if (now - lastTalk >= 10.0) {
                        lastTalk = now;
                        out.key("scan: memory search still working: %llu MB read so far, %s", job->bytes.load() >> 20, describeRegion(*job, job->region.load()).c_str());
                    }
                    if (now - lastBeatAt >= opt.stallSeconds || now - tStart >= opt.maxSeconds + 30.0) {
                        stuck = true;
                        char b[400]; std::snprintf(b, sizeof b, "%s, step %d, after %llu MB", describeRegion(*job, job->region.load()).c_str(), job->stage.load(), job->bytes.load() >> 20);
                        stuckWhere = b;
                        break;
                    }
                }
            }
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
            for (size_t t = 0; t < plans.size(); ++t) {
                out.key("scan: live %s: %d hit(s), %d look like a real running copy%s", plans[t].full.c_str(), job->candidatesPer[t], job->acceptedPer[t],
                         job->acceptedPer[t] > kMaxLivePrinted ? " (first few written out)" : "");
            }
            std::vector<std::vector<std::string>> firstValues(plans.size());       // the values of the first copy of each class
            for (size_t i = 0; i < job->found.size(); ++i) {
                const LiveObject& lo = job->found[i];
                const LivePlan& p = plans[lo.plan];
                int ordinal = 1; for (size_t k = 0; k < i; ++k) if (job->found[k].plan == lo.plan) ++ordinal;
                char cachedText[40] = "n/a";
                if (p.unityObject) { uint64_t cp; std::memcpy(&cp, lo.bytes.data() + 16, 8); std::snprintf(cachedText, sizeof cachedText, "%s", cp ? "set" : "empty"); }
                out.key("scan: live %s #%d size=%d native-link=%s in %s%s", p.full.c_str(), ordinal, p.size, cachedText, lo.where.c_str(),
                        ordinal > 1 ? "   (only the fields that differ from copy #1 are listed)" : "");
                std::vector<std::string>& first = firstValues[lo.plan];
                for (size_t fi = 0; fi < p.fields.size(); ++fi) {
                    const FieldPlan& f = p.fields[fi];
                    const std::string v = formatValue(f, lo.bytes.data(), lo.bytes.size());
                    if (ordinal == 1) first.push_back(v);
                    else if (fi < first.size() && first[fi] == v) continue;
                    out.key("scan:   live %s = %s   (%s : %s @%d)", f.name.c_str(), v.c_str(), f.owner.c_str(), f.typeName.c_str(), f.offset);
                }
                ++sum.liveObjects;
            }
        }
    }
    if (stuck) {
        sum.memoryStuck = true;
        out.key("scan: MEMORY SEARCH STUCK and given up on (%s). The class, field and method lists above are complete; only the live values are missing. The game is not affected.", stuckWhere.c_str());
    }

    gStep.store("finished");
    out.key("scan: DONE. assemblies=%d classes=%d index=%d detailed=%d field-hits=%d type-hits=%d live-copies=%d%s%s", sum.assemblies, sum.classes, sum.indexed, sum.matchedClasses, sum.fieldHits, sum.typeHits, sum.liveObjects,
             sum.memoryStuck ? " (live search stuck)" : "", out.full() ? " (output was cut at the line limit)" : "");
    sum.ok = true;
    return sum;
}

}  // namespace tzscan
