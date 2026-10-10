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
//   Stage D9 adds a second mode, BANK: instead of aiming straight at the ring it works out a launch that hits the FRONT of the backboard and bounces into the ring
//   (maths in bank.cpp, game reads in aim_bank.cpp). Only one of the two modes is ever active. When a bank shot is not possible the throw is left exactly as it was thrown
//   and the menu says "Bank unavailable: <why>" - it never turns into a direct shot.
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
#include "bank.h"
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
    double yGraceSeconds = 0.10;               // "Hold Y": Y also counts when it was let go of up to this long before you let go of the ball
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
    unsigned long long notHoldingY = 0, yUnknown = 0;      // stage D8c ("Hold Y to aim"): shots left alone because Y was not held / because the Y button could not be read
    unsigned long long yReadsProbe = 0, yHeldProbe = 0, yReadsDoorway = 0, yHeldDoorway = 0;   // how often the Y button was read (and seen held) by the menu's thread / by the controller doorway
    unsigned long long errors = 0;      // engine calls that threw an error in the game
    unsigned long long overridden = 0;  // the game changed the speed we set
    unsigned long long scored = 0, missed = 0;     // what the game said about watched shots (_shotMade)
    unsigned long long bankAimed = 0, bankUnavailable = 0, bankScored = 0, bankMissed = 0;   // stage D9 (Bank mode): bank shots set / shots where a bank shot was not possible / results
};

class AimLink {
public:
    AimLink();
    ~AimLink();
    bool start(const Config& cfg);        // starts the link thread. It does nothing in the game until the switch is on.
    void stop();

    // The menu's Aimbot switch and max shot distance (5 ... 50 m; 50 = Unlimited). Cheap; called often.
    // `holdY` = the menu's "Hold Y to aim" switch: the throw is only changed when the Y button (left Touch controller) is held as you let go of the ball.
    void setAsk(bool on, float capM, bool holdY = false);
    // stage D9: the same with the mode: 0 off, 1 Direct (aim straight at the ring), 2 Bank (hit the backboard first). Only one mode can be on.
    void setAsk(int mode, float capM, bool holdY);
    int mode() const { return mode_.load(); }

    // The real Y button, as seen right now. Called by the menu's thread (about 50 times a second) and by the controller doorway (about 185 times a second).
    // Cheap, never waits, ignored while the switch is off. `fromDoorway` only tells the facts file which of the two saw it.
    void noteY(bool held, bool fromDoorway);

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
        tzscan::ClassInfo loco;                    // stage D8b: ShovelTools.PlayerLocomotion (the player; the jump state lives there). Optional.
        void* rbKlass = nullptr;                   // UnityEngine.Rigidbody
        // Basketball
        int bRigid = -1, bWasShot = -1, bShotMade = -1, bUnheld = -1, bHoopsLen = -1, bOrigDrag = -1, bCurPos = -1;
        int bHoop[6] = {-1, -1, -1, -1, -1, -1};   // north, south, north01, south01, north02, south02
        // BallControlManager
        int cTimerL = -1, cTimerR = -1, cLastRel = -1, cBall = -1, cRaw = -1;
        // stage D8b: the player's jump state. BallControlManager._playerLocomotion -> PlayerLocomotion._locomotionVerticalState (+ two "jump pressed" flags, for the report)
        int cLoco = -1, lVert = -1, lJumpL = -1, lJumpR = -1, lState = -1, lRead = 0;     // lRead = how many bytes of the player object are read
        bool jumpOk = false; std::string jumpWhy;                                          // jumpOk: the jump state can be read; jumpWhy: why not
        // GameManager
        int gBcm = -1, gState = -1, gComp = -1, gGm = -1, gSolo = -1, gNba = -1;
        // engine and game methods (null = not found)
        void *mGetVel = nullptr, *mSetVel = nullptr, *mGetPos = nullptr, *mGetDrag = nullptr, *mGetUseGravity = nullptr,
             *mGetGravity = nullptr, *mGetFixedDt = nullptr, *mGmInstance = nullptr, *mGmOwned = nullptr;
    };
    // ---- stage D9 (Bank mode): everything the bank needs from the game, found on the link thread the first time Bank mode is on. Every piece has a name for the facts file.
    struct BankLayout {
        bool ok = false;
        std::string why;                           // why it cannot be used
        tzscan::ClassInfo props, goal, gman;       // BasketballProperties (the ball's physics settings), BasketballGoal (hoop + backboard), BasketballGoalManager (the list of goals)
        int bProps = -1, bAssistGoal = -1, bAssistTime = -1, bAngDrag = -1;                       // in the ball: _properties, _assistInGoal, _lastBankAssistInTime, _originalAngularDrag
        int pCollider = -1, pBaseMat = -1, pRuntimeMat = -1, pRtBounce = -1, pRtStatic = -1, pRtDyn = -1;       // in the ball properties
        int gRimT = -1, gBoardT = -1, gNormal = -1, gSize = -1, gOffset = -1, gRimRadius = -1;    // in a goal
        int mgGoals = -1;                          // in the goal manager: the list of goals
        void* klassCollider = nullptr;             // UnityEngine.Collider (its System.Type object is made fresh when needed)
        std::vector<std::string> combineNames, ccdNames;     // the engine's PhysicMaterialCombine / CollisionDetectionMode value names in order (so the numbers are looked up, not remembered)
        void *mGoalRim = nullptr, *mGoalBoard = nullptr, *mGmanInstance = nullptr;
        void *mTPos = nullptr, *mTScale = nullptr, *mTChildCount = nullptr, *mTGetChild = nullptr, *mCTransform = nullptr;
        void *mGetComp = nullptr, *mGetCompKids = nullptr, *mGetCompParent = nullptr;
        // stage D9b: more ways to find the backboard's collider (the real game keeps it OUTSIDE the goal's own little group of objects)
        void *mTParent = nullptr, *mObjName = nullptr, *mCIsTrigger = nullptr, *mCEnabled = nullptr;
        struct Overload { void* m = nullptr; std::vector<std::string> types; };      // an engine function and the type names of all its parameters
        Overload ovCompsKids, ovOverlapSphere, ovOverlapBox, ovFindAll;               // Component.GetComponentsInChildren(Type[,bool]), Physics.OverlapSphere / OverlapBox, Object.FindObjectsOfType(Type)
        std::string engineList;                                                        // the engine's search calls that exist (for the facts file)
        void *mCBounds = nullptr, *mCMaterial = nullptr, *mSRadius = nullptr;
        void *mMBounce = nullptr, *mMDyn = nullptr, *mMStat = nullptr, *mMBounceCombine = nullptr, *mMFrictionCombine = nullptr;
        void *mRAngVel = nullptr, *mRAngDrag = nullptr, *mRMass = nullptr, *mRInertia = nullptr, *mRCcd = nullptr, *mPBounceThr = nullptr;
    };
    // One backboard as measured from the game (kept for the next shots). Game thread only.
    struct BoardInfo {
        uintptr_t goal = 0, collider = 0;
        bool built = false;
        tzaim::Vec3 centre, n;                     // the middle of the FRONT face; the direction from the face towards the court
        tzaim::Vec3 cBounds;                       // where the collider's box was when it was measured (to notice a moved / replaced board)
        float halfW = 0, halfH = 0, thick = 0;
        std::string how;                           // where the numbers came from (for the facts file)
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
        std::vector<int> sampleAt;                  // the steps at which the flight is compared with the maths (empty = the standard list; Bank mode: around the touch with the board)
        bool crossed = false; float crossMiss = 0, crossT = 0; tzaim::Vec3 crossPos;
        float closest = 1e9f; int closestStep = 0;
        int madeFlag = -1;                           // _shotMade at the end: -1 unknown, 0 no, 1 yes
        bool overridden = false; float overrideDiff = 0;
        std::string why;                             // when nothing was changed
        // stage D8c: the Y button at the moment of the release (and again when the decision is made)
        bool holdY = false;                          // the menu's "Hold Y to aim" at that moment
        bool yKnown = false, yHeldAtRelease = false; // yKnown: the Y button was being read; yHeldAtRelease: it was held when you let go
        double yAgo = -1;                            // when not held at the release: seconds since it was last seen held (-1 = not within this session)
        bool yRecent = false, yAtDecision = false;   // let go of Y up to yGraceSeconds before the release / held when the decision was made
        // stage D8b (report only since D8c): the player's jump state, read at the moment of the release
        bool vKnown = false; int vState = -1;        // the game's vertical state number (0 = on the floor); vKnown = it could be read
        int jumpPressed = 0, locoState = -1;         // report only: bit 1 left jump pressed, bit 2 right jump pressed; the game's locomotion state number
        std::string vWhy;                            // when it could not be read
        // stage D9 (Bank mode)
        int mode = 1;                                // the mode at the moment of the release: 1 Direct, 2 Bank
        bool bankTried = false, bankOk = false;      // a bank plan was tried / a bank shot was worked out
        std::string bankWhy;                         // when not possible: the reason ("too close to the backboard")
        tzbank::Plan plan; tzbank::Scene scene;      // the bank plan and the numbers it was worked out from
        tzaim::Vec3 ring;                            // the ring centre the plan uses (the game's own GetRimCenter)
        float minB = 1e9f; int minBStep = 0; tzaim::Vec3 minBPos;          // watching: the closest the ball centre came to the board's front plane
        struct Trace { int step; tzaim::Vec3 pos, vel, spin; bool haveSpin; };
        std::vector<Trace> trace;                    // watching: position / speed / spin on the steps around the touch with the board
        float assistTime0 = 0, assistTime1 = 0; uintptr_t assistGoal0 = 0, assistGoal1 = 0;     // the game's own bank assist (fields of the ball) before / after
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
    void readJump(Shot& s);                                                       // game thread: the player's vertical state at the release (report only)
    void readY(Shot& s, double now);                                              // game thread: the Y button at the release
    std::string jumpText(const Shot& s) const;
    std::string yText(const Shot& s, bool readable) const;
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
    bool callInt(void* method, uintptr_t self, int* out);
    uintptr_t callObjArg(void* method, uintptr_t self, void* arg0);                // one argument: a game object itself, or a pointer to an int
    bool callBounds(void* method, uintptr_t self, tzaim::Vec3* centre, tzaim::Vec3* extents);
    uintptr_t callOverload(const BankLayout::Overload& o, uintptr_t self, const tzaim::Vec3* v0, const tzaim::Vec3* v1, float f, void* ref, bool boolFill);   // fills the arguments by their type names
    bool readObjArray(uintptr_t arr, size_t cap, std::vector<uintptr_t>* out, size_t* total);                     // a managed array of objects
    std::string bankNameOf(uintptr_t obj);                                                                         // the engine's name of an object ("" when it cannot be read)
    std::string bankClassOf(uintptr_t obj) const;                                                                  // the class name of an object

    // ---- stage D9 (Bank mode), aim_bank.cpp
    bool resolveBankLayout(std::string* why, bool* transient);                    // link thread
    void bankPrepare(double now);                                                 // game thread, idle ticks: measures the backboards before the first shot
    void bankShot(Shot& s, const std::string& head, const std::string& hoopTxt, const unsigned char* ballBytes, const tzaim::Vec3& pos, const tzaim::Vec3& vel, float elevDeg);
    bool bankListGoals(std::vector<uintptr_t>* out, std::string* why);
    bool bankGoalFor(const tzaim::Vec3& hoop, uintptr_t* goal, tzaim::Vec3* ring, std::string* why);
    bool bankBoardFor(uintptr_t goal, const tzaim::Vec3& ring, BoardInfo** out, std::string* why);
    bool bankBoardBuild(uintptr_t goal, const tzaim::Vec3& ring, BoardInfo** out, std::string* why);              // the real work behind bankBoardFor (which remembers failures)
    uintptr_t bankFindCollider(uintptr_t goal, uintptr_t boardT, const tzaim::Vec3& expectCentre, const tzaim::Vec3& size, bool* strict, tzaim::Vec3* cOut, tzaim::Vec3* eOut, std::string* how);
    bool bankMaterial(uintptr_t collider, float* bounce, float* dyn, float* stat, int* bounceCombine, int* frictionCombine, std::string* note);
    void bankWatchStep(Shot& s, int stepNow, const tzaim::Vec3& pos);
    std::string bankResultText(const Shot& s) const;
    void bankUnavailable(Shot& s, const std::string& head, const std::string& reason, const std::string& detail);

    Config cfg_;
    Layout L_;                                   // written once by the link thread, then read-only (published by layoutReady_)
    std::atomic<bool> layoutReady_{false};
    tzscan::CopyPipe pipeGame_, pipeLink_;       // one pipe per thread (a pipe is not shared between the two)
    bool pipesOk_ = false;
    std::atomic<bool> on_{false};
    std::atomic<int> mode_{0};                   // 0 off, 1 Direct, 2 Bank (on_ is true for both)
    BankLayout B_;                               // written once by the link thread, then read-only (published by bankReady_)
    std::atomic<bool> bankReady_{false}, bankTried_{false};
    double nextBankLayoutAt_ = 0; int bankLayoutTries_ = 0;       // under mu_
    std::string bankFailWhy_;                    // under mu_: why Bank mode cannot be used (shown in the menu)
    std::atomic<float> capM_{50.0f};
    std::atomic<bool> holdY_{false};
    // The Y button has two readers ([0] the menu's thread, [1] the controller doorway). Each keeps its latest reading and when it was made (the link's clock).
    // "Y is held" = ANY reader that is still reporting says held, so one reader that sees nothing (e.g. the doorway asked about the right hand only) cannot cancel the other.
    std::atomic<bool> yNow_[2] = {{false}, {false}};
    std::atomic<double> ySeen_[2] = {{-1e9}, {-1e9}};
    std::atomic<double> yHeldAt_{-1e9};          // when Y was last seen HELD by anyone
    bool yHeldNow(double now) const;             // some reader says held, and made that reading no more than 0.15 s ago
    bool yReadable(double now) const;            // some reader made a reading within the last second
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
        std::vector<BoardInfo> boards;             // stage D9: backboards measured so far
        struct BoardFail { uintptr_t goal = 0; double next = 0; int tries = 0; std::string why; };
        bool heavyOk = true;                       // stage D9b: the slow search through the whole scene is allowed on this attempt (the 1st and the 3rd only)
        std::vector<BoardFail> boardFails;         // stage D9b: a backboard that could not be measured is tried again later, not on every shot
        double nextBankPrep = 0; int bankPrepNotes = 0, bankShotNotes = 0;
    } g_;
    std::vector<unsigned char> bcmBuf_, ballBuf_, gmBuf_, locoBuf_;
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
    std::atomic<unsigned long long> calls_{0}, ticks_{0}, releases_{0}, shotLike_{0}, aimed_{0}, tooFar_{0}, wrongDir_{0}, noHoop_{0}, notShot_{0}, noSolution_{0}, refused_{0}, notHoldingY_{0}, yUnknown_{0},
                                    yReads_{0}, yHeld_{0}, yReadsDoor_{0}, yHeldDoor_{0}, errors_{0}, overridden_{0}, scored_{0}, missed_{0},
                                    bankAimed_{0}, bankUnavail_{0}, bankScored_{0}, bankMissed_{0};
    std::atomic<int> shotSeq_{0};
    std::atomic<double> onSinceAtomic_{-1};
    std::string engineDeadWhy_;                  // under mu_
    // thread
    pthread_t th_{};
    bool started_ = false;
    std::atomic<bool> quit_{false};
};

}  // namespace tzaimlink
