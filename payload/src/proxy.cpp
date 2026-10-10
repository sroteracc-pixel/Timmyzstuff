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
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <set>
#include <string>
#include <vector>

#include "menu_input.h"   // the open/close rules (pure C++, tested on a PC)
#include "frame_stubs.h"
#include "movement.h"     // the movement rules (stage D4)
#include "il2cpp_scan.h"   // read-only scan of the game's code (stage D4)
#include "game_link.h"     // the link that really changes speed / jump / gravity (stage D6)
#include "aimbot.h"        // the Aimbot rules and maths (stage D7)
#include "aim_link.h"      // the Aimbot's game part (stage D8): reads the ball when you let go and, if the rules say yes, aims it
#include "overlay.h"      // shows the menu picture in the headset (stage D2)  // counting doorways for ovrp_EndFrame4 / BeginFrame4 / WaitToBeginFrame

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
const double kOverlayDelay = 0.3;
const double kWatchEvery = 1.0;
#else
const int kWaitForLibSeconds = 600;   // wait up to 10 min for the game to load its VR library
const int kWaitForInitSeconds = 120;  // then up to 2 min for it to finish starting
const int kSampleMillis = 0;          // 0 = keep watching the controllers for the WHOLE game session (the menu needs this)
const int kSettleSeconds = 10;        // pause so the graphics drivers finish loading
const int kBeatSeconds = 10;          // how often a "frames per second" line is written
const double kOverlayDelay = 3.0;      // after the frame watcher is running, wait this long before creating the panel
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
    FILE* out = gOut;
    if (!out) return;
    char b[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(b, sizeof b - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > static_cast<int>(sizeof b) - 2) n = static_cast<int>(sizeof b) - 2;
    b[n++] = '\n';
    flockfile(out);                      // one whole line at a time, even when two threads (menu, scan, game link) write together
    std::fwrite(b, 1, static_cast<size_t>(n), out);
    std::fflush(out);
    funlockfile(out);
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

// ---- stage D7: which thread calls our doorways? --------------------------------------------------------------------
// The Aimbot must change the ball on the game's MAIN thread (Unity's functions are only safe there). The controller doorway is called by the
// game every frame; if it is the game's main thread, that is the place to do the work. We only count here; the facts file says which thread it is.
struct ThreadTally { std::atomic<int> tid[4]; std::atomic<unsigned long long> calls[4]; char name[4][20]; };
ThreadTally gInputThreads, gFrameThreads;
void tallyThread(ThreadTally& t) {
    static thread_local int me = 0;
    if (!me) me = static_cast<int>(syscall(SYS_gettid));
    for (int i = 0; i < 4; ++i) {
        int cur = t.tid[i].load(std::memory_order_relaxed);
        if (cur == 0) {      // a thread we have not seen: write down its name NOW (a thread that ends later can no longer be asked)
            char nm[20] = {0}; prctl(PR_GET_NAME, nm, 0, 0, 0);
            int zero = 0;
            if (t.tid[i].compare_exchange_strong(zero, -1)) { std::memcpy(t.name[i], nm, sizeof nm); t.tid[i].store(me); }
            cur = t.tid[i].load(std::memory_order_relaxed);
        }
        if (cur == me) { t.calls[i].fetch_add(1, std::memory_order_relaxed); return; }
    }
}
std::string describeTally(ThreadTally& t) {
    std::string r;
    for (int i = 0; i < 4; ++i) {
        const int tid = t.tid[i].load();
        if (tid <= 0) break;                                            // 0 = no more threads, -1 = a thread is being written down right now
        char nm[21]; std::memcpy(nm, t.name[i], 20); nm[20] = 0;
        char b[160]; std::snprintf(b, sizeof b, "%sthread %d '%s' (%llu calls)%s", i ? ", " : "", tid, nm, static_cast<unsigned long long>(t.calls[i].load()), tid == getpid() ? " [= the process's first thread]" : "");
        r += b;
    }
    return r.empty() ? "(never called)" : r;
}

bool gWatchPatched[3] = {false, false, false};

// The doorway for ovrp_EndFrame4 (index 0). Same counting as the asm stub, then the overlay code
// (which adds our menu panel to the layer list while the menu is open) calls the real function.
extern "C" int tzEndFrame4Hook(int frame, const void* const* layers, int n, void* extra) {
    tallyThread(gFrameThreads);
    FrameWatch* w = frameWatch(0);
    *w->count = *w->count + 1;
    w->args[0] = static_cast<uint64_t>(static_cast<unsigned>(frame));
    w->args[1] = reinterpret_cast<uint64_t>(layers);
    w->args[2] = static_cast<uint64_t>(static_cast<unsigned>(n));
    w->args[3] = reinterpret_cast<uint64_t>(extra);
    return tzoverlay::hookEndFrame4(frame, layers, n, extra);
}

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
                if (stub != 0) *slot = stub;                    // one aligned 8-byte store (stub == 0: only count, change nothing)
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
        if (i == 0) tzoverlay::setRealEndFrame4(reinterpret_cast<uint64_t>(real));
        void* doorway = (i == 0) ? reinterpret_cast<void*>(&tzEndFrame4Hook) : w->stub;
        std::vector<unsigned long> where;
        const int n = patchHeapSlot(reinterpret_cast<uint64_t>(real), reinterpret_cast<uint64_t>(doorway), &where);
        if (n > 0) {
            gWatchPatched[i] = true;
            fact("frame watch %s: installed in %d heap slot(s), first at %lx", w->name, n, where.empty() ? 0UL : where[0]);
        } else {
            all = false;
        }
    }
    return all;
}



// ---- stage D3: hide button presses from the game while the menu is open ---------------------
// Same trick as the frame watcher: the game's VR plugin keeps a saved copy of the address of
// ovrp_GetControllerState4. If we find it, we swap it for this doorway. The doorway calls the real
// function, then (ONLY while the menu is open) blanks the buttons / triggers / sticks it reports to the
// game. Our own menu reads the real function directly, so it still sees everything.
// NOT confirmed: the game may read its controllers a different way. The facts file says which.
uint64_t gInputReal = 0;
bool gInputPatched = false;
// stage D8: the Aimbot's game part. It does nothing at all until the menu's Aimbot switch is on. Its onGameThread() is called from this doorway because
// the facts file proved that the doorway is called ~185 times a second by the game's own thread "UnityMain" (the only caller) - the only place where
// it is safe to call the game's own functions.
// The three game-facing objects below are created once and NEVER destroyed on purpose: the probe thread and the game's threads keep using them while the
// process is shutting down, and a destroyed object makes them call into nothing (a PC test showed "pure virtual method called" at exit).
tzaimlink::AimLink& gAim = *new tzaimlink::AimLink;
extern "C" int tzControllerState4Hook(unsigned int mask, void* out) {
    typedef int (*Fn)(unsigned int, void*);
    const Fn real = reinterpret_cast<Fn>(gInputReal);
    if (!real) return -1;
    tallyThread(gInputThreads);
    const int rc = real(mask, out);
    gAim.onGameThread();                         // returns at once when the switch is off; never waits, never writes a file
    return tzoverlay::filterControllerState(rc, out);
}

void installInputBlock(void* ovr, int attempt) {
    static const char* others[] = {"ovrp_GetControllerState5", "ovrp_GetControllerState6", "ovrp_GetControllerState2", "ovrp_GetControllerState", nullptr};
    void* real = dlsym(ovr, "ovrp_GetControllerState4");
    if (!real) { fact("input block: ovrp_GetControllerState4 not found"); return; }
    gInputReal = reinterpret_cast<uint64_t>(real);                 // set BEFORE patching
    std::vector<unsigned long> where;
    const int n = patchHeapSlot(gInputReal, reinterpret_cast<uint64_t>(&tzControllerState4Hook), &where);
    if (n > 0) { gInputPatched = true; fact("input block: ovrp_GetControllerState4 doorway installed in %d heap slot(s), first at %lx", n, where.empty() ? 0UL : where[0]); }
    else fact("input block: try %d - ovrp_GetControllerState4 is not saved in the heap table", attempt);
    if (attempt == 1) {
        for (int i = 0; others[i]; ++i) {
            void* f = dlsym(ovr, others[i]);
            if (!f) { fact("input block: %s does not exist in this plugin", others[i]); continue; }
            fact("input block: %s exists; saved in %d heap slot(s) (read-only check, not changed)", others[i], patchHeapSlot(reinterpret_cast<uint64_t>(f), 0, nullptr));
        }
    }
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

// Writes down what the GAME itself hands to ovrp_EndFrame4 (its own layers), so the record layout can be
// checked against real data. Read-only and crash-proof.
void dumpGameLayers() {
    FrameWatch* e = frameWatch(0);
    const uint64_t arr = e->args[1];
    const int n = static_cast<int>(e->args[2]);
    fact("--- game layers: EndFrame4 latest call had %d layer(s), list at %s ---", n, arr ? "an address" : "NULL");
    if (!arr || n < 1 || n > 8) return;
    static unsigned char buf[0x1c0];
    uint64_t ptrs[8] = {0};
    if (!safeCopy(arr, reinterpret_cast<unsigned char*>(ptrs), sizeof(uint64_t) * n)) { fact("could not read the layer list"); return; }
    for (int i = 0; i < n; ++i) {
        std::memset(buf, 0, sizeof buf);
        if (!safeCopy(ptrs[i], buf, sizeof buf)) { fact("layer %d: unreadable", i); continue; }
        char hx[2 * 0x1c0 + 1];
        hex(buf, sizeof buf, hx, sizeof hx);
        int id, stage, vp[8]; float pose[7];
        std::memcpy(&id, buf, 4); std::memcpy(&stage, buf + 4, 4); std::memcpy(vp, buf + 8, 32); std::memcpy(pose, buf + 40, 28);
        fact("layer %d: id=%d stage=%d viewports=(%d,%d,%d,%d)(%d,%d,%d,%d) pose q=(%.2f %.2f %.2f %.2f) p=(%.2f %.2f %.2f)",
             i, id, stage, vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], vp[6], vp[7], pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6]);
        fact("layer %d bytes=%s", i, hx);
    }
}

// ---- stage D4: the "Scan game code" button -------------------------------------------------
// Runs on its own thread so the menu keeps working. Read-only: it only asks libil2cpp.so for names.
std::atomic<bool> gScanRunning{false};

// The scan itself runs on a second thread; this thread only waits for it. If the scan is still not finished after a long time
// (something in the game's runtime never answered), we say so in the facts file and tell the menu - the menu never shows "Scanning..." forever.
struct ScanJob { tzscan::Api api; tzscan::Options options; tzscan::Summary summary; std::atomic<bool> done{false}; };

void* scanWorker(void* p) {
    ScanJob* j = static_cast<ScanJob*>(p);
    j->summary = tzscan::run(j->api, j->options, fact);
    j->done.store(true);
    return nullptr;
}

// Finds the game's libil2cpp.so that is already loaded (never loads anything new). `base` = where it starts in memory.
void* openIl2cpp(uintptr_t* base) {
    void* lib = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    uintptr_t b = 0; std::string path;
    for (const MapRegion& r : readMaps()) {
        if (r.path.size() >= 12 && r.path.compare(r.path.size() - 12, 12, "libil2cpp.so") == 0) { if (!b || r.start < b) b = r.start; path = r.path; }
    }
    if (!lib && !path.empty()) lib = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (base) *base = b;
    return lib;
}

// For the movement link: gives it the game's il2cpp functions (asked again and again until the game has loaded them).
bool il2cppForLink(tzscan::Api* api, std::string* why) {
    void* lib = openIl2cpp(nullptr);
    if (!lib) { *why = "libil2cpp.so is not loaded in the game yet"; return false; }
    std::string missing;
    if (!tzscan::loadApi(lib, api, &missing)) { *why = "libil2cpp.so lacks: " + missing; return false; }
    return true;
}

void* scanMain(void* arg) {
    const tzscan::Topic topic = static_cast<tzscan::Topic>(reinterpret_cast<intptr_t>(arg));
    fact("--- %s scan requested from the menu at t=%.1f ---", topic == tzscan::Topic::Shot ? "BALL AND HOOPS" : "movement", secondsSinceStart());
    bool ok = false; int matches = 0;
    uintptr_t base = 0;
    void* lib = openIl2cpp(&base);
    if (!lib) {
        fact("scan: libil2cpp.so is not loaded in the game (so there is nothing to read)");
    } else {
        ScanJob* job = new ScanJob;
        std::string missing;
        if (!tzscan::loadApi(lib, &job->api, &missing)) {
            fact("scan: libil2cpp.so does not export these needed functions: %s", missing.c_str());
            delete job;
        } else {
            job->options.libBase = base;
            job->options.brief = true;          // stage D5c: short report about the headset movement classes (keeps the facts file small enough to send)
            if (topic == tzscan::Topic::Shot) {  // stage D7: balls, hoops, rims, shots. The facts file that reaches me is cut at about 265 KB, so this report has its own size budget.
                job->options.topic = tzscan::Topic::Shot; job->options.brief = false;
                job->options.maxLines = 3000; job->options.reservedLines = 500; job->options.maxBytes = 190000; job->options.reservedBytes = 45000;
                job->options.shotIndex = false; job->options.listAssemblies = false;      // stage D7c: the earlier report already has the index and the assembly list
                job->options.liveDelaySeconds = 25;                                       // time to close the menu, pick up a ball, take a shot and hold a ball again
#ifdef TZ_FAST_TEST
                job->options.liveDelaySeconds = std::getenv("TZ_SHOT_LIVE_DELAY_S") ? std::atoi(std::getenv("TZ_SHOT_LIVE_DELAY_S")) : 0;     // PC test only
#endif
            }
            double deadline = job->options.maxSeconds + job->options.stallSeconds + 120.0;      // longer than the scan can honestly take
#ifdef TZ_FAST_TEST
            if (topic == tzscan::Topic::Movement && !std::getenv("TZ_SCAN_BRIEF")) job->options.brief = false;    // PC test only: the full report unless asked otherwise
            if (const char* e = std::getenv("TZ_SCAN_DEADLINE_S")) deadline = std::atof(e);      // PC test only: pretend the runtime hangs
            if (const char* e = std::getenv("TZ_SCAN_TEST_HANG_CHUNKS")) job->options.testHangAfterChunks = std::atoi(e);
            if (const char* e = std::getenv("TZ_SCAN_TEST_PIPE_BYTES")) job->options.testPipeBytes = static_cast<size_t>(std::atol(e));
            if (const char* e = std::getenv("TZ_SCAN_TEST_STALL_S")) job->options.stallSeconds = std::atoi(e);
#endif
            pthread_t t;
            if (pthread_create(&t, nullptr, scanWorker, job) != 0) {
                fact("scan: could not start the scan worker thread");
                delete job;
            } else {
                pthread_detach(t);
                const double started = secondsSinceStart();
                while (!job->done.load() && secondsSinceStart() - started < deadline) usleep(200000);
                if (job->done.load()) {
                    ok = job->summary.ok; matches = job->summary.matchedClasses + job->summary.fieldHits + job->summary.typeHits + job->summary.liveObjects;
                    if (!job->summary.ok) fact("scan: FAILED: %s", job->summary.error.c_str());
                    delete job;
                } else {
                    // the worker is still busy: leave it (and its memory) alone, report where it is
                    fact("scan: GAVE UP waiting after %.0f s: the scan is still in step \"%s\". Everything written above this line is valid.", secondsSinceStart() - started, tzscan::currentStep());
                }
            }
        }
    }
    tzoverlay::setScanResult(ok, matches);
    fact("--- scan finished: %s ---", ok ? "ok" : "FAILED");
    gScanRunning.store(false);
    return nullptr;
}

void startScan(tzscan::Topic topic = tzscan::Topic::Movement) {
    bool expected = false;
    if (!gScanRunning.compare_exchange_strong(expected, true)) return;     // one scan at a time
    pthread_t t;
    if (pthread_create(&t, nullptr, scanMain, reinterpret_cast<void*>(static_cast<intptr_t>(topic))) == 0) pthread_detach(t);
    else { gScanRunning.store(false); tzoverlay::setScanResult(false, 0); fact("scan: could not start the scan thread"); }
}

// ---- stage D4 / D6: movement ----------------------------------------------------------------
// The rules (tested on a PC) live in movement.cpp. Since stage D6 the controller talks to the game through game_link.cpp
// (it finds the headset's PlayerLocomotion object when a switch is turned on and writes "original x factor" into it).
// The requests and what the link does are written to the facts file.
tzmove::Controller& gMove = *new tzmove::Controller;      // never destroyed on purpose (see gAim)
tzgame::PlayerLink& gLink = *new tzgame::PlayerLink;

#ifdef TZ_FAST_TEST
// PC test only: pretend the menu switches are set like this. TZ_TEST_ASK="speed,jump,gravityMode,pct" (speed / jump of 1 = switch off),
// from TZ_TEST_ASK_ON_S to TZ_TEST_ASK_OFF_S seconds after the start.
void testOverride(tzoverlay::MovementAsk* a, double now) {
    const char* e = std::getenv("TZ_TEST_ASK");
    if (!e) return;
    float sp = 1, ju = 1, pct = 0; int mode = 0;
    std::sscanf(e, "%f,%f,%d,%f", &sp, &ju, &mode, &pct);
    double onAt = 0, offAt = 1e18;
    if (const char* o = std::getenv("TZ_TEST_ASK_ON_S")) onAt = std::atof(o);
    if (const char* o = std::getenv("TZ_TEST_ASK_OFF_S")) offAt = std::atof(o);
    if (now < onAt || now >= offAt) return;
    a->speedOn = sp >= 1.05f; a->speed = sp; a->jumpOn = ju >= 1.05f; a->jump = ju;
    a->gravityMode = mode; a->lowPct = mode == 1 ? pct : 0.0f; a->highPct = mode == 2 ? pct : 0.0f;
}
#endif

void* probeMain(void*) {
    secondsSinceStart();                               // start the clock
    std::string where;
    gOut = openFactsFile(where);
    if (!gOut) { logf_("facts: could not open a facts file anywhere"); return nullptr; }
    logf_("facts file: %s", where.c_str());

    fact("Timmyzstuff facts (stage D8: clickable menu + Movement page (REAL game link for Speed Boost / Jump Boost / Low and High Gravity) + Basketball page with the Aimbot (switch, distance slider, FIRST VERSION of the part that really aims the ball - not yet proven in the real game) + ball-and-hoops scan)");
    fact("package: %s", packageName().c_str());
    fact("this file: %s", where.c_str());
    {   // saved menu settings (sound, colour, size) live next to this file
        const size_t slash = where.rfind('/');
        const std::string settingsPath = (slash == std::string::npos ? std::string(".") : where.substr(0, slash)) + "/timmyzstuff_settings.txt";
        tzoverlay::setSettingsPath(settingsPath.c_str(), fact);
    }
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
    {   tzgame::LinkConfig lc; lc.provider = il2cppForLink; lc.note = fact;
#ifdef TZ_FAST_TEST
        lc.retrySoonSeconds = 0.3; lc.minSearchGapSeconds = 0.3; lc.refreshSeconds = 30; lc.searchSeconds = 20; lc.stallSeconds = 5;     // PC test only: quicker
#endif
        gMove.setAdapter(&gLink);
        gLink.start(lc);                                 // does nothing in the game until a movement switch is turned on
    }
    {   tzaimlink::Config ac; ac.provider = il2cppForLink; ac.note = fact;     // stage D8: the Aimbot's game part (does nothing until the Aimbot switch is on)
#ifdef TZ_FAST_TEST
        ac.retrySoonSeconds = 0.3; ac.minSearchGapSeconds = 0.3; ac.searchSeconds = 20; ac.stallSeconds = 5;     // PC test only: quicker
#endif
        gAim.start(ac);
    }
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
    int overlayTries = 0;
    double overlayNextTry = 0;
    int inputTries = 0;
    double inputNextTry = 0;
    uint64_t lastCounts[3] = {0, 0, 0};
    double lastBeatTime = last;
    double sampleMillis = kSampleMillis;
#ifdef TZ_FAST_TEST
    if (const char* e = std::getenv("TZ_SAMPLE_MS")) sampleMillis = std::atof(e);          // PC test only: keep the facts file open longer
#endif
    const double endAt = sampleMillis > 0 ? last + sampleMillis / 1000.0 : 1e18;
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
        if (visible && !wasVisible) {
            ++opens; fact("t=%.2f MENU OPEN  (#%d) - both triggers + A", now, opens);
            float head[7] = {0, 0, 0, 1, 0, 1.6f, 0};            // used only if the pose call fails
            bool gotHead = false;
            if (poseFn) {
                alignas(16) unsigned char pb[256]; std::memset(pb, 0, sizeof pb);
                const int prc = poseFn(-1, -1, 0, pb);               // node 0 = head (confirmed in stage C)
                if (prc >= 0) { std::memcpy(head, pb, 16); std::memcpy(head + 4, pb + 16, 12); gotHead = true; }
            }
            if (opens <= 6) fact("overlay: head %s q=(%.2f %.2f %.2f %.2f) p=(%.2f %.2f %.2f) -> panel %s", gotHead ? "pose" : "pose UNAVAILABLE, using a default",
                                 head[0], head[1], head[2], head[3], head[4], head[5], head[6], tzoverlay::ready() ? "shown" : "NOT READY, nothing to show");
            tzoverlay::show(head);
        }
        if (!visible && wasVisible) { ++closes; fact("t=%.2f MENU CLOSE (#%d) - B button or the X button on the menu", now, closes); tzoverlay::hide(); }
        wasVisible = visible;
        if (aPressed) poseSnapshot("A pressed");
        else if (bPressed) poseSnapshot("B pressed");

        // ---- stage D3: point and click while the menu is open
        if (visible && poseFn && tzoverlay::ready()) {
            tzoverlay::PointerSample ps; std::memset(&ps, 0, sizeof ps);
            for (int hnd = 0; hnd < 2; ++hnd) {                 // node 3 = left hand, 4 = right hand (confirmed in stage C)
                alignas(16) unsigned char pb[256]; std::memset(pb, 0, sizeof pb);
                const int prc = poseFn(-1, -1, 3 + hnd, pb);
                std::memcpy(ps.pose[hnd], pb, 16); std::memcpy(ps.pose[hnd] + 4, pb + 16, 12);
                const float* q = ps.pose[hnd];
                const float norm = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
                ps.valid[hnd] = prc >= 0 && std::fabs(norm - 1.0f) < 0.1f && std::isfinite(q[4]) && std::isfinite(q[5]) && std::isfinite(q[6]);
            }
            ps.trigger[0] = pad.leftTrigger; ps.trigger[1] = pad.rightTrigger;
            const tzoverlay::PointerResult pr = tzoverlay::pointer(now, ps, fact);
            if (pr.close) menu.close();                        // the X button on the menu
        }

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
            if (watchesDone) { fact("t=%.1f all frame watches are installed", now); overlayNextTry = now + kOverlayDelay; inputNextTry = now + 1.0; }
        }
        if (!diag2 && now >= watchGiveUp) {
            diag2 = true;
            for (int i = 0; i < frameWatchCount(); ++i)
                if (!gWatchPatched[i]) fact("frame watch %s: NOT installed after %d seconds of searching", frameWatch(i)->name, kWatchTrySeconds);
        }
        // ---- stage D3: input block (hide clicks from the game while the menu is open)
        if (watchesDone && !gInputPatched && inputTries < 3 && now >= inputNextTry) {
            ++inputTries;
            installInputBlock(ovr, inputTries);
            inputNextTry = now + 6.0;
            if (!gInputPatched && inputTries == 3) fact("input block: NOT installed - the game reads its controllers another way, so clicks on the menu also reach the game");
        }
        // ---- stage D2: create the menu panel once the frame watcher is running
        if (watchesDone && overlayTries < 2 && now >= overlayNextTry) {
            if (overlayTries == 0) dumpGameLayers();
            ++overlayTries;
            fact("--- overlay: creating the menu panel (try %d) at t=%.1f ---", overlayTries, now);
            const bool okOverlay = tzoverlay::init(ovr, fact);
            fact("overlay: init %s", okOverlay ? "OK" : "FAILED");
            overlayNextTry = now + 15.0;
        }
        // ---- stage D4: scan button + movement rules
        if (tzoverlay::takeScanRequest()) startScan();
        if (tzoverlay::takeShotScanRequest()) startScan(tzscan::Topic::Shot);      // stage D7: the Basketball page's "Scan ball and hoops"
#ifdef TZ_FAST_TEST
        { static bool testScanDone = false; if (!testScanDone && now >= 2.0) { testScanDone = true; startScan(std::getenv("TZ_TEST_SHOT_SCAN") ? tzscan::Topic::Shot : tzscan::Topic::Movement); } }     // PC test only: press "Scan" by itself
#endif
        {
            tzoverlay::MovementAsk ask = tzoverlay::movementAsk();
#ifdef TZ_FAST_TEST
            testOverride(&ask, now);
#endif
            tzmove::Request req;
            req.speedOn = ask.speedOn; req.speed = ask.speed; req.jumpOn = ask.jumpOn; req.jump = ask.jump;
            req.gravityMode = ask.gravityMode; req.lowPct = ask.lowPct; req.highPct = ask.highPct; req.flyActive = false;   // there is no Fly yet
            gLink.setWanted(req.speedOn || req.jumpOn || req.gravityMode != 0);       // only now does the link look for the player object
            gMove.update(req);
            const int linkUi = gLink.uiState();
            tzoverlay::setLinkState(linkUi);
            static int moveLogs = 0; static double lastMoveLog = -10; static tzoverlay::MovementAsk lastAsk = {false, false, 0, 0, 0, 0, 0};
            const bool changed = ask.speedOn != lastAsk.speedOn || ask.jumpOn != lastAsk.jumpOn || ask.gravityMode != lastAsk.gravityMode ||
                                 (now - lastMoveLog > 1.0 && (ask.speed != lastAsk.speed || ask.jump != lastAsk.jump || ask.lowPct != lastAsk.lowPct || ask.highPct != lastAsk.highPct) && (ask.speedOn || ask.jumpOn || ask.gravityMode));
            if (changed && moveLogs < 40) {
                ++moveLogs; lastMoveLog = now; lastAsk = ask;
                const tzmove::Effective e = tzmove::resolve(req);
                const char* what = gMove.status() == tzmove::Controller::ACTIVE ? "applied to the game" :
                                   gMove.status() == tzmove::Controller::ERROR_ ? "the game's values could not be read: nothing changed" :
                                   gMove.status() == tzmove::Controller::IDLE ? "nothing to change" : "game link not connected (yet): nothing changed in the game so far";
                fact("movement: menu asks speed=%s %.1fx | jump=%s %.1fx | gravity=%s %.0f%%  ->  game gets speed x%.2f, jump height x%.2f, gravity x%.2f  [%s]",
                     ask.speedOn ? "ON" : "off", ask.speed, ask.jumpOn ? "ON" : "off", ask.jump,
                     ask.gravityMode == 1 ? "LOW" : (ask.gravityMode == 2 ? "HIGH" : "off"), ask.gravityMode == 1 ? ask.lowPct : (ask.gravityMode == 2 ? ask.highPct : 0.0f),
                     e.speedMul, e.jumpHeightMul, e.gravityMul, what);
            }
            // the link's own numbers: whenever its state changes and then every 15 seconds while a switch is on (never more than 100 lines)
            static int lastLinkUi = 0, linkLogs = 0; static double nextLinkLog = 0;
            if ((linkUi != lastLinkUi || (linkUi != 0 && now >= nextLinkLog)) && linkLogs < 100) {
                ++linkLogs; lastLinkUi = linkUi; nextLinkLog = now + 15.0;
                fact("t=%.0f %s", now, gLink.summary().c_str());
            }
        }
        {   // ---- stage D8: the Aimbot switch and its distance go to the game part; its state and last shot go back to the menu.
            tzoverlay::AimAsk aim = tzoverlay::aimAsk();
#ifdef TZ_FAST_TEST
            if (const char* e = std::getenv("TZ_TEST_AIM")) { float cap = 50; std::sscanf(e, "%f", &cap); aim.on = now >= 3.0; aim.capM = cap; }     // PC test only: "TZ_TEST_AIM=23" = switch on at 23 m from 3 s on
#endif
            gAim.setAsk(aim.on, aim.capM);
            const int aimUi = aim.on ? gAim.uiState() : 0;
            tzoverlay::setAimInfo(aimUi, aim.on ? gAim.headline().c_str() : "", gAim.lastShotText().c_str());
            static bool lastOn = false; static float lastCap = -1; static int aimLogs = 0; static double lastAimLog = -10;
            if ((aim.on != lastOn || (aim.on && aim.capM != lastCap && now - lastAimLog > 1.0)) && aimLogs < 30) {
                ++aimLogs; lastOn = aim.on; lastCap = aim.capM; lastAimLog = now;
                fact("aimbot: menu asks aimbot=%s, max shot distance=%s  [stage D8: when ON, a throw that the rules accept gets a new launch speed; the link reports every throw below]",
                     aim.on ? "ON" : "off", tzaim::capLabel(aim.capM).c_str());
            }
            // the game part's own numbers: whenever its state changes and then every 20 seconds while the switch is on (never more than 60 lines)
            // ... and also whenever a throw was judged as a shot or its flight ended (the numbers change then)
            static int lastAimUi = 0, aimSumLogs = 0; static double nextAimSum = 0; static unsigned long long lastShotKey = 0;
            const tzaimlink::Counters ac = gAim.counters();
            const unsigned long long shotKey = ac.shotLike + ac.scored + ac.missed;
            if ((aimUi != lastAimUi || shotKey != lastShotKey || (aimUi != 0 && now >= nextAimSum)) && aimSumLogs < 60) {
                ++aimSumLogs; lastAimUi = aimUi; lastShotKey = shotKey; nextAimSum = now + 20.0;
                fact("t=%.0f %s", now, gAim.summary().c_str());
            }
        }
        tzoverlay::tick(now, fact);
        if (now >= nextBeat) {
            fact("t=%.0f still sampling (%ld samples, menu opened %d times, closed %d times)", now, samples, opens, closes);
            if (gWatchPatched[0] || gWatchPatched[1] || gWatchPatched[2]) {
                writeFrameLine("frames", now, now - lastBeatTime, lastCounts);
                { const tzoverlay::Stats st = tzoverlay::stats();
                  fact("overlay: ready=%d menuVisible=%d frames-with-panel=%llu lastRc=%d firstBadRc=%d broken=%d",
                       tzoverlay::ready() ? 1 : 0, tzoverlay::visible() ? 1 : 0, (unsigned long long)st.withOverlay, st.lastRc, st.firstBadRc, st.broken ? 1 : 0); }
                { const tzoverlay::InputStats is = tzoverlay::inputStats();
                  fact("input: block %s; game asked for controller state %llu times, %llu of them blanked", gInputPatched ? "installed" : "not installed", (unsigned long long)is.calls, (unsigned long long)is.masked); }
                { static int threadLines = 0; static std::string lastIn, lastFr;
                  const std::string in = describeTally(gInputThreads), fr = describeTally(gFrameThreads);
                  // the call counts keep growing, so only write the line again when a NEW thread shows up (and never more than 6 times)
                  auto names = [](const std::string& x) { std::string r; for (size_t i = 0; i < x.size(); ++i) { if (x[i] == '(') { while (i < x.size() && x[i] != ')') ++i; } else r += x[i]; } return r; };
                  if ((names(in) != lastIn || names(fr) != lastFr) && threadLines < 6) {
                      ++threadLines; lastIn = names(in); lastFr = names(fr);
                      fact("threads: controller-state doorway is called by: %s | EndFrame4 doorway is called by: %s", in.c_str(), fr.c_str());
                  } }
                for (int i = 0; i < frameWatchCount(); ++i) lastCounts[i] = *frameWatch(i)->count;
                lastBeatTime = now;
            }
            nextBeat += (now > 600.0 ? 60.0 : kBeatSeconds);       // after the first 10 minutes write progress lines only once a minute
        }
        usleep(20 * 1000);                              // 50 samples per second
    }
    fact("summary: menu opened %d times, closed %d times", opens, closes);
    fact("threads (final): controller-state doorway: %s | EndFrame4 doorway: %s", describeTally(gInputThreads).c_str(), describeTally(gFrameThreads).c_str());
    fact("%s", gLink.summary().c_str());
    fact("%s", gAim.summary().c_str());
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
    logf_("payload loaded (stage D8: pass-through + input probe + frame watcher + clickable menu panel + movement page + game link + aimbot with game link)");

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

    tzoverlay::setJavaVM(vm);
    const jint result = realOnLoad(vm, reserved);
    logf_("original JNI_OnLoad returned 0x%x", static_cast<unsigned>(result));
    if (result > 0) startProbe();                          // only after the game started fine
    return result;
}
