// aimbot.cpp - see aimbot.h.
#include "aimbot.h"

#include <cmath>
#include <cstdio>

namespace tzaim {

namespace {
const float kPi = 3.14159265358979f;
float rad(float deg) { return deg * kPi / 180.0f; }
float deg(float r) { return r * 180.0f / kPi; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

bool isUnlimited(float capM) { return capM >= kCapMaxM - 0.5f; }

float snapCap(float capM) {
    if (!(capM == capM)) return kCapDefaultM;                         // NaN
    return clampf(std::floor(capM / kCapStepM + 0.5f) * kCapStepM, kCapMinM, kCapMaxM);
}

std::string capLabel(float capM) {
    if (isUnlimited(capM)) return "Unlimited";
    char b[24];
    std::snprintf(b, sizeof b, "%d m", static_cast<int>(snapCap(capM)));
    return b;
}

const char* verdictText(Verdict v) {
    switch (v) {
    case Verdict::Off: return "aimbot is off";
    case Verdict::NotAShot: return "not a shot (too slow or too flat)";
    case Verdict::NoHoop: return "no hoop known";
    case Verdict::WrongDirection: return "no hoop in the direction of the throw";
    case Verdict::TooFar: return "hoop is farther than the max shot distance";
    case Verdict::Ok: return "aiming at the hoop";
    }
    return "?";
}

float flatDistance(const Vec3& a, const Vec3& b) {
    const float dx = b.x - a.x, dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

Decision decide(bool on, float capM, const Shot& shot, const Vec3* hoops, int n, const Rules& rules) {
    Decision d;
    const float vx = shot.vel.x, vy = shot.vel.y, vz = shot.vel.z;
    const float flat = std::sqrt(vx * vx + vz * vz);
    d.speed = std::sqrt(vx * vx + vy * vy + vz * vz);
    d.elevationDeg = deg(std::atan2(vy, flat));
    if (!on) { d.verdict = Verdict::Off; return d; }
    // Bad numbers (NaN / infinity) from a misread game: never act on them.
    if (!(d.speed == d.speed) || d.speed > 1e6f) { d.verdict = Verdict::NotAShot; return d; }
    if (d.speed < rules.minShotSpeed || d.elevationDeg < rules.minShotElevationDeg) { d.verdict = Verdict::NotAShot; return d; }

    int best = -1; float bestAngle = 1e9f, bestDist = 0;
    for (int i = 0; hoops && i < n; ++i) {
        const float dx = hoops[i].x - shot.pos.x, dz = hoops[i].z - shot.pos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (!(dist == dist) || dist < rules.minHoopDistanceM) continue;
        float angle = 180.0f;                                    // a throw straight up has no direction: worst possible
        if (flat > 0.1f) {
            const float c = clampf((vx * dx + vz * dz) / (flat * dist), -1.0f, 1.0f);
            angle = deg(std::acos(c));
        }
        // smallest angle wins; if two are (almost) equally well aimed, the nearer one
        if (angle < bestAngle - 0.5f || (std::fabs(angle - bestAngle) <= 0.5f && best >= 0 && dist < bestDist)) { best = i; bestAngle = angle; bestDist = dist; }
    }
    if (best < 0) { d.verdict = Verdict::NoHoop; return d; }
    d.hoop = best; d.angleDeg = bestAngle; d.distanceM = bestDist;
    if (bestAngle > rules.maxAimAngleDeg) { d.verdict = Verdict::WrongDirection; return d; }
    if (!isUnlimited(capM) && bestDist > capM) { d.verdict = Verdict::TooFar; return d; }
    d.verdict = Verdict::Ok;
    return d;
}

Solution solve(const Vec3& from, const Vec3& to, const SolveParams& p) {
    Solution s;
    const float g = p.gravity;
    if (!(g > 0.1f) || g > 100.0f) { s.why = "gravity value is not usable"; return s; }
    const float dx = to.x - from.x, dz = to.z - from.z, h = to.y - from.y;
    const float D = std::sqrt(dx * dx + dz * dz);
    if (!(D == D) || !(h == h)) { s.why = "positions are not numbers"; return s; }
    if (D < 0.05f) { s.why = "the hoop is straight above or below the ball"; return s; }

    const float lo = rad(clampf(p.minLaunchDeg, 5.0f, 85.0f)), hi = rad(clampf(p.maxLaunchDeg, 5.0f, 89.0f));
    float theta = clampf(rad(p.preferredLaunchDeg), lo, hi);
    // The ball must arrive at the ring steeply enough: tan(entry) = tan(launch) - 2h/D. If the thrown arc is too flat, make it steeper.
    const float tanNeeded = std::tan(rad(clampf(p.minEntryDeg, 1.0f, 85.0f))) + 2.0f * h / D;
    if (std::tan(theta) < tanNeeded) theta = std::atan(tanNeeded);
    if (!(theta <= hi + 1e-6f)) { s.why = "cannot come down steeply enough within the maximum launch angle"; return s; }

    const float t = std::tan(theta), c = std::cos(theta), sn = std::sin(theta);
    const float rise = D * t - h;                                    // always > 0 here (the entry rule above guarantees it)
    if (!(rise > 1e-4f)) { s.why = "the hoop cannot be reached with this launch angle"; return s; }
    const float v2 = g * D * D / (2.0f * c * c * rise);
    const float v = std::sqrt(v2);
    if (!(v == v) || v > p.maxSpeed) { s.why = "would need a faster throw than the maximum"; return s; }

    s.ok = true;
    s.speed = v;
    s.launchDeg = deg(theta);
    s.entryDeg = deg(std::atan(t - 2.0f * h / D));
    s.vel.x = dx / D * v * c; s.vel.z = dz / D * v * c; s.vel.y = v * sn;
    s.flightSeconds = D / (v * c);
    return s;
}

}  // namespace tzaim
