// theme.h - the menu's colours. Dark navy by default; the accent colour is the part you change.
// Pure C++ (no ImGui) so it can be tested on a PC.
#pragma once
#include <string>

struct Color { float r, g, b, a; };

struct Theme {
    Color window;        // menu background (dark navy)
    Color panel;         // boxes / rows
    Color accent;        // glow border, selected tab, switches, slider
    Color button;        // normal button
    Color buttonHover;   // button under the pointer
    Color text;          // main text
    Color textMuted;     // quieter text

    static Theme defaults();                  // dark navy + cyan accent
    Theme withAccent(const Color& accent) const;   // same theme, new accent (hover colour follows it)

    // The 5 quick accent swatches shown in Settings (cyan, purple, magenta, blue, white)
    static int swatchCount();
    static Color swatch(int index);
    static const char* swatchName(int index);

    std::string toText() const;
    static Theme fromText(const std::string& text);   // bad or missing lines fall back to defaults
};

bool saveTheme(const Theme& t, const std::string& path);
Theme loadTheme(const std::string& path, bool* found = nullptr);
