// menu_input.h - decides WHEN the menu opens and closes.
// Pure C++: no game code, no ImGui. That is why it can be tested on a PC.
#pragma once

// What the controllers are doing right now. YOU (later) fill this in from the game/headset.
struct ControllerState {
    float leftTrigger  = 0.0f;   // index trigger, 0 = not pressed ... 1 = fully pressed
    float rightTrigger = 0.0f;
    bool  buttonA = false;       // true while A is held down
    bool  buttonB = false;       // true while B is held down

    // Where the controller's pointing ray hits the menu, in menu pixels (0,0 = top-left of the menu).
    // The INTEGRATION works this out (it needs the controller pose + where the menu floats). Not done yet.
    float pointerX = 0.0f, pointerY = 0.0f;
    bool  pointerOnMenu = false;
};

class MenuInput {
public:
    // Call once per frame. dt = seconds since the last frame. Returns true if the menu should be visible.
    bool update(const ControllerState& now, float dt);

    bool visible() const { return visible_; }
    void close() { visible_ = false; }          // the on-screen X button calls this

    // "Menu Enabled" switch. When OFF the normal gesture is not enough: you must hold
    // both triggers + A for 3 seconds. (So switching it off can never lock you out.)
    void setMenuEnabled(bool on) { enabled_ = on; }

    // A = "select" while the menu is open. True while A is held, BUT only after A has been let go
    // once since the menu opened - so the very A click that opened the menu never presses a button.
    bool selectDown() const { return visible_ && prevA_ && !waitForAReleaseAfterOpen_; }

private:
    bool held(float value, bool wasHeld) const;
    bool visible_ = false, enabled_ = true;
    bool leftHeld_ = false, rightHeld_ = false;
    bool prevA_ = false, prevB_ = false;
    float holdSeconds_ = 0.0f;
    bool waitForAReleaseAfterOpen_ = false;
};
