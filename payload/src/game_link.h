// game_link.h - the part that REALLY reaches into the game (stage D6): Speed Boost, Jump Boost, Low / High Gravity.
//
// WHAT THIS IS (plain words)
//   The headset game keeps the player's movement numbers in ONE running object of the class ShovelTools.PlayerLocomotion
//   (the stage D5c scan found it and wrote down every field and its exact position). This file
//     1. finds that object in the game's memory (only when a switch is turned on),
//     2. remembers the game's own values ("the originals"),
//     3. writes   original x factor   into the fields that matter, and keeps them that way (the game may write its own value again),
//     4. puts the originals back exactly when a switch is turned off.
//   Fields used (all of them are floats; positions come from the game's own runtime, they are not guessed):
//     Speed Boost : _forwardMaxSpeed _forwardAcceleration _forwardDeceleration   (and the same three for lateral and backward)
//     Jump Boost  : _jumpHeightMultiplier
//     Gravity     : _gravity (the three numbers of the vector, all scaled)
//   Every read and write goes through a "safe copy" (the kernel says "no" for a bad address instead of the game crashing).
//   If the object disappears (new scene) the link notices, drops it and looks again.
//
//   NOT proven by any PC test: that the game really moves differently after these writes. That can only be seen on the headset.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "il2cpp_scan.h"
#include "movement.h"
#include "safe_copy.h"

namespace tzgame {

// Gives the link the game's il2cpp functions (called on the link thread, again and again until it works - the game may not have loaded libil2cpp.so yet).
typedef bool (*ApiProvider)(tzscan::Api* api, std::string* why);

struct LinkConfig {
    ApiProvider provider = nullptr;
    const char* ns = "ShovelTools";
    const char* cls = "PlayerLocomotion";
    int searchSeconds = 30;             // one memory search stops after this long
    int stallSeconds = 10;              // ... and is given up on when it makes no progress for this long
    int maxTargets = 4;                 // running copies that are changed at the same time
    int tickMs = 10;                    // how often the link checks the game's numbers while a switch is on (100 times a second)
    double retrySoonSeconds = 3.0;      // no active player object yet: look again after this long (grows to 15 s)
    double refreshSeconds = 45.0;       // while a switch is on: look again for new copies this often
    double minSearchGapSeconds = 2.0;   // never two searches closer together than this
    tzscan::LogFn note = nullptr;       // one line for the facts file (optional)
    int testHangAfterChunks = 0, testPollMicros = 0;       // only for the PC tests
};

class PlayerLink : public tzmove::Adapter {
public:
    PlayerLink();
    ~PlayerLink();
    bool start(const LinkConfig& cfg);          // starts the link thread. It does nothing in the game until setWanted(true).
    void stop();                                // stops the thread (the Controller puts the originals back before)

    // The menu has at least one movement switch on. Turning it on starts the search for the player object.
    void setWanted(bool wanted);

    // ---- tzmove::Adapter
    bool ready() override;                      // true when a live player object is linked
    bool captureOriginals() override;
    void apply(const tzmove::Effective& e) override;
    void restoreOriginals() override;

    // ---- for the menu and the facts file
    // 0 idle (no switch on), 1 connected, 2 looking for the player object, 3 failed (see failReason)
    int uiState() const;
    std::string failReason() const;
    std::string summary() const;                // one line: state, counters, the three key values of the first linked object
    int aliveTargets() const;
    unsigned long long tickCount() const { return ticks_.load(); }
    unsigned searches() const { return searches_.load(); }

    // For the PC tests: the current value of one of the managed floats in the first linked object (false if not linked).
    bool debugValue(const char* field, int component, float* now, float* original) const;

private:
    struct Def { const char* name; int comp; int offset; int group; float lo, hi; };
    struct Slot { int def = 0; float anchor = 0, orig = 0, last = 0; bool have = false, blocked = false; unsigned rebases = 0; };
    struct Target {
        uintptr_t addr = 0; bool alive = false; std::vector<Slot> slots;
        unsigned long long ticks = 0, writes = 0, overwrites = 0, rebases = 0, ignored = 0, headMoves = 0;
        float peakSpeed = 0, peakJumpSpeed = 0; unsigned char lastHead[12] = {0};
        std::string died;
    };
    struct Layout {
        bool ok = false; tzscan::ClassInfo cls; std::vector<Def> defs;
        int objSize = 0, hmdInit = -1, curSpeed = -1, curJumpSpeed = -1, head = -1;
    };

    static void* threadMain(void* self);
    void loop();
    bool resolveLayout(std::string* why, bool* transient);
    void doSearch();
    int judge(const std::vector<unsigned char>& bytes) const;          // 0 = looks like the active player, 1 = head tracking not started, 2 = numbers not sane, 3 = too short
    void install(const std::vector<uintptr_t>& addrs, const std::vector<std::vector<unsigned char>>& bytes, double now);
    void tickLocked(double now);                                       // check every linked object, write what must be written
    void processSlot(Target& t, Slot& s, const unsigned char* bytes, float factor);
    bool writeFloat(Target& t, int offset, float v);
    void kill(Target& t, const char* why);
    float factorFor(int group) const;
    bool readObject(uintptr_t addr, unsigned char* out, size_t n) const;       // safe copy, class pointer kept inverted
    int aliveLocked() const;
    void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void fail(const std::string& why);

    LinkConfig cfg_;
    mutable tzscan::CopyPipe pipe_;
    bool pipeOk_ = false;
    mutable std::mutex mu_;
    // ---- shared state (only under mu_)
    Layout layout_;
    std::vector<Target> targets_;
    std::vector<unsigned char> buf_;
    bool wanted_ = false, active_ = false, searching_ = false, failed_ = false, searchRequested_ = false;
    tzmove::Effective want_;
    std::string failReason_;
    double nextSearchAt_ = 0, nextRefreshAt_ = 1e18, lastSearchEnd_ = -1e9;
    int emptySearches_ = 0, notes_ = 0, putBackNotes_ = 0, applyNotes_ = 0, onNotes_ = 0, offNotes_ = 0;
    unsigned long long writesTotal_ = 0, overwritesTotal_ = 0, rebasesTotal_ = 0, ignoredTotal_ = 0, lostTotal_ = 0;
    std::string lastSearchText_;
    // ---- thread
    pthread_t th_{};
    bool started_ = false;
    std::atomic<bool> quit_{false};
    std::atomic<unsigned long long> ticks_{0};
    std::atomic<unsigned> searches_{0};
};

}  // namespace tzgame
