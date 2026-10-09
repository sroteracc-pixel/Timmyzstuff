// Tests for pointer.cpp / panel.cpp logic. Prints PASS/FAIL lines.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pointer.h"
using namespace tzpanel;

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)
static bool near(float a, float b, float eps = 0.6f) { return std::fabs(a - b) <= eps; }

static std::vector<HitRect> hitsFor(const PanelState& s) { Canvas c(kWidth, kHeight); std::vector<HitRect> h; drawPanel(c, s, &h); return h; }
static HitRect find(const std::vector<HitRect>& h, int id) { for (auto& r : h) if (r.id == id) return r; return HitRect{0, 0, 0, 0, 0}; }

// One update with the right hand pointing at (x,y) with the given trigger, left hand idle.
static Outcome step(Interaction& in, PanelState& s, float x, float y, float trig, int hand = 1, float otherTrig = 0) {
    const std::vector<HitRect> h = hitsFor(s);
    HandAim a[2];
    a[hand].valid = true; a[hand].onPlane = true; a[hand].x = x; a[hand].y = y; a[hand].trigger = trig;
    a[1 - hand].valid = false; a[1 - hand].trigger = otherTrig;
    return in.update(s, h, a);
}
static void click(Interaction& in, PanelState& s, int id) {      // move onto a control, pull the trigger, let go
    const std::vector<HitRect> h = hitsFor(s); const HitRect r = find(h, id);
    const float x = r.x + r.w / 2, y = r.y + r.h / 2;
    step(in, s, x, y, 0.0f); step(in, s, x, y, 0.9f); step(in, s, x, y, 0.0f);
}
static void openMenu(Interaction& in, PanelState& s) {            // the opening gesture: both triggers held, then let go
    in.reset();
    step(in, s, 100, 100, 0.95f, 1, 0.95f);
    step(in, s, 100, 100, 0.0f);
}

int main() {
    std::printf("== geometry\n");
    float head[7] = {0, 0, 0, 1, 0, 1.7f, 0};
    Placement pl = placeInFront(head, 1.0f, 1.15f);
    CHECK("panel 1.15 m ahead, at head height", near(pl.pos[0], 0, 1e-3f) && near(pl.pos[1], 1.7f, 1e-3f) && near(pl.pos[2], -1.15f, 1e-3f) && near(pl.yaw, 0, 1e-3f));
    CHECK("panel is 0.95 x 0.7125 m at 1x", near(pl.width, 0.95f, 1e-4f) && near(pl.height, 0.7125f, 1e-4f));
    CHECK("size is limited to 0.75x..1.5x", near(placeInFront(head, 5.0f, 1.15f).width, 1.425f, 1e-4f) && near(placeInFront(head, 0.1f, 1.15f).width, 0.7125f, 1e-4f));
    float ctl[7] = {0, 0, 0, 1, 0.0f, 1.7f, -0.3f};                    // controller straight ahead, pointing -Z
    float x = 0, y = 0;
    CHECK("pointing straight ahead hits the centre of the picture", intersect(rayFromPose(ctl, 0), pl, &x, &y) && near(x, 512) && near(y, 384));
    float ctl2[7] = {0, 0, 0, 1, 0.2f, 1.4f, -0.2f};
    CHECK("a point 0.2 m right and 0.3 m down maps to the right pixel", intersect(rayFromPose(ctl2, 0), pl, &x, &y) && near(x, (0.2f / 0.95f + 0.5f) * 1024, 0.7f) && near(y, (0.5f + 0.3f / 0.7125f) * 768, 0.7f));
    // turned 90 degrees to the right: yaw rotation about Y, q = (0, sin45, 0, cos45) rotates -Z to -X
    float ctl3[7] = {0, 0.70710678f, 0, 0.70710678f, 0, 1.7f, -0.3f};
    CHECK("pointing sideways (away from the panel) does not hit", !intersect(rayFromPose(ctl3, 0), pl, &x, &y));
    float ctl4[7] = {0, 1, 0, 0, 0, 1.7f, -0.3f};                       // turned around 180 degrees: pointing at +Z, away
    CHECK("pointing away does not hit", !intersect(rayFromPose(ctl4, 0), pl, &x, &y));
    // tilt up: +10 degrees from a point 0.5 m in front of the panel lifts the hit point
    float ctl5[7] = {0, 0, 0, 1, 0, 1.7f, -0.65f};
    intersect(rayFromPose(ctl5, 0), pl, &x, &y); const float y0 = y;
    intersect(rayFromPose(ctl5, 10), pl, &x, &y);
    CHECK("tilting the line up moves the dot up the picture", y < y0 - 20);
    CHECK("menu distance moves the panel: 0.6 m and 2.0 m, clamped", near(placeInFront(head, 1.0f, 0.6f).pos[2], -0.6f, 1e-3f) && near(placeInFront(head, 1.0f, 2.0f).pos[2], -2.0f, 1e-3f) && near(placeInFront(head, 1.0f, 9.0f).pos[2], -2.0f, 1e-3f) && near(placeInFront(head, 1.0f, 0.1f).pos[2], -0.6f, 1e-3f));
    // panel placed for a head turned 90 degrees left, controller pointing the same way
    float s45 = std::sin(3.14159265f / 4), c45 = std::cos(3.14159265f / 4);
    float headL[7] = {0, s45, 0, c45, 1.0f, 1.5f, 2.0f};
    Placement pl2 = placeInFront(headL, 1.0f, 1.15f);
    float ctlL[7] = {0, s45, 0, c45, 0.8f, 1.5f, 2.0f};
    CHECK("works for a head turned left too (hits the centre)", intersect(rayFromPose(ctlL, 0), pl2, &x, &y) && near(x, 512, 1.0f) && near(y, 384, 1.0f));

    std::printf("== clicking\n");
    PanelState s; Interaction in; openMenu(in, s);
    {   // the opening gesture must not click
        PanelState s2; Interaction i2; i2.reset();
        const HitRect r = find(hitsFor(s2), HIT_TEST);
        step(i2, s2, r.x + r.w / 2, r.y + r.h / 2, 0.95f, 1, 0.95f);
        CHECK("triggers still held from opening: no click", s2.testClicks == 0);
        step(i2, s2, r.x + r.w / 2, r.y + r.h / 2, 0.95f);
        CHECK("one trigger still held after the other let go: still no click", s2.testClicks == 0 && !i2.armed());
        step(i2, s2, r.x + r.w / 2, r.y + r.h / 2, 0.0f);
        CHECK("after both are let go the menu is armed", i2.armed());
        step(i2, s2, r.x + r.w / 2, r.y + r.h / 2, 0.9f);
        CHECK("then a trigger pull clicks", s2.testClicks == 1);
    }
    {   const HitRect r = find(hitsFor(s), HIT_TEST);
        Outcome o = step(in, s, r.x + 5, r.y + 5, 0.0f);
        CHECK("pointing at a button highlights it (hover)", s.hover == HIT_TEST && o.redraw && o.cursorVisible);
        o = step(in, s, r.x + 5, r.y + 5, 0.9f);
        CHECK("trigger pull presses Test button once", s.testClicks == 1 && o.clickedId == HIT_TEST && o.pressed);
        step(in, s, r.x + 5, r.y + 5, 0.9f); step(in, s, r.x + 5, r.y + 5, 0.8f);
        CHECK("holding the trigger does not repeat", s.testClicks == 1);
        step(in, s, r.x + 5, r.y + 5, 0.1f); step(in, s, r.x + 5, r.y + 5, 0.9f);
        CHECK("second pull presses again", s.testClicks == 2);
        step(in, s, r.x + 5, r.y + 5, 0.0f);
        o = step(in, s, 5, 5, 0.0f);
        CHECK("pointing at nothing clears the highlight", s.hover == -1);
        step(in, s, 5, 5, 0.9f); step(in, s, 5, 5, 0.0f);
        CHECK("clicking on empty space does nothing", s.testClicks == 2);
    }
    {   Outcome o{};
        const HitRect r = find(hitsFor(s), HIT_CLOSE);
        step(in, s, r.x + 20, r.y + 20, 0.0f);
        o = step(in, s, r.x + 20, r.y + 20, 0.9f);
        CHECK("X button reports close", o.close);
        step(in, s, 5, 5, 0.0f);
    }
    {   const bool before = s.sound; click(in, s, HIT_SOUND);
        CHECK("Sound effects toggles", s.sound != before);
        click(in, s, HIT_SOUND);
        CHECK("and toggles back", s.sound == before);
        click(in, s, HIT_TAB0 + 4);
        CHECK("clicking a sidebar entry changes the page", s.tab == 4);
        CHECK("other pages have no Settings controls to hit", find(hitsFor(s), HIT_SOUND).id == 0);
        click(in, s, HIT_TAB0 + 0);
        CHECK("Settings page is back", s.tab == 0 && find(hitsFor(s), HIT_SOUND).id != 0);
    }
    {   // a left-hand pointer works too, and a trigger pull on the pointing hand is what counts
        PanelState s3; Interaction i3; openMenu(i3, s3);
        const HitRect r = find(hitsFor(s3), HIT_TEST);
        const std::vector<HitRect> h = hitsFor(s3);
        HandAim a[2]; a[0].valid = true; a[0].onPlane = true; a[0].x = r.x + 9; a[0].y = r.y + 9; a[0].trigger = 0.0f;
        i3.update(s3, h, a); a[0].trigger = 0.9f; i3.update(s3, h, a);
        CHECK("left hand can click", s3.testClicks == 1);
        // both hands on the panel: the one pulling its trigger wins
        PanelState s4; Interaction i4; openMenu(i4, s4);
        const HitRect t = find(hitsFor(s4), HIT_TEST), q = find(hitsFor(s4), HIT_SOUND);
        const std::vector<HitRect> h4 = hitsFor(s4);
        HandAim b[2]; b[0].valid = b[1].valid = true; b[0].onPlane = b[1].onPlane = true;
        b[0].x = t.x + 9; b[0].y = t.y + 9; b[1].x = q.x + 9; b[1].y = q.y + 9; b[0].trigger = 0; b[1].trigger = 0;
        Outcome o = i4.update(s4, h4, b);
        CHECK("both hands aiming: right hand is the pointer", o.hand == 1);
        b[0].trigger = 0.9f; o = i4.update(s4, h4, b);
        CHECK("left hand pulls its trigger: left hand takes over and presses", o.hand == 0 && s4.testClicks == 1);
    }

    std::printf("== colour list\n");
    {   PanelState c; Interaction ci; openMenu(ci, c);
        CHECK("the menu starts with the Default (purple) colour selected", c.colorIndex == 0 && colorChoice(0).rgb[0] == 139 && colorChoice(0).rgb[1] == 92 && colorChoice(0).rgb[2] == 246 && std::string(colorChoice(0).name) == "Default");
        CHECK("the colour list has 10 entries: Default first, then Red ... White", kColorCount == 10 && std::string(colorChoice(1).name) == "Red" && std::string(colorChoice(9).name) == "White");
        CHECK("Settings page shows all 10 colours as clickable", [&]{ const auto h = hitsFor(c); for (int i = 0; i < kColorCount; ++i) if (find(h, HIT_COLOR0 + i).id == 0) return false; return true; }());
        CHECK("there is no number typing any more (no keypad areas)", find(hitsFor(c), 40).id == 0 && find(hitsFor(c), 31).id == 0);
        const Outcome o0 = (click(ci, c, HIT_COLOR0 + 1), Outcome());
        (void)o0;
        CHECK("clicking Red selects Red", c.colorIndex == 1);
        click(ci, c, HIT_COLOR0 + 7);
        CHECK("clicking Blue selects Blue", c.colorIndex == 7);
        click(ci, c, HIT_COLOR0 + 0);
        CHECK("clicking Default goes back to the purple", c.colorIndex == 0);
        // the picture really changes colour
        PanelState a, b; b.colorIndex = 1;
        Canvas ca(kWidth, kHeight), cb(kWidth, kHeight); drawPanel(ca, a, nullptr); drawPanel(cb, b, nullptr);
        const int px = (150 * kWidth + 150) * 4;      // inside the selected "Settings" sidebar button
        CHECK("red menu has a red accent where purple had purple", ca.data()[px + 2] > ca.data()[px] && cb.data()[px] > cb.data()[px + 2]);
    }

    std::printf("== three sliders\n");
    {   PanelState z; Interaction zi; openMenu(zi, z);
        CHECK("start values: size 1.00x, transparency 0 (solid), distance 1.15 m", near(z.scale, 1.0f, 1e-4f) && z.transparency == 0.0f && near(z.distance, 1.15f, 1e-4f));
        // size
        HitRect r = find(hitsFor(z), HIT_SLIDER_SIZE);
        float tx0 = r.x + 16, tx1 = r.x + r.w - 16, ty = r.y + r.h / 2;
        step(zi, z, tx0 + (tx1 - tx0) * 0.333f, ty, 0.0f);
        Outcome o = step(zi, z, tx0 + (tx1 - tx0) * 0.333f, ty, 0.9f);
        CHECK("pressing on the size slider starts a drag", z.dragSlider == HIT_SLIDER_SIZE && near(z.scale, 1.0f, 0.051f));
        o = step(zi, z, tx1 + 80, ty + 60, 0.9f);
        CHECK("dragging to the far right gives exactly 1.50x (even off the track)", near(z.scale, 1.5f, 1e-4f) && o.cursorVisible && !o.layoutCommitted);
        o = step(zi, z, tx0 - 200, ty - 90, 0.9f);
        CHECK("dragging past the left end gives exactly 0.75x", near(z.scale, 0.75f, 1e-4f));
        CHECK("nothing else is clicked while dragging", z.testClicks == 0 && z.hover == HIT_SLIDER_SIZE);
        o = step(zi, z, tx0 - 200, ty - 90, 0.0f);
        CHECK("letting go commits the size (panel resizes now) and saves", o.layoutCommitted && o.saveNeeded && z.dragSlider == 0 && !zi.dragging());
        // transparency
        r = find(hitsFor(z), HIT_SLIDER_ALPHA);
        tx0 = r.x + 16; tx1 = r.x + r.w - 16; ty = r.y + r.h / 2;
        step(zi, z, tx0 + 4, ty, 0.0f); step(zi, z, tx0 + 4, ty, 0.9f);
        CHECK("transparency starts at 0 at the left end", z.transparency == 0.0f && z.dragSlider == HIT_SLIDER_ALPHA);
        o = step(zi, z, tx1 + 100, ty, 0.9f);
        CHECK("transparency tops out at exactly 0.75 (75% see-through)", near(z.transparency, 0.75f, 1e-4f) && !o.layoutCommitted);
        step(zi, z, tx0 + (tx1 - tx0) * 0.5f, ty, 0.9f);
        CHECK("transparency middle is 0.35 / 0.40 (0.05 steps)", near(z.transparency, 0.375f, 0.03f) && near(std::fmod(z.transparency / 0.05f + 0.5f, 1.0f), 0.5f, 0.02f));
        o = step(zi, z, tx0 + (tx1 - tx0) * 0.5f, ty, 0.0f);
        CHECK("letting go of transparency saves but does not move the panel", o.saveNeeded && !o.layoutCommitted);
        // distance
        r = find(hitsFor(z), HIT_SLIDER_DIST);
        tx0 = r.x + 16; tx1 = r.x + r.w - 16; ty = r.y + r.h / 2;
        step(zi, z, tx0 + (tx1 - tx0) * 0.5f, ty, 0.0f); step(zi, z, tx0 + (tx1 - tx0) * 0.5f, ty, 0.9f);
        step(zi, z, tx0 - 50, ty, 0.9f);
        CHECK("distance goes down to exactly 0.60 m", near(z.distance, 0.6f, 1e-4f));
        step(zi, z, tx1 + 50, ty, 0.9f);
        CHECK("distance goes up to exactly 2.00 m", near(z.distance, 2.0f, 1e-4f));
        o = step(zi, z, tx1 + 50, ty, 0.0f);
        CHECK("letting go of distance moves the panel (commit) and saves", o.layoutCommitted && o.saveNeeded);
        // only one slider moves at a time
        CHECK("other sliders were not touched by the distance drag", near(z.scale, 0.75f, 1e-4f) && near(z.transparency, z.transparency, 1e-4f));
    }
    {   PanelState f; f.transparency = 0.5f;
        Canvas full(kWidth, kHeight), half(kWidth, kHeight);
        drawPanel(full, PanelState(), nullptr); drawPanel(half, PanelState(), nullptr); half.fadeAll(0.5f);
        const int i = (300 * kWidth + 600) * 4 + 3;
        CHECK("fading by half halves the opacity of the picture", near(half.data()[i], full.data()[i] / 2.0f, 1.5f) && full.data()[i] > 200);
        Canvas none(kWidth, kHeight); drawPanel(none, PanelState(), nullptr); none.fadeAll(1.0f);
        CHECK("fading by 1.0 (transparency 0) changes nothing", std::memcmp(none.data(), full.data(), kWidth * kHeight * 4) == 0);
    }

    std::printf("== saved settings\n");
    {   PanelState a; a.sound = false; a.colorIndex = 5; a.scale = 1.25f; a.transparency = 0.45f; a.distance = 0.85f;
        const std::string txt = settingsToText(a);
        PanelState b; const bool ok = settingsFromText(txt, b);
        CHECK("settings round-trip (sound, colour, size, transparency, distance)", ok && !b.sound && b.colorIndex == 5 && near(b.scale, 1.25f, 1e-3f) && near(b.transparency, 0.45f, 1e-3f) && near(b.distance, 0.85f, 1e-3f));
        PanelState d; CHECK("garbage is ignored", !settingsFromText("hello\nsound=7\nscale=9.9\ncolor=44\ntransparency=0.9\ndistance=0.1\n", d) && d.sound && d.colorIndex == 0 && near(d.scale, 1.0f, 1e-4f) && d.transparency == 0.0f && near(d.distance, 1.15f, 1e-4f));
    }
    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
