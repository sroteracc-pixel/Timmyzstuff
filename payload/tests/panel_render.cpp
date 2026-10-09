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
int main(int, char** argv) {
    Canvas c(kWidth, kHeight);
    PanelState s; std::vector<HitRect> hits;
    drawPanel(c, s, &hits); save(argv[1], c);
    std::printf("hits=%zu\n", hits.size());
    s.colorIndex = 1; s.sound = false; s.hover = HIT_COLOR0 + 7; s.transparency = 0.25f; s.scale = 1.25f; s.distance = 0.8f; s.dragSlider = HIT_SLIDER_DIST;
    drawPanel(c, s, nullptr); drawCursor(c, 700, 480, s, false); save(argv[2], c);
    PanelState t; t.colorIndex = 7; t.tab = 4; t.hover = HIT_TAB0 + 6;
    drawPanel(c, t, nullptr); c.fadeAll(0.25f); save(argv[3], c);
    return 0;
}
