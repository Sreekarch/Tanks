#pragma once
// common.h — shared types and cross-file declarations for tankshooter.
// Everything here is used by more than one translation unit: config,
// small POD types, the replay snapshots, GameCtx, and the wreck/kill
// templates. Entity classes live in tank.h / walker.h.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "collide.h"
#include "input.h"

class PlayerTank;
class AlliedTank;
class Walker;
struct Tower;
struct Building;
struct NavGrid;
struct Ditch;
struct Bridge;
struct Shell;
struct Flash;
struct Particle;

// Tuning constants.
static constexpr float ARENA_HALF   = 190.0f;  // playable square extends +/- this
static constexpr float TANK_RADIUS = 2.2f;    // collision circle around the tank
static constexpr float MAX_SPEED   = 14.0f;   // units / second
static constexpr float MAX_REVERSE = -6.0f;
static constexpr float ACCEL       = 18.0f;
static constexpr float TURN_RATE   = 1.9f;     // rad / second at full speed
static constexpr float TURRET_SENS = 0.0035f;  // rad per mouse pixel

inline float NormalizeAngle(float a) {
    while (a > PI)  a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

struct Config {
    float shellSpeed       = 70.0f;
    float shellCooldown    = 1.5f;
    float shellLifetime    = 4.0f;
    float shellRadius      = 0.18f;
    int   buildingHits     = 3;
    float collapseDuration = 1.6f;
    // Player
    int   playerHits       = 3;
    float playerSpeed      = 14.0f;
    // Enemy
    int   enemyCount       = 4;
    int   enemyHits        = 2;
    bool  wreckBlocks      = true;
    Color enemyColor       = { 165, 70, 50, 255 };
    float enemySpeed       = 10.0f;
    float enemyRange       = 70.0f;   // shoot when closer than this + LOS
    float enemyFireInt     = 3.5f;    // seconds between shots
    float enemySpread      = 0.06f;   // radians of random aim error
    float enemyAimTime     = 1.2f;    // tracking (telegraph) before firing
    float enemyCoverWait   = 4.0f;    // seconds hidden before re-emerging
    float staggerSeconds   = 0.55f;  // stumble duration: 1-2 quick backpedal steps
    float staggerStepSpeed = 13.0f;  // backpedal speed while stumbling
    float staggerDistance  = 0.8f;   // initial impact jolt (visual, front-loaded)
    float swaySeconds      = 0.8f;   // head tilt-away duration (rears up, settles)
    // Ally
    int   allyCount        = 2;
    int   allyHits         = 2;
    Color allyColor        = { 70, 120, 180, 255 };
    float allySpeed        = 10.0f;
    bool  allyEngage       = true;    // engage enemies on sight
    float allyRange        = 70.0f;
    float allyFireInt      = 3.5f;
    float allySpread       = 0.06f;
    float allyAimTime      = 1.2f;
    // Fog of war (hides enemies outside line of sight)
    bool  fogEnabled       = true;
    float fogSightRadius   = 90.0f;  // revealers see this far (with LOS)
    float fogEdgeWidth     = 14.0f;  // soft reveal band around the sight radius
    float fogPuffAlpha     = 0.32f;  // per-puff alpha of the visible fog banks
    float fogPuffSize      = 22.0f;  // world-unit diameter of one fog puff
    bool  replayEnabled    = true;   // defeat replays the last seconds
    float replayDuration   = 10.0f;  // seconds of replay footage
    float replayPostSeconds = 4.0f;  // aftermath included in the victory replay
    float replayPostDefeatSeconds = 4.0f;  // aftermath after the player's death
    // Ditches (tanks get stuck/slowed) and bridges
    float ditchSlowFactor  = 0.15f;  // speed multiplier inside a ditch
    float ditchDepth       = 3.0f;
    // Turret towers (static defenses, hostile to player side)
    int   towerHits        = 3;
    float towerRange       = 75.0f;
    float towerFireInt     = 4.0f;
    float towerAimTime     = 1.0f;
    // Map
    char  mapDefault[64]   = "Crossroads";
    // Minimap (gunner mode picture-in-picture)
    bool  minimapEnabled   = true;
    int   minimapSize      = 240;    // pixels (square)
    float minimapHeight    = 150.0f; // top-down camera altitude
    // Chase drone view (gunner mode picture-in-picture above the minimap)
    bool  chaseEnabled     = true;
    int   chaseWidth       = 320;
    int   chaseHeight      = 180;
    float chaseCamHeight   = 26.0f;  // camera altitude above the tank
    float chaseCamDist     = 30.0f;  // camera distance behind the tank
    // Gamepad stick sensitivities (keyboard/mouse unaffected)
    float padDriveSens     = 0.6f;   // left stick: scales throttle/steer (and drone move)
    float padAimSens       = 10.0f;  // right stick: mouse-pixel equivalents per frame
    // Pathfinding (walkers + allies); off = legacy point-to-point steering
    bool  pathEnabled      = true;
    float pathCellSize     = 2.0f;   // nav grid resolution (world units)
    float pathRepathSeconds = 0.75f; // max time between re-plans per entity
    bool  pathDrawPaths    = false;  // debug: draw active paths in-world
    // Alien perception & patrol (stealth); off = legacy omniscient rush
    bool  stealthEnabled   = true;
    bool  nightMode        = true;   // dark scene so searchlights read as light
    float visionRange      = 55.0f;  // searchlight cone length
    float visionHalfAngleDeg = 35.0f; // searchlight cone half-angle
    float detectSeconds    = 1.1f;   // in-cone time to full alert (closer = faster)
    float forgetSeconds    = 2.5f;   // detection meter drain time
    float alarmSeconds     = 0.9f;   // alarm-call animation before broadcast
    float alarmRadius      = 60.0f;  // who hears a raised alarm
    float noiseRadius      = 55.0f;  // who hears a player shot
    float memorySeconds    = 4.0f;   // LOS memory before giving up to SEARCH
    float searchSeconds    = 3.5f;   // look-around time (investigate/search)
    float patrolSpeedFactor = 0.55f; // patrol pace vs combat speed
    float patrolPauseSeconds = 1.6f; // stop-and-sweep at each patrol point
    bool nightActive() const { return stealthEnabled && nightMode; }
    // Display
    int   windowWidth  = 1280;
    int   windowHeight = 720;
    bool  fullscreen   = false;
    bool  msaa4x       = true;  // 4x anti-aliasing on the main view
    int   insetScale   = 2;     // render scale for minimap/chase insets (sharper downscale)
    bool  showFps      = true;  // FPS readout under the drone view
};

Config LoadConfig();

// Shared enums.
enum class WalkerKind { TRIPEDAL, CRAB, BIPED };

enum class AIState { ADVANCE, SHOOT, SEEK_COVER, COVER_WAIT, STAGGER,
                     PATROL, INVESTIGATE, ALARM, SEARCH };

enum class AllyOrder { FOLLOW, MOVE, HOLD, ATTACK };

enum class CamMode { GUNNER, DRONE };

enum class Phase { SETUP, COMBAT };

// Projectiles, effects, orders.
struct Shell {
    Vector3 pos;
    Vector3 vel;
    float life;
    bool fromEnemy = false;   // true: hostile shell, hits the player
    bool fromPlayer = false;  // true: fired by the player (not an ally)
    static constexpr int TRAIL = 24;
    Vector3 trail[TRAIL];
    int trailCount = 0;
};

struct Flash {
    Vector3 pos;
    float t;      // time remaining
    float maxT;   // total duration
    float size;
};

struct Particle {
    Vector3 pos;
    Vector3 vel;
    float life;
    float maxLife;
    float size;
    Color color;
    float grav;   // vertical accel; negative rises (smoke), positive falls
};

struct OrderPing {
    Vector3 pos;
    float t;
    float maxT;
    Color color;
};

// World geometry types (definitions live in tankshooter.cpp).
struct HitMark {
    Vector3 pos;     // point of impact on the building surface
    Vector3 normal;  // outward face normal (for orienting the scorch)
    float seed;      // randomizes the blast splotch pattern
};

struct Ditch {
    Vector3 center = { 0, 0, 0 };
    float hx = 10.0f, hz = 10.0f;  // half-extents
    float depth = 3.0f;
};

struct Bridge {
    Vector3 center = { 0, 0, 0 };
    float hx = 6.0f, hz = 14.0f;   // deck half-extents
};

struct Tower {
    Vector3 pos = { 0, 0, 0 };
    int hp = 3, maxHp = 3;
    float turretAngle = 0.0f;  // world-space
    float fireTimer = 0.0f, aimTimer = 0.0f;
    bool targetPlayer = false;
    int targetAlly = -1;
    bool alive = true;
    float hitFlashT = 0.0f;
};

struct Building {
    Vector3 center;   // center of the box
    Vector3 size;     // full extents
    Color   color;
    int   hp = 3;         // hits remaining before collapse
    int   maxHp = 3;
    bool  destroyed = false;
    float collapseT = 0.0f;   // 0..1 collapse animation progress
    Vector3 fallAxis = { 1.0f, 0.0f, 0.0f };  // random horizontal tip-over axis
    std::vector<HitMark> marks = {};  // persistent scorch marks from shell hits
};

// Replay snapshots (recorded in tankshooter.cpp, replayed there too).
struct ReplayTankSnap {
    Vector3 pos; float hullAngle, turretAngle; bool alive;
    float deathT; Vector3 turretPos; float turretSpin;  // ally wrecks
};
struct ReplayEnemySnap {
    Vector3 pos; float hullAngle, turretAngle; bool alive;
    float deathT; Vector3 turretPos; float turretSpin;
    WalkerKind kind; float walkPhase; Vector3 fallAxis;
    float staggerT; Vector3 staggerDir;
    float swayT; float gunPitch;
    float swayPitchAmp, swayRollAmp, swayYawAmp;
};
struct ReplayShellSnap { Vector3 pos; };
struct ReplayFlashSnap { Vector3 pos; float t, maxT, size; };
struct ReplayParticleSnap { Vector3 pos; Color color; float size; float life, maxLife; };
struct ReplayBldSnap { bool destroyed; float collapseT; int hp; Vector3 fallAxis; };
struct ReplayTowerSnap { bool alive; float turretAngle; };
struct ReplayFrame {
    float time = 0.0f;
    ReplayTankSnap player;
    std::vector<ReplayTankSnap> allies;
    std::vector<ReplayEnemySnap> enemies;
    std::vector<ReplayShellSnap> shells;
    std::vector<ReplayFlashSnap> flashes;
    std::vector<ReplayParticleSnap> particles;
    std::vector<ReplayBldSnap> buildings;
    std::vector<ReplayTowerSnap> towers;
    // Follow-drone camera (chase-view framing), recomputed per snapshot so
    // the player's tank and its destruction stay in frame.
    Vector3 camPos;
    Vector3 camTarget;
};

// Drone camera (methods in tankshooter.cpp).
class DroneCam {
public:
    Vector3 pos   = { -ARENA_HALF + 30.0f, 40.0f, ARENA_HALF + 20.0f };
    float yaw   = 0.0f;   // radians, 0 = looking -Z
    float pitch = -0.5f;  // radians, negative looks down

    void update(float dt, const InputState &in);
    // Place the drone just above and behind the tank, looking the way the
    // tank faces (toward the village). Used at startup, on TAB, and on R.
    void reset(const PlayerTank &tank);
};

// Soft contact shadow that grounds a unit on the terrain.
inline void DrawBlobShadow(Vector3 pos, float radius, unsigned char alpha) {
    rlPushMatrix();
    rlTranslatef(pos.x, 0.04f, pos.z);
    rlScalef(1.0f, 0.05f, 1.0f);
    DrawSphere(Vector3{ 0.0f, 0.0f, 0.0f }, radius, Color{ 0, 0, 0, alpha });
    rlPopMatrix();
}

// Bundle of world references passed to entity updates.
struct GameCtx {
    PlayerTank &player;
    std::vector<std::unique_ptr<Walker>> &walkers;
    std::vector<AlliedTank> &allies;
    std::vector<Tower> &towers;
    std::vector<Building> &village;
    const std::vector<Ditch> &ditches;
    const std::vector<Bridge> &bridges;
    std::vector<Shell> &shells;
    std::vector<Flash> &flashes;
    std::vector<Particle> &particles;
    const Config &cfg;
    const InputState &in;
    bool inCombat = false;
    float dt = 0.0f;
    NavGrid *nav = nullptr;  // pathfinding grid (pathfind.h); null = direct steering
    Vector3 noisePos = { 0, 0, 0 };  // last player shot position
    float noiseAge = 1e9f;           // seconds since that shot
};

// Cross-file helpers (defined in tankshooter.cpp).
bool LosBlocked(const Vector3 &a, const Vector3 &b,
                const std::vector<Building> &village);
float DitchDepthAt(float x, float z, const std::vector<Ditch> &ditches,
                   const std::vector<Bridge> &bridges);
bool PointInTower(float x, float z, const std::vector<Tower> &towers, float pad);
void ResolveBuildingCollisions(Vector3 &pos,
                               const std::vector<Building> &village);
void ResolveTowerCollisions(Vector3 &pos, const std::vector<Tower> &towers);
void Burst(std::vector<Particle> &ps, Vector3 c, int n,
           Color color, float speed, float up, float size, float life, float grav);

// Wreck animation + kill helpers shared by walkers and allied tanks.
template <typename T>
void UpdateWreckAnim(T &u, std::vector<Particle> &particles, float dt) {
    u.deathT += dt;
    if (!u.turretLanded) {
        u.turretVel.y -= 22.0f * dt;
        u.turretPos = Vector3Add(u.turretPos, Vector3Scale(u.turretVel, dt));
        u.turretSpin += u.turretSpinVel * dt;
        if (u.turretPos.y <= 0.55f) {
            u.turretPos.y = 0.55f;
            float dx = u.turretPos.x - u.pos.x, dz = u.turretPos.z - u.pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 20.25f) {
                float d = sqrtf(d2);
                if (d < 1e-3f) { dx = 1.0f; dz = 0.0f; d = 1.0f; }
                u.turretPos.x = u.pos.x + dx / d * 4.5f;
                u.turretPos.z = u.pos.z + dz / d * 4.5f;
            }
            u.turretLanded = true;
        }
    }
    u.burnAccum += dt;
    while (u.burnAccum >= 0.22f) {
        u.burnAccum -= 0.22f;
        Vector3 fp = { u.pos.x + (float)GetRandomValue(-80, 80) / 100.0f, 1.4f,
                       u.pos.z + (float)GetRandomValue(-80, 80) / 100.0f };
        particles.push_back(Particle{ fp,
            { (float)GetRandomValue(-10, 10) / 10.0f, (float)GetRandomValue(20, 45) / 10.0f,
              (float)GetRandomValue(-10, 10) / 10.0f },
            (float)GetRandomValue(5, 9) / 10.0f, (float)GetRandomValue(30, 55) / 100.0f,
            0.5f, Color{ 255, 140, 30, 255 }, 6.0f });
        Vector3 sp = { u.pos.x, 2.2f, u.pos.z };
        particles.push_back(Particle{ sp,
            { (float)GetRandomValue(-8, 8) / 10.0f, (float)GetRandomValue(25, 50) / 10.0f,
              (float)GetRandomValue(-8, 8) / 10.0f },
            (float)GetRandomValue(18, 30) / 10.0f, (float)GetRandomValue(80, 130) / 100.0f,
            0.8f, Color{ 70, 65, 60, 255 }, -3.0f });
    }
}

template <typename T>
void KillUnit(T &u, std::vector<Particle> &particles, std::vector<Flash> &flashes) {
    u.alive = false;
    u.deathT = 0.0f;
    Vector3 c = { u.pos.x, 1.6f, u.pos.z };
    flashes.push_back(Flash{ c, 0.55f, 0.55f, 3.8f });
    Burst(particles, c, 10, Color{ 255, 150, 40, 255 }, 9.0f, 7.0f, 0.9f, 0.7f, 6.0f);
    Burst(particles, c, 12, Color{ 90, 85, 80, 255 }, 4.0f, 9.0f, 1.4f, 2.6f, -3.0f);
    Burst(particles, c, 6, Color{ 255, 220, 120, 255 }, 14.0f, 5.0f, 0.5f, 0.4f, 10.0f);
    u.turretPos = { u.pos.x, 2.25f, u.pos.z };
    float popA = (float)GetRandomValue(0, 360) * DEG2RAD;
    float popS = (float)GetRandomValue(50, 80) / 10.0f;
    u.turretVel = { cosf(popA) * popS,
                    (float)GetRandomValue(75, 115) / 10.0f,
                    sinf(popA) * popS };
    u.turretSpinVel = (float)GetRandomValue(-9, 9);
    u.turretLanded = false;
    u.burnAccum = 0.0f;
}
