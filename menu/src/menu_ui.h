// menu_ui.h - what the menu LOOKS like (uses Dear ImGui). Not yet compiled: needs the ImGui source.
#pragma once
#include <string>
#include "imgui.h"
#include "menu_input.h"
#include "settings.h"
#include "theme.h"

// Paint the ImGui colours/rounding from a Theme. Call once at start, and again whenever colours change.
void TzApplyTheme(const Theme& theme);

// Draw the menu window. Call between ImGui::NewFrame() and ImGui::Render(), only while visible.
void TzDrawMenu(MenuInput& input, Theme& theme, MenuSettings& settings, const std::string& filePath);
