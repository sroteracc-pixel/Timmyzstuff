// pointer.h - turns "where the controllers point" into clicks on the menu. Pure C++ (no Android, no VR),
// so every rule here is tested on a normal computer.
//
// WHAT THIS IS (plain words)
//   The menu is a flat picture floating in the air. Each controller has a "pointing line". We find where
//   that line touches the picture (like a laser dot), work out which button is under the dot, and when
//   you pull the trigger we press that button.
//
//   NOT confirmed on a headset yet: whether the controller's "forward" is exactly where you feel you
//   point. If the dot looks a bit high or low, that is one number (kPointerPitchDeg) to adjust.
#pragma once
#include <vector>
#include "panel.h"

namespace tzpanel {

const float kPointerPitchDeg = 0.0f;       // tilt of the pointing line relative to the controller's forward. + = up.

// Where the floating picture is, in the headset's world (metres, Y up). The picture faces back towards the player.
struct Placement { float pos[3]; float yaw; float width, height; };

// head = qx,qy,qz,qw,px,py,pz. The menu appears `distance` metres in front (1.15 by default), at head height, turned to face you.
Placement placeInFront(const float head[7], float scale, float distance);

struct Ray { float o[3]; float d[3]; };
// pose = qx,qy,qz,qw,px,py,pz of a controller. The line starts at the controller and goes along its forward (-Z).
Ray rayFromPose(const float pose[7], float pitchDeg);

// Where the line meets the picture's flat surface, in picture pixels (0,0 = top-left). The point can be
// outside the picture. Returns false if the line points away from the surface.
bool intersect(const Ray& r, const Placement& p, float* px, float* py);

// What one controller is doing this moment.
struct HandAim { bool valid = false; bool onPlane = false; float x = 0, y = 0; float trigger = 0; };

// What happened in this update (the caller redraws / saves / closes as needed).
struct Outcome {
    bool close = false;             // the X button was pressed
    bool redraw = false;            // the menu picture changed
    bool layoutCommitted = false;   // the size or distance slider was let go: move / resize the real panel now
    bool saveNeeded = false;        // a saved setting changed
    bool cursorVisible = false;     // show the pointer dot
    float cursorX = 0, cursorY = 0;
    bool pressed = false;           // the chosen trigger is down right now
    int hand = -1;                  // which controller is the pointer (0 = left, 1 = right), -1 = none
    int clickedId = 0;              // HitId pressed in this update (0 = none)
};

class Interaction {
public:
    // Call when the menu opens. Clicks are ignored until both triggers have been let go once
    // (you open the menu with both triggers held, so that must not count as a click).
    void reset();
    // Call every ~20 ms while the menu is open. `hits` = the clickable areas of the picture as last drawn.
    Outcome update(PanelState& s, const std::vector<HitRect>& hits, const HandAim hands[2]);
    bool dragging() const { return dragging_; }
    bool armed() const { return armed_; }

private:
    void press(int id, float x, PanelState& s, const std::vector<HitRect>& hits, Outcome& o, int hand);
    void dragTo(float x, PanelState& s, const std::vector<HitRect>& hits, Outcome& o);
    bool armed_ = false, down_ = false, dragging_ = false;
    int dragHand_ = -1, dragId_ = 0;
    float lastX_ = 0;
};

}  // namespace tzpanel
