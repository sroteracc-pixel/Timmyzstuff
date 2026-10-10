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
#include "pointer.h"

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

// ---- stage D3: pointing and clicking -------------------------------------------------------
// What the two controllers are doing right now (the caller reads it from Meta's functions).
// pose = qx,qy,qz,qw,px,py,pz in the same space as the head pose given to show(). Index 0 = left, 1 = right.
struct PointerSample { bool valid[2]; float pose[2][7]; float trigger[2]; };
struct PointerResult { bool close = false; bool clicked = false; int clickedId = 0; int hand = -1; bool onMenu = false; float x = 0, y = 0; };

// Call every ~20 ms while the menu is open. Moves the dot, highlights, presses buttons, drags the slider.
// `close` = the X button was pressed (the caller should close the menu).
PointerResult pointer(double nowSeconds, const PointerSample& sample, LogFn log);

// Saved settings (sound, colour, size, transparency, distance, movement slider values). Loads the file now (if it exists) and saves whenever a setting changes.
void setSettingsPath(const char* path, LogFn log);

// ---- Movement page <-> the rest of the payload ----
// The "Scan game code" button sets a request; the payload's main loop picks it up (once) and starts the scan.
bool takeScanRequest();
bool takeShotScanRequest();                   // the "Scan ball and hoops" button on the Basketball page (the same one-at-a-time scan, a different report)
void setScanResult(bool ok, int matches);     // called when the scan ends (any thread)
void setLinkState(int state);                 // 0 = waiting (no switch on), 1 = connected to the game, 2 = looking for the player object, 3 = failed
// stage D8: the Basketball page's game link card. state: 0 off, 1 connected, 2 looking for your ball control, 3 failed. The two texts are short plain sentences (cut to fit).
void setAimInfo(int state, const char* headline, const char* lastShot);
// A copy of what the Movement switches/sliders ask for right now (taken under the menu's lock).
struct MovementAsk { bool speedOn, jumpOn; int gravityMode; float speed, jump, lowPct, highPct; };
MovementAsk movementAsk();
// What the Basketball page asks for right now (taken under the menu's lock). `capM` is 5 .. 50; 50 means "Unlimited".
// `on` = the Direct Aimbot, `bank` = Aimbot Bank (stage D9). The menu never turns both on; if both were ever on, the caller treats it as "neither" (nothing is guessed).
struct AimAsk { bool on; float capM; bool holdY; bool bank; };
AimAsk aimAsk();
// stage D10 (Troll page, "Shot points"): what the page asks for right now, and the link card's texts going back to the page.
// `stop` is the slider stop 1 .. 12 (see points.h: stops 1 .. 11 = that many points, 12 = 999).
struct PointsAsk { bool on; float stop; };
PointsAsk pointsAsk();
void setPointsInfo(int state, const char* headline, const char* lastBasket);     // state: 0 off, 1 connected, 2 looking for your ball, 3 failed
// stage D11 (Troll page, "Hitbox expander" + "See hitbox"): what the page asks for right now, and the status line going back to the page.
// `mul` is the slider value 1.0 .. 5.0 (1.0 = normal size). `see` is the See hitbox switch.
struct HitboxAsk { bool on; float mul; bool see; };
HitboxAsk hitboxAsk();
void setHitboxInfo(int state, const char* headline);     // state: 0 off, 1 connected, 2 looking for your hands, 3 failed

// True while the menu is open, and for a moment after it closes. The input doorway uses this to hide
// button presses from the game so that clicking the menu does not also play the game.
bool inputBlocked();
int filterControllerState(int realResult, void* stateOut);   // call AFTER the real ovrp_GetControllerState4; blanks the game's view while the menu is open
void countInput(bool masked);          // statistics for the facts file
struct InputStats { uint64_t calls, masked; };
InputStats inputStats();

// Counters for the facts file.
struct Stats { uint64_t calls, withOverlay, retries; int lastRc; int firstBadRc; bool broken; };
Stats stats();

#ifdef TZ_PC_TEST
void testForceReady(int layerId, int minorVersion);   // PC test only
const unsigned char* testSubmitBytes();
#endif

}  // namespace tzoverlay
