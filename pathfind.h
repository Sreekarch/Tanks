#pragma once
// pathfind.h — grid A* navigation for walkers and allied tanks.
// Buildings/towers are rasterized into a blocked grid (inflated by an
// agent pad); A* finds the route and a greedy line-of-sight pass
// smooths it into a few waypoints. Config.pathEnabled swaps between
// this and the legacy direct steering (steer straight at the target).

#include "common.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>
#include <vector>

struct NavGrid {
    // (Re)rasterize when the village/towers changed (buildings collapse,
    // towers die, map reloads). Cheap no-op when nothing changed.
    // Ditches rasterize as blocked (they're pits); bridge decks are
    // carved back open so routes cross pits at bridges, not through them.
    void EnsureBuilt(const std::vector<Building> &village,
                     const std::vector<Tower> &towers,
                     const std::vector<Ditch> &ditches,
                     const std::vector<Bridge> &bridges,
                     float cellSize) {
        int destroyed = 0;
        int hash = 0;
        for (const auto &b : village) {
            destroyed += b.destroyed ? 1 : 0;
            hash += (int)(b.center.x * 13.0f) + (int)(b.center.z * 29.0f);
        }
        int aliveT = 0;
        for (const auto &t : towers) aliveT += t.alive ? 1 : 0;
        for (const auto &d : ditches)
            hash += (int)(d.center.x * 7.0f) + (int)(d.center.z * 17.0f);
        for (const auto &b : bridges)
            hash += (int)(b.center.x * 5.0f) + (int)(b.center.z * 11.0f);
        if ((int)village.size() == sigB && destroyed == sigD &&
            (int)towers.size() == sigT && aliveT == sigA &&
            (int)ditches.size() == sigDi && (int)bridges.size() == sigBr &&
            cellSize == sigCell && hash == sigHash) {
            return;
        }
        sigB = (int)village.size(); sigD = destroyed; sigHash = hash;
        sigT = (int)towers.size(); sigA = aliveT;
        sigDi = (int)ditches.size(); sigBr = (int)bridges.size();
        sigT = (int)towers.size(); sigA = aliveT; sigCell = cellSize;
        cell = cellSize > 0.5f ? cellSize : 2.0f;
        n = (int)ceilf(2.0f * ARENA_HALF / cell);
        blocked.assign((size_t)n * n, 0);
        const float pad = 3.4f;  // walker disk 3.0 / tank 2.2 + margin
        auto markBox = [&](float minX, float minZ, float maxX, float maxZ) {
            int i0 = (int)floorf((minX + ARENA_HALF) / cell);
            int i1 = (int)floorf((maxX + ARENA_HALF) / cell);
            int j0 = (int)floorf((minZ + ARENA_HALF) / cell);
            int j1 = (int)floorf((maxZ + ARENA_HALF) / cell);
            if (i0 < 0) i0 = 0;
            if (j0 < 0) j0 = 0;
            if (i1 >= n) i1 = n - 1;
            if (j1 >= n) j1 = n - 1;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    blocked[(size_t)j * n + i] = 1;
        };
        for (const auto &b : village) {
            if (b.destroyed) continue;
            float hx = b.size.x * 0.5f + pad, hz = b.size.z * 0.5f + pad;
            markBox(b.center.x - hx, b.center.z - hz,
                    b.center.x + hx, b.center.z + hz);
        }
        for (const auto &t : towers) {
            if (!t.alive) continue;
            float r = 3.5f + pad;
            int i0 = (int)floorf((t.pos.x - r + ARENA_HALF) / cell);
            int i1 = (int)floorf((t.pos.x + r + ARENA_HALF) / cell);
            int j0 = (int)floorf((t.pos.z - r + ARENA_HALF) / cell);
            int j1 = (int)floorf((t.pos.z + r + ARENA_HALF) / cell);
            if (i0 < 0) i0 = 0;
            if (j0 < 0) j0 = 0;
            if (i1 >= n) i1 = n - 1;
            if (j1 >= n) j1 = n - 1;
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    Vector3 c = center(i, j);
                    float dx = c.x - t.pos.x, dz = c.z - t.pos.z;
                    if (dx * dx + dz * dz <= r * r) blocked[(size_t)j * n + i] = 1;
                }
            }
        }
        // Pits block; bridge decks carve the crossing back open. Cells
        // blocked by buildings/towers (not the pit) stay blocked.
        if (!ditches.empty()) {
            std::vector<char> hard = blocked;  // buildings/towers only
            auto markRect = [&](float cx, float cz, float hx, float hz, char v) {
                int i0 = (int)floorf((cx - hx + ARENA_HALF) / cell);
                int i1 = (int)floorf((cx + hx + ARENA_HALF) / cell);
                int j0 = (int)floorf((cz - hz + ARENA_HALF) / cell);
                int j1 = (int)floorf((cz + hz + ARENA_HALF) / cell);
                if (i0 < 0) i0 = 0;
                if (j0 < 0) j0 = 0;
                if (i1 >= n) i1 = n - 1;
                if (j1 >= n) j1 = n - 1;
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i) blocked[(size_t)j * n + i] = v;
            };
            for (const auto &d : ditches)
                markRect(d.center.x, d.center.z, d.hx + 1.0f, d.hz + 1.0f, 1);
            for (const auto &b : bridges) {
                int i0 = (int)floorf((b.center.x - b.hx + ARENA_HALF) / cell);
                int i1 = (int)floorf((b.center.x + b.hx + ARENA_HALF) / cell);
                int j0 = (int)floorf((b.center.z - b.hz + ARENA_HALF) / cell);
                int j1 = (int)floorf((b.center.z + b.hz + ARENA_HALF) / cell);
                if (i0 < 0) i0 = 0;
                if (j0 < 0) j0 = 0;
                if (i1 >= n) i1 = n - 1;
                if (j1 >= n) j1 = n - 1;
                for (int j = j0; j <= j1; ++j) {
                    for (int i = i0; i <= i1; ++i) {
                        size_t k = (size_t)j * n + i;
                        if (blocked[k] && !hard[k]) blocked[k] = 0;  // deck over pit
                    }
                }
            }
        }
    }

    // A* from `from` to `to`; on success `out` holds ground waypoints
    // ending at the exact goal. False when no route exists (caller
    // falls back to direct steering).
    bool FindPath(const Vector3 &from, const Vector3 &to,
                  std::vector<Vector3> &out) const {
        out.clear();
        if (n == 0) return false;
        int si, sj, gi, gj;
        cellOf(from, si, sj);
        cellOf(to, gi, gj);
        if (!nearestFree(si, sj) || !nearestFree(gi, gj)) return false;
        int start = sj * n + si, goal = gj * n + gi;
        if (start == goal) { out.push_back(to); return true; }

        const float INF = 1e30f;
        std::vector<float> gScore((size_t)n * n, INF);
        std::vector<int> came((size_t)n * n, -1);
        std::vector<char> closed((size_t)n * n, 0);
        auto heur = [&](int idx) {
            int x = idx % n, y = idx / n;
            float dx = (float)abs(x - gi), dz = (float)abs(y - gj);
            return (dx + dz) + (1.41421356f - 2.0f) * fminf(dx, dz);
        };
        using Node = std::pair<float, int>;  // (f, cell)
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        gScore[start] = 0.0f;
        open.push({ heur(start), start });
        static const int DIRS[8][2] = { {1,0},{-1,0},{0,1},{0,-1},
                                        {1,1},{1,-1},{-1,1},{-1,-1} };
        int found = -1;
        while (!open.empty()) {
            int cur = open.top().second;
            open.pop();
            if (closed[cur]) continue;
            closed[cur] = 1;
            if (cur == goal) { found = cur; break; }
            int cx = cur % n, cy = cur / n;
            for (auto &d : DIRS) {
                int nx = cx + d[0], ny = cy + d[1];
                if (!free(nx, ny)) continue;
                bool diag = d[0] != 0 && d[1] != 0;
                // No corner cutting through blocked orthogonal neighbors.
                if (diag && (!free(cx + d[0], cy) || !free(cx, cy + d[1]))) continue;
                int ni = ny * n + nx;
                float step = diag ? 1.41421356f : 1.0f;
                float ng = gScore[cur] + step;
                if (ng < gScore[ni]) {
                    gScore[ni] = ng;
                    came[ni] = cur;
                    open.push({ ng + heur(ni), ni });
                }
            }
        }
        if (found < 0) return false;
        // Reconstruct cell centers, then smooth by line-of-sight shortcut.
        std::vector<Vector3> cells;
        for (int c = goal; c != -1; c = came[c]) cells.push_back(center(c % n, c / n));
        std::reverse(cells.begin(), cells.end());  // start..goal cells
        Vector3 anchor = from;
        size_t ai = 0;  // index in cells of the anchor's cell
        while (ai + 1 < cells.size()) {
            size_t far = ai + 1;
            for (size_t k = ai + 2; k < cells.size(); ++k) {
                if (segmentFree(anchor, cells[k])) far = k; else break;
            }
            out.push_back(cells[far]);
            anchor = cells[far];
            ai = far;
        }
        out.push_back(to);  // exact goal as the final approach point
        return true;
    }

private:
    void cellOf(const Vector3 &p, int &i, int &j) const {
        i = (int)floorf((p.x + ARENA_HALF) / cell);
        j = (int)floorf((p.z + ARENA_HALF) / cell);
        if (i < 0) i = 0;
        if (j < 0) j = 0;
        if (i >= n) i = n - 1;
        if (j >= n) j = n - 1;
    }
    Vector3 center(int i, int j) const {
        return { -ARENA_HALF + ((float)i + 0.5f) * cell, 0.0f,
                 -ARENA_HALF + ((float)j + 0.5f) * cell };
    }
    bool free(int i, int j) const {
        return i >= 0 && j >= 0 && i < n && j < n && !blocked[(size_t)j * n + i];
    }
    bool nearestFree(int &i, int &j) const {
        if (free(i, j)) return true;
        for (int r = 1; r <= 8; ++r) {
            for (int dj = -r; dj <= r; ++dj) {
                for (int di = -r; di <= r; ++di) {
                    if (free(i + di, j + dj)) { i += di; j += dj; return true; }
                }
            }
        }
        return false;
    }
    bool segmentFree(const Vector3 &a, const Vector3 &b) const {
        float dx = b.x - a.x, dz = b.z - a.z;
        float len = sqrtf(dx * dx + dz * dz);
        int steps = (int)(len / (cell * 0.45f)) + 1;
        for (int s = 0; s <= steps; ++s) {
            float t = (float)s / (float)steps;
            int i, j;
            cellOf({ a.x + dx * t, 0.0f, a.z + dz * t }, i, j);
            if (!free(i, j)) return false;
        }
        return true;
    }

    float cell = 2.0f;
    int n = 0;  // cells per side
    std::vector<char> blocked;
    int sigB = -1, sigD = -1, sigT = -1, sigA = -1, sigHash = 0;
    int sigDi = -1, sigBr = -1;
    float sigCell = -1.0f;
};

// Per-entity path state: replans on an interval / when the goal moved,
// then steers waypoint to waypoint. Falls back to direct steering
// (returns false) when no route exists.
struct PathFollower {
    std::vector<Vector3> points;
    size_t next = 0;
    Vector3 goal = { 0, 0, 0 };
    float repathT = 1e9f;  // forces an immediate first plan
    bool has = false;

    void reset() {
        points.clear(); next = 0; has = false; repathT = 1e9f;
    }

    bool steer(const NavGrid &grid, const Vector3 &pos, const Vector3 &goalPos,
               float dt, float repathInterval, float &outAngle) {
        repathT += dt;
        float gdx = goalPos.x - goal.x, gdz = goalPos.z - goal.z;
        bool goalMoved = !has || (gdx * gdx + gdz * gdz > 25.0f);
        if (goalMoved || repathT >= repathInterval) {
            repathT = 0.0f;
            has = grid.FindPath(pos, goalPos, points);
            next = 0;
            goal = goalPos;
            if (!has) { points.clear(); return false; }
        }
        if (!has || points.empty()) return false;
        while (next < points.size()) {
            float dx = points[next].x - pos.x, dz = points[next].z - pos.z;
            if (dx * dx + dz * dz < 1.6f * 1.6f) { ++next; continue; }
            break;
        }
        Vector3 target = (next < points.size()) ? points[next] : goalPos;
        outAngle = atan2f(target.x - pos.x, -(target.z - pos.z));
        return true;
    }

    // Debug overlay (Config.pathDrawPaths): remaining path as a polyline.
    void draw(const Vector3 &pos, Color c) const {
        if (!has || next >= points.size()) return;
        Vector3 prev = { pos.x, 0.45f, pos.z };
        for (size_t i = next; i < points.size(); ++i) {
            Vector3 p = { points[i].x, 0.45f, points[i].z };
            DrawLine3D(prev, p, c);
            DrawSphere(p, 0.35f, c);
            prev = p;
        }
    }
};
