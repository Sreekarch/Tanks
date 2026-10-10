// tank.cpp — PlayerTank and AlliedTank behavior + tank drawing.
#include "tank.h"

#include "walker.h"

#include <cmath>

Vector3 TurretWorldPos(const PlayerTank &t) {
    return Vector3{ t.pos.x, 1.9f, t.pos.z };
}

Vector3 TurretForward(const PlayerTank &t) {
    float a = t.hullAngle + t.turretAngle;
    return Vector3{ sinf(a), 0.0f, -cosf(a) };  // 0 rad faces -Z
}

Vector3 MuzzleWorldPos(const PlayerTank &t) {
    // Tip of the barrel, following the gun's elevation: 3.55 along the
    // pitched gun direction from the turret center (barrel height 2.25).
    float cp = cosf(t.aimPitch), sp = sinf(t.aimPitch);
    float dx = sinf(t.aimAngle) * cp, dz = -cosf(t.aimAngle) * cp;
    return Vector3{ t.pos.x + dx * 3.55f, 2.25f + sp * 3.55f, t.pos.z + dz * 3.55f };
}

void FireShell(const PlayerTank &t, std::vector<Shell> &shells, const Config &cfg) {
    float cp = cosf(t.aimPitch), sp = sinf(t.aimPitch);
    Vector3 dir = { sinf(t.aimAngle) * cp, sp, -cosf(t.aimAngle) * cp };
    Vector3 muzzle = MuzzleWorldPos(t);
    Shell s;
    s.pos = muzzle;
    s.vel = Vector3Scale(dir, cfg.shellSpeed);
    s.life = cfg.shellLifetime;
    s.fromPlayer = true;
    s.trail[0] = muzzle;
    s.trailCount = 1;
    shells.push_back(s);
}

// Tank drawing helpers (this file only).
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

static void DrawTankBarrel(const Vector3 &center, float totalAngle, float pitch,
                           float alpha = 1.0f) {
    unsigned char a = (unsigned char)(255.0f * fmaxf(0.0f, fminf(1.0f, alpha)));
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);  // elevate the muzzle
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

static void DrawTankTurret(const Vector3 &center, float totalAngle, Color armor,
                           float alpha = 1.0f, float pitch = 0.0f) {
    unsigned char a = (unsigned char)(255.0f * fmaxf(0.0f, fminf(1.0f, alpha)));
    Color body = armor; body.a = a;
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    rlRotatef(-totalAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    // NOTE: DrawCylinder's position is its BASE: base at -0.35 puts the
    // 0.7-tall cylinder spanning y -0.35..+0.35 around the center.
    DrawCylinder(Vector3{ 0, -0.35f, 0 }, 1.15f, 1.35f, 0.7f, 12, body);
    rlPopMatrix();
    DrawTankBarrel(center, totalAngle, pitch, alpha);
}

void DrawTurretGhost(const PlayerTank &t) {
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

void PlayerTank::update(float dt, const InputState &in, const GameCtx &g, bool allowDrive) {
    // Driving input only counts in gunner mode; in drone mode the tank
    // just rolls to a stop instead of shadowing the drone keys.
    float throttle = allowDrive ? in.throttle : 0.0f;
    float steer = allowDrive ? in.steer : 0.0f;

    // Ditches sap drive power.
    float dm = DitchDepthAt(pos.x, pos.z, g.ditches, g.bridges) > 0.0f
        ? g.cfg.ditchSlowFactor : 1.0f;
    float effMax = g.cfg.playerSpeed * dm;
    if (throttle > 0.0f)      speed = fminf(speed + ACCEL * dt, effMax);
    else if (throttle < 0.0f) speed = fmaxf(speed - ACCEL * dt, MAX_REVERSE * dm);
    else {
        // Engine braking.
        if (speed > 0.0f)      speed = fmaxf(speed - ACCEL * 1.5f * dt, 0.0f);
        else if (speed < 0.0f) speed = fminf(speed + ACCEL * 1.5f * dt, 0.0f);
    }

    // Tanks can pivot in place; scale a little with speed for feel.
    float turnAuthority = 0.55f + 0.45f * fminf(fabsf(speed) / g.cfg.playerSpeed, 1.0f);
    hullAngle += steer * TURN_RATE * turnAuthority * dt * (speed < 0.0f ? -1.0f : 1.0f);

    Vector3 fwd = { sinf(hullAngle), 0.0f, -cosf(hullAngle) };
    pos.x += fwd.x * speed * dt;
    pos.z += fwd.z * speed * dt;

    ResolveBuildingCollisions(pos, g.village);
    if (hitFlashT > 0.0f) hitFlashT -= dt;
}

void PlayerTank::draw(bool gunnerView) const {
    static const Color PLAYER_ARMOR = { 74, 94, 62, 255 };
    if (!gunnerView) DrawBlobShadow(pos, 3.4f, 100);
    DrawTankHull(pos, hullAngle, PLAYER_ARMOR);
    Vector3 tc = { pos.x, 2.25f, pos.z };
    float ta = hullAngle + turretAngle;
    if (!gunnerView) {
        // Solid turret in drone view.
        DrawTankTurret(tc, ta, PLAYER_ARMOR, 1.0f, aimPitch);
    } else {
        // Gunner view: solid barrel now, translucent turret body later
        // (DrawTurretGhost) so the camera inside can see through it.
        DrawTankBarrel(tc, ta, aimPitch);
    }

    // Heading whisker so turret direction is readable from the drone.
    // Hidden in gunner view where it would cross the camera.
    if (!gunnerView) {
        Vector3 tp = TurretWorldPos(*this);
        Vector3 tf = TurretForward(*this);
        DrawLine3D(tp, Vector3{ tp.x + tf.x * 8.0f, tp.y, tp.z + tf.z * 8.0f }, YELLOW);
    }
}

void DamageAlly(AlliedTank &a, std::vector<Particle> &particles, std::vector<Flash> &flashes) {
    if (!a.alive) return;
    a.hitFlashT = 0.18f;
    if (--a.hp > 0) return;
    KillUnit(a, particles, flashes);
}

std::vector<AlliedTank> SpawnAllies(int count, int hits, const Vector3 &playerPos) {
    std::vector<AlliedTank> out;
    for (int i = 0; i < count; ++i) {
        AlliedTank a;
        // Echelon left: behind and to the side of the player.
        a.pos = { playerPos.x - 8.0f - (float)i * 7.0f, 0.0f, playerPos.z + 6.0f + (float)i * 4.0f };
        a.hullAngle = 0.0f;
        a.hp = a.maxHp = hits;
        a.order = AllyOrder::FOLLOW;
        out.push_back(a);
    }
    return out;
}

void AlliedTank::driveToward(float wantAngle, float speed, const GameCtx &g) {
    float diff = NormalizeAngle(wantAngle - hullAngle);
    hullAngle += Clamp(diff * 3.0f, -1.6f, 1.6f) * g.dt;
    float throttle = (fabsf(diff) < 0.6f) ? 1.0f : 0.25f;
    float dm = DitchDepthAt(pos.x, pos.z, g.ditches, g.bridges) > 0.0f
        ? g.cfg.ditchSlowFactor : 1.0f;
    pos.x += sinf(hullAngle) * speed * throttle * g.dt * dm;
    pos.z += -cosf(hullAngle) * speed * throttle * g.dt * dm;
}

void AlliedTank::update(float dt, GameCtx &g, size_t index) {
    if (!alive) {
        UpdateWreckAnim(*this, g.particles, dt);
        return;
    }
    if (hitFlashT > 0.0f) hitFlashT -= dt;
    fireTimer += dt;

    const Config &cfg = g.cfg;
    const PlayerTank &player = g.player;
    // Pick a target: explicit ATTACK order, else nearest visible in range.
    // No engagement during SETUP (enemies are hidden).
    int tgt = -1;
    if (order == AllyOrder::ATTACK && targetEnemy >= 0 &&
        targetEnemy < (int)g.walkers.size() && g.walkers[targetEnemy]->alive) {
        tgt = targetEnemy;
    } else if (g.inCombat && cfg.allyEngage) {
        float bestD2 = cfg.allyRange * cfg.allyRange;
        Vector3 eye = { pos.x, 2.25f, pos.z };
        for (size_t ei = 0; ei < g.walkers.size(); ++ei) {
            if (!g.walkers[ei]->alive) continue;
            float dx = g.walkers[ei]->pos.x - pos.x, dz = g.walkers[ei]->pos.z - pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 > bestD2) continue;
            Vector3 et = { g.walkers[ei]->pos.x, 2.0f, g.walkers[ei]->pos.z };
            if (LosBlocked(eye, et, g.village)) continue;
            bestD2 = d2; tgt = (int)ei;
        }
    }
    engageIdx = tgt;

    if (tgt >= 0) {
        // Engaging: turret tracks, fire when locked.
        Walker &e = *g.walkers[tgt];
        float dx = e.pos.x - pos.x, dz = e.pos.z - pos.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float wantWorld = atan2f(dx, -dz);
        float wantTurret = NormalizeAngle(wantWorld - hullAngle);
        float tdiff = NormalizeAngle(wantTurret - turretAngle);
        turretAngle += Clamp(tdiff * 4.0f, -2.5f, 2.5f) * dt;
        Vector3 eye = { pos.x, 2.25f, pos.z };
        Vector3 et = { e.pos.x, 2.0f, e.pos.z };
        if (!LosBlocked(eye, et, g.village) && dist < cfg.allyRange) {
            aimTimer += dt;
            if (aimTimer >= cfg.allyAimTime && fireTimer >= cfg.allyFireInt) {
                float spread = ((float)GetRandomValue(-100, 100) / 100.0f) * cfg.allySpread;
                float fa = hullAngle + turretAngle + spread;
                Vector3 fwd = { sinf(fa), 0.0f, -cosf(fa) };
                Vector3 muzzle = { pos.x + fwd.x * 3.55f, 2.25f, pos.z + fwd.z * 3.55f };
                Shell s;
                s.pos = muzzle;
                s.vel = Vector3Scale(fwd, cfg.shellSpeed);
                s.life = cfg.shellLifetime;
                s.fromEnemy = false;  // hits enemies, not the player
                g.shells.push_back(s);
                g.flashes.push_back(Flash{ muzzle, 0.22f, 0.22f, 0.9f });
                fireTimer = 0.0f;
                aimTimer = 0.0f;
            }
        } else {
            aimTimer = 0.0f;
        }
        // ATTACK order closes distance; otherwise hold while shooting.
        if (order == AllyOrder::ATTACK && dist > cfg.allyRange * 0.7f) {
            float steer = wantWorld;
            if (g.nav) {
                float a;
                if (navPath.steer(*g.nav, pos, e.pos, dt,
                                  cfg.pathRepathSeconds, a)) steer = a;
            }
            driveToward(steer, cfg.allySpeed, g);
        }
    } else {
        // No target: follow orders.
        aimTimer = 0.0f;
        turretAngle = NormalizeAngle(turretAngle - turretAngle * fminf(dt * 2.0f, 1.0f));
        Vector3 dest = pos;
        bool move = false;
        if (order == AllyOrder::FOLLOW) {
            float pa = player.hullAngle;
            Vector3 fwd = { sinf(pa), 0.0f, -cosf(pa) };
            Vector3 right = { -fwd.z, 0.0f, fwd.x };
            float side = (g.allies.size() <= 2) ? (index == 0 ? -7.0f : 7.0f)
                              : ((float)index - (float)(g.allies.size() - 1) / 2.0f) * 7.0f;
            dest = { player.pos.x - fwd.x * 10.0f + right.x * side, 0.0f,
                     player.pos.z - fwd.z * 10.0f + right.z * side };
            move = true;
        } else if (order == AllyOrder::MOVE) {
            dest = orderPos;
            move = true;
        }
        if (move) {
            float dx = dest.x - pos.x, dz = dest.z - pos.z;
            if (dx * dx + dz * dz > 9.0f) {
                // Pathfinding (if enabled) routes around buildings;
                // otherwise steer straight at the destination.
                float steer = atan2f(dx, -dz);
                if (g.nav) {
                    float a;
                    if (navPath.steer(*g.nav, pos, dest, dt,
                                      cfg.pathRepathSeconds, a)) steer = a;
                }
                driveToward(steer, cfg.allySpeed, g);
            } else if (order == AllyOrder::MOVE) {
                order = AllyOrder::HOLD;  // arrived
            }
        }
    }

    ResolveBuildingCollisions(pos, g.village);
}

void AlliedTank::draw(const Config &cfg, bool isSelected,
                      const std::vector<std::unique_ptr<Walker>> &walkers) const {
    static const Color CHARRED = { 38, 33, 28, 255 };
    if (cfg.pathDrawPaths && alive)
        navPath.draw(pos, Color{ 100, 190, 255, 220 });  // blue route
    if (alive) {
        Color armor = cfg.allyColor;
        if (hitFlashT > 0.0f) armor = Color{ 255, 240, 230, 255 };
        DrawBlobShadow(pos, 3.4f, 100);
        DrawTankHull(pos, hullAngle, armor);
        DrawTankTurret(Vector3{ pos.x, 2.25f, pos.z },
                       hullAngle + turretAngle, armor);
        // Selected: white pulsing ring.
        if (isSelected) {
            float pulse = 2.8f + sinf((float)GetTime() * 6.0f) * 0.4f;
            DrawCylinderWires(Vector3{ pos.x, 0.08f, pos.z },
                              pulse, pulse, 0.12f, 24, Color{ 255, 255, 255, 230 });
        }
        // Objective marker.
        if (order == AllyOrder::MOVE) {
            // Light pillar + pulsing ring + line from the ally.
            DrawCylinder(Vector3{ orderPos.x, 5.0f, orderPos.z },
                         0.3f, 0.3f, 10.0f, 12, Color{ 80, 160, 255, 90 });
            float pulse = 1.5f + sinf((float)GetTime() * 5.0f) * 0.3f;
            DrawCylinderWires(Vector3{ orderPos.x, 0.08f, orderPos.z },
                              pulse, pulse, 0.12f, 16, Color{ 80, 160, 255, 230 });
            DrawLine3D(Vector3{ pos.x, 0.5f, pos.z },
                       Vector3{ orderPos.x, 0.5f, orderPos.z },
                       Color{ 80, 160, 255, 150 });
        } else if (order == AllyOrder::ATTACK && targetEnemy >= 0 &&
                   targetEnemy < (int)walkers.size() && walkers[targetEnemy]->alive) {
            const auto &e = walkers[targetEnemy];
            DrawCylinderWires(Vector3{ e->pos.x, 0.08f, e->pos.z },
                              3.2f, 3.2f, 0.12f, 24, Color{ 255, 80, 80, 230 });
        }
    } else {
        DrawTankHull(pos, hullAngle, CHARRED);
        DrawTankTurret(turretPos, turretSpin, CHARRED);
        float f = 0.75f + 0.25f * sinf(deathT * 13.0f);
        DrawSphere(Vector3{ pos.x, 1.7f, pos.z }, 0.55f * f, Color{ 255, 120, 25, 210 });
    }
}

void DrawAllies(const std::vector<AlliedTank> &allies,
                       const std::vector<std::unique_ptr<Walker>> &walkers,
                       const Config &cfg, int selected) {
    for (size_t i = 0; i < allies.size(); ++i)
        allies[i].draw(cfg, (int)i == selected, walkers);
}
