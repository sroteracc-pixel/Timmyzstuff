// Renders the menu picture to .ppm files so a person can LOOK at it. PC only.
#include <cstdio>
#include "panel.h"
using namespace tzpanel;
static void save(const char* path, const Canvas& c, int bgGrey = 60) {
    FILE* f = std::fopen(path, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
    for (int y = 0; y < c.height(); ++y) for (int x = 0; x < c.width(); ++x) {
        const uint8_t* p = c.data() + (y * c.width() + x) * 4;
        // show on a mid grey so transparent parts are visible
        unsigned bg = bgGrey + ((x / 32 + y / 32) % 2) * 20; unsigned a = p[3];
        unsigned out[3] = { p[0] + bg * (255 - a) / 255, p[1] + bg * (255 - a) / 255, p[2] + bg * (255 - a) / 255 };
        for (int k = 0; k < 3; ++k) std::fputc(out[k] > 255 ? 255 : out[k], f);
    }
    std::fclose(f);
}
int main(int argc, char** argv) {
    Canvas c(kWidth, kHeight);
    PanelState s; std::vector<HitRect> hits;
    drawPanel(c, s, &hits); save(argv[1], c);
    std::printf("hits=%zu\n", hits.size());
    s.colorIndex = 1; s.sound = false; s.hover = HIT_COLOR0 + 7; s.transparency = 0.25f; s.scale = 1.25f; s.distance = 0.8f; s.dragSlider = HIT_SLIDER_DIST;
    drawPanel(c, s, nullptr); drawCursor(c, 700, 480, s, false); save(argv[2], c);
    PanelState t; t.colorIndex = 7; t.tab = 4; t.hover = HIT_TAB0 + 6;
    drawPanel(c, t, nullptr); c.fadeAll(0.25f); save(argv[3], c);
    if (argc > 5) {   // Movement page: everything off, then a mix of settings with the scan result showing
        PanelState m; m.tab = kTabMovement;
        drawPanel(c, m, nullptr); save(argv[4], c);
        PanelState n; n.tab = kTabMovement; n.colorIndex = 4;
        n.speedOn = true; n.speedMul = 3.7f; n.jumpMul = 2.3f; n.gravityMode = 1; n.lowGravPct = 90; n.highGravPct = 35; n.hover = HIT_TOGGLE_JUMP; n.scanState = 2; n.linkState = 0;
        drawPanel(c, n, nullptr); save(argv[5], c);
    }
    if (argc > 7) {   // Basketball page (Aimbot): off at Unlimited, then on at 23 m while the slider is dragged, with a scan running
        PanelState a; a.tab = kTabBasketball;
        drawPanel(c, a, nullptr); save(argv[6], c);
        PanelState b; b.tab = kTabBasketball; b.aimOn = true; b.aimCapM = 23; b.dragSlider = HIT_SLIDER_AIMCAP; b.scanState = 1;
        drawPanel(c, b, nullptr); save(argv[7], c);
    }
    if (argc > 9) {   // stage D8: the game link card with a connected link and a shot result, then a failed link with a very long reason
        PanelState a; a.tab = kTabBasketball; a.aimOn = true; a.aimLinkState = 1; a.aimCapM = 30;
        a.aimHeadline = "connected - waiting for your shot"; a.aimLastShot = "shot #3: AIMED from 14 m - SCORED, 0.03 m off centre";
        drawPanel(c, a, nullptr); save(argv[8], c);
        PanelState b; b.tab = kTabBasketball; b.aimOn = true; b.aimLinkState = 3;
        b.aimHeadline = "FAILED: the game's class ShovelTools.BallControlManager has no field called _releasedRightTimer and the memory search found no GameManager either";
        b.aimLastShot = "shot #1: not aimed - 31 m is farther than your 20 m limit and some more words that cannot fit";
        drawPanel(c, b, nullptr); save(argv[9], c);
    }
    return 0;
}
