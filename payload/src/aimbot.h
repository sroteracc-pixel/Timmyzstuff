// aimbot.h - the RULES and the MATH of the Aimbot (stage D7). Pure C++: no Android, no game, no VR. Tested on a normal computer.
//
// WHAT THIS IS (plain words)
//   The Aimbot idea: when you let go of the basketball, the game checks three things.
//     1. Is the Aimbot switch on?
//     2. Does this look like a SHOT (thrown up and forward, not a flat pass)?
//     3. Which hoop are you aiming at, and is that hoop closer than your "max shot distance"?
//   If all three say yes, the ball gets a new launch speed that makes it fly into THAT hoop. If any says no, nothing is changed
//   and the throw is exactly what you threw.
//
//   The slider "max shot distance" goes from 5 m to 50 m in steps of 1 m. At 50 the menu says "Unlimited" and the distance check is skipped.
//
//   WHAT IS NOT HERE: anything that touches the game. This file only answers questions ("should the aimbot act?", "what launch
//   velocity reaches the hoop?"). The part that reads the real ball and hoop, and writes the new velocity, needs facts that nobody has
//   seen yet (the stage D7 scan collects them). Every number marked "ASSUMPTION" below is a guess that the real game can prove wrong.
//
//   Coordinates: metres, Y is up (Unity's convention). Gravity is given by the caller (the game's own value), never assumed.
#pragma once
#include <string>

namespace tzaim {

// ---- the slider ----
const float kCapMinM = 5.0f, kCapMaxM = 50.0f, kCapStepM = 1.0f;     // 5 m ... 50 m, one metre per step
const float kCapDefaultM = kCapMaxM;                                  // starts at "Unlimited" (the switch itself always starts OFF)
bool isUnlimited(float capM);                                         // true at the top of the slider (50)
float snapCap(float capM);                                            // whole metres, kept between 5 and 50
std::string capLabel(float capM);                                     // "Unlimited" or "23 m"

struct Vec3 { float x = 0, y = 0, z = 0; };

// What the game tells us at the moment the ball leaves the hand.
struct Shot {
    Vec3 pos;      // where the ball is (centre of the ball)
    Vec3 vel;      // how fast and in which direction it is moving (m/s)
};

// ASSUMPTIONS (guesses, to be checked against the real game's numbers):
struct Rules {
    float minShotSpeed = 2.5f;          // slower than this is a drop or a bounce, not a shot (m/s)
    float minShotElevationDeg = 20.0f;  // flatter than this is a pass, not a shot
    float maxAimAngleDeg = 40.0f;       // the hoop must be within this angle of where the ball is heading (looked at from above)
    float minHoopDistanceM = 0.5f;      // directly under the hoop: no aiming possible
};

enum class Verdict {
    Off,             // the switch is off
    NotAShot,        // too slow or too flat: leave the throw alone
    NoHoop,          // the game gave no hoop at all
    WrongDirection,  // no hoop in the direction of the throw
    TooFar,          // the hoop you aim at is farther than your max shot distance
    Ok               // go ahead: aim the ball at hoop `hoop`
};
const char* verdictText(Verdict v);                                   // short text for the facts file

struct Decision {
    Verdict verdict = Verdict::Off;
    int hoop = -1;            // index of the chosen hoop (set for Ok, TooFar and also WrongDirection = the closest in angle)
    float distanceM = 0;      // flat distance (along the floor) from the ball to the chosen hoop
    float angleDeg = 0;       // flat angle between the throw direction and the chosen hoop
    float elevationDeg = 0;   // how steeply the ball was thrown upward
    float speed = 0;
};

// The decision. `hoops` = the centre of each hoop's ring, `n` of them. Pure function.
Decision decide(bool on, float capM, const Shot& shot, const Vec3* hoops, int n, const Rules& rules = Rules());

// ---- the launch velocity ----
struct SolveParams {
    float gravity = 9.81f;          // how strongly the ball falls (m/s^2, positive number). The caller reads it from the game.
    float minLaunchDeg = 35.0f;     // ASSUMPTION: never flatter than this
    float maxLaunchDeg = 80.0f;     // ... never steeper than this
    float minEntryDeg = 38.0f;      // ASSUMPTION: the ball must come down at least this steeply (below horizontal) at the ring, so it fits through
    float maxSpeed = 60.0f;         // a launch faster than this is refused (m/s)
    float preferredLaunchDeg = 50.0f;   // the arc the player threw (the solver keeps it when it works)
};
struct Solution {
    bool ok = false;
    Vec3 vel;               // the new launch velocity
    float launchDeg = 0;    // upward angle of that velocity
    float entryDeg = 0;     // angle below horizontal when the ball arrives at the ring
    float speed = 0;
    float flightSeconds = 0;
    const char* why = "";   // when !ok
};
// A velocity that makes a ball with no air drag, launched at `from`, pass through `to`, falling on the way. (Air drag and spin are NOT included.)
Solution solve(const Vec3& from, const Vec3& to, const SolveParams& p);

// Flat (floor) distance and the vector helpers used by the tests and the game part.
float flatDistance(const Vec3& a, const Vec3& b);

}  // namespace tzaim
