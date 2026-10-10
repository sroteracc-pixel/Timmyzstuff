// Tests for the Aimbot RULES and MATH (aimbot.cpp). PC only. Nothing here touches a game.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "aimbot.h"
using namespace tzaim;

static int passed = 0, failed = 0;
#define CHECK(name, ...) do { if (__VA_ARGS__) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static const float kPi = 3.14159265f;
// A shot thrown from `p` towards flat direction (dx,dz) with upward angle `elev` degrees at `speed` m/s.
static Shot shotAt(Vec3 p, float dx, float dz, float elevDeg, float speed) {
    const float n = std::sqrt(dx * dx + dz * dz), e = elevDeg * kPi / 180.0f;
    Shot s; s.pos = p; s.vel = Vec3{dx / n * speed * std::cos(e), speed * std::sin(e), dz / n * speed * std::cos(e)};
    return s;
}
// Independent check of the maths: fly a ball with plain steps (no formulas from the solver) and see where it crosses the hoop's height on the way down.
struct Flight { bool crossed; float missM; float entryDeg; };
static Flight fly(Vec3 from, Vec3 vel, Vec3 to, float g) {
    // plain small steps in double precision (exact for constant gravity)
    const double dt = 0.0002, G = g;
    double px = from.x, py = from.y, pz = from.z, vx = vel.x, vy = vel.y, vz = vel.z;
    Flight f{false, 1e9f, 0};
    for (long i = 0; i < 2000000; ++i) {
        const double qx = px, qy = py, qz = pz;
        px += vx * dt; pz += vz * dt; py += vy * dt - 0.5 * G * dt * dt; vy -= G * dt;
        if (vy < 0 && qy >= to.y && py < to.y) {          // crossed the hoop's height going down
            const double k = (qy - to.y) / (qy - py);
            const double cx = qx + (px - qx) * k, cz = qz + (pz - qz) * k;
            f.crossed = true; f.missM = (float)std::sqrt((cx - to.x) * (cx - to.x) + (cz - to.z) * (cz - to.z));
            f.entryDeg = (float)(std::atan2(-vy, std::sqrt(vx * vx + vz * vz)) * 180.0 / 3.14159265358979);
            return f;
        }
        if (py < from.y - 200) break;
    }
    return f;
}

int main() {
    std::printf("== the distance slider\n");
    CHECK("slider runs 5 m ... 50 m, one metre per step", kCapMinM == 5.0f && kCapMaxM == 50.0f && kCapStepM == 1.0f);
    CHECK("50 is 'Unlimited'", capLabel(50.0f) == "Unlimited" && isUnlimited(50.0f));
    CHECK("49 is a real number, not unlimited", capLabel(49.0f) == "49 m" && !isUnlimited(49.0f));
    CHECK("5 shows as 5 m", capLabel(5.0f) == "5 m");
    CHECK("23 shows as 23 m", capLabel(23.0f) == "23 m");
    CHECK("snapping: 23.4 -> 23, 23.6 -> 24, 2 -> 5, 90 -> 50", snapCap(23.4f) == 23.0f && snapCap(23.6f) == 24.0f && snapCap(2.0f) == 5.0f && snapCap(90.0f) == 50.0f);
    CHECK("snapping a broken number gives the default (Unlimited)", snapCap(std::nanf("")) == kCapDefaultM);
    CHECK("46 distinct slider positions (5, 6, ... 50)", [] { int n = 0; for (float c = kCapMinM; c <= kCapMaxM + 1e-3f; c += kCapStepM) ++n; return n == 46; }());

    std::printf("== when the aimbot acts\n");
    const Vec3 hoopA{0, 3.05f, 10}, hoopB{0, 3.05f, -10};
    const Vec3 hoops[2] = {hoopA, hoopB};
    const Vec3 hand{0, 1.6f, 0};
    {   const Shot s = shotAt(hand, 0, 1, 50, 8);                      // thrown towards hoop A (10 m away)
        CHECK("switch off -> never acts", decide(false, 50, s, hoops, 2).verdict == Verdict::Off);
        Decision d = decide(true, 50, s, hoops, 2);
        CHECK("shot towards a hoop, Unlimited -> acts, picks that hoop", d.verdict == Verdict::Ok && d.hoop == 0 && near(d.distanceM, 10.0f, 1e-3f));
        const Shot s2 = shotAt(hand, 0, -1, 50, 8);
        CHECK("shot the other way -> picks the other hoop", decide(true, 50, s2, hoops, 2).hoop == 1);
        const Shot s3 = shotAt(hand, 0.3f, 1, 50, 8);
        CHECK("a little off to the side still means that hoop", decide(true, 50, s3, hoops, 2).verdict == Verdict::Ok);
        const Shot s4 = shotAt(hand, 1, 0, 50, 8);
        CHECK("thrown sideways (90 degrees away from both hoops) -> no hoop in that direction, nothing changes", decide(true, 50, s4, hoops, 2).verdict == Verdict::WrongDirection);
    }
    std::printf("== the distance cap\n");
    {   const Shot s = shotAt(hand, 0, 1, 50, 8);       // 10 m from hoop A
        CHECK("cap 9 m, hoop 10 m away -> does NOT activate", decide(true, 9, s, hoops, 2).verdict == Verdict::TooFar);
        CHECK("cap 10 m, hoop exactly 10 m away -> activates (the limit itself is allowed)", decide(true, 10, s, hoops, 2).verdict == Verdict::Ok);
        CHECK("cap 11 m -> activates", decide(true, 11, s, hoops, 2).verdict == Verdict::Ok);
        CHECK("cap 5 m (the lowest) -> does NOT activate from 10 m", decide(true, 5, s, hoops, 2).verdict == Verdict::TooFar);
        CHECK("cap 49 m, hoop 10 m -> activates", decide(true, 49, s, hoops, 2).verdict == Verdict::Ok);
        const Vec3 farHoop[1] = {{0, 3.05f, 400}};
        const Shot far = shotAt(hand, 0, 1, 50, 8);
        CHECK("cap 49 m, hoop 400 m away -> does NOT activate", decide(true, 49, far, farHoop, 1).verdict == Verdict::TooFar);
        CHECK("Unlimited (50), hoop 400 m away -> still activates (no distance check at all)", decide(true, 50, far, farHoop, 1).verdict == Verdict::Ok);
        CHECK("TooFar still tells which hoop and how far (for the facts file)", [&] { const Decision d = decide(true, 9, s, hoops, 2); return d.hoop == 0 && near(d.distanceM, 10.0f, 1e-3f); }());
    }
    {   // The distance is measured along the floor: height does not count.
        const Vec3 high[1] = {{0, 30.0f, 10}};
        CHECK("the distance is flat (along the floor): a hoop 10 m away is 10 m even if it is high up", decide(true, 10, shotAt(hand, 0, 1, 50, 8), high, 1).verdict == Verdict::Ok);
    }
    {   // Two hoops, the aimed one is the far one: the cap judges the AIMED hoop, not the nearest.
        const Vec3 two[2] = {{0, 3.05f, 4}, {10, 3.05f, 20}};
        const Shot s = shotAt(Vec3{10, 1.6f, 0}, 0, 1, 50, 9);       // thrown straight at the far hoop (20 m), the near one is 6.3 m away but 68 degrees off
        const Decision d = decide(true, 15, s, two, 2);
        CHECK("aiming at the far hoop with a cap shorter than that hoop -> does NOT activate (the near hoop in the corner does not rescue it)", d.verdict == Verdict::TooFar && d.hoop == 1);
    }
    std::printf("== passes, drops and bad numbers are left alone\n");
    {   CHECK("a flat pass (5 degrees) is not a shot", decide(true, 50, shotAt(hand, 0, 1, 5, 9), hoops, 2).verdict == Verdict::NotAShot);
        CHECK("a slow drop is not a shot", decide(true, 50, shotAt(hand, 0, 1, 60, 1), hoops, 2).verdict == Verdict::NotAShot);
        CHECK("a throw straight down is not a shot", decide(true, 50, shotAt(hand, 0, 1, -60, 8), hoops, 2).verdict == Verdict::NotAShot);
        Shot up; up.pos = hand; up.vel = Vec3{0, 9, 0};
        CHECK("a throw straight up has no direction -> no hoop in the direction", decide(true, 50, up, hoops, 2).verdict == Verdict::WrongDirection);
        Shot bad = shotAt(hand, 0, 1, 50, 8); bad.vel.x = std::nanf("");
        CHECK("a NaN velocity -> never acts", decide(true, 50, bad, hoops, 2).verdict == Verdict::NotAShot);
        Shot huge = shotAt(hand, 0, 1, 50, 8); huge.vel.y = 1e9f;
        CHECK("an absurd velocity -> never acts", decide(true, 50, huge, hoops, 2).verdict == Verdict::NotAShot);
        CHECK("no hoops known -> nothing to do", decide(true, 50, shotAt(hand, 0, 1, 50, 8), nullptr, 0).verdict == Verdict::NoHoop);
        const Vec3 under[1] = {{0, 3.05f, 0.1f}};
        CHECK("standing right under the only hoop -> nothing to aim at", decide(true, 50, shotAt(hand, 0, 1, 50, 8), under, 1).verdict == Verdict::NoHoop);
    }
    std::printf("== the launch velocity (checked by flying the ball step by step, not with the solver's own formula)\n");
    {   SolveParams p; p.gravity = 9.81f;
        const Vec3 from{0, 1.6f, 0}, to{0, 3.05f, 10};
        const Solution s = solve(from, to, p);
        CHECK("a normal shot from 10 m has a solution", s.ok);
        const Flight f = fly(from, s.vel, to, p.gravity);
        CHECK("the ball crosses the hoop's height going DOWN", f.crossed);
        CHECK("... within 1 cm of the middle of the ring", f.crossed && f.missM < 0.01f);
        CHECK("... coming down at the angle the solver promised", f.crossed && near(f.entryDeg, s.entryDeg, 0.2f));
        CHECK("the ball comes down steeply enough to fit through (>= 38 degrees)", s.entryDeg >= p.minEntryDeg - 0.01f);
        CHECK("the solver's flight time matches the step-by-step flight", [&] { // time to cross, by stepping
            float t = 0; Vec3 pp = from, v = s.vel; const float dt = 0.0005f;
            while (t < 20) { const float oy = pp.y; pp.y += v.y * dt - 0.5f * p.gravity * dt * dt; v.y -= p.gravity * dt; t += dt; if (v.y < 0 && oy >= to.y && pp.y < to.y) break; }
            return std::fabs(t - s.flightSeconds) < 0.01f; }());
        const Vec3 dir{s.vel.x, 0, s.vel.z};
        CHECK("the ball is sent towards the hoop (sideways part points at it)", s.vel.x == 0.0f && s.vel.z > 0.0f && dir.z > 0);
    }
    {   SolveParams p; const Vec3 from{3, 1.5f, -2}, to{-6, 3.05f, 14};   // diagonal
        const Solution s = solve(from, to, p);
        const Flight f = fly(from, s.vel, to, p.gravity);
        CHECK("a diagonal shot also lands in the middle (within 1 cm)", s.ok && f.crossed && f.missM < 0.01f);
    }
    {   SolveParams p; p.preferredLaunchDeg = 50;
        const Solution s = solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 10}, p);
        CHECK("when the player's own arc (50 degrees) works, it is kept", s.ok && near(s.launchDeg, 50.0f, 0.01f));
    }
    {   SolveParams p; p.preferredLaunchDeg = 36;       // a flat throw would come down too shallow from 10 m ... check what the solver does
        const Solution s = solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 10}, p);
        CHECK("a flat arc is made steeper when it would come down too flat", s.ok && s.entryDeg >= p.minEntryDeg - 0.01f && s.launchDeg >= 36.0f);
    }
    {   SolveParams p;
        const Solution s = solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 0.01f}, p);
        CHECK("straight under the hoop -> refuses (no division by zero)", !s.ok);
    }
    {   SolveParams p; p.gravity = 0;
        CHECK("a gravity of 0 or less -> refuses", !solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 10}, p).ok);
        p.gravity = -9.81f; CHECK("a negative gravity number -> refuses", !solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 10}, p).ok);
        p.gravity = std::nanf(""); CHECK("a NaN gravity -> refuses", !solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 10}, p).ok);
    }
    {   SolveParams p; p.maxSpeed = 5;
        const Solution s = solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, 30}, p);
        CHECK("if the throw would have to be faster than the maximum -> refuses (the ball is not launched like a rocket)", !s.ok && std::strlen(s.why) > 0);
    }
    {   SolveParams p;
        const Solution s = solve(Vec3{0, 1.6f, 0}, Vec3{std::nanf(""), 3.05f, 10}, p);
        CHECK("NaN positions -> refuses", !s.ok);
    }
    {   // Works for every metre of the slider, at several heights, and in light and heavy gravity.
        int bad = 0, total = 0, refused = 0, refusedOk = 0; float worst = 0;
        for (float g : {3.0f, 9.81f, 20.0f})
            for (float h : {-1.0f, 0.0f, 1.45f, 3.0f})
                for (int D = 1; D <= 50; ++D) {
                    SolveParams p; p.gravity = g; p.maxSpeed = 200;
                    const Vec3 from{0, 1.6f, 0}, to{D * 0.6f, 1.6f + h, D * 0.8f};      // distance D along the floor
                    const Solution s = solve(from, to, p);
                    ++total;
                    if (!s.ok) {                      // only allowed when the hoop is really too high and too close to come down steeply from a normal throw
                        ++refused;
                        if (h >= 3.0f && D <= 1 && std::strstr(s.why, "steeply")) ++refusedOk;
                        continue;
                    }
                    const Flight f = fly(from, s.vel, to, g);
                    if (!f.crossed || f.missM > 0.01f || f.entryDeg < p.minEntryDeg - 0.2f) { ++bad; continue; }
                    if (f.missM > worst) worst = f.missM;
                }
        std::printf("       (%d solutions: %d flown step by step, worst miss %.5f m; %d refused as impossible geometry)\n", total, total - refused, worst, refused);
        CHECK("every distance 1-50 m, 4 heights, 3 gravities: a hit within 1 cm and a steep enough entry", bad == 0 && total - refused > 500);
        CHECK("the only refusals are 'hoop 3 m above the ball and 1 m away' (cannot come down steeply enough), 3 of them", refused == 3 && refusedOk == 3);
    }
    {   // With the normal gravity and the default maximum speed, every metre of the slider is reachable.
        int notOk = 0;
        for (int D = 1; D <= 50; ++D) { SolveParams p; if (!solve(Vec3{0, 1.6f, 0}, Vec3{0, 3.05f, (float)D}, p).ok) ++notOk; }
        CHECK("normal gravity, default limits: all 50 distances have a solution", notOk == 0);
    }
    std::printf("== random shots: the cap rule never contradicts itself\n");
    {   std::srand(12345);
        int contradictions = 0, ok = 0, tooFar = 0;
        for (int i = 0; i < 20000; ++i) {
            const Vec3 hs[3] = {{(float)(std::rand() % 40 - 20), 3.05f, (float)(std::rand() % 40 - 20)}, {(float)(std::rand() % 40 - 20), 3.05f, (float)(std::rand() % 40 - 20)}, {0, 3.05f, 15}};
            const Shot s = shotAt(Vec3{(float)(std::rand() % 30 - 15), 1.6f, (float)(std::rand() % 30 - 15)}, (float)(std::rand() % 21 - 10) + 0.5f, (float)(std::rand() % 21 - 10) + 0.5f, 15.0f + std::rand() % 60, 2.0f + std::rand() % 12);
            const int cap = 5 + std::rand() % 46;
            const Decision a = decide(true, (float)cap, s, hs, 3), u = decide(true, 50, s, hs, 3);
            if (a.verdict == Verdict::Ok) ++ok;
            if (a.verdict == Verdict::TooFar) ++tooFar;
            // Unlimited decides the same hoop, and everything but the cap is the same
            if (a.verdict == Verdict::TooFar && u.verdict != Verdict::Ok) ++contradictions;
            if (a.verdict == Verdict::Ok && u.verdict != Verdict::Ok) ++contradictions;
            if (a.verdict == Verdict::Ok && a.hoop != u.hoop) ++contradictions;
            if (a.verdict == Verdict::TooFar && !(a.distanceM > cap)) ++contradictions;
            if (a.verdict == Verdict::Ok && !isUnlimited((float)cap) && a.distanceM > cap) ++contradictions;
            // a bigger cap never turns an Ok into TooFar
            if (cap < 50) { const Decision b = decide(true, (float)(cap + 1), s, hs, 3); if (a.verdict == Verdict::Ok && b.verdict != Verdict::Ok) ++contradictions; }
        }
        std::printf("       (20000 random shots: %d acted, %d refused as too far)\n", ok, tooFar);
        CHECK("Unlimited never refuses for distance; the cap only ever removes shots; a bigger cap never removes one", contradictions == 0 && ok > 1000 && tooFar > 1000);
    }
    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
