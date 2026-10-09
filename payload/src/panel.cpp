// panel.cpp - see panel.h. Pure CPU drawing, no Android, no VR.
#include "panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tzpanel {

namespace {
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// distance from point to a rounded box (negative inside)
inline float sdRoundRect(float px, float py, float cx, float cy, float hx, float hy, float r) {
    const float qx = std::fabs(px - cx) - (hx - r), qy = std::fabs(py - cy) - (hy - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}
inline Color mix(Color a, Color b, float t) { return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
inline Color withA(Color c, float a) { c.a = a; return c; }
inline Color scaleC(Color c, float k) { return Color{clamp01(c.r * k), clamp01(c.g * k), clamp01(c.b * k), c.a}; }
}  // namespace

const char* tabName(int i) {
    static const char* names[kTabCount] = {"Settings", "Favorites", "Active", "Movement", "Basketball", "Baseball",
                                           "Soccer", "Football", "Paintball", "Boxing", "Troll"};
    return (i >= 0 && i < kTabCount) ? names[i] : "";
}

const ColorChoice& colorChoice(int i) {
    static const ColorChoice colors[kColorCount] = {
        {"Default", {139, 92, 246}},    // the original purple
        {"Red",     {239, 68, 68}},
        {"Orange",  {249, 115, 22}},
        {"Yellow",  {250, 204, 21}},
        {"Lime",    {132, 204, 22}},
        {"Green",   {34, 197, 94}},
        {"Cyan",    {6, 182, 212}},
        {"Blue",    {59, 130, 246}},
        {"Pink",    {236, 72, 153}},
        {"White",   {226, 232, 240}},
    };
    return colors[(i >= 0 && i < kColorCount) ? i : 0];
}

bool sliderRange(int id, float* lo, float* hi, float* step) {
    switch (id) {
    case HIT_SLIDER_SIZE:  *lo = kScaleMin; *hi = kScaleMax; *step = kScaleStep; return true;
    case HIT_SLIDER_ALPHA: *lo = kAlphaMin; *hi = kAlphaMax; *step = kAlphaStep; return true;
    case HIT_SLIDER_DIST:  *lo = kDistMin;  *hi = kDistMax;  *step = kDistStep;  return true;
    default: return false;
    }
}
float& sliderValue(PanelState& s, int id) {
    static float dummy = 0;
    switch (id) {
    case HIT_SLIDER_SIZE: return s.scale;
    case HIT_SLIDER_ALPHA: return s.transparency;
    case HIT_SLIDER_DIST: return s.distance;
    default: return dummy;
    }
}
float snapSlider(int id, float v) {
    float lo, hi, step;
    if (!sliderRange(id, &lo, &hi, &step)) return v;
    v = lo + std::round((v - lo) / step) * step;
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------------- Canvas
Canvas::Canvas(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h * 4, 0) {}

void Canvas::clear(Color c) {
    const float a = c.a;
    for (size_t i = 0; i < px_.size(); i += 4) {
        px_[i] = static_cast<uint8_t>(clamp01(c.r * a) * 255 + 0.5f);
        px_[i + 1] = static_cast<uint8_t>(clamp01(c.g * a) * 255 + 0.5f);
        px_[i + 2] = static_cast<uint8_t>(clamp01(c.b * a) * 255 + 0.5f);
        px_[i + 3] = static_cast<uint8_t>(clamp01(a) * 255 + 0.5f);
    }
}

void Canvas::fadeAll(float keep) {
    keep = clamp01(keep);
    const unsigned k = static_cast<unsigned>(keep * 256.0f + 0.5f);       // 0..256
    for (size_t i = 0; i < px_.size(); ++i) px_[i] = static_cast<uint8_t>((px_[i] * k) >> 8);
}

void Canvas::blend(int x, int y, Color c, float coverage) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    const float a = clamp01(c.a * coverage);
    if (a <= 0.0f) return;
    uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    const float inv = 1.0f - a;
    p[0] = static_cast<uint8_t>(std::min(255.0f, c.r * a * 255.0f + p[0] * inv + 0.5f));
    p[1] = static_cast<uint8_t>(std::min(255.0f, c.g * a * 255.0f + p[1] * inv + 0.5f));
    p[2] = static_cast<uint8_t>(std::min(255.0f, c.b * a * 255.0f + p[2] * inv + 0.5f));
    p[3] = static_cast<uint8_t>(std::min(255.0f, a * 255.0f + p[3] * inv + 0.5f));
}

void Canvas::fillRoundRectGradient(float x, float y, float w, float h, float r, Color top, Color bottom) {
    const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 1), x1 = std::min(w_, static_cast<int>(std::ceil(x + w)) + 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 1), y1 = std::min(h_, static_cast<int>(std::ceil(y + h)) + 1);
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    r = std::min(r, std::min(hx, hy));
    for (int py = y0; py < y1; ++py) {
        const Color col = mix(top, bottom, clamp01((py + 0.5f - y) / h));
        for (int px = x0; px < x1; ++px) {
            const float d = sdRoundRect(px + 0.5f, py + 0.5f, cx, cy, hx, hy, r);
            const float cov = clamp01(0.5f - d);
            if (cov > 0) blend(px, py, col, cov);
        }
    }
}
void Canvas::fillRoundRect(float x, float y, float w, float h, float r, Color c) { fillRoundRectGradient(x, y, w, h, r, c, c); }

void Canvas::strokeRoundRect(float x, float y, float w, float h, float r, float width, Color c) {
    const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 1), x1 = std::min(w_, static_cast<int>(std::ceil(x + w)) + 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 1), y1 = std::min(h_, static_cast<int>(std::ceil(y + h)) + 1);
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    r = std::min(r, std::min(hx, hy));
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const float d = sdRoundRect(px + 0.5f, py + 0.5f, cx, cy, hx, hy, r);
            const float cov = clamp01(0.5f - d) * clamp01(0.5f + d + width);   // the ring just inside the edge
            if (cov > 0) blend(px, py, c, cov);
        }
}

void Canvas::glowRoundRect(float x, float y, float w, float h, float r, float spread, Color c) {
    const int pad = static_cast<int>(spread * 2.2f);
    const int x0 = std::max(0, static_cast<int>(x) - pad), x1 = std::min(w_, static_cast<int>(x + w) + pad);
    const int y0 = std::max(0, static_cast<int>(y) - pad), y1 = std::min(h_, static_cast<int>(y + h) + pad);
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    r = std::min(r, std::min(hx, hy));
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const float d = sdRoundRect(px + 0.5f, py + 0.5f, cx, cy, hx, hy, r);
            if (d <= -1.0f) continue;
            const float fall = std::exp(-(std::max(d, 0.0f) * std::max(d, 0.0f)) / (spread * spread * 0.6f));
            blend(px, py, c, fall * clamp01(d + 1.0f));
        }
}

void Canvas::fillCircle(float cx, float cy, float r, Color c) {
    const int x0 = std::max(0, static_cast<int>(cx - r) - 1), x1 = std::min(w_, static_cast<int>(cx + r) + 2);
    const int y0 = std::max(0, static_cast<int>(cy - r) - 1), y1 = std::min(h_, static_cast<int>(cy + r) + 2);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const float d = std::hypot(px + 0.5f - cx, py + 0.5f - cy) - r;
            const float cov = clamp01(0.5f - d);
            if (cov > 0) blend(px, py, c, cov);
        }
}

void Canvas::strokeCircle(float cx, float cy, float r, float width, Color c) {
    const int x0 = std::max(0, static_cast<int>(cx - r) - 2), x1 = std::min(w_, static_cast<int>(cx + r) + 3);
    const int y0 = std::max(0, static_cast<int>(cy - r) - 2), y1 = std::min(h_, static_cast<int>(cy + r) + 3);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const float d = std::fabs(std::hypot(px + 0.5f - cx, py + 0.5f - cy) - r) - width / 2;
            const float cov = clamp01(0.5f - d);
            if (cov > 0) blend(px, py, c, cov);
        }
}

void Canvas::line(float ax, float ay, float bx, float by, float width, Color c) {
    const float pad = width / 2 + 2;
    const int x0 = std::max(0, static_cast<int>(std::min(ax, bx) - pad)), x1 = std::min(w_, static_cast<int>(std::max(ax, bx) + pad) + 1);
    const int y0 = std::max(0, static_cast<int>(std::min(ay, by) - pad)), y1 = std::min(h_, static_cast<int>(std::max(ay, by) + pad) + 1);
    const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
            const float t = len2 > 0 ? clamp01((qx * dx + qy * dy) / len2) : 0.0f;
            const float d = std::hypot(qx - dx * t, qy - dy * t) - width / 2;
            const float cov = clamp01(0.5f - d);
            if (cov > 0) blend(px, py, c, cov);
        }
}

void Canvas::fillPolygon(const std::vector<float>& xy, Color c) {
    const size_t n = xy.size() / 2;
    if (n < 3) return;
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (size_t i = 0; i < n; ++i) { minx = std::min(minx, xy[2 * i]); maxx = std::max(maxx, xy[2 * i]); miny = std::min(miny, xy[2 * i + 1]); maxy = std::max(maxy, xy[2 * i + 1]); }
    const int x0 = std::max(0, static_cast<int>(minx) - 1), x1 = std::min(w_, static_cast<int>(maxx) + 2);
    const int y0 = std::max(0, static_cast<int>(miny) - 1), y1 = std::min(h_, static_cast<int>(maxy) + 2);
    const int S = 4;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            int inside = 0;
            for (int sy = 0; sy < S; ++sy)
                for (int sx = 0; sx < S; ++sx) {
                    const float fx = px + (sx + 0.5f) / S, fy = py + (sy + 0.5f) / S;
                    bool in = false;
                    for (size_t i = 0, j = n - 1; i < n; j = i++) {
                        const float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
                        if (((yi > fy) != (yj > fy)) && (fx < (xj - xi) * (fy - yi) / (yj - yi) + xi)) in = !in;
                    }
                    inside += in;
                }
            if (inside) blend(px, py, c, inside / float(S * S));
        }
}

float Canvas::textWidth(const Font& f, const std::string& s) const {
    float w = 0;
    for (unsigned char ch : s) if (ch >= 32 && ch <= 126) w += f.glyphs[ch - 32].adv;
    return w;
}

void Canvas::text(const Font& f, float x, float baselineY, const std::string& s, Color c) {
    float pen = x;
    for (unsigned char ch : s) {
        if (ch < 32 || ch > 126) continue;
        const Glyph& g = f.glyphs[ch - 32];
        const int gx = static_cast<int>(std::lround(pen)) + g.xoff, gy = static_cast<int>(std::lround(baselineY)) + g.yoff;
        for (int row = 0; row < g.h; ++row)
            for (int col = 0; col < g.w; ++col) {
                const unsigned char a = f.alpha[g.offset + row * g.w + col];
                if (a) blend(gx + col, gy + row, c, a / 255.0f);
            }
        pen += g.adv;
    }
}
void Canvas::textCentered(const Font& f, float cx, float baselineY, const std::string& s, Color c) { text(f, cx - textWidth(f, s) / 2, baselineY, s, c); }

// ---------------------------------------------------------------------------------- icons
namespace {
const float kPi = 3.14159265f;

void arcLine(Canvas& c, float cx, float cy, float r, float a0, float a1, float w, Color col) {
    const int steps = 14;
    float px = cx + r * std::cos(a0), py = cy + r * std::sin(a0);
    for (int i = 1; i <= steps; ++i) {
        const float a = a0 + (a1 - a0) * i / steps;
        const float nx = cx + r * std::cos(a), ny = cy + r * std::sin(a);
        c.line(px, py, nx, ny, w, col);
        px = nx; py = ny;
    }
}

// Small pictures for the sidebar (about 26 px). Drawn with simple shapes.
void drawIcon(Canvas& c, int id, float x, float y, Color col, Color bg) {
    const float d2r = kPi / 180.0f;
    switch (id) {
    case 0: {  // gear
        for (int i = 0; i < 8; ++i) { const float a = i * kPi / 4; c.line(x + 8 * std::cos(a), y + 8 * std::sin(a), x + 12 * std::cos(a), y + 12 * std::sin(a), 4.5f, col); }
        c.strokeCircle(x, y, 8, 4, col);
        break; }
    case 1: {  // star
        std::vector<float> p;
        for (int i = 0; i < 10; ++i) { const float r = (i % 2 == 0) ? 13.0f : 5.5f, a = -kPi / 2 + i * kPi / 5; p.push_back(x + r * std::cos(a)); p.push_back(y + r * std::sin(a)); }
        c.fillPolygon(p, col);
        break; }
    case 2: {  // pulse
        const float pts[][2] = {{-13, 0}, {-6, 0}, {-3, -9}, {1, 9}, {4, -4}, {6, 0}, {13, 0}};
        for (int i = 0; i < 6; ++i) c.line(x + pts[i][0], y + pts[i][1], x + pts[i + 1][0], y + pts[i + 1][1], 2.6f, col);
        break; }
    case 3: {  // runner
        c.fillCircle(x + 4, y - 10, 3.6f, col);
        c.line(x + 2, y - 5, x - 1, y + 3, 3.4f, col);
        c.line(x + 2, y - 4, x + 9, y - 1, 2.8f, col);
        c.line(x + 2, y - 4, x - 6, y - 2, 2.8f, col);
        c.line(x - 1, y + 3, x + 5, y + 12, 3.0f, col);
        c.line(x - 1, y + 3, x - 8, y + 10, 3.0f, col);
        break; }
    case 4: {  // basketball
        c.strokeCircle(x, y, 11.5f, 2.4f, col);
        c.line(x, y - 11, x, y + 11, 2.2f, col);
        c.line(x - 11, y, x + 11, y, 2.2f, col);
        arcLine(c, x - 15, y, 12, -50 * d2r, 50 * d2r, 2.2f, col);
        arcLine(c, x + 15, y, 12, 130 * d2r, 230 * d2r, 2.2f, col);
        break; }
    case 5: {  // baseball
        c.strokeCircle(x, y, 11.5f, 2.4f, col);
        arcLine(c, x - 16, y, 13, -40 * d2r, 40 * d2r, 2.0f, col);
        arcLine(c, x + 16, y, 13, 140 * d2r, 220 * d2r, 2.0f, col);
        break; }
    case 6: {  // soccer
        c.strokeCircle(x, y, 11.5f, 2.4f, col);
        std::vector<float> p;
        for (int i = 0; i < 5; ++i) { const float a = -kPi / 2 + i * 2 * kPi / 5; p.push_back(x + 4.6f * std::cos(a)); p.push_back(y + 4.6f * std::sin(a)); c.line(x + 4.6f * std::cos(a), y + 4.6f * std::sin(a), x + 10 * std::cos(a), y + 10 * std::sin(a), 2.0f, col); }
        c.fillPolygon(p, col);
        break; }
    case 7: {  // football
        std::vector<float> p; const float ca = std::cos(-35 * d2r), sa = std::sin(-35 * d2r);
        for (int i = 0; i < 24; ++i) { const float t = i * 2 * kPi / 24, ex = 13 * std::cos(t), ey = 7.5f * std::sin(t); p.push_back(x + ex * ca - ey * sa); p.push_back(y + ex * sa + ey * ca); }
        c.fillPolygon(p, col);
        c.line(x - 5 * ca, y - 5 * sa, x + 5 * ca, y + 5 * sa, 1.8f, bg);
        for (int k = -1; k <= 1; ++k) c.line(x + k * 3.2f * ca + 2.5f * sa, y + k * 3.2f * sa - 2.5f * ca, x + k * 3.2f * ca - 2.5f * sa, y + k * 3.2f * sa + 2.5f * ca, 1.6f, bg);
        break; }
    case 8: {  // paintball marker
        c.fillRoundRect(x - 13, y - 5, 22, 7, 2.5f, col);
        c.fillRoundRect(x - 13, y - 3, 6, 11, 2.0f, col);
        c.fillRoundRect(x - 3, y + 1, 6, 11, 2.0f, col);
        c.fillCircle(x + 1, y - 9, 4.2f, col);
        c.line(x + 9, y - 2, x + 14, y - 2, 3.0f, col);
        break; }
    case 9: {  // boxing glove
        c.fillRoundRect(x - 9, y - 12, 19, 19, 8.0f, col);
        c.fillRoundRect(x - 7, y + 5, 14, 8, 2.5f, col);
        c.fillCircle(x - 9, y - 1, 4.0f, col);
        c.line(x - 6, y + 7, x + 7, y + 7, 1.6f, bg);
        break; }
    default: {  // troll face
        c.strokeCircle(x, y, 11.5f, 2.4f, col);
        c.fillCircle(x - 4.5f, y - 3.5f, 1.9f, col);
        c.fillCircle(x + 4.5f, y - 3.5f, 1.9f, col);
        arcLine(c, x, y - 1, 8.0f, 25 * d2r, 155 * d2r, 2.4f, col);
        break; }
    }
}
}  // namespace

// ---------------------------------------------------------------------------------- the menu picture
void drawPanel(Canvas& c, const PanelState& s, std::vector<HitRect>* hits) {
    if (hits) hits->clear();
    const ColorChoice& chosen = colorChoice(s.colorIndex);
    const Color accent = rgba(chosen.rgb[0], chosen.rgb[1], chosen.rgb[2]);
    const Color accentHi = mix(accent, rgba(255, 255, 255), 0.25f);
    const Color accentLo = scaleC(accent, 0.55f);
    const Color white = rgba(244, 244, 250), grey = rgba(150, 150, 172), dimGrey = rgba(104, 104, 124);
    const Color card = rgba(27, 27, 37), cardEdge = rgba(44, 44, 58), plate = rgba(20, 20, 28);

    c.clear(rgba(0, 0, 0, 0));
    // outer glass panel
    c.glowRoundRect(14, 14, 996, 740, 38, 12, withA(accent, 0.35f));
    c.fillRoundRectGradient(14, 14, 996, 740, 38, rgba(25, 25, 34), rgba(13, 13, 19));
    c.strokeRoundRect(14, 14, 996, 740, 38, 2.5f, withA(accentHi, 0.55f));

    // header
    c.fillRoundRect(32, 30, 960, 80, 22, plate);
    c.strokeRoundRect(32, 30, 960, 80, 22, 1.5f, rgba(40, 40, 54));
    c.text(kFontTitle, 58, 87, "Timmyzstuff", withA(accent, 0.45f));   // soft glow copy
    c.text(kFontTitle, 56, 85, "Timmyzstuff", white);
    // X button
    {
        const float bx = 926, by = 44, bs = 52;
        const bool hv = s.hover == HIT_CLOSE;
        c.fillRoundRectGradient(bx, by, bs, bs, 15, hv ? accent : rgba(40, 40, 54), hv ? accentLo : rgba(28, 28, 38));
        c.strokeRoundRect(bx, by, bs, bs, 15, 1.6f, hv ? accentHi : rgba(64, 64, 82));
        c.line(bx + 17, by + 17, bx + bs - 17, by + bs - 17, 3.4f, white);
        c.line(bx + bs - 17, by + 17, bx + 17, by + bs - 17, 3.4f, white);
        if (hits) hits->push_back({HIT_CLOSE, bx, by, bs, bs});
    }

    // sidebar
    c.fillRoundRect(32, 124, 246, 622, 24, plate);
    c.strokeRoundRect(32, 124, 246, 622, 24, 1.5f, rgba(40, 40, 54));
    for (int i = 0; i < kTabCount; ++i) {
        const float ix = 44, iy = 136 + i * 54.0f, iw = 222, ih = 46;
        const bool sel = (i == s.tab), hv = (s.hover == HIT_TAB0 + i);
        if (sel) {
            c.glowRoundRect(ix, iy, iw, ih, 14, 8, withA(accent, 0.45f));
            c.fillRoundRectGradient(ix, iy, iw, ih, 14, accent, accentLo);
            c.strokeRoundRect(ix, iy, iw, ih, 14, 1.8f, withA(accentHi, 0.9f));
        } else {
            c.fillRoundRectGradient(ix, iy, iw, ih, 14, hv ? rgba(40, 40, 54) : rgba(30, 30, 41), hv ? rgba(32, 32, 44) : rgba(24, 24, 33));
            c.strokeRoundRect(ix, iy, iw, ih, 14, 1.4f, hv ? withA(accentHi, 0.8f) : rgba(42, 42, 56));
        }
        drawIcon(c, i, ix + 30, iy + ih / 2, sel ? white : rgba(205, 205, 220), sel ? accentLo : rgba(30, 30, 41));
        c.text(kFontLabel, ix + 60, iy + ih / 2 + 8, tabName(i), sel ? white : rgba(222, 222, 234));
        if (hits) hits->push_back({HIT_TAB0 + i, ix, iy, iw, ih});
    }

    // content card
    const float cx0 = 296, cy0 = 124, cw = 696, ch = 500;
    c.fillRoundRect(cx0, cy0, cw, ch, 24, plate);
    c.strokeRoundRect(cx0, cy0, cw, ch, 24, 1.5f, rgba(40, 40, 54));
    c.text(kFontHead, cx0 + 30, cy0 + 56, tabName(s.tab), white);

    const float rx = cx0 + 24, rw = cw - 48;
    if (s.tab == 0) {
        float y = cy0 + 70;
        // --- Sound effects
        {
            const float ry = y, rh = 52;
            const bool hv = s.hover == HIT_SOUND;
            c.fillRoundRect(rx, ry, rw, rh, 16, card);
            c.strokeRoundRect(rx, ry, rw, rh, 16, 1.4f, hv ? withA(accentHi, 0.8f) : cardEdge);
            c.text(kFontLabel, rx + 22, ry + rh / 2 + 8, "Sound effects", white);
            const float tw = 66, th = 32, tx = rx + rw - tw - 22, ty = ry + (rh - th) / 2;
            c.fillRoundRect(tx, ty, tw, th, th / 2, s.sound ? accent : rgba(58, 58, 74));
            c.strokeRoundRect(tx, ty, tw, th, th / 2, 1.4f, s.sound ? accentHi : rgba(84, 84, 104));
            c.fillCircle(s.sound ? tx + tw - th / 2 : tx + th / 2, ty + th / 2, th / 2 - 4, rgba(250, 250, 255));
            if (hits) hits->push_back({HIT_SOUND, rx, ry, rw, rh});
            y += rh + 8;
        }
        // --- Menu color: a list of colours, click one
        {
            const float ry = y, rh = 120;
            c.fillRoundRect(rx, ry, rw, rh, 16, card);
            c.strokeRoundRect(rx, ry, rw, rh, 16, 1.4f, cardEdge);
            c.text(kFontLabel, rx + 22, ry + 30, "Menu color", white);
            c.text(kFontTiny, rx + 22 + c.textWidth(kFontLabel, "Menu color") + 14, ry + 29, "click one", dimGrey);
            const float pad = 14, gap = 8, pw = (rw - 2 * pad - 4 * gap) / 5, ph = 32, py0 = ry + 42;
            for (int i = 0; i < kColorCount; ++i) {
                const ColorChoice& cc = colorChoice(i);
                const Color col = rgba(cc.rgb[0], cc.rgb[1], cc.rgb[2]);
                const float px = rx + pad + (i % 5) * (pw + gap), py = py0 + (i / 5) * (ph + gap);
                const bool sel = (i == s.colorIndex), hv = (s.hover == HIT_COLOR0 + i);
                if (sel) {
                    c.glowRoundRect(px, py, pw, ph, 12, 7, withA(col, 0.45f));
                    c.fillRoundRectGradient(px, py, pw, ph, 12, scaleC(col, 0.55f), scaleC(col, 0.32f));
                    c.strokeRoundRect(px, py, pw, ph, 12, 2.2f, mix(col, rgba(255, 255, 255), 0.35f));
                } else {
                    c.fillRoundRectGradient(px, py, pw, ph, 12, hv ? rgba(48, 48, 64) : rgba(36, 36, 49), hv ? rgba(36, 36, 50) : rgba(27, 27, 37));
                    c.strokeRoundRect(px, py, pw, ph, 12, 1.4f, hv ? mix(col, rgba(255, 255, 255), 0.2f) : rgba(56, 56, 74));
                }
                c.fillCircle(px + 18, py + ph / 2, 8, col);
                c.strokeCircle(px + 18, py + ph / 2, 8, 1.2f, rgba(255, 255, 255, 0.55f));
                c.text(kFontSmall, px + 34, py + ph / 2 + 6, cc.name, sel ? white : rgba(222, 222, 234));
                if (hits) hits->push_back({HIT_COLOR0 + i, px, py, pw, ph});
            }
            y += rh + 8;
        }
        // --- three sliders: size, transparency, distance
        struct SliderRow { int id; const char* label; bool onRelease; };
        static const SliderRow rows[3] = {{HIT_SLIDER_SIZE, "Menu size", true}, {HIT_SLIDER_ALPHA, "Menu transparency", false}, {HIT_SLIDER_DIST, "Menu distance", true}};
        for (const SliderRow& row : rows) {
            const float ry = y, rh = 52;
            c.fillRoundRect(rx, ry, rw, rh, 16, card);
            c.strokeRoundRect(rx, ry, rw, rh, 16, 1.4f, cardEdge);
            c.text(kFontLabel, rx + 22, ry + rh / 2 + 8, row.label, white);
            float lo, hi, step; sliderRange(row.id, &lo, &hi, &step);
            PanelState tmp = s;
            const float v = sliderValue(tmp, row.id);
            char buf[24];
            if (row.id == HIT_SLIDER_SIZE) std::snprintf(buf, sizeof buf, "%.2fx", v);
            else if (row.id == HIT_SLIDER_ALPHA) std::snprintf(buf, sizeof buf, "%.2f", v);
            else std::snprintf(buf, sizeof buf, "%.2f m", v);
            const bool drag = s.dragSlider == row.id;
            const float vy = (drag && row.onRelease) ? ry + 24 : ry + rh / 2 + 8;
            c.text(kFontLabel, rx + rw - 22 - c.textWidth(kFontLabel, buf), vy, buf, accentHi);
            if (drag && row.onRelease) c.text(kFontTiny, rx + rw - 22 - c.textWidth(kFontTiny, "applies when you let go"), ry + 45, "applies when you let go", dimGrey);
            const float tx0 = rx + 284, tx1 = rx + rw - 140, ty = ry + rh / 2;
            const float t = clamp01((v - lo) / (hi - lo)), kx = tx0 + (tx1 - tx0) * t;
            c.fillRoundRect(tx0, ty - 5, tx1 - tx0, 10, 5, rgba(14, 14, 20));
            c.strokeRoundRect(tx0, ty - 5, tx1 - tx0, 10, 5, 1.2f, rgba(52, 52, 68));
            c.fillRoundRectGradient(tx0, ty - 5, std::max(10.0f, kx - tx0), 10, 5, accentHi, accent);
            const bool hot = drag || s.hover == row.id;
            if (hot) c.glowRoundRect(kx - 10, ty - 10, 20, 20, 10, 10, withA(accentHi, 0.7f));
            c.fillCircle(kx, ty, hot ? 14.5f : 12.5f, rgba(250, 250, 255));
            c.strokeCircle(kx, ty, hot ? 14.5f : 12.5f, 3, hot ? accentHi : accent);
            if (hits) hits->push_back({row.id, tx0 - 16, ry + 4, tx1 - tx0 + 32, rh - 8});
            y += rh + 8;
        }
        // --- Test button
        {
            const float ry = y, rh = 46;
            const bool hv = s.hover == HIT_TEST;
            c.fillRoundRectGradient(rx, ry, rw, rh, 16, hv ? rgba(48, 48, 64) : rgba(38, 38, 52), hv ? rgba(34, 34, 46) : rgba(28, 28, 38));
            c.strokeRoundRect(rx, ry, rw, rh, 16, 1.8f, hv ? accentHi : withA(accentHi, 0.55f));
            c.textCentered(kFontLabel, rx + rw / 2, ry + rh / 2 + 8, "Test button", white);
            if (s.testClicks > 0) {
                char buf[32]; std::snprintf(buf, sizeof buf, "Pressed %d", s.testClicks);
                c.text(kFontSmall, rx + rw - 22 - c.textWidth(kFontSmall, buf), ry + rh / 2 + 7, buf, accentHi);
            }
            if (hits) hits->push_back({HIT_TEST, rx, ry, rw, rh});
        }
    } else {
        const float ry = cy0 + 78, rh = 120;
        c.fillRoundRect(rx, ry, rw, rh, 18, card);
        c.strokeRoundRect(rx, ry, rw, rh, 18, 1.4f, cardEdge);
        c.text(kFontLabel, rx + 26, ry + 50, "Coming soon", white);
        c.text(kFontSmall, rx + 26, ry + 86, "Nothing here yet. This page is a placeholder.", grey);
    }

    // footer
    {
        const float fy = 638, fh = 108;
        c.fillRoundRect(cx0, fy, cw, fh, 24, plate);
        c.strokeRoundRect(cx0, fy, cw, fh, 24, 1.5f, rgba(40, 40, 54));
        // headset icon
        const float hx = cx0 + 56, hy = fy + fh / 2;
        c.strokeRoundRect(hx - 22, hy - 13, 44, 26, 10, 3.0f, grey);
        c.line(hx - 22, hy - 2, hx - 28, hy + 8, 3.0f, grey);
        c.line(hx + 22, hy - 2, hx + 28, hy + 8, 3.0f, grey);
        c.text(kFontLabel, cx0 + 110, fy + 44, "Hold both triggers + A", rgba(224, 224, 236));
        c.text(kFontLabel, cx0 + 110, fy + 84, "Click B to exit menu", rgba(224, 224, 236));
        const float ax = cx0 + cw - 52;
        c.strokeCircle(ax, fy + 36, 17, 2.6f, accentHi);   c.textCentered(kFontLabel, ax, fy + 44, "A", white);
        c.strokeCircle(ax, fy + 76, 17, 2.6f, accentHi);   c.textCentered(kFontLabel, ax, fy + 84, "B", white);
    }
}

// ---------------------------------------------------------------------------------- pointer dot
void drawCursor(Canvas& c, float x, float y, const PanelState& s, bool pressed) {
    const ColorChoice& cc = colorChoice(s.colorIndex);
    const Color accent = rgba(cc.rgb[0], cc.rgb[1], cc.rgb[2]);
    const Color accentHi = mix(accent, rgba(255, 255, 255), 0.35f);
    c.fillCircle(x + 1.5f, y + 2.0f, 13, rgba(0, 0, 0, 0.35f));                 // soft shadow
    c.fillCircle(x, y, pressed ? 9.0f : 11.0f, withA(accentHi, 0.28f));
    c.strokeCircle(x, y, pressed ? 9.0f : 11.0f, 3.0f, rgba(255, 255, 255));
    c.strokeCircle(x, y, pressed ? 9.0f : 11.0f, 1.2f, accent);
    c.fillCircle(x, y, 3.6f, rgba(255, 255, 255));
}

// ---------------------------------------------------------------------------------- saved settings
std::string settingsToText(const PanelState& s) {
    char b[200];
    std::snprintf(b, sizeof b, "sound=%d\ncolor=%d\nscale=%.2f\ntransparency=%.2f\ndistance=%.2f\n", s.sound ? 1 : 0, s.colorIndex, s.scale, s.transparency, s.distance);
    return b;
}
bool settingsFromText(const std::string& text, PanelState& s) {
    bool any = false; size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos); if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos); pos = end + 1;
        const size_t eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        char* e = nullptr;
        if (key == "sound" && (val == "0" || val == "1")) { s.sound = (val == "1"); any = true; }
        else if (key == "color") { const long v = std::strtol(val.c_str(), &e, 10); if (e != val.c_str() && v >= 0 && v < kColorCount) { s.colorIndex = static_cast<int>(v); any = true; } }
        else if (key == "scale") { const float f = std::strtof(val.c_str(), &e); if (e != val.c_str() && f >= kScaleMin - 1e-4f && f <= kScaleMax + 1e-4f) { s.scale = snapSlider(HIT_SLIDER_SIZE, f); any = true; } }
        else if (key == "transparency") { const float f = std::strtof(val.c_str(), &e); if (e != val.c_str() && f >= kAlphaMin - 1e-4f && f <= kAlphaMax + 1e-4f) { s.transparency = snapSlider(HIT_SLIDER_ALPHA, f); any = true; } }
        else if (key == "distance") { const float f = std::strtof(val.c_str(), &e); if (e != val.c_str() && f >= kDistMin - 1e-4f && f <= kDistMax + 1e-4f) { s.distance = snapSlider(HIT_SLIDER_DIST, f); any = true; } }
    }
    return any;
}

}  // namespace tzpanel
