#include "menu_ui.h"

static ImVec4 V(const Color& c) { return ImVec4(c.r, c.g, c.b, c.a); }
static ImU32  U(const Color& c) { return ImGui::GetColorU32(V(c)); }

void TzApplyTheme(const Theme& t) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 18.f;  s.FrameRounding = 10.f; s.GrabRounding = 10.f;
    s.ChildRounding = 12.f;   s.PopupRounding = 10.f; s.TabRounding = 10.f;
    s.WindowPadding = ImVec2(16, 14);  s.FramePadding = ImVec2(12, 8);  s.ItemSpacing = ImVec2(10, 10);
    s.WindowBorderSize = 2.f;  s.ChildBorderSize = 1.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = V(t.window);
    c[ImGuiCol_ChildBg]        = V(t.panel);
    c[ImGuiCol_PopupBg]        = V(t.window);
    c[ImGuiCol_Border]         = V(t.accent);          // the glowing outline
    c[ImGuiCol_Text]           = V(t.text);
    c[ImGuiCol_TextDisabled]   = V(t.textMuted);
    c[ImGuiCol_FrameBg]        = V(t.button);
    c[ImGuiCol_FrameBgHovered] = V(t.buttonHover);
    c[ImGuiCol_FrameBgActive]  = V(t.buttonHover);
    c[ImGuiCol_Button]         = V(t.button);
    c[ImGuiCol_ButtonHovered]  = V(t.buttonHover);
    c[ImGuiCol_ButtonActive]   = V(t.accent);
    c[ImGuiCol_Header]         = V(t.buttonHover);     // selected sidebar row
    c[ImGuiCol_HeaderHovered]  = V(t.buttonHover);
    c[ImGuiCol_HeaderActive]   = V(t.accent);
    c[ImGuiCol_CheckMark]      = V(t.accent);
    c[ImGuiCol_SliderGrab]     = V(t.accent);
    c[ImGuiCol_SliderGrabActive] = V(t.accent);
}

// A switch like the ones in the design. Returns true if it was clicked this frame.
static bool Toggle(const char* label, bool* value, const Theme& t) {
    ImGui::PushID(label);
    const float rowH = 40.f, trackW = 52.f, trackH = 26.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##toggle", ImVec2(rowW, rowH));
    const bool clicked = ImGui::IsItemClicked();
    if (clicked) *value = !*value;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), U(t.button), 10.f);
    dl->AddText(ImVec2(p.x + 14.f, p.y + (rowH - ImGui::GetFontSize()) * 0.5f), U(t.text), label);
    const ImVec2 tp(p.x + rowW - trackW - 14.f, p.y + (rowH - trackH) * 0.5f);
    dl->AddRectFilled(tp, ImVec2(tp.x + trackW, tp.y + trackH), *value ? U(t.accent) : U(t.panel), trackH * 0.5f);
    const float cx = *value ? tp.x + trackW - trackH * 0.5f : tp.x + trackH * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, tp.y + trackH * 0.5f), trackH * 0.5f - 3.f, IM_COL32(255, 255, 255, 255));
    ImGui::PopID();
    return clicked;
}

static void DrawGlow(const Theme& t) {
    // Soft outer glow around the window: a few faint outlines that fade out.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b = ImVec2(a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight());
    for (int i = 1; i <= 4; ++i) {
        Color g = t.accent; g.a = 0.20f / i;
        dl->AddRect(ImVec2(a.x - i * 2.f, a.y - i * 2.f), ImVec2(b.x + i * 2.f, b.y + i * 2.f), U(g), 18.f + i * 2.f, 0, 2.f);
    }
}

static const char* kTabs[] = {"Settings", "Favorites", "Active", "Movement", "Basketball",
                              "Baseball", "Soccer", "Football", "Paintball", "Boxing"};

void TzDrawMenu(MenuInput& input, Theme& theme, MenuSettings& settings, const std::string& filePath) {
    static int tab = 0;
    static std::string message;

    ImGui::SetNextWindowSize(ImVec2(760, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("##timmyzstuff_menu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    DrawGlow(theme);

    // ---- title row: "Timmyz" + accent-coloured "stuff", X button on the right ----
    ImGui::SetWindowFontScale(1.7f);   // (newer ImGui versions: use a bigger font instead)
    ImGui::TextUnformatted("Timmyz");
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, V(theme.accent));
    ImGui::TextUnformatted("stuff");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine(ImGui::GetWindowWidth() - 60.f);
    if (ImGui::Button("X", ImVec2(38, 38))) input.close();          // <- the X button

    // ---- left sidebar ----
    ImGui::BeginChild("##sidebar", ImVec2(190, -48), true);
    for (int i = 0; i < (int)(sizeof kTabs / sizeof kTabs[0]); ++i) {
        if (ImGui::Selectable(kTabs[i], tab == i, 0, ImVec2(0, 38))) tab = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right content ----
    ImGui::BeginChild("##content", ImVec2(0, -48), true);
    if (tab == 0) {
        ImGui::TextUnformatted("Settings");
        ImGui::Separator();
        bool changed = false;

        // Harmless self-test: proves the pointer + A "select" really reach the menu.
        static int testClicks = 0;
        if (ImGui::Button("Test button")) ++testClicks;
        ImGui::SameLine();
        ImGui::TextDisabled("clicked %d times", testClicks);
        ImGui::Spacing();
        changed |= Toggle("Menu Enabled", &settings.menuEnabled, theme);
        if (!settings.menuEnabled) ImGui::TextDisabled("Off: open the menu by holding both triggers + A for 3 seconds.");
        changed |= Toggle("Show Menu Button", &settings.showMenuButton, theme);
        changed |= Toggle("Enable Notifications", &settings.notifications, theme);
        changed |= Toggle("Smooth UI", &settings.smoothUi, theme);

        ImGui::Spacing();
        ImGui::TextUnformatted("Menu Distance");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderFloat("##dist", &settings.menuDistance, 0.5f, 3.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::TextUnformatted("Accent Color");
        for (int i = 0; i < Theme::swatchCount(); ++i) {
            if (i) ImGui::SameLine();
            ImGui::PushID(i);
            const Color sw = Theme::swatch(i);
            if (ImGui::ColorButton(Theme::swatchName(i), V(sw), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(40, 40))) {
                theme = theme.withAccent(sw); TzApplyTheme(theme); changed = true;
            }
            // ring around the selected swatch
            const bool selected = (theme.accent.r == sw.r && theme.accent.g == sw.g && theme.accent.b == sw.b);
            if (selected) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 6.f, 0, 2.f);
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Customize all colors")) {
            bool c2 = false;
            c2 |= ImGui::ColorEdit4("Background", &theme.window.r);
            c2 |= ImGui::ColorEdit4("Panels",     &theme.panel.r);
            c2 |= ImGui::ColorEdit4("Accent",     &theme.accent.r);
            c2 |= ImGui::ColorEdit4("Buttons",    &theme.button.r);
            c2 |= ImGui::ColorEdit4("Hover",      &theme.buttonHover.r);
            c2 |= ImGui::ColorEdit4("Text",       &theme.text.r);
            c2 |= ImGui::ColorEdit4("Quiet text", &theme.textMuted.r);
            if (c2) { TzApplyTheme(theme); changed = true; }
            if (ImGui::Button("Reset to dark default")) { theme = Theme::defaults(); TzApplyTheme(theme); changed = true; }
        }

        if (changed) {   // save automatically so settings survive restarts
            message = saveAll(theme, settings, filePath) ? "" : "Could not save settings.";
        }
        if (!message.empty()) ImGui::TextDisabled("%s", message.c_str());
    } else {
        ImGui::TextUnformatted(kTabs[tab]);
        ImGui::Separator();
        ImGui::TextDisabled("Nothing here yet.");   // placeholder page - no features added
    }
    ImGui::EndChild();

    // ---- footer hints (like the design) ----
    ImGui::TextDisabled("Point: aim at options     A: select     B: close menu");
    ImGui::End();
}
