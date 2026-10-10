// overlay.cpp - see overlay.h.
#include "overlay.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <ctime>
#include <vector>

namespace tzoverlay {

namespace {

// ---- layout facts read from machine code of YOUR libOVRPlugin.so (1.65) -----------------------
// ovrp_CalculateLayerDesc(shape, layout, &size, mips, samples, format, flags, &desc)
// ovrp_SetupLayer(device, &desc, &layerId)
// The desc's flags word is at byte 28; bit 7 (value 128) makes the layer Android-surface backed.
// The per-frame "submit" record: 0 layerId | 4 textureStage | 8 + 24 two viewport rects (x,y,w,h ints)
// | 40 pose (qx,qy,qz,qw,px,py,pz floats) | 68 flags | 72 colour scale | 88 colour offset |
// 104 override-rect flag | 108..172 rect matrix | quad size (w,h floats) at 188 (SDK minor >= 60).
const int kShapeQuad = 0;
const int kLayoutMono = 1;
const int kFlagAndroidSurface = 1 << 7;
const unsigned long kSetupLayerOffset = 0x1419d4;     // where ovrp_SetupLayer sits in this exact build
const unsigned long kGlobalObjectSlot = 0x417fb0;     // holds a pointer to the plugin's main object

typedef int (*CalcDescFn)(int, int, const int*, int, int, int, int, void*);
typedef int (*SetupLayerFn)(void*, const void*, int*);
typedef int (*GetSurfaceFn)(int, void**);
typedef int (*EndFrame4Fn)(int, const void* const*, int, void*);

// NDK window functions, looked up by name at run time (so this compiles without the Android headers)
struct ARectLike { int32_t left, top, right, bottom; };
struct WindowBuffer { int32_t width, height, stride, format; void* bits; uint32_t reserved[6]; };
typedef void* (*FromSurfaceFn)(void* env, void* surface);
typedef int32_t (*LockFn)(void* win, WindowBuffer* out, ARectLike* dirty);
typedef int32_t (*UnlockFn)(void* win);
typedef int32_t (*SetGeomFn)(void* win, int32_t w, int32_t h, int32_t fmt);

JavaVM* gVm = nullptr;
std::mutex gMutex;                      // guards everything below except the atomics
bool gReady = false;
int gLayerId = -1;
int gMinor = 0;
void* gWindow = nullptr;
LockFn gLock = nullptr;
UnlockFn gUnlock = nullptr;
bool gDirty = true;
double gLastPost = -100.0;
int gPostCount = 0;
tzpanel::PanelState gState;
tzpanel::Canvas* gBase = nullptr;          // the menu picture without the pointer dot
tzpanel::Canvas* gComposite = nullptr;     // the picture with the dot on top
std::vector<tzpanel::HitRect> gHits;       // clickable areas of gBase
bool gBaseValid = false;
tzpanel::Interaction gInteraction;
tzpanel::Placement gPlace = {{0, 0, -1}, 0, 0.95f, 0.7125f};
float gAppliedScale = 1.0f;                // the size / distance the real panel has (the sliders apply when you let go)
float gAppliedDistance = tzpanel::kDistDefault;
bool gCursorOn = false, gCursorPressed = false;
float gCurX = 0, gCurY = 0;
std::string gSettingsPath;
int gPointerLogLines = 0;
std::atomic<int64_t> gMaskUntilMs{0};
std::atomic<bool> gScanRequested{false}, gShotScanRequested{false};
std::atomic<uint64_t> gInputCalls{0}, gInputMasked{0};

alignas(16) unsigned char gSubmitA[0x200];
alignas(16) unsigned char gSubmitB[0x200];
std::atomic<const unsigned char*> gSubmit{nullptr};   // the record handed to Meta (double buffered)
std::atomic<bool> gShow{false};
std::atomic<bool> gBroken{false};
std::atomic<uint64_t> gReal{0};
std::atomic<uint64_t> gCalls{0}, gWith{0}, gRetries{0};
std::atomic<int> gLastRc{0}, gFirstBadRc{0};
float gHead[7] = {0, 0, 0, 1, 0, 0, 0};
bool gHaveHead = false;

// copy memory without ever crashing (the kernel says "no" for a bad address)
bool safeCopy(uintptr_t from, void* to, size_t n) {
    static int pfd[2] = {-1, -1};
    if (pfd[0] < 0 && pipe(pfd) != 0) { pfd[0] = pfd[1] = -1; return false; }
    if (n > 4096) n = 4096;
    const ssize_t w = write(pfd[1], reinterpret_cast<const void*>(from), n);
    if (w != static_cast<ssize_t>(n)) { unsigned char d[4096]; if (w > 0) { ssize_t left = w; while (left > 0) { ssize_t r = read(pfd[0], d, left); if (r <= 0) break; left -= r; } } return false; }
    size_t got = 0;
    while (got < n) { const ssize_t r = read(pfd[0], static_cast<unsigned char*>(to) + got, n - got); if (r <= 0) return false; got += r; }
    return true;
}

int64_t nowMs() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

uintptr_t libBase(const char* name) {
    FILE* m = std::fopen("/proc/self/maps", "r");
    if (!m) return 0;
    char line[1024]; uintptr_t lowest = 0;
    while (std::fgets(line, sizeof line, m)) {
        if (!std::strstr(line, name)) continue;
        unsigned long a = 0;
        if (std::sscanf(line, "%lx-", &a) == 1 && (lowest == 0 || a < lowest)) lowest = a;
    }
    std::fclose(m);
    return lowest;
}

std::string hexOf(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef"; std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

void putI(unsigned char* b, int off, int v) { std::memcpy(b + off, &v, 4); }
void putF(unsigned char* b, int off, float v) { std::memcpy(b + off, &v, 4); }

// Builds the per-frame record for our panel and publishes it (double buffered).
void buildSubmit() {
    static bool useB = false;
    unsigned char* b = useB ? gSubmitB : gSubmitA;
    useB = !useB;
    std::memset(b, 0, 0x200);
    putI(b, 0, gLayerId);
    putI(b, 4, 0);
    for (int k = 0; k < 2; ++k) { putI(b, 8 + 16 * k, 0); putI(b, 12 + 16 * k, 0); putI(b, 16 + 16 * k, tzpanel::kWidth); putI(b, 20 + 16 * k, tzpanel::kHeight); }

    // where the panel sits: 1.15 m in front of the head, level, turned to face it (shared with the pointer code)
    gPlace = tzpanel::placeInFront(gHead, gAppliedScale, gAppliedDistance);
    putF(b, 40, 0.0f); putF(b, 44, std::sin(gPlace.yaw / 2)); putF(b, 48, 0.0f); putF(b, 52, std::cos(gPlace.yaw / 2));
    putF(b, 56, gPlace.pos[0]); putF(b, 60, gPlace.pos[1]); putF(b, 64, gPlace.pos[2]);
    const float w = gPlace.width, h = gPlace.height;
    int sizeOff = 188;                                  // SDK minor >= 60
    if (gMinor < 60) sizeOff = (gMinor >= 31) ? 176 : 72;
    if (gMinor >= 31) { putF(b, 72, 1); putF(b, 76, 1); putF(b, 80, 1); putF(b, 84, 1); }   // colour scale 1,1,1,1
    putF(b, sizeOff, w); putF(b, sizeOff + 4, h);
    gSubmit.store(b, std::memory_order_release);
}

void ensureBase() {
    if (!gBase) gBase = new tzpanel::Canvas(tzpanel::kWidth, tzpanel::kHeight);
    if (gDirty || !gBaseValid) { tzpanel::drawPanel(*gBase, gState, &gHits); gBaseValid = true; gDirty = false; }
}

bool postPicture(LogFn log) {
    ensureBase();
    if (!gWindow || !gLock || !gUnlock) return false;
    const tzpanel::Canvas* src = gBase;
    const bool fade = gState.transparency > 0.001f;
    if (gCursorOn || fade) {
        if (!gComposite) gComposite = new tzpanel::Canvas(tzpanel::kWidth, tzpanel::kHeight);
        *gComposite = *gBase;
        if (fade) gComposite->fadeAll(1.0f - gState.transparency);     // see-through menu; the dot below stays solid
        if (gCursorOn) tzpanel::drawCursor(*gComposite, gCurX, gCurY, gState, gCursorPressed);
        src = gComposite;
    }
    WindowBuffer buf; std::memset(&buf, 0, sizeof buf);
    const int32_t rc = gLock(gWindow, &buf, nullptr);
    if (rc != 0 || !buf.bits) { if (gPostCount < 3 && log) log("overlay: lock failed rc=%d", rc); ++gPostCount; return false; }
    if (gPostCount < 3 && log) log("overlay: locked surface %dx%d stride=%d format=%d", buf.width, buf.height, buf.stride, buf.format);
    const int cw = tzpanel::kWidth, ch = tzpanel::kHeight;
    const int copyW = buf.width < cw ? buf.width : cw, copyH = buf.height < ch ? buf.height : ch;
    for (int y = 0; y < copyH; ++y)
        std::memcpy(static_cast<unsigned char*>(buf.bits) + static_cast<size_t>(y) * buf.stride * 4, src->data() + static_cast<size_t>(y) * cw * 4, static_cast<size_t>(copyW) * 4);
    gUnlock(gWindow);
    ++gPostCount;
    return true;
}

void saveSettings(LogFn log) {
    if (gSettingsPath.empty()) return;
    FILE* f = std::fopen(gSettingsPath.c_str(), "w");
    if (!f) { if (log) log("settings: could not write %s", gSettingsPath.c_str()); return; }
    const std::string t = tzpanel::settingsToText(gState);
    std::fwrite(t.data(), 1, t.size(), f);
    std::fclose(f);
}

}  // namespace

void setJavaVM(void* vm) { gVm = static_cast<JavaVM*>(vm); }
tzpanel::PanelState& state() { return gState; }
void markDirty() { std::lock_guard<std::mutex> g(gMutex); gDirty = true; }
bool ready() { return gReady; }
bool visible() { return gShow.load(); }
void setRealEndFrame4(uint64_t f) { gReal.store(f); }
Stats stats() { return Stats{gCalls.load(), gWith.load(), gRetries.load(), gLastRc.load(), gFirstBadRc.load(), gBroken.load()}; }

bool init(void* ovr, LogFn log) {
    std::lock_guard<std::mutex> g(gMutex);
    if (gReady) return true;
    auto say = [&](const char* msg) { if (log) log("overlay: %s", msg); };

    CalcDescFn calc = reinterpret_cast<CalcDescFn>(dlsym(ovr, "ovrp_CalculateLayerDesc"));
    SetupLayerFn setup = reinterpret_cast<SetupLayerFn>(dlsym(ovr, "ovrp_SetupLayer"));
    GetSurfaceFn getSurf = reinterpret_cast<GetSurfaceFn>(dlsym(ovr, "ovrp_GetLayerAndroidSurfaceObject"));
    if (!calc || !setup || !getSurf) { say("a Meta function is missing - cannot create the panel"); return false; }

    // 1. read the plugin's own version and the device it remembers (read-only, crash-proof)
    void* device = nullptr;
    const uintptr_t base = libBase("libOVRPlugin.so");
    if (base && reinterpret_cast<uintptr_t>(setup) - base == kSetupLayerOffset) {
        uint64_t objPtr = 0;
        if (safeCopy(base + kGlobalObjectSlot, &objPtr, 8) && objPtr) {
            int ver[2] = {0, 0}; uint64_t dev = 0;
            safeCopy(objPtr + 8, ver, 8);
            safeCopy(objPtr + 0x1210, &dev, 8);
            gMinor = ver[1];
            device = reinterpret_cast<void*>(dev);
            if (log) log("overlay: plugin reports version %d.%d, remembered device pointer %s", ver[0], ver[1], dev ? "present" : "empty");
        } else say("could not read the plugin's main object");
    } else say("libOVRPlugin.so is not the build I analysed (offset check failed) - assuming minor version 65");
    if (gMinor == 0) gMinor = 65;

    // 2. let Meta's own function fill in the layer description (so I never guess its layout)
    alignas(16) unsigned char desc[512]; std::memset(desc, 0, sizeof desc);
    const int size[2] = {tzpanel::kWidth, tzpanel::kHeight};
    int rc = calc(kShapeQuad, kLayoutMono, size, 1, 1, 0, kFlagAndroidSurface, desc);
    if (log) log("overlay: ovrp_CalculateLayerDesc rc=%d desc[0..48]=%s", rc, hexOf(desc, 48).c_str());
    if (rc < 0) return false;

    // 3. create the layer
    int layerId = -1;
    void* devices[2] = {device, nullptr};
    bool made = false;
    for (int i = 0; i < 2 && !made; ++i) {
        if (i == 1 && devices[0] == nullptr) break;           // nothing different to try
        layerId = -1;
        if (log) log("overlay: calling ovrp_SetupLayer (device %s)", devices[i] ? "from plugin" : "null");
        rc = setup(devices[i], desc, &layerId);
        if (log) log("overlay: ovrp_SetupLayer rc=%d layerId=%d", rc, layerId);
        made = (rc >= 0 && layerId >= 0);
    }
    if (!made) return false;

    // 4. get the Android surface behind it
    void* surface = nullptr;
    rc = getSurf(layerId, &surface);
    if (log) log("overlay: ovrp_GetLayerAndroidSurfaceObject rc=%d surface=%s", rc, surface ? "present" : "null");
    if (rc < 0 || !surface) return false;

    // 5. turn it into a drawable window
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) { say("libandroid.so not found"); return false; }
    FromSurfaceFn fromSurface = reinterpret_cast<FromSurfaceFn>(dlsym(lib, "ANativeWindow_fromSurface"));
    SetGeomFn setGeom = reinterpret_cast<SetGeomFn>(dlsym(lib, "ANativeWindow_setBuffersGeometry"));
    gLock = reinterpret_cast<LockFn>(dlsym(lib, "ANativeWindow_lock"));
    gUnlock = reinterpret_cast<UnlockFn>(dlsym(lib, "ANativeWindow_unlockAndPost"));
    if (!fromSurface || !gLock || !gUnlock || !gVm) { say("window functions or Java VM missing"); return false; }
#ifdef __ANDROID__
    JNIEnv* env = nullptr;
    if (gVm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { say("could not attach to Java"); return false; }
    gWindow = fromSurface(env, surface);
#else
    say("not running on Android - cannot open the surface");
    (void)fromSurface;
    return false;
#endif
    if (log) log("overlay: ANativeWindow_fromSurface -> %s", gWindow ? "ok" : "null");
    if (!gWindow) return false;
    if (setGeom) { const int32_t g = setGeom(gWindow, tzpanel::kWidth, tzpanel::kHeight, 1); if (log) log("overlay: setBuffersGeometry(%dx%d RGBA8888) rc=%d", tzpanel::kWidth, tzpanel::kHeight, g); }

    gLayerId = layerId;
    gDirty = true;
    gCursorOn = false;
    const bool posted = postPicture(log);
    if (log) log("overlay: first picture posted: %s", posted ? "yes" : "NO");
    if (!posted) return false;
    gReady = true;
    say("READY - the panel exists. It will be shown when you open the menu.");
    return true;
}

void show(const float head[7]) {
    std::lock_guard<std::mutex> g(gMutex);
    std::memcpy(gHead, head, sizeof gHead);
    gHaveHead = true;
    if (!gReady) return;
    gState.hover = -1; gState.dragSlider = 0;
    gAppliedScale = gState.scale; gAppliedDistance = gState.distance;
    gInteraction.reset();
    gCursorOn = false; gCursorPressed = false;
    buildSubmit();
    gShow.store(true, std::memory_order_release);
    gDirty = true;
}

void hide() {
    gShow.store(false, std::memory_order_release);
    gMaskUntilMs.store(nowMs() + 400);        // keep hiding buttons from the game a moment: the B press that closed us is still down
}

void tick(double now, LogFn log) {
    std::lock_guard<std::mutex> g(gMutex);
    if (!gReady || !gShow.load()) return;
    if (gDirty || now - gLastPost > 0.5) {
        postPicture(gPostCount < 5 ? log : nullptr);
        gLastPost = now;
    }
}

PointerResult pointer(double now, const PointerSample& smp, LogFn log) {
    std::lock_guard<std::mutex> g(gMutex);
    PointerResult r;
    if (!gReady || !gShow.load()) return r;
    ensureBase();
    tzpanel::HandAim aims[2];
    for (int i = 0; i < 2; ++i) {
        aims[i].valid = smp.valid[i];
        aims[i].trigger = smp.trigger[i];
        if (smp.valid[i]) aims[i].onPlane = tzpanel::intersect(tzpanel::rayFromPose(smp.pose[i], tzpanel::kPointerPitchDeg), gPlace, &aims[i].x, &aims[i].y);
    }
    static double lastAimLog = -100; static int aimLogs = 0;
    if (log && aimLogs < 10 && now - lastAimLog > 2.0) {
        lastAimLog = now; ++aimLogs;
        log("pointer: aim left valid=%d onPlane=%d x=%.0f y=%.0f trig=%.2f | right valid=%d onPlane=%d x=%.0f y=%.0f trig=%.2f | panel centre=(%.2f %.2f %.2f) size=%.2fx%.2f m",
            aims[0].valid, aims[0].onPlane, aims[0].x, aims[0].y, aims[0].trigger, aims[1].valid, aims[1].onPlane, aims[1].x, aims[1].y, aims[1].trigger,
            gPlace.pos[0], gPlace.pos[1], gPlace.pos[2], gPlace.width, gPlace.height);
    }
    const bool wasCursor = gCursorOn;
    const tzpanel::Outcome o = gInteraction.update(gState, gHits, aims);
    if (o.scanRequested) { gScanRequested.store(true); if (log) log("pointer: Scan game code pressed"); }
    if (o.shotScanRequested) { gShotScanRequested.store(true); if (log) log("pointer: Scan ball and hoops pressed"); }
    if (o.layoutCommitted) { gAppliedScale = gState.scale; gAppliedDistance = gState.distance; buildSubmit(); }
    if (o.saveNeeded) saveSettings(log);
    if (o.redraw) gDirty = true;
    if (log && gPointerLogLines < 14) {
        if (o.cursorVisible && !wasCursor) { ++gPointerLogLines; log("pointer: dot appeared with the %s controller at picture x=%.0f y=%.0f", o.hand == 1 ? "RIGHT" : "LEFT", o.cursorX, o.cursorY); }
        if (o.clickedId) { ++gPointerLogLines; log("pointer: clicked control #%d at x=%.0f y=%.0f (tab=%d sound=%d colour=%s size=%.2f transparency=%.2f distance=%.2f)", o.clickedId, o.cursorX, o.cursorY, gState.tab, gState.sound ? 1 : 0, tzpanel::colorChoice(gState.colorIndex).name, gState.scale, gState.transparency, gState.distance); }
        if (o.close) { ++gPointerLogLines; log("pointer: X button pressed - closing the menu"); }
        if (o.layoutCommitted) { ++gPointerLogLines; log("pointer: slider let go - panel is now %.2fx at %.2f m", gAppliedScale, gAppliedDistance); }
    }
    const bool moved = (o.cursorVisible != gCursorOn) || (o.cursorVisible && (std::fabs(o.cursorX - gCurX) > 1.0f || std::fabs(o.cursorY - gCurY) > 1.0f)) || (o.cursorVisible && o.pressed != gCursorPressed);
    gCursorOn = o.cursorVisible; gCurX = o.cursorX; gCurY = o.cursorY; gCursorPressed = o.pressed;
    if (gDirty || moved) { postPicture(gPostCount < 5 ? log : nullptr); gLastPost = now; }
    r.close = o.close; r.clicked = o.clickedId != 0; r.clickedId = o.clickedId; r.hand = o.hand; r.onMenu = o.cursorVisible; r.x = o.cursorX; r.y = o.cursorY;
    return r;
}

void setSettingsPath(const char* path, LogFn log) {
    std::lock_guard<std::mutex> g(gMutex);
    gSettingsPath = path ? path : "";
    if (gSettingsPath.empty()) return;
    FILE* f = std::fopen(gSettingsPath.c_str(), "r");
    if (!f) { if (log) log("settings: no saved settings yet (%s)", gSettingsPath.c_str()); return; }
    std::string text; char buf[256]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 4096) text.append(buf, n);
    std::fclose(f);
    const bool ok = tzpanel::settingsFromText(text, gState);
    gAppliedScale = gState.scale; gAppliedDistance = gState.distance; gDirty = true;
    if (log) log("settings: loaded %s -> sound=%d colour=%s size=%.2f transparency=%.2f distance=%.2f", ok ? "ok" : "(nothing usable)", gState.sound ? 1 : 0, tzpanel::colorChoice(gState.colorIndex).name, gState.scale, gState.transparency, gState.distance);
}

bool takeScanRequest() { return gScanRequested.exchange(false); }
bool takeShotScanRequest() { return gShotScanRequested.exchange(false); }
void setScanResult(bool ok, int matches) {
    std::lock_guard<std::mutex> g(gMutex);
    gState.scanState = ok ? 2 : 3; gState.scanMatches = matches; gDirty = true;
}
void setLinkState(int state) {
    std::lock_guard<std::mutex> g(gMutex);
    if (gState.linkState != state) { gState.linkState = state; gDirty = true; }
}
MovementAsk movementAsk() {
    std::lock_guard<std::mutex> g(gMutex);
    return MovementAsk{gState.speedOn, gState.jumpOn, gState.gravityMode, gState.speedMul, gState.jumpMul, gState.lowGravPct, gState.highGravPct};
}

AimAsk aimAsk() {
    std::lock_guard<std::mutex> g(gMutex);
    return AimAsk{gState.aimOn, gState.aimCapM};
}

bool inputBlocked() { return gShow.load(std::memory_order_acquire) || nowMs() < gMaskUntilMs.load(std::memory_order_relaxed); }
int filterControllerState(int rc, void* out) {
    const bool block = out && rc >= 0 && inputBlocked();
    if (block) std::memset(static_cast<unsigned char*>(out) + 4, 0, 60);     // buttons, touches, triggers, grips, sticks, touchpads
    countInput(block);
    return rc;
}
void countInput(bool masked) { gInputCalls.fetch_add(1, std::memory_order_relaxed); if (masked) gInputMasked.fetch_add(1, std::memory_order_relaxed); }
InputStats inputStats() { return InputStats{gInputCalls.load(), gInputMasked.load()}; }

int hookEndFrame4(int frame, const void* const* layers, int count, void* extra) {
    EndFrame4Fn real = reinterpret_cast<EndFrame4Fn>(gReal.load(std::memory_order_relaxed));
    if (!real) return -1;
    gCalls.fetch_add(1, std::memory_order_relaxed);
    const unsigned char* sub = gSubmit.load(std::memory_order_acquire);
    if (gShow.load(std::memory_order_acquire) && sub && !gBroken.load(std::memory_order_relaxed) && layers && count >= 0 && count < 15) {
        const void* arr[16];
        for (int i = 0; i < count; ++i) arr[i] = layers[i];
        arr[count] = sub;
        const int rc = real(frame, arr, count + 1, extra);
        gWith.fetch_add(1, std::memory_order_relaxed);
        gLastRc.store(rc, std::memory_order_relaxed);
        if (rc >= 0) return rc;
        // Meta refused our layer: never lose the game's frame. Switch the panel off and send the game's own layers.
        if (gFirstBadRc.load() == 0) gFirstBadRc.store(rc);
        gBroken.store(true);
        gRetries.fetch_add(1, std::memory_order_relaxed);
        return real(frame, layers, count, extra);
    }
    return real(frame, layers, count, extra);
}

#ifdef TZ_PC_TEST
void testForceReady(int layerId, int minorVersion) { gLayerId = layerId; gMinor = minorVersion; gReady = true; }
const unsigned char* testSubmitBytes() { return gSubmit.load(); }
#endif

}  // namespace tzoverlay
