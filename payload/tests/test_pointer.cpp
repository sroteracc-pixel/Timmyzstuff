// Tests for pointer.cpp / panel.cpp logic. Prints PASS/FAIL lines.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "pointer.h"
#include "aimbot.h"       // only to check that the menu and the aimbot rules use the same numbers
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
        CHECK("transparency tops out at exactly 0.25 (25% see-through)", near(z.transparency, 0.25f, 1e-4f) && !o.layoutCommitted);
        step(zi, z, tx0 + (tx1 - tx0) * 0.5f, ty, 0.9f);
        CHECK("transparency middle is 0.10 or 0.15 (0.05 steps)", near(z.transparency, 0.125f, 0.03f) && near(std::fmod(z.transparency / 0.05f + 0.5f, 1.0f), 0.5f, 0.02f));
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

    std::printf("== Movement page\n");
    {   PanelState m; Interaction mi; openMenu(mi, m);
        click(mi, m, HIT_TAB0 + 3);
        CHECK("the Movement entry opens the Movement page", m.tab == kTabMovement);
        const auto h = hitsFor(m);
        CHECK("it has 4 switches, 4 sliders and the Scan button", find(h, HIT_TOGGLE_SPEED).id && find(h, HIT_TOGGLE_JUMP).id && find(h, HIT_TOGGLE_LOWGRAV).id && find(h, HIT_TOGGLE_HIGHGRAV).id &&
              find(h, HIT_SLIDER_SPEED).id && find(h, HIT_SLIDER_JUMP).id && find(h, HIT_SLIDER_LOWGRAV).id && find(h, HIT_SLIDER_HIGHGRAV).id && find(h, HIT_SCAN).id);
        CHECK("the Settings controls are not on this page", find(h, HIT_SOUND).id == 0 && find(h, HIT_SLIDER_SIZE).id == 0 && find(h, HIT_COLOR0).id == 0);
        bool inside = true, overlap = false;
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i].x < 0 || h[i].y < 0 || h[i].x + h[i].w > kWidth || h[i].y + h[i].h > kHeight) inside = false;
            if (h[i].x >= 296 && h[i].id != HIT_CLOSE && h[i].y + h[i].h > 624) inside = false;      // page controls (right of the sidebar) must end above the footer (starts at y=638)
            for (size_t j = i + 1; j < h.size(); ++j)
                if (h[i].x < h[j].x + h[j].w && h[j].x < h[i].x + h[i].w && h[i].y < h[j].y + h[j].h && h[j].y < h[i].y + h[i].h) overlap = true;
        }
        CHECK("every control fits on the picture; page controls end above the footer", inside);
        CHECK("no two controls overlap (a click can only mean one thing)", !overlap);
        PanelState fresh;
        CHECK("everything starts OFF", !fresh.speedOn && !fresh.jumpOn && fresh.gravityMode == 0 && !m.speedOn && !m.jumpOn && m.gravityMode == 0);
        CHECK("slider starts are the smallest values: 1.1x, 1.1x, 0%, 0%", near(fresh.speedMul, 1.1f, 1e-4f) && near(fresh.jumpMul, 1.1f, 1e-4f) && fresh.lowGravPct == 0 && fresh.highGravPct == 0);

        const std::vector<HitRect> mh = hitsFor(m);                 // the places of the controls do not depend on their values: draw once
        auto stepH = [&](float xx, float yy, float trig) { HandAim a[2]; a[1].valid = true; a[1].onPlane = true; a[1].x = xx; a[1].y = yy; a[1].trigger = trig; a[0].valid = false; a[0].trigger = 0; return mi.update(m, mh, a); };
        auto clickH = [&](int id) { const HitRect r = find(mh, id); const float xx = r.x + r.w / 2, yy = r.y + r.h / 2; stepH(xx, yy, 0.0f); stepH(xx, yy, 0.9f); stepH(xx, yy, 0.0f); };

        // --- switches
        click(mi, m, HIT_TOGGLE_SPEED);
        CHECK("Speed switch turns on", m.speedOn && !m.jumpOn && m.gravityMode == 0);
        click(mi, m, HIT_TOGGLE_JUMP);
        CHECK("Jump switch turns on too (speed and jump may both be on)", m.speedOn && m.jumpOn);
        click(mi, m, HIT_TOGGLE_SPEED);
        CHECK("Speed switch turns off again, jump unchanged", !m.speedOn && m.jumpOn);
        click(mi, m, HIT_TOGGLE_JUMP);
        CHECK("Jump switch turns off", !m.jumpOn);
        click(mi, m, HIT_TOGGLE_LOWGRAV);
        CHECK("Low Gravity on", m.gravityMode == 1);
        click(mi, m, HIT_TOGGLE_HIGHGRAV);
        CHECK("High Gravity on turns Low Gravity OFF (only one gravity mode)", m.gravityMode == 2);
        click(mi, m, HIT_TOGGLE_LOWGRAV);
        CHECK("Low Gravity again swaps back, High goes off", m.gravityMode == 1);
        click(mi, m, HIT_TOGGLE_LOWGRAV);
        CHECK("pressing the active gravity switch turns gravity off", m.gravityMode == 0);
        click(mi, m, HIT_TOGGLE_HIGHGRAV); click(mi, m, HIT_TOGGLE_HIGHGRAV);
        CHECK("High Gravity on then off", m.gravityMode == 0);
        {   // however you press them, the two gravity switches can never both show ON
            bool never = true; unsigned seed = 12345;
            for (int i = 0; i < 300; ++i) {
                seed = seed * 1103515245u + 12345u; const int pick = (seed >> 16) % 4;
                clickH(pick == 0 ? HIT_TOGGLE_SPEED : pick == 1 ? HIT_TOGGLE_JUMP : pick == 2 ? HIT_TOGGLE_LOWGRAV : HIT_TOGGLE_HIGHGRAV);
                if (m.gravityMode < 0 || m.gravityMode > 2) never = false;
            }
            CHECK("300 random presses: gravity mode is always 0, 1 or 2 (never both)", never);
            if (m.speedOn) click(mi, m, HIT_TOGGLE_SPEED);
            if (m.jumpOn) click(mi, m, HIT_TOGGLE_JUMP);
            if (m.gravityMode == 1) click(mi, m, HIT_TOGGLE_LOWGRAV);
            if (m.gravityMode == 2) click(mi, m, HIT_TOGGLE_HIGHGRAV);
            CHECK("and everything can be switched back off", !m.speedOn && !m.jumpOn && m.gravityMode == 0);
        }

        // --- sliders: exact ends, exact steps, nothing else changes
        auto slide = [&](int id, float frac, bool release) -> Outcome {
            const HitRect r = find(mh, id);
            const float a = r.x + 16, b = r.x + r.w - 16, yy = r.y + r.h / 2, xx = a + (b - a) * frac;
            if (m.dragSlider == 0) { stepH(xx, yy, 0.0f); stepH(xx, yy, 0.9f); }
            Outcome o = stepH(xx, yy, 0.9f);
            if (release) o = stepH(xx, yy, 0.0f);
            return o;
        };
        auto valueOf = [&](int id) { PanelState t = m; return sliderValue(t, id); };
        struct Spec { int id; const char* name; float lo, hi, st; };
        const Spec specs[4] = {{HIT_SLIDER_SPEED, "Speed", 1.1f, 5.0f, 0.1f}, {HIT_SLIDER_JUMP, "Jump", 1.1f, 5.0f, 0.1f}, {HIT_SLIDER_LOWGRAV, "Low Gravity", 0.0f, 90.0f, 5.0f}, {HIT_SLIDER_HIGHGRAV, "High Gravity", 0.0f, 90.0f, 5.0f}};
        for (const Spec& sp : specs) {
            char nm[96];
            slide(sp.id, 1.0f, false);
            std::snprintf(nm, sizeof nm, "%s slider: far right is exactly the maximum", sp.name);
            CHECK(nm, near(valueOf(sp.id), sp.hi, 1e-3f));
            slide(sp.id, 0.0f, false);
            std::snprintf(nm, sizeof nm, "%s slider: far left is exactly the minimum", sp.name);
            CHECK(nm, near(valueOf(sp.id), sp.lo, 1e-3f));
            // sweep the whole track: every value is a whole number of steps, and every step is reachable
            std::set<int> seen; bool onStep = true, inRange = true;
            for (int i = 0; i <= 400; ++i) {
                slide(sp.id, i / 400.0f, false);
                const float v = valueOf(sp.id);
                const float k = (v - sp.lo) / sp.st;
                if (std::fabs(k - std::round(k)) > 0.002f) onStep = false;
                if (v < sp.lo - 1e-3f || v > sp.hi + 1e-3f) inRange = false;
                seen.insert(static_cast<int>(std::lround(k)));
            }
            const int steps = static_cast<int>(std::lround((sp.hi - sp.lo) / sp.st)) + 1;
            std::snprintf(nm, sizeof nm, "%s slider: only whole steps of %g between %g and %g", sp.name, sp.st, sp.lo, sp.hi);
            CHECK(nm, onStep && inRange);
            std::snprintf(nm, sizeof nm, "%s slider: all %d positions can be reached", sp.name, steps);
            CHECK(nm, static_cast<int>(seen.size()) == steps);
            const Outcome o = slide(sp.id, 0.5f, true);
            std::snprintf(nm, sizeof nm, "%s slider: letting go saves, does not move the panel, does not flip a switch", sp.name);
            CHECK(nm, o.saveNeeded && !o.layoutCommitted && !m.speedOn && !m.jumpOn && m.gravityMode == 0);
        }
        CHECK("the sliders move even while their switch is off (switches stay off)", !m.speedOn && !m.jumpOn && m.gravityMode == 0);
        slide(HIT_SLIDER_SPEED, 1.0f, true); slide(HIT_SLIDER_JUMP, 0.0f, true); slide(HIT_SLIDER_LOWGRAV, 1.0f, true); slide(HIT_SLIDER_HIGHGRAV, 0.5f, true);
        CHECK("each slider keeps its own value: speed 5.0, jump 1.1, low 90%, high 45%", near(m.speedMul, 5.0f, 1e-3f) && near(m.jumpMul, 1.1f, 1e-3f) && near(m.lowGravPct, 90.0f, 1e-3f) && near(m.highGravPct, 45.0f, 1.01f) && m.highGravPct == 45.0f);
        CHECK("the Settings sliders were not touched", near(m.scale, 1.0f, 1e-4f) && m.transparency == 0.0f && near(m.distance, 1.15f, 1e-4f));

        // --- the exact value is drawn on the page (changing the value changes the picture where the number is)
        {   PanelState a2, b2; a2.tab = b2.tab = kTabMovement; a2.speedMul = 1.1f; b2.speedMul = 1.2f;
            Canvas ca(kWidth, kHeight), cb(kWidth, kHeight); drawPanel(ca, a2, nullptr); drawPanel(cb, b2, nullptr);
            const HitRect r = find(hitsFor(a2), HIT_SLIDER_SPEED);
            bool differs = false;
            for (int yy = (int)r.y; yy < (int)(r.y + r.h) && !differs; ++yy)
                for (int xx = (int)(r.x + r.w + 20); xx < (int)(r.x + r.w + 120) && !differs; ++xx)
                    if (std::memcmp(ca.data() + (yy * kWidth + xx) * 4, cb.data() + (yy * kWidth + xx) * 4, 4) != 0) differs = true;
            CHECK("the number next to the Speed slider changes with the value", differs);
        }

        // --- Scan button
        PanelState sc; Interaction si; openMenu(si, sc); sc.tab = kTabMovement;
        const HitRect sb = find(hitsFor(sc), HIT_SCAN);
        const float bx = sb.x + sb.w / 2, by = sb.y + sb.h / 2;
        step(si, sc, bx, by, 0.0f);
        Outcome so = step(si, sc, bx, by, 0.9f);
        CHECK("pressing Scan asks for a scan and shows 'scanning'", so.scanRequested && sc.scanState == 1);
        step(si, sc, bx, by, 0.0f); so = step(si, sc, bx, by, 0.9f);
        CHECK("pressing it again while it is scanning does NOT start a second scan", !so.scanRequested && sc.scanState == 1);
        step(si, sc, bx, by, 0.0f);
        sc.scanState = 2;
        so = step(si, sc, bx, by, 0.9f);
        CHECK("after a finished scan it can be run again", so.scanRequested && sc.scanState == 1);
        CHECK("the Scan button changes no movement setting", !sc.speedOn && !sc.jumpOn && sc.gravityMode == 0);
    }


    std::printf("== Basketball page (Aimbot)\n");
    {   PanelState m; Interaction mi; openMenu(mi, m);
        click(mi, m, HIT_TAB0 + 4);
        CHECK("the Basketball entry opens the Basketball page", m.tab == kTabBasketball);
        const std::vector<HitRect> h = hitsFor(m);
        CHECK("it has the Aimbot switch, the distance slider and the 'Scan ball and hoops' button", find(h, HIT_TOGGLE_AIM).id && find(h, HIT_SLIDER_AIMCAP).id && find(h, HIT_SCAN_SHOT).id);
        CHECK("the Movement and Settings controls are not on this page", find(h, HIT_TOGGLE_SPEED).id == 0 && find(h, HIT_SCAN).id == 0 && find(h, HIT_SOUND).id == 0 && find(h, HIT_SLIDER_SPEED).id == 0);
        bool inside = true, overlap = false;
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i].x < 0 || h[i].y < 0 || h[i].x + h[i].w > kWidth || h[i].y + h[i].h > kHeight) inside = false;
            if (h[i].x >= 296 && h[i].id != HIT_CLOSE && h[i].y + h[i].h > 624) inside = false;
            for (size_t j = i + 1; j < h.size(); ++j)
                if (h[i].x < h[j].x + h[j].w && h[j].x < h[i].x + h[i].w && h[i].y < h[j].y + h[j].h && h[j].y < h[i].y + h[i].h) overlap = true;
        }
        CHECK("every control fits on the picture and ends above the footer", inside);
        CHECK("no two controls overlap", !overlap);
        PanelState fresh;
        CHECK("the Aimbot starts OFF, at Unlimited (50)", !fresh.aimOn && !m.aimOn && fresh.aimCapM == 50.0f && m.aimCapM == 50.0f && fresh.aimLinkState == 0);
        CHECK("the menu's slider numbers are the aimbot rules' numbers (5 m, 50 m, 1 m, default 50)", kAimMin == tzaim::kCapMinM && kAimMax == tzaim::kCapMaxM && kAimStep == tzaim::kCapStepM && kAimDefault == tzaim::kCapDefaultM);
        {   bool same = true;
            for (float v = 5.0f; v <= 50.0f; v += 0.1f) if (aimCapText(v) != tzaim::capLabel(v)) { same = false; std::printf("      differs at %.2f: '%s' vs '%s'\n", v, aimCapText(v).c_str(), tzaim::capLabel(v).c_str()); break; }
            CHECK("the text on the page is the same as the aimbot's own label for every value (Unlimited at 50, '23 m' ...)", same); }
        CHECK("labels: 5 -> '5 m', 23 -> '23 m', 49 -> '49 m', 50 -> 'Unlimited'", aimCapText(5) == "5 m" && aimCapText(23) == "23 m" && aimCapText(49) == "49 m" && aimCapText(50) == "Unlimited");

        click(mi, m, HIT_TOGGLE_AIM);
        CHECK("the Aimbot switch turns on", m.aimOn && !m.speedOn && !m.jumpOn && m.gravityMode == 0);
        click(mi, m, HIT_TOGGLE_AIM);
        CHECK("... and off again", !m.aimOn);
        CHECK("the page has the 'Hold Y to aim' switch, and it starts ON", find(h, HIT_TOGGLE_AIMY).id != 0 && fresh.aimHoldY && m.aimHoldY);
        {   const HitRect jr = find(hitsFor(m), HIT_TOGGLE_AIMY); const float jx = jr.x + jr.w / 2, jy = jr.y + jr.h / 2;
            step(mi, m, jx, jy, 0.0f); const Outcome jo = step(mi, m, jx, jy, 0.9f); step(mi, m, jx, jy, 0.0f);
            CHECK("clicking it turns it off, asks for a save (it is a setting), and changes nothing else", !m.aimHoldY && jo.saveNeeded && !m.aimOn && m.aimCapM == 50.0f && !m.speedOn && !m.jumpOn);
            click(mi, m, HIT_TOGGLE_AIMY);
            CHECK("... and on again", m.aimHoldY); }

        // dragging the slider: far left, far right, and every position on the way is a whole number between 5 and 50
        const std::vector<HitRect> bh = hitsFor(m);
        auto stepH = [&](float xx, float yy, float trig) { HandAim a[2]; a[1].valid = true; a[1].onPlane = true; a[1].x = xx; a[1].y = yy; a[1].trigger = trig; a[0].valid = false; a[0].trigger = 0; return mi.update(m, bh, a); };
        const HitRect sr = find(bh, HIT_SLIDER_AIMCAP);
        const float y0 = sr.y + sr.h / 2, x0 = sr.x + 16, x1 = sr.x + sr.w - 16;
        bool allWhole = true; int seen = 0; float minSeen = 99, maxSeen = 0; std::set<int> values;
        stepH(x0, y0, 0.0f); stepH(x0, y0, 0.9f);
        for (int i = 0; i <= 400; ++i) { const float xx = x0 + (x1 - x0) * i / 400.0f; stepH(xx, y0, 0.9f); const float v = m.aimCapM; ++seen;
            if (v != std::floor(v) || v < 5.0f || v > 50.0f) allWhole = false; if (v < minSeen) minSeen = v; if (v > maxSeen) maxSeen = v; values.insert((int)v); }
        stepH(x1, y0, 0.0f);
        CHECK("dragging from the far left to the far right only ever gives whole metres from 5 to 50", allWhole && minSeen == 5.0f && maxSeen == 50.0f);
        CHECK("... and it passes through every one of the 46 positions (5, 6, ... 50)", values.size() == 46);
        CHECK("... ends on 50 = Unlimited at the far right", m.aimCapM == 50.0f && aimCapText(m.aimCapM) == "Unlimited");
        stepH(x0, y0, 0.0f); stepH(x0, y0, 0.9f); stepH(x0 - 40, y0, 0.9f); stepH(x0 - 40, y0, 0.0f);
        CHECK("dragging past the far left stays at 5 m", m.aimCapM == 5.0f);
        stepH(x0 + (x1 - x0) * 0.5f, y0, 0.0f); stepH(x0 + (x1 - x0) * 0.5f, y0, 0.9f); stepH(x0 + (x1 - x0) * 0.5f, y0, 0.0f);
        CHECK("the middle of the slider is about 27-28 m", m.aimCapM >= 27.0f && m.aimCapM <= 28.0f);
        CHECK("the slider changes no movement setting", !m.speedOn && !m.jumpOn && m.gravityMode == 0 && near(m.speedMul, 1.1f, 1e-4f) && m.lowGravPct == 0);

        // the number on the page changes with the value
        {   PanelState a2, b2; a2.tab = b2.tab = kTabBasketball; a2.aimCapM = 23; b2.aimCapM = 24;
            Canvas ca(kWidth, kHeight), cb(kWidth, kHeight); drawPanel(ca, a2, nullptr); drawPanel(cb, b2, nullptr);
            const HitRect r = find(hitsFor(a2), HIT_SLIDER_AIMCAP);
            bool differs = false;
            for (int yy = (int)r.y; yy < (int)(r.y + r.h) + 12 && !differs; ++yy)
                for (int xx = (int)(r.x + r.w + 20); xx < (int)(r.x + r.w + 150) && !differs; ++xx)
                    if (std::memcmp(ca.data() + (yy * kWidth + xx) * 4, cb.data() + (yy * kWidth + xx) * 4, 4) != 0) differs = true;
            CHECK("the number next to the slider changes with the value (23 m vs 24 m)", differs); }

        // the scan button of this page
        PanelState sc; Interaction si; openMenu(si, sc); sc.tab = kTabBasketball;
        const HitRect sb = find(hitsFor(sc), HIT_SCAN_SHOT);
        const float bx = sb.x + sb.w / 2, by = sb.y + sb.h / 2;
        step(si, sc, bx, by, 0.0f);
        Outcome so = step(si, sc, bx, by, 0.9f);
        CHECK("'Scan ball and hoops' asks for a BALL scan (not the movement scan) and shows 'scanning'", so.shotScanRequested && !so.scanRequested && sc.scanState == 1);
        step(si, sc, bx, by, 0.0f); so = step(si, sc, bx, by, 0.9f);
        CHECK("pressing it again while it is scanning does NOT start a second scan", !so.shotScanRequested && sc.scanState == 1);
        step(si, sc, bx, by, 0.0f); sc.scanState = 3;
        so = step(si, sc, bx, by, 0.9f);
        CHECK("after a failed or finished scan it can be run again", so.shotScanRequested && sc.scanState == 1);
        CHECK("the scan button changes no setting", !sc.aimOn && sc.aimCapM == 50.0f && !sc.speedOn);
        // a running scan from the OTHER page also blocks this one (one scan at a time)
        PanelState sd; Interaction sdi; openMenu(sdi, sd); sd.tab = kTabBasketball; sd.scanState = 1;
        step(sdi, sd, bx, by, 0.0f); so = step(sdi, sd, bx, by, 0.9f);
        CHECK("while the movement scan is running, this button does nothing", !so.shotScanRequested && !so.scanRequested);
    }

    std::printf("== saved settings\n");
    {   PanelState a; a.sound = false; a.colorIndex = 5; a.scale = 1.25f; a.transparency = 0.20f; a.distance = 0.85f;
        const std::string txt = settingsToText(a);
        PanelState b; const bool ok = settingsFromText(txt, b);
        CHECK("settings round-trip (sound, colour, size, transparency, distance)", ok && !b.sound && b.colorIndex == 5 && near(b.scale, 1.25f, 1e-3f) && near(b.transparency, 0.20f, 1e-3f) && near(b.distance, 0.85f, 1e-3f));
        PanelState d; CHECK("garbage is ignored", !settingsFromText("hello\nsound=7\nscale=9.9\ncolor=44\ntransparency=abc\ndistance=0.1\n", d) && d.sound && d.colorIndex == 0 && near(d.scale, 1.0f, 1e-4f) && d.transparency == 0.0f && near(d.distance, 1.15f, 1e-4f));
    }
    {   PanelState a; a.speedMul = 3.7f; a.jumpMul = 2.3f; a.lowGravPct = 45; a.highGravPct = 75;
        a.speedOn = true; a.jumpOn = true; a.gravityMode = 2;
        const std::string txt = settingsToText(a);
        PanelState b; const bool ok = settingsFromText(txt, b);
        CHECK("movement slider values are saved and come back exactly", ok && near(b.speedMul, 3.7f, 1e-3f) && near(b.jumpMul, 2.3f, 1e-3f) && b.lowGravPct == 45 && b.highGravPct == 75);
        CHECK("the switches are NEVER saved: everything starts OFF next time", !b.speedOn && !b.jumpOn && b.gravityMode == 0);
        CHECK("nothing about on/off is in the saved text", txt.find("speedon") == std::string::npos && txt.find("jumpon") == std::string::npos && txt.find("mode") == std::string::npos && txt.find("enabled") == std::string::npos);
        PanelState g; settingsFromText("speed=9\njump=0.5\nlowgravity=95\nhighgravity=-5\nspeed=abc\n", g);
        CHECK("out-of-range or garbage movement values are ignored (defaults stay)", near(g.speedMul, 1.1f, 1e-4f) && near(g.jumpMul, 1.1f, 1e-4f) && g.lowGravPct == 0 && g.highGravPct == 0);
        PanelState e; settingsFromText("speed=5.0\njump=1.1\nlowgravity=90\nhighgravity=0\n", e);
        CHECK("the exact end values are accepted", near(e.speedMul, 5.0f, 1e-4f) && near(e.jumpMul, 1.1f, 1e-4f) && e.lowGravPct == 90 && e.highGravPct == 0);
        PanelState o2; settingsFromText("speed=3.14\nlowgravity=47\n", o2);
        CHECK("a hand-edited value is snapped to a real step (3.1x, 45%)", near(o2.speedMul, 3.1f, 1e-3f) && o2.lowGravPct == 45);
    }
    {   PanelState a; a.aimCapM = 23; a.aimOn = true;
        const std::string txt = settingsToText(a);
        PanelState b; const bool ok = settingsFromText(txt, b);
        CHECK("the aimbot distance is saved and comes back exactly (23 m)", ok && b.aimCapM == 23.0f && txt.find("aimdistance=23\n") != std::string::npos);
        CHECK("the Aimbot SWITCH is never saved: it starts OFF next time", !b.aimOn && txt.find("aimon") == std::string::npos && txt.find("aimbot") == std::string::npos);
        PanelState u; u.aimCapM = 50; CHECK("Unlimited (50) is saved as aimdistance=50", settingsToText(u).find("aimdistance=50\n") != std::string::npos);
        PanelState g; settingsFromText("aimdistance=3\naimdistance=99\naimdistance=abc\n", g);
        CHECK("out-of-range or garbage distances are ignored (stays Unlimited)", g.aimCapM == 50.0f);
        PanelState e1, e2; settingsFromText("aimdistance=5\n", e1); settingsFromText("aimdistance=50\n", e2);
        CHECK("the exact end values 5 and 50 are accepted", e1.aimCapM == 5.0f && e2.aimCapM == 50.0f);
        PanelState o3; settingsFromText("aimdistance=23.6\n", o3);
        CHECK("a hand-edited 23.6 is snapped to a whole metre (24)", o3.aimCapM == 24.0f);
        PanelState old; const bool okOld = settingsFromText("sound=0\ncolor=2\nspeed=2.0\n", old);
        CHECK("an older settings file without the aimbot line leaves the default (Unlimited)", okOld && old.aimCapM == 50.0f && !old.aimOn);
    }
    {   PanelState a; a.aimHoldY = false;
        const std::string txt = settingsToText(a);
        PanelState b; const bool ok = settingsFromText(txt, b);
        CHECK("'Hold Y to aim' OFF is saved and comes back off", ok && !b.aimHoldY && txt.find("aimy=0\n") != std::string::npos);
        PanelState c; CHECK("ON is saved as aimy=1 and is the default", c.aimHoldY && settingsToText(c).find("aimy=1\n") != std::string::npos);
        PanelState d; settingsFromText("aimy=7\naimy=abc\n", d);
        CHECK("garbage in that line is ignored (stays ON)", d.aimHoldY);
        PanelState old; settingsFromText("sound=0\naimdistance=20\n", old);
        CHECK("an older settings file without the line leaves 'Hold Y to aim' ON", old.aimHoldY && old.aimCapM == 20.0f);
        PanelState d8b; settingsFromText("aimdistance=30\naimjump=0\n", d8b);
        CHECK("the line the previous test version wrote (aimjump=0) is ignored, and nothing else is lost", d8b.aimHoldY && d8b.aimCapM == 30.0f);
    }
    {   PanelState o; settingsFromText("transparency=0.60\n", o);
        CHECK("an old saved transparency of 0.60 is pulled down to the new maximum 0.25", near(o.transparency, 0.25f, 1e-4f)); }
    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
