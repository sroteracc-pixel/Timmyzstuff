// panel.h - draws the Timmyzstuff menu picture with plain CPU code (no graphics library).
//
// WHAT THIS IS (plain words)
//   The menu is just a picture (1024 x 768 pixels). This file knows how to paint that picture:
//   rounded boxes, glows, text, small icons. It has NO Android or VR code in it, so it can be
//   tested on a normal computer (it writes the picture to a file and you can look at it).
//   Another file (overlay.cpp) shows the picture inside the headset.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace tzpanel {

const int kWidth = 1024;
const int kHeight = 768;

struct Glyph { unsigned int offset; unsigned char w, h; signed char xoff, yoff; unsigned char adv; };
struct Font { int size; int ascent; int lineHeight; const Glyph* glyphs; const unsigned char* alpha; };
extern const Font kFontTitle, kFontHead, kFontLabel, kFontSmall, kFontTiny;

struct Color { float r, g, b, a; };
inline Color rgba(int r, int g, int b, float a = 1.0f) { return Color{r / 255.0f, g / 255.0f, b / 255.0f, a}; }

// A picture. Pixels are RGBA, 8 bits each, "premultiplied" (colour already multiplied by alpha).
class Canvas {
public:
    Canvas(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    const uint8_t* data() const { return px_.data(); }
    void clear(Color c);

    void fillRoundRect(float x, float y, float w, float h, float r, Color c);
    void fillRoundRectGradient(float x, float y, float w, float h, float r, Color top, Color bottom);
    void strokeRoundRect(float x, float y, float w, float h, float r, float width, Color c);
    void glowRoundRect(float x, float y, float w, float h, float r, float spread, Color c);   // soft light just outside the box
    void fillCircle(float cx, float cy, float r, Color c);
    void strokeCircle(float cx, float cy, float r, float width, Color c);
    void line(float x0, float y0, float x1, float y1, float width, Color c);
    void fillPolygon(const std::vector<float>& xy, Color c);                                 // x0,y0,x1,y1,...

    float textWidth(const Font& f, const std::string& s) const;
    void text(const Font& f, float x, float baselineY, const std::string& s, Color c);
    void textCentered(const Font& f, float cx, float baselineY, const std::string& s, Color c);

private:
    void blend(int x, int y, Color c, float coverage);
    int w_, h_;
    std::vector<uint8_t> px_;
};

// Everything the picture depends on.
struct PanelState {
    int tab = 0;                    // 0 Settings ... 10 Troll
    bool sound = true;              // "Sound effects" switch
    int rgb[3] = {139, 92, 246};    // menu colour (default: purple)
    float scale = 1.0f;             // menu size, 0.75 .. 1.5
    int testClicks = 0;             // how many times Test button was pressed
    int hover = -1;                 // which control the pointer is over (a HitId, or -1)
    std::string rgbText = "139, 92, 246";   // what is typed in the colour box
    bool editingRgb = false;        // the colour box is being typed into
};

// Clickable areas (in picture pixels), so a pointer can later find what it points at.
enum HitId {
    HIT_CLOSE = 1,
    HIT_TAB0 = 10,                  // 10 .. 20 = the 11 sidebar entries
    HIT_SOUND = 30,
    HIT_RGBFIELD = 31,
    HIT_APPLY = 32,
    HIT_SLIDER = 33,
    HIT_TEST = 34,
};
struct HitRect { int id; float x, y, w, h; };

const int kTabCount = 11;
const char* tabName(int i);

// Paints the whole menu. `hits` (optional) receives the clickable areas.
void drawPanel(Canvas& c, const PanelState& s, std::vector<HitRect>* hits);

// Parses "255, 0, 0" (also "255 0 0" or "255,0,0"). Returns false (and leaves rgb alone) if it is not 3 numbers 0-255.
bool parseRgb(const std::string& text, int rgb[3]);

}  // namespace tzpanel
