// tz_menu.h - the one object the payload talks to.
#pragma once
#include <string>
#include "menu_input.h"
#include "settings.h"
#include "theme.h"

class TzMenu {
public:
    void init();            // load saved colours/settings (or the dark default) and apply them. Call once, after ImGui exists.
    void frame();           // call once per frame INSIDE the ImGui frame (between NewFrame and Render).
    bool visible() const { return input_.visible(); }
    const MenuSettings& settings() const { return settings_; }   // the integration reads menuDistance etc. from here
private:
    MenuInput input_;
    Theme theme_ = Theme::defaults();
    MenuSettings settings_;
    std::string path_;
    bool wasVisible_ = false;
    float fade_ = 0.0f;     // 0 = invisible, 1 = fully visible (used by "Smooth UI")
};
