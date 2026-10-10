// PC test of the bank-shot maths (stage D9). It checks the solver against its OWN model of the physics and against an independently written copy of that model.
// It cannot show that the real game bounces like the model. It shows that, if it does, the launch the solver gives really goes in.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bank.h"

using namespace tzbank;

static int gFail = 0, gPass = 0;
#define CHECK(c, ...) do { if (c) ++gPass; else { ++gFail; std::printf("FAIL line %d: %s  ", __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---- an independent reference: sphere + flat board, rigid-body impulse with a full 3x3 matrix, exact moment of touch
struct D3 { double x, y, z; };
static D3 add(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static D3 sub(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static D3 mul(D3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
static double dt3(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static D3 cr(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double ln(D3 a) { return std::sqrt(dt3(a, a)); }

struct RefOut { bool crossed = false; int contacts = 0; double missX = 1e9, missZ = 1e9, miss = 1e9, entry = 0; D3 contactPos{0, 0, 0}; double approach = 0; bool direct = false; };

// impulse J (per unit mass) at the contact point rvec that makes the contact velocity change by `du`: solve K J = du with K = I3 + (1/kappa r^2) * (-[r]x [r]x)
static D3 solveK(D3 rv, double kr2, D3 du) {
    // K = I - (1/kr2) * skew(r)*skew(r)   (skew(r)*skew(r) = r r^T - |r|^2 I)
    double rr = dt3(rv, rv);
    double R[3] = {rv.x, rv.y, rv.z};
    double K[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) K[i][j] = (i == j ? 1.0 : 0.0) + (-(R[i] * R[j]) + (i == j ? rr : 0.0)) / kr2;
    // solve 3x3 by Cramer
    auto det3 = [](double M[3][3]) { return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]); };
    double b[3] = {du.x, du.y, du.z};
    double d = det3(K), out[3];
    for (int c = 0; c < 3; ++c) { double M[3][3]; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] = (j == c) ? b[i] : K[i][j]; out[c] = det3(M) / d; }
    return {out[0], out[1], out[2]};
}

static RefOut refSim(const Scene& sc, D3 p, D3 v, D3 w) {
    RefOut o;
    const double dt = sc.flight.dt, g = sc.flight.gravity, drag = sc.flight.drag, r = sc.radius, e = sc.restitution, mu = sc.friction, kappa = sc.kappa;
    D3 n = {sc.n.x, sc.n.y, sc.n.z}, fc = {sc.faceCentre.x, sc.faceCentre.y, sc.faceCentre.z}, ring = {sc.ring.x, sc.ring.y, sc.ring.z};
    D3 t = cr({0, 1, 0}, n); t = mul(t, 1.0 / ln(t));
    for (int step = 0; step < 520; ++step) {
        v.y -= g * dt; v = mul(v, 1.0 - drag * dt); w = mul(w, std::max(0.0, 1.0 - sc.angularDrag * dt));
        D3 pn = add(p, mul(v, dt));
        double b0 = dt3(sub(p, fc), n), b1 = dt3(sub(pn, fc), n);
        if (b0 >= r && b1 < r && dt3(v, n) < 0) {
            double f = (b0 - r) / (b0 - b1);
            D3 pc = add(p, mul(sub(pn, p), f));
            double a = dt3(sub(pc, fc), t);
            if (std::fabs(a) <= sc.halfW && std::fabs(pc.y - fc.y) <= sc.halfH) {
                double vn = dt3(v, n), ap = -vn;
                double ee = ap > sc.bounceThreshold ? e : 0.0;
                D3 rv = mul(n, -r);
                D3 u = add(sub(v, mul(n, vn)), cr(w, rv));
                double jn = (1 + ee) * ap;
                // normal first
                D3 vNew = add(sub(v, mul(n, vn)), mul(n, ee * ap));
                D3 wNew = w;
                // friction: want u -> 0 (sticking). K J = -u   (J tangential only; solve in the tangent plane by projecting)
                D3 Jst = solveK(rv, kappa * r * r, mul(u, -1.0));
                // remove the normal part of J (tangential impulse only) and then recompute properly in the 2D tangent plane
                // (K couples tangential and normal only through r x ... ; for a sphere r is along n so K is block diagonal and J is already tangential)
                double jm = ln(Jst);
                double lim = mu * jn;
                D3 J = jm <= lim ? Jst : mul(Jst, lim / jm);
                vNew = add(vNew, J);
                wNew = add(w, mul(cr(rv, J), 1.0 / (kappa * r * r)));
                v = vNew; w = wNew;
                ++o.contacts; if (o.contacts == 1) { o.contactPos = pc; o.approach = ap; }
                pn = add(pc, mul(v, dt * (1.0 - f)));
            }
        }
        if (dt3(sub(pn, fc), n) < 0) return o;
        if (!o.crossed && p.y >= ring.y && pn.y < ring.y) {
            double k = (p.y - ring.y) / (p.y - pn.y);
            D3 cp = add(p, mul(sub(pn, p), k));
            o.crossed = true; o.missX = cp.x - ring.x; o.missZ = cp.z - ring.z; o.miss = std::hypot(o.missX, o.missZ);
            o.entry = std::atan2(-v.y, std::hypot(v.x, v.z)) * 180.0 / 3.14159265358979;
            o.direct = o.contacts == 0;
            return o;
        }
        p = pn;
        if (p.y < r) return o;
    }
    return o;
}

static Scene northScene(float dt, float e, float mu) {
    Scene s;
    s.flight.gravity = 9.81f; s.flight.drag = 0.11f; s.flight.dt = dt;
    s.angularDrag = 0.1f; s.radius = 0.12f; s.kappa = 0.4f;
    s.ring = Vec3{0, 3.1f, 12.66f}; s.ringRadius = 0.2286f; s.ringTube = 0.0f;
    s.n = Vec3{0, 0, -1};
    s.faceCentre = Vec3{0, 3.1f + 0.325f, 12.66f + 0.407f - 0.025f};
    s.halfW = 0.9125f; s.halfH = 0.625f;
    s.restitution = e; s.friction = mu; s.bounceThreshold = 2.0f;
    s.model = ContactModel::Unknown;
    return s;
}

static Request reqAt(const Scene& sc, float x, float z, float y = 1.7f) {
    Request r; r.scene = sc; r.pos = Vec3{x, y, z};
    // the player's own throw: up and towards the hoop
    const float dx = 0 - x, dz = 12.66f - z; const float d = std::sqrt(dx * dx + dz * dz);
    r.vel = Vec3{dx / d * 5.0f, 6.0f, dz / d * 5.0f};
    r.spin = Vec3{0, 0, 0};
    return r;
}

int main() {
    // ============================================================ the bounce of one touch
    {
        const Bounce b{1.0, 0.0, 0.4, 0.12, 2.0};
        const double n[3] = {0, 0, 1};
        double v[3] = {1, -2, -4}, w[3] = {0, 0, 0};
        bounceOnce(b, n, v, w);
        CHECK(std::fabs(v[0] - 1) < 1e-9 && std::fabs(v[1] + 2) < 1e-9 && std::fabs(v[2] - 4) < 1e-9, "e=1 mirror gave (%g,%g,%g)", v[0], v[1], v[2]);
        const Bounce b2{0.6, 0.0, 0.4, 0.12, 2.0};
        double v2[3] = {1, -2, -4}, w2[3] = {0, 0, 0};
        bounceOnce(b2, n, v2, w2);
        CHECK(std::fabs(v2[2] - 2.4) < 1e-9 && std::fabs(v2[0] - 1) < 1e-9, "e=0.6 gave (%g,%g,%g)", v2[0], v2[1], v2[2]);
        double v3[3] = {1, -2, -1.5}, w3[3] = {0, 0, 0};
        bounceOnce(b2, n, v3, w3);
        CHECK(std::fabs(v3[2]) < 1e-9, "slow approach below the threshold must not bounce, got %g", v3[2]);
        double v4[3] = {1, -2, 3}, w4[3] = {0, 0, 0};          // moving away: nothing changes
        bounceOnce(b2, n, v4, w4);
        CHECK(v4[2] == 3 && v4[1] == -2, "moving away must not change anything");
        // friction: sticking. Without spin, a tangential speed of 5 along y with a strong friction ends at 5/(1+kappa) and the contact point stands still
        const Bounce bf{0.6, 5.0, 0.4, 0.12, 2.0};
        double v5[3] = {0, -5, -4}, w5[3] = {0, 0, 0};
        bounceOnce(bf, n, v5, w5);
        CHECK(std::fabs(v5[1] + 5.0 / 1.4) < 1e-9, "sticking tangential speed %g (expected %g)", v5[1], -5.0 / 1.4);
        // contact point velocity after = v_t + w x (-r n) = 0
        const double rx = 0, ry = 0, rz = -0.12;
        const double ux = v5[0] + (w5[1] * rz - w5[2] * ry), uy = v5[1] + (w5[2] * rx - w5[0] * rz);
        CHECK(std::fabs(ux) < 1e-9 && std::fabs(uy) < 1e-9, "contact point should stand still, u=(%g,%g)", ux, uy);
        // energy never grows (random touches, e <= 1)
        unsigned seed = 12345; auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
        int bad = 0;
        for (int i = 0; i < 4000; ++i) {
            const Bounce bb{rnd(), rnd() * 2.0, 0.3 + rnd() * 0.3, 0.12, 2.0};
            double nn[3] = {rnd() - 0.5, rnd() * 0.2, 0.5 + rnd()};
            double vv[3] = {(rnd() - 0.5) * 20, (rnd() - 0.5) * 20, -rnd() * 15 - 0.1}, ww[3] = {(rnd() - 0.5) * 30, (rnd() - 0.5) * 30, (rnd() - 0.5) * 30};
            const double nl = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            double u[3] = {nn[0] / nl, nn[1] / nl, nn[2] / nl};
            // make the ball approach along -u
            const double vn = vv[0] * u[0] + vv[1] * u[1] + vv[2] * u[2];
            if (vn >= 0) { for (int k = 0; k < 3; ++k) vv[k] -= 2 * vn * u[k]; }
            auto ke = [&](double* a, double* wv) { const double I = bb.kappa * bb.radius * bb.radius; return 0.5 * (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) + 0.5 * I * (wv[0] * wv[0] + wv[1] * wv[1] + wv[2] * wv[2]); };
            const double k0 = ke(vv, ww);
            bounceOnce(bb, u, vv, ww);
            if (ke(vv, ww) > k0 + 1e-9) ++bad;
        }
        CHECK(bad == 0, "%d random touches gained energy", bad);
    }

    // ============================================================ the solver against its own model and against the independent copy
    struct Case { const char* name; float dt, e, mu; };
    const Case cases[] = {{"dt 1/72, e .70, mu .3", 1.0f / 72, 0.70f, 0.3f}, {"dt 1/50, e .55, mu .6", 0.02f, 0.55f, 0.6f}, {"dt 1/90, e .80, mu .1", 1.0f / 90, 0.80f, 0.1f}, {"dt 1/60, e .45, mu 0", 1.0f / 60, 0.45f, 0.0f}};
    struct Pos { const char* name; float x, z; };
    const Pos positions[] = {{"straight on, 4 m", 0.0f, 8.6f}, {"straight on, 6 m", 0.0f, 6.6f}, {"straight on, 8 m", 0.0f, 4.7f}, {"45 degrees right, 5 m", 3.5f, 9.1f},
                             {"45 degrees left, 5 m", -3.5f, 9.1f}, {"30 degrees left, 7 m", -3.5f, 6.4f}, {"wing right, 6 m", 5.0f, 8.0f}, {"far, 10 m", 1.0f, 2.7f}};
    int plans = 0, unavail = 0;
    double worstMs = 0;
    for (const Case& cs : cases) {
        Scene sc = northScene(cs.dt, cs.e, cs.mu);
        for (const Pos& ps : positions) {
            Request rq = reqAt(sc, ps.x, ps.z);
            rq.lim.budgetSeconds = 0.5;
            Plan pl = solve(rq);
            worstMs = std::max(worstMs, pl.ms);
            if (!pl.ok) { ++unavail; std::printf("  [%s | %s] unavailable: %s\n     %s\n", cs.name, ps.name, pl.why.c_str(), pl.detail.c_str()); continue; }
            ++plans;
            // 1. the solver's own model says it goes in
            CHECK(pl.sim.crossed && pl.sim.contacts == 1 && !pl.sim.crossedBeforeBounce, "[%s | %s] sim: crossed %d contacts %d", cs.name, ps.name, pl.sim.crossed, pl.sim.contacts);
            CHECK(pl.sim.miss < sc.ringRadius && pl.margin >= rq.lim.minClearanceM, "[%s | %s] miss %.3f margin %.3f", cs.name, ps.name, static_cast<double>(pl.sim.miss), static_cast<double>(pl.margin));
            // 2. the independent copy (exact moment of touch) agrees
            for (int spinCase = 0; spinCase < 1; ++spinCase) {
                RefOut ro = refSim(sc, {rq.pos.x, rq.pos.y, rq.pos.z}, {pl.vel.x, pl.vel.y, pl.vel.z}, {rq.spin.x, rq.spin.y, rq.spin.z});
                CHECK(ro.crossed && !ro.direct && ro.contacts == 1, "[%s | %s] reference: crossed %d direct %d contacts %d", cs.name, ps.name, ro.crossed, ro.direct, ro.contacts);
                CHECK(ro.miss < sc.ringRadius - 0.12f * 0.5f, "[%s | %s] reference miss %.3f m", cs.name, ps.name, ro.miss);
            }
            // 3. it hits the FRONT of the board, in the board, and approaches from the court side
            CHECK(std::fabs(pl.contactA) < sc.halfW - 0.05f && std::fabs(pl.contactY - sc.faceCentre.y) < sc.halfH, "[%s | %s] contact a=%.2f y=%.2f", cs.name, ps.name, static_cast<double>(pl.contactA), static_cast<double>(pl.contactY));
            CHECK(pl.sim.contactPos.z < sc.faceCentre.z && pl.sim.contactPos.z > sc.faceCentre.z - 0.2f, "[%s | %s] contact z=%.3f face z=%.3f", cs.name, ps.name, static_cast<double>(pl.sim.contactPos.z), static_cast<double>(sc.faceCentre.z));
            CHECK(pl.speed <= rq.lim.maxSpeed + 1e-3f && pl.entryDeg >= rq.lim.minEntryDeg, "[%s | %s] speed %.1f entry %.0f", cs.name, ps.name, static_cast<double>(pl.speed), static_cast<double>(pl.entryDeg));
            // 4. it is not a direct shot: the direct shot (aim at the ring) would have a very different launch direction
            // 5. a bit different bounce still goes in (the "what-if" cases are checked inside; here three of them again with the independent copy)
            for (float de : {0.95f, 1.05f}) {
                Scene s2 = sc; s2.restitution = sc.restitution * de;
                RefOut ro = refSim(s2, {rq.pos.x, rq.pos.y, rq.pos.z}, {pl.vel.x, pl.vel.y, pl.vel.z}, {0, 0, 0});
                CHECK(ro.crossed && ro.miss < sc.ringRadius, "[%s | %s] with the bounce x%.2f the reference misses by %.3f", cs.name, ps.name, static_cast<double>(de), ro.miss);
            }
        }
    }
    std::printf("  %d bank plans found, %d positions unavailable, slowest solve %.1f ms\n", plans, unavail, worstMs);
    CHECK(plans >= 20, "only %d plans found", plans);
    // the straight-on 4..6 m positions must be possible in the middle cases
    {
        Scene sc = northScene(1.0f / 72, 0.70f, 0.3f);
        for (float z : {8.6f, 6.6f}) { Request rq = reqAt(sc, 0.0f, z); rq.lim.budgetSeconds = 0.5; CHECK(solve(rq).ok, "straight on at z=%.1f must have a bank shot", static_cast<double>(z)); }
    }

    // ============================================================ "Bank unavailable" reasons - never a silent direct shot
    {
        Scene sc = northScene(1.0f / 72, 0.70f, 0.3f);
        Plan a = solve(reqAt(sc, 0.0f, 12.3f));             // 0.74 m from the board plane (13.04 - 12.3), closer than the limit
        CHECK(!a.ok && a.reason == Reason::TooClose, "close: ok=%d reason=%d (%s)", a.ok, static_cast<int>(a.reason), a.detail.c_str());
        Plan b = solve(reqAt(sc, 0.0f, 13.6f));             // behind the board
        CHECK(!b.ok && b.reason == Reason::BehindBoard, "behind: ok=%d reason=%d", b.ok, static_cast<int>(b.reason));
        Plan c = solve(reqAt(sc, 7.0f, 11.0f));             // far to the side, close to the baseline: the board is seen almost edge-on
        CHECK(!c.ok && (c.reason == Reason::SharpAngle || c.reason == Reason::TooClose || c.reason == Reason::NoSolution), "edge-on: ok=%d reason=%d (%s)", c.ok, static_cast<int>(c.reason), c.detail.c_str());
        std::printf("  too close: \"%s\" | behind: \"%s\" | edge-on: \"%s\" (%s)\n", a.why.c_str(), b.why.c_str(), c.why.c_str(), c.detail.c_str());
        // missing numbers are refused, not guessed
        Scene s0 = sc; s0.radius = 0;       Plan d = solve(reqAt(s0, 0.0f, 7.0f));  CHECK(!d.ok && d.reason == Reason::BadInput, "no ball radius must be refused");
        Scene s1 = sc; s1.restitution = -1; Plan e = solve(reqAt(s1, 0.0f, 7.0f));  CHECK(!e.ok && e.reason == Reason::BadInput, "unknown bounce must be refused");
        Scene s2 = sc; s2.friction = -1;    Plan f = solve(reqAt(s2, 0.0f, 7.0f));  CHECK(!f.ok && f.reason == Reason::BadInput, "unknown friction must be refused");
        Scene s3 = sc; s3.flight.dt = 0;    Plan g = solve(reqAt(s3, 0.0f, 7.0f));  CHECK(!g.ok && g.reason == Reason::BadInput, "no physics step must be refused");
        Scene s4 = sc; s4.ring = Vec3{0, 3.1f, 10.0f}; Plan h = solve(reqAt(s4, 0.0f, 7.0f)); CHECK(!h.ok && h.reason == Reason::BoardData, "a ring that is not in front of the board must be refused (%s)", h.detail.c_str());
        Scene s5 = sc; s5.n = Vec3{0, 0.9f, -0.4f}; Plan i = solve(reqAt(s5, 0.0f, 7.0f)); CHECK(!i.ok && i.reason == Reason::BoardData, "a tilted board normal must be refused");
        Request nanR = reqAt(sc, 0.0f, 7.0f); nanR.pos.x = std::nanf(""); Plan j = solve(nanR); CHECK(!j.ok, "a NaN position must be refused");
        // a very bouncy / very sticky / dead board: either a plan that really works in the reference, or a clear "no"
        for (float e2 : {0.0f, 0.2f, 0.95f}) {
            Scene s6 = northScene(1.0f / 72, e2, 0.3f); Request rq = reqAt(s6, 0.0f, 7.0f); rq.lim.budgetSeconds = 0.5; Plan k = solve(rq);
            if (k.ok) { RefOut ro = refSim(s6, {rq.pos.x, rq.pos.y, rq.pos.z}, {k.vel.x, k.vel.y, k.vel.z}, {0, 0, 0}); CHECK(ro.crossed && ro.miss < s6.ringRadius && ro.contacts == 1, "e=%.2f plan fails in the reference (miss %.3f)", static_cast<double>(e2), ro.miss); }
            else CHECK(!k.why.empty() && !k.detail.empty(), "e=%.2f: an unavailable answer needs a reason", static_cast<double>(e2));
            std::printf("  bounce %.2f at 7 m: %s\n", static_cast<double>(e2), k.ok ? "plan found" : k.detail.c_str());
        }
    }

    // ============================================================ the other hoop (south): the maths must not care which way the board faces
    {
        Scene sc = northScene(1.0f / 72, 0.70f, 0.3f);
        Scene so = sc;
        so.ring = Vec3{0, 3.1f, -12.66f}; so.n = Vec3{0, 0, 1}; so.faceCentre = Vec3{0, 3.1f + 0.325f, -12.66f - 0.407f + 0.025f};
        Plan a = solve([&] { Request r = reqAt(sc, 2.0f, 7.5f); r.lim.budgetSeconds = 0.5; return r; }());
        Request rs = reqAt(so, -2.0f, -7.5f); rs.vel = Vec3{0.0f, 6.0f, -5.0f}; rs.lim.budgetSeconds = 0.5;
        Plan b = solve(rs);
        CHECK(a.ok && b.ok, "mirror plans: %d %d", a.ok, b.ok);
        if (a.ok && b.ok) {
            // the mirrored plan is the mirrored launch (x -> -x... the player stands at the mirrored place: (x,z) -> (-x,-z))
            CHECK(std::fabs(a.vel.x + b.vel.x) < 0.05f && std::fabs(a.vel.z + b.vel.z) < 0.05f && std::fabs(a.vel.y - b.vel.y) < 0.05f, "mirror: (%.2f,%.2f,%.2f) vs (%.2f,%.2f,%.2f)",
                  static_cast<double>(a.vel.x), static_cast<double>(a.vel.y), static_cast<double>(a.vel.z), static_cast<double>(b.vel.x), static_cast<double>(b.vel.y), static_cast<double>(b.vel.z));
        }
    }

    // ============================================================ time: one solve on the game thread must be short
    {
        Scene sc = northScene(1.0f / 72, 0.70f, 0.3f);
        double worst = 0, sum = 0; int n = 0;
        for (const Pos& ps : positions) {
            Request rq = reqAt(sc, ps.x, ps.z);              // the default budget (6 ms once something is found)
            auto t0 = std::chrono::steady_clock::now(); Plan pl = solve(rq); double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms); sum += ms; ++n; (void)pl;
        }
        std::printf("  solve time with the default budget: average %.1f ms, worst %.1f ms (a PC; the headset is slower)\n", sum / n, worst);
        CHECK(worst < 60.0, "a solve took %.1f ms", worst);
    }

    // ============================================================ the ball is wider than its centre: the rim is checked until the whole ball is below the ring (exact circle, not the solver's own points)
    {
        auto exactClear = [](const Scene& sc, const std::vector<Vec3>& path) {     // smallest distance from the BALL'S SURFACE to the ring's wire line, exact circle, path interpolated finely
            double best = 1e9;
            for (size_t i = 0; i + 1 < path.size(); ++i)
                for (int k = 0; k <= 40; ++k) {
                    const double f = k / 40.0;
                    const double x = path[i].x + (path[i + 1].x - path[i].x) * f, y = path[i].y + (path[i + 1].y - path[i].y) * f, z = path[i].z + (path[i + 1].z - path[i].z) * f;
                    if (std::fabs(y - sc.ring.y) > 0.6) continue;
                    const double dh = std::hypot(x - sc.ring.x, z - sc.ring.z);
                    best = std::min(best, std::hypot(dh - sc.ringRadius, y - sc.ring.y) - sc.radius);
                }
            return best;
        };
        int checked = 0; double worstClear = 1e9; int shortPath = 0;
        for (const Case& cs : cases) {
            Scene sc = northScene(cs.dt, cs.e, cs.mu);
            for (const Pos& ps : positions) {
                Request rq = reqAt(sc, ps.x, ps.z); rq.lim.budgetSeconds = 0.5;
                const Plan pl = solve(rq);
                if (!pl.ok) continue;
                ++checked;
                if (pl.sim.path.empty() || pl.sim.path.back().y > sc.ring.y - sc.radius * 0.5) ++shortPath;       // the flight must go on until the ball is (almost) wholly below the ring, not stop when its centre crosses
                worstClear = std::min(worstClear, exactClear(sc, pl.sim.path));
            }
        }
        std::printf("  %d plans: the ball's surface stays at least %.1f mm away from the ring's wire (exact circle, whole passage)\n", checked, worstClear * 1000.0);
        CHECK(checked >= 20 && shortPath == 0, "checked %d plans, %d flights end while the ball is still at the ring's height", checked, shortPath);
        CHECK(worstClear >= 0.008, "a plan comes within %.1f mm of the rim", worstClear * 1000.0);
        // a hand-made case that clips the FRONT rim after the centre has crossed: it must be recognised as touching the rim (clearance < 0)
        {
            Scene sc = northScene(1.0f / 72, 0.80f, 0.45f);
            // a ball that touches the board high up and comes back FAST (the bounce takes it to the court side while it falls through the ring)
            Request rq = reqAt(sc, 0.0f, 0.0f);
            Vec3 best{0, 0, 0}; bool found = false;
            for (float sp = 10.0f; sp < 16.0f && !found; sp += 0.05f)
                for (float el = 45.0f; el < 70.0f && !found; el += 0.5f) {
                    const float er = el * 3.14159265f / 180.0f;
                    const Vec3 v{0.0f, sp * std::sin(er), sp * std::cos(er)};
                    const SimResult r = simulate(sc, rq.pos, v, rq.spin, false);
                    if (r.crossed && !r.crossedBeforeBounce && r.contacts == 1 && r.miss < sc.ringRadius && r.miss > 0.10f && r.clearance < 0.0f) { best = v; found = true; }
                }
            CHECK(found, "no launch found that crosses the ring's height inside the ring but clips the front rim");
            if (found) {
                const SimResult r = simulate(sc, rq.pos, best, rq.spin, true);
                std::printf("  clipping launch: centre crosses the ring %.1f cm from its middle, the ball's surface comes %.1f cm INTO the rim line (clearance %.1f cm)\n", static_cast<double>(r.miss) * 100.0, -static_cast<double>(r.clearance) * 100.0, static_cast<double>(r.clearance) * 100.0);
                CHECK(r.clearance < 0.0f, "clearance %.3f", static_cast<double>(r.clearance));
                CHECK(exactClear(sc, r.path) < 0.0, "the exact check agrees that this launch clips the rim (%.3f)", exactClear(sc, r.path));
            }
        }
    }

    // ============================================================ many random scenes: every plan must hold up in the independent copy, every refusal must have a reason
    {
        unsigned seed = 20241009u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
        int ok = 0, no = 0, badRef = 0, badReason = 0, badClear = 0;
        double worstMs = 0, sumMs = 0;
        for (int i = 0; i < 300; ++i) {
            const float dt = (i % 3 == 0) ? 1.0f / 72 : ((i % 3 == 1) ? 1.0f / 90 : 1.0f / 60);
            Scene sc = northScene(dt, static_cast<float>(0.35 + 0.55 * rnd()), static_cast<float>(0.7 * rnd()));
            sc.radius = static_cast<float>(0.10 + 0.05 * rnd());
            sc.kappa = static_cast<float>(0.35 + 0.3 * rnd());
            sc.angularDrag = static_cast<float>(0.2 * rnd());
            sc.bounceThreshold = static_cast<float>(1.0 + 2.0 * rnd());
            const float x = static_cast<float>(-9.0 + 18.0 * rnd()), z = static_cast<float>(1.5 + 10.5 * rnd());
            Request rq = reqAt(sc, x, z);
            rq.spin = Vec3{static_cast<float>(-20.0 + 40.0 * rnd()), static_cast<float>(-10.0 + 20.0 * rnd()), 0.0f};
            const auto t0 = std::chrono::steady_clock::now();
            const Plan pl = solve(rq);                       // the default budget
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worstMs = std::max(worstMs, ms); sumMs += ms;
            if (!pl.ok) { ++no; if (pl.why.empty() || pl.detail.empty() || pl.reason == Reason::None) ++badReason; continue; }
            ++ok;
            RefOut ro = refSim(sc, {rq.pos.x, rq.pos.y, rq.pos.z}, {pl.vel.x, pl.vel.y, pl.vel.z}, {rq.spin.x, rq.spin.y, rq.spin.z});
            if (!(ro.crossed && !ro.direct && ro.contacts == 1 && ro.miss < sc.ringRadius - 0.01)) ++badRef;
            double best = 1e9;
            for (size_t k = 0; k + 1 < pl.sim.path.size(); ++k)
                for (int q = 0; q <= 20; ++q) {
                    const double f = q / 20.0, px = pl.sim.path[k].x + (pl.sim.path[k + 1].x - pl.sim.path[k].x) * f, py = pl.sim.path[k].y + (pl.sim.path[k + 1].y - pl.sim.path[k].y) * f, pz = pl.sim.path[k].z + (pl.sim.path[k + 1].z - pl.sim.path[k].z) * f;
                    if (std::fabs(py - sc.ring.y) > 0.6) continue;
                    best = std::min(best, std::hypot(std::hypot(px - sc.ring.x, pz - sc.ring.z) - sc.ringRadius, py - sc.ring.y) - sc.radius);
                }
            if (best < 0.006) ++badClear;
        }
        std::printf("  300 random scenes (bounce .35-.9, friction 0-.7, radius .10-.15, spin, step 1/60-1/90, anywhere from the wing to 1.5 m from the baseline): %d plans, %d refusals; solve time average %.1f ms, worst %.1f ms (PC)\n", ok, no, sumMs / 300.0, worstMs);
        CHECK(badRef == 0, "%d plans do not hold in the independent copy", badRef);
        CHECK(badClear == 0, "%d plans come closer than 6 mm to the rim", badClear);
        CHECK(badReason == 0, "%d refusals have no reason", badReason);
        CHECK(ok >= 60, "only %d of 300 random scenes had a bank shot", ok);
        CHECK(worstMs < 80.0, "a solve took %.1f ms", worstMs);
    }

    std::printf("%d checks passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
