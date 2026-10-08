#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

std::string MenuSettings::toText() const {
    std::ostringstream o;
    o << "menuEnabled=" << (menuEnabled ? 1 : 0) << "\n"
      << "showMenuButton=" << (showMenuButton ? 1 : 0) << "\n"
      << "notifications=" << (notifications ? 1 : 0) << "\n"
      << "smoothUi=" << (smoothUi ? 1 : 0) << "\n";
    char buf[48]; std::snprintf(buf, sizeof buf, "menuDistance=%.2f\n", menuDistance); o << buf;
    return o.str();
}

MenuSettings MenuSettings::fromText(const std::string& s) {
    MenuSettings m;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        char* end = nullptr;
        double d = std::strtod(val.c_str(), &end);
        if (end == val.c_str()) continue;                       // not a number -> ignore (also skips colour lines)
        if (val.find(',') != std::string::npos) continue;       // colour lines look like "a,b,c,d"
        if (key == "menuEnabled") m.menuEnabled = d != 0;
        else if (key == "showMenuButton") m.showMenuButton = d != 0;
        else if (key == "notifications") m.notifications = d != 0;
        else if (key == "smoothUi") m.smoothUi = d != 0;
        else if (key == "menuDistance") m.menuDistance = d < 0.5 ? 0.5f : (d > 3.0 ? 3.0f : (float)d);
    }
    return m;
}

bool saveAll(const Theme& theme, const MenuSettings& settings, const std::string& path) {
    const std::string tmp = path + ".tmp";       // write beside it, then rename: never a half-written file
    { std::ofstream f(tmp, std::ios::trunc); if (!f) return false; f << theme.toText() << settings.toText(); if (!f) return false; }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool loadAll(const std::string& path, Theme& theme, MenuSettings& settings) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    theme = Theme::fromText(ss.str());
    settings = MenuSettings::fromText(ss.str());
    return true;
}
