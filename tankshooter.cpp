// tankshooter.cpp — v0.1 prototype
//
// Cover-based tank shooter: drivable player tank, broken-down village arena,
// gunner (first-person) and drone (free-fly) camera modes.
//
// Controls:
//   W/S drive, A/D turn hull, mouse aims turret (gunner) / looks (drone),
//   TAB toggles drone <-> gunner, Q/E drone down/up, ESC quits.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <vector>

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
struct Building {
    Vector3 center;   // center of the box
    Vector3 size;     // full extents
    Color   color;
};

// Deterministic broken-down village: a street grid with buildings of random
// height, some ruined (half height), plus rubble piles and broken walls.
static std::vector<Building> BuildVillage() {
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

    return out;
}

static void DrawVillage(const std::vector<Building> &village) {
    for (const Building &b : village) {
        DrawCube(b.center, b.size.x, b.size.y, b.size.z, b.color);
        DrawCubeWires(b.center, b.size.x, b.size.y, b.size.z, Color{ 0, 0, 0, 60 });
        // Darker "roof" cap so buildings read as 3D from the drone.
        DrawCube(Vector3{ b.center.x, b.size.y + 0.05f, b.center.z },
                 b.size.x * 0.98f, 0.1f, b.size.z * 0.98f,
                 Color{ 70, 62, 55, 255 });
    }
}

// Push a circle (tank) out of every building AABB it overlaps, XZ plane.
static void ResolveBuildingCollisions(Vector3 &pos, const std::vector<Building> &village) {
    for (const Building &b : village) {
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
};

static Vector3 TurretWorldPos(const Tank &t) {
    return Vector3{ t.pos.x, 1.9f, t.pos.z };
}

static Vector3 TurretForward(const Tank &t) {
    float a = t.hullAngle + t.turretAngle;
    return Vector3{ sinf(a), 0.0f, -cosf(a) };  // 0 rad faces -Z
}

static void UpdateTank(Tank &t, const std::vector<Building> &village, float dt, bool allowDrive) {
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

    if (throttle > 0.0f)      t.speed = fminf(t.speed + ACCEL * dt, MAX_SPEED);
    else if (throttle < 0.0f) t.speed = fmaxf(t.speed - ACCEL * dt, MAX_REVERSE);
    else {
        // Engine braking.
        if (t.speed > 0.0f)      t.speed = fmaxf(t.speed - ACCEL * 1.5f * dt, 0.0f);
        else if (t.speed < 0.0f) t.speed = fminf(t.speed + ACCEL * 1.5f * dt, 0.0f);
    }

    // Tanks can pivot in place; scale a little with speed for feel.
    float turnAuthority = 0.55f + 0.45f * fminf(fabsf(t.speed) / MAX_SPEED, 1.0f);
    t.hullAngle += steer * TURN_RATE * turnAuthority * dt * (t.speed < 0.0f ? -1.0f : 1.0f);

    Vector3 fwd = { sinf(t.hullAngle), 0.0f, -cosf(t.hullAngle) };
    t.pos.x += fwd.x * t.speed * dt;
    t.pos.z += fwd.z * t.speed * dt;

    ResolveBuildingCollisions(t.pos, village);
}

static void DrawTank(const Tank &t, bool gunnerView) {
    // Hull
    Vector3 fwd = { sinf(t.hullAngle), 0.0f, -cosf(t.hullAngle) };
    Vector3 right = { -fwd.z, 0.0f, fwd.x };
    Vector3 hullC = { t.pos.x, 0.75f, t.pos.z };

    rlPushMatrix();
    rlTranslatef(hullC.x, hullC.y, hullC.z);
    // NOTE: negated — rlRotatef(+a) about Y turns local -Z toward -X, but our
    // angle convention faces (sin a, 0, -cos a), i.e. toward +X for a > 0.
    rlRotatef(-t.hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawCube(Vector3{ 0, 0, 0 }, 3.2f, 1.1f, 4.6f, Color{ 74, 94, 62, 255 });       // hull
    DrawCube(Vector3{ 0, 0.75f, -0.4f }, 2.4f, 0.5f, 2.6f, Color{ 84, 106, 70, 255 }); // upper deck
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
    // Turret (rotates relative to hull)
    rlRotatef(-t.turretAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // Barrel sits forward of the turret ring and below the gunner camera's
    // sight line, so the in-turret camera looks over it instead of into its
    // breech — the gun stays visible, glued under the crosshair.
    DrawCube(Vector3{ 0, 1.5f, -2.3f }, 0.3f, 0.3f, 2.0f, Color{ 50, 52, 48, 255 }); // barrel
    // In gunner view the turret is drawn later as a translucent ghost
    // (DrawTurretGhost, after the village) so the camera inside it can see
    // the hull and the world through it. In drone view it is solid.
    if (!gunnerView) {
        // NOTE: DrawCylinder's position is its BASE: this spans y 1.9–2.6 world.
        DrawCylinder(Vector3{ 0, 1.15f, 0 }, 1.15f, 1.35f, 0.7f, 12, Color{ 74, 94, 62, 255 });
    }
    rlPopMatrix();

    // Heading whisker so turret direction is readable from the drone.
    // Hidden in gunner view where it would cross the camera.
    if (!gunnerView) {
        Vector3 tp = TurretWorldPos(t);
        Vector3 tf = TurretForward(t);
        DrawLine3D(tp, Vector3{ tp.x + tf.x * 8.0f, tp.y, tp.z + tf.z * 8.0f }, YELLOW);
    }
    (void)right;
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

static void UpdateDrone(DroneCam &d, Vector2 md, float dt) {
    d.yaw   += md.x * 0.003f;   // mouse right = look right
    d.pitch  = Clamp(d.pitch - md.y * 0.003f, -1.45f, 1.45f);

    Vector3 fwd = { sinf(d.yaw) * cosf(d.pitch), sinf(d.pitch), -cosf(d.yaw) * cosf(d.pitch) };
    Vector3 right = { -fwd.z, 0.0f, fwd.x };
    right = Vector3Normalize(right);

    float sp = 40.0f * (IsKeyDown(KEY_LEFT_SHIFT) ? 2.5f : 1.0f);
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    d.pos = Vector3Add(d.pos, Vector3Scale(fwd, sp * dt));
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  d.pos = Vector3Subtract(d.pos, Vector3Scale(fwd, sp * dt));
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  d.pos = Vector3Subtract(d.pos, Vector3Scale(right, sp * dt));
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) d.pos = Vector3Add(d.pos, Vector3Scale(right, sp * dt));
    if (IsKeyDown(KEY_Q)) d.pos.y -= sp * dt;
    if (IsKeyDown(KEY_E)) d.pos.y += sp * dt;
    d.pos.y = Clamp(d.pos.y, 2.0f, 150.0f);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    const int screenWidth = 1280, screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Tankshooter v0.1");
    SetTargetFPS(60);
    DisableCursor();

    std::vector<Building> village = BuildVillage();
    Tank tank;
    DroneCam drone;
    CamMode mode = CamMode::GUNNER;
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
            }
        }
        tabWasDown = tabDown;

        UpdateTank(tank, village, dt, mode == CamMode::GUNNER);

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
            UpdateDrone(drone, md, dt);
            Vector3 fwd = { sinf(drone.yaw) * cosf(drone.pitch), sinf(drone.pitch),
                            -cosf(drone.yaw) * cosf(drone.pitch) };
            camera.position = drone.pos;
            camera.target = Vector3Add(drone.pos, fwd);
        }

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // Ground: dusty plain with a street grid feel.
        DrawPlane(Vector3{ 0, 0, 0 }, Vector2{ ARENA_HALF * 2 + 40, ARENA_HALF * 2 + 40 },
                  Color{ 168, 148, 118, 255 });
        DrawGrid(40, 20.0f);
        DrawVillage(village);
        DrawTank(tank, mode == CamMode::GUNNER);
        // The ghost turret blends over the world, so it goes last.
        if (mode == CamMode::GUNNER) DrawTurretGhost(tank);
        EndMode3D();

        // HUD
        const char *modeName = (mode == CamMode::GUNNER) ? "GUNNER" : "DRONE";
        DrawText(TextFormat("TANKSHOOTER v0.1  [%s]  TAB to switch", modeName), 16, 12, 22, DARKGRAY);
        DrawText("W/S drive  A/D turn hull  Mouse aim  Arrows = WASD  ESC quit", 16, 40, 18, GRAY);
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
