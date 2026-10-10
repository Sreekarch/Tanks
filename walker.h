#pragma once
// walker.h — Walker base class for the alien gun-walkers and the
// Tripedal concrete walker. A future crab walker subclasses Walker the
// same way; the game loop only ever talks to Walker.

#include "common.h"
#include "pathfind.h"

// Tripedal proportions (taller WotW-style fighting machine).
static constexpr float WALKER_CHASSIS_Y = 4.05f;  // armored pod center height
static constexpr float WALKER_NECK_Y    = 5.5f;   // head-assembly pivot height
static constexpr float WALKER_GUN_DY    = -2.9f;  // gun pod below the neck pivot
static constexpr float WALKER_GUN_FWD   = -1.0f;  // gun pod forward of the pivot
static constexpr float WALKER_BARREL    = 2.5f;   // gun barrel length
static constexpr float WALKER_GUN_Y     = WALKER_NECK_Y + WALKER_GUN_DY;  // ≈2.6

class Walker : public Collidable {
public:
    Vector3 pos;
    float hullAngle   = 0.0f;
    float turretAngle = 0.0f;
    int hp = 2;
    int maxHp = 2;
    bool alive = true;
    float walkPhase = 0.0f;   // leg animation phase, advanced by movement
    Vector3 fallAxis = { 1.0f, 0.0f, 0.0f };

  // wreck collapse axis
    float staggerT = 0.0f;    // hit reaction: staggers back + stumbles
    Vector3 staggerDir = { 0.0f, 0.0f, 0.0f };  // knockback dir, away from the shot
    float swayT = 0.0f;       // head/turret tilt timer (rears up/away, then settles)
    float swayPitchAmp = 0.0f;  // skyward tilt amplitude, rad
    float swayRollAmp = 0.0f;   // lateral tilt-away amplitude, rad
    float swayYawAmp = 0.0f;    // flinch-away yaw amplitude, rad
    float gunPitch = 0.0f;    // gun pod elevation, radians (+ = up), set by AI
    float fogFactor = 0.0f;   // fog of war: 0 = revealed, 1 = fully fogged
    float hitFlashT = 0.0f;   // white hit feedback timer
    // AI state.
    AIState aiState = AIState::ADVANCE;
    float aiTimer = 0.0f;       // time in current state
    Vector3 coverPos = { 0, 0, 0 };
    float fireTimer = 0.0f;     // time since last shot
    float aimTimer = 0.0f;      // lock-on tracking time (telegraph)
    // Death animation state.
    float deathT = 0.0f;
    Vector3 turretPos;        // detached turret world position
    Vector3 turretVel;
    float turretSpin = 0.0f;
    float turretSpinVel = 0.0f;
    bool turretLanded = false;
    float burnAccum = 0.0f;   // spawner accumulator for wreck smoke/flame

    virtual ~Walker() = default;
    virtual WalkerKind kind() const = 0;
    virtual void update(float dt, GameCtx &g) = 0;
    virtual void draw(float alpha, const Config &cfg) const = 0;

    PathFollower navPath;  // route to the current AI destination (if pathEnabled)

    // Nonlethal hit: hit reaction + stagger; lethal hit: kill sequence.
    void damage(GameCtx &g, const Vector3 &hitDir);
    // Death animation (turret pop, burn) shared by all walkers.
    void updateDeath(std::vector<Particle> &particles, float dt);

    // Collidable: single disk; the tripedal's leg span reads ~6 wide.
    Vector3 cPos() const override { return pos; }
    void cSetPos(const Vector3 &p) override { pos = p; }
    float cYaw() const override { return hullAngle; }
    float cMass() const override { return 1.0f; }
    bool cAlive() const override { return alive; }
    std::vector<CollisionDisk> cDisks() const override {
        return { CollisionDisk{ Vector3{ 0.0f, 0.0f, 0.0f }, 3.0f } };
    }
};

class Tripedal : public Walker {
public:
    WalkerKind kind() const override { return WalkerKind::TRIPEDAL; }
    void update(float dt, GameCtx &g) override;
    void draw(float alpha, const Config &cfg) const override;
private:
    static void drawLocal(float walkPhase, float bobY, Color body, Color metal,
                          Color dark, Color trim, float alpha, float crumple);
    void drawHeadLocal(float bobY, float yaw, float pitchDeg, float rollDeg,
                       Color armor, Color trim, Color glow, float alpha) const;
    void drawWalker(const Config &cfg, float alpha) const;
    void drawWreck() const;
    void driveToward(float wantAngle, float speed, const GameCtx &g);
};

// Walker helpers used by the main loop.
void WalkerMuzzle(const Walker &e, float yaw, float pitch,
                         Vector3 *outPos, Vector3 *outDir);

std::vector<std::unique_ptr<Walker>> SpawnEnemies(int count, int hits,
                                       const std::vector<Building> &village,
                                       const std::vector<Ditch> &ditches,
                                       const std::vector<Bridge> &bridges,
                                       const std::vector<Tower> &towers);

bool ShellHitsEnemy(const Vector3 &p, float r, const Walker &e);

void UpdateWalkers(std::vector<std::unique_ptr<Walker>> &walkers,
                          std::vector<Particle> &particles, float dt);

void DrawEnemies(const std::vector<std::unique_ptr<Walker>> &walkers, const Config &cfg);

void ResolveWreckCollisions(Vector3 &pos, const std::vector<std::unique_ptr<Walker>> &walkers, bool wreckBlocks);
