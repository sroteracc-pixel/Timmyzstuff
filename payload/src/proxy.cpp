// proxy.cpp - STAGE D1 payload: the Stage A pass-through PLUS a read-only "input probe" PLUS a
// "frame watcher" that only COUNTS the game's VR frame calls (see frame_stubs.h). Draws nothing.
//
// PART 1 - PASS-THROUGH (unchanged from Stage A, proven on your Quest)
//   The patcher renames the game's real library to libmain_orig.so and puts THIS file in its
//   place, named libmain.so. When the game starts it loads this file first. We:
//     1. work out where we are (e.g. .../lib/arm64/libmain.so),
//     2. load the real library sitting next to us (.../libmain_orig.so),
//     3. call the real library's JNI_OnLoad and return what it returns,
//   so the game starts exactly as it normally would.
//
// PART 2 - FACTS PROBE (new, READ-ONLY)
//   After the game has started, a small background thread writes a text file of facts:
//     * which graphics/VR libraries the game really loaded (Vulkan vs OpenGL),
//     * which of Meta's VR functions (ovrp_*) exist (by NAME only - no addresses),
//     * the raw bytes ovrp_GetControllerState4 returns while you press buttons, so the exact
//       layout can be READ from real data instead of guessed.
//   It changes nothing in the game and draws nothing. No menu yet.
//
//   The ONLY calls into the game's VR library are ovrp_GetInitialized and
//   ovrp_GetControllerState4 - both just read. Documented shape (OVRPlugin.cs, v1.16+):
//   `result f(uint controllerMask, ControllerState4* out)`. We pass a 512-byte zeroed buffer,
//   far larger than that struct, so it cannot overflow.

#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "menu_input.h"   // the open/close rules (pure C++, tested on a PC)
#include "frame_stubs.h"  // counting doorways for ovrp_EndFrame4 / BeginFrame4 / WaitToBeginFrame

#ifdef __ANDROID__
#include <android/log.h>
#endif

static void logf_(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "Timmyzstuff", "%s", buf);
#else
    std::fprintf(stderr, "[Timmyzstuff] %s\n", buf);   // used by the PC test
#endif
}

// "/x/lib/arm64/libmain.so" -> "/x/lib/arm64/libmain_orig.so"
static std::string originalPathFor(const std::string& self) {
    const std::string ext = ".so";
    if (self.size() > ext.size() && self.compare(self.size() - ext.size(), ext.size(), ext) == 0)
        return self.substr(0, self.size() - ext.size()) + "_orig.so";
    return self + "_orig.so";
}

namespace {

#ifdef TZ_FAST_TEST          // PC test only: shorter waits
const int kWaitForLibSeconds = 5;
const int kWaitForInitSeconds = 3;
const int kSampleMillis = 3000;
const int kSettleSeconds = 1;
const int kBeatSeconds = 1;
const int kWatchTrySeconds = 3;
const int kWatchFirstSeconds = 1;
const double kWatchEvery = 1.0;
#else
const int kWaitForLibSeconds = 600;   // wait up to 10 min for the game to load its VR library
const int kWaitForInitSeconds = 120;  // then up to 2 min for it to finish starting
const int kSampleMillis = 600 * 1000; // then watch the controllers for 10 minutes
const int kSettleSeconds = 10;        // pause so the graphics drivers finish loading
const int kBeatSeconds = 10;          // how often a "frames per second" line is written
const int kWatchFirstSeconds = 8;      // wait for the game to finish setting up before the first search
const double kWatchEvery = 10.0;       // each search reads the whole heap, so do it rarely
const int kWatchTrySeconds = 90;      // keep looking for the plugin's function list this long
#endif
const int kMaxChangeLines = 20;       // raw controller lines: only enough to confirm the layout
const int kMaxPoseSnapshots = 3;      // hand/head position snapshots (taken when A or B is pressed)
const size_t kBufSize = 512;          // zeroed buffer handed to the controller function
const size_t kDumpBytes = 192;        // how many bytes of it we write down

FILE* gOut = nullptr;

void fact(const char* fmt, ...) {
    if (!gOut) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(gOut, fmt, ap);
    va_end(ap);
    std::fputc('\n', gOut);
    std::fflush(gOut);
}

double secondsSinceStart() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static double first = ts.tv_sec + ts.tv_nsec / 1e9;
    return ts.tv_sec + ts.tv_nsec / 1e9 - first;
}

void mkdirs(const std::string& path) {          // like "mkdir -p"
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0775);
    }
}

std::string packageName() {                      // the game's package = its process name
    char buf[256] = {0};
    FILE* f = std::fopen("/proc/self/cmdline", "r");
    if (f) { std::fread(buf, 1, sizeof buf - 1, f); std::fclose(f); }
    return buf[0] ? std::string(buf) : std::string("unknown.package");
}

// Open the facts file in the first folder the game is allowed to write to.
FILE* openFactsFile(std::string& chosen) {
    std::vector<std::string> dirs;
#ifdef __ANDROID__
    const std::string pkg = packageName();
    const int user = static_cast<int>(getuid() / 100000);
    char b[96];
    dirs.push_back("/sdcard/Android/data/" + pkg + "/files");
    std::snprintf(b, sizeof b, "/storage/emulated/%d/Android/data/", user);
    dirs.push_back(std::string(b) + pkg + "/files");
    std::snprintf(b, sizeof b, "/data/user/%d/", user);
    dirs.push_back(std::string(b) + pkg + "/files");
#else
    const char* d = std::getenv("TZ_FACTS_DIR");
    dirs.push_back(d ? d : "/tmp");
#endif
    for (const std::string& dir : dirs) {
        mkdirs(dir);
        const std::string path = dir + "/timmyzstuff_facts.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) { chosen = path; return f; }
    }
    return nullptr;
}

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// File names (not paths) of every library mapped into the game right now.
std::set<std::string> mappedLibraryNames(std::string* ovrPath = nullptr) {
    std::set<std::string> names;
    FILE* m = std::fopen("/proc/self/maps", "r");
    if (!m) return names;
    char line[1024];
    while (std::fgets(line, sizeof line, m)) {
        char* slash = std::strrchr(line, '/');
        if (!slash) continue;
        std::string base(slash + 1);
        while (!base.empty() && (base.back() == '\n' || base.back() == '\r')) base.pop_back();
        if (base.find(".so") == std::string::npos) continue;
        names.insert(base);
        if (ovrPath && base == "libOVRPlugin.so" && ovrPath->empty()) {
            const char* start = std::strchr(line, '/');
            if (start) {
                std::string full(start);
                while (!full.empty() && (full.back() == '\n' || full.back() == '\r')) full.pop_back();
                *ovrPath = full;
            }
        }
    }
    std::fclose(m);
    return names;
}

void writeLoadedLibraries(const char* when) {
    static const char* interesting[] = {"vulkan", "gles", "egl", "vrapi", "ovr", "oculus", "unity",
                                        "il2cpp", "adreno", "mali", "libmain", "xr"};
    fact("--- libraries loaded %s ---", when);
    int shown = 0;
    for (const std::string& n : mappedLibraryNames()) {
        const std::string l = lower(n);
        for (const char* key : interesting) {
            if (l.find(key) != std::string::npos) { fact("loaded: %s", n.c_str()); ++shown; break; }
        }
    }
    if (shown == 0) fact("(none of the interesting ones)");
}

void* findOvrPlugin() {
    for (int i = 0; i < kWaitForLibSeconds; ++i) {
        void* h = dlopen("libOVRPlugin.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) {
            std::string path;
            mappedLibraryNames(&path);
            if (!path.empty()) h = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        }
        if (h) return h;
        sleep(1);
    }
    return nullptr;
}

typedef int (*GetInitializedFn)();
typedef int (*GetControllerState4Fn)(unsigned int mask, void* out);

void hex(const unsigned char* p, size_t n, char* out, size_t outSize) {
    size_t w = 0;
    for (size_t i = 0; i < n && w + 3 <= outSize; ++i) w += std::snprintf(out + w, outSize - w, "%02x", p[i]);
    out[w < outSize ? w : outSize - 1] = 0;
}

float asFloat(const unsigned char* p) { float f; std::memcpy(&f, p, 4); return f; }

// "Worth writing down" filter: the first 16 bytes differ, or any of the next 8 decimal numbers
// moved by more than 0.05. A wrong guess about the layout only means more or fewer lines.
bool worthWriting(const unsigned char* a, const unsigned char* b) {
    if (std::memcmp(a, b, 16) != 0) return true;
    for (int i = 0; i < 8; ++i) {
        const float x = asFloat(a + 16 + i * 4), y = asFloat(b + 16 + i * 4);
        if (!(std::fabs(x - y) <= 0.05f)) return true;       // NaN also counts as "changed"
    }
    return false;
}


// ---- the frame watcher ------------------------------------------------------------------
// Finds the game's VR plugin (libOculusXRPlugin.so) in memory, looks through its WRITABLE data for
// the saved address of a Meta function, and swaps that one saved address for our counting stub.
// Only memory that /proc/self/maps lists as read+write is ever touched.
// Copies memory without ever crashing: the kernel does the read through a pipe and just says "no" for a bad address.
bool safeCopy(uintptr_t from, unsigned char* to, size_t n) {
    static int pfd[2] = {-1, -1};
    if (pfd[0] < 0) {
        if (pipe(pfd) != 0) { pfd[0] = pfd[1] = -1; return false; }
#ifdef F_SETPIPE_SZ
        fcntl(pfd[1], F_SETPIPE_SZ, 1 << 17);
#endif
    }
    if (n > 65536) n = 65536;
    const ssize_t w = write(pfd[1], reinterpret_cast<const void*>(from), n);
    if (w != static_cast<ssize_t>(n)) { unsigned char d[4096]; if (w > 0) { ssize_t left = w; while (left > 0) { ssize_t r = read(pfd[0], d, left > 4096 ? 4096 : left); if (r <= 0) break; left -= r; } } return false; }
    size_t got = 0;
    while (got < n) { const ssize_t r = read(pfd[0], to + got, n - got); if (r <= 0) return false; got += r; }
    return true;
}

struct MapRegion { uintptr_t start, end; bool rw; std::string path; };

std::vector<MapRegion> readMaps() {
    std::vector<MapRegion> out;
    FILE* m = std::fopen("/proc/self/maps", "r");
    if (!m) return out;
    char line[1024];
    while (std::fgets(line, sizeof line, m)) {
        unsigned long a = 0, b = 0; char perms[8] = {0}; int pos = 0;
        if (std::sscanf(line, "%lx-%lx %7s %*s %*s %*s %n", &a, &b, perms, &pos) < 3) continue;
        MapRegion r;
        r.start = a; r.end = b; r.rw = (perms[0] == 'r' && perms[1] == 'w');
        r.path = pos > 0 ? std::string(line + pos) : std::string();
        while (!r.path.empty() && (r.path.back() == '\n' || r.path.back() == '\r' || r.path.back() == ' ')) r.path.pop_back();
        out.push_back(r);
    }
    std::fclose(m);
    return out;
}

bool gWatchPatched[3] = {false, false, false};

// Only the program's own heap is touched: that is where the game's VR plugin keeps its table of
// saved Meta function addresses (found by the stage D1c report). The plugin's own data, other
// libraries, stacks and OUR OWN variables are never touched.
bool isHeapRegion(const MapRegion& r) {
    if (!r.rw || r.end - r.start > (256UL << 20)) return false;
    if (r.path.find("libc_malloc") != std::string::npos || r.path.find("scudo") != std::string::npos || r.path == "[heap]") return true;
    return false;
}

// Replaces every saved copy of `target` in the heap, reading safely. Returns how many.
int patchHeapSlot(uint64_t target, uint64_t stub, std::vector<unsigned long>* where) {
    static unsigned char chunk[65536];
    int patched = 0;
    for (const MapRegion& r : readMaps()) {
        if (!isHeapRegion(r)) continue;
        for (uintptr_t c = r.start; c < r.end; c += sizeof(chunk)) {
            const size_t n = r.end - c < sizeof(chunk) ? static_cast<size_t>(r.end - c) : sizeof(chunk);
            if (!safeCopy(c, chunk, n)) continue;
            for (size_t o = 0; o + 8 <= n; o += 8) {
                uint64_t v; std::memcpy(&v, chunk + o, 8);
                if (v != target) continue;
                volatile uint64_t* slot = reinterpret_cast<volatile uint64_t*>(c + o);
                if (*slot != target) continue;                  // changed since we read it
                *slot = stub;                                   // one aligned 8-byte store
                if (where) where->push_back(static_cast<unsigned long>(c + o));
                ++patched;
            }
        }
    }
    return patched;
}

// Tries to install any watch that is not installed yet. Returns true when all three are done.
bool tryInstallWatches(void* ovr) {
    bool all = true;
    for (int i = 0; i < frameWatchCount(); ++i) {
        FrameWatch* w = frameWatch(i);
        if (gWatchPatched[i]) continue;
        void* real = dlsym(ovr, w->name);
        if (!real) { all = false; continue; }
        *w->orig = reinterpret_cast<uint64_t>(real);           // set BEFORE patching, so the stub can always jump on
        std::vector<unsigned long> where;
        const int n = patchHeapSlot(reinterpret_cast<uint64_t>(real), reinterpret_cast<uint64_t>(w->stub), &where);
        if (n > 0) {
            gWatchPatched[i] = true;
            fact("frame watch %s: installed in %d heap slot(s), first at %lx", w->name, n, where.empty() ? 0UL : where[0]);
        } else {
            all = false;
        }
    }
    return all;
}



// Read-only report: where are the plugin's regions, and where does each Meta address appear in memory?
void diagnoseWatches(void* ovr, const char* when) {
    static unsigned char chunk[65536];
    const std::vector<MapRegion> maps = readMaps();
    fact("diag (%s): plugin and neighbouring regions:", when);
    int shown = 0;
    for (size_t i = 0; i < maps.size() && shown < 24; ++i) {
        if (maps[i].path.find("libOculusXRPlugin.so") == std::string::npos) continue;
        for (size_t j = i; j < maps.size() && j < i + 6; ++j, ++shown)
            fact("diag   region %lx-%lx %s size=%lu name='%s'", (unsigned long)maps[j].start, (unsigned long)maps[j].end,
                 maps[j].rw ? "rw" : "other", (unsigned long)(maps[j].end - maps[j].start), maps[j].path.c_str());
        break;
    }
    for (int i = 0; i < frameWatchCount(); ++i) {
        FrameWatch* w = frameWatch(i);
        void* real = dlsym(ovr, w->name);
        if (!real) { fact("diag %s: dlsym found nothing", w->name); continue; }
        const uint64_t target = reinterpret_cast<uint64_t>(real);
        int found = 0;
        for (const MapRegion& r : maps) {
            if (!r.rw || r.end - r.start > (256UL << 20)) continue;
            if (r.path.compare(0, 5, "/dev/") == 0 || r.path.find("dmabuf") != std::string::npos ||
                r.path.find("kgsl") != std::string::npos || r.path.find("ashmem") != std::string::npos ||
                r.path.find("memfd") != std::string::npos) continue;
            for (uintptr_t c = r.start; c < r.end; c += sizeof(chunk)) {
                size_t n = r.end - c < sizeof(chunk) ? static_cast<size_t>(r.end - c) : sizeof(chunk);
                if (!safeCopy(c, chunk, n)) continue;           // unreadable piece: skip, never crash
                for (size_t o = 0; o + 8 <= n; o += 8) {
                    uint64_t v; std::memcpy(&v, chunk + o, 8);
                    if (v == target && found < 8) {
                        fact("diag %s: address %llx found at %lx in region %lx-%lx name='%s'", w->name,
                             (unsigned long long)target, (unsigned long)(c + o), (unsigned long)r.start, (unsigned long)r.end, r.path.c_str());
                        uint64_t nb[8] = {0};
                        const uintptr_t from = (c + o >= 24) ? c + o - 24 : c + o;
                        if (safeCopy(from, reinterpret_cast<unsigned char*>(nb), sizeof nb))
                            fact("diag   neighbours (8 slots from -3): %llx %llx %llx %llx %llx %llx %llx %llx",
                                 (unsigned long long)nb[0], (unsigned long long)nb[1], (unsigned long long)nb[2], (unsigned long long)nb[3],
                                 (unsigned long long)nb[4], (unsigned long long)nb[5], (unsigned long long)nb[6], (unsigned long long)nb[7]);
                        ++found;
                    }
                }
            }
        }
        if (!found) fact("diag %s: address %llx found NOWHERE in writable memory", w->name, (unsigned long long)target);
    }
}

// Writes small argument values as numbers, and anything big (a pointer) as "ptr".
void fmtArg(uint64_t v, char* out, size_t n) {
    if (v < 1000000ULL) std::snprintf(out, n, "%llu", static_cast<unsigned long long>(v));
    else std::snprintf(out, n, "ptr");
}

void writeFrameLine(const char* tag, double now, double sinceLast, const uint64_t* lastCounts) {
    char line[512]; size_t w = 0;
    w += std::snprintf(line + w, sizeof line - w, "%s t=%.0f:", tag, now);
    for (int i = 0; i < frameWatchCount(); ++i) {
        FrameWatch* w2 = frameWatch(i);
        const uint64_t c = *w2->count;
        const double perSec = sinceLast > 0 ? (c - lastCounts[i]) / sinceLast : 0.0;
        w += std::snprintf(line + w, sizeof line - w, " %s calls=%llu (%.1f/s)", w2->name, static_cast<unsigned long long>(c), perSec);
    }
    FrameWatch* e = frameWatch(0);
    char a[8][16];
    for (int i = 0; i < 8; ++i) fmtArg(e->args[i], a[i], sizeof a[i]);
    std::snprintf(line + w, sizeof line - w, " | EndFrame4 latest args: %s %s %s %s %s %s %s %s",
                  a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    fact("%s", line);
}

void* probeMain(void*) {
    secondsSinceStart();                               // start the clock
    std::string where;
    gOut = openFactsFile(where);
    if (!gOut) { logf_("facts: could not open a facts file anywhere"); return nullptr; }
    logf_("facts file: %s", where.c_str());

    fact("Timmyzstuff facts (stage D1: input probe + frame watcher) - draws nothing");
    fact("package: %s", packageName().c_str());
    fact("this file: %s", where.c_str());
    writeLoadedLibraries("at start");

    void* ovr = findOvrPlugin();
    if (!ovr) {
        fact("libOVRPlugin.so never appeared within %d seconds", kWaitForLibSeconds);
        std::fclose(gOut); gOut = nullptr;
        return nullptr;
    }
    fact("t=%.1fs libOVRPlugin.so is loaded", secondsSinceStart());

    static const char* names[] = {"ovrp_GetInitialized", "ovrp_GetControllerState4", "ovrp_GetControllerState",
                                  "ovrp_GetInputState", "ovrp_SetupLayer", "ovrp_EnqueueSetupLayer2",
                                  "ovrp_EnqueueSubmitLayer2", "ovrp_EndFrame4", "ovrp_BeginFrame4",
                                  "ovrp_GetLayerAndroidSurfaceObject", "ovrp_GetNodePoseState3",
                                  "ovrp_GetAppHasInputFocus", nullptr};
    for (int i = 0; names[i]; ++i) fact("function %s: %s", names[i], dlsym(ovr, names[i]) ? "found" : "MISSING");

    GetInitializedFn initFn = reinterpret_cast<GetInitializedFn>(dlsym(ovr, "ovrp_GetInitialized"));
    GetControllerState4Fn ctrlFn = reinterpret_cast<GetControllerState4Fn>(dlsym(ovr, "ovrp_GetControllerState4"));
    if (!ctrlFn) {
        fact("ovrp_GetControllerState4 is missing, so there is nothing to sample");
        std::fclose(gOut); gOut = nullptr;
        return nullptr;
    }

    bool ready = false;
    for (int i = 0; i < kWaitForInitSeconds; ++i) {
        if (initFn && initFn()) { ready = true; break; }
        if (!initFn && i >= 30) break;
        sleep(1);
    }
    fact("t=%.1fs VR system initialised: %s", secondsSinceStart(), ready ? "yes" : "NOT CONFIRMED (sampling anyway)");
    sleep(kSettleSeconds);
    writeLoadedLibraries("after the VR system started");

    // ---- hand/head position function (read-only) ------------------------------------------
    // Shape CONFIRMED by reading the function's machine code in your libOVRPlugin.so:
    //   int ovrp_GetNodePoseState3(int step, int frameIndex, int node, void* out)
    // It refuses a null `out` and otherwise writes exactly 88 bytes. We pass 256 zeroed bytes.
    // What is NOT confirmed: which node numbers mean "left hand" / "right hand", and which
    // step / frame values give a live pose. So we try a few and write what comes back.
    typedef int (*GetNodePoseState3Fn)(int step, int frameIndex, int node, void* out);
    GetNodePoseState3Fn poseFn = reinterpret_cast<GetNodePoseState3Fn>(dlsym(ovr, "ovrp_GetNodePoseState3"));
    int poseSnapshots = 0;
    auto poseSnapshot = [&](const char* why) {
        if (!poseFn || poseSnapshots >= kMaxPoseSnapshots) return;
        ++poseSnapshots;
        fact("--- pose snapshot %d (%s) at t=%.1f: point the left and right controllers at DIFFERENT places ---",
             poseSnapshots, why, secondsSinceStart());
        static const int steps[] = {-1, 0};
        static const int frames[] = {0, -1};
        for (int step : steps) for (int frame : frames) for (int node = 0; node < 8; ++node) {
            alignas(16) unsigned char pb[256];
            std::memset(pb, 0, sizeof pb);
            const int rc = poseFn(step, frame, node, pb);
            char hx[2 * 88 + 1];
            hex(pb, 88, hx, sizeof hx);
            fact("pose step=%d frame=%d node=%d rc=%d bytes=%s", step, frame, node, rc, hx);
        }
    };
    poseSnapshot("at start");

    // ---- controllers -> the real open/close rules ------------------------------------------
    fact("--- controller test: hold BOTH triggers and click A to open; B closes ---");
    fact("layout (matches your stage B file): bytes 4-7 buttons (A=1 B=2 X=256 Y=512), floats at 16 (left trigger) and 20 (right trigger)");
    MenuInput menu;                                      // the same rules the real menu will use
    alignas(16) unsigned char buf[kBufSize];
    alignas(16) unsigned char prev[kBufSize];
    std::memset(prev, 0, sizeof prev);
    int lines = 0, lastRc = -12345, opens = 0, closes = 0;
    long samples = 0;
    bool wasVisible = false;
    double last = secondsSinceStart();
    double nextBeat = last + kBeatSeconds;
    double nextWatchTry = last + kWatchFirstSeconds;
    const double watchGiveUp = last + kWatchTrySeconds;
    bool watchesDone = false;
    bool diag2 = false;
    uint64_t lastCounts[3] = {0, 0, 0};
    double lastBeatTime = last;
    const double endAt = last + kSampleMillis / 1000.0;
    while (secondsSinceStart() < endAt) {
        std::memset(buf, 0, sizeof buf);
        const int rc = ctrlFn(0x3u, buf);               // 0x3 = left + right Touch controller
        ++samples;
        const double now = secondsSinceStart();

        ControllerState pad;
        const unsigned int buttons = *reinterpret_cast<const unsigned int*>(buf + 4);
        pad.leftTrigger = asFloat(buf + 16);
        pad.rightTrigger = asFloat(buf + 20);
        pad.buttonA = (buttons & 0x1u) != 0;
        pad.buttonB = (buttons & 0x2u) != 0;
        const bool aPressed = pad.buttonA && !(*reinterpret_cast<const unsigned int*>(prev + 4) & 0x1u);
        const bool bPressed = pad.buttonB && !(*reinterpret_cast<const unsigned int*>(prev + 4) & 0x2u);
        const bool visible = menu.update(pad, static_cast<float>(now - last));
        last = now;
        if (visible && !wasVisible) { ++opens; fact("t=%.2f MENU OPEN  (#%d) - both triggers + A", now, opens); }
        if (!visible && wasVisible) { ++closes; fact("t=%.2f MENU CLOSE (#%d) - B pressed", now, closes); }
        wasVisible = visible;
        if (aPressed) poseSnapshot("A pressed");
        else if (bPressed) poseSnapshot("B pressed");

        if (lines < kMaxChangeLines && (samples == 1 || rc != lastRc || worthWriting(prev, buf))) {
            char hx[2 * kDumpBytes + 1];
            hex(buf, kDumpBytes, hx, sizeof hx);
            fact("t=%.1f rc=%d bytes=%s", now, rc, hx);
            ++lines;
            lastRc = rc;
            if (lines == kMaxChangeLines) fact("(raw line limit reached - events below are still recorded)");
        }
        std::memcpy(prev, buf, sizeof prev);
        if (!watchesDone && now >= nextWatchTry && now < watchGiveUp) {
            watchesDone = tryInstallWatches(ovr);
            nextWatchTry = now + kWatchEvery;
            if (watchesDone) fact("t=%.1f all frame watches are installed", now);
        }
        if (!diag2 && now >= watchGiveUp) {
            diag2 = true;
            for (int i = 0; i < frameWatchCount(); ++i)
                if (!gWatchPatched[i]) fact("frame watch %s: NOT installed after %d seconds of searching", frameWatch(i)->name, kWatchTrySeconds);
            diagnoseWatches(ovr, "search ended");
        }
        if (now >= nextBeat) {
            fact("t=%.0f still sampling (%ld samples, menu opened %d times, closed %d times)", now, samples, opens, closes);
            if (gWatchPatched[0] || gWatchPatched[1] || gWatchPatched[2]) {
                writeFrameLine("frames", now, now - lastBeatTime, lastCounts);
                for (int i = 0; i < frameWatchCount(); ++i) lastCounts[i] = *frameWatch(i)->count;
                lastBeatTime = now;
            }
            nextBeat += kBeatSeconds;
        }
        usleep(20 * 1000);                              // 50 samples per second
    }
    if (!diag2) { diag2 = true; diagnoseWatches(ovr, "end of sampling"); }
    fact("summary: menu opened %d times, closed %d times", opens, closes);
    for (int i = 0; i < frameWatchCount(); ++i)
        fact("frame watch %s: %s, %llu calls counted in total", frameWatch(i)->name, gWatchPatched[i] ? "installed" : "NOT installed (the plugin never saved that address)", static_cast<unsigned long long>(*frameWatch(i)->count));
    fact("done: %ld samples, %d change lines", samples, lines);
    std::fclose(gOut);
    gOut = nullptr;
    return nullptr;
}

void startProbe() {
    static bool started = false;
    if (started) return;
    started = true;
    pthread_t t;
    if (pthread_create(&t, nullptr, probeMain, nullptr) == 0) pthread_detach(t);
    else logf_("facts: could not start the probe thread");
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    logf_("payload loaded (stage D1: pass-through + input probe + frame watcher)");

    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&JNI_OnLoad), &info) || !info.dli_fname) {
        logf_("ERROR: could not find my own path, so I can't find the original library");
        return JNI_ERR;
    }
    const std::string original = originalPathFor(info.dli_fname);
    logf_("I am %s -> loading original %s", info.dli_fname, original.c_str());

    void* handle = dlopen(original.c_str(), RTLD_NOW);   // never dlclose: the game keeps using it
    if (!handle) {
        const char* why = dlerror();
        logf_("ERROR: could not load the original library: %s", why ? why : "unknown");
        return JNI_ERR;                                    // fail loudly rather than half-start
    }

    typedef jint (*OnLoadFn)(JavaVM*, void*);
    OnLoadFn realOnLoad = reinterpret_cast<OnLoadFn>(dlsym(handle, "JNI_OnLoad"));
    if (!realOnLoad) {
        logf_("WARNING: the original has no JNI_OnLoad; returning JNI_VERSION_1_6");
        return JNI_VERSION_1_6;
    }

    const jint result = realOnLoad(vm, reserved);
    logf_("original JNI_OnLoad returned 0x%x", static_cast<unsigned>(result));
    if (result > 0) startProbe();                          // only after the game started fine
    return result;
}
