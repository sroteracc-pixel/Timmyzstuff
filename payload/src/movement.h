// movement.h - the RULES for the movement features (Speed Boost, Jump Boost, Low / High Gravity).
// Pure C++: no game code, no Android. So every rule here is tested on a normal computer.
//
// WHAT THIS IS (plain words)
//   The menu says what you ASK for ("speed 2.0x on"). This file turns that into what the game should
//   actually get, and makes sure of the safety rules:
//     * a switch that is off changes nothing;
//     * turning a switch off puts the game's ORIGINAL value back, exactly;
//     * a value is always "original x factor" - never "current value x factor" - so repeating it,
//       or moving a slider, can never pile one change on top of another;
//     * only one gravity mode is active at a time;
//     * while Fly is active, the boosts step aside (Fly wins) and come back afterwards.
//
//   The part that really reaches INTO the game is an "adapter" (see Adapter below). THERE IS NO ADAPTER
//   YET: reading the game's real movement code has not been possible (see the facts-file scan), so right
//   now the Controller reports "no game link" and changes nothing in the game.
#pragma once

namespace tzmove {

// What the menu asks for. (Slider values are already in range.)
struct Request {
    bool speedOn = false;   float speed = 1.0f;            // Speed Boost: 1.1 .. 5.0 times
    bool jumpOn = false;    float jump = 1.0f;             // Jump Boost: 1.1 .. 5.0 times the jump HEIGHT
    int gravityMode = 0;    float lowPct = 0, highPct = 0; // 0 off / 1 Low / 2 High; percent 0 .. 90
    bool flyActive = false;                                // true while Fly is on (Fly does not exist yet; the rule is ready)
};

// What the game should get. 1.0 means "the game's own value, untouched".
struct Effective {
    float speedMul = 1.0f;          // walking / running speed
    float jumpHeightMul = 1.0f;     // how HIGH a jump goes (not just how fast it starts)
    float gravityMul = 1.0f;        // 0.10 .. 1.90 of normal gravity
    bool isIdentity() const { return speedMul == 1.0f && jumpHeightMul == 1.0f && gravityMul == 1.0f; }
    bool sameAs(const Effective& o) const;
    // A jump that starts with upward speed v reaches height v*v / (2*g). To get `jumpHeightMul` times the
    // height under the SAME gravity, the starting speed must be multiplied by the square root.
    float jumpSpeedMul() const;
};

// All the rules in one place.
Effective resolve(const Request& r);

// The part that touches the game. Whoever implements it must obey:
//   captureOriginals(): remember the game's own values. Only called while NO effect is applied.
//   apply(e):           set every value to  ORIGINAL x factor  (never on top of what is there now).
//   restoreOriginals(): put the remembered originals back exactly.
class Adapter {
public:
    virtual ~Adapter() {}
    virtual bool ready() = 0;                     // can it reach the game right now?
    virtual bool captureOriginals() = 0;          // false = could not read the game's values
    virtual void apply(const Effective& e) = 0;
    virtual void restoreOriginals() = 0;
};

class Controller {
public:
    enum Status { NO_LINK, IDLE, ACTIVE, ERROR_ };
    void setAdapter(Adapter* a) { adapter_ = a; }
    // Call about 50 times a second with the menu's current request. Returns true when it changed something in the game.
    bool update(const Request& r);
    void shutdown();                              // put everything back (used when the payload stops)
    Status status() const { return status_; }
    const Effective& applied() const { return applied_; }
    long applyCalls() const { return applyCalls_; }
    long restoreCalls() const { return restoreCalls_; }
private:
    Adapter* adapter_ = nullptr;
    Status status_ = NO_LINK;
    Effective applied_, failedWith_;
    bool haveOriginals_ = false;
    long applyCalls_ = 0, restoreCalls_ = 0;
};

}  // namespace tzmove
