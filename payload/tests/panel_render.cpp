// Renders the menu picture to .ppm files so a person can LOOK at it. PC only.
#include <cstdio>
#include "panel.h"
using namespace tzpanel;
static void save(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
    for (int y = 0; y < c.height(); ++y) for (int x = 0; x < c.width(); ++x) {
        const uint8_t* p = c.data() + (y * c.width() + x) * 4;
        // show on a checker-ish mid grey so transparent corners are visible
        unsigned bg = 60; unsigned a = p[3];
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
    s.tab = 4; s.sound = false; s.rgb[0] = 255; s.rgb[1] = 0; s.rgb[2] = 0; s.hover = HIT_TAB0 + 6;
    drawPanel(c, s, nullptr); save(argv[2], c);
    return 0;
}
