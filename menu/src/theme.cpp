#include "theme.h"
#include <cstdio>
#include <fstream>
#include <sstream>

static Color C(int r, int g, int b, int a = 255) { return {r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }
static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static Color mix(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color Theme::swatch(int i) {
    switch (i) {
        case 1:  return C(155, 77, 255);   // purple
        case 2:  return C(224, 64, 200);   // magenta
        case 3:  return C(61, 123, 255);   // blue
        case 4:  return C(240, 242, 250);  // white
        default: return C(63, 208, 255);   // cyan (default)
    }
}
int Theme::swatchCount() { return 5; }
const char* Theme::swatchName(int i) {
    static const char* n[] = {"Cyan", "Purple", "Magenta", "Blue", "White"};
    return (i >= 0 && i < 5) ? n[i] : n[0];
}

Theme Theme::withAccent(const Color& a) const {
    Theme t = *this;
    t.accent = a;
    t.buttonHover = mix(button, a, 0.22f);     // hover glows a little in the accent colour
    return t;
}

Theme Theme::defaults() {
    Theme base{C(11, 15, 28, 245), C(18, 24, 40), C(63, 208, 255), C(24, 32, 54), C(34, 48, 80),
               C(242, 246, 255), C(138, 148, 176)};
    return base.withAccent(swatch(0));
}

static void put(std::ostringstream& o, const char* key, const Color& c) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s=%.4f,%.4f,%.4f,%.4f\n", key, c.r, c.g, c.b, c.a);
    o << buf;
}

std::string Theme::toText() const {
    std::ostringstream o;
    o << "# Timmyzstuff menu theme\n";
    put(o, "window", window);   put(o, "panel", panel);       put(o, "accent", accent);
    put(o, "button", button);   put(o, "buttonHover", buttonHover);
    put(o, "text", text);       put(o, "textMuted", textMuted);
    return o.str();
}

Theme Theme::fromText(const std::string& s) {
    Theme t = defaults();
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        float v[4];
        if (std::sscanf(line.c_str() + eq + 1, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) continue;
        Color c{clamp01(v[0]), clamp01(v[1]), clamp01(v[2]), clamp01(v[3])};
        if (key == "window") t.window = c;           else if (key == "panel") t.panel = c;
        else if (key == "accent") t.accent = c;      else if (key == "button") t.button = c;
        else if (key == "buttonHover") t.buttonHover = c;
        else if (key == "text") t.text = c;          else if (key == "textMuted") t.textMuted = c;
    }
    return t;
}

bool saveTheme(const Theme& t, const std::string& path) {
    const std::string tmp = path + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); if (!f) return false; f << t.toText(); if (!f) return false; }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

Theme loadTheme(const std::string& path, bool* found) {
    std::ifstream f(path);
    if (found) *found = static_cast<bool>(f);
    if (!f) return Theme::defaults();
    std::stringstream ss; ss << f.rdbuf();
    return Theme::fromText(ss.str());
}
