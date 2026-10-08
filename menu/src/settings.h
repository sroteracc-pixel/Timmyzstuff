// settings.h - the menu's own switches (from the Settings page). Pure C++, tested on a PC.
// These are MENU settings only - no game features.
#pragma once
#include <string>
#include "theme.h"

struct MenuSettings {
    bool  menuEnabled     = true;   // off = the open gesture needs a 3-second hold (so you can't lock yourself out)
    bool  showMenuButton  = true;   // a floating "open" button (placement is the integration's job)
    bool  notifications   = false;  // small pop-up messages (nothing sends any yet)
    bool  smoothUi        = true;   // fade the menu in/out
    float menuDistance    = 1.5f;   // how far from you the menu floats (integration uses it), 0.5 - 3.0

    std::string toText() const;
    static MenuSettings fromText(const std::string& text);   // bad lines ignored, values clamped
};

// One file holds both the colours and the switches.
bool saveAll(const Theme& theme, const MenuSettings& settings, const std::string& path);
bool loadAll(const std::string& path, Theme& theme, MenuSettings& settings);   // false = no file (defaults kept)
