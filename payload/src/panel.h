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
    void fadeAll(float keep);                    // keeps only `keep` (0..1) of the picture's opacity, e.g. 0.25 = 75% see-through

    float textWidth(const Font& f, const std::string& s) const;
    void text(const Font& f, float x, float baselineY, const std::string& s, Color c);
    void textCentered(const Font& f, float cx, float baselineY, const std::string& s, Color c);

private:
    void blend(int x, int y, Color c, float coverage);
    int w_, h_;
    std::vector<uint8_t> px_;
};

// The colours you can pick for the menu. Number 0 ("Default") is the purple the menu started with.
struct ColorChoice { const char* name; int rgb[3]; };
const int kColorCount = 10;
const ColorChoice& colorChoice(int i);

// Slider ranges
const float kScaleMin = 0.75f, kScaleMax = 1.5f, kScaleStep = 0.05f;               // menu size
const float kAlphaMin = 0.0f,  kAlphaMax = 0.25f, kAlphaStep = 0.05f;              // menu transparency (0.25 = 25% see-through)
const float kDistMin = 0.6f,   kDistMax = 2.0f,  kDistStep = 0.05f;                // menu distance in metres
const float kDistDefault = 1.15f;
// Movement sliders (page "Movement")
const float kSpeedMin = 1.1f, kSpeedMax = 5.0f, kSpeedStep = 0.1f;                 // Speed Boost, shown as 1.1x .. 5.0x
const float kJumpMin = 1.1f,  kJumpMax = 5.0f,  kJumpStep = 0.1f;                  // Jump Boost (jump HEIGHT), 1.1x .. 5.0x
const float kGravMin = 0.0f,  kGravMax = 90.0f, kGravStep = 5.0f;                  // Low / High Gravity in percent
// Aimbot slider (page "Basketball"): max shot distance. These three numbers are the same as kCapMinM / kCapMaxM / kCapStepM in aimbot.h (a test checks it).
const float kAimMin = 5.0f, kAimMax = 50.0f, kAimStep = 1.0f;                      // 5 m ... 50 m, 1 m per step; 50 is shown as "Unlimited"
const float kAimDefault = 50.0f;
std::string aimCapText(float capM);                                                // "Unlimited" or "23 m"

// Everything the picture depends on.
struct PanelState {
    int tab = 0;                    // 0 Settings ... 10 Troll
    bool sound = true;              // "Sound effects" switch
    int colorIndex = 0;             // which colour in the list (0 = Default purple)
    float scale = 1.0f;             // menu size, 0.75 .. 1.5
    float transparency = 0.0f;      // 0 = solid ... 0.25 = 25% see-through
    float distance = kDistDefault;  // how far in front of you the menu floats (metres)
    int testClicks = 0;             // how many times Test button was pressed
    int hover = -1;                 // which control the pointer is over (a HitId, or -1)
    int dragSlider = 0;             // HitId of the slider being dragged (0 = none)

    // ---- Movement page. The switches are NEVER saved: every launch starts with all of them off. ----
    bool speedOn = false;           // Speed Boost
    bool jumpOn = false;            // Jump Boost
    int gravityMode = 0;            // 0 = off, 1 = Low Gravity, 2 = High Gravity (one value, so two can never be on together)
    float speedMul = kSpeedMin;     // 1.1 .. 5.0
    float jumpMul = kJumpMin;       // 1.1 .. 5.0 (times the normal jump HEIGHT)
    float lowGravPct = 0.0f;        // 0 .. 90: 90 means only 10% of normal gravity is left
    float highGravPct = 0.0f;       // 0 .. 90: 90 means 190% of normal gravity
    int linkState = 0;              // 0 = waiting (no switch on yet), 1 = connected to the game's movement code, 2 = looking for the player, 3 = failed
    int scanState = 0;              // game-code scan: 0 not run, 1 running, 2 done, 3 failed
    int scanMatches = 0;            // how many interesting classes the scan wrote down

    // ---- Basketball page (Aimbot). The switch is NEVER saved: every launch starts with it off. The distance IS saved. ----
    bool aimOn = false;             // Aimbot switch (Direct: the ball goes straight into the hoop you aim at)
    bool aimBank = false;           // stage D9: "Aimbot Bank" switch (the ball hits the front of the backboard first and drops in). Never saved. Only one of aimOn / aimBank is ever on.
    float aimCapM = kAimDefault;    // max shot distance, 5 .. 50 m (50 = Unlimited)
    bool aimHoldY = true;           // "Hold Y to aim": the Aimbot acts only while the Y button is held as you let go of the ball. Starts ON; IS saved.
    int aimLinkState = 0;           // stage D8: 0 = off (switch off), 1 = connected to the game's ball, 2 = looking for your ball control, 3 = failed
    std::string aimHeadline;        // short text from the game part: what it is doing right now (empty = the menu picks a default text)
    std::string aimLastShot;        // short text: what happened to your last throw (empty = no throw seen yet)
};

// Clickable areas (in picture pixels), so a pointer can find what it points at.
enum HitId {
    HIT_CLOSE = 1,
    HIT_TAB0 = 10,                  // 10 .. 20 = the 11 sidebar entries
    HIT_SOUND = 30,
    HIT_SLIDER_SIZE = 33,
    HIT_TEST = 34,
    HIT_SLIDER_ALPHA = 35,
    HIT_SLIDER_DIST = 36,
    HIT_SLIDER_SPEED = 37,
    HIT_SLIDER_JUMP = 38,
    HIT_SLIDER_LOWGRAV = 39,
    HIT_SLIDER_HIGHGRAV = 40,
    HIT_TOGGLE_SPEED = 41,
    HIT_TOGGLE_JUMP = 42,
    HIT_TOGGLE_LOWGRAV = 43,
    HIT_TOGGLE_HIGHGRAV = 44,
    HIT_SCAN = 45,                  // "Scan game code" button on the Movement page
    HIT_SLIDER_AIMCAP = 46,         // Basketball page: max shot distance
    HIT_TOGGLE_AIM = 47,            // Basketball page: Aimbot switch
    HIT_SCAN_SHOT = 48,             // Basketball page: "Scan ball and hoops" button
    HIT_TOGGLE_AIMY = 49,           // Basketball page: "Hold Y to aim" switch
    HIT_TOGGLE_AIMBANK = 50,        // Basketball page: "Aimbot Bank" switch (stage D9)
    HIT_COLOR0 = 60,                // 60 .. 69 = the 10 colours
};
struct HitRect { int id; float x, y, w, h; };

const int kTabCount = 11;
const int kTabMovement = 3;      // index of the "Movement" page
const int kTabBasketball = 4;    // index of the "Basketball" page (the Aimbot lives here)
const char* tabName(int i);

// Paints the whole menu. `hits` (optional) receives the clickable areas.
void drawPanel(Canvas& c, const PanelState& s, std::vector<HitRect>* hits);

// The little pointer dot drawn on top of the menu (drawn separately so moving it does not repaint the menu).
void drawCursor(Canvas& c, float x, float y, const PanelState& s, bool pressed);

// ---- sliders: one place that knows each slider's range, so the picture and the pointer agree ----
bool sliderRange(int hitId, float* lo, float* hi, float* step);   // false if hitId is not a slider
float& sliderValue(PanelState& s, int hitId);
float snapSlider(int hitId, float v);                              // rounds to the slider's steps and keeps it in range

// Saved settings: a few short lines of text (sound, colour, size, transparency, distance, movement slider values).
// The movement SWITCHES are deliberately not saved.
std::string settingsToText(const PanelState& s);
bool settingsFromText(const std::string& text, PanelState& s);   // false if nothing usable was found

}  // namespace tzpanel
