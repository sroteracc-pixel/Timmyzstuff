// proxy.cpp - STAGE B payload: the Stage A pass-through PLUS a read-only "facts probe".
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
const int kSampleMillis = 1500;
const int kSettleSeconds = 1;
#else
const int kWaitForLibSeconds = 600;   // wait up to 10 min for the game to load its VR library
const int kWaitForInitSeconds = 120;  // then up to 2 min for it to finish starting
const int kSampleMillis = 600 * 1000; // then watch the controllers for 10 minutes
const int kSettleSeconds = 10;        // pause so the graphics drivers finish loading
#endif
const int kMaxChangeLines = 600;      // never write more than this many "something changed" lines
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
    for (size_t i = 0; i < n && w + 3 < outSize; ++i) w += std::snprintf(out + w, outSize - w, "%02x", p[i]);
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

void* probeMain(void*) {
    secondsSinceStart();                               // start the clock
    std::string where;
    gOut = openFactsFile(where);
    if (!gOut) { logf_("facts: could not open a facts file anywhere"); return nullptr; }
    logf_("facts file: %s", where.c_str());

    fact("Timmyzstuff facts (stage B probe) - read-only");
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

    fact("--- controller samples: press both triggers, A, B, X, Y and move the sticks ---");
    fact("format: t=<seconds> rc=<result code> bytes=<first %d bytes in hex>", static_cast<int>(kDumpBytes));
    alignas(16) unsigned char buf[kBufSize];
    alignas(16) unsigned char prev[kBufSize];
    std::memset(prev, 0, sizeof prev);
    int lines = 0, lastRc = -12345;
    long samples = 0;
    double nextBeat = secondsSinceStart() + 10.0;
    const double endAt = secondsSinceStart() + kSampleMillis / 1000.0;
    while (secondsSinceStart() < endAt) {
        std::memset(buf, 0, sizeof buf);
        const int rc = ctrlFn(0x3u, buf);               // 0x3 = left + right Touch controller
        ++samples;
        if (lines < kMaxChangeLines && (samples == 1 || rc != lastRc || worthWriting(prev, buf))) {
            char hx[2 * kDumpBytes + 1];
            hex(buf, kDumpBytes, hx, sizeof hx);
            fact("t=%.1f rc=%d bytes=%s", secondsSinceStart(), rc, hx);
            ++lines;
            std::memcpy(prev, buf, sizeof prev);
            lastRc = rc;
            if (lines == kMaxChangeLines) fact("(line limit reached - no more change lines will be written)");
        }
        if (secondsSinceStart() >= nextBeat) {
            fact("t=%.0f still sampling (%ld samples, %d change lines)", secondsSinceStart(), samples, lines);
            nextBeat += 30.0;
        }
        usleep(50 * 1000);                              // 20 samples per second
    }
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
    logf_("payload loaded (stage B: pass-through + read-only facts probe)");

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
