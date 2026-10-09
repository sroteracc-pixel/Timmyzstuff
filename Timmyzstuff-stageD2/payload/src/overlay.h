// overlay.h - shows the menu picture inside the headset as an extra "layer" on top of the game.
//
// WHAT THIS IS (plain words)
//   Meta's VR system lets an app send extra flat panels ("layers") next to its own picture.
//   We ask Meta's library for one such panel that is backed by an Android drawing surface, paint our
//   menu picture into it with plain CPU code, and hand it over together with the game's own layers
//   at the end of every frame. The game's own rendering is not touched.
//
//   NOTHING here is confirmed on a real headset yet. Every step is written to the facts file.
#pragma once
#include <stdint.h>
#include "panel.h"

namespace tzoverlay {

typedef void (*LogFn)(const char* fmt, ...);

// Set the Java VM (needed to talk to the Android surface). Call once from JNI_OnLoad.
void setJavaVM(void* vm);

// Creates the layer and its surface and draws the first picture. Safe to call again after a failure.
// `ovr` = handle of libOVRPlugin.so. Returns true when the panel is ready to show.
bool init(void* ovr, LogFn log);
bool ready();

// Opens the menu: head = qx,qy,qz,qw,px,py,pz of the head. The panel appears in front of it.
void show(const float head[7]);
void hide();
bool visible();

// Menu state (what the picture shows). Change it, then call markDirty().
tzpanel::PanelState& state();
void markDirty();

// Call often from a background thread: re-paints / re-posts the picture when needed.
void tick(double nowSeconds, LogFn log);

// The doorway for ovrp_EndFrame4. Called by the game thread every frame; `real` = the real function.
int hookEndFrame4(int frameIndex, const void* const* layers, int layerCount, void* extra);
void setRealEndFrame4(uint64_t realFunction);

// Counters for the facts file.
struct Stats { uint64_t calls, withOverlay, retries; int lastRc; int firstBadRc; bool broken; };
Stats stats();

#ifdef TZ_PC_TEST
void testForceReady(int layerId, int minorVersion);   // PC test only
const unsigned char* testSubmitBytes();
#endif

}  // namespace tzoverlay
