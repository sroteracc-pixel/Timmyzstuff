// pointer.cpp - see pointer.h.
#include "pointer.h"

#include <cmath>

namespace tzpanel {

namespace {
const float kPi = 3.14159265f;
void rotate(const float q[4], const float v[3], float out[3]) {     // q = x,y,z,w
    const float tx = 2 * (q[1] * v[2] - q[2] * v[1]);
    const float ty = 2 * (q[2] * v[0] - q[0] * v[2]);
    const float tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
bool inside(float x, float y) { return x >= 0 && y >= 0 && x < kWidth && y < kHeight; }
int hitAt(const std::vector<HitRect>& hits, float x, float y) {
    for (const HitRect& h : hits) if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return h.id;
    return -1;
}
}  // namespace

Placement placeInFront(const float head[7], float scale, float distance) {
    Placement p;
    const float qx = head[0], qy = head[1], qz = head[2], qw = head[3];
    float fx = -2.0f * (qx * qz + qw * qy), fz = -(1.0f - 2.0f * (qx * qx + qy * qy));   // where the head looks (flat)
    const float len = std::sqrt(fx * fx + fz * fz);
    if (len < 1e-4f) { fx = 0; fz = -1; } else { fx /= len; fz /= len; }
    p.yaw = std::atan2(-fx, -fz);
    const float dist = distance < kDistMin ? kDistMin : (distance > kDistMax ? kDistMax : distance);
    p.pos[0] = head[4] + fx * dist; p.pos[1] = head[5]; p.pos[2] = head[6] + fz * dist;
    const float sc = scale < kScaleMin ? kScaleMin : (scale > kScaleMax ? kScaleMax : scale);
    p.width = 0.95f * sc; p.height = p.width * kHeight / kWidth;
    return p;
}

Ray rayFromPose(const float pose[7], float pitchDeg) {
    Ray r;
    const float a = pitchDeg * kPi / 180.0f;
    const float local[3] = {0.0f, std::sin(a), -std::cos(a)};
    rotate(pose, local, r.d);
    r.o[0] = pose[4]; r.o[1] = pose[5]; r.o[2] = pose[6];
    return r;
}

bool intersect(const Ray& r, const Placement& p, float* px, float* py) {
    const float nx = std::sin(p.yaw), nz = std::cos(p.yaw);           // surface normal (faces the player)
    const float rx = std::cos(p.yaw), rz = -std::sin(p.yaw);          // picture's "right"
    const float denom = r.d[0] * nx + r.d[2] * nz;
    if (denom > -1e-4f) return false;                                  // parallel, or pointing away
    const float t = ((p.pos[0] - r.o[0]) * nx + (p.pos[2] - r.o[2]) * nz) / denom;
    if (t <= 0) return false;
    const float hx = r.o[0] + r.d[0] * t - p.pos[0], hy = r.o[1] + r.d[1] * t - p.pos[1], hz = r.o[2] + r.d[2] * t - p.pos[2];
    const float u = hx * rx + hz * rz;
    if (px) *px = (u / p.width + 0.5f) * kWidth;
    if (py) *py = (0.5f - hy / p.height) * kHeight;
    return true;
}

void Interaction::reset() { armed_ = false; down_ = false; dragging_ = false; dragHand_ = -1; dragId_ = 0; }

void Interaction::dragTo(float x, PanelState& s, const std::vector<HitRect>& hits, Outcome& o) {
    float lo, hi, step;
    if (!sliderRange(dragId_, &lo, &hi, &step)) return;
    for (const HitRect& h : hits) {
        if (h.id != dragId_) continue;
        const float tx0 = h.x + 16, tx1 = h.x + h.w - 16;
        float t = (x - tx0) / (tx1 - tx0);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float v = snapSlider(dragId_, lo + (hi - lo) * t);
        float& value = sliderValue(s, dragId_);
        if (std::fabs(v - value) > 1e-4f) { value = v; o.redraw = true; }
        return;
    }
}

void Interaction::press(int id, float x, PanelState& s, const std::vector<HitRect>& hits, Outcome& o, int hand) {
    o.clickedId = id;
    if (id == HIT_CLOSE) { o.close = true; return; }
    if (id >= HIT_TAB0 && id < HIT_TAB0 + kTabCount) { s.tab = id - HIT_TAB0; o.redraw = true; return; }
    if (id >= HIT_COLOR0 && id < HIT_COLOR0 + kColorCount) {
        if (s.colorIndex != id - HIT_COLOR0) { s.colorIndex = id - HIT_COLOR0; o.saveNeeded = true; }
        o.redraw = true; return;
    }
    float lo, hi, step;
    if (sliderRange(id, &lo, &hi, &step)) {
        dragging_ = true; dragHand_ = hand; dragId_ = id; s.dragSlider = id;
        dragTo(x, s, hits, o); o.redraw = true; return;
    }
    switch (id) {
    case HIT_SOUND: s.sound = !s.sound; o.redraw = o.saveNeeded = true; break;
    // Movement switches. Gravity is ONE setting (off / low / high), so the two modes can never both be on.
    // None of these are saved: the game always starts with every movement effect off.
    case HIT_TOGGLE_SPEED: s.speedOn = !s.speedOn; o.redraw = true; break;
    case HIT_TOGGLE_JUMP: s.jumpOn = !s.jumpOn; o.redraw = true; break;
    case HIT_TOGGLE_LOWGRAV: s.gravityMode = (s.gravityMode == 1) ? 0 : 1; o.redraw = true; break;
    case HIT_TOGGLE_HIGHGRAV: s.gravityMode = (s.gravityMode == 2) ? 0 : 2; o.redraw = true; break;
    case HIT_SCAN:
        if (s.scanState != 1) { s.scanState = 1; o.scanRequested = true; }
        o.redraw = true; break;
    // Basketball page. The Aimbot switch is never saved: the game always starts with it off.
    // Only one aimbot mode can be on: turning one on turns the other one off (the same click).
    case HIT_TOGGLE_AIM: s.aimOn = !s.aimOn; if (s.aimOn) s.aimBank = false; o.redraw = true; break;
    case HIT_TOGGLE_AIMBANK: s.aimBank = !s.aimBank; if (s.aimBank) s.aimOn = false; o.redraw = true; break;
    case HIT_TOGGLE_AIMY: s.aimHoldY = !s.aimHoldY; o.redraw = o.saveNeeded = true; break;      // this one IS saved (it is a setting, not an effect)
    case HIT_SCAN_SHOT:
        if (s.scanState != 1) { s.scanState = 1; o.shotScanRequested = true; }
        o.redraw = true; break;
    case HIT_TEST: ++s.testClicks; o.redraw = true; break;
    default: break;
    }
}

Outcome Interaction::update(PanelState& s, const std::vector<HitRect>& hits, const HandAim hands[2]) {
    Outcome o;
    const float maxTrigger = hands[0].trigger > hands[1].trigger ? hands[0].trigger : hands[1].trigger;
    if (!armed_ && maxTrigger < 0.35f) armed_ = true;

    // which controller is the pointer?
    int h = -1;
    if (dragging_) h = dragHand_;
    else {
        bool cand[2];
        for (int i = 0; i < 2; ++i) cand[i] = hands[i].valid && hands[i].onPlane && inside(hands[i].x, hands[i].y);
        if (cand[1] && hands[1].trigger > 0.65f) h = 1;
        else if (cand[0] && hands[0].trigger > 0.65f) h = 0;
        else if (cand[1]) h = 1;
        else if (cand[0]) h = 0;
    }
    o.hand = h;

    float x = lastX_, y = 0;
    bool haveAim = false;
    if (h >= 0 && hands[h].valid && hands[h].onPlane) { x = hands[h].x; y = hands[h].y; lastX_ = x; haveAim = true; }
    o.cursorVisible = haveAim && (inside(x, y) || dragging_);
    o.cursorX = x; o.cursorY = y;

    // what is under the dot?
    int hover = -1;
    if (dragging_) hover = dragId_;
    else if (haveAim && inside(x, y)) hover = hitAt(hits, x, y);
    if (hover != s.hover) { s.hover = hover; o.redraw = true; }

    // trigger -> press / release
    bool down = false;
    if (h >= 0 && hands[h].valid && armed_) down = down_ ? hands[h].trigger > 0.35f : hands[h].trigger > 0.65f;
    if (down && !down_ && !dragging_ && hover >= 0) press(hover, x, s, hits, o, h);
    if (dragging_) {
        if (down) dragTo(x, s, hits, o);
        else {
            const int id = dragId_;
            dragging_ = false; dragHand_ = -1; dragId_ = 0; s.dragSlider = 0;
            o.layoutCommitted = (id == HIT_SLIDER_SIZE || id == HIT_SLIDER_DIST);
            o.saveNeeded = true; o.redraw = true;
        }
    }
    down_ = down;
    o.pressed = down;
    return o;
}

}  // namespace tzpanel
