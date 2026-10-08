// Run on a PC (from the menu/ folder):
//   g++ -std=c++17 -Isrc tests/test_menu.cpp src/menu_input.cpp src/theme.cpp src/settings.cpp -o /tmp/t && /tmp/t
#include "menu_input.h"
#include "settings.h"
#include "theme.h"
#include <cmath>
#include <cstdio>
static int pass = 0, fail = 0;
#define CHECK(name, cond) do { if (cond) { ++pass; std::printf("  PASS  %s\n", name); } else { ++fail; std::printf("  FAIL  %s\n", name); } } while (0)
static ControllerState S(float l, float r, bool a, bool b) { ControllerState s; s.leftTrigger = l; s.rightTrigger = r; s.buttonA = a; s.buttonB = b; return s; }
static const float F = 1.0f / 72.0f;   // one Quest frame

int main() {
    std::printf("== open / close\n");
    { MenuInput m;
      CHECK("starts closed", !m.visible());
      m.update(S(1,1,false,false), F); CHECK("both triggers alone: still closed", !m.visible());
      m.update(S(1,1,true,false), F);  CHECK("both triggers + A click: opens", m.visible()); }
    { MenuInput m; m.update(S(1,0,false,false), F); m.update(S(1,0,true,false), F);
      CHECK("only left trigger + A: stays closed", !m.visible()); }
    { MenuInput m; m.update(S(0,0,true,false), F); CHECK("A alone: stays closed", !m.visible()); }
    { MenuInput m; m.update(S(0,0,true,false), F); m.update(S(1,1,true,false), F);
      CHECK("A held BEFORE triggers: does not open", !m.visible()); }
    { MenuInput m; m.update(S(1,1,false,false), F); m.update(S(1,1,true,false), F); m.update(S(1,1,true,false), F);
      CHECK("holding A does not close it again", m.visible()); }
    { MenuInput m; m.update(S(1,1,false,false), F); m.update(S(1,1,true,false), F);
      m.update(S(0,0,false,false), F); CHECK("releasing everything: stays open", m.visible());
      m.update(S(0,0,false,true), F);  CHECK("B click closes", !m.visible()); }
    { MenuInput m; m.update(S(1,1,false,false), F); m.update(S(1,1,true,false), F); m.close();
      CHECK("X button (close()) closes", !m.visible()); }
    { MenuInput m; m.update(S(0.75f,0.75f,false,false), F); m.update(S(0.55f,0.55f,true,false), F);
      CHECK("trigger hysteresis: 0.55 still held once pressed", m.visible()); }
    { MenuInput m; m.update(S(0.5f,0.5f,false,false), F); m.update(S(0.5f,0.5f,true,false), F);
      CHECK("half-pressed triggers never count", !m.visible()); }

    std::printf("== A selects\n");
    { MenuInput m; m.update(S(1,1,false,false), F); m.update(S(1,1,true,false), F);
      CHECK("the A click that opened the menu does NOT select", m.visible() && !m.selectDown());
      m.update(S(1,1,true,false), F); CHECK("...even while A is still held", !m.selectDown());
      m.update(S(0,0,false,false), F); CHECK("A released: nothing selected", !m.selectDown());
      m.update(S(0,0,true,false), F);  CHECK("A pressed again: selects", m.selectDown());
      m.update(S(0,0,false,false), F); CHECK("A released: select ends", !m.selectDown());
      m.update(S(0,0,true,false), F); m.close(); m.update(S(0,0,true,false), F);
      CHECK("closed menu never selects", !m.selectDown()); }

    std::printf("== Menu Enabled = off (3-second safety hold)\n");
    { MenuInput m; m.setMenuEnabled(false);
      m.update(S(1,1,false,false), F); m.update(S(1,1,true,false), F);
      CHECK("normal gesture no longer opens it", !m.visible());
      for (int i = 0; i < 72; ++i) m.update(S(1,1,true,false), F);        // ~1 second
      CHECK("1-second hold: still closed", !m.visible());
      for (int i = 0; i < 160; ++i) m.update(S(1,1,true,false), F);       // ~3.2 s total
      CHECK("3-second hold: opens (can never lock you out)", m.visible()); }
    { MenuInput m; m.setMenuEnabled(false);
      for (int i = 0; i < 150; ++i) m.update(S(1,1,true,false), F);       // ~2 s
      m.update(S(1,1,false,false), F);                                    // let go -> timer resets
      for (int i = 0; i < 150; ++i) m.update(S(1,1,true,false), F);       // ~2 s again
      CHECK("letting go resets the 3-second timer", !m.visible()); }

    std::printf("== theme\n");
    { Theme d = Theme::defaults(); Theme r = Theme::fromText(d.toText());
      CHECK("save then load gives the same colours", std::fabs(r.accent.r - d.accent.r) < 1e-3 && std::fabs(r.window.a - d.window.a) < 1e-3); }
    { Theme t = Theme::fromText("garbage\naccent=notnumbers\nbutton=2,-1,0.5,9\n"); Theme d = Theme::defaults();
      CHECK("bad lines are ignored", t.accent.r == d.accent.r);
      CHECK("out-of-range values clamp to 0..1", t.button.r == 1.f && t.button.g == 0.f && t.button.a == 1.f); }
    { Theme d = Theme::defaults();
      CHECK("default is DARK (background luminance < 0.15)", (d.window.r + d.window.g + d.window.b) / 3.f < 0.15f); }
    { Theme d = Theme::defaults(), p = d.withAccent(Theme::swatch(1));
      CHECK("withAccent changes accent but not the dark background", p.accent.r != d.accent.r && p.window.r == d.window.r && p.window.g == d.window.g); }
    { bool distinct = true; for (int i = 1; i < Theme::swatchCount(); ++i) { Color a = Theme::swatch(i), b = Theme::swatch(0); if (a.r == b.r && a.g == b.g && a.b == b.b) distinct = false; }
      CHECK("5 swatches (cyan default, purple, magenta, blue, white) are distinct", Theme::swatchCount() == 5 && distinct); }

    std::printf("== settings file\n");
    { MenuSettings m; CHECK("defaults match the design (notifications off, others on, 1.5x)",
        m.menuEnabled && m.showMenuButton && !m.notifications && m.smoothUi && std::fabs(m.menuDistance - 1.5f) < 1e-6); }
    { MenuSettings m; m.notifications = true; m.menuEnabled = false; m.menuDistance = 2.2f;
      MenuSettings r = MenuSettings::fromText(m.toText());
      CHECK("settings round-trip", r.notifications && !r.menuEnabled && std::fabs(r.menuDistance - 2.2f) < 1e-2); }
    { MenuSettings r = MenuSettings::fromText("menuDistance=99\nsmoothUi=abc\nwindow=0.1,0.2,0.3,1\n");
      CHECK("distance clamps to 3.0, junk and colour lines ignored", r.menuDistance == 3.0f && r.smoothUi); }
    { const char* p = "/tmp/tz_all_test.txt"; Theme t = Theme::defaults().withAccent(Theme::swatch(2)); MenuSettings s; s.smoothUi = false;
      CHECK("saveAll works", saveAll(t, s, p));
      Theme t2; MenuSettings s2; CHECK("loadAll finds the file", loadAll(p, t2, s2));
      CHECK("one file restores BOTH colours and switches", std::fabs(t2.accent.g - t.accent.g) < 1e-3 && !s2.smoothUi);
      std::remove(p);
      Theme t3 = Theme::defaults(); MenuSettings s3; CHECK("missing file -> loadAll false, defaults kept", !loadAll("/nonexistent/x.txt", t3, s3) && s3.smoothUi); }

    std::printf("\npassed: %d  failed: %d\n", pass, fail);
    return fail ? 1 : 0;
}
