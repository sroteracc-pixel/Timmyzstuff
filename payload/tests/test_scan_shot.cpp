// PC test of the "ball and hoops" scan (il2cpp_scan.cpp, Topic::Shot) against a PRETEND libil2cpp.so (fake_il2cpp.cpp). Does NOT prove anything about the real game.
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
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
    std::printf("== which class names count as ball / hoop / shot\n");
    check("GymClassRimBend, RimSync, RimNet match (rim as a whole word part)", shotNameMatches("GymClassRimBend") && shotNameMatches("RimSync") && shotNameMatches("RimNet"));
    check("BasketballManager, BallPool, ShotPreferences, ThrowPowerSlider, ScoreBoard, Grabbable, AimAssist, HoopTrigger match",
          shotNameMatches("BasketballManager") && shotNameMatches("BallPool") && shotNameMatches("ShotPreferences") && shotNameMatches("ThrowPowerSlider") &&
          shotNameMatches("ScoreBoard") && shotNameMatches("Grabbable") && shotNameMatches("AimAssist") && shotNameMatches("HoopTrigger"));
    check("'Primary' and 'Trim' contain 'rim' inside a word: no match", !shotNameMatches("PrimaryColor") && !shotNameMatches("TrimTool") && !shotNameMatches("Criminal"));
    check("other sports do not match (FootballBall, SoccerBall, BaseballBat, PaintballGun)", !shotNameMatches("FootballBall") && !shotNameMatches("SoccerBall") && !shotNameMatches("BaseballBat") && !shotNameMatches("PaintballGun"));
    check("... unless the name says basketball too", shotNameMatches("FootballBasketballCombo"));
    check("BallPhysics and BallPhysicsUtilities match (the ball's own physics: found in the real facts file)", shotNameMatches("BallPhysics") && shotNameMatches("BallPhysicsUtilities"));
    check("unrelated names do not match (Menu, Network, Netting, PlayerMovement)", !shotNameMatches("Menu") && !shotNameMatches("Network") && !shotNameMatches("Netting") && !shotNameMatches("PlayerMovement"));

    std::printf("== the REAL class names (copied from your facts files) are picked the right way\n");
    {
        struct Real { const char* name; const char* parent; int rank; bool live; };
        // name (without namespace), parent class, expected detail rank, running copy searched?   (all are in Assembly-CSharp)
        static const Real real[] = {
            {"ParameterBasketballAngularDrag", "MulticastDelegate", -1, false}, {"ParameterBasketballBounciness", "MulticastDelegate", -1, false}, {"ParameterBasketballDrag", "MulticastDelegate", -1, false},
            {"ParameterBasketballDynamicFriction", "MulticastDelegate", -1, false}, {"ParameterBasketballMass", "MulticastDelegate", -1, false}, {"ParameterBasketballStaticFriction", "MulticastDelegate", -1, false},
            {"ParameterMaxThrowMultiplier", "MulticastDelegate", -1, false}, {"ParameterMinThrowAssistVelocity", "MulticastDelegate", -1, false}, {"ParameterRimPhysicsBounciness", "MulticastDelegate", -1, false},
            {"ParameterRimPhysicsDynamicFriction", "MulticastDelegate", -1, false}, {"ParameterRimPhysicsStaticFriction", "MulticastDelegate", -1, false}, {"ParameterShortThrowDirectionThreshold", "MulticastDelegate", -1, false},
            {"BallPhysics", "MonoBehaviour", 3, true}, {"BallPhysicsUtilities", "Object", 3, false}, {"BasketballPlayer", "Object", 3, true}, {"BasketballSinglePlayer", "MonoBehaviour", 3, true},
            {"SpawnBasketballSinglePlayer", "MonoBehaviour", 3, true}, {"RimPhysics", "Object", 3, true}, {"ClientShootParameters", "ValueType", 2, false}, {"ShootParameters", "ValueType", 2, false},
            {"HoldBallParameters", "ValueType", 3, false}, {"PlayerGivesShootOrderToBotCommand", "ACommand", 0, false}, {"SoccerBallPhysics", "MonoBehaviour", -1, false}, {"PlayerMovement", "MonoBehaviour", -1, false},
        };
        int wrongRank = 0, wrongLive = 0; std::string firstBad;
        for (const Real& r : real) {
            if (shotDetailRank(r.name, r.parent) != r.rank) { ++wrongRank; if (firstBad.empty()) firstBad = r.name; }
            if (shotLiveWanted(r.name, true, r.parent) != r.live) { ++wrongLive; if (firstBad.empty()) firstBad = std::string("live:") + r.name; }
        }
        if (!firstBad.empty()) std::printf("       (first wrong one: %s)\n", firstBad.c_str());
        check("all 24 real class names get the right detail rank (the 12 'Parameter...' events are skipped; BallPhysics, RimPhysics, BasketballPlayer ... are written out)", wrongRank == 0);
        check("... and the right live-search answer (BallPhysics, BasketballPlayer, BasketballSinglePlayer yes; events, structs, other classes no)", wrongLive == 0);
        check("the network room and views are searched even though they are not 'our' code; the transforms are not", shotLiveWanted("Normal.Realtime.Realtime", false, "MonoBehaviour") && shotLiveWanted("Normal.Realtime.RealtimeView", false, "MonoBehaviour") &&
              !shotLiveWanted("Normal.Realtime.RealtimeTransform", false, "MonoBehaviour") && !shotLiveWanted("Normal.Realtime.RealtimeVoice", false, "MonoBehaviour"));
    }

    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::printf("cannot load the pretend runtime: %s\n", dlerror()); return 2; }
    auto setShot = reinterpret_cast<void (*)(int)>(dlsym(lib, "fake_set_shot_world"));
    auto makeObj = reinterpret_cast<void* (*)(int, int)>(dlsym(lib, "fake_make_shot_object"));
    auto makePL = reinterpret_cast<void* (*)(int)>(dlsym(lib, "fake_make_player_locomotion"));
    Api api; std::string missing;
    check("loadApi works and also finds the three new optional functions", loadApi(lib, &api, &missing) && missing.empty() && api.runtime_invoke && api.class_get_method_from_name && api.resolve_icall);
    setShot(1);

    // running copies: two RimSync, three GymClassRimBend, one ball (held), the player (holding a ball: the field at 1040 is set)
    void* rs0 = makeObj(0, 0); void* rs1 = makeObj(0, 1);
    void* rb0 = makeObj(1, 0); void* rb1 = makeObj(1, 1); void* rb2 = makeObj(1, 2);
    void* ball = makeObj(2, 1);
    unsigned char* player = static_cast<unsigned char*>(makePL(0));
    { const uint64_t held = 0x7a12345000ULL; std::memcpy(player + 1040, &held, 8); }
    (void)rs0; (void)rs1; (void)rb0; (void)rb1; (void)rb2; (void)ball;
    void* bp0 = makeObj(3, 0); void* bp1 = makeObj(3, 1);              // the ball physics (two balls)
    void* rt0 = makeObj(4, 0);                                         // the network room: connected
    void* v0 = makeObj(5, 0); void* v1 = makeObj(5, 1); void* v2 = makeObj(5, 2);       // three network views
    void* tr0 = makeObj(6, 0);                                         // a RealtimeTransform copy: must NOT be searched
    (void)bp0; (void)bp1; (void)rt0; (void)v0; (void)v1; (void)v2; (void)tr0;

    Options opt; opt.topic = Topic::Shot; opt.maxSeconds = 60;
    const Summary s = run(api, opt, logFn);
    dump();
    check("run says ok", s.ok && s.error.empty());
    check("it says it is the ball and hoops scan", has("BALL AND HOOPS scan"));
    std::printf("== index\n");
    check("index: the rims, the slider, the ball and the shot preferences are listed",
          has("scan: index ShovelTools.RimSync : MonoBehaviour [IRL.GymFake] fields=3 methods=2") && has("scan: index ShovelTools.GymClassRimBend") && has("scan: index ShovelTools.GymClassSlider") &&
          has("scan: index ShovelTools.BasketballBall") && has("scan: index ShovelTools.ShotPreferences"));
    check("index: the hand-grab library classes are listed (they live in another assembly)", has("scan: index Autohand.GrabbableBase") && has("scan: index Autohand.Grabbable ") && has("scan: index Autohand.Hand "));
    check("index: PrimaryColor, FootballBall, BaseballBat, HandMenu, RealtimeVoice and the movement classes are NOT listed",
          !has("index ShovelTools.PrimaryColor") && !has("index ShovelTools.FootballBall") && !has("index ShovelTools.BaseballBat") && !has("index Autohand.HandMenu") && !has("index Normal.Realtime.RealtimeVoice") && !has("index Game."));
    check("index: BallPhysics, the bot command and the (useless) event ParameterBasketballMass are listed by name", has("scan: index ShovelTools.BallPhysics : MonoBehaviour") && has("scan: index ShovelTools.PlayerShotCommand : ACommand") && has("scan: index ShovelTools.ParameterBasketballMass : MulticastDelegate"));
    check("index: the three network classes are listed (they live in the Normal.Realtime assembly)", has("scan: index Normal.Realtime.Realtime : MonoBehaviour [Normal.Realtime]") && has("scan: index Normal.Realtime.RealtimeView ") && has("scan: index Normal.Realtime.RealtimeTransform "));
    check("index: exactly 14 lines (8 from before + BallPhysics + event + command + 3 network)", countOf("scan: index ") == 14);
    check("no movement index or single-line hits in this report", !has("scan: field-hit") && !has("scan: type-hit") && !has("index of class names (the game's own code + Normal"));
    std::printf("== detail\n");
    check("RimSync is written out with every field and its position", has("scan: CLASS ShovelTools.RimSync : MonoBehaviour") && has("field _rimTransform : UnityEngine.Transform @24") && has("field _hoopHeight : System.Single @36"));
    check("GymClassRimBend and GymClassSlider are written out", has("scan: CLASS ShovelTools.GymClassRimBend") && has("field _bendAmount : System.Single @32") && has("scan: CLASS ShovelTools.GymClassSlider"));
    check("BasketballBall (strong name) is written out, with its Rigidbody field", has("scan: CLASS ShovelTools.BasketballBall : MonoBehaviour") && has("field _body : UnityEngine.Rigidbody @24") && has("method OnRelease(1) : System.Void rva="));
    check("ShotPreferences (strong name) is written out", has("scan: CLASS ShovelTools.ShotPreferences"));
    check("the hand-grab classes are written out (Hand, GrabbableBase, Grabbable)", has("scan: CLASS Autohand.GrabbableBase : MonoBehaviour") && has("field body : UnityEngine.Rigidbody @24") && has("scan: CLASS Autohand.Grabbable : GrabbableBase") && has("scan: CLASS Autohand.Hand : MonoBehaviour"));
    check("PrimaryColor, FootballBall, BaseballBat, HandMenu, RealtimeVoice, and the movement classes are NOT written out",
          !has("CLASS ShovelTools.PrimaryColor") && !has("CLASS ShovelTools.FootballBall") && !has("CLASS ShovelTools.BaseballBat") && !has("CLASS Autohand.HandMenu") && !has("CLASS Normal.Realtime.RealtimeVoice") && !has("CLASS Game.") && !has("CLASS ShovelTools.PlayerLocomotion"));
    check("BallPhysics (the real ball physics class, only a name token 'ball') IS written out in full, with its fields", has("scan: CLASS ShovelTools.BallPhysics : MonoBehaviour") && has("field _drag : System.Single @32") && has("method OnThrown(1) : System.Void rva="));
    check("an event delegate (ParameterBasketballMass) is NOT written out: it would only waste a place", !has("CLASS ShovelTools.ParameterBasketballMass"));
    check("the bot command (parent ACommand) IS written out, but after the ball classes", has("scan: CLASS ShovelTools.PlayerShotCommand : ACommand"));
    check("the network classes are written out (room, view, transform) with their ownership methods", has("scan: CLASS Normal.Realtime.Realtime : MonoBehaviour") && has("field _connected : System.Boolean @24") && has("scan: CLASS Normal.Realtime.RealtimeView") &&
          has("method RequestOwnership(0)") && has("method get_isOwnedLocallySelf(0)") && has("scan: CLASS Normal.Realtime.RealtimeTransform"));
    check("no network class is reported missing in this pretend game", !has("network class") || !has("NOT found in this game"));
    check("13 classes written out in total (8 + BallPhysics + command + 3 network)", s.matchedClasses == 13);
    { int posBall = -1, posCmd = -1; for (size_t i = 0; i < gLines.size(); ++i) { if (gLines[i].find("scan: CLASS ShovelTools.BallPhysics") != std::string::npos) posBall = (int)i; if (gLines[i].find("scan: CLASS ShovelTools.PlayerShotCommand") != std::string::npos) posCmd = (int)i; }
      check("order: BallPhysics before the bot command", posBall >= 0 && posCmd > posBall); }
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
    check("the two RimSync copies are found and written", has("scan: live ShovelTools.RimSync: ") && has("2 look like a real running copy") && has("scan: live ShovelTools.RimSync #1 size=48 native-link=set in ") && has("scan: live ShovelTools.RimSync #2"));
    check("the three rim-bend copies are found and all three are written (up to 4 per class in this report)", has("scan: live ShovelTools.GymClassRimBend: ") && has("3 look like a real running copy") && has("GymClassRimBend #3"));
    check("a field value of a rim is written (the second RimSync has bend 0.25)", has("live _bend = 0.25   ("));
    check("a class whose name says basketball is looked for and found (the ball)", has("scan: live ShovelTools.BasketballBall: ") && has("live _isHeld = true   ("));
    check("the player is looked for, and ONLY its ball / rim / grab fields are written", has("scan: live ShovelTools.PlayerLocomotion #1") && has("live _rimSync = ") && has("live _grabUpdateBasketball = set   (") && !has("live _forwardMaxSpeed") && !has("live _gravity"));
    check("the ball physics are searched and found: both balls, with drag and held flag", has("scan: live ShovelTools.BallPhysics: ") && has("2 look like a real running copy") && has("live _drag = ") && has("live _inHand = true   ("));
    check("the network room is searched and written: connected and the client id", has("scan: live Normal.Realtime.Realtime #1") && has("live _connected = true   (") && has("live _clientId = 3   ("));
    check("the network views are searched and written: owner ids (up to 4 per class)", has("scan: live Normal.Realtime.RealtimeView: ") && has("3 look like a real running copy") && has("live _ownerId = "));
    check("RealtimeTransform copies are NOT searched (there are very many and the view says who owns it)", !has("scan: live Normal.Realtime.RealtimeTransform"));
    check("the event delegate and the bot command are NOT searched", !has("scan: live ShovelTools.ParameterBasketballMass") && !has("scan: live ShovelTools.PlayerShotCommand"));
    check("the summary counts the live copies (2 rims + 3 bends + 1 ball + 1 player + 2 ball physics + 1 room + 3 views = 13)", s.liveObjects == 13 && has("live-copies=13"));
    check("exactly 7 classes are planned for the live search (RimSync, GymClassRimBend, PlayerLocomotion, BallPhysics, Realtime, RealtimeView, BasketballBall) - never more than 12", s.liveClasses == 7);
    { size_t a = std::string::npos, b = std::string::npos;       // the important ones come before the "also wanted" BasketballBall in the plan line
      for (const auto& l : gLines) if (l.find("scan: step 5 done:") != std::string::npos) { a = l.find("Normal.Realtime.RealtimeView"); b = l.find("ShovelTools.BasketballBall"); }
      check("the plan puts the network views before the ball class (priority order)", a != std::string::npos && b != std::string::npos && a < b); }
    check("DONE line is there", has("scan: DONE."));

    std::printf("== the size budget\n");
    {
        size_t fullBytes = 0; for (const auto& l : gLines) fullBytes += l.size() + 1;       // (the previous run's output)
        gLines.clear();
        Options small = opt; small.maxBytes = fullBytes * 8 / 10; small.reservedBytes = fullBytes * 6 / 10; small.maxLines = 2000; small.reservedLines = 450;
        const Summary sb = run(api, small, logFn);
        size_t bytes = 0; for (const auto& l : gLines) bytes += l.size() + 1;
        std::printf("       (%zu bytes written with a %zu-byte budget; the full report is %zu bytes)\n", bytes, small.maxBytes, fullBytes);
        check("with a small byte budget the report stays inside it (plus the one closing DONE line)", sb.ok && bytes <= small.maxBytes + 400);
        check("... some ordinary lines were dropped, but the important lines (live summary, DONE) still got through", bytes < fullBytes && has("scan: DONE.") && has("scan: step 6 of 6") && has("scan: live ShovelTools.RimSync: "));
    }
    {
        gLines.clear();
        Options unlimited = opt;      // the real budget used by the payload
        unlimited.maxLines = 2000; unlimited.reservedLines = 450; unlimited.maxBytes = 190000; unlimited.reservedBytes = 45000;
        run(api, unlimited, logFn);
        size_t bytes = 0; for (const auto& l : gLines) bytes += l.size() + 1;
        check("the real budget (190 KB) is not hit by this small pretend game", bytes < 190000 && has("scan: DONE."));
    }
    std::printf("== the movement report is not changed by the new classes\n");
    {
        gLines.clear();
        Options mv; mv.brief = true; mv.maxSeconds = 60;
        const Summary sm = run(api, mv, logFn);
        check("a movement scan in the same game writes no ball or hoop classes and no engine section", sm.ok && !has("CLASS ShovelTools.RimSync") && !has("engine functions the Aimbot would need") && has("scan: CLASS ShovelTools.PlayerLocomotion"));
    }
    std::printf("\npassed: %d  failed: %d\n", pass, failn);
    return failn == 0 ? 0 : 1;
}
