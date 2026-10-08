// proxy.cpp - STAGE A payload: a "pass-through" library.
//
// WHAT IT DOES (in plain words)
//   The patcher renames the game's real library to libmain_orig.so and puts THIS file in
//   its place, named libmain.so. When the game starts it loads this file first. All we do is:
//     1. work out where we are (e.g. .../lib/arm64/libmain.so),
//     2. load the real library sitting next to us (.../libmain_orig.so),
//     3. call the real library's JNI_OnLoad and return what it returns,
//   so the game starts exactly as it normally would. We also write a few lines to logcat
//   (tag "Timmyzstuff") so you can SEE that our code ran.
//
// WHAT IT DOES NOT DO: no menu, no hooks, no gameplay changes. It exists to prove the
// install -> load -> hand-over chain works before anything else is added.
//
// ASSUMPTION (from the reference mod, not proven for this game): the original libmain.so
// needs only JNI_OnLoad forwarded. Check with tools/list_exports (see NEXT_STEPS.md).

#include <dlfcn.h>
#include <jni.h>
#include <cstdarg>
#include <cstdio>
#include <string>

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

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    logf_("payload loaded (stage A: pass-through only)");

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
    return result;
}
