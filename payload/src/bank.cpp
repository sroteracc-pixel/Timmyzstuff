// bank.cpp - see bank.h.
#include "bank.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace tzbank {

namespace {

struct V { double x = 0, y = 0, z = 0; };
inline V operator+(const V& a, const V& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(const V& a, const V& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(const V& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V cross(const V& a, const V& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(const V& a) { return std::sqrt(dot(a, a)); }
inline V unit(const V& a) { const double l = len(a); return l > 1e-12 ? a * (1.0 / l) : V{0, 0, 0}; }
inline V fromVec(const Vec3& v) { return {v.x, v.y, v.z}; }
inline Vec3 toVec(const V& v) { return Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
inline bool finiteV(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
const double kPi = 3.14159265358979323846;
inline double rad2deg(double r) { return r * 180.0 / kPi; }

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) { char b[600]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }

// ---------------------------------------------------------------- one touch of the ball on the board
// n = unit normal pointing out of the board towards the ball. The contact point lies at -radius*n from the ball's centre.
// Normal: v_n' = e * approach (only when the approach is faster than the threshold). Tangential: Coulomb friction acts on the contact point's sliding
// speed u = v_t + w x (-r n) until it stops sliding (the impulse that stops it is |u| / (1 + 1/kappa)) or reaches mu * normal impulse.
void bounceV(const Bounce& b, const V& n, V& v, V& w, double* approachOut) {
    const double vn = dot(v, n);
    const double approach = -vn;
    if (approachOut) *approachOut = approach;
    if (approach <= 0) return;
    const double e = approach > b.threshold ? b.e : 0.0;
    V vt = v - n * vn;
    const V rvec = n * (-b.radius);
    const V u = vt + cross(w, rvec);
    const double un = len(u);
    const double jn = (1.0 + e) * approach;
    if (un > 1e-9 && b.mu > 0 && b.kappa > 1e-6) {
        const double J = std::min(b.mu * jn, un / (1.0 + 1.0 / b.kappa));
        const V Jt = u * (-J / un);
        vt = vt + Jt;
        w = w + cross(rvec, Jt) * (1.0 / (b.kappa * b.radius * b.radius));
    }
    v = vt + n * (e * approach);
}

// distance from point q to the segment a-b
double distPointSeg(const V& q, const V& a, const V& b) {
    const V ab = b - a;
    const double l2 = dot(ab, ab);
    double k = l2 > 1e-14 ? dot(q - a, ab) / l2 : 0.0;
    k = std::max(0.0, std::min(1.0, k));
    return len(q - (a + ab * k));
}

struct Geo {                 // the board and the ring in the scene's own terms
    V fc, n, t, ring;
    double r = 0;
};

SimResult runSim(const Scene& sc, const Perturb& pt, const V& pos0, const V& vel0, const V& spin0, bool keepPath, bool wantClearance) {
    SimResult out;
    const double dt = sc.flight.dt, g = sc.flight.gravity;
    const double m = 1.0 - sc.flight.drag * pt.dragScale * dt;
    const double am = std::max(0.0, 1.0 - sc.angularDrag * dt);
    const double e = std::max(0.0, std::min(1.0, sc.restitution * pt.eScale));
    const double mu = std::max(0.0, sc.friction * pt.muScale);
    const double r = sc.radius;
    const V n = unit(fromVec(sc.n)), fc = fromVec(sc.faceCentre);
    const V t = unit(cross(V{0, 1, 0}, n));
    const V R = fromVec(sc.ring);
    ContactModel model = pt.model >= 0 ? static_cast<ContactModel>(pt.model) : sc.model;
    if (model == ContactModel::Unknown) model = ContactModel::Discrete;
    const Bounce bc{e, mu, sc.kappa, r, sc.bounceThreshold};
    auto inFace = [&](const V& p) { return std::fabs(dot(p - fc, t)) <= sc.halfW && std::fabs(p.y - fc.y) <= sc.halfH; };
    // the ring as 24 points (for the clearance)
    const int kRingPts = 32;
    V circ[kRingPts];
    if (wantClearance) for (int i = 0; i < kRingPts; ++i) { const double a = 2.0 * kPi * i / kRingPts; circ[i] = {R.x + sc.ringRadius * std::cos(a), R.y, R.z + sc.ringRadius * std::sin(a)}; }
    const double tube = sc.ringTube;

    V p = pos0, v = vel0 * pt.speedScale, w = spin0 * pt.spinScale;
    if (keepPath) out.path.push_back(toVec(p));
    out.apexY = static_cast<float>(p.y);
    int inside = 0, postSteps = 0;
    bool postCross = false;            // the ball's centre has come down through the ring's height; the flight goes on a little, because the ball is wider than its centre (it can still touch the front rim)
    double minD = 1e9;
    auto segClear = [&](const V& a, const V& b) {
        if (!wantClearance) return;
        if (std::max(a.y, b.y) < R.y - 0.9 || std::min(a.y, b.y) > R.y + 0.9) return;
        for (int i = 0; i < kRingPts; ++i) minD = std::min(minD, distPointSeg(circ[i], a, b));
    };
    const int maxSteps = 520;
    int step = 0;
    for (step = 1; step <= maxSteps; ++step) {
        const V prev = p;
        v.y -= g * dt; v = v * m; w = w * am;
        V pn;
        auto doContact = [&](const V& at) {
            V vin = v;
            double ap = 0;
            bounceV(bc, n, v, w, &ap);
            if (out.contacts == 0) { out.contactStep = step; out.contactPos = toVec(at); out.vIn = toVec(vin); out.vOut = toVec(v); out.approach = static_cast<float>(ap); }
            else out.secondContact = true;
            ++out.contacts;
        };
        if (postCross) {
            pn = p + v * dt;                                  // after the ring's height: no more board (the ball is inside the ring and falling)
        } else if (model == ContactModel::Discrete) {
            const double b = dot(p - fc, n);
            if (b < r && b >= 0 && dot(v, n) < 0 && inFace(p)) {
                ++inside;
                if (inside > pt.lateSteps) { doContact(p); inside = 0; }
            } else inside = 0;
            pn = p + v * dt;
        } else {
            pn = p + v * dt;
            const double b0 = dot(p - fc, n), b1 = dot(pn - fc, n);
            if (b0 >= r && b1 < r && dot(v, n) < 0) {
                const double f = (b0 - r) / (b0 - b1);
                const V pc = p + (pn - p) * f;
                if (inFace(pc)) { doContact(pc); pn = pc + v * (dt * (1.0 - f)); }
            }
        }
        if (!postCross && dot(pn - fc, n) < 0) { out.missedBoard = true; p = pn; if (keepPath) out.path.push_back(toVec(p)); break; }
        p = pn;
        if (keepPath) out.path.push_back(toVec(p));
        out.apexY = std::max(out.apexY, static_cast<float>(p.y));
        segClear(prev, p);
        if (p.y < r) { out.hitFloor = true; break; }
        if (!out.crossed && prev.y >= R.y && p.y < R.y) {
            const double k = (prev.y - R.y) / (prev.y - p.y);
            const V cp = prev + (p - prev) * k;
            out.crossed = true; out.crossStep = step; out.crossPos = toVec(cp);
            out.miss = static_cast<float>(std::sqrt((cp.x - R.x) * (cp.x - R.x) + (cp.z - R.z) * (cp.z - R.z)));
            out.entryDeg = static_cast<float>(rad2deg(std::atan2(-v.y, std::sqrt(v.x * v.x + v.z * v.z))));
            out.crossedBeforeBounce = out.contacts == 0;
            if (!wantClearance) break;                        // (the correction does not need the rest)
            postCross = true;
        }
        if (postCross && (p.y < R.y - r - 0.03 || ++postSteps > 24)) break;       // the whole ball is below the ring: done
    }
    out.steps = std::min(step, maxSteps);
    if (wantClearance) out.clearance = static_cast<float>(minD - sc.radius - tube - 0.0025);
    return out;
}

// ---- closed forms of the game's step order (v += g*dt; v *= m; p += v*dt) for k steps, m = 1 - drag*dt
struct Closed {
    double dt, g, m;
    double S(double k) const { return m == 1.0 ? k * dt : dt * m * (1.0 - std::pow(m, k)) / (1.0 - m); }                 // how far one unit of start speed carries in k steps
    double Gy(double k) const { return m == 1.0 ? -g * dt * dt * k * (k + 1.0) / 2.0 : -g * dt * dt * (m / (1.0 - m)) * (k - m * (1.0 - std::pow(m, k)) / (1.0 - m)); }   // height drop from gravity
    double Vg(double k) const { return m == 1.0 ? -g * dt * k : -g * dt * m * (1.0 - std::pow(m, k)) / (1.0 - m); }      // speed change from gravity
};

struct Seed { int k1 = 0; double ac = 0, yc = 0, speed = 0; };

enum Rej { RJ_NOTCONV, RJ_SPEED, RJ_ENTRY, RJ_CLEAR, RJ_BOARD, RJ_DIRECT, RJ_APPROACH, RJ_APEX, RJ_LAUNCH, RJ_ROBUST, RJ_MISS, RJ_COUNT };
const char* const kRejText[RJ_COUNT] = {
    "the correction did not settle on the ring", "it needs a launch faster than the limit", "the ball would come down too flat into the ring",
    "the ball would touch the rim", "the ball would not hit the board in a good place", "the ball would reach the ring without touching the board",
    "the ball would touch the board too gently to bounce", "the lob would go too high", "the launch angle would be out of range",
    "it is too sensitive to small differences in the bounce", "the ball would miss the ring"};

}  // namespace

const char* reasonText(Reason r) {
    switch (r) {
    case Reason::None: return "";
    case Reason::BadInput: return "the game's numbers are not usable";
    case Reason::BoardData: return "the backboard and hoop data do not fit together";
    case Reason::BehindBoard: return "you are behind the backboard";
    case Reason::TooClose: return "too close to the backboard";
    case Reason::SharpAngle: return "angle too sideways to reach the board";
    case Reason::NoSolution: return "no safe bank shot from this spot";
    }
    return "";
}

void bounceOnce(const Bounce& b, const double nrm[3], double v[3], double w[3]) {
    V vv{v[0], v[1], v[2]}, ww{w[0], w[1], w[2]};
    bounceV(b, unit(V{nrm[0], nrm[1], nrm[2]}), vv, ww, nullptr);
    v[0] = vv.x; v[1] = vv.y; v[2] = vv.z; w[0] = ww.x; w[1] = ww.y; w[2] = ww.z;
}

SimResult simulate(const Scene& s, const Vec3& pos, const Vec3& vel, const Vec3& spin, bool keepPath, const Perturb& pt) {
    return runSim(s, pt, fromVec(pos), fromVec(vel), fromVec(spin), keepPath, true);
}

// ---------------------------------------------------------------- the solver
Plan solve(const Request& rq) {
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    Plan pl;
    const Scene& sc = rq.scene;
    const Limits& lim = rq.lim;
    auto fail = [&](Reason r, const std::string& why, const std::string& detail) { pl.ok = false; pl.reason = r; pl.why = why; pl.detail = detail; pl.ms = elapsed() * 1000.0; return pl; };

    // ---- the numbers must be believable (they come from the game; nothing is made up here)
    const FlightModel& fm = sc.flight;
    if (!finiteV(rq.pos) || !finiteV(rq.vel) || !finiteV(rq.spin) || !finiteV(sc.faceCentre) || !finiteV(sc.n) || !finiteV(sc.ring) ||
        !std::isfinite(fm.gravity) || !std::isfinite(fm.drag) || !std::isfinite(fm.dt) || !(fm.gravity > 0.5f && fm.gravity < 100.0f) || !(fm.dt > 0.002f && fm.dt < 0.1f) ||
        !(fm.drag >= 0.0f && fm.drag * fm.dt < 0.5f))
        return fail(Reason::BadInput, "the game's physics numbers are not usable", "physics numbers (gravity/drag/step) missing or not believable");
    if (!(sc.radius > 0.03f && sc.radius < 0.4f) || !(sc.restitution >= 0.0f && sc.restitution <= 1.2f) || !(sc.friction >= 0.0f && sc.friction < 20.0f) ||
        !(sc.kappa > 0.05f && sc.kappa < 1.0f) || !(sc.halfW > 0.2f && sc.halfW < 3.0f) || !(sc.halfH > 0.2f && sc.halfH < 3.0f) || !(sc.ringRadius > 0.1f && sc.ringRadius < 0.6f))
        return fail(Reason::BadInput, "a ball or board number from the game is missing or not believable",
                    fmt("radius %.3f, bounce %.3f, friction %.3f, kappa %.3f, board half size %.3f x %.3f, ring radius %.3f", static_cast<double>(sc.radius), static_cast<double>(sc.restitution),
                        static_cast<double>(sc.friction), static_cast<double>(sc.kappa), static_cast<double>(sc.halfW), static_cast<double>(sc.halfH), static_cast<double>(sc.ringRadius)));

    const V fc = fromVec(sc.faceCentre), n0 = fromVec(sc.n), ring = fromVec(sc.ring), p0 = fromVec(rq.pos);
    if (std::fabs(n0.y) > 0.2 || len(n0) < 0.5) return fail(Reason::BoardData, "the backboard does not face sideways", fmt("board normal (%.3f, %.3f, %.3f) is not horizontal", n0.x, n0.y, n0.z));
    const V n = unit(V{n0.x, 0, n0.z});
    const V t = unit(cross(V{0, 1, 0}, n));
    const double r = sc.radius, e = sc.restitution;
    const double a0 = dot(p0 - fc, t), b0 = dot(p0 - fc, n), y0 = p0.y;
    const double Ra = dot(ring - fc, t), Rb = dot(ring - fc, n), Ry = ring.y;
    pl.distToBoard = static_cast<float>(b0); pl.sideOffset = static_cast<float>(a0);
    if (Rb - r < 0.05 || Rb > 1.5 || std::fabs(Ra) > sc.halfW || std::fabs(Ry - fc.y) > sc.halfH + 0.8 || Ry < 1.0)
        return fail(Reason::BoardData, "the backboard and hoop data do not fit together",
                    fmt("ring is %.2f m in front of the board face (sideways %.2f, height %.2f above the face centre) - not a believable hoop", Rb, Ra, Ry - fc.y));
    if (b0 < 0.0) return fail(Reason::BehindBoard, "you are behind the backboard", fmt("the ball is %.2f m BEHIND the board's front plane", -b0));
    const double A = b0 - r, B = Rb - r;
    if (A < lim.minBoardDistanceM) return fail(Reason::TooClose, "too close to the backboard", fmt("the ball is %.2f m from the board's front plane (needs %.1f m)", b0, static_cast<double>(lim.minBoardDistanceM + sc.radius)));
    const double acSeed = (B * a0 + e * A * Ra) / (B + e * A);
    pl.planAngleDeg = static_cast<float>(rad2deg(std::atan2(std::fabs(acSeed - a0), A)));
    if (pl.planAngleDeg > lim.maxPlanAngleDeg || std::fabs(acSeed) > sc.halfW - lim.edgeMarginM)
        return fail(Reason::SharpAngle, "angle too sideways to reach the board",
                    fmt("from here the ball would have to meet the board at %.0f degrees sideways (limit %.0f) near %.2f m from the middle (board half width %.2f)",
                        static_cast<double>(pl.planAngleDeg), static_cast<double>(lim.maxPlanAngleDeg), acSeed, static_cast<double>(sc.halfW)));

    // ---- seeds: one per flight time (in physics steps) from the ideal-bounce formulas
    const double dt = fm.dt, g = fm.gravity, m = 1.0 - static_cast<double>(fm.drag) * dt;
    const Closed C{dt, g, m};
    int k1min = static_cast<int>(std::ceil(0.30 / dt)), k1max = static_cast<int>(std::floor(3.4 / dt));
    const int stride = lim.stride > 0 ? lim.stride : std::max(1, (k1max - k1min) / 150);
    std::vector<Seed> seeds;
    int rej[RJ_COUNT] = {0};
    int seedTries = 0;
    const double ac = acSeed;
    for (int k1 = k1min; k1 <= k1max; k1 += stride) {
        ++seedTries;
        const double D1 = std::sqrt((ac - a0) * (ac - a0) + A * A);
        const double H0 = D1 / C.S(k1);
        const double V1h = std::pow(m, k1) * H0;
        const double vb1 = V1h * A / D1, va1 = V1h * (ac - a0) / D1;
        if (vb1 < lim.minApproachSpeed) { ++rej[RJ_APPROACH]; continue; }
        const double Wh = std::sqrt(va1 * va1 + e * vb1 * e * vb1);
        if (Wh < 1e-6) { ++rej[RJ_APPROACH]; continue; }
        const double D2 = std::sqrt((Ra - ac) * (Ra - ac) + B * B);
        const double Sneed = D2 / Wh;
        double k2;
        if (m == 1.0) k2 = Sneed / dt;
        else {
            const double lim2 = dt * m / (1.0 - m);
            if (Sneed >= 0.98 * lim2) { ++rej[RJ_MISS]; continue; }
            k2 = std::log(1.0 - Sneed * (1.0 - m) / (dt * m)) / std::log(m);
        }
        const double vy0 = (Ry - y0 - C.Gy(k1) - C.S(k2) * C.Vg(k1) - C.Gy(k2)) / (C.S(k1) + C.S(k2) * std::pow(m, k1));
        const double yc = y0 + C.S(k1) * vy0 + C.Gy(k1);
        const double speed = std::sqrt(H0 * H0 + vy0 * vy0);
        if (speed > lim.maxSpeed * 1.4) { ++rej[RJ_SPEED]; continue; }
        if (yc < fc.y - sc.halfH + lim.edgeMarginM || yc > fc.y + sc.halfH - lim.edgeMarginM) { ++rej[RJ_BOARD]; continue; }
        const double vyCross = std::pow(m, k2) * (std::pow(m, k1) * vy0 + C.Vg(k1)) + C.Vg(k2);
        if (vyCross > -1.0) { ++rej[RJ_ENTRY]; continue; }
        Seed s; s.k1 = k1; s.ac = ac; s.yc = yc; s.speed = speed;
        seeds.push_back(s);
    }
    pl.tried = seedTries;
    std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) { return a.speed < b.speed; });

    // ---- polishing each seed with the full physics copy, then judging it
    const double thrownElev = rad2deg(std::atan2(static_cast<double>(rq.vel.y), std::sqrt(static_cast<double>(rq.vel.x) * rq.vel.x + static_cast<double>(rq.vel.z) * rq.vel.z)));
    const double minApproach = std::max(static_cast<double>(lim.minApproachSpeed), 1.15 * static_cast<double>(sc.bounceThreshold));
    const V spin = fromVec(rq.spin);
    const Perturb nominal;
    struct Cand { V v0; SimResult sim; double margin = 0, score = 0, acx = 0, ycx = 0; int pass = 0, total = 0; std::string failedWhat; };
    std::vector<Cand> valid;
    int nearRej[RJ_COUNT] = {0};
    double bestMiss = 1e9;

    auto launchFor = [&](int k1, double acx, double ycx) {
        const V Cp = fc + t * acx + n * (r - 0.003) + V{0, ycx - fc.y, 0};
        return (Cp - p0 - V{0, C.Gy(k1), 0}) * (1.0 / C.S(k1));
    };
    // The correction aims between two cases: the touch is found on time, and the touch is found one physics step late (the ball is then a little deeper
    // inside the board before it bounces). Nobody can see from outside which one the engine does, so the aim point is the middle of the two.
    Perturb late1; late1.lateSteps = 1;
    auto resid = [&](int k1, double acx, double ycx, double res[2]) -> bool {
        const V v0 = launchFor(k1, acx, ycx);
        const SimResult s = runSim(sc, nominal, p0, v0, spin, false, false);
        if (!s.crossed || s.crossedBeforeBounce || s.contacts < 1) return false;
        const SimResult q = runSim(sc, late1, p0, v0, spin, false, false);
        if (!q.crossed || q.crossedBeforeBounce || q.contacts < 1) return false;
        res[0] = 0.5 * (s.crossPos.x + q.crossPos.x) - ring.x; res[1] = 0.5 * (s.crossPos.z + q.crossPos.z) - ring.z;
        return true;
    };
    // Newton's method on the two numbers "where on the board does it touch" (sideways, height): the flight time to the board is fixed by k1
    auto polish = [&](Seed& s) -> bool {
        double x = s.ac, y = s.yc, res[2];
        if (!resid(s.k1, x, y, res)) return false;
        double nrm = std::hypot(res[0], res[1]);
        bestMiss = std::min(bestMiss, nrm);
        for (int it = 0; it < 12 && nrm > 0.004; ++it) {
            const double d = 0.01;
            double r1[2], r2[2];
            if (!resid(s.k1, x + d, y, r1) || !resid(s.k1, x, y + d, r2)) return false;
            const double j00 = (r1[0] - res[0]) / d, j10 = (r1[1] - res[1]) / d, j01 = (r2[0] - res[0]) / d, j11 = (r2[1] - res[1]) / d;
            const double det = j00 * j11 - j01 * j10;
            if (std::fabs(det) < 1e-9) return false;
            double dx = -(j11 * res[0] - j01 * res[1]) / det, dy = -(-j10 * res[0] + j00 * res[1]) / det;
            const double stp = std::hypot(dx, dy);
            if (stp > 0.3) { dx *= 0.3 / stp; dy *= 0.3 / stp; }
            bool ok = false; double lam = 1.0;
            for (int ls = 0; ls < 4; ++ls, lam *= 0.5) {
                double rn[2];
                if (resid(s.k1, x + lam * dx, y + lam * dy, rn) && std::hypot(rn[0], rn[1]) < nrm) { x += lam * dx; y += lam * dy; nrm = std::hypot(rn[0], rn[1]); res[0] = rn[0]; res[1] = rn[1]; ok = true; break; }
            }
            if (!ok) return false;
            bestMiss = std::min(bestMiss, nrm);
        }
        if (nrm > 0.004) return false;
        s.ac = x; s.yc = y;
        return true;
    };
    // a launch is "clean" when it reaches the ring after exactly one touch of the board, inside the ring, without touching the rim
    auto clean = [&](const SimResult& s, double minClear) {
        return s.crossed && !s.crossedBeforeBounce && !s.missedBoard && !s.hitFloor && s.contacts == 1 && !s.secondContact && s.miss < sc.ringRadius && s.clearance >= minClear;
    };
    auto judge = [&](const Seed& s, Cand& c) -> int {      // returns -1 when accepted, else the reason it was refused
        const V v0 = launchFor(s.k1, s.ac, s.yc);
        c.v0 = v0; c.acx = s.ac; c.ycx = s.yc;
        const double speed = len(v0), launchDeg = rad2deg(std::atan2(v0.y, std::hypot(v0.x, v0.z)));
        if (speed > lim.maxSpeed) return RJ_SPEED;
        if (launchDeg < lim.minLaunchDeg || launchDeg > lim.maxLaunchDeg) return RJ_LAUNCH;
        SimResult a = runSim(sc, nominal, p0, v0, spin, true, true);
        SimResult b;
        const bool both = sc.model == ContactModel::Unknown;
        if (both) { Perturb pc; pc.model = static_cast<int>(ContactModel::Continuous); b = runSim(sc, pc, p0, v0, spin, false, true); }
        const SimResult* sims[2] = {&a, &b};
        for (int i = 0; i < (both ? 2 : 1); ++i) {
            const SimResult& q = *sims[i];
            if (!q.crossed) return RJ_MISS;
            if (q.crossedBeforeBounce) return RJ_DIRECT;
            if (q.missedBoard || q.contacts != 1 || q.secondContact || q.hitFloor) return RJ_BOARD;
            if (q.miss >= sc.ringRadius) return RJ_MISS;
            if (q.clearance < lim.minClearanceM) return RJ_CLEAR;
            if (q.entryDeg < lim.minEntryDeg) return RJ_ENTRY;
            if (q.apexY > Ry + lim.maxApexAboveRimM) return RJ_APEX;
            if (q.approach < minApproach) return RJ_APPROACH;
            const V cp = fromVec(q.contactPos);
            if (std::fabs(dot(cp - fc, t)) > sc.halfW - lim.edgeMarginM || std::fabs(cp.y - fc.y) > sc.halfH - lim.edgeMarginM) return RJ_BOARD;
        }
        // how solid is it? the same launch under slightly different bounce / friction / spin / timing
        std::vector<Perturb> what; std::vector<const char*> names;
        { Perturb p; p.eScale = 0.95; what.push_back(p); names.push_back("5% less bounce"); }
        { Perturb p; p.eScale = 1.05; what.push_back(p); names.push_back("5% more bounce"); }
        { Perturb p; p.muScale = 0.7; what.push_back(p); names.push_back("30% less friction"); }
        { Perturb p; p.muScale = 1.3; what.push_back(p); names.push_back("30% more friction"); }
        { Perturb p; p.spinScale = 0.0; what.push_back(p); names.push_back("no spin"); }
        { Perturb p; p.spinScale = 1.3; what.push_back(p); names.push_back("30% more spin"); }
        { Perturb p; p.lateSteps = 1; what.push_back(p); names.push_back("touch found 1 step late"); }
        { Perturb p; const char* nm = "touch found 2 steps late"; if (sc.model == ContactModel::Discrete) { p.model = 1; nm = "exact-moment touch"; } else if (sc.model == ContactModel::Continuous) { p.model = 0; nm = "end-of-step touch"; } else p.lateSteps = 2; what.push_back(p); names.push_back(nm); }
        c.total = static_cast<int>(what.size()); c.pass = 0;
        for (size_t wi = 0; wi < what.size(); ++wi) {
            const SimResult q = runSim(sc, what[wi], p0, v0, spin, false, true);
            if (clean(q, 0.0)) ++c.pass; else { if (!c.failedWhat.empty()) c.failedWhat += ", "; c.failedWhat += names[wi]; }
        }
        if (c.pass < static_cast<int>(std::ceil(lim.minRobustFraction * c.total))) return RJ_ROBUST;
        c.sim = a;
        c.margin = both ? std::min(a.clearance, b.clearance) : a.clearance;
        const double frac = static_cast<double>(c.pass) / c.total;
        c.score = 4.0 * std::min(static_cast<double>(c.margin), 0.05) / 0.05 + 2.0 * frac + (a.entryDeg >= 50.0f ? 1.0 : (a.entryDeg - lim.minEntryDeg) / 10.0)
                  - 0.05 * speed - 0.15 * std::max(0.0, a.apexY - Ry - 2.5) - 0.5 * std::fabs(launchDeg - thrownElev) / 90.0;
        return -1;
    };

    const double hardStop = std::max(lim.budgetSeconds * 6.0, 0.02);
    bool timedOut = false;
    for (size_t i = 0; i < seeds.size(); ++i) {
        const double el = elapsed();
        if (!valid.empty() && el > lim.budgetSeconds) break;
        if (el > hardStop) { timedOut = true; break; }
        ++pl.polished;
        Seed s = seeds[i];
        if (!polish(s)) { ++nearRej[RJ_NOTCONV]; continue; }
        Cand c;
        const int why = judge(s, c);
        if (why >= 0) { ++nearRej[why]; continue; }
        valid.push_back(c);
        int good = 0; for (const Cand& v : valid) if (v.margin >= 0.03) ++good;
        if (good >= 3) break;
    }
    pl.valid = static_cast<int>(valid.size());
    pl.ms = elapsed() * 1000.0;
    pl.tried = seedTries;
    if (valid.empty()) {
        int tot[RJ_COUNT]; int top = RJ_NOTCONV;
        for (int i = 0; i < RJ_COUNT; ++i) { tot[i] = rej[i] + nearRej[i]; if (tot[i] > tot[top]) top = i; }
        pl.ok = false; pl.reason = Reason::NoSolution;
        pl.why = timedOut ? "ran out of time working it out (try again)" : "no safe bank shot from this spot";       // a time-out is not the same as "impossible": say so
        std::string d = fmt("no bank shot: player %.2f m in front of the board plane, %.2f m sideways (touch would be %.0f degrees sideways); tried %d flight times, %d usable starts, %d polished%s; main blocker: %s (%d times)",
                            b0, a0, static_cast<double>(pl.planAngleDeg), seedTries, static_cast<int>(seeds.size()), pl.polished, timedOut ? " (stopped by the time limit)" : "", kRejText[top], tot[top]);
        d += " | counts:";
        for (int i = 0; i < RJ_COUNT; ++i) if (tot[i]) d += fmt(" [%s: %d]", kRejText[i], tot[i]);
        if (bestMiss < 1e8) d += fmt(" | closest correction ended %.3f m from the ring centre", bestMiss);
        pl.detail = d;
        return pl;
    }
    size_t bi = 0;
    for (size_t i = 1; i < valid.size(); ++i) if (valid[i].score > valid[bi].score) bi = i;
    const Cand& c = valid[bi];
    pl.ok = true; pl.reason = Reason::None;
    pl.vel = toVec(c.v0); pl.speed = static_cast<float>(len(c.v0));
    pl.launchDeg = static_cast<float>(rad2deg(std::atan2(c.v0.y, std::hypot(c.v0.x, c.v0.z))));
    pl.sim = c.sim;
    const V cp = fromVec(c.sim.contactPos);
    pl.contactA = static_cast<float>(dot(cp - fc, t)); pl.contactY = static_cast<float>(cp.y);
    pl.approach = c.sim.approach; pl.entryDeg = c.sim.entryDeg; pl.margin = static_cast<float>(c.margin);
    pl.robustPass = c.pass; pl.robustTotal = c.total; pl.score = static_cast<float>(c.score);
    pl.detail = fmt("bank shot: player %.2f m in front of the board plane, %.2f m sideways; tried %d flight times, %d polished, %d solid; chosen: touches the board %.2f m sideways of its middle at height %.2f m (ball centre), hits it at %.1f m/s, then enters the ring at %.0f degrees, %.1f cm to spare from the rim, miss %.1f cm from the centre; launch %.1f m/s at %.1f degrees; bounce held in %d of %d what-if cases%s%s",
                    b0, a0, seedTries, pl.polished, pl.valid, static_cast<double>(pl.contactA), static_cast<double>(pl.contactY), static_cast<double>(pl.approach), static_cast<double>(pl.entryDeg),
                    static_cast<double>(pl.margin) * 100.0, static_cast<double>(c.sim.miss) * 100.0, static_cast<double>(pl.speed), static_cast<double>(pl.launchDeg), c.pass, c.total, c.failedWhat.empty() ? "" : " (not in: ", c.failedWhat.empty() ? "" : (c.failedWhat + ")").c_str());
    return pl;
}

}  // namespace tzbank
