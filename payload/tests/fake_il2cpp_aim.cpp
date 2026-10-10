// A PRETEND libil2cpp.so + a pretend basketball world for the PC test of the aim link (stage D8).
// Class names, field names, types and positions are copied from the real stage D7c lobby facts file (ShovelTools.Basketball, ShovelTools.BallControlManager,
// ShovelTools.GameManager); the engine functions have the real names and argument counts (UnityEngine.Rigidbody get_velocity / set_velocity / get_position ...).
// The pretend ball is a real little physics simulation (Unity's step order: gravity, then drag, then move), so the test can check where a shot REALLY ends up.
// Nothing here proves anything about the real game. It only proves our code does what it should against a game that behaves as I read it.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
struct Klass;
struct Type { std::string name; Klass* klass; };
struct Field { std::string name; Type* type; size_t offset; int flags; };
struct Method { uint64_t methodPointer; std::string name; unsigned params; Type* ret; uint32_t flags; int id; std::vector<Type*> ptypes; };
struct Klass { std::string name, ns; Klass* parent; std::vector<Field> fields; std::vector<Method*> methods; int valueSize; bool isValue, isEnum; int instanceSize; };
struct Image { std::string name; std::vector<Klass*> classes; std::vector<Klass*> original; };

enum { M_NONE = 0, M_RB_GETVEL, M_RB_SETVEL, M_RB_GETPOS, M_RB_GETDRAG, M_RB_GETUSEGRAV, M_PH_GETGRAV, M_TM_FIXEDDT, M_GM_INSTANCE, M_GM_OWNED,
       // stage D9 (Bank mode)
       M_TR_POS, M_TR_SCALE, M_TR_CHILDCOUNT, M_TR_GETCHILD, M_CO_TRANSFORM, M_CO_GETCOMP_STR, M_CO_GETCOMP, M_CO_GETKIDS, M_CO_GETPARENT, M_COL_BOUNDS, M_COL_MATERIAL, M_SPH_RADIUS,
       M_MAT_BOUNCE, M_MAT_DYN, M_MAT_STAT, M_MAT_BCOMB, M_MAT_FCOMB, M_RB_ANGVEL, M_RB_ANGDRAG, M_RB_MASS, M_RB_INERTIA, M_RB_CCD, M_PH_BOUNCETHR, M_GOAL_RIM, M_GOAL_BOARD, M_GMAN_INSTANCE2,
       // stage D9b (finding the board's collider like the real game needs)
       M_TR_PARENT, M_CO_COMPS1, M_CO_COMPS2, M_PH_OVSPHERE2, M_PH_OVSPHERE3, M_PH_OVSPHERE4, M_PH_OVBOX2, M_PH_OVBOX5, M_OBJ_FIND1, M_OBJ_FIND2, M_OBJ_NAME, M_COL_TRIGGER, M_COL_ENABLED,
       // stage D11 (Hitbox expander): the engine's box / sphere / capsule collider sizes and the game's own "show hand colliders" function
       M_BOX_GET, M_BOX_SET, M_SPH_SET, M_CAP_RGET, M_CAP_RSET, M_CAP_HGET, M_CAP_HSET, M_BC_VIZ };

Klass kEnumState{"EGameState", "ShovelTools.GameManager", nullptr, {}, {}, 4, true, true, 4};
Klass kOther{"OtherThing", "Game", nullptr, {}, {}, 0, false, false, 32};
Type tInt{"System.Int32", nullptr}, tFloat{"System.Single", nullptr}, tBool{"System.Boolean", nullptr}, tVec3{"UnityEngine.Vector3", nullptr}, tVoid{"System.Void", nullptr},
     tState{"ShovelTools.GameManager.EGameState", &kEnumState}, tOther{"Game.OtherThing", &kOther};
Klass kBehaviour{"MonoBehaviour", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
Klass kObjectU{"Object", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Klass kComponent{"Component", "UnityEngine", nullptr, {}, {}, 0, false, false, 24};
Klass kRigidbody{"Rigidbody", "UnityEngine", &kComponent, {}, {}, 0, false, false, 24};
Klass kPhysics{"Physics", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Klass kTime{"Time", "UnityEngine", nullptr, {}, {}, 0, false, false, 16};
Type tRB{"UnityEngine.Rigidbody", &kRigidbody};
Klass kBall, kBcm, kGm, kLoco;
// stage D9 (Bank mode): the engine's Transform / Collider / SphereCollider / PhysicMaterial and the game's goal classes
Klass kTransform{"Transform", "UnityEngine", &kComponent, {}, {}, 0, false, false, 32};
Klass kCollider{"Collider", "UnityEngine", &kComponent, {}, {}, 0, false, false, 32};
Klass kSphere{"SphereCollider", "UnityEngine", &kCollider, {}, {}, 0, false, false, 32};
Klass kPhysMat{"PhysicMaterial", "UnityEngine", nullptr, {}, {}, 0, false, false, 32};
Klass kCombine{"PhysicMaterialCombine", "UnityEngine", nullptr, {}, {}, 4, true, true, 4};
Klass kCcd{"CollisionDetectionMode", "UnityEngine", nullptr, {}, {}, 4, true, true, 4};
Klass kProps, kGoal, kGman;
Type tTransform{"UnityEngine.Transform", &kTransform}, tSphere{"UnityEngine.SphereCollider", &kSphere}, tPhysMat{"UnityEngine.PhysicMaterial", &kPhysMat},
     tProps{"ShovelTools.BasketballProperties", &kProps}, tGoal{"ShovelTools.BasketballGoal", &kGoal}, tGman{"ShovelTools.BasketballGoalManager", &kGman},
     tGoalList{"System.Collections.Generic.List<ShovelTools.BasketballGoal>", &kOther},
     tSystemType{"System.Type", &kOther}, tString{"System.String", &kOther}, tCombineE{"UnityEngine.PhysicMaterialCombine", &kCombine}, tCcdE{"UnityEngine.CollisionDetectionMode", &kCcd},
     tColliderObj{"UnityEngine.Collider", &kCollider}, tQti{"UnityEngine.QueryTriggerInteraction", &kOther}, tQuat{"UnityEngine.Quaternion", nullptr};
std::map<Klass*, Type*> gKlassTypes;               // il2cpp_class_get_type
std::map<Type*, unsigned char*> gTypeObjects;      // il2cpp_type_get_object
Klass kEnumVert{"LocomotionVerticalState", "", nullptr, {}, {}, 4, true, true, 4};      // the player's vertical state (FLOOR, JUMPING, FALLING, GRABBING): a nested enum, so no namespace
Type tBall{"ShovelTools.Basketball", &kBall}, tBcm{"ShovelTools.BallControlManager", &kBcm}, tLoco{"ShovelTools.PlayerLocomotion", &kLoco},
     tVert{"ShovelTools.PlayerLocomotion.LocomotionVerticalState", &kEnumVert};

// stage D11 (Hitbox expander): the game's BallControl and Autohand.Hand and the engine's other collider kinds. Names, types and positions are copied from the real facts files.
Klass kGameObject{"GameObject", "UnityEngine", &kObjectU, {}, {}, 0, false, false, 24};
Klass kBoxC{"BoxCollider", "UnityEngine", &kCollider, {}, {}, 0, false, false, 32};
Klass kCapsuleC{"CapsuleCollider", "UnityEngine", &kCollider, {}, {}, 0, false, false, 32};
Klass kMeshC{"MeshCollider", "UnityEngine", &kCollider, {}, {}, 0, false, false, 32};
Klass kBallControl{"BallControl", "ShovelTools", &kBehaviour, {}, {}, 0, false, false, 232};
Klass kHand{"Hand", "Autohand", &kBehaviour, {}, {}, 0, false, false, 880};
Type tGameObject{"UnityEngine.GameObject", &kGameObject}, tBallControl{"ShovelTools.BallControl", &kBallControl}, tHand{"Autohand.Hand", &kHand}, tColliderArr{"UnityEngine.Collider[]", &kOther};

char gCode[0x1000];
Method* mk(const char* n, unsigned p, Type* r, int id, uint32_t fl = 0) { return new Method{reinterpret_cast<uint64_t>(gCode) + 0x100 + static_cast<uint64_t>(id) * 16, n, p, r, fl, id, {}}; }

Image iGame{"Assembly-CSharp", {&kBall, &kBcm, &kGm, &kLoco, &kEnumVert, &kProps, &kGoal, &kGman, &kBallControl, &kHand}, {}};
Image iPhys{"UnityEngine.PhysicsModule", {&kRigidbody, &kPhysics, &kCollider, &kSphere, &kPhysMat, &kCombine, &kCcd, &kBoxC, &kCapsuleC, &kMeshC}, {}};
Image iCore{"UnityEngine.CoreModule", {&kObjectU, &kComponent, &kTime, &kTransform, &kGameObject}, {}};
Image iMscorlib{"mscorlib", {}, {}};
void* gAssemblies[4] = {&iMscorlib, &iGame, &iPhys, &iCore};
int gDomain = 1;
bool gInit = false;

void buildMethods();
void buildFields() {       // called for every new scenario, so a scenario that breaks a class does not spoil the next one
    kBall.fields = {{"_currFrameVelocity", &tVec3, 24, 0}, {"playerNetworked", &tOther, 64, 0}, {"_rigidbody", &tRB, 72, 0}, {"_wasShot", &tBool, 80, 0}, {"_shotMade", &tBool, 81, 0},
         {"_ballControlManager", &tBcm, 144, 0}, {"_hoopsLength", &tInt, 164, 0}, {"_northHoopPosition", &tVec3, 180, 0}, {"_southHoopPosition", &tVec3, 192, 0},
         {"_northHoop01Position", &tVec3, 204, 0}, {"_southHoop01Position", &tVec3, 216, 0}, {"_northHoop02Position", &tVec3, 228, 0}, {"_southHoop02Position", &tVec3, 240, 0},
         {"_hoopRadius", &tFloat, 304, 0}, {"_currentBallPosition", &tVec3, 316, 0}, {"_dragValuesWereCleared", &tBool, 348, 0}, {"_originalDrag", &tFloat, 352, 0},
         {"_unheldTime", &tFloat, 388, 0}, {"_basketballState", &tInt, 396, 0}, {"_NEGATIVE_VELOCITY_Y", &tFloat, 0, 0x10}};
    kBcm.fields = {{"_leftBallControl", &tBallControl, 40, 0}, {"_isAI", &tBool, 290, 0}, {"_basketballRigidbody", &tRB, 152, 0}, {"_basketball", &tBall, 168, 0},
         {"_releasedLeftTimer", &tFloat, 200, 0}, {"_releasedRightTimer", &tFloat, 204, 0}, {"lastReleaseTime", &tFloat, 384, 0}, {"_lastRawThrowVelocity", &tVec3, 388, 0},
         {"_playerLocomotion", &tLoco, 144, 0}};      // (last in the list, so the tests that remove fields by number keep working)
    kLoco.fields = {{"_isLeftJumpPressed", &tBool, 240, 0}, {"_isRightJumpPressed", &tBool, 241, 0}, {"_locomotionState", &tInt, 812, 0}, {"_locomotionVerticalState", &tVert, 828, 0}};
    kGm.fields = {{"User", &tOther, 392, 0}, {"_playerBallControlManager", &tBcm, 408, 0}, {"_isInCompetitionMode", &tBool, 480, 0}, {"_isGMMode", &tBool, 760, 0}, {"_isSolo", &tBool, 761, 0},
         {"_isNBA", &tBool, 762, 0}, {"_gameState", &tState, 856, 0}};
    // stage D9: new ball fields at the END of the list (tests that remove fields by number keep working); the goal classes (names, types, positions from the real lobby facts file)
    kBall.fields.push_back({"_properties", &tProps, 264, 0}); kBall.fields.push_back({"_originalAngularDrag", &tFloat, 356, 0});
    kBall.fields.push_back({"_lastBankAssistInTime", &tFloat, 364, 0}); kBall.fields.push_back({"_assistInGoal", &tGoal, 368, 0});
    kProps.fields = {{"_basketballRigidbody", &tRB, 88, 0}, {"_basketballCollider", &tSphere, 96, 0}, {"_basketballPhysicMaterial", &tPhysMat, 104, 0}, {"_runtimePhysicMaterial", &tPhysMat, 128, 0},
                     {"_runtimeStaticFriction", &tFloat, 136, 0}, {"_runtimeDynamicFriction", &tFloat, 140, 0}, {"_runtimeBounciness", &tFloat, 144, 0},
                     {"_shootingPlayerId", &tOther, 152, 0}, {"_distanceFromStart", &tFloat, 160, 0}, {"_pointValue", &tInt, 164, 0}};      // (stage D10: the real position of _pointValue is 164)
    kGoal.fields = {{"_rimCenter", &tTransform, 24, 0}, {"_rimYOffset", &tFloat, 32, 0}, {"_rimBankYOffset", &tFloat, 36, 0}, {"_rimRadius", &tFloat, 40, 0}, {"_backboardCenter", &tTransform, 48, 0},
                    {"_backboardNormal", &tVec3, 56, 0}, {"_bankGridRows", &tInt, 68, 0}, {"_bankGridCols", &tInt, 72, 0}, {"_bankShotBoundsSize", &tVec3, 76, 0}, {"_bankShotBoundsOffset", &tVec3, 88, 0},
                    {"_backboardSize", &tVec3, 100, 0}, {"_backboardOffset", &tVec3, 112, 0}};
    kGman.fields = {{"_goals", &tGoalList, 24, 0}, {"_instance", &tGman, 0, 0x10}};
    kBcm.fields.push_back({"_rightBallControl", &tBallControl, 48, 0});
    kBallControl.fields = {{"_hand", &tHand, 48, 0}, {"_palmCollider", &tGameObject, 112, 0}, {"_fingerVizPrefab", &tGameObject, 160, 0}};
    kHand.fields = {{"left", &tBool, 80, 0}, {"palmRadius", &tFloat, 96, 0}, {"_handColliders", &tColliderArr, 144, 0}, {"_hasAuthority", &tBool, 512, 0}};
    kCombine.fields = {{"value__", &tInt, 0, 0}, {"Average", &tCombineE, 0, 0x56}, {"Multiply", &tCombineE, 0, 0x56}, {"Minimum", &tCombineE, 0, 0x56}, {"Maximum", &tCombineE, 0, 0x56}};
    kCcd.fields = {{"value__", &tInt, 0, 0}, {"Discrete", &tCcdE, 0, 0x56}, {"Continuous", &tCcdE, 0, 0x56}, {"ContinuousDynamic", &tCcdE, 0, 0x56}, {"ContinuousSpeculative", &tCcdE, 0, 0x56}};
}

void init() {
    if (gInit) return;
    gInit = true;
    for (Image* im : {&iGame, &iPhys, &iCore}) im->original = im->classes;
    kBall.name = "Basketball"; kBall.ns = "ShovelTools"; kBall.parent = &kBehaviour; kBall.instanceSize = 408;
    kBcm.name = "BallControlManager"; kBcm.ns = "ShovelTools"; kBcm.parent = &kBehaviour; kBcm.instanceSize = 424;
    kGm.name = "GameManager"; kGm.ns = "ShovelTools"; kGm.parent = &kBehaviour; kGm.instanceSize = 864;
    kLoco.name = "PlayerLocomotion"; kLoco.ns = "ShovelTools"; kLoco.parent = &kBehaviour; kLoco.instanceSize = 1056;
    kProps.name = "BasketballProperties"; kProps.ns = "ShovelTools"; kProps.parent = &kBehaviour; kProps.instanceSize = 176;
    kGoal.name = "BasketballGoal"; kGoal.ns = "ShovelTools"; kGoal.parent = &kBehaviour; kGoal.instanceSize = 128;
    kGman.name = "BasketballGoalManager"; kGman.ns = "ShovelTools"; kGman.parent = &kBehaviour; kGman.instanceSize = 40;
    kComponent.parent = &kObjectU;
    kBallControl.parent = &kBehaviour; kHand.parent = &kBehaviour;
    buildFields();
    buildMethods();
}

void buildMethods() {          // (also rebuilt for every scenario: a scenario that hides a method must not spoil the next one)
    kRigidbody.methods = {mk("get_velocity", 0, &tVec3, M_RB_GETVEL), mk("set_velocity", 1, &tVoid, M_RB_SETVEL), mk("get_position", 0, &tVec3, M_RB_GETPOS),
                          mk("get_drag", 0, &tFloat, M_RB_GETDRAG), mk("get_useGravity", 0, &tBool, M_RB_GETUSEGRAV)};
    kPhysics.methods = {mk("get_gravity", 0, &tVec3, M_PH_GETGRAV, 0x10)};
    kTime.methods = {mk("get_fixedDeltaTime", 0, &tFloat, M_TM_FIXEDDT, 0x10)};
    kGm.methods = {mk("get_Instance", 0, &tVoid, M_GM_INSTANCE, 0x10), mk("GetOwnedBallControlManager", 0, &tVoid, M_GM_OWNED)};
    // stage D9
    kRigidbody.methods.push_back(mk("get_angularVelocity", 0, &tVec3, M_RB_ANGVEL)); kRigidbody.methods.push_back(mk("get_angularDrag", 0, &tFloat, M_RB_ANGDRAG));
    kRigidbody.methods.push_back(mk("get_mass", 0, &tFloat, M_RB_MASS)); kRigidbody.methods.push_back(mk("get_inertiaTensor", 0, &tVec3, M_RB_INERTIA));
    kRigidbody.methods.push_back(mk("get_collisionDetectionMode", 0, &tCcdE, M_RB_CCD));
    kPhysics.methods.push_back(mk("get_bounceThreshold", 0, &tFloat, M_PH_BOUNCETHR, 0x10));
    kTransform.methods = {mk("get_position", 0, &tVec3, M_TR_POS), mk("get_lossyScale", 0, &tVec3, M_TR_SCALE), mk("get_childCount", 0, &tInt, M_TR_CHILDCOUNT), mk("GetChild", 1, &tTransform, M_TR_GETCHILD)};
    kTransform.methods[3]->ptypes = {&tInt};
    {   // the string overload comes FIRST on purpose: a lookup by name and argument count alone would pick the wrong one
        Method* a = mk("GetComponent", 1, &tColliderObj, M_CO_GETCOMP_STR); a->ptypes = {&tString};
        Method* b = mk("GetComponent", 1, &tColliderObj, M_CO_GETCOMP); b->ptypes = {&tSystemType};
        Method* c = mk("GetComponentInChildren", 1, &tColliderObj, M_CO_GETKIDS); c->ptypes = {&tSystemType};
        Method* d = mk("GetComponentInParent", 1, &tColliderObj, M_CO_GETPARENT); d->ptypes = {&tSystemType};
        kComponent.methods = {a, mk("get_transform", 0, &tTransform, M_CO_TRANSFORM), b, c, d};
    }
    kCollider.methods = {mk("get_bounds", 0, &tVoid, M_COL_BOUNDS), mk("get_sharedMaterial", 0, &tPhysMat, M_COL_MATERIAL)};
    kSphere.methods = {mk("get_radius", 0, &tFloat, M_SPH_RADIUS)};
    kPhysMat.methods = {mk("get_bounciness", 0, &tFloat, M_MAT_BOUNCE), mk("get_dynamicFriction", 0, &tFloat, M_MAT_DYN), mk("get_staticFriction", 0, &tFloat, M_MAT_STAT),
                        mk("get_bounceCombine", 0, &tCombineE, M_MAT_BCOMB), mk("get_frictionCombine", 0, &tCombineE, M_MAT_FCOMB)};
    kGoal.methods = {mk("GetRimCenter", 0, &tVec3, M_GOAL_RIM), mk("GetBackboardCenterPosition", 0, &tVec3, M_GOAL_BOARD)};
    kGman.methods = {mk("get_Instance", 0, &tGman, M_GMAN_INSTANCE2, 0x10)};
    // stage D9b: more ways to find the board's collider. Several overloads each, like the real engine (the link must pick the one with the fewest parameters)
    kTransform.methods.push_back(mk("get_parent", 0, &tTransform, M_TR_PARENT));
    {
        Method* c1 = mk("GetComponentsInChildren", 1, &tOther, M_CO_COMPS1); c1->ptypes = {&tSystemType};
        Method* c2 = mk("GetComponentsInChildren", 2, &tOther, M_CO_COMPS2); c2->ptypes = {&tSystemType, &tBool};
        kComponent.methods.push_back(c2); kComponent.methods.push_back(c1);
    }
    {
        Method* a4 = mk("OverlapSphere", 4, &tOther, M_PH_OVSPHERE4, 0x10); a4->ptypes = {&tVec3, &tFloat, &tInt, &tQti};
        Method* a3 = mk("OverlapSphere", 3, &tOther, M_PH_OVSPHERE3, 0x10); a3->ptypes = {&tVec3, &tFloat, &tInt};
        Method* a2 = mk("OverlapSphere", 2, &tOther, M_PH_OVSPHERE2, 0x10); a2->ptypes = {&tVec3, &tFloat};
        Method* b5 = mk("OverlapBox", 5, &tOther, M_PH_OVBOX5, 0x10); b5->ptypes = {&tVec3, &tVec3, &tQuat, &tInt, &tQti};
        Method* b2 = mk("OverlapBox", 2, &tOther, M_PH_OVBOX2, 0x10); b2->ptypes = {&tVec3, &tVec3};
        for (Method* m : {a4, a3, a2, b5, b2}) kPhysics.methods.push_back(m);
        Method* f2 = mk("FindObjectsOfType", 2, &tOther, M_OBJ_FIND2, 0x10); f2->ptypes = {&tSystemType, &tBool};
        Method* f1 = mk("FindObjectsOfType", 1, &tOther, M_OBJ_FIND1, 0x10); f1->ptypes = {&tSystemType};
        kObjectU.methods = {mk("get_name", 0, &tString, M_OBJ_NAME), f2, f1};
    }
    kCollider.methods.push_back(mk("get_isTrigger", 0, &tBool, M_COL_TRIGGER)); kCollider.methods.push_back(mk("get_enabled", 0, &tBool, M_COL_ENABLED));
    // stage D11: the engine's hitbox sizes (set_radius only exists on the sphere from now on; the ball's own sphere still uses get_radius only)
    { Method* sr = mk("set_radius", 1, &tVoid, M_SPH_SET); sr->ptypes = {&tFloat}; kSphere.methods.push_back(sr); }
    { Method* g = mk("get_size", 0, &tVec3, M_BOX_GET); Method* st = mk("set_size", 1, &tVoid, M_BOX_SET); st->ptypes = {&tVec3}; kBoxC.methods = {g, st}; }
    { Method* a = mk("get_radius", 0, &tFloat, M_CAP_RGET); Method* b = mk("set_radius", 1, &tVoid, M_CAP_RSET); b->ptypes = {&tFloat};
      Method* c = mk("get_height", 0, &tFloat, M_CAP_HGET); Method* d = mk("set_height", 1, &tVoid, M_CAP_HSET); d->ptypes = {&tFloat}; kCapsuleC.methods = {a, b, c, d}; }
    { Method* v = mk("SetHandColliderVisual", 2, &tVoid, M_BC_VIZ); v->ptypes = {&tGameObject, &tFloat}; kBallControl.methods = {v}; }
}

// ---------------------------------------------------------------- the pretend world
struct World {
    unsigned char *gm = nullptr, *bcm = nullptr, *ball = nullptr, *rb = nullptr, *loco = nullptr;
    double p[3] = {0, 1.6, 0}, v[3] = {0, 0, 0};
    double drag = 0.11, gravity = 9.81, fdt = 1.0 / 72.0;
    bool useGravity = true, held = true, released = false;
    bool getInstanceNull = false, getOwnedNull = false, throwAll = false, rbDestroyed = false;
    long invokes = 0, setVelCalls = 0, steps = 0, stepsSinceRelease = 0;
    int overrideAtStep = -1; double ov[3] = {0, 0, 0};
    double time = 0;
    int unheldMode = 0;            // 1 = _unheldTime never counts up (a game that does not use it as I read it)
};
World W;
std::vector<int> gAllocatedSize;

void setF(unsigned char* o, int off, float v) { std::memcpy(o + off, &v, 4); }
float getF(const unsigned char* o, int off) { float v; std::memcpy(&v, o + off, 4); return v; }
void setV(unsigned char* o, int off, double x, double y, double z) { setF(o, off, static_cast<float>(x)); setF(o, off + 4, static_cast<float>(y)); setF(o, off + 8, static_cast<float>(z)); }
std::vector<unsigned char*> gAllocated;          // every pretend object, so the next scenario can wipe them (a stale one would look like a second running copy)
unsigned char* makeObject(Klass* k, int size) {
    unsigned char* o = static_cast<unsigned char*>(calloc(1, static_cast<size_t>(size)));
    gAllocated.push_back(o); gAllocatedSize.push_back(size);
    std::memcpy(o, &k, 8);
    const uint64_t cached = reinterpret_cast<uint64_t>(o) + 0x40; std::memcpy(o + 16, &cached, 8);
    return o;
}

unsigned char gBox[16][48];
int gBoxNext = 0;
unsigned char* box() { unsigned char* b = gBox[gBoxNext++ & 15]; std::memset(b, 0, 48); return b; }

// ---------------------------------------------------------------- stage D9: the pretend backboards (two goals), the ball's collider and materials, and the physics of a ball hitting a board
// The ball hits an axis-aligned box (the board's collider). Rigid-body maths with a full 3x3 contact matrix (a different way of writing it than bank.cpp, on purpose).
struct Node { unsigned char* parent = nullptr; std::vector<unsigned char*> kids; double pos[3] = {0, 0, 0}, scale[3] = {1, 1, 1}; unsigned char* col = nullptr; };
struct Col { unsigned char* node = nullptr; bool sphere = false, physical = true, trigger = false, enabled = true; double c[3] = {0, 0, 0}, h[3] = {0, 0, 0}, radius = 0; unsigned char* mat = nullptr; int goal = -1, kind = 0; };   // kind 1 = board, 2 = pole
struct Mat { double bounce = 0, dyn = 0.6, stat = 0.6; int bcomb = 0, fcomb = 0; };
struct Bank {
    std::map<unsigned char*, Node> nodes; std::map<unsigned char*, Col> cols; std::map<unsigned char*, Mat> mats;
    unsigned char *gman = nullptr, *list = nullptr, *props = nullptr, *ballNode = nullptr, *ballCol = nullptr, *ballMat = nullptr, *boardMat = nullptr;
    std::map<unsigned char*, std::string> names;           // the engine's names of the pretend objects (Object.get_name)
    unsigned char *gnode[2] = {nullptr, nullptr};          // the goal's OWN transform (the root for layouts 0-2; a small leaf object for the real-like layouts)
    unsigned char *goal[2] = {nullptr, nullptr}, *root[2] = {nullptr, nullptr}, *rim[2] = {nullptr, nullptr}, *board[2] = {nullptr, nullptr}, *boardCol[2] = {nullptr, nullptr}, *poleCol[2] = {nullptr, nullptr};
    double ring[2][3] = {{0, 3.1, 12.66}, {0, 3.1, -12.66}};
    double ringDataDy = 0, boardDataDy = 0;
    double w[3] = {0, 0, 0}, relSpin[3] = {0, 0, 0}, angDrag = 0.05, mass = 0.6, kappa = 0.4, bounceThr = 2.0;
    int ccd = 0;
    bool hideManager = false, getCompBroken = false;
    // what happened to the last throw (the test reads it)
    int boardContacts = 0, poleContacts = 0, rimTouchSteps = 0, firstContactStep = 0;
    double contactPos[3] = {0, 0, 0}, vInN = 0, vOutN = 0, eUsed = 0, muUsed = 0, apprUsed = 0;
};
Bank Bk;

// ---------------------------------------------------------------- stage D10: the pretend scoring (what the game does with the ball's point value)
// The game writes ITS OWN number into the ball's point value when a throw is released (like SetPointValue does). The pretend scoreboard then counts a basket in one of three ways:
//   mode 0: it reads the ball's point value at the moment the ball goes in (the way the mod hopes the real game works)
//   mode 1: it uses the number the game wrote at the release (a copy; later changes of the ball's number change nothing)
//   mode 2: it ignores the ball and always counts 3
struct Pts {
    int gameNumber = 3;            // what the game writes at the release
    int mode = 0;
    int rewriteAt = -1, rewriteTo = 3;     // the game writes its own number AGAIN this many physics steps after the release (-1 = never)
    int score = 0, baskets = 0, snapshot = 0;
    unsigned char *ball2 = nullptr, *props2 = nullptr;     // "somebody else's" ball (never controlled by us)
    unsigned char* roProps = nullptr;                      // a settings object on a read-only page (writes into it are refused by the system)
};
Pts Ps;
int getPropsValue(unsigned char* props) { int v = 0; if (props) std::memcpy(&v, props + 164, 4); return v; }
void setPropsValue(unsigned char* props, int v) { if (props) std::memcpy(props + 164, &v, 4); }

// ---------------------------------------------------------------- stage D11: the pretend hands and their hitboxes
// Two hands of YOURS (reached from your ball control manager: _leftBallControl / _rightBallControl -> _hand -> _handColliders) and one hand of ANOTHER player (never linked from your
// ball control manager). Each hand has 5 hitboxes: a box, a sphere, two capsules and a mesh collider (a kind whose size cannot be changed). Index 0-4 left, 5-9 right, 10-14 the other player's.
struct HbCol { int kind = 0; double v[3] = {0, 0, 0}, orig[3] = {0, 0, 0}; };
struct Hbx {
    unsigned char *bc[3] = {nullptr, nullptr, nullptr}, *hand[3] = {nullptr, nullptr, nullptr}, *arr[3] = {nullptr, nullptr, nullptr}, *prefab[3] = {nullptr, nullptr, nullptr};
    std::vector<unsigned char*> order;
    std::map<unsigned char*, HbCol> cols;
    long sets = 0, gets = 0, vizCalls = 0; int viz[3] = {0, 0, 0}, vizOn[3] = {0, 0, 0}, vizOff[3] = {0, 0, 0};
    bool setThrows = false, getThrows = false, vizThrows = false, vizBool = false, icalls = false;
};
Hbx Hx;
unsigned char* mkRaw(size_t size);                                  // defined further down
void putPtr(unsigned char* o, int off, const void* p);              // defined further down
unsigned char* mkHbArray(const std::vector<unsigned char*>& v) {
    unsigned char* a = mkRaw(40 + 8 * v.size());
    const uint64_t n = v.size(); std::memcpy(a + 24, &n, 8);
    for (size_t i = 0; i < v.size(); ++i) putPtr(a, 32 + 8 * static_cast<int>(i), v[i]);
    return a;
}
// (re)builds one hand: 0 = your left, 1 = your right, 2 = another player's
void buildHand(int h) {
    const double scale = h == 2 ? 3.0 : 1.0;
    std::vector<unsigned char*> list;
    struct Spec { int kind; double v[3]; };
    const Spec specs[5] = {{1, {0.08, 0.02, 0.10}}, {2, {0.05, 0, 0}}, {3, {0.012, 0.06, 0}}, {3, {0.010, 0.05, 0}}, {4, {0, 0, 0}}};
    for (const Spec& sp : specs) {
        Klass* k = sp.kind == 1 ? &kBoxC : (sp.kind == 2 ? &kSphere : (sp.kind == 3 ? &kCapsuleC : &kMeshC));
        unsigned char* o = makeObject(k, 32);
        HbCol c; c.kind = sp.kind;
        for (int i = 0; i < 3; ++i) c.orig[i] = c.v[i] = sp.v[i] * scale;
        Hx.cols[o] = c; Hx.order.push_back(o); list.push_back(o);
    }
    Hx.arr[h] = mkHbArray(list);
    Hx.hand[h] = makeObject(&kHand, 880);
    putPtr(Hx.hand[h], 144, Hx.arr[h]); Hx.hand[h][512] = h == 2 ? 0 : 1; Hx.hand[h][80] = h == 0 ? 1 : 0;
    Hx.prefab[h] = makeObject(&kGameObject, 24);
    Hx.bc[h] = makeObject(&kBallControl, 232);
    putPtr(Hx.bc[h], 48, Hx.hand[h]); putPtr(Hx.bc[h], 160, Hx.prefab[h]);
}
void buildHands() {
    Hx = Hbx();
    for (int h = 0; h < 3; ++h) buildHand(h);
    putPtr(W.bcm, 40, Hx.bc[0]); putPtr(W.bcm, 48, Hx.bc[1]);
}

double ballRadius() {
    auto c = Bk.cols.find(Bk.ballCol);
    auto n = Bk.nodes.find(Bk.ballNode);
    if (c == Bk.cols.end() || n == Bk.nodes.end()) return 0.12;
    return c->second.radius * std::max(std::fabs(n->second.scale[0]), std::max(std::fabs(n->second.scale[1]), std::fabs(n->second.scale[2])));
}
double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross3(const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
// x = K^-1 b (3x3, Cramer's rule)
bool solve3(const double K[9], const double b[3], double x[3]) {
    const double det = K[0] * (K[4] * K[8] - K[5] * K[7]) - K[1] * (K[3] * K[8] - K[5] * K[6]) + K[2] * (K[3] * K[7] - K[4] * K[6]);
    if (std::fabs(det) < 1e-18) return false;
    double m[9];
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < 9; ++i) m[i] = K[i];
        for (int r = 0; r < 3; ++r) m[r * 3 + c] = b[r];
        x[c] = (m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6])) / det;
    }
    return true;
}
int combPriority(int value) {      // the engine's rule: Average < Minimum < Multiply < Maximum (by NAME, the numbers are the enum's order here)
    const std::string& nm = kCombine.fields[1 + static_cast<size_t>(value)].name;
    return nm == "Average" ? 0 : (nm == "Minimum" ? 1 : (nm == "Multiply" ? 2 : 3));
}
double mixRule(int ca, double a, int cb, double b) {
    const int p = std::max(combPriority(ca), combPriority(cb));
    return p == 0 ? 0.5 * (a + b) : (p == 1 ? std::min(a, b) : (p == 2 ? a * b : std::max(a, b)));
}
// closest point of a box to p; false when p is inside
bool ballVsBox(const double p[3], const Col& bx, double n[3], double* dist) {
    double d[3];
    for (int i = 0; i < 3; ++i) { const double q = std::max(bx.c[i] - bx.h[i], std::min(bx.c[i] + bx.h[i], p[i])); d[i] = p[i] - q; }
    const double l = std::sqrt(dot3(d, d));
    if (l < 1e-9) return false;
    for (int i = 0; i < 3; ++i) n[i] = d[i] / l;
    *dist = l;
    return true;
}
// the bounce of the ball (velocity W.v, spin Bk.w) off a surface with this normal (pointing out of the surface towards the ball). Returns false when the ball is not approaching.
bool bounceOff(const Col& bx, const double n[3]) {
    const double r = ballRadius(), m = Bk.mass, I = Bk.kappa * m * r * r;
    const double rc[3] = {-r * n[0], -r * n[1], -r * n[2]};
    double wxr[3]; cross3(Bk.w, rc, wxr);
    const double u[3] = {W.v[0] + wxr[0], W.v[1] + wxr[1], W.v[2] + wxr[2]};
    const double un = dot3(u, n);
    if (un >= 0) return false;
    Mat bm; { auto it = Bk.mats.find(Bk.ballMat); if (it != Bk.mats.end()) bm = it->second; }
    Mat wm; if (bx.mat) { auto it = Bk.mats.find(bx.mat); if (it != Bk.mats.end()) wm = it->second; }
    const double eMix = mixRule(bm.bcomb, bm.bounce, wm.bcomb, wm.bounce), mu = mixRule(bm.fcomb, bm.dyn, wm.fcomb, wm.dyn);
    const double e = -un > Bk.bounceThr ? eMix : 0.0;
    const double du[3] = {-e * un * n[0] - u[0], -e * un * n[1] - u[1], -e * un * n[2] - u[2]};      // stick: tangential contact speed -> 0, normal -> -e * un
    double K[9];
    for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) K[a * 3 + b] = (a == b ? 1.0 / m + r * r / I : 0.0) - rc[a] * rc[b] / I;
    double J[3];
    if (!solve3(K, du, J)) return false;
    const double jn = dot3(J, n);
    double jt[3] = {J[0] - jn * n[0], J[1] - jn * n[1], J[2] - jn * n[2]};
    const double jtl = std::sqrt(dot3(jt, jt));
    if (jtl > mu * jn && jtl > 1e-12) { for (int i = 0; i < 3; ++i) J[i] = jn * n[i] + (mu * jn / jtl) * jt[i]; }       // slides: the friction cone limits the sideways impulse
    const double vinN = dot3(W.v, n);
    for (int i = 0; i < 3; ++i) W.v[i] += J[i] / m;
    double rxj[3]; cross3(rc, J, rxj);
    for (int i = 0; i < 3; ++i) Bk.w[i] += rxj[i] / I;
    if (bx.kind == 1) {
        if (Bk.boardContacts == 0) { Bk.firstContactStep = static_cast<int>(W.stepsSinceRelease); Bk.vInN = vinN; Bk.vOutN = dot3(W.v, n); Bk.eUsed = e; Bk.muUsed = mu; Bk.apprUsed = -un; for (int i = 0; i < 3; ++i) Bk.contactPos[i] = W.p[i]; }
        ++Bk.boardContacts;
    } else if (bx.kind == 2) ++Bk.poleContacts;
    return true;
}
// discrete: the contacts found at the START of the step (the ball is checked where the last step left it) change the velocity, then the ball moves
void discreteContacts() {
    const double r = ballRadius();
    for (auto& kv : Bk.cols) {
        const Col& bx = kv.second;
        if (!bx.physical || bx.sphere) continue;
        double n[3], d;
        if (ballVsBox(W.p, bx, n, &d) && d < r && dot3(W.v, n) < 0) bounceOff(bx, n);
    }
}
// continuous: the exact moment of the touch inside the step
void moveContinuous() {
    const double r = ballRadius();
    double dx[3] = {W.v[0] * W.fdt, W.v[1] * W.fdt, W.v[2] * W.fdt};
    const int N = 400;
    for (int s = 0; s <= N; ++s) {
        const double f = static_cast<double>(s) / N;
        const double pt[3] = {W.p[0] + dx[0] * f, W.p[1] + dx[1] * f, W.p[2] + dx[2] * f};
        for (auto& kv : Bk.cols) {
            const Col& bx = kv.second;
            if (!bx.physical || bx.sphere) continue;
            double n[3], d;
            if (!(ballVsBox(pt, bx, n, &d) && d < r && dot3(W.v, n) < 0)) continue;
            double lo = s == 0 ? 0.0 : static_cast<double>(s - 1) / N, hi = f;
            for (int it = 0; it < 40; ++it) {
                const double mid = 0.5 * (lo + hi);
                const double q[3] = {W.p[0] + dx[0] * mid, W.p[1] + dx[1] * mid, W.p[2] + dx[2] * mid};
                double n2[3], d2;
                if (ballVsBox(q, bx, n2, &d2) && d2 < r) hi = mid; else lo = mid;
            }
            for (int i = 0; i < 3; ++i) W.p[i] += dx[i] * hi;
            double n3[3], d3;
            if (ballVsBox(W.p, bx, n3, &d3)) bounceOff(bx, n3);
            for (int i = 0; i < 3; ++i) W.p[i] += W.v[i] * W.fdt * (1.0 - hi);
            return;
        }
    }
    for (int i = 0; i < 3; ++i) W.p[i] += dx[i];
}
void countRim() {            // the ring itself is not solid in this pretend world; the test only wants to know whether the ball would have touched it
    const double r = ballRadius();
    for (int g = 0; g < 2; ++g) {
        const double dh = std::sqrt((W.p[0] - Bk.ring[g][0]) * (W.p[0] - Bk.ring[g][0]) + (W.p[2] - Bk.ring[g][2]) * (W.p[2] - Bk.ring[g][2]));
        const double dd = std::sqrt((dh - 0.2286) * (dh - 0.2286) + (W.p[1] - Bk.ring[g][1]) * (W.p[1] - Bk.ring[g][1]));
        if (dd < r + 0.01) { ++Bk.rimTouchSteps; return; }
    }
}
unsigned char* mkRaw(size_t size) { unsigned char* o = static_cast<unsigned char*>(calloc(1, size)); gAllocated.push_back(o); gAllocatedSize.push_back(static_cast<int>(size)); return o; }
unsigned char* mkNode(const double p[3], unsigned char* parent) {
    unsigned char* o = makeObject(&kTransform, 32);
    Node& n = Bk.nodes[o]; n.parent = parent; for (int i = 0; i < 3; ++i) n.pos[i] = p[i];
    if (parent) Bk.nodes[parent].kids.push_back(o);
    return o;
}
unsigned char* mkBox(unsigned char* node, const double c[3], const double h[3], unsigned char* mat, int goal, int kind) {
    unsigned char* o = makeObject(&kCollider, 32);
    Col& k = Bk.cols[o]; k.node = node; k.sphere = false; for (int i = 0; i < 3; ++i) { k.c[i] = c[i]; k.h[i] = h[i]; } k.mat = mat; k.goal = goal; k.kind = kind;
    Bk.nodes[node].col = o;
    return o;
}
unsigned char* mkMat(double bounce, double dyn, double stat, int bcomb, int fcomb) {
    unsigned char* o = makeObject(&kPhysMat, 32);
    Mat& m = Bk.mats[o]; m.bounce = bounce; m.dyn = dyn; m.stat = stat; m.bcomb = bcomb; m.fcomb = fcomb;
    return o;
}
void putPtr(unsigned char* o, int off, const void* p) { const uint64_t v = reinterpret_cast<uint64_t>(p); std::memcpy(o + off, &v, 8); }
// Two hoops like the real lobby: rings at (0, 3.1, +-12.66). Board: 1.83 x 1.07 x 0.05 m, its front face 0.38 m behind the ring centre, its centre 0.35 m above it.
void buildBank(int boardPlace) {
    Bk = Bank();
    const double kZero[3] = {0, 0, 0};
    Bk.ballMat = mkMat(0.8, 0.5, 0.5, 0, 0);          // bounce 0.8, friction 0.5, both "Average"
    Bk.boardMat = mkMat(0.6, 0.4, 0.4, 3, 0);         // bounce 0.6 mixed by "Maximum", friction 0.4 "Average"  -> the pair bounces 0.8 and rubs 0.45
    Bk.ballNode = mkNode(kZero, nullptr); Bk.nodes[Bk.ballNode].scale[0] = Bk.nodes[Bk.ballNode].scale[1] = Bk.nodes[Bk.ballNode].scale[2] = 0.24;
    { Bk.ballCol = makeObject(&kSphere, 32); Col& k = Bk.cols[Bk.ballCol]; k.node = Bk.ballNode; k.sphere = true; k.physical = false; k.radius = 0.5; k.mat = Bk.ballMat; Bk.nodes[Bk.ballNode].col = Bk.ballCol; }
    Bk.props = makeObject(&kProps, 176);
    putPtr(Bk.props, 88, W.rb); putPtr(Bk.props, 96, Bk.ballCol); putPtr(Bk.props, 104, Bk.ballMat); putPtr(Bk.props, 128, Bk.ballMat);
    setF(Bk.props, 136, 0.5f); setF(Bk.props, 140, 0.5f); setF(Bk.props, 144, 0.8f);
    putPtr(W.ball, 264, Bk.props); setF(W.ball, 356, 0.05f); setF(W.ball, 364, -1.0f);
    unsigned char* courtRoot = nullptr;
    Bk.list = mkRaw(32); { unsigned char* arr = mkRaw(32 + 16); const uint64_t two = 2; std::memcpy(arr + 24, &two, 8); putPtr(Bk.list, 0, &kOther); putPtr(Bk.list, 16, arr); const int sz = 2; std::memcpy(Bk.list + 24, &sz, 4);
                          for (int g = 0; g < 2; ++g) {
        const double s = g == 0 ? 1.0 : -1.0;
        const double ringP[3] = {0, 3.1, 12.66 * s};
        for (int i = 0; i < 3; ++i) Bk.ring[g][i] = ringP[i];
        const double rootP[3] = {0, 0, ringP[2] + s * 0.6};
        Bk.root[g] = mkNode(rootP, nullptr);
        Bk.names[Bk.root[g]] = g == 0 ? "Hoop_North" : "Hoop_South";
        const double boardC[3] = {0, ringP[1] + 0.35, ringP[2] + s * (0.38 + 0.025)};
        const double boardH[3] = {0.915, 0.535, 0.025};
        const bool realLike = boardPlace >= 3;
        // layouts 0-2: the goal's own transform is the root of everything (the collider is below it).
        // layouts 3, 4 (like the real game's facts file: the goal is a small leaf object whose children are only two markers):
        //   3: the board's collider sits on a SIBLING of the goal's object, under the hoop's root    4: it sits far away, under the court's root (only a physics query or a scene search finds it)
        Bk.gnode[g] = realLike ? mkNode(rootP, Bk.root[g]) : Bk.root[g];
        Bk.names[Bk.gnode[g]] = "BasketballGoal";
        Bk.rim[g] = mkNode(ringP, Bk.gnode[g]);
        Bk.names[Bk.rim[g]] = "RimCenter";
        const double rimH[3] = {0.25, 0.01, 0.25};                                    // a thin plate under the ring: a collider that is NOT the board
        unsigned char* rimHolder = realLike ? mkNode(ringP, Bk.root[g]) : Bk.rim[g];
        Bk.names[rimHolder] = "RimPlate";
        { unsigned char* rc = mkBox(rimHolder, ringP, rimH, nullptr, g, 3); Bk.cols[rc].physical = false; Bk.cols[rc].trigger = true; }       // (a trigger: it has a size and a place, but the ball flies through it)
        const double poleC[3] = {0, 1.75, ringP[2] + s * 0.62}, poleH[3] = {0.1, 1.75, 0.1};
        unsigned char* poleN = mkNode(poleC, Bk.root[g]);
        Bk.names[poleN] = "Pole";
        Bk.poleCol[g] = mkBox(poleN, poleC, poleH, nullptr, g, 2);
        Bk.board[g] = mkNode(boardC, Bk.gnode[g]);
        Bk.names[Bk.board[g]] = "BackboardCenter";
        if (boardPlace == 0) Bk.boardCol[g] = mkBox(Bk.board[g], boardC, boardH, Bk.boardMat, g, 1);                       // on the _backboardCenter object itself
        else if (boardPlace == 1) { unsigned char* glass = mkNode(boardC, Bk.board[g]); Bk.boardCol[g] = mkBox(glass, boardC, boardH, Bk.boardMat, g, 1); }   // on a child of it
        else if (boardPlace == 2) { unsigned char* sib = mkNode(boardC, Bk.root[g]); Bk.boardCol[g] = mkBox(sib, boardC, boardH, Bk.boardMat, g, 1); }                           // on a sibling object
        else if (boardPlace == 3) { unsigned char* glass = mkNode(boardC, Bk.root[g]); Bk.names[glass] = "Backboard_Glass"; Bk.boardCol[g] = mkBox(glass, boardC, boardH, Bk.boardMat, g, 1); }
        else {
            if (!courtRoot) { const double cp[3] = {0, 0, 0}; courtRoot = mkNode(cp, nullptr); Bk.names[courtRoot] = "Court"; }
            unsigned char* glass = mkNode(boardC, courtRoot); Bk.names[glass] = "Backboard_Glass"; Bk.boardCol[g] = mkBox(glass, boardC, boardH, Bk.boardMat, g, 1);
        }
        Bk.goal[g] = makeObject(&kGoal, 128);
        putPtr(Bk.goal[g], 24, Bk.rim[g]); setF(Bk.goal[g], 40, 0.2286f); putPtr(Bk.goal[g], 48, Bk.board[g]);
        setV(Bk.goal[g], 56, 0, 0, -s); setV(Bk.goal[g], 100, 1.83, 1.07, 0.05); setV(Bk.goal[g], 112, 0, 0, 0);
        putPtr(arr, 32 + 8 * g, Bk.goal[g]);
        Bk.nodes[Bk.root[g]].col = nullptr;
    } }
    Bk.gman = makeObject(&kGman, 40);
    putPtr(Bk.gman, 24, Bk.list);
}
}  // namespace

extern "C" {
#define EXPORT __attribute__((visibility("default")))

// ---- control of the pretend world (used by the test)
EXPORT void fake_aim_create(int withHoops) {
    init();
    buildFields();
    buildMethods();
    for (Image* im : {&iGame, &iPhys, &iCore}) im->classes = im->original;
    for (size_t i = 0; i < gAllocated.size(); ++i) { std::memset(gAllocated[i], 0, static_cast<size_t>(gAllocatedSize[i])); free(gAllocated[i]); }
    gAllocated.clear(); gAllocatedSize.clear();
    W = World();
    W.gm = makeObject(&kGm, 864); W.bcm = makeObject(&kBcm, 424); W.ball = makeObject(&kBall, 408); W.rb = makeObject(&kRigidbody, 24); W.loco = makeObject(&kLoco, 1056);
    const uint64_t pRb = reinterpret_cast<uint64_t>(W.rb), pBall = reinterpret_cast<uint64_t>(W.ball), pBcm = reinterpret_cast<uint64_t>(W.bcm);
    std::memcpy(W.ball + 72, &pRb, 8);
    std::memcpy(W.bcm + 168, &pBall, 8); std::memcpy(W.bcm + 152, &pRb, 8);
    { const uint64_t pLoco = reinterpret_cast<uint64_t>(W.loco); std::memcpy(W.bcm + 144, &pLoco, 8); }
    std::memcpy(W.gm + 408, &pBcm, 8);
    std::memcpy(W.ball + 144, &pBcm, 8);
    if (withHoops) { setV(W.ball, 180, 0, 3.1, 12.66); setV(W.ball, 192, 0, 3.1, -12.66); }
    const int hl = 1; std::memcpy(W.ball + 164, &hl, 4);
    setF(W.ball, 304, 0.2286f); setF(W.ball, 352, 0.11f);
    setF(W.bcm, 200, 12.33f); setF(W.bcm, 204, 5.0f);
    const int st = 3; std::memcpy(W.gm + 856, &st, 4); W.gm[760] = 1;
    gTypeObjects.clear();
    if (Ps.roProps) { munmap(Ps.roProps, 4096); }
    Ps = Pts();
    buildBank(0);
    buildHands();                       // stage D11
}
EXPORT void* fake_aim_object(int which) { return which == 0 ? W.gm : (which == 1 ? W.bcm : (which == 2 ? W.ball : W.rb)); }
EXPORT void fake_aim_set(const char* key, double a, double b, double c) {
    const std::string k = key;
    if (k == "drag") W.drag = a; else if (k == "gravity") W.gravity = a; else if (k == "fdt") W.fdt = a; else if (k == "usegravity") W.useGravity = a != 0;
    else if (k == "get_instance_null") W.getInstanceNull = a != 0; else if (k == "get_owned_null") W.getOwnedNull = a != 0; else if (k == "throw_all") W.throwAll = a != 0;
    else if (k == "override") { W.overrideAtStep = static_cast<int>(a); W.ov[0] = b; W.ov[1] = c; W.ov[2] = 0; }
    else if (k == "override_vz") W.ov[2] = a;
    else if (k == "unheld_mode") W.unheldMode = static_cast<int>(a);
    else if (k == "destroy_rb") { W.rbDestroyed = a != 0; const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.rb) + 0x40; std::memcpy(W.rb + 16, &z, 8); }
    else if (k == "destroy_bcm") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.bcm) + 0x40; std::memcpy(W.bcm + 16, &z, 8); }
    else if (k == "vstate") { const int v = static_cast<int>(a); std::memcpy(W.loco + 828, &v, 4); }                       // the player's vertical state: 0 floor, 1 jumping, 2 falling, 3 grabbing
    else if (k == "locostate") { const int v = static_cast<int>(a); std::memcpy(W.loco + 812, &v, 4); }
    else if (k == "jumpbtn") { W.loco[240] = a != 0; W.loco[241] = b != 0; }
    else if (k == "loco_link_null") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.loco); std::memcpy(W.bcm + 144, &z, 8); }
    else if (k == "destroy_loco") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(W.loco) + 0x40; std::memcpy(W.loco + 16, &z, 8); }
    else if (k == "break_vert_type") { for (Field& f : kLoco.fields) if (f.name == "_locomotionVerticalState") f.type = &tInt; }
    else if (k == "remove_loco_vert") { for (size_t i = 0; i < kLoco.fields.size(); ++i) if (kLoco.fields[i].name == "_locomotionVerticalState") { kLoco.fields.erase(kLoco.fields.begin() + static_cast<long>(i)); break; } }
    else if (k == "hoops_zero") { setV(W.ball, 180, 0, 0, 0); setV(W.ball, 192, 0, 0, 0); }
    else if (k == "break_rigidbody_type") { for (Field& f : kBall.fields) if (f.name == "_rigidbody") f.type = &tInt; }
    else if (k == "remove_field") { /* a = index of the field, b = 0: ball control class, 1: ball class */
        std::vector<Field>& fl = b == 0 ? kBcm.fields : kBall.fields;
        if (a >= 0 && a < static_cast<double>(fl.size())) fl.erase(fl.begin() + static_cast<long>(a)); }
    (void)b; (void)c;
}
EXPORT void fake_aim_hold(double x, double y, double z) {
    W.p[0] = x; W.p[1] = y; W.p[2] = z; W.v[0] = W.v[1] = W.v[2] = 0; W.held = true; W.released = false;
    setF(W.ball, 388, 0.0f); W.ball[80] = 0; W.ball[81] = 0;
    setV(W.ball, 316, x, y, z);
    Bk.boardContacts = Bk.poleContacts = Bk.rimTouchSteps = Bk.firstContactStep = 0; Bk.w[0] = Bk.w[1] = Bk.w[2] = 0;
}
// you let go: hand 1 = left, 2 = right, 3 = both. `throwVelocity` is what the player's throw gives the ball.
EXPORT void fake_aim_release(int hand, double vx, double vy, double vz) {
    W.held = false; W.released = true; W.stepsSinceRelease = 0;
    W.v[0] = vx; W.v[1] = vy; W.v[2] = vz;
    if (hand & 1) setF(W.bcm, 200, 0.0f);
    if (hand & 2) setF(W.bcm, 204, 0.0f);
    setF(W.bcm, 384, static_cast<float>(W.time));
    setV(W.bcm, 388, vx, vy, vz);
    W.ball[80] = 1;
    for (int i = 0; i < 3; ++i) Bk.w[i] = Bk.relSpin[i];
    setPropsValue(Bk.props, Ps.gameNumber); Ps.snapshot = Ps.gameNumber;          // stage D10: the game puts its own number into the ball (SetPointValue)
}
// one physics step (Unity's order: gravity, then drag, then move)
EXPORT void fake_aim_step() {
    ++W.steps; W.time += W.fdt;
    setF(W.bcm, 200, getF(W.bcm, 200) + static_cast<float>(W.fdt));
    setF(W.bcm, 204, getF(W.bcm, 204) + static_cast<float>(W.fdt));
    if (W.held) return;
    ++W.stepsSinceRelease;
    if (W.overrideAtStep >= 0 && W.stepsSinceRelease == W.overrideAtStep) { W.v[0] = W.ov[0]; W.v[1] = W.ov[1]; W.v[2] = W.ov[2]; }
    if (Ps.rewriteAt >= 0 && W.stepsSinceRelease == Ps.rewriteAt) setPropsValue(Bk.props, Ps.rewriteTo);        // stage D10: the game writes its own number again
    const double qy = W.p[1];
    if (W.useGravity) W.v[1] -= W.gravity * W.fdt;
    const double damp = 1.0 - W.drag * W.fdt;
    for (int i = 0; i < 3; ++i) W.v[i] *= damp;
    { const double wd = 1.0 - Bk.angDrag * W.fdt; for (int i = 0; i < 3; ++i) Bk.w[i] *= wd; }
    if (Bk.ccd == 1 || Bk.ccd == 2) moveContinuous();
    else { discreteContacts(); for (int i = 0; i < 3; ++i) W.p[i] += W.v[i] * W.fdt; }
    countRim();
    if (W.p[1] < 0.12) { W.p[1] = 0.12; W.v[1] = 0; W.v[0] *= 0.5; W.v[2] *= 0.5; }       // the floor
    if (W.unheldMode == 0) setF(W.ball, 388, getF(W.ball, 388) + static_cast<float>(W.fdt));
    setV(W.ball, 316, W.p[0], W.p[1], W.p[2]);
    // the game's own scoring: the ball comes down through a ring (inside the ring radius)
    for (int h = 0; h < 2; ++h) {
        const double hz = h == 0 ? 12.66 : -12.66;
        if (qy >= 3.1 && W.p[1] < 3.1 && W.v[1] < 0) {
            const double k = (qy - 3.1) / (qy - W.p[1]);
            const double cx = W.p[0] - W.v[0] * W.fdt * (1 - k), cz = W.p[2] - W.v[2] * W.fdt * (1 - k);
            if (std::sqrt(cx * cx + (cz - hz) * (cz - hz)) < 0.2286 && !W.ball[81]) {
                W.ball[81] = 1;
                Ps.score += Ps.mode == 0 ? getPropsValue(Bk.props) : (Ps.mode == 1 ? Ps.snapshot : 3);      // stage D10: the pretend scoreboard
                ++Ps.baskets;
            }
        }
    }
}
EXPORT void fake_aim_state(double* out) { for (int i = 0; i < 3; ++i) { out[i] = W.p[i]; out[3 + i] = W.v[i]; } out[6] = static_cast<double>(W.invokes); out[7] = static_cast<double>(W.setVelCalls); out[8] = W.ball[81]; out[9] = static_cast<double>(W.steps); }
EXPORT double fake_aim_fdt() { return W.fdt; }
// extra GameManager copies (for the memory-search tests): kind 0 = a copy whose ball control link is empty (junk), 1 = a copy that points at ANOTHER valid ball control object
EXPORT void* fake_aim_extra_gm(int kind) {
    unsigned char* g = makeObject(&kGm, 864);
    if (kind == 1) { unsigned char* b2 = makeObject(&kBcm, 424); const uint64_t p = reinterpret_cast<uint64_t>(b2); std::memcpy(g + 408, &p, 8); }
    return g;
}

// ---- stage D9: knobs and readings of the pretend backboards
EXPORT void fake_bank_set(const char* key, double a, double b, double c) {
    const std::string k = key;
    (void)b; (void)c;
    auto setMat = [&](unsigned char* m, double bounce, double fr) { Mat& mm = Bk.mats[m]; if (bounce >= 0) mm.bounce = bounce; if (fr >= 0) { mm.dyn = fr; mm.stat = fr; } };
    if (k == "board_e") setMat(Bk.boardMat, a, -1);
    else if (k == "board_mu") setMat(Bk.boardMat, -1, a);
    else if (k == "ball_e") { setMat(Bk.ballMat, a, -1); setF(Bk.props, 144, static_cast<float>(a)); }
    else if (k == "ball_mu") { setMat(Bk.ballMat, -1, a); setF(Bk.props, 140, static_cast<float>(a)); setF(Bk.props, 136, static_cast<float>(a)); }
    else if (k == "board_bcomb") Bk.mats[Bk.boardMat].bcomb = static_cast<int>(a);
    else if (k == "board_fcomb") Bk.mats[Bk.boardMat].fcomb = static_cast<int>(a);
    else if (k == "ball_bcomb") Bk.mats[Bk.ballMat].bcomb = static_cast<int>(a);
    else if (k == "ball_fcomb") Bk.mats[Bk.ballMat].fcomb = static_cast<int>(a);
    else if (k == "ccd") Bk.ccd = static_cast<int>(a);
    else if (k == "ang_drag") Bk.angDrag = a;
    else if (k == "kappa") Bk.kappa = a;
    else if (k == "bounce_threshold") Bk.bounceThr = a;
    else if (k == "release_spin") { Bk.relSpin[0] = a; Bk.relSpin[1] = b; Bk.relSpin[2] = c; }
    else if (k == "ball_scale") { Node& n = Bk.nodes[Bk.ballNode]; n.scale[0] = n.scale[1] = n.scale[2] = a; }
    else if (k == "rebuild") buildBank(static_cast<int>(a));                      // 0: board collider on the _backboardCenter object, 1: on its child, 2: on a sibling
    else if (k == "thick_board") { for (int g = 0; g < 2; ++g) { Col& cc = Bk.cols[Bk.boardCol[g]]; const double s = g == 0 ? 1.0 : -1.0; cc.c[2] += s * (a - cc.h[2]); cc.h[2] = a; } }       // a thicker box with the same FRONT face
    else if (k == "getcomp_broken") Bk.getCompBroken = a != 0;                    // GetComponent(Type) always answers "none" (a call that does not work as it is used)
    else if (k == "no_board_collider") { for (int g = 0; g < 2; ++g) { Bk.cols.erase(Bk.boardCol[g]); for (auto& kv : Bk.nodes) if (kv.second.col == Bk.boardCol[g]) kv.second.col = nullptr; } }
    else if (k == "destroy_board_collider") { for (int g = 0; g < 2; ++g) { const uint64_t z = 0; std::memcpy(Bk.boardCol[g] + 16, &z, 8); } }
    else if (k == "board_no_material") { for (int g = 0; g < 2; ++g) Bk.cols[Bk.boardCol[g]].mat = nullptr; }
    else if (k == "board_material_gone") { const uint64_t z = 0; std::memcpy(Bk.boardMat + 16, &z, 8); }
    else if (k == "wrong_size_collider") { for (int g = 0; g < 2; ++g) { Col& cc = Bk.cols[Bk.boardCol[g]]; for (int i = 0; i < 3; ++i) cc.h[i] *= a; } }
    else if (k == "flip_normal") { for (int g = 0; g < 2; ++g) { float z = getF(Bk.goal[g], 64); setF(Bk.goal[g], 64, -z); } }
    else if (k == "ring_data_dy") Bk.ringDataDy = a;
    else if (k == "board_data_dy") Bk.boardDataDy = a;
    else if (k == "board_size_data") { for (int g = 0; g < 2; ++g) setV(Bk.goal[g], 100, a, b, c); }
    else if (k == "hide_manager") Bk.hideManager = a != 0;
    else if (k == "goal_list_size") { const int sz = static_cast<int>(a); std::memcpy(Bk.list + 24, &sz, 4); }
    else if (k == "goal_list_null") putPtr(Bk.gman, 24, nullptr);
    else if (k == "ball_collider_null") putPtr(Bk.props, 96, nullptr);
    else if (k == "props_null") putPtr(W.ball, 264, nullptr);
    else if (k == "destroy_ball_collider") { const uint64_t z = 0; std::memcpy(Bk.ballCol + 16, &z, 8); }
    else if (k == "rb_mass") Bk.mass = a;
    else if (k == "assist_time") setF(W.ball, 364, static_cast<float>(a));
}
// hide one engine / game method (a game that was updated or stripped): every class that has a method with this name loses it
EXPORT void fake_bank_hide(const char* method) {
    Klass* all[] = {&kRigidbody, &kPhysics, &kTransform, &kCollider, &kSphere, &kPhysMat, &kComponent, &kGoal, &kGman, &kObjectU};
    for (Klass* k : all) for (size_t i = 0; i < k->methods.size();) { if (k->methods[i]->name == method) k->methods.erase(k->methods.begin() + static_cast<long>(i)); else ++i; }
}
// hide ONE overload of a method (the one with this many parameters), like a game whose build kept only some overloads
EXPORT void fake_bank_hide_params(const char* method, int nparams) {
    Klass* all[] = {&kRigidbody, &kPhysics, &kTransform, &kCollider, &kSphere, &kPhysMat, &kComponent, &kGoal, &kGman, &kObjectU};
    for (Klass* k : all) for (size_t i = 0; i < k->methods.size();) { if (k->methods[i]->name == method && static_cast<int>(k->methods[i]->params) == nparams) k->methods.erase(k->methods.begin() + static_cast<long>(i)); else ++i; }
}
// change a class of the pretend game: action 0 = remove the field, 1 = give the field the wrong type (an int), 2 = remove the whole class from the game
EXPORT void fake_bank_field(const char* klass, const char* field, int action) {
    Klass* all[] = {&kBall, &kProps, &kGoal, &kGman, &kBcm, &kRigidbody, &kTransform, &kCollider, &kSphere, &kPhysMat, &kCombine, &kCcd};
    for (Klass* k : all) {
        if (k->name != klass) continue;
        if (action == 2) { for (Image* im : {&iGame, &iPhys, &iCore}) for (size_t i = 0; i < im->classes.size();) { if (im->classes[i] == k) im->classes.erase(im->classes.begin() + static_cast<long>(i)); else ++i; } return; }
        for (size_t i = 0; i < k->fields.size(); ++i) if (k->fields[i].name == field) {
            if (action == 0) k->fields.erase(k->fields.begin() + static_cast<long>(i)); else k->fields[i].type = &tInt;
            return;
        }
    }
}
EXPORT void fake_bank_state(double* out) {
    out[0] = Bk.boardContacts; out[1] = Bk.poleContacts; out[2] = Bk.rimTouchSteps; out[3] = Bk.firstContactStep;
    out[4] = Bk.contactPos[0]; out[5] = Bk.contactPos[1]; out[6] = Bk.contactPos[2];
    out[7] = Bk.vInN; out[8] = Bk.vOutN; out[9] = Bk.eUsed; out[10] = Bk.muUsed; out[11] = Bk.apprUsed; out[12] = ballRadius();
}
EXPORT void* fake_bank_object(int which, int goal) {      // 0 goal, 1 goal manager, 2 board collider, 3 board node, 4 props
    switch (which) { case 0: return Bk.goal[goal & 1]; case 1: return Bk.gman; case 2: return Bk.boardCol[goal & 1]; case 3: return Bk.board[goal & 1]; default: return Bk.props; }
}

// ---- stage D10: knobs and readings of the pretend scoring
EXPORT void fake_pts_set(const char* key, double a, double b) {
    const std::string k = key;
    if (k == "game_number") Ps.gameNumber = static_cast<int>(a);
    else if (k == "mode") Ps.mode = static_cast<int>(a);
    else if (k == "rewrite") { Ps.rewriteAt = static_cast<int>(a); Ps.rewriteTo = static_cast<int>(b); }
    else if (k == "value") setPropsValue(Bk.props, static_cast<int>(a));
    else if (k == "other_value") setPropsValue(Ps.props2, static_cast<int>(a));
    else if (k == "remove_pointvalue") { for (size_t i = 0; i < kProps.fields.size(); ++i) if (kProps.fields[i].name == "_pointValue") { kProps.fields.erase(kProps.fields.begin() + static_cast<long>(i)); break; } }
    else if (k == "retype_pointvalue") { for (Field& f : kProps.fields) if (f.name == "_pointValue") f.type = &tFloat; }
    else if (k == "remove_properties") { for (size_t i = 0; i < kBall.fields.size(); ++i) if (kBall.fields[i].name == "_properties") { kBall.fields.erase(kBall.fields.begin() + static_cast<long>(i)); break; } }
    else if (k == "retype_properties") { for (Field& f : kBall.fields) if (f.name == "_properties") f.type = &tInt; }
    else if (k == "destroy_props") { const uint64_t z = a != 0 ? 0 : reinterpret_cast<uint64_t>(Bk.props) + 0x40; std::memcpy(Bk.props + 16, &z, 8); }
    else if (k == "props_null") putPtr(W.ball, 264, a != 0 ? nullptr : Bk.props);
    else if (k == "props_readonly") {          // a copy of the settings object on a page the system will not let anybody write
        void* pg = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pg != MAP_FAILED) { std::memcpy(pg, Bk.props, 176); mprotect(pg, 4096, PROT_READ); Ps.roProps = static_cast<unsigned char*>(pg); putPtr(W.ball, 264, Ps.roProps); }
    }
    else if (k == "props_normal") putPtr(W.ball, 264, Bk.props);
    (void)b;
}
EXPORT void fake_pts_state(double* out) {
    out[0] = Ps.score; out[1] = Ps.baskets; out[2] = getPropsValue(Bk.props); out[3] = Ps.props2 ? getPropsValue(Ps.props2) : -1;
    out[4] = Ps.roProps ? getPropsValue(Ps.roProps) : -1; out[5] = Ps.snapshot;
}
// "somebody else's" ball: a second Basketball object with its own settings object (point value 7), NOT linked from our ball control
EXPORT void fake_pts_make_other() {
    Ps.ball2 = makeObject(&kBall, 408); Ps.props2 = makeObject(&kProps, 176);
    putPtr(Ps.ball2, 72, W.rb); putPtr(Ps.ball2, 264, Ps.props2); putPtr(Ps.props2, 88, W.rb); setPropsValue(Ps.props2, 7);
    const int hl = 1; std::memcpy(Ps.ball2 + 164, &hl, 4);
}
// our ball control points at the other ball from now on (we "picked up another ball") / back at the first one
EXPORT void fake_pts_switch_ball(int toSecond) { putPtr(W.bcm, 168, toSecond ? Ps.ball2 : W.ball); }
EXPORT void* fake_pts_object(int which) { return which == 0 ? Bk.props : (which == 1 ? Ps.ball2 : (which == 2 ? Ps.props2 : Ps.roProps)); }

// ---- stage D11: knobs and readings of the pretend hands
EXPORT void fake_hb_set(const char* key, double a, double b) {
    const std::string k = key;
    (void)b;
    if (k == "set_throws") Hx.setThrows = a != 0;
    else if (k == "get_throws") Hx.getThrows = a != 0;
    else if (k == "viz_throws") Hx.vizThrows = a != 0;
    else if (k == "icalls") Hx.icalls = a != 0;
    else if (k == "viz_bool") { Hx.vizBool = true; kBallControl.methods[0]->ptypes = {&tGameObject, &tBool}; }
    else if (k == "viz_weird") { kBallControl.methods[0]->ptypes = {&tInt, &tInt}; }
    else if (k == "game_reset") { for (int i = 0; i < 10; ++i) { HbCol& c = Hx.cols[Hx.order[static_cast<size_t>(i)]]; for (int j = 0; j < 3; ++j) c.v[j] = c.orig[j]; } }          // the game puts all your hitboxes back to their own size
    else if (k == "game_new_size") { for (int i = 0; i < 10; ++i) { HbCol& c = Hx.cols[Hx.order[static_cast<size_t>(i)]]; for (int j = 0; j < 3; ++j) { c.orig[j] *= a; c.v[j] = c.orig[j]; } } }    // ... to a NEW own size
    else if (k == "destroy_col") { const uint64_t z = 0; std::memcpy(Hx.order[static_cast<size_t>(a)] + 16, &z, 8); }
    else if (k == "destroy_hand") { const uint64_t z = 0; std::memcpy(Hx.hand[static_cast<int>(a)] + 16, &z, 8); }
    else if (k == "destroy_bc") { const uint64_t z = 0; std::memcpy(Hx.bc[static_cast<int>(a)] + 16, &z, 8); }
    else if (k == "arr_null") putPtr(Hx.hand[static_cast<int>(a)], 144, nullptr);
    else if (k == "arr_back") putPtr(Hx.hand[static_cast<int>(a)], 144, Hx.arr[static_cast<int>(a)]);
    else if (k == "unlink") putPtr(W.bcm, a == 0 ? 40 : 48, nullptr);
    else if (k == "relink") putPtr(W.bcm, a == 0 ? 40 : 48, Hx.bc[static_cast<int>(a)]);
    else if (k == "authority") { Hx.hand[0][512] = Hx.hand[1][512] = a != 0; }
    else if (k == "prefab_null") { putPtr(Hx.bc[0], 160, nullptr); putPtr(Hx.bc[1], 160, nullptr); }
    else if (k == "rebuild") {          // the game makes new hands (new avatar): new objects with their own default sizes; the old ones are destroyed
        for (int h = 0; h < 2; ++h) { const uint64_t z = 0; std::memcpy(Hx.bc[h] + 16, &z, 8); std::memcpy(Hx.hand[h] + 16, &z, 8); for (int i = 0; i < 5; ++i) std::memcpy(Hx.order[static_cast<size_t>(5 * h + i)] + 16, &z, 8); }
        std::vector<unsigned char*> keep(Hx.order.begin() + 10, Hx.order.end());
        unsigned char* oldBc2 = Hx.bc[2]; unsigned char* oldHand2 = Hx.hand[2]; unsigned char* oldArr2 = Hx.arr[2]; unsigned char* oldPref2 = Hx.prefab[2];
        std::vector<unsigned char*> oldOrder = Hx.order;
        Hx.order.clear(); buildHand(0); buildHand(1);
        std::vector<unsigned char*> fresh = Hx.order;
        Hx.order = fresh; for (unsigned char* o : keep) Hx.order.push_back(o);
        Hx.bc[2] = oldBc2; Hx.hand[2] = oldHand2; Hx.arr[2] = oldArr2; Hx.prefab[2] = oldPref2;
        putPtr(W.bcm, 40, Hx.bc[0]); putPtr(W.bcm, 48, Hx.bc[1]);
    }
}
EXPORT void fake_hb_state(double* out) {
    out[0] = static_cast<double>(Hx.sets); out[1] = static_cast<double>(Hx.gets); out[2] = static_cast<double>(Hx.vizCalls);
    out[3] = Hx.viz[0]; out[4] = Hx.viz[1]; out[5] = Hx.viz[2]; out[6] = Hx.vizOn[0]; out[7] = Hx.vizOn[1]; out[8] = Hx.vizOff[0]; out[9] = Hx.vizOff[1];
}
// one hitbox: 0 kind, 1-3 size now, 4-6 the game's own size, 7 alive
EXPORT void fake_hb_col(int i, double* out) {
    for (int j = 0; j < 8; ++j) out[j] = 0;
    if (i < 0 || i >= static_cast<int>(Hx.order.size())) return;
    unsigned char* o = Hx.order[static_cast<size_t>(i)];
    const HbCol& c = Hx.cols[o];
    out[0] = c.kind; for (int j = 0; j < 3; ++j) { out[1 + j] = c.v[j]; out[4 + j] = c.orig[j]; }
    uint64_t ca; std::memcpy(&ca, o + 16, 8); out[7] = ca ? 1 : 0;
}
// how far the hitboxes of one hand reach: the biggest of (box: half the longest side, sphere: radius, capsule: radius or half the height)
EXPORT double fake_hb_reach(int hand) {
    double best = 0;
    for (int i = 0; i < 5; ++i) {
        const HbCol& c = Hx.cols[Hx.order[static_cast<size_t>(5 * hand + i)]];
        double r = 0;
        if (c.kind == 1) r = 0.5 * std::max(c.v[0], std::max(c.v[1], c.v[2])); else if (c.kind == 2) r = c.v[0]; else if (c.kind == 3) r = std::max(c.v[0], 0.5 * c.v[1]);
        best = std::max(best, r);
    }
    return best;
}
EXPORT void* fake_hb_object(int which, int hand) { return which == 0 ? Hx.bc[hand] : (which == 1 ? Hx.hand[hand] : (which == 2 ? Hx.arr[hand] : Hx.order[static_cast<size_t>(hand)])); }
// hide ONE engine / game method of the hitbox classes (a game that was updated or stripped)
EXPORT void fake_hb_hide(const char* klass, const char* method) {
    Klass* all[] = {&kBoxC, &kSphere, &kCapsuleC, &kBallControl};
    for (Klass* k : all) { if (k->name != klass) continue; for (size_t i = 0; i < k->methods.size();) { if (k->methods[i]->name == method) k->methods.erase(k->methods.begin() + static_cast<long>(i)); else ++i; } }
}
// change a class of the pretend game: action 0 = remove the field, 1 = give the field the wrong type (an int), 2 = remove the whole class from the game
EXPORT void fake_hb_field(const char* klass, const char* field, int action) {
    Klass* all[] = {&kBcm, &kBallControl, &kHand, &kBoxC, &kSphere, &kCapsuleC};
    for (Klass* k : all) {
        if (k->name != klass) continue;
        if (action == 2) { for (Image* im : {&iGame, &iPhys, &iCore}) for (size_t i = 0; i < im->classes.size();) { if (im->classes[i] == k) im->classes.erase(im->classes.begin() + static_cast<long>(i)); else ++i; } return; }
        for (size_t i = 0; i < k->fields.size(); ++i) if (k->fields[i].name == field) {
            if (action == 0) k->fields.erase(k->fields.begin() + static_cast<long>(i)); else k->fields[i].type = &tInt;
            return;
        }
    }
}

// ---- the runtime functions the link uses
EXPORT void* il2cpp_domain_get() { init(); return &gDomain; }
EXPORT void** il2cpp_domain_get_assemblies(void*, size_t* n) { *n = 4; return gAssemblies; }
EXPORT void* il2cpp_assembly_get_image(void* a) { return a; }
EXPORT const char* il2cpp_image_get_name(void* i) { return static_cast<Image*>(i)->name.c_str(); }
EXPORT size_t il2cpp_image_get_class_count(void* i) { return static_cast<Image*>(i)->classes.size(); }
EXPORT void* il2cpp_image_get_class(void* i, size_t k) { return static_cast<Image*>(i)->classes[k]; }
EXPORT const char* il2cpp_class_get_name(void* k) { return static_cast<Klass*>(k)->name.c_str(); }
EXPORT const char* il2cpp_class_get_namespace(void* k) { return static_cast<Klass*>(k)->ns.c_str(); }
EXPORT void* il2cpp_class_get_parent(void* k) { return static_cast<Klass*>(k)->parent; }
EXPORT void* il2cpp_class_get_fields(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->fields.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return &c->fields[i];
}
EXPORT const char* il2cpp_field_get_name(void* f) { return static_cast<Field*>(f)->name.c_str(); }
EXPORT void* il2cpp_field_get_type(void* f) { return static_cast<Field*>(f)->type; }
EXPORT size_t il2cpp_field_get_offset(void* f) { return static_cast<Field*>(f)->offset; }
EXPORT int il2cpp_field_get_flags(void* f) { return static_cast<Field*>(f)->flags; }
EXPORT void* il2cpp_class_get_methods(void* k, void** it) {
    Klass* c = static_cast<Klass*>(k); size_t i = reinterpret_cast<size_t>(*it);
    if (i >= c->methods.size()) return nullptr;
    *it = reinterpret_cast<void*>(i + 1); return c->methods[i];
}
EXPORT const char* il2cpp_method_get_name(void* m) { return static_cast<Method*>(m)->name.c_str(); }
EXPORT unsigned il2cpp_method_get_param_count(void* m) { return static_cast<Method*>(m)->params; }
EXPORT void* il2cpp_method_get_return_type(void* m) { return static_cast<Method*>(m)->ret; }
EXPORT char* il2cpp_type_get_name(void* t) { return strdup(static_cast<Type*>(t)->name.c_str()); }
EXPORT void il2cpp_free(void* p) { free(p); }
EXPORT void* il2cpp_thread_attach(void*) { return &gDomain; }
EXPORT void il2cpp_thread_detach(void*) {}
EXPORT int il2cpp_class_instance_size(void* k) { return static_cast<Klass*>(k)->instanceSize; }
EXPORT void* il2cpp_class_from_type(void* t) { Type* ty = static_cast<Type*>(t); return ty->klass ? static_cast<void*>(ty->klass) : static_cast<void*>(&kOther); }
EXPORT bool il2cpp_class_is_valuetype(void* k) { return static_cast<Klass*>(k)->isValue; }
EXPORT bool il2cpp_class_is_enum(void* k) { return static_cast<Klass*>(k)->isEnum; }
EXPORT int il2cpp_class_value_size(void* k, unsigned* align) { if (align) *align = 4; return static_cast<Klass*>(k)->valueSize; }
EXPORT void* il2cpp_thread_current() { return &gDomain; }
// stage D11: the engine's own internal calls for the hitbox sizes (only when the test switches them on; otherwise "not found", like before)
static HbCol* hbOf(void* self, int kind) { auto it = Hx.cols.find(static_cast<unsigned char*>(self)); if (it == Hx.cols.end() || it->second.kind != kind) return nullptr; uint64_t ca; std::memcpy(&ca, static_cast<unsigned char*>(self) + 16, 8); return ca ? &it->second : nullptr; }
static float icSphGet(void* self) { ++Hx.gets; HbCol* c = hbOf(self, 2); return c ? static_cast<float>(c->v[0]) : 0.0f; }
static void icSphSet(void* self, float v) { ++Hx.sets; if (HbCol* c = hbOf(self, 2)) c->v[0] = v; }
static float icCapRGet(void* self) { ++Hx.gets; HbCol* c = hbOf(self, 3); return c ? static_cast<float>(c->v[0]) : 0.0f; }
static void icCapRSet(void* self, float v) { ++Hx.sets; if (HbCol* c = hbOf(self, 3)) c->v[0] = v; }
static float icCapHGet(void* self) { ++Hx.gets; HbCol* c = hbOf(self, 3); return c ? static_cast<float>(c->v[1]) : 0.0f; }
static void icCapHSet(void* self, float v) { ++Hx.sets; if (HbCol* c = hbOf(self, 3)) c->v[1] = v; }
static void icBoxGet(void* self, float* out) { ++Hx.gets; if (HbCol* c = hbOf(self, 1)) for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(c->v[i]); }
static void icBoxSet(void* self, const float* in) { ++Hx.sets; if (HbCol* c = hbOf(self, 1)) for (int i = 0; i < 3; ++i) c->v[i] = in[i]; }
EXPORT void* il2cpp_resolve_icall(const char* name) {
    if (!Hx.icalls || !name) return nullptr;
    const std::string n = name;
    if (n == "UnityEngine.SphereCollider::get_radius") return reinterpret_cast<void*>(&icSphGet);
    if (n == "UnityEngine.SphereCollider::set_radius") return reinterpret_cast<void*>(&icSphSet);
    if (n == "UnityEngine.CapsuleCollider::get_radius") return reinterpret_cast<void*>(&icCapRGet);
    if (n == "UnityEngine.CapsuleCollider::set_radius") return reinterpret_cast<void*>(&icCapRSet);
    if (n == "UnityEngine.CapsuleCollider::get_height") return reinterpret_cast<void*>(&icCapHGet);
    if (n == "UnityEngine.CapsuleCollider::set_height") return reinterpret_cast<void*>(&icCapHSet);
    if (n == "UnityEngine.BoxCollider::get_size_Injected(UnityEngine.Vector3&)") return reinterpret_cast<void*>(&icBoxGet);
    if (n == "UnityEngine.BoxCollider::set_size_Injected(UnityEngine.Vector3&)") return reinterpret_cast<void*>(&icBoxSet);
    return nullptr;
}

EXPORT void* il2cpp_class_get_method_from_name(void* k, const char* name, int argc) {     // like the real runtime: this class first, then its parents
    for (Klass* c = static_cast<Klass*>(k); c; c = c->parent)
        for (Method* m : c->methods) if (m->name == name && static_cast<int>(m->params) == argc) return m;
    return nullptr;
}
EXPORT void* il2cpp_object_get_class(void* o) { void* k = nullptr; std::memcpy(&k, o, 8); return k; }
EXPORT void* il2cpp_class_get_type(void* k) {
    static bool filled = false;
    if (!filled) { filled = true; for (Type* t : {&tTransform, &tSphere, &tPhysMat, &tProps, &tGoal, &tGman, &tColliderObj, &tCombineE, &tCcdE, &tRB, &tBall, &tBcm}) if (t->klass) gKlassTypes[t->klass] = t; }
    auto it = gKlassTypes.find(static_cast<Klass*>(k));
    return it == gKlassTypes.end() ? nullptr : static_cast<void*>(it->second);
}
EXPORT void* il2cpp_type_get_object(void* t) {
    auto it = gTypeObjects.find(static_cast<Type*>(t));
    if (it != gTypeObjects.end()) return it->second;
    unsigned char* o = makeObject(&kOther, 48);
    gTypeObjects[static_cast<Type*>(t)] = o;
    return o;
}
EXPORT void* il2cpp_method_get_param(void* m, unsigned i) { Method* mm = static_cast<Method*>(m); return i < mm->ptypes.size() ? static_cast<void*>(mm->ptypes[i]) : nullptr; }

EXPORT void* il2cpp_runtime_invoke(void* method, void* obj, void** params, void** exc) {
    ++W.invokes;
    Method* m = static_cast<Method*>(method);
    if (W.throwAll) { if (exc) *exc = &gDomain; return nullptr; }
    auto bad = [&]() -> void* { if (exc) *exc = &gDomain; return nullptr; };
    auto boxVec = [&](double x, double y, double z) -> void* { unsigned char* b = box(); setV(b, 16, x, y, z); return b; };
    auto mkArr = [&](const std::vector<unsigned char*>& v) -> unsigned char* {          // a managed array of objects: length at +24, the objects from +32
        unsigned char* a = mkRaw(40 + 8 * v.size());
        const uint64_t n = v.size(); std::memcpy(a + 24, &n, 8);
        for (size_t i = 0; i < v.size(); ++i) putPtr(a, 32 + 8 * static_cast<int>(i), v[i]);
        return a;
    };
    auto nodeOf = [&](void* o) -> Node* { auto it = Bk.nodes.find(static_cast<unsigned char*>(o)); return it == Bk.nodes.end() ? nullptr : &it->second; };
    auto transformOf = [&](void* o) -> unsigned char* {          // Component.transform: a Transform is its own transform, a collider's is the object it sits on, a goal's is its root
        unsigned char* p = static_cast<unsigned char*>(o);
        if (nodeOf(p)) return p;
        auto c = Bk.cols.find(p); if (c != Bk.cols.end()) return c->second.node;
        for (int g = 0; g < 2; ++g) if (p == Bk.goal[g]) return Bk.gnode[g] ? Bk.gnode[g] : Bk.root[g];
        return nullptr;
    };
    switch (m->id) {
    case M_RB_GETVEL: if (obj != W.rb || W.rbDestroyed) return bad(); return boxVec(W.v[0], W.v[1], W.v[2]);
    case M_RB_GETPOS: if (obj != W.rb || W.rbDestroyed) return bad(); return boxVec(W.p[0], W.p[1], W.p[2]);
    case M_RB_SETVEL: {
        if (obj != W.rb || W.rbDestroyed || !params) return bad();
        const float* f = static_cast<const float*>(params[0]);
        W.v[0] = f[0]; W.v[1] = f[1]; W.v[2] = f[2]; ++W.setVelCalls;
        return nullptr;
    }
    case M_RB_GETDRAG: { if (obj != W.rb) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(W.drag)); return b; }
    case M_RB_GETUSEGRAV: { if (obj != W.rb) return bad(); unsigned char* b = box(); b[16] = W.useGravity ? 1 : 0; return b; }
    case M_PH_GETGRAV: return boxVec(0, -W.gravity, 0);
    case M_TM_FIXEDDT: { unsigned char* b = box(); setF(b, 16, static_cast<float>(W.fdt)); return b; }
    case M_GM_INSTANCE: return W.getInstanceNull ? nullptr : W.gm;
    case M_GM_OWNED: if (obj != W.gm) return bad(); return W.getOwnedNull ? nullptr : W.bcm;
    // ---- stage D9
    case M_RB_ANGVEL: if (obj != W.rb) return bad(); return boxVec(Bk.w[0], Bk.w[1], Bk.w[2]);
    case M_RB_ANGDRAG: { if (obj != W.rb) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(Bk.angDrag)); return b; }
    case M_RB_MASS: { if (obj != W.rb) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(Bk.mass)); return b; }
    case M_RB_INERTIA: { if (obj != W.rb) return bad(); const double r = ballRadius(), I = Bk.kappa * Bk.mass * r * r; return boxVec(I, I, I); }
    case M_RB_CCD: { if (obj != W.rb) return bad(); unsigned char* b = box(); const int v = Bk.ccd; std::memcpy(b + 16, &v, 4); return b; }
    case M_PH_BOUNCETHR: { unsigned char* b = box(); setF(b, 16, static_cast<float>(Bk.bounceThr)); return b; }
    case M_TR_POS: {
        Node* n = nodeOf(obj); if (!n) return bad();
        double dy = 0; for (int g = 0; g < 2; ++g) if (obj == Bk.board[g]) dy = Bk.boardDataDy;
        return boxVec(n->pos[0], n->pos[1] + dy, n->pos[2]);
    }
    case M_TR_SCALE: { Node* n = nodeOf(obj); if (!n) return bad(); return boxVec(n->scale[0], n->scale[1], n->scale[2]); }
    case M_TR_CHILDCOUNT: { Node* n = nodeOf(obj); if (!n) return bad(); unsigned char* b = box(); const int v = static_cast<int>(n->kids.size()); std::memcpy(b + 16, &v, 4); return b; }
    case M_TR_GETCHILD: {
        Node* n = nodeOf(obj); if (!n || !params) return bad();
        const int idx = *static_cast<const int*>(params[0]);
        if (idx < 0 || idx >= static_cast<int>(n->kids.size())) return bad();
        return n->kids[static_cast<size_t>(idx)];
    }
    case M_CO_TRANSFORM: { unsigned char* t = transformOf(obj); if (!t) return bad(); return t; }
    case M_CO_GETCOMP_STR: return bad();                       // the (string) overload: the wrong one for a Type argument
    case M_CO_GETCOMP: case M_CO_GETKIDS: case M_CO_GETPARENT: {
        unsigned char* t = transformOf(obj); if (!t || !params) return bad();
        auto ty = gTypeObjects.find(&tColliderObj);
        if (ty == gTypeObjects.end() || params[0] != ty->second) return nullptr;      // some other kind of component: none
        if (Bk.getCompBroken) return nullptr;
        if (m->id == M_CO_GETCOMP) return nodeOf(t)->col;
        if (m->id == M_CO_GETPARENT) { for (unsigned char* x = t; x; x = nodeOf(x)->parent) if (nodeOf(x)->col) return nodeOf(x)->col; return nullptr; }
        std::vector<unsigned char*> stack{t};              // children: this object first, then its children one after the other
        while (!stack.empty()) { unsigned char* x = stack.back(); stack.pop_back(); Node* nx = nodeOf(x); if (nx->col) return nx->col; for (size_t i = nx->kids.size(); i > 0; --i) stack.push_back(nx->kids[i - 1]); }
        return nullptr;
    }
    case M_COL_BOUNDS: {
        auto it = Bk.cols.find(static_cast<unsigned char*>(obj)); if (it == Bk.cols.end()) return bad();
        unsigned char* b = box(); const Col& c = it->second;
        if (c.sphere) { setV(b, 16, 0, 0, 0); const double rr = ballRadius(); setV(b, 28, rr, rr, rr); }
        else { setV(b, 16, c.c[0], c.c[1], c.c[2]); setV(b, 28, c.h[0], c.h[1], c.h[2]); }
        return b;
    }
    case M_COL_MATERIAL: { auto it = Bk.cols.find(static_cast<unsigned char*>(obj)); if (it == Bk.cols.end()) return bad(); return it->second.mat; }
    case M_SPH_RADIUS: { auto hh = Hx.cols.find(static_cast<unsigned char*>(obj)); if (hh != Hx.cols.end()) { ++Hx.gets; if (Hx.getThrows || hh->second.kind != 2) return bad(); uint64_t ca; std::memcpy(&ca, static_cast<unsigned char*>(obj) + 16, 8); if (!ca) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(hh->second.v[0])); return b; }
        auto it = Bk.cols.find(static_cast<unsigned char*>(obj)); if (it == Bk.cols.end() || !it->second.sphere) return bad(); unsigned char* b = box(); setF(b, 16, static_cast<float>(it->second.radius)); return b; }
    case M_MAT_BOUNCE: case M_MAT_DYN: case M_MAT_STAT: case M_MAT_BCOMB: case M_MAT_FCOMB: {
        auto it = Bk.mats.find(static_cast<unsigned char*>(obj)); if (it == Bk.mats.end()) return bad();
        unsigned char* b = box(); const Mat& mm = it->second;
        if (m->id == M_MAT_BOUNCE) setF(b, 16, static_cast<float>(mm.bounce));
        else if (m->id == M_MAT_DYN) setF(b, 16, static_cast<float>(mm.dyn));
        else if (m->id == M_MAT_STAT) setF(b, 16, static_cast<float>(mm.stat));
        else { const int v = m->id == M_MAT_BCOMB ? mm.bcomb : mm.fcomb; std::memcpy(b + 16, &v, 4); }
        return b;
    }
    case M_GOAL_RIM: { int g = -1; for (int i = 0; i < 2; ++i) if (obj == Bk.goal[i]) g = i; if (g < 0) return bad(); return boxVec(Bk.ring[g][0], Bk.ring[g][1] + Bk.ringDataDy, Bk.ring[g][2]); }
    case M_GOAL_BOARD: { int g = -1; for (int i = 0; i < 2; ++i) if (obj == Bk.goal[i]) g = i; if (g < 0) return bad(); const Node& n = Bk.nodes[Bk.board[g]]; return boxVec(n.pos[0], n.pos[1] + Bk.boardDataDy, n.pos[2]); }
    case M_GMAN_INSTANCE2: return Bk.hideManager ? nullptr : Bk.gman;
    // ---- stage D9b
    case M_TR_PARENT: { Node* n = nodeOf(obj); if (!n) return bad(); return n->parent; }
    case M_CO_COMPS1: case M_CO_COMPS2: {
        unsigned char* t = transformOf(obj); if (!t || !params) return bad();
        auto ty = gTypeObjects.find(&tColliderObj);
        std::vector<unsigned char*> found;
        if (ty != gTypeObjects.end() && params[0] == ty->second) {
            std::vector<unsigned char*> stack{t};
            while (!stack.empty()) { unsigned char* x = stack.back(); stack.pop_back(); Node* nx = nodeOf(x); if (nx->col) found.push_back(nx->col); for (size_t i = nx->kids.size(); i > 0; --i) stack.push_back(nx->kids[i - 1]); }
        }
        return mkArr(found);
    }
    case M_PH_OVSPHERE2: case M_PH_OVSPHERE3: case M_PH_OVSPHERE4: case M_PH_OVBOX2: case M_PH_OVBOX5: {
        if (!params) return bad();
        const float* c = static_cast<const float*>(params[0]);
        const bool sphere = m->id == M_PH_OVSPHERE2 || m->id == M_PH_OVSPHERE3 || m->id == M_PH_OVSPHERE4;
        const float* p1 = static_cast<const float*>(params[1]);
        const double hx = sphere ? p1[0] : p1[0], hy = sphere ? p1[0] : p1[1], hz = sphere ? p1[0] : p1[2];
        const bool ignoreTriggers = m->id == M_PH_OVSPHERE4 ? *static_cast<const int*>(params[3]) == 1 : (m->id == M_PH_OVBOX5 ? *static_cast<const int*>(params[4]) == 1 : false);
        std::vector<unsigned char*> found;
        for (auto& kv : Bk.cols) {
            const Col& k = kv.second; if (k.trigger && ignoreTriggers) continue;
            double cc[3], hh[3];
            if (k.sphere) { const double rr = ballRadius(); for (int i = 0; i < 3; ++i) { cc[i] = 0; hh[i] = rr; } } else for (int i = 0; i < 3; ++i) { cc[i] = k.c[i]; hh[i] = k.h[i]; }
            const double q[3] = {c[0], c[1], c[2]}, h[3] = {hx, hy, hz};
            bool hit = true;
            if (sphere) { double d2 = 0; for (int i = 0; i < 3; ++i) { const double d = std::max(0.0, std::fabs(q[i] - cc[i]) - hh[i]); d2 += d * d; } hit = d2 <= h[0] * h[0]; }
            else for (int i = 0; i < 3; ++i) if (std::fabs(q[i] - cc[i]) > hh[i] + h[i]) hit = false;
            if (hit) found.push_back(kv.first);
        }
        return mkArr(found);
    }
    case M_OBJ_FIND1: case M_OBJ_FIND2: {
        if (!params) return bad();
        auto ty = gTypeObjects.find(&tColliderObj);
        std::vector<unsigned char*> found;
        if (ty != gTypeObjects.end() && params[0] == ty->second) for (auto& kv : Bk.cols) found.push_back(kv.first);
        return mkArr(found);
    }
    case M_OBJ_NAME: {
        unsigned char* p = static_cast<unsigned char*>(obj);
        std::string nm = "?";
        auto it = Bk.names.find(p);
        if (it != Bk.names.end()) nm = it->second;
        else { auto c = Bk.cols.find(p); if (c != Bk.cols.end()) { auto it2 = Bk.names.find(c->second.node); if (it2 != Bk.names.end()) nm = it2->second; } else for (int g = 0; g < 2; ++g) if (p == Bk.goal[g]) nm = "BasketballGoal"; }
        unsigned char* str = mkRaw(24 + 2 * nm.size() + 8);
        const int len = static_cast<int>(nm.size()); std::memcpy(str + 16, &len, 4);
        for (size_t i = 0; i < nm.size(); ++i) { str[20 + 2 * i] = static_cast<unsigned char>(nm[i]); str[21 + 2 * i] = 0; }
        return str;
    }
    // ---- stage D11: hitbox sizes and the game's own "show hand colliders" function
    case M_BOX_GET: case M_BOX_SET: case M_SPH_SET: case M_CAP_RGET: case M_CAP_RSET: case M_CAP_HGET: case M_CAP_HSET: {
        unsigned char* o = static_cast<unsigned char*>(obj);
        auto it = Hx.cols.find(o); if (it == Hx.cols.end()) return bad();
        uint64_t ca; std::memcpy(&ca, o + 16, 8); if (!ca) return bad();               // a destroyed engine object: the engine throws
        HbCol& c = it->second;
        const bool isGet = m->id == M_BOX_GET || m->id == M_CAP_RGET || m->id == M_CAP_HGET;
        const int want = (m->id == M_BOX_GET || m->id == M_BOX_SET) ? 1 : ((m->id == M_SPH_SET) ? 2 : 3);
        if (c.kind != want) return bad();
        if (isGet) {
            ++Hx.gets; if (Hx.getThrows) return bad();
            if (m->id == M_BOX_GET) return boxVec(c.v[0], c.v[1], c.v[2]);
            unsigned char* b = box(); setF(b, 16, static_cast<float>(m->id == M_CAP_HGET ? c.v[1] : c.v[0])); return b;
        }
        ++Hx.sets; if (Hx.setThrows || !params) return bad();
        if (m->id == M_BOX_SET) { const float* f = static_cast<const float*>(params[0]); for (int i = 0; i < 3; ++i) c.v[i] = f[i]; }
        else { const float f = *static_cast<const float*>(params[0]); if (m->id == M_CAP_HSET) c.v[1] = f; else c.v[0] = f; }
        return nullptr;
    }
    case M_BC_VIZ: {
        int idx = -1; for (int h = 0; h < 3; ++h) if (obj == Hx.bc[h]) idx = h;
        if (idx < 0 || Hx.vizThrows || !params) return bad();
        if (params[0] == nullptr) return bad();                                          // no model: the game would fail inside
        const bool on = Hx.vizBool ? (*static_cast<const unsigned char*>(params[1]) != 0) : (*static_cast<const float*>(params[1]) > 0.5f);
        ++Hx.vizCalls; Hx.viz[idx] = on ? 1 : 0; if (on) ++Hx.vizOn[idx]; else ++Hx.vizOff[idx];
        return nullptr;
    }
    case M_COL_TRIGGER: case M_COL_ENABLED: {
        auto it = Bk.cols.find(static_cast<unsigned char*>(obj)); if (it == Bk.cols.end()) return bad();
        unsigned char* b = box(); b[16] = (m->id == M_COL_TRIGGER ? it->second.trigger : it->second.enabled) ? 1 : 0; return b;
    }
    }
    return bad();
}
}
