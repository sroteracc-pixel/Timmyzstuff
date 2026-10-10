// Tests for the movement RULES (movement.cpp) using a pretend game. PC only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "movement.h"
using namespace tzmove;

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::printf("  PASS  %s\n", name); } else { ++failed; std::printf("  FAIL  %s\n", name); } } while (0)
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A pretend game: three numbers the movement code could change.
struct PretendGame : Adapter {
    float speed = 3.0f, jumpSpeed = 5.0f, gravity = -9.81f;       // the game's current values
    float oSpeed = 0, oJump = 0, oGrav = 0;                       // remembered originals
    bool isReady = true, captureWorks = true;
    int captures = 0, applies = 0, restores = 0;
    bool ready() override { return isReady; }
    bool captureOriginals() override { ++captures; if (!captureWorks) return false; oSpeed = speed; oJump = jumpSpeed; oGrav = gravity; return true; }
    void apply(const Effective& e) override { ++applies; speed = oSpeed * e.speedMul; jumpSpeed = oJump * e.jumpSpeedMul(); gravity = oGrav * e.gravityMul; }   // ABSOLUTE: original x factor
    void restoreOriginals() override { ++restores; speed = oSpeed; jumpSpeed = oJump; gravity = oGrav; }
};
static float jumpHeight(float v, float g) { return v * v / (2.0f * std::fabs(g)); }

int main() {
    std::printf("== what the menu asks for -> what the game gets\n");
    Request r;
    CHECK("everything starts off: the game's own values are untouched", resolve(r).isIdentity());
    r.speedOn = true; r.speed = 2.0f;
    { const Effective e = resolve(r); CHECK("Speed Boost 2.0x -> speed x2.0, nothing else", near(e.speedMul, 2.0f) && e.jumpHeightMul == 1.0f && e.gravityMul == 1.0f); }
    r.speedOn = false; r.jumpOn = true; r.jump = 3.0f;
    { const Effective e = resolve(r);
      CHECK("Jump Boost 3.0x -> jump HEIGHT x3.0, nothing else", near(e.jumpHeightMul, 3.0f) && e.speedMul == 1.0f && e.gravityMul == 1.0f);
      const float v = 5.0f, g = -9.81f;
      CHECK("the starting speed is scaled by the square root, so the jump really is 3x as HIGH", near(jumpHeight(v * e.jumpSpeedMul(), g), 3.0f * jumpHeight(v, g), 1e-4f));
      CHECK("... and the same holds under low gravity (still 3x the height you would otherwise get)", near(jumpHeight(v * e.jumpSpeedMul(), g * 0.4f), 3.0f * jumpHeight(v, g * 0.4f), 1e-4f)); }
    r = Request(); r.gravityMode = 1; r.lowPct = 90;
    CHECK("Low Gravity 90% -> only 10% of normal gravity", near(resolve(r).gravityMul, 0.10f));
    r.lowPct = 0; CHECK("Low Gravity 0% -> normal gravity", near(resolve(r).gravityMul, 1.0f) && resolve(r).isIdentity());
    r.lowPct = 35; CHECK("Low Gravity 35% -> 65% of normal", near(resolve(r).gravityMul, 0.65f));
    r = Request(); r.gravityMode = 2; r.highPct = 90;
    CHECK("High Gravity 90% -> 190% of normal gravity", near(resolve(r).gravityMul, 1.90f));
    r.highPct = 45; CHECK("High Gravity 45% -> 145% of normal", near(resolve(r).gravityMul, 1.45f));
    r.highPct = 0; CHECK("High Gravity 0% -> normal gravity", resolve(r).isIdentity());
    r = Request(); r.gravityMode = 1; r.lowPct = 50; r.highPct = 80;
    CHECK("only the chosen gravity mode counts (Low), the High slider is ignored", near(resolve(r).gravityMul, 0.5f));
    r.gravityMode = 2; CHECK("only the chosen gravity mode counts (High), the Low slider is ignored", near(resolve(r).gravityMul, 1.8f));
    r = Request(); r.speedOn = true; r.speed = 9.0f; r.jumpOn = true; r.jump = 0.2f; r.gravityMode = 1; r.lowPct = 200;
    { const Effective e = resolve(r); CHECK("out-of-range values are pulled back into range (speed<=5, jump>=1, gravity>=10%)", near(e.speedMul, 5.0f) && near(e.jumpHeightMul, 1.0f) && near(e.gravityMul, 0.10f)); }
    r = Request(); r.speedOn = true; r.speed = 4.0f; r.jumpOn = true; r.jump = 4.0f; r.gravityMode = 2; r.highPct = 60; r.flyActive = true;
    CHECK("while Fly is active every boost steps aside (Fly wins)", resolve(r).isIdentity());
    r.flyActive = false;
    CHECK("... and they are back when Fly stops", near(resolve(r).speedMul, 4.0f) && near(resolve(r).gravityMul, 1.6f));
    r = Request(); r.speedOn = true; r.speed = 2.0f; r.jumpOn = true; r.jump = 2.0f; r.gravityMode = 1; r.lowPct = 50;
    { const Effective e = resolve(r); CHECK("different features combine once each (speed x2, jump x2, gravity x0.5), nothing is squared", near(e.speedMul, 2.0f) && near(e.jumpHeightMul, 2.0f) && near(e.gravityMul, 0.5f)); }

    std::printf("== putting values into the (pretend) game\n");
    {   PretendGame g; Controller c; Request q;
        CHECK("no adapter: status NO_LINK and nothing happens", !c.update(q) && c.status() == Controller::NO_LINK);
        q.speedOn = true; q.speed = 2.0f;
        CHECK("no adapter: even with a switch on, nothing happens", !c.update(q) && c.status() == Controller::NO_LINK);
        c.setAdapter(&g); g.isReady = false;
        CHECK("adapter not ready: no link, the game is untouched", !c.update(q) && g.applies == 0 && g.captures == 0 && g.speed == 3.0f);
        g.isReady = true;
        CHECK("Speed Boost 2.0x on: applied once, speed = 3.0 x 2.0 = 6.0", c.update(q) && g.speed == 6.0f && g.applies == 1 && g.captures == 1 && c.status() == Controller::ACTIVE);
        for (int i = 0; i < 1000; ++i) c.update(q);
        CHECK("asking the same thing 1000 more times does NOT apply it again (no stacking)", g.applies == 1 && g.speed == 6.0f);
        q.speed = 3.0f; c.update(q);
        CHECK("moving the slider to 3.0x gives 9.0 (original x 3), not 6 x 3 = 18", g.speed == 9.0f && g.captures == 1);
        q.speed = 1.1f; c.update(q);
        CHECK("moving it back down works from the original too (3.3)", near(g.speed, 3.3f, 1e-4f));
        q.gravityMode = 1; q.lowPct = 50; c.update(q);
        CHECK("adding Low Gravity 50% changes gravity only: -9.81 x 0.5", near(g.gravity, -4.905f, 1e-4f) && near(g.speed, 3.3f, 1e-4f));
        q.gravityMode = 2; q.highPct = 90; c.update(q);
        CHECK("switching to High Gravity 90% replaces Low (never both): -9.81 x 1.9", near(g.gravity, -18.639f, 1e-3f));
        q.gravityMode = 0; c.update(q);
        CHECK("gravity off: the ORIGINAL gravity is back exactly, speed boost still on", g.gravity == -9.81f && near(g.speed, 3.3f, 1e-4f));
        q.jumpOn = true; q.jump = 4.0f; c.update(q);
        CHECK("Jump Boost 4.0x: the starting speed doubles (sqrt 4), i.e. 4x the height", near(g.jumpSpeed, 10.0f, 1e-4f) && near(jumpHeight(g.jumpSpeed, g.gravity), 4.0f * jumpHeight(5.0f, -9.81f), 1e-3f));
        q.speedOn = false; q.jumpOn = false; c.update(q);
        CHECK("everything off: ALL original values come back bit-for-bit", g.speed == 3.0f && g.jumpSpeed == 5.0f && g.gravity == -9.81f && c.status() == Controller::IDLE && g.restores == 1);
        // the game changes its own base value while nothing is applied (e.g. a new level)
        g.speed = 4.0f; q.speedOn = true; q.speed = 2.0f; c.update(q);
        CHECK("originals are read again next time (the game's own value 4.0 -> 8.0)", g.speed == 8.0f);
        q.speedOn = false; c.update(q);
        CHECK("and restored to that 4.0", g.speed == 4.0f);
    }
    {   PretendGame g; Controller c; c.setAdapter(&g); Request q;
        q.speedOn = true; q.speed = 5.0f; q.jumpOn = true; q.jump = 5.0f; q.gravityMode = 1; q.lowPct = 90;
        for (int i = 0; i < 1000; ++i) { q.speedOn = (i % 2 == 0); q.gravityMode = (i % 3 == 0) ? 1 : (i % 3 == 1 ? 2 : 0); q.highPct = 90; c.update(q); }
        q = Request(); c.update(q);
        CHECK("1000 random on/off flips later, everything is exactly back to the original", g.speed == 3.0f && g.jumpSpeed == 5.0f && g.gravity == -9.81f);
    }
    {   PretendGame g; Controller c; c.setAdapter(&g); Request q; q.speedOn = true; q.speed = 2.0f; q.gravityMode = 1; q.lowPct = 50;
        c.update(q);
        Request flying = q; flying.flyActive = true; c.update(flying);
        CHECK("Fly turns on: boosts are taken off (originals back)", g.speed == 3.0f && g.gravity == -9.81f);
        c.update(q);
        CHECK("Fly turns off: boosts return, once, from the originals", g.speed == 6.0f && near(g.gravity, -4.905f, 1e-4f));
    }
    {   PretendGame g; Controller c; c.setAdapter(&g); Request q; q.speedOn = true; q.speed = 2.0f; g.captureWorks = false;
        for (int i = 0; i < 50; ++i) c.update(q);
        CHECK("if the game's values cannot be read: error, nothing is changed, and it does not hammer the game (1 try)", c.status() == Controller::ERROR_ && g.applies == 0 && g.captures == 1 && g.speed == 3.0f);
        q.speed = 2.5f; g.captureWorks = true; c.update(q);
        CHECK("a different request tries again and works", g.applies == 1 && near(g.speed, 7.5f, 1e-4f));
        c.shutdown();
        CHECK("shutdown puts everything back", g.speed == 3.0f && c.status() == Controller::IDLE);
    }
    {   PretendGame g; Controller c; c.setAdapter(&g); Request q; q.speedOn = true; q.speed = 2.0f; g.captureWorks = false;
        c.update(q);
        const bool wasError = c.status() == Controller::ERROR_;
        g.isReady = false; c.update(q); g.isReady = true; g.captureWorks = true; c.update(q);
        CHECK("a link that drops and comes back gets a fresh try even with the same request", wasError && g.applies == 1 && near(g.speed, 6.0f) && c.status() == Controller::ACTIVE);
    }
    std::printf("\npassed: %d  failed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
