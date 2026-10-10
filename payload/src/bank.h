// bank.h - the MATH of the Aimbot's "Bank" mode (stage D9). Pure C++: no Android, no game, no VR. Tested on a normal computer.
//
// WHAT THIS IS (plain words)
//   A bank shot is a shot that does NOT fly straight into the hoop: the ball hits the FRONT of the backboard first and bounces from there into the ring.
//   This file answers one question: "the ball is HERE, moving like THIS - which launch speed makes it hit the backboard and then drop through the ring?"
//   It needs numbers that must come from the game, never from guesses (the game part reads them and refuses to shoot when it cannot):
//       * gravity, air drag and the physics step (the same ones the direct Aimbot already uses)
//       * where the front of the backboard is, which way it faces and how big it is
//       * where the ring is, and its size
//       * the ball's size, and how the ball and the board bounce (bounciness, friction), plus how the ball is spinning
//   What it does with them:
//       1. A quick "ideal bounce" formula gives a first idea for many flight times.
//       2. Every idea is corrected step by step with a full copy of the physics (gravity, drag, bounce with spin and friction).
//       3. Only a shot that really ends INSIDE the ring (not touching the rim, only one touch of the board, not flying in directly) is accepted.
//       4. The accepted shot is shaken (a bit more / less bounce, friction, drag) to see how fragile it is. The most solid shot wins.
//       5. If nothing is solid enough, the answer is "no bank shot" with the reason. It NEVER turns into a direct shot.
//
//   WHAT IS NOT PROVEN: that the real game's bounce works like this model (a ball hitting a flat board, "bounciness" and "friction" mixed the way the
//   physics engine mixes them). The PC tests prove the maths against this same model. The first real shots will show how close the model is.
//
//   Coordinates: metres, Y is up (Unity). The board is a flat upright rectangle. `n` points from the board's front towards the court.
#pragma once
#include <string>
#include <vector>

#include "aimbot.h"

namespace tzbank {

using tzaim::FlightModel;
using tzaim::Vec3;

// How the physics engine finds the touch between the ball and the board.
//   Discrete   = the usual: the ball is checked only at the end of each physics step (it may be a little inside the board when the touch is found).
//   Continuous = the engine works out the exact moment of the touch.
//   Unknown    = could not be read: a shot must work with BOTH.
enum class ContactModel { Discrete = 0, Continuous = 1, Unknown = 2 };

struct Scene {
    FlightModel flight;                 // gravity, drag, physics step (all read from the game)
    float angularDrag = 0;              // the ball's spin slow-down (Rigidbody angularDrag); 0 = none
    float radius = 0;                   // ball radius in metres (read from the game's collider; 0 = unknown -> refused)
    float kappa = 0.4f;                 // spin inertia of the ball: inertia / (mass * radius^2). 0.4 = a solid ball, 0.667 = a hollow ball
    Vec3 faceCentre;                    // the middle of the board's FRONT face
    Vec3 n;                             // unit direction from the front face towards the court (horizontal)
    float halfW = 0, halfH = 0;         // half width / half height of the front face
    float restitution = -1;             // bounce: 0 = dead, 1 = perfect (the engine's mix of ball and board). -1 = unknown -> refused
    float friction = -1;                // sliding friction of the touch (the engine's mix). -1 = unknown -> refused
    float bounceThreshold = 2.0f;       // slower approaches than this do not bounce at all (Physics.bounceThreshold)
    Vec3 ring;                          // centre of the ring
    float ringRadius = 0;               // free radius of the ring (centre line)
    float ringTube = 0;                 // thickness of the rim wire (radius), 0 = not known; the clearance limit below covers it
    ContactModel model = ContactModel::Unknown;
};

struct Limits {
    float maxSpeed = 26.0f;             // never launch faster than this (m/s)
    float minLaunchDeg = 25.0f, maxLaunchDeg = 85.0f;
    float minEntryDeg = 40.0f;          // the ball must come down at least this steeply (below horizontal) when it passes through the ring
    float maxApexAboveRimM = 4.5f;      // never lob higher than this above the ring
    float minClearanceM = 0.012f;       // the ball may not come closer than this to the rim wire (a 1.2 cm spare covers the rim thickness)
    float minApproachSpeed = 1.2f;      // the ball must hit the board at least this fast (a gentle touch may not bounce at all)
    float maxPlanAngleDeg = 70.0f;      // the ball may not hit the board more sideways than this (seen from above, 0 = straight at it)
    float minBoardDistanceM = 0.9f;     // closer than this to the board plane: unavailable
    float edgeMarginM = 0.08f;          // keep the touch point this far away from the edges of the board
    float minRobustFraction = 0.6f;     // the share of "what if the bounce is a little different" cases that must still go in
    double budgetSeconds = 0.006;       // stop searching after this long (once something solid was found)
    int stride = 0;                     // 0 = automatic (about 150 flight times are tried)
};

struct Request {
    Scene scene;
    Vec3 pos;                           // the ball's centre now
    Vec3 vel;                           // how it moves now (only used to prefer a similar shot)
    Vec3 spin;                          // the ball's spin (rad/s, world axes)
    Limits lim;
};

enum class Reason { None, BadInput, BoardData, BehindBoard, TooClose, SharpAngle, NoSolution };

// What a full copy of the physics says about one launch.
struct SimResult {
    bool crossed = false;               // the ball came down through the ring's height
    bool crossedBeforeBounce = false;   // ... but it did so WITHOUT having touched the board first (a direct shot: never accepted)
    bool missedBoard = false;           // the ball went through the board's plane beside or above/below the board
    bool secondContact = false;         // the ball touched the board twice
    bool hitFloor = false;
    int contacts = 0;                   // board touches
    int contactStep = 0;                // step number of the first touch
    Vec3 contactPos;                    // the ball's centre at the touch
    Vec3 vIn, vOut;                     // velocity just before / after the touch
    float approach = 0;                 // speed towards the board at the touch
    int crossStep = 0;
    Vec3 crossPos;                      // where it crossed the ring's height
    float miss = 1e9f;                  // distance of that point from the ring centre
    float entryDeg = 0;                 // steepness there
    float clearance = -1e9f;            // closest approach of the ball's surface to the rim wire (negative = it touches the rim)
    float apexY = 0;
    int steps = 0;
    std::vector<Vec3> path;             // the ball's centre after every step (index 0 = the start)
};

struct Perturb {                        // "what if" changes used to test how solid a shot is
    double eScale = 1, muScale = 1, dragScale = 1, spinScale = 1, speedScale = 1;
    int lateSteps = 0;                  // the engine notices the touch this many steps late
    int model = -1;                     // -1 = use the scene's model (Unknown = Discrete); 0 discrete; 1 continuous
};
SimResult simulate(const Scene& s, const Vec3& pos, const Vec3& vel, const Vec3& spin, bool keepPath, const Perturb& pt = Perturb());

struct Plan {
    bool ok = false;
    Reason reason = Reason::None;
    std::string why;                    // short plain text when !ok ("too close to the backboard")
    std::string detail;                 // longer text for the facts file (counts, closest attempt)
    Vec3 vel;                           // the launch velocity to set
    float speed = 0, launchDeg = 0;
    SimResult sim;                      // what the physics copy says about that launch (with the path)
    float contactA = 0, contactY = 0;   // where on the board it touches (sideways from the middle, height)
    float approach = 0;                 // speed into the board
    float entryDeg = 0, margin = 0;     // steepness through the ring; spare room to the rim (m)
    int robustPass = 0, robustTotal = 0;
    float score = 0;
    int tried = 0, polished = 0, valid = 0;
    double ms = 0;
    // where the player is, seen from the board
    float distToBoard = 0, sideOffset = 0, planAngleDeg = 0;
};

// The solver. Never throws, never touches the game.
Plan solve(const Request& req);

// The text for the menu ("too close to the backboard", ...).
const char* reasonText(Reason r);

// The bounce of one touch (exposed for the tests): v, w are changed. n = unit normal pointing out of the board towards the ball.
struct Bounce { double e, mu, kappa, radius, threshold; };
void bounceOnce(const Bounce& b, const double nrm[3], double v[3], double w[3]);

}  // namespace tzbank
