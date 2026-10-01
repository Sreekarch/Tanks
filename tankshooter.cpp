// tankshooter.cpp — v0.2 prototype
//
// Cover-based tank shooter: drivable player tank, broken-down village arena,
// gunner (first-person) and drone (free-fly) camera modes, shooting with
// destructible cover.
//
// Controls:
//   W/S drive, A/D turn hull, mouse aims turret (gunner) / looks (drone),
//   Left-click or Space fires (gunner), TAB toggles drone <-> gunner,
//   Q/E drone down/up, ESC quits.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

// ---------------------------------------------------------------------------
// Runtime config (Tanks.json)
// ---------------------------------------------------------------------------
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
};

// Loads Tanks.json from the working directory. Missing file or bad values
// fall back to the defaults above, so the game always runs.
static Config LoadConfig() {
    Config c;
    std::ifstream f("Tanks.json");
    if (!f) return c;
    try {
        nlohmann::json j;
        f >> j;
        auto shell    = j.value("shell", nlohmann::json::object());
        auto building = j.value("building", nlohmann::json::object());
        auto collapse = j.value("collapse", nlohmann::json::object());
        auto enemy    = j.value("enemy", nlohmann::json::object());
        auto ally     = j.value("ally", nlohmann::json::object());
        auto player   = j.value("player", nlohmann::json::object());
        c.shellSpeed       = shell.value("speed", c.shellSpeed);
        c.shellCooldown    = shell.value("cooldownSeconds", c.shellCooldown);
        c.shellLifetime    = shell.value("lifetimeSeconds", c.shellLifetime);
        c.shellRadius      = shell.value("radius", c.shellRadius);
        c.buildingHits     = building.value("hitsToDestroy", c.buildingHits);
        c.collapseDuration = collapse.value("durationSeconds", c.collapseDuration);
        c.playerHits       = player.value("hitsToDestroy", c.playerHits);
        c.playerSpeed      = player.value("speed", c.playerSpeed);
        c.enemyCount       = enemy.value("count", c.enemyCount);
        c.enemyHits        = enemy.value("hitsToDestroy", c.enemyHits);
        c.wreckBlocks      = enemy.value("wreckBlocksMovement", c.wreckBlocks);
        c.enemySpeed       = enemy.value("speed", c.enemySpeed);
        c.enemyRange       = enemy.value("shootRange", c.enemyRange);
        c.enemyFireInt     = enemy.value("fireInterval", c.enemyFireInt);
        c.enemySpread      = enemy.value("aimSpread", c.enemySpread);
        c.enemyAimTime     = enemy.value("aimTime", c.enemyAimTime);
        c.enemyCoverWait   = enemy.value("coverWaitTime", c.enemyCoverWait);
        c.allyCount        = ally.value("count", c.allyCount);
        c.allyHits         = ally.value("hitsToDestroy", c.allyHits);
        c.allySpeed        = ally.value("speed", c.allySpeed);
        c.allyEngage       = ally.value("engageOnSight", c.allyEngage);
        c.allyRange        = ally.value("shootRange", c.allyRange);
        c.allyFireInt      = ally.value("fireInterval", c.allyFireInt);
        c.allySpread       = ally.value("aimSpread", c.allySpread);
        c.allyAimTime      = ally.value("aimTime", c.allyAimTime);
        auto ec = enemy.value("color", nlohmann::json::object());
        c.enemyColor.r = (unsigned char)ec.value("r", (int)c.enemyColor.r);
        c.enemyColor.g = (unsigned char)ec.value("g", (int)c.enemyColor.g);
        c.enemyColor.b = (unsigned char)ec.value("b", (int)c.enemyColor.b);
        auto ac = ally.value("color", nlohmann::json::object());
        c.allyColor.r = (unsigned char)ac.value("r", (int)c.allyColor.r);
        c.allyColor.g = (unsigned char)ac.value("g", (int)c.allyColor.g);
        c.allyColor.b = (unsigned char)ac.value("b", (int)c.allyColor.b);
    } catch (...) { /* keep defaults */ }
    return c;
}

// ---------------------------------------------------------------------------
// World constants
// ---------------------------------------------------------------------------
static constexpr float ARENA_HALF   = 190.0f;  // playable square extends +/- this
static constexpr float TANK_RADIUS = 2.2f;    // collision circle around the tank
static constexpr float MAX_SPEED   = 14.0f;   // units / second
static constexpr float MAX_REVERSE = -6.0f;
static constexpr float ACCEL       = 18.0f;
static constexpr float TURN_RATE   = 1.9f;     // rad / second at full speed
static constexpr float TURRET_SENS = 0.0035f;  // rad per mouse pixel

// Wrap an angle to [-PI, PI].
static float NormalizeAngle(float a) {
    while (a > PI)  a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

// ---------------------------------------------------------------------------
// Village
// ---------------------------------------------------------------------------
struct HitMark {
    Vector3 pos;     // point of impact on the building surface
    Vector3 normal;  // outward face normal (for orienting the scorch)
    float seed;      // randomizes the blast splotch pattern
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
    std::vector<HitMark> marks;  // persistent scorch marks from shell hits
};

static Vector3 HitFaceNormal(const Vector3 &p, const Building &b) {
    // The AABB face with the least penetration is the one the shell struck.
    float px = b.size.x * 0.5f - fabsf(p.x - b.center.x);
    float py = b.size.y * 0.5f - fabsf(p.y - b.center.y);
    float pz = b.size.z * 0.5f - fabsf(p.z - b.center.z);
    if (px < py && px < pz) return Vector3{ (p.x > b.center.x) ? 1.0f : -1.0f, 0.0f, 0.0f };
    if (py < pz)            return Vector3{ 0.0f, (p.y > b.center.y) ? 1.0f : -1.0f, 0.0f };
    return Vector3{ 0.0f, 0.0f, (p.z > b.center.z) ? 1.0f : -1.0f };
}

static void DrawHitMark(const HitMark &m) {
    // Irregular blast damage: a soft central scorch plus satellite craters
    // at seeded offsets — reads as an explosion, not a bullseye.
    Vector3 axis; float angle;
    if (fabsf(m.normal.x) > 0.5f)      { axis = Vector3{ 0, 1, 0 }; angle = 90.0f * m.normal.x; }
    else if (fabsf(m.normal.y) > 0.5f) { axis = Vector3{ 1, 0, 0 }; angle = -90.0f * m.normal.y; }
    else                               { axis = Vector3{ 0, 1, 0 }; angle = (m.normal.z > 0) ? 0.0f : 180.0f; }
    // Two in-plane axes for scattering splotches across the wall face.
    Vector3 u, v;
    if (fabsf(m.normal.x) > 0.5f)      { u = Vector3{ 0, 1, 0 }; v = Vector3{ 0, 0, 1 }; }
    else if (fabsf(m.normal.y) > 0.5f) { u = Vector3{ 1, 0, 0 }; v = Vector3{ 0, 0, 1 }; }
    else                               { u = Vector3{ 1, 0, 0 }; v = Vector3{ 0, 1, 0 }; }

    Vector3 c = Vector3Add(m.pos, Vector3Scale(m.normal, 0.07f));
    DrawCircle3D(c, 0.7f, axis, angle, Color{ 48, 38, 30, 225 });
    float s = m.seed * 6.2831f;
    for (int i = 0; i < 3; ++i) {
        float a = s + i * 2.094f;
        float r = 0.5f + 0.25f * sinf(s * 3.0f + (float)i * 1.7f);
        Vector3 sc = Vector3Add(c, Vector3Add(Vector3Scale(u, cosf(a) * r),
                                              Vector3Scale(v, sinf(a) * r)));
        DrawCircle3D(sc, 0.3f, axis, angle, Color{ 22, 17, 13, 225 });
    }
    DrawCircle3D(c, 0.26f, axis, angle, Color{ 10, 8, 6, 255 });
}

// Deterministic broken-down village: a street grid with buildings of random
// height, some ruined (half height), plus rubble piles and broken walls.
static std::vector<Building> BuildVillage(int hitsToDestroy) {
    std::vector<Building> out;
    SetRandomSeed(1337);

    const float block = 40.0f;          // city block pitch

    for (float bx = -ARENA_HALF + block * 0.5f; bx < ARENA_HALF - block * 0.5f; bx += block) {
        for (float bz = -ARENA_HALF + block * 0.5f; bz < ARENA_HALF - block * 0.5f; bz += block) {
            // Leave the spawn clearing in the south-west corner empty.
            if (bx < -ARENA_HALF + block * 1.5f && bz > ARENA_HALF - block * 2.5f) continue;
            if (GetRandomValue(0, 9) < 2) continue;  // some empty lots -> rubble

            float w = (float)GetRandomValue(14, 22);
            float d = (float)GetRandomValue(14, 22);
            float h = (float)GetRandomValue(8, 26);
            bool ruined = GetRandomValue(0, 9) < 4;
            if (ruined) h *= 0.45f;

            float jx = (float)GetRandomValue(-4, 4);
            float jz = (float)GetRandomValue(-4, 4);

            Color c = ruined
                ? Color{ 105, 95, 85, 255 }    // broken concrete
                : Color{ 150, 130, 105, 255 }; // dusty plaster

            out.push_back(Building{
                Vector3{ bx + jx, h * 0.5f, bz + jz },
                Vector3{ w, h, d },
                c });

            // Rubble pile beside about half the buildings (low cover).
            if (GetRandomValue(0, 1) == 1) {
                float rw = (float)GetRandomValue(6, 10);
                float rh = (float)GetRandomValue(2, 4);
                out.push_back(Building{
                    Vector3{ bx + jx + w * 0.5f + rw * 0.4f, rh * 0.5f, bz + jz },
                    Vector3{ rw, rh, rw },
                    Color{ 95, 88, 80, 255 } });
            }
        }
    }

    // Broken perimeter walls (cover along the edges, with gaps).
    for (float x = -ARENA_HALF; x <= ARENA_HALF; x += 24.0f) {
        if (GetRandomValue(0, 9) < 3) continue;  // gaps in the wall
        float h = (float)GetRandomValue(3, 6);
        out.push_back(Building{ Vector3{ x, h * 0.5f, -ARENA_HALF }, Vector3{ 20.0f, h, 3.0f }, Color{ 110, 100, 90, 255 } });
        if (GetRandomValue(0, 9) < 7)
            out.push_back(Building{ Vector3{ x, h * 0.5f, ARENA_HALF }, Vector3{ 20.0f, h, 3.0f }, Color{ 110, 100, 90, 255 } });
        if (GetRandomValue(0, 9) < 7)
            out.push_back(Building{ Vector3{ -ARENA_HALF, h * 0.5f, x }, Vector3{ 3.0f, h, 20.0f }, Color{ 110, 100, 90, 255 } });
        if (GetRandomValue(0, 9) < 7)
            out.push_back(Building{ Vector3{ ARENA_HALF, h * 0.5f, x }, Vector3{ 3.0f, h, 20.0f }, Color{ 110, 100, 90, 255 } });
    }

    for (auto &b : out) b.hp = b.maxHp = hitsToDestroy;
    return out;
}

static void DrawVillage(const std::vector<Building> &village) {
    for (const Building &b : village) {
        if (b.destroyed) {
            if (b.collapseT < 1.0f) {
                // Collapse animation: tip over around a random horizontal axis
                // while sinking. Pivots at the ground so it reads as toppling.
                float t = b.collapseT;
                rlPushMatrix();
                rlTranslatef(b.center.x, 0.0f, b.center.z);
                rlRotatef(t * 68.0f, b.fallAxis.x, 0.0f, b.fallAxis.z);
                rlTranslatef(0.0f, b.center.y - t * b.size.y * 0.75f, 0.0f);
                DrawCube(Vector3{ 0, 0, 0 }, b.size.x, b.size.y, b.size.z,
                         Color{ 90, 82, 74, 255 });
                rlPopMatrix();
            } else {
                // Settled rubble: three low chunks where the building stood.
                // Non-blocking (tank can drive over), purely visual cover.
                float rx = b.size.x * 0.5f, rz = b.size.z * 0.5f;
                DrawCube(Vector3{ b.center.x - rx * 0.3f, 0.6f, b.center.z + rz * 0.2f },
                         rx * 0.9f, 1.2f, rz * 0.8f, Color{ 95, 88, 80, 255 });
                DrawCube(Vector3{ b.center.x + rx * 0.35f, 0.45f, b.center.z - rz * 0.25f },
                         rx * 0.7f, 0.9f, rz * 0.7f, Color{ 100, 92, 84, 255 });
                DrawCube(Vector3{ b.center.x + rx * 0.05f, 0.9f, b.center.z + rz * 0.05f },
                         rx * 0.5f, 1.8f, rz * 0.5f, Color{ 90, 82, 74, 255 });
            }
            continue;
        }
        // Damage tint: darkens as HP drops, so hits read visually.
        float f = 0.55f + 0.45f * ((float)b.hp / (float)b.maxHp);
        Color c = Color{ (unsigned char)(b.color.r * f), (unsigned char)(b.color.g * f),
                         (unsigned char)(b.color.b * f), 255 };
        DrawCube(b.center, b.size.x, b.size.y, b.size.z, c);
        DrawCubeWires(b.center, b.size.x, b.size.y, b.size.z, Color{ 0, 0, 0, 60 });
        // Darker "roof" cap so buildings read as 3D from the drone.
        DrawCube(Vector3{ b.center.x, b.size.y + 0.05f, b.center.z },
                 b.size.x * 0.98f, 0.1f, b.size.z * 0.98f,
                 Color{ 70, 62, 55, 255 });
        // Persistent scorch marks where shells struck.
        rlDisableBackfaceCulling();
        for (const auto &m : b.marks) DrawHitMark(m);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
}

// Push a circle (tank) out of every building AABB it overlaps, XZ plane.
// Destroyed buildings (settled rubble) no longer block.
static void ResolveBuildingCollisions(Vector3 &pos, const std::vector<Building> &village) {
    for (const Building &b : village) {
        if (b.destroyed) continue;
        float hx = b.size.x * 0.5f, hz = b.size.z * 0.5f;
        float dx = pos.x - b.center.x;
        float dz = pos.z - b.center.z;
        float cx = Clamp(dx, -hx, hx);
        float cz = Clamp(dz, -hz, hz);
        float px = dx - cx, pz = dz - cz;
        float distSq = px * px + pz * pz;
        if (distSq < TANK_RADIUS * TANK_RADIUS) {
            if (distSq > 1e-6f) {
                float dist = sqrtf(distSq);
                pos.x = b.center.x + cx + px / dist * TANK_RADIUS;
                pos.z = b.center.z + cz + pz / dist * TANK_RADIUS;
            } else {
                // Center inside the box: push out along the smallest axis.
                float ox = hx - fabsf(dx), oz = hz - fabsf(dz);
                if (ox < oz) pos.x = b.center.x + (dx >= 0 ? hx + TANK_RADIUS : -hx - TANK_RADIUS);
                else         pos.z = b.center.z + (dz >= 0 ? hz + TANK_RADIUS : -hz - TANK_RADIUS);
            }
        }
    }
    // Keep the tank inside the arena.
    pos.x = Clamp(pos.x, -ARENA_HALF, ARENA_HALF);
    pos.z = Clamp(pos.z, -ARENA_HALF, ARENA_HALF);
}

// ---------------------------------------------------------------------------
// Player tank
// ---------------------------------------------------------------------------
struct Tank {
    Vector3 pos   = { -ARENA_HALF + 30.0f, 0.0f, ARENA_HALF - 30.0f };
    float hullAngle   = 0.0f;  // radians, 0 = facing -Z
    float turretAngle = 0.0f;  // radians, relative to hull (rendering + stops)
    float aimAngle    = 0.0f;  // radians, world-space gun direction (stabilized sight)
    float speed       = 0.0f;
    int hp = 3;
    int maxHp = 3;
    float hitFlashT = 0.0f;    // red hit feedback timer
};

static Vector3 TurretWorldPos(const Tank &t) {
    return Vector3{ t.pos.x, 1.9f, t.pos.z };
}

static Vector3 TurretForward(const Tank &t) {
    float a = t.hullAngle + t.turretAngle;
    return Vector3{ sinf(a), 0.0f, -cosf(a) };  // 0 rad faces -Z
}

static void UpdateTank(Tank &t, const std::vector<Building> &village, float dt,
                       bool allowDrive, float maxSpeed) {
    float throttle = 0.0f;
    float steer = 0.0f;
    // Driving input only counts in gunner mode; in drone mode the tank
    // just rolls to a stop instead of shadowing the drone keys.
    if (allowDrive) {
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    throttle += 1.0f;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  throttle -= 1.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  steer -= 1.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) steer += 1.0f;
    }

    if (throttle > 0.0f)      t.speed = fminf(t.speed + ACCEL * dt, maxSpeed);
    else if (throttle < 0.0f) t.speed = fmaxf(t.speed - ACCEL * dt, MAX_REVERSE);
    else {
        // Engine braking.
        if (t.speed > 0.0f)      t.speed = fmaxf(t.speed - ACCEL * 1.5f * dt, 0.0f);
        else if (t.speed < 0.0f) t.speed = fminf(t.speed + ACCEL * 1.5f * dt, 0.0f);
    }

    // Tanks can pivot in place; scale a little with speed for feel.
    float turnAuthority = 0.55f + 0.45f * fminf(fabsf(t.speed) / maxSpeed, 1.0f);
    t.hullAngle += steer * TURN_RATE * turnAuthority * dt * (t.speed < 0.0f ? -1.0f : 1.0f);

    Vector3 fwd = { sinf(t.hullAngle), 0.0f, -cosf(t.hullAngle) };
    t.pos.x += fwd.x * t.speed * dt;
    t.pos.z += fwd.z * t.speed * dt;

    ResolveBuildingCollisions(t.pos, village);
    if (t.hitFlashT > 0.0f) t.hitFlashT -= dt;
}

// Hull mesh shared by the player and enemies. armor is the base color;
// the deck is derived lighter, treads/drums stay fixed.
static void DrawTankHull(const Vector3 &pos, float hullAngle, Color armor) {
    Color deck = { (unsigned char)fminf(armor.r * 1.14f, 255.0f),
                   (unsigned char)fminf(armor.g * 1.14f, 255.0f),
                   (unsigned char)fminf(armor.b * 1.14f, 255.0f), 255 };
    Vector3 hullC = { pos.x, 0.75f, pos.z };

    rlPushMatrix();
    rlTranslatef(hullC.x, hullC.y, hullC.z);
    // NOTE: negated — rlRotatef(+a) about Y turns local -Z toward -X, but our
    // angle convention faces (sin a, 0, -cos a), i.e. toward +X for a > 0.
    rlRotatef(-hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawCube(Vector3{ 0, 0, 0 }, 3.2f, 1.1f, 4.6f, armor);                         // hull
    DrawCube(Vector3{ 0, 0.75f, -0.4f }, 2.4f, 0.5f, 2.6f, deck);                  // upper deck
    DrawCube(Vector3{ -1.85f, -0.15f, 0 }, 0.7f, 0.9f, 4.8f, Color{ 45, 48, 44, 255 }); // treads
    DrawCube(Vector3{ 1.85f, -0.15f, 0 }, 0.7f, 0.9f, 4.8f, Color{ 45, 48, 44, 255 });
    // Forward/back cues so the gunner can read hull direction at a glance:
    // headlights on the nose, and two external
    // fuel drums on the rear deck (the back of the tank, Soviet-style).
    DrawCube(Vector3{ -0.9f, 0.6f, -2.33f }, 0.25f, 0.2f, 0.1f, Color{ 255, 240, 200, 255 }); // headlight L
    DrawCube(Vector3{  0.9f, 0.6f, -2.33f }, 0.25f, 0.2f, 0.1f, Color{ 255, 240, 200, 255 }); // headlight R
    for (float dx : { -0.7f, 0.7f }) {
        rlPushMatrix();
        rlTranslatef(dx, 0.83f, 1.6f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        DrawCylinder(Vector3{ 0, 0, 0 }, 0.28f, 0.28f, 0.9f, 10, Color{ 130, 75, 45, 255 }); // fuel drum
        rlPopMatrix();
    }
    rlPopMatrix();
}

// Barrel only. In gunner view the turret body is a translucent ghost but the
// gun itself stays solid — the gunner needs to see where it's pointing.
static void DrawTankBarrel(const Vector3 &center, float totalAngle) {
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // Barrel: a cylinder laid along -Z (forward), breech at the turret wall.
    // After rotating -90 deg about X the +Y height axis points down -Z and
    // the barrel spans z -1.3 to -3.3 from the turret center.
    rlPushMatrix();
    rlTranslatef(0.0f, 0.0f, -1.3f);
    rlRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    DrawCylinder(Vector3{ 0, 0, 0 }, 0.15f, 0.15f, 2.0f, 12, Color{ 50, 52, 48, 255 });
    rlPopMatrix();
    rlPopMatrix();
}

// Turret mesh (cylinder + barrel) centered at `center` (world), rotated by
// totalAngle = hullAngle + turretAngle. Used attached for player/enemies,
// and detached (with a spin) for a popped wreck turret.
static void DrawTankTurret(const Vector3 &center, float totalAngle, Color armor) {
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // NOTE: DrawCylinder's position is its BASE: base at -0.35 puts the
    // 0.7-tall cylinder spanning y -0.35..+0.35 around the center.
    DrawCylinder(Vector3{ 0, -0.35f, 0 }, 1.15f, 1.35f, 0.7f, 12, armor);
    rlPopMatrix();
    DrawTankBarrel(center, totalAngle);
}

static void DrawTank(const Tank &t, bool gunnerView) {
    static const Color PLAYER_ARMOR = { 74, 94, 62, 255 };
    DrawTankHull(t.pos, t.hullAngle, PLAYER_ARMOR);
    Vector3 tc = { t.pos.x, 2.25f, t.pos.z };
    float ta = t.hullAngle + t.turretAngle;
    if (!gunnerView) {
        // Solid turret in drone view.
        DrawTankTurret(tc, ta, PLAYER_ARMOR);
    } else {
        // Gunner view: solid barrel now, translucent turret body later
        // (DrawTurretGhost) so the camera inside can see through it.
        DrawTankBarrel(tc, ta);
    }

    // Heading whisker so turret direction is readable from the drone.
    // Hidden in gunner view where it would cross the camera.
    if (!gunnerView) {
        Vector3 tp = TurretWorldPos(t);
        Vector3 tf = TurretForward(t);
        DrawLine3D(tp, Vector3{ tp.x + tf.x * 8.0f, tp.y, tp.z + tf.z * 8.0f }, YELLOW);
    }
}

// Translucent turret shell for the gunner view. Drawn AFTER the village so the
// world blends through it, with backface culling disabled so the camera inside
// the ring sees the interior ghost. The batch is flushed before culling is
// re-enabled because raylib reads the cull state at flush time.
static void DrawTurretGhost(const Tank &t) {
    Vector3 hullC = { t.pos.x, 0.75f, t.pos.z };
    rlPushMatrix();
    rlTranslatef(hullC.x, hullC.y, hullC.z);
    rlRotatef(-t.hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(-t.turretAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlDisableBackfaceCulling();
    // Full-height ghost: faint glass fill, then a dark wireframe cage so the
    // turret's structure reads from inside without blocking the hull or the
    // world. Nothing solid here — the drive view stays open.
    DrawCylinder(Vector3{ 0, 1.15f, 0 }, 1.15f, 1.35f, 0.7f, 12, Color{ 74, 94, 62, 70 });
    rlDrawRenderBatchActive();
    DrawCylinderWires(Vector3{ 0, 1.15f, 0 }, 1.15f, 1.35f, 0.7f, 12, Color{ 22, 32, 18, 255 });
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlPopMatrix();
}

// ---------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------
enum class CamMode { GUNNER, DRONE };

struct DroneCam {
    Vector3 pos   = { -ARENA_HALF + 30.0f, 40.0f, ARENA_HALF + 20.0f };
    float yaw   = 0.0f;   // radians, 0 = looking -Z
    float pitch = -0.5f;  // radians, negative looks down
};

static void UpdateDrone(DroneCam &d, float dt) {
    // Arrow keys rotate the view (mouse is a free cursor for orders).
    if (IsKeyDown(KEY_LEFT))  d.yaw   -= 1.5f * dt;
    if (IsKeyDown(KEY_RIGHT)) d.yaw   += 1.5f * dt;
    if (IsKeyDown(KEY_UP))    d.pitch = Clamp(d.pitch - 1.0f * dt, -1.45f, 1.45f);
    if (IsKeyDown(KEY_DOWN))  d.pitch = Clamp(d.pitch + 1.0f * dt, -1.45f, 1.45f);

    Vector3 fwd = { sinf(d.yaw) * cosf(d.pitch), sinf(d.pitch), -cosf(d.yaw) * cosf(d.pitch) };
    Vector3 right = { -fwd.z, 0.0f, fwd.x };
    right = Vector3Normalize(right);

    float sp = 40.0f * (IsKeyDown(KEY_LEFT_SHIFT) ? 2.5f : 1.0f);
    if (IsKeyDown(KEY_W))    d.pos = Vector3Add(d.pos, Vector3Scale(fwd, sp * dt));
    if (IsKeyDown(KEY_S))    d.pos = Vector3Subtract(d.pos, Vector3Scale(fwd, sp * dt));
    if (IsKeyDown(KEY_A))    d.pos = Vector3Subtract(d.pos, Vector3Scale(right, sp * dt));
    if (IsKeyDown(KEY_D))    d.pos = Vector3Add(d.pos, Vector3Scale(right, sp * dt));
    if (IsKeyDown(KEY_Q)) d.pos.y -= sp * dt;
    if (IsKeyDown(KEY_E)) d.pos.y += sp * dt;
    d.pos.y = Clamp(d.pos.y, 2.0f, 150.0f);
}

// ---------------------------------------------------------------------------
// Shooting (v0.2): shells, muzzle flash, hit detection, destruction
// ---------------------------------------------------------------------------
struct Shell {
    Vector3 pos;
    Vector3 vel;
    float life;
    bool fromEnemy = false;  // true: hostile shell, hits the player
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

// Expanding ring ping shown where an order was issued (drone mode).
struct OrderPing {
    Vector3 pos;
    float t;
    float maxT;
    Color color;
};

// Lightweight particle for explosions, smoke, and fire. No pooling —
// counts stay small (a kill bursts ~15), so a vector is fine.
struct Particle {
    Vector3 pos;
    Vector3 vel;
    float life;
    float maxLife;
    float size;
    Color color;
    float grav;   // vertical accel; negative rises (smoke), positive falls
};

// Enemy tank AI states (v0.5).
enum class AIState { ADVANCE, SHOOT, SEEK_COVER, COVER_WAIT };

// Enemy tank: AI-driven in v0.5 (advance / shoot / seek-cover).
// On death the turret pops off ballistically and the hull becomes a
// persistent burning wreck.
struct Enemy {
    Vector3 pos;
    float hullAngle   = 0.0f;
    float turretAngle = 0.0f;
    int hp = 2;
    int maxHp = 2;
    bool alive = true;
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
};

// Allied tank orders (v0.6, issued in drone mode).
enum class AllyOrder { FOLLOW, MOVE, HOLD, ATTACK };

// Allied tank: follows player orders, engages enemies on sight (if
// configured). Death uses the same turret-pop + burning wreck as enemies.
struct Ally {
    Vector3 pos;
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
    // Death animation (same as Enemy).
    float deathT = 0.0f;
    Vector3 turretPos;
    Vector3 turretVel;
    float turretSpin = 0.0f;
    float turretSpinVel = 0.0f;
    bool turretLanded = false;
    float burnAccum = 0.0f;
};

static Vector3 MuzzleWorldPos(const Tank &t) {
    Vector3 fwd = TurretForward(t);
    // Tip of the barrel: 3.3 forward of the turret center at barrel height.
    // Pushed 0.25 further out so the flash sits just beyond the muzzle,
    // where the round leaves — unambiguously at the tip, not the breech.
    return Vector3{ t.pos.x + fwd.x * 3.55f, 2.25f, t.pos.z + fwd.z * 3.55f };
}

static void FireShell(const Tank &t, std::vector<Shell> &shells, const Config &cfg) {
    Vector3 fwd = TurretForward(t);
    Vector3 muzzle = MuzzleWorldPos(t);
    Shell s;
    s.pos = muzzle;
    s.vel = Vector3Scale(fwd, cfg.shellSpeed);
    s.life = cfg.shellLifetime;
    s.trail[0] = muzzle;
    s.trailCount = 1;
    shells.push_back(s);
}

static bool ShellHitsBuilding(const Vector3 &p, float r, const Building &b) {
    float hx = b.size.x * 0.5f, hy = b.size.y * 0.5f, hz = b.size.z * 0.5f;
    float cx = Clamp(p.x, b.center.x - hx, b.center.x + hx);
    float cy = Clamp(p.y, b.center.y - hy, b.center.y + hy);
    float cz = Clamp(p.z, b.center.z - hz, b.center.z + hz);
    float dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
    return (dx * dx + dy * dy + dz * dz) < r * r;
}

// Does the segment A->B hit any standing building? For AI line-of-sight.
static bool LosBlocked(const Vector3 &a, const Vector3 &b, const std::vector<Building> &village) {
    Vector3 d = Vector3Subtract(b, a);
    for (const auto &bd : village) {
        if (bd.destroyed) continue;
        float tmin = 0.0f, tmax = 1.0f;
        Vector3 mn = { bd.center.x - bd.size.x * 0.5f, 0.0f, bd.center.z - bd.size.z * 0.5f };
        Vector3 mx = { bd.center.x + bd.size.x * 0.5f, bd.size.y, bd.center.z + bd.size.z * 0.5f };
        const float o[3] = { a.x, a.y, a.z }, dd[3] = { d.x, d.y, d.z };
        const float n[3] = { mn.x, mn.y, mn.z }, x[3] = { mx.x, mx.y, mx.z };
        bool hit = true;
        for (int i = 0; i < 3; ++i) {
            if (fabsf(dd[i]) < 1e-8f) {
                if (o[i] < n[i] || o[i] > x[i]) { hit = false; break; }
            } else {
                float t1 = (n[i] - o[i]) / dd[i], t2 = (x[i] - o[i]) / dd[i];
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                tmin = fmaxf(tmin, t1); tmax = fminf(tmax, t2);
                if (tmin > tmax) { hit = false; break; }
            }
        }
        if (hit) return true;
    }
    return false;
}

// Nearest building between the enemy and the player; returns a spot on the
// far side to hide behind. Falls back to the enemy's position (no cover).
static Vector3 PickCover(const Enemy &e, const Vector3 &playerPos,
                         const std::vector<Building> &village) {
    Vector3 best = e.pos;
    float bestD2 = 1e18f;
    for (const auto &b : village) {
        if (b.destroyed) continue;
        float ex = playerPos.x - e.pos.x, ez = playerPos.z - e.pos.z;
        float elen2 = ex * ex + ez * ez;
        if (elen2 < 1e-3f) continue;
        float bx = b.center.x - e.pos.x, bz = b.center.z - e.pos.z;
        float t = (bx * ex + bz * ez) / elen2;  // 0=enemy, 1=player
        if (t < 0.15f || t > 0.85f) continue;
        float px = bx - ex * t, pz = bz - ez * t;
        float bRad = fmaxf(b.size.x, b.size.z) * 0.5f;
        if (px * px + pz * pz > (bRad + 3.0f) * (bRad + 3.0f)) continue;
        float ax = b.center.x - playerPos.x, az = b.center.z - playerPos.z;
        float alen = sqrtf(ax * ax + az * az);
        if (alen < 1e-3f) continue;
        Vector3 spot = { b.center.x + ax / alen * (bRad + 5.0f), 0.0f,
                         b.center.z + az / alen * (bRad + 5.0f) };
        float sx = spot.x - e.pos.x, sz = spot.z - e.pos.z;
        float d2 = sx * sx + sz * sz;
        if (d2 < bestD2) { bestD2 = d2; best = spot; }
    }
    return best;
}

static void UpdateEnemyAI(Enemy &e, const Tank &player, const std::vector<Building> &village,
                          std::vector<Shell> &shells, std::vector<Flash> &flashes,
                          const Config &cfg, float dt) {
    if (!e.alive) return;
    e.aiTimer += dt;
    e.fireTimer += dt;
    if (e.hitFlashT > 0.0f) e.hitFlashT -= dt;

    float dx = player.pos.x - e.pos.x, dz = player.pos.z - e.pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    Vector3 eye = { e.pos.x, 2.25f, e.pos.z };
    Vector3 tgt = { player.pos.x, 2.0f, player.pos.z };
    bool los = !LosBlocked(eye, tgt, village);
    bool inRange = dist < cfg.enemyRange;

    auto driveToward = [&](float wantAngle, float speed) {
        float diff = NormalizeAngle(wantAngle - e.hullAngle);
        e.hullAngle += Clamp(diff * 3.0f, -1.6f, 1.6f) * dt;
        float throttle = (fabsf(diff) < 0.6f) ? 1.0f : 0.25f;
        e.pos.x += sinf(e.hullAngle) * speed * throttle * dt;
        e.pos.z += -cosf(e.hullAngle) * speed * throttle * dt;
    };

    switch (e.aiState) {
    case AIState::ADVANCE: {
        float want = atan2f(dx, -dz);
        float throttleScale = (los && inRange && dist < cfg.enemyRange * 0.7f) ? 0.0f : 1.0f;
        if (throttleScale > 0.0f) driveToward(want, cfg.enemySpeed);
        // Turret relaxes toward hull-forward when not engaged.
        e.turretAngle = NormalizeAngle(e.turretAngle - e.turretAngle * fminf(dt * 2.0f, 1.0f));
        if (los && inRange) {
            e.aiState = AIState::SHOOT;
            e.aiTimer = 0.0f;
            e.aimTimer = 0.0f;
        }
        break;
    }
    case AIState::SHOOT: {
        // Turret visibly tracks the player — the telegraph.
        float wantWorld = atan2f(dx, -dz);
        float wantTurret = NormalizeAngle(wantWorld - e.hullAngle);
        float tdiff = NormalizeAngle(wantTurret - e.turretAngle);
        e.turretAngle += Clamp(tdiff * 4.0f, -2.5f, 2.5f) * dt;
        if (los && inRange) {
            e.aimTimer += dt;
            if (e.aimTimer >= cfg.enemyAimTime && e.fireTimer >= cfg.enemyFireInt) {
                float spread = ((float)GetRandomValue(-100, 100) / 100.0f) * cfg.enemySpread;
                float fa = e.hullAngle + e.turretAngle + spread;
                Vector3 fwd = { sinf(fa), 0.0f, -cosf(fa) };
                Vector3 muzzle = { e.pos.x + fwd.x * 3.55f, 2.25f, e.pos.z + fwd.z * 3.55f };
                Shell s;
                s.pos = muzzle;
                s.vel = Vector3Scale(fwd, cfg.shellSpeed);
                s.life = cfg.shellLifetime;
                s.fromEnemy = true;
                shells.push_back(s);
                flashes.push_back(Flash{ muzzle, 0.22f, 0.22f, 0.9f });
                e.fireTimer = 0.0f;
                e.aimTimer = 0.0f;
            }
        } else {
            e.aimTimer = 0.0f;  // lost the lock
            e.aiState = AIState::ADVANCE;
            e.aiTimer = 0.0f;
        }
        break;
    }
    case AIState::SEEK_COVER: {
        float cdx = e.coverPos.x - e.pos.x, cdz = e.coverPos.z - e.pos.z;
        if (cdx * cdx + cdz * cdz < 9.0f) {
            e.aiState = AIState::COVER_WAIT;
            e.aiTimer = 0.0f;
        } else {
            driveToward(atan2f(cdx, -cdz), cfg.enemySpeed);
        }
        break;
    }
    case AIState::COVER_WAIT: {
        if (e.aiTimer >= cfg.enemyCoverWait) {
            e.aiState = AIState::ADVANCE;
            e.aiTimer = 0.0f;
        }
        break;
    }
    }

    ResolveBuildingCollisions(e.pos, village);
}

static void DamageBuilding(Building &b, const Vector3 &hitPos) {
    if (b.destroyed || b.hp <= 0) return;
    float seed = (float)GetRandomValue(0, 1000) / 1000.0f;
    b.marks.push_back(HitMark{ hitPos, HitFaceNormal(hitPos, b), seed });
    if (--b.hp <= 0) {
        b.destroyed = true;
        b.collapseT = 0.0f;
        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
        b.fallAxis = Vector3{ cosf(a), 0.0f, sinf(a) };
    }
}

// ---------------------------------------------------------------------------
// Ally tanks
// ---------------------------------------------------------------------------

// Enemy tanks
// ---------------------------------------------------------------------------
// Deterministic scatter: clear of buildings, of the player spawn, and of
// each other. Stationary until v0.5 AI.
static std::vector<Enemy> SpawnEnemies(int count, int hits, const std::vector<Building> &village) {
    std::vector<Enemy> out;
    SetRandomSeed(4242);
    int guard = 0;
    while ((int)out.size() < count && guard++ < 2000) {
        float x = (float)GetRandomValue(-140, 140);
        float z = (float)GetRandomValue(-140, 140);
        // Keep clear of the player spawn (south-west).
        float dx = x - (-ARENA_HALF + 30.0f), dz = z - (ARENA_HALF - 30.0f);
        if (dx * dx + dz * dz < 45.0f * 45.0f) continue;
        // Not inside (or hugging) a building.
        bool bad = false;
        for (const auto &b : village) {
            if (fabsf(x - b.center.x) < b.size.x * 0.5f + 5.0f &&
                fabsf(z - b.center.z) < b.size.z * 0.5f + 5.0f) { bad = true; break; }
        }
        if (bad) continue;
        // Spaced from other enemies.
        for (const auto &e : out) {
            float ex = x - e.pos.x, ez = z - e.pos.z;
            if (ex * ex + ez * ez < 30.0f * 30.0f) { bad = true; break; }
        }
        if (bad) continue;
        Enemy e;
        e.pos = { x, 0.0f, z };
        e.hullAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
        e.turretAngle = (float)GetRandomValue(-60, 60) * DEG2RAD;
        e.hp = e.maxHp = hits;
        out.push_back(e);
    }
    return out;
}

// Allies spawn in formation near the player (south-west corner).
static std::vector<Ally> SpawnAllies(int count, int hits, const Vector3 &playerPos) {
    std::vector<Ally> out;
    for (int i = 0; i < count; ++i) {
        Ally a;
        // Echelon left: behind and to the side of the player.
        a.pos = { playerPos.x - 8.0f - (float)i * 7.0f, 0.0f, playerPos.z + 6.0f + (float)i * 4.0f };
        a.hullAngle = 0.0f;
        a.hp = a.maxHp = hits;
        a.order = AllyOrder::FOLLOW;
        out.push_back(a);
    }
    return out;
}

// Generous hitbox: vertical cylinder around the tank. Shells fly at
// turret height, so this reads as hitting the turret/mass.
static bool ShellHitsEnemy(const Vector3 &p, float r, const Enemy &e) {
    if (!e.alive) return false;
    float dx = p.x - e.pos.x, dz = p.z - e.pos.z;
    float rr = 2.2f + r;
    return dx * dx + dz * dz < rr * rr && p.y > 0.0f && p.y < 3.2f;
}

static void Burst(std::vector<Particle> &ps, Vector3 c, int n,
                  Color color, float speed, float up, float size, float life, float grav) {
    for (int i = 0; i < n; ++i) {
        float a = (float)GetRandomValue(0, 360) * DEG2RAD;
        float s = (float)GetRandomValue(20, 100) / 100.0f * speed;
        Particle p;
        p.pos = c;
        p.vel = { cosf(a) * s, up * ((float)GetRandomValue(50, 130) / 100.0f), sinf(a) * s };
        p.life = p.maxLife = life * ((float)GetRandomValue(70, 130) / 100.0f);
        p.size = size * ((float)GetRandomValue(70, 130) / 100.0f);
        p.color = color;
        p.grav = grav;
        ps.push_back(p);
    }
}
// Wreck animation shared by enemy and ally deaths (turret pop + burn).
template <typename T>
static void UpdateWreckAnim(T &u, std::vector<Particle> &particles, float dt) {
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

// Shared kill sequence: fireball, particles, turret pop.
template <typename T>
static void KillUnit(T &u, std::vector<Particle> &particles, std::vector<Flash> &flashes) {
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
static void DamageAlly(Ally &a, std::vector<Particle> &particles, std::vector<Flash> &flashes) {
    if (!a.alive) return;
    a.hitFlashT = 0.18f;
    if (--a.hp > 0) return;
    KillUnit(a, particles, flashes);
}

static void UpdateAllies(std::vector<Ally> &allies, const Tank &player,
                         std::vector<Enemy> &enemies, const std::vector<Building> &village,
                         std::vector<Shell> &shells, std::vector<Flash> &flashes,
                         std::vector<Particle> &particles, const Config &cfg, float dt) {
    for (size_t ai = 0; ai < allies.size(); ++ai) {
        Ally &a = allies[ai];
        if (!a.alive) {
            UpdateWreckAnim(a, particles, dt);
            continue;
        }
        if (a.hitFlashT > 0.0f) a.hitFlashT -= dt;
        a.fireTimer += dt;

        // Pick a target: explicit ATTACK order, else nearest visible in range.
        int tgt = -1;
        if (a.order == AllyOrder::ATTACK && a.targetEnemy >= 0 &&
            a.targetEnemy < (int)enemies.size() && enemies[a.targetEnemy].alive) {
            tgt = a.targetEnemy;
        } else if (cfg.allyEngage) {
            float bestD2 = cfg.allyRange * cfg.allyRange;
            Vector3 eye = { a.pos.x, 2.25f, a.pos.z };
            for (size_t ei = 0; ei < enemies.size(); ++ei) {
                if (!enemies[ei].alive) continue;
                float dx = enemies[ei].pos.x - a.pos.x, dz = enemies[ei].pos.z - a.pos.z;
                float d2 = dx * dx + dz * dz;
                if (d2 > bestD2) continue;
                Vector3 et = { enemies[ei].pos.x, 2.0f, enemies[ei].pos.z };
                if (LosBlocked(eye, et, village)) continue;
                bestD2 = d2; tgt = (int)ei;
            }
        }
        a.engageIdx = tgt;

        auto driveToward = [&](float wantAngle, float speed) {
            float diff = NormalizeAngle(wantAngle - a.hullAngle);
            a.hullAngle += Clamp(diff * 3.0f, -1.6f, 1.6f) * dt;
            float throttle = (fabsf(diff) < 0.6f) ? 1.0f : 0.25f;
            a.pos.x += sinf(a.hullAngle) * speed * throttle * dt;
            a.pos.z += -cosf(a.hullAngle) * speed * throttle * dt;
        };

        if (tgt >= 0) {
            // Engaging: turret tracks, fire when locked.
            Enemy &e = enemies[tgt];
            float dx = e.pos.x - a.pos.x, dz = e.pos.z - a.pos.z;
            float dist = sqrtf(dx * dx + dz * dz);
            float wantWorld = atan2f(dx, -dz);
            float wantTurret = NormalizeAngle(wantWorld - a.hullAngle);
            float tdiff = NormalizeAngle(wantTurret - a.turretAngle);
            a.turretAngle += Clamp(tdiff * 4.0f, -2.5f, 2.5f) * dt;
            Vector3 eye = { a.pos.x, 2.25f, a.pos.z };
            Vector3 et = { e.pos.x, 2.0f, e.pos.z };
            if (!LosBlocked(eye, et, village) && dist < cfg.allyRange) {
                a.aimTimer += dt;
                if (a.aimTimer >= cfg.allyAimTime && a.fireTimer >= cfg.allyFireInt) {
                    float spread = ((float)GetRandomValue(-100, 100) / 100.0f) * cfg.allySpread;
                    float fa = a.hullAngle + a.turretAngle + spread;
                    Vector3 fwd = { sinf(fa), 0.0f, -cosf(fa) };
                    Vector3 muzzle = { a.pos.x + fwd.x * 3.55f, 2.25f, a.pos.z + fwd.z * 3.55f };
                    Shell s;
                    s.pos = muzzle;
                    s.vel = Vector3Scale(fwd, cfg.shellSpeed);
                    s.life = cfg.shellLifetime;
                    s.fromEnemy = false;  // hits enemies, not the player
                    shells.push_back(s);
                    flashes.push_back(Flash{ muzzle, 0.22f, 0.22f, 0.9f });
                    a.fireTimer = 0.0f;
                    a.aimTimer = 0.0f;
                }
            } else {
                a.aimTimer = 0.0f;
            }
            // ATTACK order closes distance; otherwise hold while shooting.
            if (a.order == AllyOrder::ATTACK && dist > cfg.allyRange * 0.7f)
                driveToward(wantWorld, cfg.allySpeed);
        } else {
            // No target: follow orders.
            a.aimTimer = 0.0f;
            a.turretAngle = NormalizeAngle(a.turretAngle - a.turretAngle * fminf(dt * 2.0f, 1.0f));
            Vector3 dest = a.pos;
            bool move = false;
            if (a.order == AllyOrder::FOLLOW) {
                float pa = player.hullAngle;
                Vector3 fwd = { sinf(pa), 0.0f, -cosf(pa) };
                Vector3 right = { -fwd.z, 0.0f, fwd.x };
                float side = (allies.size() <= 2) ? (ai == 0 ? -7.0f : 7.0f)
                                                  : ((float)ai - (float)(allies.size() - 1) / 2.0f) * 7.0f;
                dest = { player.pos.x - fwd.x * 10.0f + right.x * side, 0.0f,
                         player.pos.z - fwd.z * 10.0f + right.z * side };
                move = true;
            } else if (a.order == AllyOrder::MOVE) {
                dest = a.orderPos;
                move = true;
            }
            if (move) {
                float dx = dest.x - a.pos.x, dz = dest.z - a.pos.z;
                if (dx * dx + dz * dz > 9.0f) {
                    driveToward(atan2f(dx, -dz), cfg.allySpeed);
                } else if (a.order == AllyOrder::MOVE) {
                    a.order = AllyOrder::HOLD;  // arrived
                }
            }
        }

        ResolveBuildingCollisions(a.pos, village);
    }
}

// ---------------------------------------------------------------------------

static void DamageEnemy(Enemy &e, std::vector<Particle> &particles,
                        std::vector<Flash> &flashes, const Vector3 &playerPos,
                        const std::vector<Building> &village) {
    if (!e.alive) return;
    e.hitFlashT = 0.18f;
    if (--e.hp > 0) {
        // Nonlethal hit: break off and seek cover behind a building.
        e.coverPos = PickCover(e, playerPos, village);
        e.aiState = AIState::SEEK_COVER;
        e.aiTimer = 0.0f;
        e.aimTimer = 0.0f;
        return;
    }
    // Kill: fireball flash, flame + smoke burst, turret pops off.
    e.alive = false;
    e.deathT = 0.0f;
    Vector3 c = { e.pos.x, 1.6f, e.pos.z };
    flashes.push_back(Flash{ c, 0.55f, 0.55f, 3.8f });
    Burst(particles, c, 10, Color{ 255, 150, 40, 255 }, 9.0f, 7.0f, 0.9f, 0.7f, 6.0f);   // flames
    Burst(particles, c, 12, Color{ 90, 85, 80, 255 }, 4.0f, 9.0f, 1.4f, 2.6f, -3.0f);    // smoke
    Burst(particles, c, 6, Color{ 255, 220, 120, 255 }, 14.0f, 5.0f, 0.5f, 0.4f, 10.0f);  // sparks
    e.turretPos = { e.pos.x, 2.25f, e.pos.z };
    // Strong outward pop: guaranteed to clear the hull (half-diagonal ~2.8
    // + turret radius 1.35) so the barrel doesn't end up inside the wreck.
    float popA = (float)GetRandomValue(0, 360) * DEG2RAD;
    float popS = (float)GetRandomValue(50, 80) / 10.0f;
    e.turretVel = { cosf(popA) * popS,
                    (float)GetRandomValue(75, 115) / 10.0f,
                    sinf(popA) * popS };
    e.turretSpinVel = (float)GetRandomValue(-9, 9);
    e.turretLanded = false;
    e.burnAccum = 0.0f;
}

static void UpdateEnemies(std::vector<Enemy> &enemies, std::vector<Particle> &particles, float dt) {
    for (auto &e : enemies) {
        if (e.hitFlashT > 0.0f) e.hitFlashT -= dt;
        if (e.alive) continue;
        e.deathT += dt;
        // Popped turret: ballistic arc, then rests where it lands.
        if (!e.turretLanded) {
            e.turretVel.y -= 22.0f * dt;
            e.turretPos = Vector3Add(e.turretPos, Vector3Scale(e.turretVel, dt));
            e.turretSpin += e.turretSpinVel * dt;
            if (e.turretPos.y <= 0.55f) {
                e.turretPos.y = 0.55f;
                // Safety: never rest inside the hull wreck — push out so the
                // barrel stays visible instead of buried in the hull.
                float dx = e.turretPos.x - e.pos.x, dz = e.turretPos.z - e.pos.z;
                float d2 = dx * dx + dz * dz;
                if (d2 < 4.5f * 4.5f) {
                    float d = sqrtf(d2);
                    if (d < 1e-3f) { dx = 1.0f; dz = 0.0f; d = 1.0f; }
                    e.turretPos.x = e.pos.x + dx / d * 4.5f;
                    e.turretPos.z = e.pos.z + dz / d * 4.5f;
                }
                e.turretLanded = true;
            }
        }
        // Persistent burn: flame flicker + rising smoke wisps.
        e.burnAccum += dt;
        if (e.burnAccum >= 0.22f) {
            e.burnAccum = 0.0f;
            Vector3 c = { e.pos.x + (float)GetRandomValue(-10, 10) / 10.0f, 1.4f,
                          e.pos.z + (float)GetRandomValue(-10, 10) / 10.0f };
            Burst(particles, c, 1, Color{ 255, 130, 30, 255 }, 1.0f, 2.5f, 0.55f, 0.5f, -2.0f);
            Burst(particles, c, 1, Color{ 70, 66, 60, 255 }, 0.8f, 4.0f, 0.9f, 2.0f, -3.0f);
        }
    }
    // Particles: integrate, gravity, expire.
    for (auto it = particles.begin(); it != particles.end();) {
        it->vel.y -= it->grav * dt;
        it->pos = Vector3Add(it->pos, Vector3Scale(it->vel, dt));
        it->life -= dt;
        it = (it->life <= 0.0f) ? particles.erase(it) : std::next(it);
    }
}

// Push the player circle out of every wreck (if blocking is enabled).
static void ResolveWreckCollisions(Vector3 &pos, const std::vector<Enemy> &enemies, bool wreckBlocks) {
    if (!wreckBlocks) return;
    for (const auto &e : enemies) {
        if (e.alive) continue;
        float dx = pos.x - e.pos.x, dz = pos.z - e.pos.z;
        float rr = TANK_RADIUS + 2.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 < rr * rr && d2 > 1e-6f) {
            float d = sqrtf(d2);
            pos.x = e.pos.x + dx / d * rr;
            pos.z = e.pos.z + dz / d * rr;
        }
    }
}

static void DrawEnemies(const std::vector<Enemy> &enemies, const Config &cfg) {
    static const Color CHARRED = { 38, 33, 28, 255 };
    for (const auto &e : enemies) {
        if (e.alive) {
            Color armor = cfg.enemyColor;
            if (e.hitFlashT > 0.0f) armor = Color{ 255, 240, 230, 255 };
            DrawTankHull(e.pos, e.hullAngle, armor);
            DrawTankTurret(Vector3{ e.pos.x, 2.25f, e.pos.z },
                           e.hullAngle + e.turretAngle, armor);
            // Tracking ping: pulsing red ring under an enemy with a lock.
            if (e.aiState == AIState::SHOOT && e.aimTimer > 0.05f) {
                float pulse = 2.8f + sinf(e.aimTimer * 14.0f) * 0.5f;
                DrawCylinderWires(Vector3{ e.pos.x, 0.08f, e.pos.z },
                                  pulse, pulse, 0.12f, 24, Color{ 255, 40, 40, 230 });
            }
        } else {
            // Burning wreck: charred hull, turret where it landed.
            DrawTankHull(e.pos, e.hullAngle, CHARRED);
            DrawTankTurret(e.turretPos, e.turretSpin, CHARRED);
            // Fire flicker on the hull.
            float f = 0.75f + 0.25f * sinf(e.deathT * 13.0f);
            DrawSphere(Vector3{ e.pos.x, 1.7f, e.pos.z }, 0.55f * f, Color{ 255, 120, 25, 210 });
            DrawSphere(Vector3{ e.pos.x + 0.5f, 1.5f, e.pos.z - 0.3f }, 0.35f * f,
                       Color{ 255, 190, 60, 190 });
        }
    }
}

// Allies: blue armor, selection ring for the ordered unit, objective marker.
static void DrawAllies(const std::vector<Ally> &allies, const std::vector<Enemy> &enemies,
                       const Config &cfg, int selected) {
    static const Color CHARRED = { 38, 33, 28, 255 };
    for (size_t i = 0; i < allies.size(); ++i) {
        const auto &a = allies[i];
        if (a.alive) {
            Color armor = cfg.allyColor;
            if (a.hitFlashT > 0.0f) armor = Color{ 255, 240, 230, 255 };
            DrawTankHull(a.pos, a.hullAngle, armor);
            DrawTankTurret(Vector3{ a.pos.x, 2.25f, a.pos.z },
                           a.hullAngle + a.turretAngle, armor);
            // Selected: white pulsing ring.
            if ((int)i == selected) {
                float pulse = 2.8f + sinf((float)GetTime() * 6.0f) * 0.4f;
                DrawCylinderWires(Vector3{ a.pos.x, 0.08f, a.pos.z },
                                  pulse, pulse, 0.12f, 24, Color{ 255, 255, 255, 230 });
            }
            // Objective marker.
            if (a.order == AllyOrder::MOVE) {
                // Light pillar + pulsing ring + line from the ally.
                DrawCylinder(Vector3{ a.orderPos.x, 5.0f, a.orderPos.z },
                             0.3f, 0.3f, 10.0f, 12, Color{ 80, 160, 255, 90 });
                float pulse = 1.5f + sinf((float)GetTime() * 5.0f) * 0.3f;
                DrawCylinderWires(Vector3{ a.orderPos.x, 0.08f, a.orderPos.z },
                                  pulse, pulse, 0.12f, 16, Color{ 80, 160, 255, 230 });
                DrawLine3D(Vector3{ a.pos.x, 0.5f, a.pos.z },
                           Vector3{ a.orderPos.x, 0.5f, a.orderPos.z },
                           Color{ 80, 160, 255, 150 });
            } else if (a.order == AllyOrder::ATTACK && a.targetEnemy >= 0 &&
                       a.targetEnemy < (int)enemies.size() && enemies[a.targetEnemy].alive) {
                const auto &e = enemies[a.targetEnemy];
                DrawCylinderWires(Vector3{ e.pos.x, 0.08f, e.pos.z },
                                  3.2f, 3.2f, 0.12f, 24, Color{ 255, 80, 80, 230 });
            }
        } else {
            DrawTankHull(a.pos, a.hullAngle, CHARRED);
            DrawTankTurret(a.turretPos, a.turretSpin, CHARRED);
            float f = 0.75f + 0.25f * sinf(a.deathT * 13.0f);
            DrawSphere(Vector3{ a.pos.x, 1.7f, a.pos.z }, 0.55f * f, Color{ 255, 120, 25, 210 });
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    const int screenWidth = 1280, screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Tankshooter v0.6");
    SetTargetFPS(60);
    DisableCursor();

    Config cfg = LoadConfig();
    std::vector<Building> village = BuildVillage(cfg.buildingHits);
    std::vector<Enemy> enemies = SpawnEnemies(cfg.enemyCount, cfg.enemyHits, village);
    Tank tank;
    tank.hp = tank.maxHp = cfg.playerHits;
    std::vector<Ally> allies = SpawnAllies(cfg.allyCount, cfg.allyHits, tank.pos);
    int selectedAlly = 0;
    std::vector<Shell> shells;
    std::vector<Flash> flashes;
    std::vector<OrderPing> pings;
    std::vector<Particle> particles;
    float fireCooldown = 0.0f;
    DroneCam drone;
    Vector2 droneCursor = { screenWidth / 2.0f, screenHeight / 2.0f };
    CamMode mode = CamMode::GUNNER;
    bool gameOver = false;
    Camera3D camera = {};
    camera.position = Vector3{ 0.0f, 10.0f, 10.0f };
    camera.target = Vector3{ 0.0f, 0.0f, 0.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    bool tabWasDown = false;
    int frameCount = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Mouse ground point (drone mode), for the cursor + order raycast.
        Vector3 mouseGround = { 0, 0, 0 };
        bool mouseGroundValid = false;

        int aliveNow = 0;
        for (const auto &e : enemies) if (e.alive) ++aliveNow;
        if ((gameOver || aliveNow == 0) && IsKeyPressed(KEY_R)) {
            // Restart: fresh village, enemies, allies, player.
            tank = Tank{};
            tank.hp = tank.maxHp = cfg.playerHits;
            village = BuildVillage(cfg.buildingHits);
            enemies = SpawnEnemies(cfg.enemyCount, cfg.enemyHits, village);
            allies = SpawnAllies(cfg.allyCount, cfg.allyHits, tank.pos);
            selectedAlly = 0;
            shells.clear(); flashes.clear(); particles.clear(); pings.clear();
            fireCooldown = 0.0f;
            mode = CamMode::GUNNER;
            gameOver = false;
        }

        if (!gameOver) {

        // Read the mouse once per frame. Deltas from the first few frames are
        // discarded: hiding/capturing the cursor can warp the pointer and
        // inject one bogus jump (it would otherwise yaw the gun on launch).
        Vector2 md = GetMouseDelta();
        if (frameCount++ < 5) md = Vector2{ 0.0f, 0.0f };

        // Camera mode toggle (instant key; HUD button arrives in v0.3).
        bool tabDown = IsKeyDown(KEY_TAB);
        if (tabDown && !tabWasDown) {
            mode = (mode == CamMode::GUNNER) ? CamMode::DRONE : CamMode::GUNNER;
            if (mode == CamMode::DRONE) {
                // Start the drone just above and behind the tank, looking
                // the way the tank faces (toward the village).
                drone.pos = Vector3{ tank.pos.x, 35.0f, tank.pos.z + 25.0f };
                drone.yaw = tank.hullAngle;
                drone.pitch = -0.6f;
                // Drone mode needs a visible cursor for click-to-order;
                // gunner mode uses relative mouse-look.
                EnableCursor();
            } else {
                DisableCursor();
            }
        }
        tabWasDown = tabDown;

        // Ally orders (drone mode only): 1..N select, right-click ground =
        // move, right-click enemy = attack, F = follow, H = hold.
        // (Mouse ground point + right-click are handled after the camera
        // update below, so the ray uses the current frame's camera.)
        if (mode == CamMode::DRONE && !allies.empty()) {
            for (size_t i = 0; i < allies.size() && i < 9; ++i) {
                if (IsKeyPressed(KEY_ONE + (int)i)) selectedAlly = (int)i;
            }
            if (selectedAlly >= (int)allies.size()) selectedAlly = 0;
            Ally &sel = allies[selectedAlly];
            if (IsKeyPressed(KEY_F) && sel.alive) {
                sel.order = AllyOrder::FOLLOW;
                sel.targetEnemy = -1;
            }
            if (IsKeyPressed(KEY_H) && sel.alive) {
                sel.order = AllyOrder::HOLD;
                sel.targetEnemy = -1;
            }
        }

        UpdateTank(tank, village, dt, mode == CamMode::GUNNER, cfg.playerSpeed);
        ResolveWreckCollisions(tank.pos, enemies, cfg.wreckBlocks);

        fireCooldown -= dt;

        // Firing: left mouse or Space, gated by cooldown. Works in both modes —
        // in drone mode the turret fires along its current aim, so you can
        // watch the shells from outside.
        if ((IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsKeyDown(KEY_SPACE)) &&
            fireCooldown <= 0.0f) {
            FireShell(tank, shells, cfg);
            fireCooldown = cfg.shellCooldown;
            Vector3 muzzle = MuzzleWorldPos(tank);
            // Compact bright burst at the muzzle tip (not a beach ball).
            flashes.push_back(Flash{ muzzle, 0.22f, 0.22f, 0.9f });
        }

        // Shells: fly straight (flat trajectory for v0.2; parabolic + ammo
        // types come later), expire on lifetime, damage buildings on impact.
        for (auto it = shells.begin(); it != shells.end();) {
            it->pos = Vector3Add(it->pos, Vector3Scale(it->vel, dt));
            it->life -= dt;
            // Record trail: oldest at trail[0], newest at trail[trailCount-1].
            if (it->trailCount < Shell::TRAIL) {
                it->trail[it->trailCount++] = it->pos;
            } else {
                for (int i = 0; i < Shell::TRAIL - 1; ++i) it->trail[i] = it->trail[i + 1];
                it->trail[Shell::TRAIL - 1] = it->pos;
            }
            bool dead = it->life <= 0.0f;
            if (!dead) {
                for (auto &b : village) {
                    if (b.destroyed) continue;
                    if (ShellHitsBuilding(it->pos, cfg.shellRadius, b)) {
                        DamageBuilding(b, it->pos);
                        flashes.push_back(Flash{ it->pos, 0.25f, 0.25f, 2.0f });
                        dead = true;
                        break;
                    }
                }
            }
            if (!dead) {
                if (it->fromEnemy) {
                    // Hostile shell: hits the player and allies.
                    float pdx = it->pos.x - tank.pos.x, pdz = it->pos.z - tank.pos.z;
                    float prr = 2.2f + cfg.shellRadius;
                    if (pdx * pdx + pdz * pdz < prr * prr && it->pos.y > 0.0f && it->pos.y < 3.2f) {
                        tank.hp--;
                        tank.hitFlashT = 0.4f;
                        Vector3 hc = { tank.pos.x, 1.6f, tank.pos.z };
                        flashes.push_back(Flash{ hc, 0.4f, 0.4f, 2.5f });
                        Burst(particles, hc, 8, Color{ 255, 150, 40, 255 }, 7.0f, 6.0f, 0.7f, 0.6f, 6.0f);
                        if (tank.hp <= 0) gameOver = true;
                        dead = true;
                    }
                    if (!dead) {
                        for (auto &a : allies) {
                            float adx = it->pos.x - a.pos.x, adz = it->pos.z - a.pos.z;
                            if (adx * adx + adz * adz < prr * prr && it->pos.y > 0.0f && it->pos.y < 3.2f) {
                                DamageAlly(a, particles, flashes);
                                flashes.push_back(Flash{ it->pos, 0.25f, 0.25f, 2.0f });
                                dead = true;
                                break;
                            }
                        }
                    }
                } else {
                    for (auto &e : enemies) {
                        if (ShellHitsEnemy(it->pos, cfg.shellRadius, e)) {
                            DamageEnemy(e, particles, flashes, tank.pos, village);
                            flashes.push_back(Flash{ it->pos, 0.25f, 0.25f, 2.0f });
                            dead = true;
                            break;
                        }
                    }
                }
            }
            it = dead ? shells.erase(it) : std::next(it);
        }

        // Enemy AI (advance / shoot / seek-cover) + death animations.
        for (auto &e : enemies)
            UpdateEnemyAI(e, tank, village, shells, flashes, cfg, dt);
        UpdateEnemies(enemies, particles, dt);
        // Ally AI (orders + engage) + death animations.
        UpdateAllies(allies, tank, enemies, village, shells, flashes, particles, cfg, dt);
        // Wrecks block enemies too; enemies keep separation from each other.
        for (auto &e : enemies) {
            if (!e.alive) continue;
            ResolveWreckCollisions(e.pos, enemies, cfg.wreckBlocks);
            for (auto &o : enemies) {
                if (&o == &e || !o.alive) continue;
                float sx = e.pos.x - o.pos.x, sz = e.pos.z - o.pos.z;
                float d2 = sx * sx + sz * sz;
                if (d2 < 25.0f && d2 > 1e-4f) {
                    float d = sqrtf(d2);
                    e.pos.x = o.pos.x + sx / d * 5.0f;
                    e.pos.z = o.pos.z + sz / d * 5.0f;
                }
            }
        }
        // Ally wrecks block the player as well.
        for (const auto &a : allies) {
            if (a.alive || !cfg.wreckBlocks) continue;
            float dx = tank.pos.x - a.pos.x, dz = tank.pos.z - a.pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 4.0f && d2 > 1e-4f) {
                float d = sqrtf(d2);
                tank.pos.x = a.pos.x + dx / d * 2.0f;
                tank.pos.z = a.pos.z + dz / d * 2.0f;
            }
        }

        // Collapse animation progress; impact/muzzle flashes decay.
        for (auto &b : village)
            if (b.destroyed && b.collapseT < 1.0f)
                b.collapseT = fminf(1.0f, b.collapseT + dt / cfg.collapseDuration);
        for (auto it = flashes.begin(); it != flashes.end();) {
            it->t -= dt;
            it = (it->t <= 0.0f) ? flashes.erase(it) : std::next(it);
        }
        for (auto it = pings.begin(); it != pings.end();) {
            it->t -= dt;
            it = (it->t <= 0.0f) ? pings.erase(it) : std::next(it);
        }

        if (mode == CamMode::GUNNER) {
            // Stabilized gunner sight: the mouse sets a WORLD-space aim
            // direction, so turning the hull (A/D) swings the hull visibly
            // beneath a steady sight instead of dragging the camera along.
            // (This also fixes mouse X, which was inverted before.)
            tank.aimAngle += md.x * TURRET_SENS;
            // Full 360 traverse, no stops: the turret follows the aim.
            tank.turretAngle = NormalizeAngle(tank.aimAngle - tank.hullAngle);

            Vector3 tp = TurretWorldPos(tank);
            Vector3 af = { sinf(tank.aimAngle), 0.0f, -cosf(tank.aimAngle) };
            // Gunner "in the turret": the camera sits at the turret ring near
            // the gun's height and looks along the stabilized aim. The turret
            // itself renders as a translucent ghost (DrawTurretGhost, after the
            // village) so the hull and the world stay visible through it — you
            // always know which way the tank is turned.
            camera.position = Vector3{ tp.x, tp.y + 0.65f, tp.z };
            camera.target   = Vector3{ tp.x + af.x * 60.0f, tp.y + 0.65f, tp.z + af.z * 60.0f };
        } else {
            UpdateDrone(drone, dt);
            Vector3 fwd = { sinf(drone.yaw) * cosf(drone.pitch), sinf(drone.pitch),
                            -cosf(drone.yaw) * cosf(drone.pitch) };
            camera.position = drone.pos;
            camera.target = Vector3Add(drone.pos, fwd);
        }

        // Mouse ground point + right-click orders, using the current camera.
        // The ray must hit the ground in front of the camera within a sane
        // distance; otherwise the cursor hides and clicks are ignored.
        // Drone mode uses a virtual cursor (system cursor is disabled for
        // gunner mouse-look), driven by mouse deltas and clamped to screen.
        if (mode == CamMode::DRONE) {
            Vector2 mdv = GetMouseDelta();
            droneCursor.x = Clamp(droneCursor.x + mdv.x, 0.0f, (float)screenWidth);
            droneCursor.y = Clamp(droneCursor.y + mdv.y, 0.0f, (float)screenHeight);
            Ray ray = GetScreenToWorldRay(droneCursor, camera);
            if (fabsf(ray.direction.y) > 1e-4f) {
                float t = -ray.position.y / ray.direction.y;
                if (t > 0.0f && t < 250.0f) {
                    Vector3 gp = { ray.position.x + ray.direction.x * t, 0.0f,
                                   ray.position.z + ray.direction.z * t };
                    Vector3 cfwd = Vector3Normalize(
                        Vector3Subtract(camera.target, camera.position));
                    Vector3 toGp = Vector3Subtract(gp, camera.position);
                    if (Vector3DotProduct(toGp, cfwd) > 0.0f) {
                        gp.x = Clamp(gp.x, -ARENA_HALF + 5.0f, ARENA_HALF - 5.0f);
                        gp.z = Clamp(gp.z, -ARENA_HALF + 5.0f, ARENA_HALF - 5.0f);
                        mouseGround = gp;
                        mouseGroundValid = true;
                    }
                }
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && mouseGroundValid &&
                !allies.empty() && selectedAlly < (int)allies.size()) {
                Ally &sel = allies[selectedAlly];
                if (sel.alive) {
                    Vector3 gp = mouseGround;
                    int hitEnemy = -1;
                    for (size_t ei = 0; ei < enemies.size(); ++ei) {
                        if (!enemies[ei].alive) continue;
                        float dx = gp.x - enemies[ei].pos.x, dz = gp.z - enemies[ei].pos.z;
                        if (dx * dx + dz * dz < 16.0f) { hitEnemy = (int)ei; break; }
                    }
                    if (hitEnemy >= 0) {
                        sel.order = AllyOrder::ATTACK;
                        sel.targetEnemy = hitEnemy;
                        pings.push_back(OrderPing{ enemies[hitEnemy].pos, 0.6f, 0.6f,
                                                  Color{ 255, 80, 80, 255 } });
                    } else {
                        sel.order = AllyOrder::MOVE;
                        sel.orderPos = gp;
                        sel.targetEnemy = -1;
                        pings.push_back(OrderPing{ gp, 0.6f, 0.6f,
                                                  Color{ 80, 160, 255, 255 } });
                    }
                }
            }
        }
        }  // end if (!gameOver)

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // Ground: dusty plain with a street grid feel.
        DrawPlane(Vector3{ 0, 0, 0 }, Vector2{ ARENA_HALF * 2 + 40, ARENA_HALF * 2 + 40 },
                  Color{ 168, 148, 118, 255 });
        DrawGrid(40, 20.0f);
        DrawVillage(village);
        DrawTank(tank, mode == CamMode::GUNNER);
        DrawEnemies(enemies, cfg);
        DrawAllies(allies, enemies, cfg, selectedAlly);
        // Drone-mode mouse cursor: ring + crosshair on the ground.
        if (mode == CamMode::DRONE && mouseGroundValid) {
            DrawCylinderWires(Vector3{ mouseGround.x, 0.08f, mouseGround.z },
                              1.0f, 1.0f, 0.1f, 20, Color{ 255, 255, 255, 200 });
            DrawLine3D(Vector3{ mouseGround.x - 1.8f, 0.08f, mouseGround.z },
                       Vector3{ mouseGround.x + 1.8f, 0.08f, mouseGround.z },
                       Color{ 255, 255, 255, 200 });
            DrawLine3D(Vector3{ mouseGround.x, 0.08f, mouseGround.z - 1.8f },
                       Vector3{ mouseGround.x, 0.08f, mouseGround.z + 1.8f },
                       Color{ 255, 255, 255, 200 });
        }
        // Order pings: expanding rings where orders were issued.
        for (const auto &p : pings) {
            float k = 1.0f - p.t / p.maxT;  // 0 -> 1
            float r = 1.0f + k * 4.0f;
            Color c = p.color;
            c.a = (unsigned char)(255 * (1.0f - k));
            DrawCylinderWires(Vector3{ p.pos.x, 0.1f, p.pos.z }, r, r, 0.15f, 24, c);
        }
        // Shells and flashes BEFORE the ghost turret: the ghost is translucent
        // but writes depth, so anything drawn after it (and behind its far
        // wall) would be occluded. Opaque first, translucent ghost last.
        for (const auto &s : shells) {
            if (s.trailCount >= 2) {
                DrawCylinderEx(s.trail[0], s.pos, 0.22f, 0.22f, 8,
                               Color{ 255, 175, 65, 255 });
                // Hot core: thinner, brighter, full length.
                DrawCylinderEx(s.trail[0], s.pos, 0.1f, 0.1f, 6,
                               Color{ 255, 230, 150, 255 });
            }
            DrawSphere(s.pos, 0.35f, Color{ 255, 240, 180, 255 });
        }
        for (const auto &f : flashes) {
            float a = f.t / f.maxT;
            DrawSphere(f.pos, f.size * (0.5f + 0.5f * a), Color{ 255, 180, 60, (unsigned char)(255 * a) });
        }
        // Particles: fading spheres (fire, smoke, sparks).
        for (const auto &p : particles) {
            float a = fmaxf(p.life / p.maxLife, 0.0f);
            DrawSphere(p.pos, p.size * (0.4f + 0.6f * a),
                       Color{ p.color.r, p.color.g, p.color.b, (unsigned char)(255 * a) });
        }
        // The ghost turret blends over the world, so it goes last.
        if (mode == CamMode::GUNNER) DrawTurretGhost(tank);
        EndMode3D();

        // HUD
        const char *modeName = (mode == CamMode::GUNNER) ? "GUNNER" : "DRONE";
        DrawText(TextFormat("TANKSHOOTER v0.6  [%s]  TAB to switch", modeName), 16, 12, 22, DARKGRAY);
        DrawText("W/S drive  A/D turn hull  Mouse aim  Click/Space fire (both modes)  Arrows = WASD  ESC quit", 16, 40, 18, GRAY);
        if (mode == CamMode::DRONE) {
            // Virtual cursor crosshair.
            float cx = droneCursor.x, cy = droneCursor.y;
            DrawLine((int)cx - 12, (int)cy, (int)cx + 12, (int)cy, WHITE);
            DrawLine((int)cx, (int)cy - 12, (int)cx, (int)cy + 12, WHITE);
            DrawCircleLines((int)cx, (int)cy, 6.0f, WHITE);
        }
        int aliveCount = 0;
        for (const auto &e : enemies) if (e.alive) ++aliveCount;
        DrawText(TextFormat("Enemies left: %d", aliveCount), 16, 64, 20, RED);
        DrawText(TextFormat("Hull: %d/%d", tank.hp, tank.maxHp), 16, 90, 20,
                 tank.hp > 1 ? DARKGREEN : RED);
        // Ally status: order + HP for each.
        for (size_t i = 0; i < allies.size(); ++i) {
            const auto &a = allies[i];
            const char *on = "?";
            if (!a.alive) on = "DEAD";
            else if (a.order == AllyOrder::FOLLOW) on = "FOLLOW";
            else if (a.order == AllyOrder::MOVE) on = "MOVE";
            else if (a.order == AllyOrder::HOLD) on = "HOLD";
            else if (a.order == AllyOrder::ATTACK) on = "ATTACK";
            Color c = (i == (size_t)selectedAlly) ? WHITE : LIGHTGRAY;
            if (!a.alive) c = RED;
            DrawText(TextFormat("Ally %d [%s] %d/%d", (int)i + 1, on, a.hp, a.maxHp),
                     16, 116 + (int)i * 22, 18, c);
        }
        if (mode == CamMode::DRONE) {
            int y0 = 116 + (int)allies.size() * 22 + 6;
            DrawText("1/2 select ally   Right-click: move / attack   F follow   H hold", 16, y0, 18, DARKBLUE);
            DrawText("Arrows rotate view   WASD/QE move drone", 16, y0 + 22, 18, DARKBLUE);
        }
        // Tracking ping: any enemy with an active lock?
        bool tracked = false;
        for (const auto &e : enemies)
            if (e.alive && e.aiState == AIState::SHOOT && e.aimTimer > 0.05f) { tracked = true; break; }
        if (tracked && !gameOver && (frameCount / 20) % 2 == 0) {
            DrawText("!! TRACKED !!", screenWidth / 2 - 90, 70, 28, RED);
        }
        // Red edge flash on player hit.
        if (tank.hitFlashT > 0.0f) {
            float a = fminf(tank.hitFlashT / 0.4f, 1.0f);
            DrawRectangle(0, 0, screenWidth, 10, Color{ 255, 0, 0, (unsigned char)(a * 200) });
            DrawRectangle(0, screenHeight - 10, screenWidth, 10, Color{ 255, 0, 0, (unsigned char)(a * 200) });
            DrawRectangle(0, 0, 10, screenHeight, Color{ 255, 0, 0, (unsigned char)(a * 200) });
            DrawRectangle(screenWidth - 10, 0, 10, screenHeight, Color{ 255, 0, 0, (unsigned char)(a * 200) });
        }
        if (gameOver) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Color{ 0, 0, 0, 150 });
            DrawText("YOU DIED", screenWidth / 2 - 110, screenHeight / 2 - 40, 48, RED);
            DrawText("Press R to restart", screenWidth / 2 - 110, screenHeight / 2 + 20, 24, WHITE);
        } else if (aliveCount == 0) {
            DrawText("Arena Cleared!", screenWidth / 2 - 110, 110, 32, DARKGREEN);
            DrawText("Press R to restart", screenWidth / 2 - 110, 150, 22, DARKGRAY);
        }
        if (mode == CamMode::GUNNER) {
            // Crosshair
            int cx = screenWidth / 2, cy = screenHeight / 2;
            DrawLine(cx - 12, cy, cx + 12, cy, RED);
            DrawLine(cx, cy - 12, cx, cy + 12, RED);

            // Turret compass (bottom-right, top-down, north = up):
            // green bar = hull direction, yellow needle = gun direction.
            // Makes both A/D hull turns and mouse turret aim readable even
            // though the 3D view itself is locked to the gun sight.
            Vector2 compC = { (float)screenWidth - 80.0f, (float)screenHeight - 80.0f };
            DrawCircleV(compC, 38, Color{ 0, 0, 0, 90 });
            DrawCircleLines((int)compC.x, (int)compC.y, 38, DARKGRAY);
            Vector2 hf = { sinf(tank.hullAngle), -cosf(tank.hullAngle) };
            DrawLineEx(Vector2Subtract(compC, Vector2Scale(hf, 20.0f)),
                       Vector2Add(compC, Vector2Scale(hf, 20.0f)), 22.0f,
                       Color{ 74, 94, 62, 255 });
            Vector2 an = { sinf(tank.aimAngle), -cosf(tank.aimAngle) };
            DrawLineEx(compC, Vector2Add(compC, Vector2Scale(an, 33.0f)), 4.0f, YELLOW);
            DrawCircleV(compC, 4, YELLOW);
            DrawText("TURRET", (int)compC.x - 26, (int)compC.y + 44, 14, DARKGRAY);
        } else {
            DrawText("Drone: WASD/arrows fly  Q/E down/up  Shift boost", 16, 62, 18, GRAY);
        }
        DrawFPS(screenWidth - 90, 12);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
