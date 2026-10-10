#pragma once
// tank.h — PlayerTank (the player's tank) and AlliedTank (AI wingmen).
// Both are Collidable so the one-pass ResolveCollisions() covers them.

#include "common.h"
#include "pathfind.h"

class PlayerTank : public Collidable {
public:
    Vector3 pos   = { -ARENA_HALF + 30.0f, 0.0f, ARENA_HALF - 30.0f };
    float hullAngle   = 0.0f;  // radians, 0 = facing -Z
    float turretAngle = 0.0f;  // radians, relative to hull (rendering + stops)
    float aimAngle    = 0.0f;  // radians, world-space gun direction (stabilized sight)
    float aimPitch    = 0.0f;  // radians, gun elevation (+ = up), mouse Y
    float speed       = 0.0f;
    int hp = 3;
    int maxHp = 3;
    float hitFlashT = 0.0f;    // red hit feedback timer

    void update(float dt, const InputState &in, const GameCtx &g, bool allowDrive);
    void draw(bool gunnerView) const;

    // Collidable: single disk matching TANK_RADIUS.
    Vector3 cPos() const override { return pos; }
    void cSetPos(const Vector3 &p) override { pos = p; }
    float cYaw() const override { return hullAngle; }
    float cMass() const override { return 1.0f; }
    bool cAlive() const override { return hp > 0; }
    std::vector<CollisionDisk> cDisks() const override {
        return { CollisionDisk{ Vector3{ 0.0f, 0.0f, 0.0f }, TANK_RADIUS } };
    }
};

class AlliedTank : public Collidable {
public:
    Vector3 pos = { 0.0f, 0.0f, 0.0f };
    float hullAngle   = 0.0f;
    float turretAngle = 0.0f;
    int hp = 2;
    int maxHp = 2;
    bool alive = true;
    float hitFlashT = 0.0f;
    // Orders.
    AllyOrder order = AllyOrder::FOLLOW;
    Vector3 orderPos = { 0, 0, 0 };  // MOVE destination
    int targetEnemy = -1;           // ATTACK: index into enemies
    // Combat state.
    float aimTimer = 0.0f;
    float fireTimer = 0.0f;
    int engageIdx = -1;             // enemy currently being shot at
    PathFollower navPath;           // route to order destination (if pathEnabled)
    // Death animation (same as Walker).
    float deathT = 0.0f;
    Vector3 turretPos = { 0.0f, 0.0f, 0.0f };
    Vector3 turretVel = { 0.0f, 0.0f, 0.0f };
    float turretSpin = 0.0f;
    float turretSpinVel = 0.0f;
    bool turretLanded = false;
    float burnAccum = 0.0f;

    void update(float dt, GameCtx &g, size_t index);
    void draw(const Config &cfg, bool isSelected,
              const std::vector<std::unique_ptr<Walker>> &walkers) const;

    // Collidable: single disk matching TANK_RADIUS.
    Vector3 cPos() const override { return pos; }
    void cSetPos(const Vector3 &p) override { pos = p; }
    float cYaw() const override { return hullAngle; }
    float cMass() const override { return 1.0f; }
    bool cAlive() const override { return alive; }
    std::vector<CollisionDisk> cDisks() const override {
        return { CollisionDisk{ Vector3{ 0.0f, 0.0f, 0.0f }, TANK_RADIUS } };
    }
private:
    void driveToward(float wantAngle, float speed, const GameCtx &g);
};

// Tank helpers used by the main loop and replay.
Vector3 TurretWorldPos(const PlayerTank &t);

Vector3 TurretForward(const PlayerTank &t);

Vector3 MuzzleWorldPos(const PlayerTank &t);

void FireShell(const PlayerTank &t, std::vector<Shell> &shells, const Config &cfg);

void DrawTurretGhost(const PlayerTank &t);

void DamageAlly(AlliedTank &a, std::vector<Particle> &particles, std::vector<Flash> &flashes);

std::vector<AlliedTank> SpawnAllies(int count, int hits, const Vector3 &playerPos);

void DrawAllies(const std::vector<AlliedTank> &allies,
                       const std::vector<std::unique_ptr<Walker>> &walkers,
                       const Config &cfg, int selected);
