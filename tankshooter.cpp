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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
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
    // Fog of war (hides enemies outside line of sight)
    bool  fogEnabled       = true;
    float fogSightRadius   = 90.0f;  // revealers see this far (with LOS)
    float fogEdgeWidth     = 14.0f;  // soft reveal band around the sight radius
    float fogPuffAlpha     = 0.32f;  // per-puff alpha of the visible fog banks
    float fogPuffSize      = 22.0f;  // world-unit diameter of one fog puff
    bool  replayEnabled    = true;   // defeat replays the last seconds
    float replayDuration   = 10.0f;  // seconds of replay footage
    float replayPostSeconds = 4.0f;  // aftermath included in the victory replay
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
        auto mm = j.value("minimap", nlohmann::json::object());
        c.minimapEnabled = mm.value("enabled", c.minimapEnabled);
        c.minimapSize     = mm.value("size", c.minimapSize);
        c.minimapHeight   = mm.value("height", c.minimapHeight);
        auto cv = j.value("chaseView", nlohmann::json::object());
        c.chaseEnabled   = cv.value("enabled", c.chaseEnabled);
        c.chaseWidth     = cv.value("width", c.chaseWidth);
        c.chaseHeight    = cv.value("height", c.chaseHeight);
        c.chaseCamHeight = cv.value("camHeight", c.chaseCamHeight);
        c.chaseCamDist   = cv.value("camDistance", c.chaseCamDist);
        auto fg = j.value("fog", nlohmann::json::object());
        c.fogEnabled     = fg.value("enabled", c.fogEnabled);
        c.fogSightRadius = fg.value("sightRadius", c.fogSightRadius);
        c.fogEdgeWidth   = fg.value("edgeWidth", c.fogEdgeWidth);
        c.fogPuffAlpha   = fg.value("puffAlpha", c.fogPuffAlpha);
        c.fogPuffSize    = fg.value("puffSize", c.fogPuffSize);
        auto rp = j.value("replay", nlohmann::json::object());
        c.replayEnabled  = rp.value("enabled", c.replayEnabled);
        c.replayDuration = rp.value("durationSeconds", c.replayDuration);
        c.replayPostSeconds = rp.value("postVictorySeconds", c.replayPostSeconds);
        auto dt2 = j.value("ditch", nlohmann::json::object());
        c.ditchSlowFactor = dt2.value("slowFactor", c.ditchSlowFactor);
        c.ditchDepth      = dt2.value("depth", c.ditchDepth);
        auto tw = j.value("tower", nlohmann::json::object());
        c.towerHits    = tw.value("hitsToDestroy", c.towerHits);
        c.towerRange   = tw.value("shootRange", c.towerRange);
        c.towerFireInt = tw.value("fireInterval", c.towerFireInt);
        c.towerAimTime = tw.value("aimTime", c.towerAimTime);
        auto mp = j.value("map", nlohmann::json::object());
        std::string md = mp.value("default", std::string(c.mapDefault));
        // MSVC wants strncpy_s; everywhere else uses strncpy. Either way,
        // always null-terminate.
#ifdef _MSC_VER
        strncpy_s(c.mapDefault, md.c_str(), sizeof(c.mapDefault) - 1);
#else
        strncpy(c.mapDefault, md.c_str(), sizeof(c.mapDefault) - 1);
#endif
        c.mapDefault[sizeof(c.mapDefault) - 1] = '\0';
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

// ---------------------------------------------------------------------------
// Terrain: ditches, bridges, turret towers, and hand-authored maps
// ---------------------------------------------------------------------------
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
struct GameMap {
    std::string name;
    int villageSeed = 1337;
    std::vector<Ditch> ditches;
    std::vector<Bridge> bridges;
    std::vector<Vector3> towerPos;  // spawned into Tower with config HP
};

// Depth of ditch under (x,z), or 0. Bridges cancel the ditch beneath them.
static float DitchDepthAt(float x, float z, const std::vector<Ditch> &ditches,
                          const std::vector<Bridge> &bridges) {
    for (const auto &b : bridges) {
        if (fabsf(x - b.center.x) <= b.hx && fabsf(z - b.center.z) <= b.hz)
            return 0.0f;
    }
    for (const auto &d : ditches) {
        if (fabsf(x - d.center.x) <= d.hx && fabsf(z - d.center.z) <= d.hz)
            return d.depth;
    }
    return 0.0f;
}

static bool PointInTower(float x, float z, const std::vector<Tower> &towers, float pad) {
    for (const auto &t : towers) {
        if (!t.alive) continue;
        float dx = x - t.pos.x, dz = z - t.pos.z;
        if (dx * dx + dz * dz < (3.5f + pad) * (3.5f + pad)) return true;
    }
    return false;
}

// Hand-authored maps. Add entries here to author by hand; the village itself
// stays procedural per map seed.
static std::vector<GameMap> BuildMaps(float ditchDepth) {
    std::vector<GameMap> maps;

    GameMap village;
    village.name = "Village";
    village.villageSeed = 1337;
    maps.push_back(village);

    GameMap cross;
    cross.name = "Crossroads";
    cross.villageSeed = 7331;
    cross.ditches.push_back(Ditch{ Vector3{ 0, 0, 0 }, 190.0f, 12.0f, ditchDepth });
    cross.bridges.push_back(Bridge{ Vector3{ 0, 0, 0 }, 8.0f, 18.0f });
    cross.towerPos.push_back(Vector3{ 28, 0, 30 });
    cross.towerPos.push_back(Vector3{ -28, 0, -30 });
    maps.push_back(cross);

    GameMap outpost;
    outpost.name = "Outpost";
    outpost.villageSeed = 9773;
    outpost.ditches.push_back(Ditch{ Vector3{ -60, 0, -60 }, 40.0f, 14.0f, ditchDepth });
    outpost.ditches.push_back(Ditch{ Vector3{ 70, 0, 60 }, 35.0f, 14.0f, ditchDepth });
    outpost.towerPos.push_back(Vector3{ 0, 0, -70 });
    outpost.towerPos.push_back(Vector3{ -70, 0, 40 });
    maps.push_back(outpost);

    return maps;
}

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
static std::vector<Building> BuildVillage(int hitsToDestroy, int seed) {
    std::vector<Building> out;
    SetRandomSeed(seed);

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

// Ground with holes cut for ditches (tiled so pits are real openings),
// plus ditch pit walls/floors.
static void DrawGround(const std::vector<Ditch> &ditches) {
    const float tile = 20.0f;
    Color gc = { 168, 148, 118, 255 };
    Color dirt = { 140, 118, 90, 255 };
    for (float x = -ARENA_HALF; x < ARENA_HALF; x += tile) {
        for (float z = -ARENA_HALF; z < ARENA_HALF; z += tile) {
            float cx = x + tile * 0.5f, cz = z + tile * 0.5f;
            bool inPit = false, inApron = false;
            for (const auto &d : ditches) {
                float ax = fabsf(cx - d.center.x), az = fabsf(cz - d.center.z);
                if (ax < d.hx && az < d.hz) { inPit = true; break; }
                if (ax < d.hx + tile * 0.5f && az < d.hz + tile * 0.5f) inApron = true;
            }
            if (inPit) continue;
            DrawPlane(Vector3{ cx, 0.0f, cz }, Vector2{ tile, tile },
                      inApron ? dirt : gc);
        }
    }
    // Ditch pits: floor + 4 walls.
    for (const auto &d : ditches) {
        Color wall = { 101, 76, 52, 255 };
        Color dark = { 50, 38, 26, 255 };
        DrawPlane(Vector3{ d.center.x, -d.depth, d.center.z },
                  Vector2{ d.hx * 2, d.hz * 2 }, dark);
        float wy = -d.depth * 0.5f;
        DrawCube(Vector3{ d.center.x, wy, d.center.z - d.hz }, d.hx * 2, d.depth, 0.6f, wall);
        DrawCube(Vector3{ d.center.x, wy, d.center.z + d.hz }, d.hx * 2, d.depth, 0.6f, wall);
        DrawCube(Vector3{ d.center.x - d.hx, wy, d.center.z }, 0.6f, d.depth, d.hz * 2, wall);
        DrawCube(Vector3{ d.center.x + d.hx, wy, d.center.z }, 0.6f, d.depth, d.hz * 2, wall);
    }
}

static void DrawBridges(const std::vector<Bridge> &bridges) {
    for (const auto &b : bridges) {
        Color wood = { 139, 110, 70, 255 };
        DrawCube(Vector3{ b.center.x, 0.0f, b.center.z }, b.hx * 2, 0.5f, b.hz * 2, wood);
        // Rails.
        DrawCube(Vector3{ b.center.x - b.hx + 0.3f, 1.0f, b.center.z }, 0.3f, 1.0f, b.hz * 2,
                 Color{ 100, 78, 50, 255 });
        DrawCube(Vector3{ b.center.x + b.hx - 0.3f, 1.0f, b.center.z }, 0.3f, 1.0f, b.hz * 2,
                 Color{ 100, 78, 50, 255 });
    }
}

static void DrawTowers(const std::vector<Tower> &towers, int frameCount) {
    for (const auto &t : towers) {
        if (!t.alive) {
            // Rubble.
            DrawCylinder(Vector3{ t.pos.x, 0.5f, t.pos.z }, 3.2f, 3.8f, 1.0f, 8,
                         Color{ 60, 58, 55, 255 });
            continue;
        }
        Color concrete = t.hitFlashT > 0.0f ? WHITE : Color{ 130, 130, 135, 255 };
        Color dark = t.hitFlashT > 0.0f ? WHITE : Color{ 70, 70, 78, 255 };
        DrawCylinder(Vector3{ t.pos.x, 0.0f, t.pos.z }, 3.0f, 3.6f, 4.0f, 10, concrete);
        DrawCylinder(Vector3{ t.pos.x, 4.0f, t.pos.z }, 2.2f, 2.6f, 1.0f, 10, dark);
        rlPushMatrix();
        rlTranslatef(t.pos.x, 5.2f, t.pos.z);
        rlRotatef(t.turretAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
        DrawCube(Vector3{ 0, 0, 0 }, 2.6f, 1.2f, 3.4f, dark);
        DrawCube(Vector3{ 0, 0.1f, -2.8f }, 0.45f, 0.45f, 3.6f, dark);
        rlPopMatrix();
        // Blinking red warning light.
        if ((frameCount / 30) % 2 == 0)
            DrawSphere(Vector3{ t.pos.x, 6.4f, t.pos.z }, 0.45f, RED);
        // HP pips.
        for (int i = 0; i < t.maxHp; ++i)
            DrawCube(Vector3{ t.pos.x - 2.0f + i * 1.2f, 7.2f, t.pos.z }, 0.9f, 0.9f, 0.9f,
                     i < t.hp ? GREEN : DARKGRAY);
    }
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

static void UpdateTank(Tank &t, const std::vector<Building> &village,
                       const std::vector<Ditch> &ditches, const std::vector<Bridge> &bridges,
                       float dt, bool allowDrive, float maxSpeed, float ditchSlow) {
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

    // Ditches sap drive power.
    float dm = DitchDepthAt(t.pos.x, t.pos.z, ditches, bridges) > 0.0f ? ditchSlow : 1.0f;
    float effMax = maxSpeed * dm;
    if (throttle > 0.0f)      t.speed = fminf(t.speed + ACCEL * dt, effMax);
    else if (throttle < 0.0f) t.speed = fmaxf(t.speed - ACCEL * dt, MAX_REVERSE * dm);
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
static void DrawTankHull(const Vector3 &pos, float hullAngle, Color armor, float alpha = 1.0f) {
    unsigned char a = (unsigned char)(255.0f * fmaxf(0.0f, fminf(1.0f, alpha)));
    Color body = armor; body.a = a;
    Color deck = { (unsigned char)fminf(armor.r * 1.14f, 255.0f),
                   (unsigned char)fminf(armor.g * 1.14f, 255.0f),
                   (unsigned char)fminf(armor.b * 1.14f, 255.0f), a };
    Color treadC = { 45, 48, 44, a };
    Color lightC = { 255, 240, 200, a };
    Color drumC  = { 130, 75, 45, a };
    Vector3 hullC = { pos.x, 0.75f, pos.z };

    rlPushMatrix();
    rlTranslatef(hullC.x, hullC.y, hullC.z);
    // NOTE: negated — rlRotatef(+a) about Y turns local -Z toward -X, but our
    // angle convention faces (sin a, 0, -cos a), i.e. toward +X for a > 0.
    rlRotatef(-hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawCube(Vector3{ 0, 0, 0 }, 3.2f, 1.1f, 4.6f, body);                            // hull
    DrawCube(Vector3{ 0, 0.75f, -0.4f }, 2.4f, 0.5f, 2.6f, deck);                   // upper deck
    DrawCube(Vector3{ -1.85f, -0.15f, 0 }, 0.7f, 0.9f, 4.8f, treadC);              // treads
    DrawCube(Vector3{ 1.85f, -0.15f, 0 }, 0.7f, 0.9f, 4.8f, treadC);
    // Forward/back cues so the gunner can read hull direction at a glance:
    // headlights on the nose, and two external
    // fuel drums on the rear deck (the back of the tank, Soviet-style).
    DrawCube(Vector3{ -0.9f, 0.6f, -2.33f }, 0.25f, 0.2f, 0.1f, lightC); // headlight L
    DrawCube(Vector3{  0.9f, 0.6f, -2.33f }, 0.25f, 0.2f, 0.1f, lightC); // headlight R
    for (float dx : { -0.7f, 0.7f }) {
        rlPushMatrix();
        rlTranslatef(dx, 0.83f, 1.6f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        DrawCylinder(Vector3{ 0, 0, 0 }, 0.28f, 0.28f, 0.9f, 10, drumC); // fuel drum
        rlPopMatrix();
    }
    rlPopMatrix();
}

// Barrel only. In gunner view the turret body is a translucent ghost but the
// gun itself stays solid — the gunner needs to see where it's pointing.
static void DrawTankBarrel(const Vector3 &center, float totalAngle, float alpha = 1.0f) {
    unsigned char a = (unsigned char)(255.0f * fmaxf(0.0f, fminf(1.0f, alpha)));
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // Barrel: a cylinder laid along -Z (forward), breech at the turret wall.
    // After rotating -90 deg about X the +Y height axis points down -Z and
    // the barrel spans z -1.3 to -3.3 from the turret center.
    rlPushMatrix();
    rlTranslatef(0.0f, 0.0f, -1.3f);
    rlRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    DrawCylinder(Vector3{ 0, 0, 0 }, 0.15f, 0.15f, 2.0f, 12, Color{ 50, 52, 48, a });
    rlPopMatrix();
    rlPopMatrix();
}

// Turret mesh (cylinder + barrel) centered at `center` (world), rotated by
// totalAngle = hullAngle + turretAngle. Used attached for player/enemies,
// and detached (with a spin) for a popped wreck turret.
static void DrawTankTurret(const Vector3 &center, float totalAngle, Color armor,
                           float alpha = 1.0f) {
    unsigned char a = (unsigned char)(255.0f * fmaxf(0.0f, fminf(1.0f, alpha)));
    Color body = armor; body.a = a;
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // NOTE: DrawCylinder's position is its BASE: base at -0.35 puts the
    // 0.7-tall cylinder spanning y -0.35..+0.35 around the center.
    DrawCylinder(Vector3{ 0, -0.35f, 0 }, 1.15f, 1.35f, 0.7f, 12, body);
    rlPopMatrix();
    DrawTankBarrel(center, totalAngle, alpha);
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

// Game phase: SETUP (position forces, enemies hidden) -> COMBAT.
enum class Phase { SETUP, COMBAT };

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

// Place the drone just above and behind the tank, looking the way the
// tank faces (toward the village). Used at startup, on TAB, and on R.
static void ResetDroneView(DroneCam &d, const Tank &tank) {
    d.pos = Vector3{ tank.pos.x, 35.0f, tank.pos.z + 25.0f };
    d.yaw = tank.hullAngle;
    d.pitch = -0.6f;
}

// ---------------------------------------------------------------------------
// Shooting (v0.2): shells, muzzle flash, hit detection, destruction
// ---------------------------------------------------------------------------
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

// Expanding ring ping shown where an order was issued (drone mode).
struct OrderPing {
    Vector3 pos;
    float t;
    float maxT;
    Color color;
};

// Mechanical alien invader variants. Enemies are walkers now (red tanks
// are retired); the AI underneath is unchanged.
enum class WalkerKind { TRIPOD, CRAB, BIPED };

// Death replay: ring buffer of world snapshots (20 Hz, ~15 s) so the
// defeat screen can replay the last seconds from the drone's point of view.
// Only the visual state is recorded — positions, damage, death anims.
struct ReplayTankSnap {
    Vector3 pos; float hullAngle, turretAngle; bool alive;
    float deathT; Vector3 turretPos; float turretSpin;  // ally wrecks
};
struct ReplayEnemySnap {
    Vector3 pos; float hullAngle, turretAngle; bool alive;
    float deathT; Vector3 turretPos; float turretSpin;
    WalkerKind kind; float walkPhase; Vector3 fallAxis;
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
    WalkerKind kind = WalkerKind::TRIPOD;  // mechanical alien variant
    float walkPhase = 0.0f;   // leg animation phase, advanced by movement
    Vector3 fallAxis = { 1.0f, 0.0f, 0.0f };  // wreck collapse axis
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
    s.fromPlayer = true;
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

// Fog of war: how fogged is (x,z)? 0 = fully revealed, 1 = fully fogged.
// Each revealer (player, ally, drone) clears a soft disk: fully clear
// inside sightRadius - edge/2, fully fogged beyond sightRadius + edge/2,
// smoothstepped between. Intact buildings block a revealer's sight, so the
// area behind them stays fogged. The cheapest revealer (by distance past
// the radius) wins, so overlapping disks merge seamlessly.
static float FogFactorAt(float x, float z,
                         const std::vector<Vector3> &revealers,
                         const std::vector<Building> &village,
                         float radius, float edge) {
    float best = 1e18f;  // min over revealers of (dist - radius)
    for (const auto &r : revealers) {
        float dx = x - r.x, dz = z - r.z;
        float excess = sqrtf(dx * dx + dz * dz) - radius;
        if (excess >= best) continue;  // can't beat the current best; skip LOS
        Vector3 eye = { r.x, 2.25f, r.z };
        Vector3 tgt = { x, 2.0f, z };
        if (LosBlocked(eye, tgt, village)) continue;
        best = excess;
    }
    if (best > 1e17f) return 1.0f;  // no revealer has line of sight
    float t = (best + edge * 0.5f) / edge;
    t = fmaxf(0.0f, fminf(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
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
                          const std::vector<Ditch> &ditches, const std::vector<Bridge> &bridges,
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
        float dm = DitchDepthAt(e.pos.x, e.pos.z, ditches, bridges) > 0.0f
            ? cfg.ditchSlowFactor : 1.0f;
        e.pos.x += sinf(e.hullAngle) * speed * throttle * dt * dm;
        e.pos.z += -cosf(e.hullAngle) * speed * throttle * dt * dm;
        // Walkers animate their legs proportional to distance covered.
        e.walkPhase += speed * throttle * dt * dm * 1.1f;
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
                // Walkers fire from the ventral gun pod (low, in the hit band);
                // tanks fire from the turret.
                float muzzleY = (e.kind == WalkerKind::TRIPOD) ? 2.5f : 2.25f;
                float muzzleD = (e.kind == WalkerKind::TRIPOD) ? 3.4f : 3.55f;
                Vector3 muzzle = { e.pos.x + fwd.x * muzzleD, muzzleY, e.pos.z + fwd.z * muzzleD };
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
static std::vector<Enemy> SpawnEnemies(int count, int hits, const std::vector<Building> &village,
                                       const std::vector<Ditch> &ditches,
                                       const std::vector<Bridge> &bridges,
                                       const std::vector<Tower> &towers) {
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
        // Not in a ditch, not inside a tower.
        if (DitchDepthAt(x, z, ditches, bridges) > 0.0f) continue;
        if (PointInTower(x, z, towers, 4.0f)) continue;
        // Spaced from other enemies.
        for (const auto &e : out) {
            float ex = x - e.pos.x, ez = z - e.pos.z;
            if (ex * ex + ez * ez < 30.0f * 30.0f) { bad = true; break; }
        }
        if (bad) continue;
        Enemy e;
        e.pos = { x, 0.0f, z };
        e.kind = WalkerKind::TRIPOD;
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
                         const std::vector<Ditch> &ditches, const std::vector<Bridge> &bridges,
                         std::vector<Shell> &shells, std::vector<Flash> &flashes,
                         std::vector<Particle> &particles, const Config &cfg,
                         bool inCombat, float dt) {
    for (size_t ai = 0; ai < allies.size(); ++ai) {
        Ally &a = allies[ai];
        if (!a.alive) {
            UpdateWreckAnim(a, particles, dt);
            continue;
        }
        if (a.hitFlashT > 0.0f) a.hitFlashT -= dt;
        a.fireTimer += dt;

        // Pick a target: explicit ATTACK order, else nearest visible in range.
        // No engagement during SETUP (enemies are hidden).
        int tgt = -1;
        if (a.order == AllyOrder::ATTACK && a.targetEnemy >= 0 &&
            a.targetEnemy < (int)enemies.size() && enemies[a.targetEnemy].alive) {
            tgt = a.targetEnemy;
        } else if (inCombat && cfg.allyEngage) {
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
            float dm = DitchDepthAt(a.pos.x, a.pos.z, ditches, bridges) > 0.0f
                ? cfg.ditchSlowFactor : 1.0f;
            a.pos.x += sinf(a.hullAngle) * speed * throttle * dt * dm;
            a.pos.z += -cosf(a.hullAngle) * speed * throttle * dt * dm;
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
    // Kill: fireball flash, flame + smoke burst, head/turret pops off.
    e.alive = false;
    e.deathT = 0.0f;
    float burstY = (e.kind == WalkerKind::TRIPOD) ? 2.8f : 1.6f;
    Vector3 c = { e.pos.x, burstY, e.pos.z };
    flashes.push_back(Flash{ c, 0.55f, 0.55f, 3.8f });
    Burst(particles, c, 10, Color{ 255, 150, 40, 255 }, 9.0f, 7.0f, 0.9f, 0.7f, 6.0f);   // flames
    Burst(particles, c, 12, Color{ 90, 85, 80, 255 }, 4.0f, 9.0f, 1.4f, 2.6f, -3.0f);    // smoke
    Burst(particles, c, 6, Color{ 255, 220, 120, 255 }, 14.0f, 5.0f, 0.5f, 0.4f, 10.0f);  // sparks
    float headY = (e.kind == WalkerKind::TRIPOD) ? 4.5f : 2.25f;
    e.turretPos = { e.pos.x, headY, e.pos.z };
    // Walkers keel over around a random horizontal axis as they die.
    float fa = (float)GetRandomValue(0, 360) * DEG2RAD;
    e.fallAxis = { cosf(fa), 0.0f, sinf(fa) };
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

// Turret towers: static defenses hostile to the player side. Track the
// nearest of player/allies in range + LOS, aim, then fire.
static void UpdateTowers(std::vector<Tower> &towers, const Tank &player,
                         std::vector<Ally> &allies, const std::vector<Building> &village,
                         std::vector<Shell> &shells, std::vector<Flash> &flashes,
                         const Config &cfg, float dt, bool victory) {
    for (auto &t : towers) {
        if (!t.alive) continue;
        if (t.hitFlashT > 0.0f) t.hitFlashT -= dt;
        // Victory ceasefire: the battle is over, turrets stand down so the
        // post-battle tour isn't interrupted by impact flashes on the tank.
        if (victory) continue;
        t.fireTimer += dt;

        // Pick target: nearest of player / alive allies in range with LOS.
        float bestD2 = cfg.towerRange * cfg.towerRange;
        Vector3 teye = { t.pos.x, 5.5f, t.pos.z };
        bool tp = false; int ta = -1;
        Vector3 tgtPos = { 0, 0, 0 };
        if (player.hp > 0) {
            float dx = player.pos.x - t.pos.x, dz = player.pos.z - t.pos.z;
            float d2 = dx * dx + dz * dz;
            Vector3 pp = { player.pos.x, 2.0f, player.pos.z };
            if (d2 < bestD2 && !LosBlocked(teye, pp, village)) {
                bestD2 = d2; tp = true; tgtPos = pp;
            }
        }
        for (size_t i = 0; i < allies.size(); ++i) {
            if (!allies[i].alive) continue;
            float dx = allies[i].pos.x - t.pos.x, dz = allies[i].pos.z - t.pos.z;
            float d2 = dx * dx + dz * dz;
            Vector3 ap = { allies[i].pos.x, 2.0f, allies[i].pos.z };
            if (d2 < bestD2 && !LosBlocked(teye, ap, village)) {
                bestD2 = d2; tp = false; ta = (int)i; tgtPos = ap;
            }
        }

        if (bestD2 >= cfg.towerRange * cfg.towerRange) {
            t.targetPlayer = false; t.targetAlly = -1; t.aimTimer = 0.0f;
            continue;
        }
        t.targetPlayer = tp; t.targetAlly = ta;
        float want = atan2f(tgtPos.x - t.pos.x, -(tgtPos.z - t.pos.z));
        float diff = NormalizeAngle(want - t.turretAngle);
        t.turretAngle += Clamp(diff * 2.5f, -1.2f, 1.2f) * dt;
        if (fabsf(diff) < 0.08f) t.aimTimer += dt; else t.aimTimer = 0.0f;
        if (t.aimTimer >= cfg.towerAimTime && t.fireTimer >= cfg.towerFireInt) {
            // Fire.
            Vector3 fwd = { sinf(t.turretAngle), 0.0f, -cosf(t.turretAngle) };
            float spread = ((float)GetRandomValue(-100, 100) / 100.0f) * 0.05f;
            float fa = t.turretAngle + spread;
            fwd = { sinf(fa), 0.0f, -cosf(fa) };
            Vector3 muzzle = { t.pos.x + fwd.x * 3.0f, 5.2f, t.pos.z + fwd.z * 3.0f };
            Shell s;
            s.pos = muzzle;
            s.vel = Vector3Scale(fwd, cfg.shellSpeed);
            s.life = cfg.shellLifetime;
            s.fromEnemy = true;  // hits player + allies
            shells.push_back(s);
            flashes.push_back(Flash{ muzzle, 0.25f, 0.25f, 1.0f });
            t.fireTimer = 0.0f; t.aimTimer = 0.0f;
        }
    }
}

static bool ShellHitsTower(const Vector3 &p, float r, const Tower &t) {
    if (!t.alive) return false;
    float dx = p.x - t.pos.x, dz = p.z - t.pos.z;
    float rr = 3.6f + r;
    return dx * dx + dz * dz < rr * rr && p.y < 7.0f;
}

static void DamageTower(Tower &t, std::vector<Particle> &particles,
                        std::vector<Flash> &flashes) {
    if (!t.alive) return;
    t.hitFlashT = 0.18f;
    if (--t.hp > 0) return;
    t.alive = false;
    Vector3 c = { t.pos.x, 3.0f, t.pos.z };
    flashes.push_back(Flash{ c, 0.6f, 0.6f, 4.0f });
    Burst(particles, c, 26, Color{ 255, 150, 40, 255 }, 9.0f, 7.0f, 1.0f, 0.9f, 7.0f);
    Burst(particles, c, 14, Color{ 80, 80, 85, 255 }, 5.0f, 4.0f, 1.4f, 1.2f, 5.0f);
}

// Push a position out of live tower bases.
static void ResolveTowerCollisions(Vector3 &pos, const std::vector<Tower> &towers) {
    for (const auto &t : towers) {
        if (!t.alive) continue;
        float dx = pos.x - t.pos.x, dz = pos.z - t.pos.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < 3.6f * 3.6f && d2 > 1e-4f) {
            float d = sqrtf(d2);
            pos.x = t.pos.x + dx / d * 3.6f;
            pos.z = t.pos.z + dz / d * 3.6f;
        }
    }
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

static Color WithAlpha(Color c, float a) {
    c.a = (unsigned char)(255.0f * Clamp(a, 0.0f, 1.0f));
    return c;
}

// Tapered limb segment between two points (walker legs, antennae, gun pods).
static void DrawLimb(const Vector3 &a, const Vector3 &b, float r, Color c) {
    DrawCylinderEx(a, b, r, r * 0.7f, 8, c);
}

// Tripod walker chassis + legs in a local frame: origin on the ground under
// the walker, -Z forward, already yawed by the hull angle. crumple 0 = intact,
// 1 = collapsed wreck (legs fold, chassis drops).
static void DrawTripodLocal(float walkPhase, float bobY, Color body, Color metal,
                            Color dark, Color trim, float alpha, float crumple) {
    float chassisY = 3.0f + bobY - crumple * 1.7f;
    // Central armored pod (scaled sphere).
    rlPushMatrix();
    rlTranslatef(0.0f, chassisY, 0.0f);
    rlScalef(1.5f, 0.95f, 1.5f);
    DrawSphere(Vector3{ 0.0f, 0.0f, 0.0f }, 1.5f, WithAlpha(body, alpha));
    rlPopMatrix();
    // Armor trim ring around the pod's equator.
    DrawCylinder(Vector3{ 0.0f, chassisY - 0.15f, 0.0f }, 2.1f, 2.25f, 0.35f, 12,
                 WithAlpha(trim, alpha));
    // Twin sensor antennae with glowing tips.
    for (float sx : { -0.55f, 0.55f }) {
        Vector3 ab = { sx, chassisY + 1.1f, 0.35f };
        Vector3 at = { sx * 1.5f, chassisY + 2.1f, 0.5f };
        DrawLimb(ab, at, 0.06f, WithAlpha(dark, alpha));
        DrawSphere(at, 0.13f, WithAlpha(trim, alpha));
    }
    // Three legs, 120 degrees apart, procedural walk cycle.
    for (int i = 0; i < 3; ++i) {
        float a = (float)i * (2.0f * PI / 3.0f);
        Vector3 outward = { sinf(a), 0.0f, -cosf(a) };
        Vector3 hip = { outward.x * 1.1f, chassisY - 0.3f, outward.z * 1.1f };
        float ph = walkPhase + (float)i * (2.0f * PI / 3.0f);
        float stepping = (crumple > 0.5f) ? 0.0f : 1.0f;
        float stride = sinf(ph) * 1.0f * stepping;
        float lift = fmaxf(0.0f, sinf(ph + PI * 0.5f)) * 0.8f * stepping;
        float spread = 2.9f - crumple * 1.3f;
        Vector3 foot = { outward.x * spread, lift - crumple * 0.3f,
                         outward.z * spread - stride };
        Vector3 knee = { (hip.x + foot.x) * 0.5f + outward.x * 0.9f,
                         (hip.y + foot.y) * 0.5f + 1.0f - crumple * 1.4f,
                         (hip.z + foot.z) * 0.5f + outward.z * 0.9f };
        DrawLimb(hip, knee, 0.28f, WithAlpha(metal, alpha));
        DrawLimb(knee, foot, 0.2f, WithAlpha(dark, alpha));
        // Hip and knee joint pods in trim color.
        DrawSphere(hip, 0.34f, WithAlpha(trim, alpha));
        DrawSphere(knee, 0.26f, WithAlpha(metal, alpha));
        DrawSphere(foot, 0.32f, WithAlpha(dark, alpha));
    }
}

// Sensor head + ventral gun pod, yawed by the walker's aim (hull + turret).
// World space; the head is the part that pops off on death.
static void DrawTripodHead(const Enemy &e, Color armor, Color trim, Color glow, float alpha) {
    float headA = e.hullAngle + e.turretAngle;
    Vector3 hd = { sinf(headA), 0.0f, -cosf(headA) };
    float bobY = sinf(e.walkPhase * 2.0f) * 0.08f;
    // Neck from the pod top to the head.
    Vector3 neckB = { e.pos.x + hd.x * 0.7f, 4.1f + bobY, e.pos.z + hd.z * 0.7f };
    Vector3 hc = { e.pos.x + hd.x * 1.5f, 4.6f + bobY, e.pos.z + hd.z * 1.5f };
    DrawLimb(neckB, hc, 0.3f, WithAlpha(armor, alpha));
    DrawSphere(hc, 0.8f, WithAlpha(armor, alpha));
    // Glowing sensor eye on the head's face.
    Vector3 eyeP = { hc.x + hd.x * 0.68f, hc.y + 0.15f, hc.z + hd.z * 0.68f };
    DrawSphere(eyeP, 0.3f, WithAlpha(glow, alpha));
    DrawSphere(eyeP, 0.16f, WithAlpha(Color{ 255, 255, 255, 255 }, alpha));
    // Ventral gun pod slung under the chassis, barrel forward.
    Vector3 gunC = { e.pos.x + hd.x * 0.9f, 2.5f + bobY, e.pos.z + hd.z * 0.9f };
    DrawSphere(gunC, 0.5f, WithAlpha(trim, alpha));
    Vector3 m1 = { gunC.x + hd.x * 2.5f, gunC.y, gunC.z + hd.z * 2.5f };
    DrawLimb(gunC, m1, 0.16f, WithAlpha(Color{ 30, 32, 38, 255 }, alpha));
    DrawSphere(m1, 0.2f, WithAlpha(glow, alpha));  // muzzle glow
}

// Living tripod walker.
static void DrawTripodWalker(const Enemy &e, const Config &cfg, float alpha) {
    static const Color GUNMETAL = { 52, 55, 62, 255 };
    static const Color LEGMETAL = { 70, 74, 82, 255 };
    static const Color DARKMETAL = { 30, 32, 38, 255 };
    Color trim = cfg.enemyColor;
    Color glow = Color{ (unsigned char)fminf(trim.r * 1.6f, 255.0f),
                       (unsigned char)fminf(trim.g * 1.6f, 255.0f),
                       (unsigned char)fminf(trim.b * 1.6f, 255.0f), 255 };
    if (e.hitFlashT > 0.0f) {
        trim = Color{ 255, 240, 230, 255 };
        glow = Color{ 255, 255, 255, 255 };
    }
    float bobY = sinf(e.walkPhase * 2.0f) * 0.08f;
    rlPushMatrix();
    rlTranslatef(e.pos.x, 0.0f, e.pos.z);
    rlRotatef(-e.hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawTripodLocal(e.walkPhase, bobY, GUNMETAL, LEGMETAL, DARKMETAL, trim, alpha, 0.0f);
    rlPopMatrix();
    DrawTripodHead(e, GUNMETAL, trim, glow, alpha);
}

// Dead tripod: chassis keeled over around its fall axis, legs crumpled,
// sensor head popped off ballistically (drawn where it landed).
static void DrawTripodWreck(const Enemy &e) {
    static const Color CHARRED = { 38, 33, 28, 255 };
    static const Color CHARRED2 = { 52, 48, 44, 255 };
    float tip = fminf(e.deathT * 2.2f, 1.05f);
    float sink = fminf(e.deathT * 0.8f, 0.8f);
    rlPushMatrix();
    rlTranslatef(e.pos.x, 0.0f, e.pos.z);
    rlRotatef(tip * RAD2DEG, e.fallAxis.x, 0.0f, e.fallAxis.z);
    rlTranslatef(0.0f, -sink, 0.0f);
    rlRotatef(-e.hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawTripodLocal(e.walkPhase, 0.0f, CHARRED, CHARRED2, CHARRED, CHARRED2, 1.0f, 1.0f);
    rlPopMatrix();
    // The popped head where it landed.
    DrawSphere(e.turretPos, 0.8f, CHARRED);
    DrawSphere(Vector3{ e.turretPos.x, e.turretPos.y + 0.15f, e.turretPos.z },
               0.3f, CHARRED2);
    // Fire flicker on the collapsed chassis.
    float f = 0.75f + 0.25f * sinf(e.deathT * 13.0f);
    DrawSphere(Vector3{ e.pos.x, 2.2f, e.pos.z }, 0.55f * f, Color{ 255, 120, 25, 210 });
    DrawSphere(Vector3{ e.pos.x + 0.5f, 2.0f, e.pos.z - 0.3f }, 0.35f * f,
               Color{ 255, 190, 60, 190 });
}

static void DrawEnemies(const std::vector<Enemy> &enemies, const Config &cfg) {
    static const Color CHARRED = { 38, 33, 28, 255 };
    for (const auto &e : enemies) {
        // Fog of war: fully fogged enemies are skipped; enemies in the
        // soft reveal band fade in so they visibly emerge from the fog
        // instead of popping into existence.
        float alpha = e.alive ? (1.0f - e.fogFactor) : 1.0f;
        if (alpha <= 0.02f) continue;
        // A fading enemy must not write depth: it would punch a
        // tank-shaped hole through the fog puffs behind it.
        bool faded = alpha < 0.99f;
        if (faded) rlDisableDepthMask();
        if (e.alive) {
            if (e.kind == WalkerKind::TRIPOD) {
                DrawTripodWalker(e, cfg, alpha);
            } else {
                Color armor = cfg.enemyColor;
                if (e.hitFlashT > 0.0f) armor = Color{ 255, 240, 230, 255 };
                DrawTankHull(e.pos, e.hullAngle, armor, alpha);
                DrawTankTurret(Vector3{ e.pos.x, 2.25f, e.pos.z },
                               e.hullAngle + e.turretAngle, armor, alpha);
            }
            // Tracking ping: pulsing red ring under an enemy with a lock.
            if (e.aiState == AIState::SHOOT && e.aimTimer > 0.05f) {
                float base = (e.kind == WalkerKind::TRIPOD) ? 3.6f : 2.8f;
                float pulse = base + sinf(e.aimTimer * 14.0f) * 0.5f;
                DrawCylinderWires(Vector3{ e.pos.x, 0.08f, e.pos.z }, pulse, pulse,
                                  0.12f, 24,
                                  Color{ 255, 40, 40, (unsigned char)(230 * alpha) });
            }
        } else {
            if (e.kind == WalkerKind::TRIPOD) {
                DrawTripodWreck(e);
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
        if (faded) rlEnableDepthMask();
    }
}

// Soft radial fog-puff sprite, generated once: white with a smooth falloff
// so overlapping puffs melt into a continuous bank instead of hard discs.
static Texture2D MakeFogPuffTexture() {
    const int S = 128;
    Image img = GenImageColor(S, S, Color{ 0, 0, 0, 0 });
    Color *px = (Color *)img.data;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float dx = (x + 0.5f) / S * 2.0f - 1.0f;
            float dy = (y + 0.5f) / S * 2.0f - 1.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float a = fmaxf(0.0f, 1.0f - d);
            a = a * a * (3.0f - 2.0f * a);
            px[y * S + x] = Color{ 255, 255, 255, (unsigned char)(255.0f * a) };
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

struct FogPuff {
    Vector3 pos;
    float size;
    float alpha;
    float dist2;  // to camera, for back-to-front sorting
};

// Visible fog of war: camera-facing soft puffs laid over fogged ground on
// a coarse grid, back-to-front, depth-tested (buildings occlude puffs behind
// them) but not depth-writing (puffs never carve holes in each other).
// Unlike horizontal planes this reads from any camera height — the gunner
// sees a fog wall on the horizon, the drone sees a soft blanket — and
// enemies visibly drive out of it. Enemies mid-reveal also carry a small
// wisp so the emergence reads up close.
static void DrawFogPuffs(const Camera3D &camera, const std::vector<Enemy> &enemies,
                         const std::vector<Vector3> &revealers,
                         const std::vector<Building> &village,
                         const Texture2D &puffTex, const Config &cfg) {
    static std::vector<FogPuff> puffs;
    puffs.clear();
    puffs.reserve(1700);
    const float step = 10.0f;
    const float maxD2 = 300.0f * 300.0f;
    for (float gz = -ARENA_HALF; gz <= ARENA_HALF; gz += step) {
        for (float gx = -ARENA_HALF; gx <= ARENA_HALF; gx += step) {
            float f = FogFactorAt(gx, gz, revealers, village,
                                  cfg.fogSightRadius, cfg.fogEdgeWidth);
            if (f < 0.04f) continue;
            float dx = gx - camera.position.x, dz = gz - camera.position.z;
            float d2 = dx * dx + dz * dz;
            if (d2 > maxD2) continue;
            // Deterministic size variation so the bank edge looks organic
            // instead of tiled (stable frame to frame — no shimmer).
            float h = sinf(gx * 12.9898f + gz * 78.233f) * 43758.5453f;
            float frac = h - floorf(h);
            float size = cfg.fogPuffSize * (0.8f + 0.45f * frac);
            puffs.push_back({ Vector3{ gx, 2.6f, gz }, size,
                              f * cfg.fogPuffAlpha, d2 });
        }
    }
    // Emergence wisps: enemies inside the reveal band drag fog with them.
    for (const auto &e : enemies) {
        if (!e.alive || e.fogFactor < 0.05f || e.fogFactor > 0.97f) continue;
        float dx = e.pos.x - camera.position.x, dz = e.pos.z - camera.position.z;
        puffs.push_back({ Vector3{ e.pos.x, 2.2f, e.pos.z },
                          cfg.fogPuffSize * 0.45f, e.fogFactor * 0.5f,
                          dx * dx + dz * dz });
    }
    std::sort(puffs.begin(), puffs.end(),
              [](const FogPuff &a, const FogPuff &b) { return a.dist2 > b.dist2; });
    rlDisableDepthMask();
    for (const auto &p : puffs) {
        unsigned char a = (unsigned char)(255.0f * fminf(1.0f, p.alpha));
        if (a == 0) continue;
        DrawBillboard(camera, puffTex, p.pos, p.size, Color{ 232, 234, 240, a });
    }
    rlEnableDepthMask();
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
// Death-replay rendering: the world redrawn from a recorded snapshot,
// from the drone's point of view. Fog of war is intentionally off — the
// point of the replay is to see (and learn from) the full battlefield.
static void DrawReplayBuildings(const std::vector<Building> &village,
                                const std::vector<ReplayBldSnap> &rb) {
    for (size_t i = 0; i < village.size() && i < rb.size(); ++i) {
        const Building &b = village[i];
        const ReplayBldSnap &r = rb[i];
        if (r.destroyed) {
            if (r.collapseT < 1.0f) {
                float t = r.collapseT;
                rlPushMatrix();
                rlTranslatef(b.center.x, 0.0f, b.center.z);
                rlRotatef(t * 68.0f, r.fallAxis.x, 0.0f, r.fallAxis.z);
                rlTranslatef(0.0f, b.center.y - t * b.size.y * 0.75f, 0.0f);
                DrawCube(Vector3{ 0, 0, 0 }, b.size.x, b.size.y, b.size.z,
                         Color{ 90, 82, 74, 255 });
                rlPopMatrix();
            } else {
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
        float f = 0.55f + 0.45f * ((float)r.hp / (float)b.maxHp);
        Color c = Color{ (unsigned char)(b.color.r * f), (unsigned char)(b.color.g * f),
                         (unsigned char)(b.color.b * f), 255 };
        DrawCube(b.center, b.size.x, b.size.y, b.size.z, c);
        DrawCubeWires(b.center, b.size.x, b.size.y, b.size.z, Color{ 0, 0, 0, 60 });
        DrawCube(Vector3{ b.center.x, b.size.y + 0.05f, b.center.z },
                 b.size.x * 0.98f, 0.1f, b.size.z * 0.98f,
                 Color{ 70, 62, 55, 255 });
    }
}

static void DrawReplayTowers(const std::vector<Tower> &towers,
                             const std::vector<ReplayTowerSnap> &rt, float replayTime) {
    for (size_t i = 0; i < towers.size() && i < rt.size(); ++i) {
        const Tower &t = towers[i];
        const ReplayTowerSnap &r = rt[i];
        if (!r.alive) {
            DrawCylinder(Vector3{ t.pos.x, 0.5f, t.pos.z }, 3.2f, 3.8f, 1.0f, 8,
                         Color{ 60, 58, 55, 255 });
            continue;
        }
        DrawCylinder(Vector3{ t.pos.x, 0.0f, t.pos.z }, 3.0f, 3.6f, 4.0f, 10,
                     Color{ 130, 130, 135, 255 });
        DrawCylinder(Vector3{ t.pos.x, 4.0f, t.pos.z }, 2.2f, 2.6f, 1.0f, 10,
                     Color{ 70, 70, 78, 255 });
        rlPushMatrix();
        rlTranslatef(t.pos.x, 5.2f, t.pos.z);
        rlRotatef(r.turretAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
        DrawCube(Vector3{ 0, 0, 0 }, 2.6f, 1.2f, 3.4f, Color{ 70, 70, 78, 255 });
        DrawCube(Vector3{ 0, 0.1f, -2.8f }, 0.45f, 0.45f, 3.6f, Color{ 70, 70, 78, 255 });
        rlPopMatrix();
        // Warning light driven by replay time (the live frame counter is frozen).
        if (((int)(replayTime * 2.0f) % 2) == 0)
            DrawSphere(Vector3{ t.pos.x, 6.4f, t.pos.z }, 0.45f, RED);
        for (int k = 0; k < t.maxHp; ++k)
            DrawCube(Vector3{ t.pos.x - 2.0f + k * 1.2f, 7.2f, t.pos.z }, 0.9f, 0.9f, 0.9f,
                     k < t.hp ? GREEN : DARKGRAY);
    }
}

// Nearest recorded frame at or before time t (buffer times ascend).
static const ReplayFrame *SampleReplay(const std::deque<ReplayFrame> &buf, float t) {
    const ReplayFrame *best = nullptr;
    for (auto it = buf.rbegin(); it != buf.rend(); ++it) {
        best = &(*it);
        if (it->time <= t) return best;
    }
    return best;
}

static void DrawReplay(const ReplayFrame &f,
                       const std::vector<Building> &village,
                       const std::vector<Tower> &towers,
                       const std::vector<Ditch> &ditches,
                       const std::vector<Bridge> &bridges,
                       const Config &cfg, float replayTime) {
    Camera3D rc = {};
    rc.position = f.camPos;
    rc.target = f.camTarget;
    rc.up = Vector3{ 0.0f, 1.0f, 0.0f };
    rc.fovy = 60.0f;
    rc.projection = CAMERA_PERSPECTIVE;

    BeginMode3D(rc);
    DrawGround(ditches);
    DrawGrid(40, 20.0f);
    DrawBridges(bridges);
    DrawReplayBuildings(village, f.buildings);
    DrawReplayTowers(towers, f.towers, replayTime);
    // Reconstruct temp tanks and reuse the regular draw paths.
    Tank pt;
    pt.pos = f.player.pos; pt.hullAngle = f.player.hullAngle;
    pt.turretAngle = f.player.turretAngle;
    DrawTank(pt, false);
    std::vector<Ally> als;
    als.reserve(f.allies.size());
    for (const auto &ra : f.allies) {
        Ally a;
        a.pos = ra.pos; a.hullAngle = ra.hullAngle; a.turretAngle = ra.turretAngle;
        a.alive = ra.alive; a.deathT = ra.deathT;
        a.turretPos = ra.turretPos; a.turretSpin = ra.turretSpin;
        als.push_back(a);
    }
    std::vector<Enemy> ens;
    ens.reserve(f.enemies.size());
    for (const auto &re : f.enemies) {
        Enemy e;
        e.pos = re.pos; e.hullAngle = re.hullAngle; e.turretAngle = re.turretAngle;
        e.alive = re.alive; e.deathT = re.deathT;
        e.turretPos = re.turretPos; e.turretSpin = re.turretSpin;
        e.kind = re.kind; e.walkPhase = re.walkPhase; e.fallAxis = re.fallAxis;
        e.fogFactor = 0.0f;  // replays show the full battlefield
        ens.push_back(e);
    }
    std::vector<Enemy> noEn;
    DrawAllies(als, noEn, cfg, -1);
    DrawEnemies(ens, cfg);
    for (const auto &s : f.shells)
        DrawSphere(s.pos, 0.35f, Color{ 255, 240, 180, 255 });
    for (const auto &fl : f.flashes) {
        float a = (fl.maxT > 0.0f) ? fl.t / fl.maxT : 0.0f;
        DrawSphere(fl.pos, fl.size * (0.5f + 0.5f * a),
                   Color{ 255, 180, 60, (unsigned char)(255 * a) });
    }
    for (const auto &p : f.particles) {
        float a = (p.maxLife > 0.0f) ? fmaxf(p.life / p.maxLife, 0.0f) : 0.0f;
        DrawSphere(p.pos, p.size * (0.4f + 0.6f * a),
                   Color{ p.color.r, p.color.g, p.color.b, (unsigned char)(255 * a) });
    }
    EndMode3D();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
// Minimap: top-down picture-in-picture that follows the player tank.
// Rendered to a square texture, then blitted to the bottom-right corner.
static void DrawMinimap(RenderTexture2D target, const Tank &tank,
                        const std::vector<Ally> &allies,
                        const std::vector<Enemy> &enemies,
                        const std::vector<Building> &village,
                        const std::vector<Ditch> &ditches,
                        const std::vector<Bridge> &bridges,
                        const std::vector<Tower> &towers,
                        const std::vector<Shell> &shells,
                        const Config &cfg, Phase phase,
                        int screenWidth, int screenHeight) {
    int S = cfg.minimapSize;
    Camera3D mc = {};
    mc.position = Vector3{ tank.pos.x, cfg.minimapHeight, tank.pos.z };
    mc.target = tank.pos;
    mc.up = Vector3{ 0.0f, 0.0f, -1.0f };  // north (-Z) is up
    mc.fovy = 55.0f;
    mc.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(target);
    ClearBackground(Color{ 25, 30, 25, 255 });
    BeginMode3D(mc);
    DrawPlane(Vector3{ tank.pos.x, 0.0f, tank.pos.z }, Vector2{ 500.0f, 500.0f },
              Color{ 40, 55, 40, 255 });
    // Ditches read as dark scars; bridges and towers as markers.
    for (const auto &d : ditches)
        DrawPlane(Vector3{ d.center.x, 0.1f, d.center.z },
                  Vector2{ d.hx * 2, d.hz * 2 }, Color{ 30, 22, 15, 255 });
    for (const auto &b : bridges)
        DrawCube(Vector3{ b.center.x, 0.5f, b.center.z }, b.hx * 2, 1.0f, b.hz * 2,
                 Color{ 150, 120, 80, 255 });
    for (const auto &t : towers)
        DrawCube(Vector3{ t.pos.x, 1.0f, t.pos.z }, 5.0f, 2.0f, 5.0f,
                 t.alive ? Color{ 90, 90, 95, 255 } : Color{ 50, 50, 50, 255 });
    for (const auto &b : village) {
        if (b.destroyed)
            DrawCube(Vector3{ b.center.x, 0.5f, b.center.z },
                     b.size.x, 1.0f, b.size.z, Color{ 85, 80, 75, 255 });
        else
            DrawCube(Vector3{ b.center.x, b.center.y, b.center.z },
                     b.size.x, b.size.y, b.size.z, Color{ 125, 120, 110, 255 });
    }
    auto marker = [](Vector3 p, float heading, Color c) {
        rlPushMatrix();
        rlTranslatef(p.x, 1.0f, p.z);
        rlRotatef(heading * RAD2DEG, 0.0f, 1.0f, 0.0f);
        DrawCube(Vector3{ 0.0f, 0.0f, 0.0f }, 4.0f, 2.0f, 6.5f, c);
        // Nose tick so heading reads at a glance.
        DrawCube(Vector3{ 0.0f, 0.0f, -4.0f }, 1.5f, 2.2f, 1.5f, WHITE);
        rlPopMatrix();
    };
    marker(tank.pos, tank.hullAngle, GREEN);
    for (const auto &a : allies)
        if (a.alive) marker(a.pos, a.hullAngle, cfg.allyColor);
    // Enemies stay hidden during SETUP, and fade with fog of war.
    if (phase == Phase::COMBAT)
        for (const auto &e : enemies) {
            if (!e.alive || e.fogFactor >= 0.98f) continue;
            Color c = cfg.enemyColor;
            c.a = (unsigned char)(255 * (1.0f - e.fogFactor));
            marker(e.pos, e.hullAngle, c);
        }
    for (const auto &s : shells) DrawSphere(s.pos, 1.2f, YELLOW);
    EndMode3D();
    EndTextureMode();

    int mx = screenWidth - S - 16, my = screenHeight - S - 16;
    DrawTextureRec(target.texture, Rectangle{ 0.0f, 0.0f, (float)S, -(float)S },
                   Vector2{ (float)mx, (float)my }, WHITE);
    DrawRectangleLines(mx, my, S, S, WHITE);
    DrawText("MAP", mx + 8, my + 6, 16, WHITE);
}

// Chase drone view: live follow-camera window above the minimap in gunner
// mode. The camera stays above/behind the player tank (high enough to see
// over buildings) and tracks it, so the tank is always in frame.
static void DrawChaseView(RenderTexture2D target, const Tank &tank,
                          const std::vector<Ally> &allies,
                          const std::vector<Enemy> &enemies,
                          const std::vector<Building> &village,
                          const std::vector<Ditch> &ditches,
                          const std::vector<Bridge> &bridges,
                          const std::vector<Tower> &towers,
                          const std::vector<Shell> &shells,
                          const std::vector<Flash> &flashes,
                          const std::vector<Particle> &particles,
                          const std::vector<Vector3> &revealers,
                          const Texture2D &fogPuffTex,
                          const Config &cfg, Phase phase, int selectedAlly,
                          int screenWidth, int screenHeight, int frameCount) {
    int W = cfg.chaseWidth, H = cfg.chaseHeight;
    Vector3 behind = { -sinf(tank.hullAngle), 0.0f, cosf(tank.hullAngle) };
    Camera3D cc = {};
    cc.position = Vector3{ tank.pos.x + behind.x * cfg.chaseCamDist,
                            tank.pos.y + cfg.chaseCamHeight,
                            tank.pos.z + behind.z * cfg.chaseCamDist };
    cc.target = Vector3{ tank.pos.x, tank.pos.y + 2.0f, tank.pos.z };
    cc.up = Vector3{ 0.0f, 1.0f, 0.0f };
    cc.fovy = 60.0f;
    cc.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(target);
    ClearBackground(SKYBLUE);
    BeginMode3D(cc);
    DrawGround(ditches);
    DrawGrid(40, 20.0f);
    DrawBridges(bridges);
    DrawVillage(village);
    DrawTowers(towers, frameCount);
    DrawTank(tank, false);  // solid, never the gunner ghost
    if (phase == Phase::COMBAT) DrawEnemies(enemies, cfg);
    DrawAllies(allies, enemies, cfg, selectedAlly);
    if (phase == Phase::COMBAT && cfg.fogEnabled)
        DrawFogPuffs(cc, enemies, revealers, village, fogPuffTex, cfg);
    for (const auto &s : shells) {
        if (s.trailCount >= 2)
            DrawCylinderEx(s.trail[0], s.pos, 0.22f, 0.22f, 8,
                           Color{ 255, 175, 65, 255 });
        DrawSphere(s.pos, 0.35f, Color{ 255, 240, 180, 255 });
    }
    for (const auto &f : flashes) {
        float a = f.t / f.maxT;
        DrawSphere(f.pos, f.size * (0.5f + 0.5f * a),
                   Color{ 255, 180, 60, (unsigned char)(255 * a) });
    }
    for (const auto &p : particles) {
        float a = fmaxf(p.life / p.maxLife, 0.0f);
        DrawSphere(p.pos, p.size * (0.4f + 0.6f * a),
                   Color{ p.color.r, p.color.g, p.color.b, (unsigned char)(255 * a) });
    }
    EndMode3D();
    EndTextureMode();

    // Blit above the minimap, right-aligned.
    int my = screenHeight - cfg.minimapSize - 16;  // minimap top edge
    int cx = screenWidth - W - 16;
    int cy = my - H - 12;
    DrawTextureRec(target.texture, Rectangle{ 0.0f, 0.0f, (float)W, -(float)H },
                   Vector2{ (float)cx, (float)cy }, WHITE);
    DrawRectangleLines(cx, cy, W, H, WHITE);
    DrawText("DRONE", cx + 8, cy + 6, 16, WHITE);
}

int main() {
    const int screenWidth = 1280, screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Tankshooter v0.6");
    SetTargetFPS(60);
    // Cursor starts enabled: the game opens in drone mode (setup phase).

    Config cfg = LoadConfig();
    RenderTexture2D minimapTarget = LoadRenderTexture(cfg.minimapSize, cfg.minimapSize);
    RenderTexture2D chaseTarget = LoadRenderTexture(cfg.chaseWidth, cfg.chaseHeight);
    // Fog-of-war puffs: one soft radial sprite, instanced as camera-facing
    // billboards over fogged ground.
    Texture2D fogPuffTex = MakeFogPuffTexture();

    std::vector<GameMap> maps = BuildMaps(cfg.ditchDepth);
    int mapIdx = 0;
    for (size_t i = 0; i < maps.size(); ++i)
        if (maps[i].name == cfg.mapDefault) mapIdx = (int)i;

    Tank tank;
    std::vector<Building> village;
    std::vector<Enemy> enemies;
    std::vector<Ally> allies;
    std::vector<Ditch> ditches;
    std::vector<Bridge> bridges;
    std::vector<Tower> towers;
    int selectedAlly = 0;
    std::vector<Shell> shells;
    std::vector<Flash> flashes;
    std::vector<OrderPing> pings;
    std::vector<Particle> particles;
    float fireCooldown = 0.0f;
    // Battle stats (reset on R, battleStart set on ENTER).
    int shotsFired = 0, shotsHit = 0, playerKills = 0;
    double battleStart = 0.0;
    double battleEnd = 0.0;
    DroneCam drone;
    Vector2 droneCursor = { screenWidth / 2.0f, screenHeight / 2.0f };
    // Setup phase starts in drone mode so the player can survey the map
    // and position allies before combat.
    CamMode mode = CamMode::DRONE;
    Phase phase = Phase::SETUP;
    bool gameOver = false;
    bool victory = false;           // arena cleared: free tour, can't lose
    // Defeat replay: ring buffer of snapshots + playback state.
    std::deque<ReplayFrame> replayBuf;
    bool replayArmed = false;       // 1s beat after defeat, before replay
    float replayWaitUntil = 0.0f;
    bool replaying = false;
    float replayPlayStart = 0.0f;   // GetTime() when playback started
    float replayFrom = 0.0f, replayTo = 0.0f;
    // Victory replay: the span is computed when R is pressed — the battle's
    // end plus a few seconds of aftermath (the buffer keeps recording the
    // post-victory tour, so the wreck-burning aftermath is in there).
    float victoryTime = 0.0f;
    bool vreplayAvail = false;

    // (Re)build everything for maps[mapIdx]: village, terrain, towers,
    // player, enemies, allies. Used at startup, on R, and on map change.
    auto loadMap = [&]() {
        const GameMap &m = maps[mapIdx];
        village = BuildVillage(cfg.buildingHits, m.villageSeed);
        ditches = m.ditches;
        bridges = m.bridges;
        towers.clear();
        for (const auto &tp : m.towerPos) {
            Tower t;
            t.pos = tp;
            t.hp = t.maxHp = cfg.towerHits;
            towers.push_back(t);
        }
        tank = Tank{};
        tank.hp = tank.maxHp = cfg.playerHits;
        enemies = SpawnEnemies(cfg.enemyCount, cfg.enemyHits, village, ditches, bridges, towers);
        allies = SpawnAllies(cfg.allyCount, cfg.allyHits, tank.pos);
        selectedAlly = 0;
        shells.clear(); flashes.clear(); particles.clear(); pings.clear();
        fireCooldown = 0.0f;
        shotsFired = 0; shotsHit = 0; playerKills = 0;
        battleStart = 0.0;
        battleEnd = 0.0;
        mode = CamMode::DRONE;
        ResetDroneView(drone, tank);
        EnableCursor();
        phase = Phase::SETUP;
        gameOver = false;
        victory = false;
        replayBuf.clear();
        replaying = false; replayArmed = false;
    };
    loadMap();
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

        if (replaying && IsKeyPressed(KEY_R)) {
            // Skip the death replay, land on the defeat panel.
            replaying = false;
        } else if (replayArmed && IsKeyPressed(KEY_R)) {
            // Skip the beat before the replay, land on the defeat panel.
            replayArmed = false;
        } else if (gameOver && !victory && IsKeyPressed(KEY_R)) {
            // Defeat panel: watch the replay again (any number of times).
            if (cfg.replayEnabled && replayBuf.size() >= 30) {
                replaying = true;
                replayPlayStart = (float)GetTime();
            }
        } else if (victory && IsKeyPressed(KEY_R)) {
            // Victory panel: watch the end-of-battle replay, including a few
            // seconds of aftermath. Resolved live so the aftermath is in the
            // buffer; if the victory footage aged out, R does nothing.
            if (vreplayAvail && !replayBuf.empty()) {
                float to = fminf(replayBuf.back().time, victoryTime + cfg.replayPostSeconds);
                float from = fmaxf(replayBuf.front().time, victoryTime - cfg.replayDuration);
                if (to > from + 1.0f) {
                    replayFrom = from;
                    replayTo = to;
                    replaying = true;
                    replayPlayStart = GetTime();
                }
            }
        } else if (victory && IsKeyPressed(KEY_ENTER)) {
            // Victory panel: start a new mission on the current map.
            loadMap();
        } else if (gameOver && !victory && IsKeyPressed(KEY_ENTER)) {
            // Defeat panel: retry the battle.
            loadMap();
        }

        // M cycles maps in the setup phase.
        if (phase == Phase::SETUP && IsKeyPressed(KEY_M)) {
            mapIdx = (mapIdx + 1) % (int)maps.size();
            loadMap();
        }

        // ENTER starts the battle from the setup phase and drops the player
        // into the gunner seat.
        if (phase == Phase::SETUP && IsKeyPressed(KEY_ENTER)) {
            phase = Phase::COMBAT;
            mode = CamMode::GUNNER;
            DisableCursor();
            battleStart = GetTime();
            battleEnd = 0.0;
        }

        // Fog-of-war revealers (player, living allies, drone), refreshed in
        // the update section below and reused by the fog-puff pass at draw.
        // Declared here so game-over frames reuse the last live values.
        std::vector<Vector3> revealers;

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
                ResetDroneView(drone, tank);
                // Drone mode needs a visible cursor for click-to-order;
                // gunner mode uses relative mouse-look.
                EnableCursor();
            } else {
                DisableCursor();
            }
        }
        tabWasDown = tabDown;

        // Ally orders (both modes): 1..N select, F = follow, H = hold.
        // Right-click move/attack targeting stays drone-only (it needs the
        // drone's ground cursor; handled after the camera update below).
        if (!allies.empty()) {
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

        UpdateTank(tank, village, ditches, bridges, dt, mode == CamMode::GUNNER,
                   cfg.playerSpeed, cfg.ditchSlowFactor);
        ResolveWreckCollisions(tank.pos, enemies, cfg.wreckBlocks);

        fireCooldown -= dt;

        // Firing: left mouse or Space, gated by cooldown. Works in both modes —
        // in drone mode the turret fires along its current aim, so you can
        // watch the shells from outside.
        if ((IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsKeyDown(KEY_SPACE)) &&
            fireCooldown <= 0.0f && phase == Phase::COMBAT) {
            FireShell(tank, shells, cfg);
            fireCooldown = cfg.shellCooldown;
            shotsFired++;
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
                        if (tank.hp <= 0 && !victory) {
                            gameOver = true;
                            battleEnd = GetTime();
                            // Arm the death replay if we have enough footage.
                            if (cfg.replayEnabled && replayBuf.size() >= 30) {
                                replayTo = replayBuf.back().time;
                                replayFrom = fmaxf(replayBuf.front().time,
                                                   replayTo - cfg.replayDuration);
                                replayArmed = true;
                                replayWaitUntil = (float)GetTime() + 1.0f;
                            }
                            replaying = false;
                        }
                        // Post-victory tour: hits still flash, but the tank can't die.
                        if (victory && tank.hp < 1) tank.hp = 1;
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
                            bool wasAlive = e.alive;
                            DamageEnemy(e, particles, flashes, tank.pos, village);
                            if (it->fromPlayer) {
                                shotsHit++;
                                if (wasAlive && !e.alive) playerKills++;
                            }
                            flashes.push_back(Flash{ it->pos, 0.25f, 0.25f, 2.0f });
                            dead = true;
                            break;
                        }
                    }
                    // Towers are hard targets for player/ally shells.
                    if (!dead) {
                        for (auto &t : towers) {
                            if (ShellHitsTower(it->pos, cfg.shellRadius, t)) {
                                DamageTower(t, particles, flashes);
                                flashes.push_back(Flash{ it->pos, 0.25f, 0.25f, 2.0f });
                                dead = true;
                                break;
                            }
                        }
                    }
                }
            }
            it = dead ? shells.erase(it) : std::next(it);
        }

        // Enemy AI (advance / shoot / seek-cover) + death animations.
        // Hidden and inert during SETUP: they spawn when combat begins.
        if (phase == Phase::COMBAT) {
            for (auto &e : enemies)
                UpdateEnemyAI(e, tank, village, ditches, bridges, shells, flashes, cfg, dt);
            UpdateTowers(towers, tank, allies, village, shells, flashes, cfg, dt, victory);
        }
        UpdateEnemies(enemies, particles, dt);
        // Ally AI (orders + engage) + death animations. In SETUP allies
        // follow orders but do not engage hidden enemies.
        UpdateAllies(allies, tank, enemies, village, ditches, bridges, shells, flashes, particles,
                     cfg, phase == Phase::COMBAT, dt);
        // Ditch sink: tanks drop to the ditch floor.
        tank.pos.y = -DitchDepthAt(tank.pos.x, tank.pos.z, ditches, bridges);
        for (auto &e : enemies) e.pos.y = -DitchDepthAt(e.pos.x, e.pos.z, ditches, bridges);
        for (auto &a : allies) a.pos.y = -DitchDepthAt(a.pos.x, a.pos.z, ditches, bridges);
        // Towers block movement.
        ResolveTowerCollisions(tank.pos, towers);
        for (auto &e : enemies) ResolveTowerCollisions(e.pos, towers);
        for (auto &a : allies) ResolveTowerCollisions(a.pos, towers);
        // Fog of war: per-enemy fog factor from revealers (player, allies,
        // drone). The revealer list is reused below by the visible fog puffs.
        revealers.clear();
        if (phase == Phase::COMBAT && cfg.fogEnabled) {
            revealers.push_back(tank.pos);
            for (const auto &a : allies) if (a.alive) revealers.push_back(a.pos);
            revealers.push_back(drone.pos);
            for (auto &e : enemies)
                e.fogFactor = e.alive ? FogFactorAt(e.pos.x, e.pos.z, revealers, village,
                                                    cfg.fogSightRadius, cfg.fogEdgeWidth)
                                      : 0.0f;
        } else {
            for (auto &e : enemies) e.fogFactor = 0.0f;
        }
        // Victory: the last enemy is destroyed. The battle is over — the map
        // stays navigable for a post-battle tour, but the player can no
        // longer lose (HP clamps at 1, see the shell-hit code).
        if (!victory && phase == Phase::COMBAT) {
            bool anyAlive = false;
            for (const auto &e : enemies) if (e.alive) { anyAlive = true; break; }
            if (!anyAlive) {
                victory = true;
                if (battleEnd == 0.0) battleEnd = GetTime();
                // Pin the victory moment; the replay span (battle end +
                // aftermath) is resolved when R is pressed.
                if (cfg.replayEnabled && replayBuf.size() >= 30) {
                    victoryTime = (float)GetTime();
                    vreplayAvail = true;
                }
            }
        }
        // Death-replay recorder: 20 Hz snapshots of the battlefield state.
        if (cfg.replayEnabled && phase == Phase::COMBAT && !gameOver && (frameCount % 3 == 0)) {
            ReplayFrame fr;
            fr.time = (float)GetTime();
            fr.player = { tank.pos, tank.hullAngle, tank.turretAngle, tank.hp > 0,
                          0.0f, Vector3{ 0, 0, 0 }, 0.0f };
            for (const auto &a : allies)
                fr.allies.push_back({ a.pos, a.hullAngle, a.turretAngle, a.alive,
                                      a.deathT, a.turretPos, a.turretSpin });
            for (const auto &e : enemies)
                fr.enemies.push_back({ e.pos, e.hullAngle, e.turretAngle, e.alive,
                                      e.deathT, e.turretPos, e.turretSpin,
                                      e.kind, e.walkPhase, e.fallAxis });
            for (const auto &s : shells) {
                if (fr.shells.size() >= 40) break;
                fr.shells.push_back({ s.pos });
            }
            for (const auto &fl : flashes) {
                if (fr.flashes.size() >= 20) break;
                fr.flashes.push_back({ fl.pos, fl.t, fl.maxT, fl.size });
            }
            for (const auto &p : particles) {
                if (fr.particles.size() >= 120) break;
                fr.particles.push_back({ p.pos, p.color, p.size, p.life, p.maxLife });
            }
            for (const auto &b : village)
                fr.buildings.push_back({ b.destroyed, b.collapseT, b.hp, b.fallAxis });
            for (const auto &t : towers)
                fr.towers.push_back({ t.alive, t.turretAngle });
            // Replay camera: a follow-drone behind the player (same framing
            // as the chase view), so the tank and its destruction stay in
            // frame even if the real drone was parked elsewhere.
            Vector3 behind = { -sinf(tank.hullAngle), 0.0f, cosf(tank.hullAngle) };
            fr.camPos = Vector3{ tank.pos.x + behind.x * cfg.chaseCamDist,
                                 tank.pos.y + cfg.chaseCamHeight,
                                 tank.pos.z + behind.z * cfg.chaseCamDist };
            fr.camTarget = Vector3{ tank.pos.x, tank.pos.y + 2.0f, tank.pos.z };
            replayBuf.push_back(std::move(fr));
            size_t maxFrames = (size_t)((cfg.replayDuration + cfg.replayPostSeconds) * 20.0f * 1.5f);
            while (replayBuf.size() > maxFrames) replayBuf.pop_front();
        }
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
                    // Enemies are hidden in SETUP: only move orders there.
                    int hitEnemy = -1;
                    if (phase == Phase::COMBAT) {
                        for (size_t ei = 0; ei < enemies.size(); ++ei) {
                            if (!enemies[ei].alive) continue;
                            float dx = gp.x - enemies[ei].pos.x, dz = gp.z - enemies[ei].pos.z;
                            if (dx * dx + dz * dz < 16.0f) { hitEnemy = (int)ei; break; }
                        }
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

        // Defeat replay timing runs on the wall clock (the sim is frozen).
        if (replayArmed && GetTime() >= replayWaitUntil) {
            replayArmed = false;
            replaying = true;
            replayPlayStart = (float)GetTime();
        }
        bool inReplay = false;
        float replayNow = 0.0f;
        if (replaying) {
            replayNow = replayFrom + (float)(GetTime() - replayPlayStart);
            if (replayNow >= replayTo || replayBuf.empty()) replaying = false;
            else inReplay = true;
        }

        BeginDrawing();
        ClearBackground(SKYBLUE);

        if (inReplay) {
            const ReplayFrame *fr = SampleReplay(replayBuf, replayNow);
            if (fr) DrawReplay(*fr, village, towers, ditches, bridges, cfg,
                               replayNow - replayFrom);
            // Replay chrome: label, progress bar, skip hint.
            DrawRectangle(0, 0, screenWidth, 36, Color{ 0, 0, 0, 160 });
            DrawText(TextFormat("REPLAY - %.0fs (full visibility)", replayTo - replayFrom),
                     12, 8, 20, YELLOW);
            DrawText("R to skip", screenWidth - 110, 8, 20, LIGHTGRAY);
            float prog = (replayTo > replayFrom)
                ? Clamp((replayNow - replayFrom) / (replayTo - replayFrom), 0.0f, 1.0f) : 0.0f;
            DrawRectangle(12, 32, (int)((screenWidth - 24) * prog), 4, YELLOW);
            EndDrawing();
            continue;
        }

        BeginMode3D(camera);
        // Ground with ditch pits, street grid, bridges, towers.
        DrawGround(ditches);
        DrawGrid(40, 20.0f);
        DrawBridges(bridges);
        DrawVillage(village);
        DrawTowers(towers, frameCount);
        DrawTank(tank, mode == CamMode::GUNNER);
        // Enemies are hidden until combat begins, and by fog of war.
        if (phase == Phase::COMBAT) DrawEnemies(enemies, cfg);
        DrawAllies(allies, enemies, cfg, selectedAlly);
        // Fog puffs over the opaque world (enemies emerge from them).
        if (phase == Phase::COMBAT && cfg.fogEnabled)
            DrawFogPuffs(camera, enemies, revealers, village, fogPuffTex, cfg);
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

        // Minimap (gunner mode): top-down view following the player tank.
        if (mode == CamMode::GUNNER && cfg.minimapEnabled)
            DrawMinimap(minimapTarget, tank, allies, enemies, village, ditches, bridges,
                        towers, shells, cfg, phase, screenWidth, screenHeight);
        // Chase drone view above the minimap: follow-cam on the player tank.
        if (mode == CamMode::GUNNER && cfg.chaseEnabled)
            DrawChaseView(chaseTarget, tank, allies, enemies, village, ditches, bridges,
                          towers, shells, flashes, particles, revealers, fogPuffTex,
                          cfg, phase, selectedAlly, screenWidth, screenHeight, frameCount);

        // HUD
        const char *modeName = (mode == CamMode::GUNNER) ? "GUNNER" : "DRONE";
        DrawText(TextFormat("[%s]  TAB to switch", modeName), 16, 12, 22, DARKGRAY);
        DrawText(TextFormat("Map: %s", maps[mapIdx].name.c_str()),
                 screenWidth - 360, 12, 20, DARKGRAY);
        if (mode == CamMode::GUNNER){
            // D-pad cross: WASD + arrow labels on the arms (drive controls).
            int dx = 16, dy = 36, s = 34;
            Color armBg = Color{ 60, 60, 70, 220 };
            DrawRectangle(dx + s, dy + s, s, s, Color{ 40, 40, 48, 220 });
            struct PadArm { int ox, oy; const char *key; const char *arr; };
            PadArm arms[4] = { {s,0,"W","^"}, {0,s,"A","<"}, {2*s,s,"D",">"}, {s,2*s,"S","v"} };
            for (auto &a : arms) {
                int bx = dx + a.ox, by = dy + a.oy;
                DrawRectangle(bx, by, s, s, armBg);
                DrawRectangleLines(bx, by, s, s, LIGHTGRAY);
                DrawText(a.key, bx + s/2 - 6, by + 2, 18, WHITE);
                DrawText(a.arr, bx + s/2 - 5, by + 20, 13, LIGHTGRAY);
            }
            DrawText("Mouse aim", dx + 3*s + 12, dy + 2, 16, WHITE);
            DrawText("Click / Space: fire", dx + 3*s + 12, dy + 24, 16, WHITE);
            DrawText("1/2 ally  F follow  H hold", dx + 3*s + 12, dy + 46, 16, WHITE);
        }
        else {
            // D-pad cross in drone mode too: WASD flies, arrows rotate view.
            int dx = 16, dy = 36, s = 34;
            Color armBg = Color{ 60, 60, 70, 220 };
            DrawRectangle(dx + s, dy + s, s, s, Color{ 40, 40, 48, 220 });
            struct PadArmD { int ox, oy; const char *key; const char *arr; };
            PadArmD armsd[4] = { {s,0,"W","^"}, {0,s,"A","<"}, {2*s,s,"D",">"}, {s,2*s,"S","v"} };
            for (auto &a : armsd) {
                int bx = dx + a.ox, by = dy + a.oy;
                DrawRectangle(bx, by, s, s, armBg);
                DrawRectangleLines(bx, by, s, s, LIGHTGRAY);
                DrawText(a.key, bx + s/2 - 6, by + 2, 18, WHITE);
                DrawText(a.arr, bx + s/2 - 5, by + 20, 13, LIGHTGRAY);
            }
            DrawText("WASD: fly drone", dx + 3*s + 12, dy + 2, 16, WHITE);
            DrawText("Arrows: rotate view", dx + 3*s + 12, dy + 24, 16, WHITE);
            DrawText("Q / E: down / up   Shift: boost", dx + 3*s + 12, dy + 46, 16, WHITE);
        }
        if (mode == CamMode::DRONE) {
            // Virtual cursor crosshair.
            float cx = droneCursor.x, cy = droneCursor.y;
            DrawLine((int)cx - 12, (int)cy, (int)cx + 12, (int)cy, WHITE);
            DrawLine((int)cx, (int)cy - 12, (int)cx, (int)cy + 12, WHITE);
            DrawCircleLines((int)cx, (int)cy, 6.0f, WHITE);
            // Reload bar under the cursor.
            {
                float frac = (phase == Phase::COMBAT && cfg.shellCooldown > 0.0f)
                    ? Clamp(1.0f - fireCooldown / cfg.shellCooldown, 0.0f, 1.0f) : 1.0f;
                int bw = 80;
                DrawRectangle((int)cx - bw/2, (int)cy + 20, bw, 6, Color{ 0, 0, 0, 140 });
                DrawRectangle((int)cx - bw/2, (int)cy + 20, (int)(bw * frac), 6,
                              frac >= 1.0f ? GREEN : ORANGE);
            }
        }
        int aliveCount = 0;
        for (const auto &e : enemies) if (e.alive) ++aliveCount;
        // Status block top: below the D-pad in both modes.
        int sy = 150;
        // Enemies: tank icon + xN.
        {
            DrawRectangle(16, sy, 26, 18, cfg.enemyColor);
            DrawRectangle(16 + 11, sy - 7, 4, 12, DARKGRAY);  // barrel
            const char *lbl = (phase == Phase::SETUP)
                ? TextFormat("x %d (hidden)", aliveCount)
                : TextFormat("x %d", aliveCount);
            DrawText(lbl, 50, sy - 2, 20, RED);
        }
        // Hull: heart + xN.
        {
            int hx = 16, hy = sy + 26;
            // Two lobes + triangle point, overlapped so they read as one heart.
            DrawCircle(hx + 8, hy + 8, 8, RED);
            DrawCircle(hx + 18, hy + 8, 8, RED);
            DrawTriangle(Vector2{ (float)hx + 1, (float)hy + 8 },
                         Vector2{ (float)hx + 25, (float)hy + 8 },
                         Vector2{ (float)hx + 13, (float)hy + 29 }, RED);
            DrawText(TextFormat("x %d", tank.hp), hx + 32, hy + 4, 20,
                     tank.hp > 1 ? DARKGREEN : RED);
        }
        // Kills + battle timer (combat only).
        if (phase == Phase::COMBAT) {
            int destroyed = cfg.enemyCount - aliveCount;
            DrawText(TextFormat("Kills: %d", destroyed), 16, sy + 52, 20, DARKGRAY);
            if (battleStart > 0.0) {
                int secs = (int)(GetTime() - battleStart);
                DrawText(TextFormat("Time %d:%02d", secs / 60, secs % 60),
                         120, sy + 52, 20, DARKGRAY);
            }
        }
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
                     16, sy + 78 + (int)i * 22, 18, c);
        }
        if (mode == CamMode::DRONE) {
            int y0 = sy + 78 + (int)allies.size() * 22 + 6;
            DrawText("1/2 select ally   Right-click: move / attack   F follow   H hold", 16, y0, 18, DARKBLUE);
            DrawText("Arrows rotate view   WASD/QE move drone", 16, y0 + 22, 18, DARKBLUE);
        }
        // SETUP overlay: position forces, see the map, then start the battle.
        if (phase == Phase::SETUP) {
            int pw = 540, ph = 260;
            int px = (screenWidth - pw) / 2, py = (screenHeight - ph) / 15;
            DrawRectangle(px, py, pw, ph, Color{ 10, 10, 20, 220 });
            DrawRectangleLines(px, py, pw, ph, SKYBLUE);
            int tx = px + 24, ty = py + 20;
            DrawText("SETUP PHASE", tx, ty, 30, SKYBLUE);
            ty += 42;
            DrawText(TextFormat("Map: %s   (M to change)", maps[mapIdx].name.c_str()),
                     tx, ty, 20, YELLOW);
            ty += 30;
            DrawText(TextFormat("Enemy armor inbound: %d tanks (positions unknown)", cfg.enemyCount),
                     tx, ty, 20, RED);
            ty += 34;
            DrawText("Position your forces before the battle begins:", tx, ty, 20, WHITE);
            ty += 26;
            if (mode == CamMode::DRONE) {
                DrawText("- Move the mouse to aim the cursor", tx, ty, 18, LIGHTGRAY);
                ty += 26;
                DrawText("- Right-click ground: send selected ally there", tx, ty, 18, LIGHTGRAY);
                ty += 26;
                DrawText("- Right-click enemy: order ally to attack it", tx, ty, 18, LIGHTGRAY);
                ty += 26;
                DrawText("- 1/2 select ally,  F follow,  H hold position", tx, ty, 18, LIGHTGRAY);
                // ty += 26;
                // DrawText("- Arrows rotate drone,  WASD/QE move drone", tx, ty, 18, LIGHTGRAY);
                ty += 36;
            }
            if ((frameCount / 30) % 2 == 0)
                DrawText("Press ENTER to start the battle (gunner view)", tx, ty, 22, GREEN);
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
        // Shared end-of-battle stats lines.
        auto battleStats = [&](int tx, int ty, int lh, Color c) {
            int secs = battleStart > 0.0 ? (int)(battleEnd > 0.0 ? (int)(battleEnd - battleStart) : 0) : 0 ;
            int destroyed = cfg.enemyCount - aliveCount;
            float acc = shotsFired > 0 ? 100.0f * shotsHit / shotsFired : 0.0f;
            DrawText(TextFormat("Time  %d:%02d", secs / 60, secs % 60), tx, ty, 20, c);
            DrawText(TextFormat("Enemies destroyed  %d / %d", destroyed, cfg.enemyCount),
                     tx, ty + lh, 20, c);
            DrawText(TextFormat("Your kills  %d", playerKills), tx, ty + lh * 2, 20, c);
            DrawText(TextFormat("Shots  %d   Accuracy  %.0f%%", shotsFired, acc),
                     tx, ty + lh * 3, 20, c);
        };
        if (victory && !replaying) {
            // Victory panel mirrors the defeat panel: stats plus a rewatchable
            // replay of the battle's end. The tour continues behind it.
            int pw = 380, ph = 300;
            int px = (screenWidth - pw) / 2, py = (screenHeight - ph) / 15;
            DrawRectangle(px, py, pw, ph, Color{ 8, 20, 12, 235 });
            DrawRectangleLines(px, py, pw, ph, GREEN);
            int tx = px + 30, ty = py + 24;
            DrawText("VICTORY", tx, ty, 40, GREEN);
            DrawText("Arena cleared! (free tour)", tx, ty + 52, 20, LIGHTGRAY);
            battleStats(tx, ty + 88, 28, WHITE);
            // Offer the replay only while the victory footage is still buffered.
            bool canReplay = false;
            if (vreplayAvail && !replayBuf.empty()) {
                float to = fminf(replayBuf.back().time, victoryTime + cfg.replayPostSeconds);
                float from = fmaxf(replayBuf.front().time, victoryTime - cfg.replayDuration);
                canReplay = (to > from + 1.0f);
            }
            if (((int)(GetTime() * 2.0) % 2) == 0) {
                int hy = ty + 88 + 28 * 4 + 8;
                if (canReplay) {
                    DrawText("R - watch replay", tx, hy, 22, GREEN);
                    hy += 30;
                }
                DrawText("ENTER - new mission", tx, hy, 22, YELLOW);
            }
        } else if (gameOver && !replaying && !replayArmed) {
            // Integrated defeat panel: battle stats plus the replay, which
            // can be rewatched any number of times.
            int pw = 380, ph = 300;
            int px = (screenWidth - pw) / 2, py = (screenHeight - ph) / 15;
            DrawRectangle(0, 0, screenWidth, screenHeight, Color{ 0, 0, 0, 150 });
            DrawRectangle(px, py, pw, ph, Color{ 20, 8, 8, 235 });
            DrawRectangleLines(px, py, pw, ph, RED);
            int tx = px + 30, ty = py + 24;
            DrawText("DEFEAT", tx, ty, 40, RED);
            DrawText("Your tank was destroyed.", tx, ty + 52, 20, LIGHTGRAY);
            battleStats(tx, ty + 88, 28, WHITE);
            bool canReplay = cfg.replayEnabled && replayBuf.size() >= 30;
            if (((int)(GetTime() * 2.0) % 2) == 0) {
                int hy = ty + 88 + 28 * 4 + 8;
                if (canReplay) {
                    DrawText("R - watch replay", tx, hy, 22, GREEN);
                    hy += 30;
                }
                DrawText("ENTER - retry", tx, hy, 22, YELLOW);
            }
        }
        if (mode == CamMode::GUNNER) {
            // Crosshair
            int cx = screenWidth / 2, cy = screenHeight / 2;
            DrawLine(cx - 12, cy, cx + 12, cy, RED);
            DrawLine(cx, cy - 12, cx, cy + 12, RED);
            // Reload bar under the gun.
            {
                float frac = (phase == Phase::COMBAT && cfg.shellCooldown > 0.0f)
                    ? Clamp(1.0f - fireCooldown / cfg.shellCooldown, 0.0f, 1.0f) : 1.0f;
                int bw = 110;
                DrawRectangle(cx - bw/2, cy + 22, bw, 7, Color{ 0, 0, 0, 140 });
                DrawRectangle(cx - bw/2, cy + 22, (int)(bw * frac), 7,
                              frac >= 1.0f ? GREEN : ORANGE);
            }

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
        } 
        DrawFPS(screenWidth - 90, 12);

        EndDrawing();
    }

    UnloadRenderTexture(minimapTarget);
    UnloadRenderTexture(chaseTarget);
    CloseWindow();
    return 0;
}
