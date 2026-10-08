#include "menu_input.h"

// A trigger counts as "held" above 0.7, and only counts as "released" below 0.4.
// The gap stops it flickering when your finger sits half-way.
bool MenuInput::held(float value, bool wasHeld) const {
    return wasHeld ? (value > 0.4f) : (value > 0.7f);
}

bool MenuInput::update(const ControllerState& now, float dt) {
    leftHeld_  = held(now.leftTrigger,  leftHeld_);
    rightHeld_ = held(now.rightTrigger, rightHeld_);

    // "Clicked" means the button went from up to down THIS frame (not just held).
    const bool aClicked = now.buttonA && !prevA_;
    const bool bClicked = now.buttonB && !prevB_;
    prevA_ = now.buttonA;
    prevB_ = now.buttonB;

    if (!visible_) {
        const bool chordHeld = leftHeld_ && rightHeld_ && now.buttonA;
        if (enabled_) {
            holdSeconds_ = 0.0f;
            if (leftHeld_ && rightHeld_ && aClicked) visible_ = true;     // OPEN: both triggers, then click A
        } else {
            holdSeconds_ = chordHeld ? holdSeconds_ + dt : 0.0f;           // OPEN (safety mode): hold 3 seconds
            if (holdSeconds_ >= 3.0f) { visible_ = true; holdSeconds_ = 0.0f; }
        }
    } else if (bClicked) {
        visible_ = false;                                                  // CLOSE: B (the X button calls close())
    }
    return visible_;
}
