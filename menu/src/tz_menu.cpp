#include "tz_menu.h"
#include "imgui.h"
#include "integration.h"
#include "menu_ui.h"

void TzMenu::init() {
    path_ = Integration_DataDir() + "menu_settings.txt";
    loadAll(path_, theme_, settings_);            // no file yet -> dark default stays
    TzApplyTheme(theme_);
}

void TzMenu::frame() {
    const float dt = ImGui::GetIO().DeltaTime;
    input_.setMenuEnabled(settings_.menuEnabled);

    ControllerState pad;
    if (Integration_ReadControllers(pad)) input_.update(pad, dt);   // no input available -> menu stays closed

    const bool nowVisible = input_.visible();
    if (nowVisible != wasVisible_) {                                  // opened or closed this frame
        Integration_SetGameInputBlocked(nowVisible);
        wasVisible_ = nowVisible;
    }

    // Smooth UI: fade in/out over ~0.15 s. Off: snap.
    const float target = nowVisible ? 1.0f : 0.0f;
    if (settings_.smoothUi) {
        const float step = dt / 0.15f;
        fade_ += (target > fade_) ? step : -step;
        if (fade_ < 0.f) fade_ = 0.f;
        if (fade_ > 1.f) fade_ = 1.f;
    } else {
        fade_ = target;
    }

    if (fade_ > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade_);
        TzDrawMenu(input_, theme_, settings_, path_);
        ImGui::PopStyleVar();
    }
}
