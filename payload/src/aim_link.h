// aim_link.h - the part of the Aimbot that REALLY reaches into the game (stage D8).
//
// WHAT THIS IS (plain words)
//   Until stage D7c the Aimbot was only a menu page, the rules ("should it act?") and the maths. This file connects them to the headset game.
//   What the game itself told us (stage D7c lobby file - these are facts, not guesses):
//     * The game keeps ONE "ball control" object for you (class ShovelTools.BallControlManager). It holds a link to the ball you control (_basketball),
//       two "seconds since you let go" timers (_releasedLeftTimer / _releasedRightTimer) and the time of the last release (lastReleaseTime).
//     * The ball (ShovelTools.Basketball) holds a link to its physics body (_rigidbody), its hoop positions (_northHoopPosition, _southHoopPosition, ...)
//       and the flags _wasShot / _shotMade / _unheldTime.
//   What this link does:
//     1. Finds your ball control object (asks the game: GameManager.get_Instance().GetOwnedBallControlManager(); if that does not work it looks through memory
//        for the GameManager and reads the link _playerBallControlManager from it).
//     2. Several times per second (on the game's own thread, inside the controller doorway) it reads the two release timers. When a timer jumps back
//        to zero, you just let go of the ball.
//     3. A few hundredths of a second later it reads the ball's position and speed from its physics body, and asks the rules (aimbot.cpp):
//          is the switch on / is it a shot / which hoop / is that hoop closer than the max shot distance?
//     4. If yes: it works out the launch speed that makes the ball fall into that hoop (with the game's real gravity, the ball's real air drag and the
//        game's real physics step), and sets it on the ball. If not: the throw is left exactly as it was.
//     5. It watches the ball fly and writes ONE report per shot into the facts file: what it saw, what it did, where the ball really went,
//        whether it scored. That report is how the next stage gets fixed, whatever went wrong.
//
//   What is NOT proven (nothing here has run in the real game yet - the PC tests use a pretend game):
//     * that the release timers behave as I read them; that the ball's physics body takes the new speed and the game does not put its own speed back;
//     * that the engine calls (Rigidbody.get_velocity / set_velocity / get_position ...) work from here; that the hoop positions kept in the ball are the real rings;
//     * that the game's physics steps and drag work the way Unity's standard physics does; that it behaves the same in an official match as in the lobby.
//   The first thing the link does with the engine calls is only READ; it writes only when those reads gave believable numbers.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "aimbot.h"
#include "il2cpp_scan.h"
#include "safe_copy.h"

namespace tzaimlink {

// Gives the link the game's il2cpp functions (called on the link thread again and again until it works - the game may not have loaded libil2cpp.so yet).
typedef bool (*ApiProvider)(tzscan::Api* api, std::string* why);

struct Config {
    ApiProvider provider = nullptr;
    tzscan::LogFn note = nullptr;              // one line for the facts file
    int searchSeconds = 30, stallSeconds = 10; // the memory search (only the fallback way of finding your ball control)
    double retrySoonSeconds = 3.0;             // things not ready yet: look again after this long
    double minSearchGapSeconds = 2.0;
    int linkTickMs = 20;                       // how often the link thread looks at its to-do list
    const char* gameThreadName = "UnityMain";  // only a thread with this name may call into the game (empty = any thread: PC tests)
    double releaseDelaySeconds = 0.04;         // after you let go: wait this long before reading the ball's speed (the throw has been applied by then)
    double releaseWindowSeconds = 0.40;        // ... and give up when no shot-like speed showed up within this long
    double watchSeconds = 4.5;                 // how long a flight is watched
    int maxReportLines = 220;                  // lines this link may write into the facts file in total
    bool allowWrite = true;                    // false: only read and report (PC tests)
    double (*clock)() = nullptr;               // PC tests only: a pretend clock for the game-thread timing (null = the real one)
    int testHangAfterChunks = 0, testPollMicros = 0;
};

struct Counters {
    unsigned long long ticks = 0;       // calls from the game thread that did work
    unsigned long long releases = 0;    // "you let go of the ball" events
    unsigned long long shotLike = 0;    // ... that looked like a shot
    unsigned long long aimed = 0;       // ... and where the ball's speed was changed
    unsigned long long tooFar = 0, wrongDirection = 0, noHoop = 0, notAShot = 0, noSolution = 0, refused = 0;   // shots left alone, by reason
    unsigned long long errors = 0;      // engine calls that threw an error in the game
    unsigned long long overridden = 0;  // the game changed the speed we set
    unsigned long long scored = 0, missed = 0;     // what the game said about watched shots (_shotMade)
};

class AimLink {
public:
    AimLink();
    ~AimLink();
    bool start(const Config& cfg);        // starts the link thread. It does nothing in the game until the switch is on.
    void stop();

    // The menu's Aimbot switch and max shot distance (5 ... 50 m; 50 = Unlimited). Cheap; called often.
    void setAsk(bool on, float capM);

    // Called by the game's own thread inside the controller doorway (about 185 times a second). Does its work in microseconds, never waits,
    // never writes to a file. Anything but the game's script thread is ignored.
    void onGameThread();

    // ---- for the menu and the facts file
    // 0 off, 1 connected, 2 looking for your ball control, 3 failed (see failReason)
    int uiState() const;
    std::string failReason() const;
    std::string headline() const;         // short text for the menu: what the link is doing
    std::string lastShotText() const;     // short text for the menu: what happened to the last shot
    std::string summary() const;          // one line for the facts file: state and counters
    Counters counters() const;

private:
    struct Layout {
        bool ok = false;
        tzscan::Api api;
        tzscan::ClassInfo ball, bcm, gm;           // the three game classes (fields with positions)
        void* rbKlass = nullptr;                   // UnityEngine.Rigidbody
        // Basketball
        int bRigid = -1, bWasShot = -1, bShotMade = -1, bUnheld = -1, bHoopsLen = -1, bOrigDrag = -1, bCurPos = -1;
        int bHoop[6] = {-1, -1, -1, -1, -1, -1};   // north, south, north01, south01, north02, south02
        // BallControlManager
        int cTimerL = -1, cTimerR = -1, cLastRel = -1, cBall = -1, cRaw = -1;
        // GameManager
        int gBcm = -1, gState = -1, gComp = -1, gGm = -1, gSolo = -1, gNba = -1;
        // engine and game methods (null = not found)
        void *mGetVel = nullptr, *mSetVel = nullptr, *mGetPos = nullptr, *mGetDrag = nullptr, *mGetUseGravity = nullptr,
             *mGetGravity = nullptr, *mGetFixedDt = nullptr, *mGmInstance = nullptr, *mGmOwned = nullptr;
    };
    enum Phase { IDLE = 0, WAIT = 1, WATCH = 2 };
    struct Sample { int steps = 0; tzaim::Vec3 pos, vel; tzaim::Pose pred; };
    struct Shot {
        int id = 0, hand = 0;                       // how the release was noticed: 1 left timer, 2 right timer, 4 release time moved, 8 the ball's "unheld" timer started
        double tEdge = 0, tApply = 0;
        uintptr_t ball = 0, rb = 0;
        tzaim::Vec3 pos0, vel0, hoops[6]; int hoopSlot[6] = {0, 0, 0, 0, 0, 0}; int nHoops = 0;
        tzaim::Decision dec; bool decided = false, applied = false, solved = false;
        tzaim::Solution sol; tzaim::FlightModel model;
        tzaim::Vec3 hoopPos; float unheld = 0; float capM = 50;
        tzaim::Vec3 readback; bool haveReadback = false;
        tzaim::Vec3 predStart;                      // the velocity the flight prediction starts from (ours if applied, else the player's)
        // watching
        int steps = 0; bool havePrev = false; tzaim::Vec3 prevPos; double prevT = 0;
        std::vector<Sample> samples; int nextSample = 0;
        bool crossed = false; float crossMiss = 0, crossT = 0; tzaim::Vec3 crossPos;
        float closest = 1e9f; int closestStep = 0;
        int madeFlag = -1;                           // _shotMade at the end: -1 unknown, 0 no, 1 yes
        bool overridden = false; float overrideDiff = 0;
        std::string why;                             // when nothing was changed
    };

    static void* threadMain(void* self);
    void loop();
    void flush();
    bool resolveLayout(std::string* why, bool* transient);
    void doSearch();
    void fail(const std::string& why);
    void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));         // link thread: straight to the facts file (bounded)
    void queue(const std::string& line);                                          // game thread: queued, written later by the link thread

    // ---- game thread
    bool gameThreadOk() const;
    double tnow() const;
    bool readObj(unsigned char* out, uintptr_t addr, size_t n);
    bool liveObj(const unsigned char* obj, uint64_t klassInv, bool unityObject) const;
    bool ensureBcm(double now);
    void fastResolve(double now);
    void dropBcm(const char* why);
    void startShot(double now, int hand, uintptr_t ballPtr);
    void tickWait(double now);
    void tickWatch(double now);
    void finishShot(const char* why, double now);
    void abandon(const char* why);
    bool rigidOk(uintptr_t rb);
    bool callVec3(void* method, uintptr_t self, tzaim::Vec3* out);
    bool callFloat(void* method, uintptr_t self, float* out);
    bool callBool(void* method, uintptr_t self, bool* out);
    uintptr_t callObj(void* method, uintptr_t self);
    bool callSetVec3(void* method, uintptr_t self, const tzaim::Vec3& v);
    void* invoke(void* method, uintptr_t self, void** args, bool* threw);
    std::string gameFlags();
    void setLast(const std::string& text);
    void killEngine(const std::string& why);

    Config cfg_;
    Layout L_;                                   // written once by the link thread, then read-only (published by layoutReady_)
    std::atomic<bool> layoutReady_{false};
    tzscan::CopyPipe pipeGame_, pipeLink_;       // one pipe per thread (a pipe is not shared between the two)
    bool pipesOk_ = false;
    std::atomic<bool> on_{false};
    std::atomic<float> capM_{50.0f};
    // handover from the link thread (memory search) to the game thread
    std::atomic<uintptr_t> bcmFound_{0}, gmFound_{0};
    std::atomic<unsigned> bcmGen_{0};
    std::atomic<bool> needBcm_{false}, fastFailed_{false}, bound_{false}, engineDead_{false}, resetExc_{false};
    // game-thread-only state
    struct G {
        bool inTick = false;
        uintptr_t bcm = 0, gm = 0, lastBall = 0; double lastBallT = -1e9, nextResolveAt = 0, lastEdgeT = -1e9;
        unsigned seenGen = 0;
        float prevL = 0, prevR = 0, prevLast = 0; bool haveTimers = false;
        float prevUnheld = 1e9f; uintptr_t prevUnheldBall = 0;
        Phase phase = IDLE; Shot shot;
        int excRun = 0, fastTries = 0;
        bool engineChecked = false;
        int bindNotes = 0, releaseNotes = 0;
    } g_;
    std::vector<unsigned char> bcmBuf_, ballBuf_, gmBuf_;
    // reports from the game thread to the facts file
    mutable std::mutex outMu_;
    std::vector<std::string> out_;
    // status for the menu and the facts file
    mutable std::mutex mu_;
    std::string failReason_, lastText_;
    bool failed_ = false;
    double nextLayoutAt_ = 0, nextSearchAt_ = 0, lastSearchEnd_ = -1e9;
    int layoutTries_ = 0, searches_ = 0, emptySearches_ = 0;
    std::atomic<int> notes_{0};
    std::atomic<unsigned long long> calls_{0}, ticks_{0}, releases_{0}, shotLike_{0}, aimed_{0}, tooFar_{0}, wrongDir_{0}, noHoop_{0}, notShot_{0}, noSolution_{0}, refused_{0},
                                    errors_{0}, overridden_{0}, scored_{0}, missed_{0};
    std::atomic<int> shotSeq_{0};
    std::atomic<double> onSinceAtomic_{-1};
    std::string engineDeadWhy_;                  // under mu_
    // thread
    pthread_t th_{};
    bool started_ = false;
    std::atomic<bool> quit_{false};
};

}  // namespace tzaimlink
