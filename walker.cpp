// walker.cpp — Walker shared behavior and the Tripedal walker.
#include "walker.h"

#include "tank.h"

#include <cmath>

// Small draw helpers (this file only).
static Color WithAlpha(Color c, float a) {
    c.a = (unsigned char)(255.0f * Clamp(a, 0.0f, 1.0f));
    return c;
}

static void DrawLimb(const Vector3 &a, const Vector3 &b, float r, Color c) {
    DrawCylinderEx(a, b, r, r * 0.7f, 8, c);
}

void WalkerMuzzle(const Walker &e, float yaw, float pitch,
                         Vector3 *outPos, Vector3 *outDir) {
    float H = e.hullAngle + yaw;
    float sh = sinf(H), ch = cosf(H);
    float cp = cosf(pitch), sp = sinf(pitch);
    // Gun center in the hull-yawed frame, then the pitched muzzle offset.
    float mx = 0.0f, my = WALKER_GUN_DY + WALKER_BARREL * sp,
          mz = WALKER_GUN_FWD - WALKER_BARREL * cp;
    outPos->x = e.pos.x + mx * ch - mz * sh;
    outPos->y = WALKER_NECK_Y + my;
    outPos->z = e.pos.z + mx * sh + mz * ch;
    outDir->x = sh * cp;
    outDir->y = sp;
    outDir->z = -ch * cp;
}

// Walker AI helpers (this file only).
static Vector3 PickCover(const Walker &e, const Vector3 &playerPos,
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

void Tripedal::driveToward(float wantAngle, float speed, const GameCtx &g) {
    float diff = NormalizeAngle(wantAngle - hullAngle);
    hullAngle += Clamp(diff * 3.0f, -1.6f, 1.6f) * g.dt;
    float throttle = (fabsf(diff) < 0.6f) ? 1.0f : 0.25f;
    float dm = DitchDepthAt(pos.x, pos.z, g.ditches, g.bridges) > 0.0f
        ? g.cfg.ditchSlowFactor : 1.0f;
    pos.x += sinf(hullAngle) * speed * throttle * g.dt * dm;
    pos.z += -cosf(hullAngle) * speed * throttle * g.dt * dm;
    // Walkers animate their legs proportional to distance covered.
    walkPhase += speed * throttle * g.dt * dm * 1.1f;
}

void Tripedal::update(float dt, GameCtx &g) {
    if (!alive) return;
    aiTimer += dt;
    fireTimer += dt;
    if (hitFlashT > 0.0f) hitFlashT -= dt;
    // Hit-reaction timers decay regardless of state.
    if (staggerT > 0.0f) staggerT = fmaxf(0.0f, staggerT - dt);
    if (swayT > 0.0f) swayT = fmaxf(0.0f, swayT - dt);

    const PlayerTank &player = g.player;
    const Config &cfg = g.cfg;

    if (!stateInit) {
        stateInit = true;
        sweepSeed = (float)GetRandomValue(0, 628) * 0.01f;
        if (cfg.stealthEnabled) aiState = AIState::PATROL;
    }

    float dx = player.pos.x - pos.x, dz = player.pos.z - pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    // Tripedals aim the gun pod in elevation at the player's hull so the
    // taller walker can still make the shot.
    gunPitch = Clamp(atan2f(1.6f - WALKER_GUN_Y, dist), -0.35f, 0.6f);
    Vector3 eye = { pos.x, 3.2f, pos.z };
    Vector3 tgt = { player.pos.x, 2.0f, player.pos.z };
    bool los = !LosBlocked(eye, tgt, g.village);
    bool inRange = dist < cfg.enemyRange;

    // --- Perception: with stealth on, the walker only knows what its
    // searchlight cone sees; the detection meter gates the alarm. ---
    float headYaw = hullAngle + turretAngle;
    float angTo = atan2f(dx, -dz);
    bool inCone = fabsf(NormalizeAngle(angTo - headYaw)) <
                  cfg.visionHalfAngleDeg * (float)DEG2RAD;
    bool seeing = cfg.stealthEnabled
        ? (los && dist < cfg.visionRange && inCone)
        : los;
    bool engaged = aiState == AIState::ADVANCE || aiState == AIState::SHOOT ||
                   aiState == AIState::SEEK_COVER || aiState == AIState::COVER_WAIT;
    if (!cfg.stealthEnabled) {
        lastSeenPos = player.pos;  // legacy omniscience
        lastSeenAgo = 0.0f;
    } else if (seeing) {
        lastSeenPos = player.pos;
        lastSeenAgo = 0.0f;
        if (!engaged && aiState != AIState::ALARM && aiState != AIState::STAGGER) {
            float prox = Clamp(1.6f - dist / cfg.visionRange, 0.5f, 1.5f);
            detect += dt * prox / cfg.detectSeconds;
            if (detect > 0.35f && aiState == AIState::PATROL) {
                stimulusPos = player.pos;  // a glimpse: go and look
                aiState = AIState::INVESTIGATE;
                searchPhase = false;
                aiTimer = 0.0f;
            }
        }
    } else {
        lastSeenAgo += dt;
        if (!engaged && aiState != AIState::ALARM)
            detect = fmaxf(0.0f, detect - dt / cfg.forgetSeconds);
    }
    detect = Clamp(detect, 0.0f, 1.0f);
    if (cfg.stealthEnabled && seeing && detect >= 1.0f && !engaged &&
        aiState != AIState::ALARM && aiState != AIState::STAGGER) {
        // Spotted: raise the alarm (the call is the telegraph; nearby
        // walkers are pulled in when it completes).
        aiState = AIState::ALARM;
        aiTimer = 0.0f;
        alarmT = 0.0f;
    }

    // Fresh gunfire pulls unaware walkers toward the sound.
    if (cfg.stealthEnabled && g.noiseAge < 0.05f && !engaged &&
        aiState != AIState::ALARM && aiState != AIState::STAGGER) {
        float ndx = g.noisePos.x - pos.x, ndz = g.noisePos.z - pos.z;
        if (ndx * ndx + ndz * ndz < cfg.noiseRadius * cfg.noiseRadius) {
            stimulusPos = g.noisePos;
            aiState = AIState::INVESTIGATE;
            searchPhase = false;
            aiTimer = 0.0f;
        }
    }

    // Where the walker believes the player is (live while seen).
    Vector3 goalPos = seeing ? player.pos : lastSeenPos;
    float gdx = goalPos.x - pos.x, gdz = goalPos.z - pos.z;
    float goalDist = sqrtf(gdx * gdx + gdz * gdz);

    // Steer along the nav path (or straight) toward a ground goal.
    auto moveTo = [&](const Vector3 &goal, float speed) {
        float steer = atan2f(goal.x - pos.x, -(goal.z - pos.z));
        if (g.nav) {
            float a;
            if (navPath.steer(*g.nav, pos, goal, dt,
                              cfg.pathRepathSeconds, a)) steer = a;
        }
        driveToward(steer, speed, g);
    };
    // Searchlight/head sweep while unaware.
    auto sweepHead = [&](float arc, float speed) {
        float target = sinf((float)GetTime() * speed + sweepSeed) * arc;
        float diff = NormalizeAngle(target - turretAngle);
        turretAngle += Clamp(diff * 3.0f, -2.2f, 2.2f) * dt;
    };

    switch (aiState) {
    case AIState::PATROL: {
        sweepHead(0.65f, 0.9f);
        if (patrolPauseT > 0.0f) {
            patrolPauseT -= dt;
            break;
        }
        if (!hasPatrolGoal) {
            // Auto-pick a patrol point worth walking to (not in a pit,
            // not inside a building).
            for (int tries = 0; tries < 8; ++tries) {
                Vector3 cand = {
                    (float)GetRandomValue(-(int)ARENA_HALF + 25, (int)ARENA_HALF - 25),
                    0.0f,
                    (float)GetRandomValue(-(int)ARENA_HALF + 25, (int)ARENA_HALF - 25) };
                float pdx = cand.x - pos.x, pdz = cand.z - pos.z;
                if (pdx * pdx + pdz * pdz < 60.0f * 60.0f) continue;
                bool bad = DitchDepthAt(cand.x, cand.z, g.ditches, g.bridges) > 0.0f;
                for (const auto &b : g.village) {
                    if (b.destroyed) continue;
                    if (fabsf(cand.x - b.center.x) < b.size.x * 0.5f + 3.0f &&
                        fabsf(cand.z - b.center.z) < b.size.z * 0.5f + 3.0f) { bad = true; break; }
                }
                patrolGoal = cand;
                hasPatrolGoal = true;
                if (!bad) break;
            }
        }
        if (hasPatrolGoal) {
            moveTo(patrolGoal, cfg.enemySpeed * cfg.patrolSpeedFactor);
            float pdx = patrolGoal.x - pos.x, pdz = patrolGoal.z - pos.z;
            if (pdx * pdx + pdz * pdz < 9.0f) {
                hasPatrolGoal = false;
                patrolPauseT = cfg.patrolPauseSeconds;  // stop and sweep
            }
        }
        break;
    }
    case AIState::INVESTIGATE: {
        if (!searchPhase) {
            sweepHead(0.3f, 1.2f);
            moveTo(stimulusPos, cfg.enemySpeed * 0.85f);
            float sdx = stimulusPos.x - pos.x, sdz = stimulusPos.z - pos.z;
            if (sdx * sdx + sdz * sdz < 12.25f) { searchPhase = true; aiTimer = 0.0f; }
        } else {
            sweepHead(1.0f, 1.5f);  // look around the stimulus point
            if (aiTimer >= cfg.searchSeconds) {
                aiState = AIState::PATROL;
                aiTimer = 0.0f;
                hasPatrolGoal = false;
            }
        }
        break;
    }
    case AIState::ALARM: {
        // Head snaps to the player and calls out; the broadcast lands
        // when the call completes (kill or break it first to stay hidden).
        float wantTurret = NormalizeAngle(atan2f(dx, -dz) - hullAngle);
        float tdiff = NormalizeAngle(wantTurret - turretAngle);
        turretAngle += Clamp(tdiff * 5.0f, -3.0f, 3.0f) * dt;
        alarmT += dt;
        if (alarmT >= cfg.alarmSeconds) {
            for (auto &w : g.walkers) {
                if (w.get() == this || !w->alive) continue;
                float ddx = w->pos.x - pos.x, ddz = w->pos.z - pos.z;
                if (ddx * ddx + ddz * ddz > cfg.alarmRadius * cfg.alarmRadius) continue;
                if (w->aiState == AIState::PATROL ||
                    w->aiState == AIState::INVESTIGATE ||
                    w->aiState == AIState::SEARCH) {
                    w->lastSeenPos = lastSeenPos;
                    w->lastSeenAgo = 0.0f;
                    w->aiState = AIState::ADVANCE;
                    w->aiTimer = 0.0f;
                }
            }
            aiState = AIState::ADVANCE;
            aiTimer = 0.0f;
        }
        break;
    }
    case AIState::ADVANCE: {
        // Engaged: hunt the live position while seen, else the last
        // known one; losing the trail drops to SEARCH.
        bool stopHere = seeing && inRange && dist < cfg.enemyRange * 0.7f;
        if (!stopHere) moveTo(goalPos, cfg.enemySpeed);
        // Turret relaxes toward hull-forward when not engaged.
        turretAngle = NormalizeAngle(turretAngle - turretAngle * fminf(dt * 2.0f, 1.0f));
        if (seeing && inRange) {
            aiState = AIState::SHOOT;
            aiTimer = 0.0f;
            aimTimer = 0.0f;
        } else if (!seeing && (goalDist < 3.5f || lastSeenAgo > cfg.memorySeconds)) {
            aiState = AIState::SEARCH;
            searchPhase = false;
            aiTimer = 0.0f;
        }
        break;
    }
    case AIState::SHOOT: {
        // Turret visibly tracks the player — the telegraph.
        float wantWorld = atan2f(dx, -dz);
        float wantTurret = NormalizeAngle(wantWorld - hullAngle);
        float tdiff = NormalizeAngle(wantTurret - turretAngle);
        turretAngle += Clamp(tdiff * 4.0f, -2.5f, 2.5f) * dt;
        if (los && inRange) {
            aimTimer += dt;
            if (aimTimer >= cfg.enemyAimTime && fireTimer >= cfg.enemyFireInt) {
                float spread = ((float)GetRandomValue(-100, 100) / 100.0f) * cfg.enemySpread;
                // Walkers fire from the elevating ventral gun pod.
                Vector3 muzzle, mdir;
                WalkerMuzzle(*this, turretAngle + spread, gunPitch, &muzzle, &mdir);
                Shell s;
                s.pos = muzzle;
                s.vel = Vector3Scale(mdir, cfg.shellSpeed);
                s.life = cfg.shellLifetime;
                s.fromEnemy = true;
                g.shells.push_back(s);
                g.flashes.push_back(Flash{ muzzle, 0.22f, 0.22f, 0.9f });
                fireTimer = 0.0f;
                aimTimer = 0.0f;
            }
        } else {
            aimTimer = 0.0f;  // lost the lock
            aiState = AIState::ADVANCE;
            aiTimer = 0.0f;
        }
        break;
    }
    case AIState::SEARCH: {
        if (!searchPhase) {
            sweepHead(0.3f, 1.2f);
            moveTo(lastSeenPos, cfg.enemySpeed * 0.85f);
            float sdx = lastSeenPos.x - pos.x, sdz = lastSeenPos.z - pos.z;
            if (sdx * sdx + sdz * sdz < 12.25f) { searchPhase = true; aiTimer = 0.0f; }
        } else {
            sweepHead(1.1f, 1.3f);  // sweep the area where it lost you
            if (aiTimer >= cfg.searchSeconds) {
                aiState = AIState::PATROL;
                aiTimer = 0.0f;
                hasPatrolGoal = false;
                detect = 0.0f;
            }
        }
        break;
    }
    case AIState::SEEK_COVER: {
        float cdx = coverPos.x - pos.x, cdz = coverPos.z - pos.z;
        if (cdx * cdx + cdz * cdz < 9.0f) {
            aiState = AIState::COVER_WAIT;
            aiTimer = 0.0f;
        } else {
            float steer = atan2f(cdx, -cdz);
            if (g.nav) {
                float a;
                if (navPath.steer(*g.nav, pos, coverPos, dt,
                                  cfg.pathRepathSeconds, a)) steer = a;
            }
            driveToward(steer, cfg.enemySpeed, g);
        }
        break;
    }
    case AIState::COVER_WAIT: {
        if (aiTimer >= cfg.enemyCoverWait) {
            aiState = AIState::ADVANCE;
            aiTimer = 0.0f;
        }
        break;
    }
    case AIState::STAGGER: {
        // Knocked off balance: quick backpedaling steps away from the shot,
        // hull unturned, legs cycling fast — fighting to regain footing.
        // staggerT (decayed at the top of the update) is the state clock.
        float dm = DitchDepthAt(pos.x, pos.z, g.ditches, g.bridges) > 0.0f
            ? cfg.ditchSlowFactor : 1.0f;
        pos.x += staggerDir.x * cfg.staggerStepSpeed * dt * dm;
        pos.z += staggerDir.z * cfg.staggerStepSpeed * dt * dm;
        walkPhase += cfg.staggerStepSpeed * dt * dm * 2.0f;  // hurried steps
        ResolveBuildingCollisions(pos, g.village);
        if (staggerT <= 0.0f) {
            if (cfg.stealthEnabled && !staggerWasEngaged) {
                // Shot while unaware: turn toward where the shot came
                // from and go find out — no alarm until it sees you.
                aiState = AIState::INVESTIGATE;
                searchPhase = false;
            } else {
                // Footing regained: break off and seek cover.
                coverPos = PickCover(*this, player.pos, g.village);
                aiState = AIState::SEEK_COVER;
            }
            aiTimer = 0.0f;
            aimTimer = 0.0f;
        }
        break;
    }
    }

    ResolveBuildingCollisions(pos, g.village);
}

void Walker::damage(GameCtx &g, const Vector3 &hitDir) {
    if (!alive) return;
    hitFlashT = 0.18f;
    if (--hp > 0) {
        // Nonlethal hit: the head/turret cranes skywards and away from the
        // incoming shot while the body stumbles back (STAGGER state); when
        // the walker regains its footing it breaks off to cover.
        staggerT = g.cfg.staggerSeconds;
        swayT = g.cfg.swaySeconds;
        float hl = sqrtf(hitDir.x * hitDir.x + hitDir.z * hitDir.z);
        staggerDir = (hl > 0.001f) ? Vector3{ hitDir.x / hl, 0.0f, hitDir.z / hl }
                                   : Vector3{ 0.0f, 0.0f, 0.0f };
        // Stealth: remember whether it was already fighting, and where
        // the shot came from (it must look there and find the shooter
        // itself before it can raise any alarm).
        staggerWasEngaged = (aiState == AIState::ADVANCE || aiState == AIState::SHOOT ||
                             aiState == AIState::SEEK_COVER || aiState == AIState::COVER_WAIT);
        stimulusPos = { pos.x - staggerDir.x * 45.0f, 0.0f,
                        pos.z - staggerDir.z * 45.0f };
        float hy = hullAngle + turretAngle;
        // Skyward crane: shots come from below, so the head rears up hard.
        swayPitchAmp = 0.85f;
        // Lateral tilt away from the shot.
        float hrx = -cosf(hy), hrz = -sinf(hy);  // head right
        float side = staggerDir.x * hrx + staggerDir.z * hrz;
        swayRollAmp = side * 0.5f;
        // Yaw flinch away from the shot.
        float awayYaw = atan2f(staggerDir.x, -staggerDir.z);
        swayYawAmp = Clamp(NormalizeAngle(awayYaw - hy), -0.7f, 0.7f);
        aiState = AIState::STAGGER;
        aiTimer = 0.0f;
        aimTimer = 0.0f;
        return;
    }
    // Kill: fireball flash, flame + smoke burst, head/turret pops off.
    alive = false;
    deathT = 0.0f;
    float burstY = (kind() == WalkerKind::TRIPEDAL) ? 3.8f : 1.6f;
    Vector3 c = { pos.x, burstY, pos.z };
    g.flashes.push_back(Flash{ c, 0.55f, 0.55f, 3.8f });
    Burst(g.particles, c, 10, Color{ 255, 150, 40, 255 }, 9.0f, 7.0f, 0.9f, 0.7f, 6.0f);   // flames
    Burst(g.particles, c, 12, Color{ 90, 85, 80, 255 }, 4.0f, 9.0f, 1.4f, 2.6f, -3.0f);    // smoke
    Burst(g.particles, c, 6, Color{ 255, 220, 120, 255 }, 14.0f, 5.0f, 0.5f, 0.4f, 10.0f);  // sparks
    float headY = (kind() == WalkerKind::TRIPEDAL) ? WALKER_NECK_Y + 0.6f : 2.25f;
    turretPos = { pos.x, headY, pos.z };
    // Walkers keel over around a random horizontal axis as they die.
    float fa = (float)GetRandomValue(0, 360) * DEG2RAD;
    fallAxis = { cosf(fa), 0.0f, sinf(fa) };
    // Strong outward pop: guaranteed to clear the hull (half-diagonal ~2.8
    // + turret radius 1.35) so the barrel doesn't end up inside the wreck.
    float popA = (float)GetRandomValue(0, 360) * DEG2RAD;
    float popS = (float)GetRandomValue(50, 80) / 10.0f;
    turretVel = { cosf(popA) * popS,
                  (float)GetRandomValue(75, 115) / 10.0f,
                  sinf(popA) * popS };
    turretSpinVel = (float)GetRandomValue(-9, 9);
    turretLanded = false;
    burnAccum = 0.0f;
}

void Walker::updateDeath(std::vector<Particle> &particles, float dt) {
    if (hitFlashT > 0.0f) hitFlashT -= dt;
    if (alive) return;
    deathT += dt;
    // Popped turret: ballistic arc, then rests where it lands.
    if (!turretLanded) {
        turretVel.y -= 22.0f * dt;
        turretPos = Vector3Add(turretPos, Vector3Scale(turretVel, dt));
        turretSpin += turretSpinVel * dt;
        if (turretPos.y <= 0.55f) {
            turretPos.y = 0.55f;
            // Safety: never rest inside the hull wreck — push out so the
            // barrel stays visible instead of buried in the hull.
            float dx = turretPos.x - pos.x, dz = turretPos.z - pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 4.5f * 4.5f) {
                float d = sqrtf(d2);
                if (d < 1e-3f) { dx = 1.0f; dz = 0.0f; d = 1.0f; }
                turretPos.x = pos.x + dx / d * 4.5f;
                turretPos.z = pos.z + dz / d * 4.5f;
            }
            turretLanded = true;
        }
    }
    // Persistent burn: flame flicker + rising smoke wisps.
    burnAccum += dt;
    if (burnAccum >= 0.22f) {
        burnAccum = 0.0f;
        Vector3 c = { pos.x + (float)GetRandomValue(-10, 10) / 10.0f, 1.4f,
                      pos.z + (float)GetRandomValue(-10, 10) / 10.0f };
        Burst(particles, c, 1, Color{ 255, 130, 30, 255 }, 1.0f, 2.5f, 0.55f, 0.5f, -2.0f);
        Burst(particles, c, 1, Color{ 70, 66, 60, 255 }, 0.8f, 4.0f, 0.9f, 2.0f, -3.0f);
    }
}

void Tripedal::drawLocal(float walkPhase, float bobY, Color body, Color metal,
                            Color dark, Color trim, float alpha, float crumple) {
    float chassisY = WALKER_CHASSIS_Y + bobY - crumple * 2.2f;
    // Central armored pod (scaled sphere).
    rlPushMatrix();
    rlTranslatef(0.0f, chassisY, 0.0f);
    rlScalef(1.6f, 1.0f, 1.6f);
    DrawSphere(Vector3{ 0.0f, 0.0f, 0.0f }, 1.5f, WithAlpha(body, alpha));
    rlPopMatrix();
    // Armor trim ring around the pod's equator.
    DrawCylinder(Vector3{ 0.0f, chassisY - 0.15f, 0.0f }, 2.3f, 2.45f, 0.35f, 12,
                 WithAlpha(trim, alpha));
    // Twin sensor antennae with glowing tips.
    for (float sx : { -0.55f, 0.55f }) {
        Vector3 ab = { sx, chassisY + 1.2f, 0.35f };
        Vector3 at = { sx * 1.5f, chassisY + 2.3f, 0.5f };
        DrawLimb(ab, at, 0.06f, WithAlpha(dark, alpha));
        DrawSphere(at, 0.13f, WithAlpha(trim, alpha));
    }
    // Three legs, 120 degrees apart, procedural walk cycle.
    for (int i = 0; i < 3; ++i) {
        float a = (float)i * (2.0f * PI / 3.0f);
        Vector3 outward = { sinf(a), 0.0f, -cosf(a) };
        Vector3 hip = { outward.x * 1.2f, chassisY - 0.3f, outward.z * 1.2f };
        float ph = walkPhase + (float)i * (2.0f * PI / 3.0f);
        float stepping = (crumple > 0.5f) ? 0.0f : 1.0f;
        float stride = sinf(ph) * 1.2f * stepping;
        float lift = fmaxf(0.0f, sinf(ph + PI * 0.5f)) * 1.0f * stepping;
        float spread = 3.3f - crumple * 1.5f;
        Vector3 foot = { outward.x * spread, lift - crumple * 0.3f,
                         outward.z * spread - stride };
        Vector3 knee = { (hip.x + foot.x) * 0.5f + outward.x * 1.0f,
                         (hip.y + foot.y) * 0.5f + 1.3f - crumple * 1.8f,
                         (hip.z + foot.z) * 0.5f + outward.z * 1.0f };
        DrawLimb(hip, knee, 0.3f, WithAlpha(metal, alpha));
        DrawLimb(knee, foot, 0.22f, WithAlpha(dark, alpha));
        // Hip and knee joint pods in trim color.
        DrawSphere(hip, 0.36f, WithAlpha(trim, alpha));
        DrawSphere(knee, 0.28f, WithAlpha(metal, alpha));
        DrawSphere(foot, 0.34f, WithAlpha(dark, alpha));
    }
}

void Tripedal::drawHeadLocal(float bobY, float yaw,
                                  float pitchDeg, float rollDeg,
                                  Color armor, Color trim,
                                  Color glow, float alpha) const {
    static const Color DARKMETAL = { 30, 32, 38, 255 };
    rlPushMatrix();
    rlTranslatef(0.0f, WALKER_NECK_Y + bobY, 0.0f);
    // Neck: pod top to head pivot (stays rooted while the head turns).
    DrawLimb(Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 0.8f, -0.3f },
             0.3f, WithAlpha(armor, alpha));
    // Head: yaws, then nods (pitch) and tilts (roll) on the pendulum.
    rlPushMatrix();
    rlTranslatef(0.0f, 0.8f, -0.3f);
    rlRotatef(-yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitchDeg, 1.0f, 0.0f, 0.0f);
    rlRotatef(rollDeg, 0.0f, 0.0f, 1.0f);
    DrawSphere(Vector3{ 0.0f, 0.0f, 0.0f }, 0.8f, WithAlpha(armor, alpha));
    // Glowing sensor eye on the head's face.
    DrawSphere(Vector3{ 0.0f, 0.15f, -0.68f }, 0.3f, WithAlpha(glow, alpha));
    DrawSphere(Vector3{ 0.0f, 0.15f, -0.68f }, 0.16f,
               WithAlpha(Color{ 255, 255, 255, 255 }, alpha));
    rlPopMatrix();
    // Ventral gun pod slung below: same yaw, pitches about its own center,
    // rolls with the head.
    rlPushMatrix();
    rlTranslatef(0.0f, WALKER_GUN_DY, WALKER_GUN_FWD);
    rlRotatef(-yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitchDeg, 1.0f, 0.0f, 0.0f);
    rlRotatef(rollDeg, 0.0f, 0.0f, 1.0f);
    DrawSphere(Vector3{ 0.0f, 0.0f, 0.0f }, 0.5f, WithAlpha(trim, alpha));
    DrawLimb(Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 0.0f, -WALKER_BARREL },
             0.16f, WithAlpha(DARKMETAL, alpha));
    DrawSphere(Vector3{ 0.0f, 0.0f, -WALKER_BARREL }, 0.2f,
               WithAlpha(glow, alpha));  // muzzle glow
    rlPopMatrix();
    rlPopMatrix();
}

void Tripedal::drawWalker(const Config &cfg, float alpha) const {
    static const Color GUNMETAL = { 52, 55, 62, 255 };
    static const Color LEGMETAL = { 70, 74, 82, 255 };
    static const Color DARKMETAL = { 30, 32, 38, 255 };
    Color trim = cfg.enemyColor;
    Color glow = Color{ (unsigned char)fminf(trim.r * 1.6f, 255.0f),
                       (unsigned char)fminf(trim.g * 1.6f, 255.0f),
                       (unsigned char)fminf(trim.b * 1.6f, 255.0f), 255 };
    if (hitFlashT > 0.0f) {
        trim = Color{ 255, 240, 230, 255 };
        glow = Color{ 255, 255, 255, 255 };
    }
    // Hit reaction: an initial jolt along the shot, then the legs backpedal
    // (STAGGER state) while the top leans back with a stumble wobble;
    // st goes 1 (just hit) -> 0.
    float st = (cfg.staggerSeconds > 0.001f)
        ? Clamp(staggerT / cfg.staggerSeconds, 0.0f, 1.0f) : 0.0f;
    Vector3 base = pos;
    base.x += staggerDir.x * cfg.staggerDistance * st * st;  // sharp initial jolt
    base.z += staggerDir.z * cfg.staggerDistance * st * st;
    float bobY = sinf(walkPhase * 2.0f) * 0.08f - 0.35f * st;  // dips under the hit
    float lean = 10.0f * st + sinf(st * 18.0f) * 5.0f * st;      // lean back + stumble
    float roll = sinf(st * 14.0f) * 7.0f * st;                   // side-to-side wobble
    // Head tilt: cranes skywards and away from the shot, then settles.
    // sw goes 1 (impact) -> 0 (settled); u goes 0 -> 1.
    float sw = (cfg.swaySeconds > 0.001f)
        ? Clamp(swayT / cfg.swaySeconds, 0.0f, 1.0f) : 0.0f;
    float u = 1.0f - sw;
    // Rear-up envelope: snaps skywards, eases back through center with a
    // slight nod, then settles.
    float tilt = cosf(u * (float)PI) * expf(-2.0f * u);
    float yawNow = turretAngle + swayYawAmp * expf(-2.5f * u);
    float pitchNow = gunPitch + swayPitchAmp * tilt;
    float rollNow = swayRollAmp * tilt;
    rlPushMatrix();
    rlTranslatef(base.x, 0.0f, base.z);
    if (st > 0.001f) {
        rlRotatef(lean, staggerDir.z, 0.0f, -staggerDir.x);
        rlRotatef(roll, staggerDir.x, 0.0f, staggerDir.z);
    }
    rlRotatef(-hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    drawLocal(walkPhase, bobY, GUNMETAL, LEGMETAL, DARKMETAL, trim, alpha, 0.0f);
    drawHeadLocal(bobY, yawNow,
                          pitchNow * RAD2DEG, rollNow * RAD2DEG,
                          GUNMETAL, trim, glow, alpha);
    rlPopMatrix();
}

void Tripedal::drawWreck() const {
    static const Color CHARRED = { 38, 33, 28, 255 };
    static const Color CHARRED2 = { 52, 48, 44, 255 };
    float tip = fminf(deathT * 2.2f, 1.05f);
    float sink = fminf(deathT * 0.8f, 0.8f);
    rlPushMatrix();
    rlTranslatef(pos.x, 0.0f, pos.z);
    rlRotatef(tip * RAD2DEG, fallAxis.x, 0.0f, fallAxis.z);
    rlTranslatef(0.0f, -sink, 0.0f);
    rlRotatef(-hullAngle * RAD2DEG, 0.0f, 1.0f, 0.0f);
    drawLocal(walkPhase, 0.0f, CHARRED, CHARRED2, CHARRED, CHARRED2, 1.0f, 1.0f);
    rlPopMatrix();
    // The popped head where it landed.
    DrawSphere(turretPos, 0.8f, CHARRED);
    DrawSphere(Vector3{ turretPos.x, turretPos.y + 0.15f, turretPos.z },
               0.3f, CHARRED2);
    // Fire flicker on the collapsed chassis.
    float f = 0.75f + 0.25f * sinf(deathT * 13.0f);
    DrawSphere(Vector3{ pos.x, 3.0f, pos.z }, 0.55f * f, Color{ 255, 120, 25, 210 });
    DrawSphere(Vector3{ pos.x + 0.5f, 2.8f, pos.z - 0.3f }, 0.35f * f,
               Color{ 255, 190, 60, 190 });
}

void Tripedal::draw(float alpha, const Config &cfg) const {
    if (alive) drawWalker(cfg, alpha);
    else drawWreck();
    if (cfg.pathDrawPaths && alive)
        navPath.draw(pos, Color{ 255, 160, 60, 220 });  // orange route
    // Searchlight: the vision cone rendered as a real beam of light —
    // layered fan with distance falloff, a bright core, and a lamp glow
    // at the head. Pale while calm, yellowing as the spotting meter
    // fills, red during the alarm call.
    if (cfg.stealthEnabled && alive &&
        (aiState == AIState::PATROL || aiState == AIState::INVESTIGATE ||
         aiState == AIState::SEARCH || aiState == AIState::ALARM)) {
        float yaw = hullAngle + turretAngle;
        float half = cfg.visionHalfAngleDeg * (float)DEG2RAD;
        unsigned char cr, cg, cb;
        if (aiState == AIState::ALARM)      { cr = 255; cg = 70;  cb = 45;  }
        else if (detect > 0.75f)            { cr = 255; cg = 120; cb = 60;  }
        else {  // pale blue-white warming toward yellow as the meter fills
            cr = (unsigned char)(205 + 50 * detect);
            cg = (unsigned char)(228 - 28 * detect);
            cb = (unsigned char)(255 - 165 * detect);
        }
        // One band of the beam between two range fractions.
        auto band = [&](float r0f, float r1f, float halfAngle, unsigned char a) {
            Color cc{ cr, cg, cb, (unsigned char)(a * alpha) };
            const int segs = 14;
            for (int i = 0; i < segs; ++i) {
                float a0 = yaw - halfAngle + 2.0f * halfAngle * (float)i / segs;
                float a1 = yaw - halfAngle + 2.0f * halfAngle * (float)(i + 1) / segs;
                float R = cfg.visionRange;
                Vector3 A = { pos.x + sinf(a0) * R * r0f, 0.25f, pos.z - cosf(a0) * R * r0f };
                Vector3 B = { pos.x + sinf(a1) * R * r0f, 0.25f, pos.z - cosf(a1) * R * r0f };
                Vector3 C = { pos.x + sinf(a0) * R * r1f, 0.25f, pos.z - cosf(a0) * R * r1f };
                Vector3 D = { pos.x + sinf(a1) * R * r1f, 0.25f, pos.z - cosf(a1) * R * r1f };
                DrawTriangle3D(A, D, C, cc);
                DrawTriangle3D(A, B, D, cc);
            }
        };
        band(0.0f, 0.4f, half, 150);        // bright near the lamp
        band(0.4f, 0.75f, half, 80);
        band(0.75f, 1.0f, half, 38);       // fading edge
        band(0.0f, 1.0f, half * 0.5f, 55); // hot core down the middle
        // Lamp glow at the head (halo + bright core).
        float fx = sinf(yaw), fz = -cosf(yaw);
        Vector3 lamp = { pos.x + fx * 1.4f, WALKER_NECK_Y + 0.55f, pos.z + fz * 1.4f };
        DrawSphere(lamp, 0.95f, Color{ cr, cg, cb, (unsigned char)(45 * alpha) });
        DrawSphere(lamp, 0.42f, Color{ 255, 246, 220, 255 });
    }
}

std::vector<std::unique_ptr<Walker>> SpawnEnemies(int count, int hits,
                                       const std::vector<Building> &village,
                                       const std::vector<Ditch> &ditches,
                                       const std::vector<Bridge> &bridges,
                                       const std::vector<Tower> &towers) {
    std::vector<std::unique_ptr<Walker>> out;
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
            float ex = x - e->pos.x, ez = z - e->pos.z;
            if (ex * ex + ez * ez < 30.0f * 30.0f) { bad = true; break; }
        }
        if (bad) continue;
        auto e = std::make_unique<Tripedal>();
        e->pos = { x, 0.0f, z };
        e->hullAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
        e->turretAngle = (float)GetRandomValue(-60, 60) * DEG2RAD;
        e->hp = e->maxHp = hits;
        out.push_back(std::move(e));
    }
    return out;
}

bool ShellHitsEnemy(const Vector3 &p, float r, const Walker &e) {
    if (!e.alive) return false;
    float dx = p.x - e.pos.x, dz = p.z - e.pos.z;
    float rr = 2.2f + r;
    // Tripedals stand ~7 tall (legs + pod + head); tanks are low.
    float top = (e.kind() == WalkerKind::TRIPEDAL) ? 7.0f : 3.2f;
    return dx * dx + dz * dz < rr * rr && p.y > 0.0f && p.y < top;
}

void UpdateWalkers(std::vector<std::unique_ptr<Walker>> &walkers,
                          std::vector<Particle> &particles, float dt) {
    for (auto &e : walkers) e->updateDeath(particles, dt);
    // Particles: integrate, gravity, expire.
    for (auto it = particles.begin(); it != particles.end();) {
        it->vel.y -= it->grav * dt;
        it->pos = Vector3Add(it->pos, Vector3Scale(it->vel, dt));
        it->life -= dt;
        it = (it->life <= 0.0f) ? particles.erase(it) : std::next(it);
    }
}

void DrawEnemies(const std::vector<std::unique_ptr<Walker>> &walkers, const Config &cfg) {
    for (const auto &e : walkers) {
        // Fog of war: fully fogged enemies are skipped; enemies in the
        // soft reveal band fade in so they visibly emerge from the fog
        // instead of popping into existence.
        float alpha = e->alive ? (1.0f - e->fogFactor) : 1.0f;
        if (alpha <= 0.02f) continue;
        // A fading enemy must not write depth: it would punch a
        // tank-shaped hole through the fog puffs behind it.
        bool faded = alpha < 0.99f;
        if (faded) rlDisableDepthMask();
        e->draw(alpha, cfg);
        if (e->alive) {
            // Tracking ping: pulsing red ring under an enemy with a lock.
            if (e->aiState == AIState::SHOOT && e->aimTimer > 0.05f) {
                float base = (e->kind() == WalkerKind::TRIPEDAL) ? 4.2f : 2.8f;
                float pulse = base + sinf(e->aimTimer * 14.0f) * 0.5f;
                DrawCylinderWires(Vector3{ e->pos.x, 0.08f, e->pos.z }, pulse, pulse,
                                  0.12f, 24,
                                  Color{ 255, 40, 40, (unsigned char)(230 * alpha) });
            }
        }
        if (faded) rlEnableDepthMask();
    }
}

void ResolveWreckCollisions(Vector3 &pos, const std::vector<std::unique_ptr<Walker>> &walkers, bool wreckBlocks) {
    if (!wreckBlocks) return;
    for (const auto &e : walkers) {
        if (e->alive) continue;
        float dx = pos.x - e->pos.x, dz = pos.z - e->pos.z;
        float rr = TANK_RADIUS + 2.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 < rr * rr && d2 > 1e-6f) {
            float d = sqrtf(d2);
            pos.x = e->pos.x + dx / d * rr;
            pos.z = e->pos.z + dz / d * rr;
        }
    }
}
