#pragma once
// collide.h — Collidable entities and the single per-frame collision
// pass. Every dynamic entity (player, allies, walkers) exposes disks in
// yawed local space; ResolveCollisions() runs ONE pairwise pass over all
// of them (positional only, no damage). Static geometry (buildings,
// towers, wrecks) keeps its dedicated resolves in tankshooter.cpp.

#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <vector>

struct CollisionDisk {
    Vector3 offset;  // entity-local (yawed) space
    float radius;
};

class Collidable {
public:
    virtual ~Collidable() = default;
    virtual Vector3 cPos() const = 0;
    virtual void cSetPos(const Vector3 &p) = 0;
    virtual float cYaw() const = 0;
    virtual float cMass() const = 0;  // <=0 => immovable
    virtual bool cAlive() const = 0;
    virtual std::vector<CollisionDisk> cDisks() const = 0;
};

inline Vector3 YawLocalToWorld(float yaw, const Vector3 &o) {
    float c = cosf(yaw), s = sinf(yaw);
    return Vector3{ o.x * c - o.z * s, o.y, o.x * s + o.z * c };
}

inline void ResolveCollisions(const std::vector<Collidable*> &cs) {
    for (int iter = 0; iter < 2; ++iter) {
        for (size_t i = 0; i < cs.size(); ++i) {
            for (size_t j = i + 1; j < cs.size(); ++j) {
                Collidable *a = cs[i], *b = cs[j];
                if (!a->cAlive() || !b->cAlive()) continue;
                float ma = a->cMass(), mb = b->cMass();
                if (ma <= 0.0f && mb <= 0.0f) continue;
                float ima = (ma > 0.0f) ? 1.0f / ma : 0.0f;
                float imb = (mb > 0.0f) ? 1.0f / mb : 0.0f;
                float invSum = ima + imb;
                if (invSum <= 0.0f) continue;
                std::vector<CollisionDisk> da = a->cDisks();
                std::vector<CollisionDisk> db = b->cDisks();
                for (const auto &xa : da) {
                    Vector3 pa = Vector3Add(a->cPos(), YawLocalToWorld(a->cYaw(), xa.offset));
                    for (const auto &xb : db) {
                        Vector3 pb = Vector3Add(b->cPos(), YawLocalToWorld(b->cYaw(), xb.offset));
                        float dx = pa.x - pb.x, dz = pa.z - pb.z;
                        float rr = xa.radius + xb.radius;
                        float d2 = dx * dx + dz * dz;
                        if (d2 >= rr * rr || d2 < 1e-8f) continue;
                        float d = sqrtf(d2);
                        float push = rr - d;
                        float nx = dx / d, nz = dz / d;
                        Vector3 apa = a->cPos();
                        apa.x += nx * push * (ima / invSum);
                        apa.z += nz * push * (ima / invSum);
                        a->cSetPos(apa);
                        Vector3 bpa = b->cPos();
                        bpa.x -= nx * push * (imb / invSum);
                        bpa.z -= nz * push * (imb / invSum);
                        b->cSetPos(bpa);
                    }
                }
            }
        }
    }
}
