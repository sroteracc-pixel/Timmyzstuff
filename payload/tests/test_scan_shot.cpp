// PC test of the "ball and hoops" scan (il2cpp_scan.cpp, Topic::Shot) against a PRETEND libil2cpp.so (fake_il2cpp.cpp). Does NOT prove anything about the real game.
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include "il2cpp_scan.h"

static std::vector<std::string> gLines;
static void logFn(const char* fmt, ...) { char b[1024]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap); gLines.push_back(b); }
static int pass = 0, failn = 0;
static void check(const char* what, bool ok) { if (ok) { ++pass; std::printf("  PASS  %s\n", what); } else { ++failn; std::printf("  FAIL  %s\n", what); } }
static bool has(const char* needle) { for (const auto& l : gLines) if (l.find(needle) != std::string::npos) return true; return false; }
static int countOf(const char* needle) { int n = 0; for (const auto& l : gLines) if (l.find(needle) != std::string::npos) ++n; return n; }
static void dump() { for (const auto& l : gLines) std::printf("      | %s\n", l.c_str()); }

int main(int, char** argv) {
    using namespace tzscan;
    std::printf("== which class names count as ball / hoop / shot (used only for the optional index)\n");
    check("GymClassRimBend, RimSync, RimNet match (rim as a whole word part)", shotNameMatches("GymClassRimBend") && shotNameMatches("RimSync") && shotNameMatches("RimNet"));
    check("'Primary' and 'Trim' contain 'rim' inside a word: no match", !shotNameMatches("PrimaryColor") && !shotNameMatches("TrimTool") && !shotNameMatches("Criminal"));
    check("other sports do not match (FootballBall, SoccerBall, BaseballBat, PaintballGun)", !shotNameMatches("FootballBall") && !shotNameMatches("SoccerBall") && !shotNameMatches("BaseballBat") && !shotNameMatches("PaintballGun"));

    std::printf("== the REAL class names (copied from the stage D7b lobby file) are picked the right way\n");
    {
        struct Real { const char* name; const char* parent; bool target; bool live; };
        // name (without namespace), parent class, written out in full?, running copy searched for?   (all are in Assembly-CSharp)
        static const Real real[] = {
            // the classes that matter
            {"Basketball", "MonoBehaviour", true, true}, {"BasketballShotAssist", "MonoBehaviour", true, true}, {"ShotAssistParams", "Object", true, true}, {"PredictedShotResult", "ValueType", true, false},
            {"BankShotCandidate", "ValueType", true, false}, {"RimTarget", "ValueType", true, false}, {"ShotData", "ValueType", true, false}, {"BasketballAssist", "Object", true, true},
            {"ThrowAssist", "Object", true, true}, {"BasketballGoal", "MonoBehaviour", true, true}, {"BasketballGoalManager", "MonoBehaviour", true, false}, {"HoopManager", "MonoBehaviour", true, true},
            {"NetRimReference", "MonoBehaviour", true, false}, {"BasketballGameContext", "Object", true, true}, {"GameManager", "MonoBehaviour", true, true}, {"BallControl", "MonoBehaviour", true, true},
            {"BallControlManager", "MonoBehaviour", true, true}, {"BasketballProperties", "MonoBehaviour", true, true}, {"ShootGesture", "Gesture", true, false}, {"ShootGameBall", "Holdable", true, false},
            {"BallPhysicsUtilities", "Object", true, false}, {"ReleasedBallCommand", "ACommand", true, false}, {"ShotManager", "MonoBehaviour", true, false}, {"ShotDetectionHelper", "MonoBehaviour", true, false},
            {"SteveBallSync", "RealtimeComponent`1", true, false}, {"BallController", "MonoBehaviour", true, false}, {"BasketballStateSync", "RealtimeComponent`1", false, true},
            // stage D10: the scoring classes (written out in full so the next step can see how a basket becomes points); only ScoreManager gets a live search
            {"ScoreManager", "MonoBehaviour", true, true}, {"PlayerScore", "MonoBehaviour", true, false}, {"ScoreSync", "RealtimeComponent`1", true, false}, {"ScoreSyncModel", "RealtimeModel", true, false},
            {"ScoreSyncHelper", "Object", true, false}, {"TeamScorePanelUI", "MonoBehaviour", true, false},
            // noise or already seen: not written out, not searched
            {"ParameterBasketballAngularDrag", "MulticastDelegate", false, false}, {"ParameterBasketballMass", "MulticastDelegate", false, false}, {"ParameterRimPhysicsBounciness", "MulticastDelegate", false, false},
            {"ParameterMaxThrowMultiplier", "MulticastDelegate", false, false}, {"CannonBall", "MonoBehaviour", false, false}, {"TetherBallCollision", "MonoBehaviour", false, false},
            {"BallPhysics", "MonoBehaviour", false, false}, {"RimPhysics", "Object", false, false}, {"BasketballPlayer", "Object", false, false}, {"BasketballSinglePlayer", "MonoBehaviour", false, false},
            {"SpawnGameBasketball", "MonoBehaviour", false, false}, {"BasketballMaterialSync", "RealtimeComponent`1", false, false}, {"PlayerNetworked", "MonoBehaviour", true, true},
            {"RimSync", "RealtimeComponent`1", false, false}, {"HoopHeight", "MonoBehaviour", false, false}, {"GymClassRimBend", "MonoBehaviour", false, false}, {"BallVFXAnchor", "VFXAnchor", false, false},
            {"PlayerLocomotion", "MonoBehaviour", false, false},
        };
        int wrongT = 0, wrongL = 0; std::string firstBad;
        for (const Real& r : real) {
            if (shotTargetName(r.name) != r.target) { ++wrongT; if (firstBad.empty()) firstBad = std::string("target:") + r.name; }
            if (shotLiveWanted(r.name, true, r.parent) != r.live) { ++wrongL; if (firstBad.empty()) firstBad = std::string("live:") + r.name; }
        }
        if (!firstBad.empty()) std::printf("       (first wrong one: %s)\n", firstBad.c_str());
        check("all real names get the right 'write it out in full' answer (the ball, the shot assist, the hoops yes; audio, tether ball, events, already-seen classes no)", wrongT == 0);
        check("... and the right 'search the running copies' answer (structs, events and everything else no)", wrongL == 0);
        check("a class of another assembly with a target name is never searched (ours=false)", !shotLiveWanted("Basketball", false, "MonoBehaviour"));
    }

    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    auto setShot = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_set_shot_world"));
    auto makeObj = reinterpret_cast<void* (*)(int, int)>(dlsym(lib, "fake_make_shot_object"));
    Api api; std::string missing;
    check("loadApi works and also finds the three optional functions", loadApi(lib, &api, &missing) && missing.empty() && api.runtime_invoke && api.class_get_method_from_name && api.resolve_icall);
    setShot(1);

    // running copies, as in the real game: two balls (one held), two sync objects, two hoops (north / south), the game's shot assist and its settings, the game manager
    // (+ some copies of decoy classes that must not be searched)
    void* b0 = makeObj(7, 0); void* b1 = makeObj(7, 1);
    void* st0 = makeObj(8, 0); void* st1 = makeObj(8, 1);
    void* g0 = makeObj(9, 0); void* g1 = makeObj(9, 1);
    void* sa = makeObj(10, 0); void* sp = makeObj(11, 0); void* gm = makeObj(12, 1);
    void* d1 = makeObj(0, 0); void* d2 = makeObj(3, 0); void* d3 = makeObj(4, 0); void* d4 = makeObj(5, 0);
    (void)b0; (void)b1; (void)st0; (void)st1; (void)g0; (void)g1; (void)sa; (void)sp; (void)gm; (void)d1; (void)d2; (void)d3; (void)d4;

    Options opt; opt.topic = Topic::Shot; opt.maxSeconds = 60; opt.listAssemblies = false;
    const auto t0 = std::chrono::steady_clock::now();
    const Summary s = run(api, opt, logFn);
    const double secondsTaken = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    dump();
    check("run says ok", s.ok && s.error.empty());
    check("it says it is the ball and hoops scan", has("BALL AND HOOPS scan"));
    std::printf("== what is left out\n");
    check("no index lines (the long class-name list is left out) and it says so", countOf("scan: index ") == 0 && has("the class-name index is left out"));
    check("no assembly list when it is switched off", countOf("scan: assembly ") == 0);
    check("no movement index or single-line hits in this report", !has("scan: field-hit") && !has("scan: type-hit") && !has("index of class names (the game's own code + Normal"));
    std::printf("== detail\n");
    check("the ball (Basketball) is written out with every field and position", has("scan: CLASS ShovelTools.Basketball : MonoBehaviour") && has("field _rb : UnityEngine.Rigidbody @24") && has("field _isHeld : System.Boolean @36") && has("method OnRelease(1)") && has("method Shoot(2)"));
    check("the game's shot assist is written out: assist object, its settings and the struct", has("scan: CLASS ShovelTools.BasketballShotAssist : MonoBehaviour") && has("method ComputeAssist(3) : UnityEngine.Vector3 rva=") &&
          has("scan: CLASS ShovelTools.ShotAssistParams") && has("field AssistRadius : System.Single @16") && has("scan: CLASS ShovelTools.PredictedShotResult : ValueType") && has("field willScore : System.Boolean @0"));
    check("the hoop (BasketballGoal) and the game manager are written out", has("scan: CLASS ShovelTools.BasketballGoal : MonoBehaviour") && has("field _isNorth : System.Boolean @32") && has("scan: CLASS ShovelTools.GameManager : MonoBehaviour") && has("field _officialMatch : System.Boolean @24"));
    check("the network classes RealtimeView and RealtimeTransform are written out (ownership)", has("scan: CLASS Normal.Realtime.RealtimeView : MonoBehaviour") && has("method RequestOwnership(0)") && has("method get_isOwnedLocallySelf(0)") && has("scan: CLASS Normal.Realtime.RealtimeTransform"));
    check("noise is NOT written out: CannonBall, TetherBallCollision, BallPhysics (a pitch system), BasketballBall, the event ParameterBasketballMass, RimSync, Realtime, PlayerLocomotion",
          !has("CLASS CannonBall") && !has("CLASS TetherBallCollision") && !has("CLASS ShovelTools.BallPhysics") && !has("CLASS ShovelTools.BasketballBall") && !has("CLASS ShovelTools.ParameterBasketballMass") &&
          !has("CLASS ShovelTools.RimSync") && !has("CLASS Normal.Realtime.Realtime :") && !has("CLASS ShovelTools.PlayerLocomotion") && !has("CLASS Autohand."));
    check("exactly 8 classes written out in this pretend game (6 targets present + 2 network)", s.matchedClasses == 8);
    check("the classes asked for but missing in this pretend game are named in one line (for example BankShotCandidate), the present ones are not", has("classes asked for by name but NOT found in this game:") && has("BankShotCandidate") && has("HoopManager") && !has("NOT found in this game: Basketball ") && !has("BasketballShotAssist BankShotCandidate"));
    { int pBall = -1, pAssist = -1, pGoal = -1, pMgr = -1;
      for (size_t i = 0; i < gLines.size(); ++i) {
          const std::string& l = gLines[i];
          if (l.find("scan: CLASS ShovelTools.Basketball :") != std::string::npos) pBall = (int)i;
          if (l.find("scan: CLASS ShovelTools.BasketballShotAssist") != std::string::npos) pAssist = (int)i;
          if (l.find("scan: CLASS ShovelTools.BasketballGoal :") != std::string::npos) pGoal = (int)i;
          if (l.find("scan: CLASS ShovelTools.GameManager") != std::string::npos) pMgr = (int)i;
      }
      check("order: ball, then shot assist, then hoop, then game manager", pBall >= 0 && pAssist > pBall && pGoal > pAssist && pMgr > pGoal); }
    std::printf("== engine functions (looked up only)\n");
    check("the report says which runtime functions exist", has("il2cpp_runtime_invoke=yes il2cpp_class_get_method_from_name=yes il2cpp_resolve_icall=yes il2cpp_thread_attach=yes"));
    check("Rigidbody is found in its engine assembly with the velocity functions", has("scan: engine class UnityEngine.Rigidbody found [assembly UnityEngine.PhysicsModule]") && has("method get_velocity(0) : UnityEngine.Vector3 rva=") && has("method set_velocity(1) : System.Void rva="));
    check("both AddForce versions are listed, MovePosition (not asked for) is not", countOf("method AddForce(") == 2 && !has("method MovePosition"));
    check("Transform, Component, Physics and Time are found", has("engine class UnityEngine.Transform found [assembly UnityEngine.CoreModule]") && has("method get_position(0) : UnityEngine.Vector3 rva=") &&
          has("engine class UnityEngine.Component found") && has("method get_transform(0)") && has("engine class UnityEngine.Physics found") && has("method get_gravity(0)") && has("engine class UnityEngine.Time found"));
    check("an icall that exists is reported found, one that does not is reported not found",
          has("icall UnityEngine.Rigidbody::get_velocity_Injected(UnityEngine.Vector3&) -> found (rva=") && has("icall UnityEngine.Rigidbody::set_velocity_Injected -> not found"));
    { int* invokes = static_cast<int*>(dlsym(lib, "fake_invoke_count"));
      check("nothing is ever CALLED in the game: runtime_invoke / class_get_method_from_name were never used", invokes && *invokes == 0); }
    std::printf("== live values\n");
    check("the plan lists the 6 classes that exist here, the ball first", s.liveClasses == 6 && has("scan: step 5 done: 6 class(es) planned for the live search: ShovelTools.Basketball(4 fields)"));
    check("the two balls are found; the held one is shown (_isHeld true, state 3)", has("scan: live ShovelTools.Basketball: ") && has("2 look like a real running copy") && has("live _isHeld = true   (") && has("live _state = 3   ("));
    check("the sync objects are found: held-left flag, shot data (a text), game-ball flag", has("scan: live BasketballStateSync: ") && has("live _isHeldLeft = true   (") && has("live _shotData = set   (") && has("live _isGameBall = true   ("));
    check("both hoops are found (north and south)", has("scan: live ShovelTools.BasketballGoal: ") && has("live _isNorth = true   (") && has("live _isNorth = false   ("));
    check("the shot assist and its settings are found with their numbers", has("scan: live ShovelTools.BasketballShotAssist #1") && has("live _strength = 0.6   (") && has("live _maxDistance = 12.5   (") && has("scan: live ShovelTools.ShotAssistParams #1") && has("live AssistRadius = 0.3   ("));
    check("the game manager is found: official-match flag", has("scan: live ShovelTools.GameManager #1") && has("live _officialMatch = true   ("));
    check("copies of decoy classes, the struct and the network room / views are NOT searched",
          !has("scan: live ShovelTools.RimSync") && !has("scan: live ShovelTools.BallPhysics") && !has("scan: live Normal.Realtime") && !has("scan: live ShovelTools.PredictedShotResult"));
    check("the summary counts the live copies (2 balls + 2 syncs + 2 hoops + 1 assist + 1 settings + 1 manager = 9)", s.liveObjects == 9 && has("live-copies=9"));
    check("no waiting without a delay setting (the scan was quick)", !has("scan: waiting") && secondsTaken < 20.0);
    check("DONE line is there", has("scan: DONE."));

    std::printf("== stage D10: the scoring classes and the code of the scoring methods\n");
    {
        auto addScore = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_add_score_class"));
        check("the pretend game can have a ScoreManager", addScore != nullptr);
        if (addScore) {
            addScore(1); gLines.clear();
            const Summary sc = run(api, opt, logFn);
            check("run says ok", sc.ok && sc.error.empty());
            check("ScoreManager is written out in full (fields and methods)", has("scan: CLASS ShovelTools.ScoreManager : MonoBehaviour") && has("field _northScore : System.Int32 @24") && has("method AddScore(1) : System.Void rva=") && has("method GetScore(0) : System.Int32 rva="));
            check("the code of the SCORING methods is written out: AddScore, GetScore and IncreasePoints (3 lines), not Reset", countOf("scan:   code320 ShovelTools.ScoreManager.") == 0 && countOf("scan:   code320 ScoreManager.") == 3 && !has("code320 ScoreManager.Reset"));
            // the method lines and the code lines must show the SAME rva (the fake keeps its method code 0x40 bytes apart: byte pattern 0x00, 0x01, 0x02 ... from the first method on)
            auto rvaOf = [&](const char* methodLine) { std::string r; for (const std::string& l : gLines) { const size_t p = l.find(methodLine); if (p != std::string::npos) { const size_t q = l.find("rva=", p); if (q != std::string::npos) r = l.substr(q + 4); } } return r; };
            const std::string rvaAdd = rvaOf("scan:   method AddScore(1) : System.Void"), rvaGet = rvaOf("scan:   method GetScore(0) : System.Int32");
            std::string line;
            for (const std::string& l : gLines) if (!rvaAdd.empty() && l.find("scan:   code320 ScoreManager.AddScore(1) rva=" + rvaAdd + " : ") != std::string::npos) line = l;
            const size_t colon = line.find(" : ");
            const std::string hex = colon == std::string::npos ? std::string() : line.substr(colon + 3);
            check("the dump has exactly 320 bytes (640 hex characters) and they are the bytes of the method (0x00 0x01 0x02 ... 0x3e 0x3f)", hex.size() == 640 && hex.compare(0, 12, "000102030405") == 0 && hex.compare(hex.size() - 4, 4, "3e3f") == 0);
            check("the dump of the second method has its own rva (the one of its method line) and starts with its own byte 0x40", !rvaGet.empty() && rvaGet != rvaAdd && has(("code320 ScoreManager.GetScore(0) rva=" + rvaGet + " : 404142434445").c_str()));
            check("nothing was CALLED in the game", [&] { int* invokes = static_cast<int*>(dlsym(lib, "fake_invoke_count")); return invokes && *invokes == 0; }());
            addScore(0); gLines.clear();
            const Summary s0 = run(api, opt, logFn);
            check("without a scoring class there are no code dumps and the scan is as before", s0.ok && countOf("scan:   code320") == 0 && s0.matchedClasses == 8);
        }
    }

    std::printf("== stage D11: the hand classes and the code of the steal / hitbox methods\n");
    {
        auto addScore_ = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_add_score_class"));
        auto addHand = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_add_hand_code"));
        check("the pretend game can have hand methods", addHand != nullptr);
        if (addHand) {
            addHand(0); gLines.clear();
            const Summary sh = run(api, opt, logFn);
            check("run says ok", sh.ok && sh.error.empty());
            check("the Hand class is written out in full (its field, and the Basketball class has the steal methods)", has("scan: CLASS Autohand.Hand : MonoBehaviour") && has("field holdingObj") && has("method CheckHandCollision(1) : System.Void rva=") && has("method TryKnockLooseFromBotHold(3)"));
            check("the code of the steal / hitbox methods is written out: CheckHandCollision, TryKnockLooseFromBotHold, PlayKnockLooseHaptics, SetColliderRadius, GetColliders (5 lines), not Grab or Shoot or OnRelease",
                  countOf("scan:   code320 ") == 5 && has("code320 Basketball.CheckHandCollision(1) rva=") && has("code320 Basketball.TryKnockLooseFromBotHold(3) rva=") && has("code320 Basketball.PlayKnockLooseHaptics(1) rva=") &&
                  has("code320 Hand.SetColliderRadius(1) rva=") && has("code320 Hand.GetColliders(0) rva=") && !has("code320 Hand.Grab") && !has("code320 Basketball.Shoot") && !has("code320 Basketball.OnRelease"));
            std::string line;
            for (const std::string& l : gLines) if (l.find("scan:   code320 Basketball.CheckHandCollision(1) rva=") != std::string::npos) line = l;
            const size_t colon = line.find(" : ");
            check("the dump has exactly 320 bytes (640 hex characters)", colon != std::string::npos && line.size() - colon - 3 == 640);
            check("nothing was CALLED in the game", [&] { int* invokes = static_cast<int*>(dlsym(lib, "fake_invoke_count")); return invokes && *invokes == 0; }());
            addHand(20); gLines.clear();
            const Summary sm = run(api, opt, logFn);
            check("with many more Collider methods the code dumps stop at 12 (the facts file stays small)", sm.ok && countOf("scan:   code320 ") == 12);
            addScore_(1); gLines.clear();
            const Summary sb = run(api, opt, logFn);
            check("scoring dumps and hitbox dumps have their own limits: 3 scoring + 12 hitbox", sb.ok && countOf("scan:   code320 ScoreManager.") == 3 && countOf("scan:   code320 ") == 15);
            addScore_(0);
            addHand(-1); gLines.clear();
            const Summary s0 = run(api, opt, logFn);
            check("without those methods there are no code dumps and the scan is as before", s0.ok && countOf("scan:   code320") == 0 && s0.matchedClasses == 8);
        }
    }

    std::printf("== the pause before the live search\n");
    {
        gLines.clear();
        Options waiting = opt; waiting.liveDelaySeconds = 2;
        const auto w0 = std::chrono::steady_clock::now();
        const Summary sw = run(api, waiting, logFn);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
        check("with a delay setting the scan says it is waiting (and why) and really waits", sw.ok && has("scan: waiting 2 seconds before looking at the running objects") && took >= 2.0);
        check("... and still finds the live objects afterwards", sw.liveObjects == 9 && has("scan: DONE."));
    }

    std::printf("== the size budget\n");
    {
        gLines.clear();
        const Summary base = run(api, opt, logFn);
        size_t fullBytes = 0; for (const auto& l : gLines) fullBytes += l.size() + 1;
        gLines.clear();
        Options small = opt; small.maxBytes = fullBytes * 8 / 10; small.reservedBytes = fullBytes * 6 / 10; small.maxLines = 2000; small.reservedLines = 450;
        const Summary sb = run(api, small, logFn);
        size_t bytes = 0; for (const auto& l : gLines) bytes += l.size() + 1;
        std::printf("       (%zu bytes written with a %zu-byte budget; the full report is %zu bytes)\n", bytes, small.maxBytes, fullBytes);
        check("with a small byte budget the report stays inside it (plus the one closing DONE line)", base.ok && sb.ok && bytes <= small.maxBytes + 400);
        check("... some ordinary lines were dropped, but the important lines (live summary, DONE) still got through", bytes < fullBytes && has("scan: DONE.") && has("scan: step 6 of 6") && has("scan: live ShovelTools.Basketball: "));
    }
    {
        gLines.clear();
        Options real = opt; real.maxLines = 3000; real.reservedLines = 500; real.maxBytes = 190000; real.reservedBytes = 45000;      // the budget the payload uses
        run(api, real, logFn);
        size_t bytes = 0; for (const auto& l : gLines) bytes += l.size() + 1;
        check("the real budget (190 KB) is not hit by this small pretend game", bytes < 190000 && has("scan: DONE."));
    }
    std::printf("== the optional index still works when asked for\n");
    {
        gLines.clear();
        Options withIndex = opt; withIndex.shotIndex = true;
        const Summary si = run(api, withIndex, logFn);
        check("with shotIndex the ball-and-hoop-like classes are listed (and the other sports are not)", si.ok && has("scan: index ShovelTools.RimSync") && has("scan: index ShovelTools.BasketballBall") && has("scan: index Autohand.GrabbableBase") && !has("index ShovelTools.FootballBall") && !has("index ShovelTools.PrimaryColor"));
    }
    std::printf("== the movement report is not changed by the new classes\n");
    {
        gLines.clear();
        Options mv; mv.brief = true; mv.maxSeconds = 60;
        const Summary sm = run(api, mv, logFn);
        check("a movement scan in the same game writes no ball or hoop classes and no engine section", sm.ok && !has("CLASS ShovelTools.Basketball") && !has("engine functions the Aimbot would need") && has("scan: CLASS ShovelTools.PlayerLocomotion"));
    }
    std::printf("\npassed: %d  failed: %d\n", pass, failn);
    return failn == 0 ? 0 : 1;
}
