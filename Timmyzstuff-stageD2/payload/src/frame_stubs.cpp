// frame_stubs.cpp - see frame_stubs.h.
// On the Quest (arm64) each stub is a few lines of assembly that keep every register and the stack
// exactly as the caller left them, then jump to the real function (a "tail call").
// x16/x17 are the two registers ARM64 reserves for exactly this kind of glue code, so using them
// cannot disturb the caller. On a PC (used only by our tests) a plain C++ version does the same job.
#include "frame_stubs.h"

#define TZ_DATA(NAME)                                                                   \
    extern "C" __attribute__((visibility("hidden"))) uint64_t tz_##NAME##_args[8];      \
    extern "C" __attribute__((visibility("hidden"))) uint64_t tz_##NAME##_count;        \
    extern "C" __attribute__((visibility("hidden"))) uint64_t tz_##NAME##_orig;         \
    uint64_t tz_##NAME##_args[8];                                                       \
    uint64_t tz_##NAME##_count;                                                         \
    uint64_t tz_##NAME##_orig;

#if defined(__aarch64__)
#define TZ_STUB(NAME)                                                                   \
    TZ_DATA(NAME)                                                                       \
    extern "C" __attribute__((naked, visibility("hidden"))) void tz_##NAME##_stub() {   \
        __asm__ volatile(                                                               \
            "adrp x16, tz_" #NAME "_args\n"                                             \
            "add  x16, x16, :lo12:tz_" #NAME "_args\n"                                  \
            "stp  x0, x1, [x16]\n"                                                      \
            "stp  x2, x3, [x16, #16]\n"                                                 \
            "stp  x4, x5, [x16, #32]\n"                                                 \
            "stp  x6, x7, [x16, #48]\n"                                                 \
            "adrp x16, tz_" #NAME "_count\n"                                            \
            "add  x16, x16, :lo12:tz_" #NAME "_count\n"                                 \
            "ldr  x17, [x16]\n"                                                         \
            "add  x17, x17, #1\n"                                                       \
            "str  x17, [x16]\n"                                                         \
            "adrp x16, tz_" #NAME "_orig\n"                                             \
            "add  x16, x16, :lo12:tz_" #NAME "_orig\n"                                  \
            "ldr  x16, [x16]\n"                                                         \
            "br   x16\n");                                                              \
    }
#else
#define TZ_STUB(NAME)                                                                   \
    TZ_DATA(NAME)                                                                       \
    extern "C" __attribute__((visibility("hidden"))) uint64_t tz_##NAME##_stub(         \
        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,                             \
        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {                           \
        tz_##NAME##_args[0] = a0; tz_##NAME##_args[1] = a1; tz_##NAME##_args[2] = a2;   \
        tz_##NAME##_args[3] = a3; tz_##NAME##_args[4] = a4; tz_##NAME##_args[5] = a5;   \
        tz_##NAME##_args[6] = a6; tz_##NAME##_args[7] = a7;                             \
        ++tz_##NAME##_count;                                                            \
        typedef uint64_t (*Fn)(uint64_t, uint64_t, uint64_t, uint64_t,                  \
                               uint64_t, uint64_t, uint64_t, uint64_t);                 \
        return reinterpret_cast<Fn>(tz_##NAME##_orig)(a0, a1, a2, a3, a4, a5, a6, a7);  \
    }
#endif

TZ_STUB(EndFrame4)
TZ_STUB(BeginFrame4)
TZ_STUB(WaitToBeginFrame)

#define TZ_ENTRY(NAME, FN) \
    {FN, reinterpret_cast<void*>(&tz_##NAME##_stub), &tz_##NAME##_count, &tz_##NAME##_orig, tz_##NAME##_args}

static FrameWatch gWatches[3] = {
    TZ_ENTRY(EndFrame4, "ovrp_EndFrame4"),
    TZ_ENTRY(BeginFrame4, "ovrp_BeginFrame4"),
    TZ_ENTRY(WaitToBeginFrame, "ovrp_WaitToBeginFrame"),
};

int frameWatchCount() { return 3; }
FrameWatch* frameWatch(int i) { return (i >= 0 && i < 3) ? &gWatches[i] : nullptr; }
