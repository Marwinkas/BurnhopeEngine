#include "gpu_scene/Mat4.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace burnhope {
void sceneActorSetCamera(SceneActors& world, const CameraCull& cam, float width, float height) {
    world.viewProj = cam.viewProj;
    world.eye[0] = cam.pos[0];
    world.eye[1] = cam.pos[1];
    world.eye[2] = cam.pos[2];
    world.right[0] = cam.right[0];
    world.right[1] = cam.right[1];
    world.right[2] = cam.right[2];
    world.up[0] = cam.up[0];
    world.up[1] = cam.up[1];
    world.up[2] = cam.up[2];
    world.forward[0] = cam.forward[0];
    world.forward[1] = cam.forward[1];
    world.forward[2] = cam.forward[2];
    world.tanX = cam.tanHalfFovX;
    world.tanY = cam.tanHalfFovY;
    world.extentW = width > 1.0f ? width : 1.0f;
    world.extentH = height > 1.0f ? height : 1.0f;
    world.ortho = cam.ortho;
    if (cam.ortho != 0 && cam.orthoHalfY > 1.0e-4f) {
        world.tanY = cam.orthoHalfY;
        world.tanX = cam.orthoHalfY * (world.extentW / world.extentH);
    }
    world.camReady = 1;
}

static void actorRay(const SceneActors& world, float px, float py, float ro[3], float rd[3]) {
    const float ndcX = (px / world.extentW) * 2.0f - 1.0f;
    const float ndcY = (py / world.extentH) * 2.0f - 1.0f;
    if (world.ortho != 0) {
        ro[0] = world.eye[0] + world.right[0] * ndcX * world.tanX + world.up[0] * (-ndcY) * world.tanY;
        ro[1] = world.eye[1] + world.right[1] * ndcX * world.tanX + world.up[1] * (-ndcY) * world.tanY;
        ro[2] = world.eye[2] + world.right[2] * ndcX * world.tanX + world.up[2] * (-ndcY) * world.tanY;
        rd[0] = world.forward[0];
        rd[1] = world.forward[1];
        rd[2] = world.forward[2];
        return;
    }
    ro[0] = world.eye[0];
    ro[1] = world.eye[1];
    ro[2] = world.eye[2];
    const float vx = ndcX * world.tanX;
    const float vy = -ndcY * world.tanY;
    rd[0] = world.right[0] * vx + world.up[0] * vy + world.forward[0];
    rd[1] = world.right[1] * vx + world.up[1] * vy + world.forward[1];
    rd[2] = world.right[2] * vx + world.up[2] * vy + world.forward[2];
    const float n = vec3Len(rd);
    if (n > 1.0e-6f) {
        rd[0] /= n;
        rd[1] /= n;
        rd[2] /= n;
    }
}

bool actorProject(const SceneActors& world, const float p[3], float& sx, float& sy) {
    const Mat4& m = world.viewProj;
    const float cx = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
    const float cy = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
    const float cw = m.m[3] * p[0] + m.m[7] * p[1] + m.m[11] * p[2] + m.m[15];
    if (cw <= 1.0e-4f) {
        return false;
    }
    sx = (cx / cw * 0.5f + 0.5f) * world.extentW;
    sy = (cy / cw * 0.5f + 0.5f) * world.extentH;
    return true;
}

static float actorScreenDist(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float ab2 = abx * abx + aby * aby;
    float t = ab2 > 1.0e-4f ? ((px - ax) * abx + (py - ay) * aby) / ab2 : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    const float dx = px - (ax + abx * t);
    const float dy = py - (ay + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

static float actorScreenAlong(float px, float py, float ax, float ay, float bx, float by, bool& ok) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float ab2 = abx * abx + aby * aby;
    ok = ab2 >= 4.0f;
    if (!ok) {
        return 0.0f;
    }
    return ((px - ax) * abx + (py - ay) * aby) / ab2;
}

static bool actorRayPlane(const float ro[3], const float rd[3], const float p0[3], const float n[3], float& t) {
    const float denom = vec3Dot(rd, n);
    if (std::fabs(denom) < 1.0e-6f) {
        return false;
    }
    float w[3];
    vec3Sub(p0, ro, w);
    t = vec3Dot(w, n) / denom;
    return t > 0.0f;
}

static float actorRayAxis(const float ro[3], const float rd[3], const float p0[3], const float dir[3], float& along) {
    float w0[3];
    vec3Sub(ro, p0, w0);
    const float b = vec3Dot(rd, dir);
    const float d = vec3Dot(rd, w0);
    const float e = vec3Dot(dir, w0);
    const float denom = 1.0f - b * b;
    if (denom < 1.0e-6f) {
        along = e;
    } else {
        along = (e - b * d) / denom;
    }
    const float hit[3] = {p0[0] + dir[0] * along, p0[1] + dir[1] * along, p0[2] + dir[2] * along};
    float closest[3];
    float t = vec3Dot(w0, rd);
    if (denom >= 1.0e-6f) {
        t = (b * e - d) / denom;
    }
    closest[0] = ro[0] + rd[0] * t;
    closest[1] = ro[1] + rd[1] * t;
    closest[2] = ro[2] + rd[2] * t;
    float delta[3];
    vec3Sub(closest, hit, delta);
    return vec3Len(delta);
}

float actorMetersPerPx(const SceneActors& world, const float pos[3]) {
    const float height = world.extentH > 1.0f ? world.extentH : 1.0f;
    if (world.ortho != 0 && world.tanY > 1.0e-4f) {
        return (2.0f * world.tanY) / height;
    }
    float delta[3];
    vec3Sub(pos, world.eye, delta);
    float dist = vec3Len(delta);
    if (dist < 0.2f) {
        dist = 0.2f;
    }
    const float tanY = world.tanY > 1.0e-4f ? world.tanY : 0.57735026f;
    return dist * (2.0f * tanY) / height;
}

static float actorIconPx(float dist) {
    float t = (dist - 8.0f) / 40.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    float px = 72.0f + (36.0f - 72.0f) * t;
    if (px < 32.0f) {
        px = 32.0f;
    }
    if (px > 80.0f) {
        px = 80.0f;
    }
    return px;
}

float actorSpan(const SceneActors& world, const float pos[3]) {
    float len = actorMetersPerPx(world, pos) * 108.0f;
    if (len < 1.0e-3f) {
        len = 1.0e-3f;
    }
    return len;
}

static float actorSnap(float value, float step) {
    if (step <= 1.0e-5f) {
        return value;
    }
    return std::round(value / step) * step;
}

static void gizmoBasis(const SceneActors& world, const float euler[3], float x[3], float y[3], float z[3]) {
    if (world.space != 0) {
        x[0] = 1.0f;
        x[1] = 0.0f;
        x[2] = 0.0f;
        y[0] = 0.0f;
        y[1] = 1.0f;
        y[2] = 0.0f;
        z[0] = 0.0f;
        z[1] = 0.0f;
        z[2] = 1.0f;
        return;
    }
    actorBasis(euler, x, y, z);
}

static void actorPivotLocal(const SceneActors& world, uint8_t actor, float out[3]) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    if (world.pivotSpace != 0 || actor >= world.count || !world.ent[actor].is_alive() || !world.ent[actor].has<Pivot>()) {
        return;
    }
    const Pivot& pivot = world.ent[actor].get<Pivot>();
    out[0] = pivot.local[0];
    out[1] = pivot.local[1];
    out[2] = pivot.local[2];
}

void actorWritePivot(SceneActors& world, uint8_t actor, const float local[3]) {
    if (actor >= world.count || !world.ent[actor].is_alive() || local == nullptr) {
        return;
    }
    Pivot pivot{};
    pivot.local[0] = local[0];
    pivot.local[1] = local[1];
    pivot.local[2] = local[2];
    world.ent[actor].set<Pivot>(pivot);
}

static void actorHandle(const SceneActors& world, const Xform& xform, uint8_t actor, float out[3]) {
    float local[3]{};
    actorPivotLocal(world, actor, local);
    float bx[3]{};
    float by[3]{};
    float bz[3]{};
    gizmoBasis(world, xform.euler, bx, by, bz);
    out[0] = xform.pos[0] + bx[0] * local[0] * xform.scale[0] + by[0] * local[1] * xform.scale[1] + bz[0] * local[2] * xform.scale[2];
    out[1] = xform.pos[1] + bx[1] * local[0] * xform.scale[0] + by[1] * local[1] * xform.scale[1] + bz[1] * local[2] * xform.scale[2];
    out[2] = xform.pos[2] + bx[2] * local[0] * xform.scale[0] + by[2] * local[1] * xform.scale[1] + bz[2] * local[2] * xform.scale[2];
}

static float actorPickPx(const SceneActors& world, const float origin[3], float span) {
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
    const float side[3] = {
        origin[0] + world.right[0] * span * 0.04f,
        origin[1] + world.right[1] * span * 0.04f,
        origin[2] + world.right[2] * span * 0.04f,
    };
    if (!actorProject(world, origin, ax, ay) || !actorProject(world, side, bx, by)) {
        return 10.0f;
    }
    float px = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) * 2.2f;
    if (px < 12.0f) {
        px = 12.0f;
    }
    if (px > 16.0f) {
        px = 16.0f;
    }
    return px;
}

static bool actorAlong(const SceneActors& world, float px, float py, const float origin[3], const float dir[3], float& along, uint8_t& screen, int stick = -1) {
    float ro[3];
    float rd[3];
    actorRay(world, px, py, ro, rd);
    const float align = std::fabs(vec3Dot(rd, dir));
    const bool useScreen = stick < 0 ? align > 0.92f : stick == 1;
    if (useScreen) {
        float ax = 0.0f;
        float ay = 0.0f;
        float bx = 0.0f;
        float by = 0.0f;
        const float tip[3] = {origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2]};
        bool ok = false;
        if (actorProject(world, origin, ax, ay) && actorProject(world, tip, bx, by)) {
            along = actorScreenAlong(px, py, ax, ay, bx, by, ok);
        }
        if (ok) {
            screen = 1;
            return true;
        }
    }
    actorRayAxis(ro, rd, origin, dir, along);
    screen = 0;
    return true;
}

static float actorRingAngle(const SceneActors& world, float px, float py, const float origin[3], const float axis[3], uint8_t& screen) {
    float ox = 0.0f;
    float oy = 0.0f;
    float ro[3];
    float rd[3];
    actorRay(world, px, py, ro, rd);
    const bool flat = std::fabs(vec3Dot(rd, axis)) <= 0.2f;
    if (screen == 0xff) {
        screen = flat ? 1 : 0;
    }
    if (screen == 0 && !flat) {
        float t = 0.0f;
        if (actorRayPlane(ro, rd, origin, axis, t)) {
            const float hit[3] = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
            float u[3];
            float v[3];
            if (mat4BasisFromAxis(axis, u, v)) {
                float rel[3];
                vec3Sub(hit, origin, rel);
                return std::atan2(vec3Dot(rel, v), vec3Dot(rel, u)) * 57.2957795f;
            }
        }
    }
    screen = 1;
    if (!actorProject(world, origin, ox, oy)) {
        return 0.0f;
    }
    return std::atan2(py - oy, px - ox) * 57.2957795f;
}

static bool sceneActorSunRig(const SceneActors& world) {
    return world.selected < world.count
        && actorKindOf(world, world.selected) == static_cast<uint8_t>(SceneActorKind::Weather);
}

void sceneActorFitTool(SceneActors& world) {
    const bool sun = sceneActorSunRig(world);
    world.rotXY = sun ? 1 : 0;
    if (!sun) {
        world.gizmo = world.tool > 4 ? 0 : world.tool;
        return;
    }
    uint8_t mode = world.tool > 4 ? 0 : world.tool;
    if (mode != 1 && mode != 3) {
        mode = 1;
    }
    world.gizmo = mode;
}

void sceneActorFillGpu(const SceneActors& world, GizmoGpu& gpu) {
    std::memcpy(gpu.viewProj, world.viewProj.m, sizeof(gpu.viewProj));
    std::memcpy(gpu.cam, world.eye, sizeof(float) * 3);
    const float tanY = world.tanY > 1.0e-4f ? world.tanY : 0.57735026f;
    const float height = world.extentH > 1.0f ? world.extentH : 1.0f;
    gpu.cam[3] = (2.0f * tanY) / height;
    gpu.origin[3] = 0.0f;
    const bool sunRig = sceneActorSunRig(world);
    uint8_t drawMode = world.gizmo;
    if (sunRig && drawMode != 1 && drawMode != 3) {
        drawMode = 1;
    }
    gpu.misc[1] = drawMode == 4 ? 0.0f : static_cast<float>(drawMode);
    gpu.padAlign[2] = sunRig ? 1.0f : 0.0f;
    gpu.misc[2] = world.drag <= 6 ? static_cast<float>(world.drag) : 255.0f;
    gpu.padAlign[0] = world.hot == kSceneActorNone ? 255.0f : static_cast<float>(world.hot);
    gpu.padAlign[1] = world.drag <= 6 ? static_cast<float>(world.drag) : 255.0f;
    uint8_t markers = 0;
    for (uint8_t i = 0; i < world.count && markers < 8; ++i) {
        const Xform* x = actorXformOf(world, i);
        if (x == nullptr) {
            continue;
        }
        gpu.marker[markers][0] = x->pos[0];
        gpu.marker[markers][1] = x->pos[1];
        gpu.marker[markers][2] = x->pos[2];
        gpu.marker[markers][3] = static_cast<float>(actorKindOf(world, i));
        markers += 1;
    }
    gpu.misc[3] = static_cast<float>(markers);
    const uint8_t boxId = world.mark < world.count ? world.mark : world.selected;
    const Xform* selected = actorXformOf(world, world.selected);
    const Xform* boxed = actorXformOf(world, boxId);
    if (selected == nullptr && boxed == nullptr) {
        gpu.misc[0] = 1.0f;
        gpu.padAlign[0] = 255.0f;
        gpu.padAlign[1] = 255.0f;
        return;
    }
    const Xform& xform = selected != nullptr ? *selected : *boxed;
    float handle[3]{};
    actorHandle(world, xform, selected != nullptr ? world.selected : boxId, handle);
    gpu.origin[0] = handle[0];
    gpu.origin[1] = handle[1];
    gpu.origin[2] = handle[2];
    gpu.origin[3] = 1.0f;
    float x[3];
    float y[3];
    float z[3];
    gizmoBasis(world, xform.euler, x, y, z);
    std::memcpy(gpu.ax, x, sizeof(x));
    std::memcpy(gpu.ay, y, sizeof(y));
    std::memcpy(gpu.az, z, sizeof(z));
    gpu.ax[3] = 0.0f;
    gpu.ay[3] = 0.0f;
    gpu.az[3] = 0.0f;
    const Xform& boxX = boxed != nullptr ? *boxed : xform;
    gpu.padAlign[4] = boxX.pos[0];
    gpu.padAlign[5] = boxX.pos[1];
    gpu.padAlign[6] = boxX.pos[2];
    gpu.padAlign[7] = 1.0f;
    if (boxId < world.count && world.ent[boxId].is_alive() && world.ent[boxId].has<ActorBounds>()) {
        const ActorBounds& box = world.ent[boxId].get<ActorBounds>();
        gpu.ax[3] = box.half[0] * boxX.scale[0];
        gpu.ay[3] = box.half[1] * boxX.scale[1];
        gpu.az[3] = box.half[2] * boxX.scale[2];
    }
    if (selected == nullptr) {
        gpu.misc[1] = 3.0f;
    }
    gpu.misc[0] = actorSpan(world, handle);
}

uint8_t sceneActorHit(const SceneActors& world, float px, float py, uint8_t& axis) {
    axis = kSceneActorNone;
    if (world.camReady == 0 || world.count == 0) {
        return kSceneActorNone;
    }
    const Xform* selectedX = actorXformOf(world, world.selected);
    if (selectedX != nullptr) {
        const Xform& xform = *selectedX;
        float basis[3][3];
        gizmoBasis(world, xform.euler, basis[0], basis[1], basis[2]);
        float org[3]{};
        actorHandle(world, xform, world.selected, org);
        const float span = actorSpan(world, org);
        const float limit = actorPickPx(world, org, span);
        float best = limit;
        float ox = 0.0f;
        float oy = 0.0f;
        if (!actorProject(world, org, ox, oy)) {
            ox = -1.0e8f;
        } else if (world.gizmo != 3) {
        const float mpp = actorMetersPerPx(world, org);
        auto reachPx = [&](float worldR) {
            float r = mpp > 1.0e-8f ? worldR / mpp : 16.0f;
            if (r < 12.0f) {
                r = 12.0f;
            }
            return r;
        };
        if (world.gizmo != 1) {
            const float hub = reachPx(span * 0.10f);
            const float cd = std::sqrt((px - ox) * (px - ox) + (py - oy) * (py - oy));
            if (cd <= hub) {
                axis = 6;
                return world.selected;
            }
            for (uint8_t missing = 0; missing < 3; ++missing) {
                float point[3] = {org[0], org[1], org[2]};
                for (uint8_t a = 0; a < 3; ++a) {
                    if (a == missing) {
                        continue;
                    }
                    point[0] += basis[a][0] * span * 0.32f;
                    point[1] += basis[a][1] * span * 0.32f;
                    point[2] += basis[a][2] * span * 0.32f;
                }
                float sx = 0.0f;
                float sy = 0.0f;
                if (!actorProject(world, point, sx, sy)) {
                    continue;
                }
                const float d = std::sqrt((px - sx) * (px - sx) + (py - sy) * (py - sy));
                if (d <= reachPx(span * 0.12f)) {
                    axis = static_cast<uint8_t>(3 + missing);
                    return world.selected;
                }
            }
        }
        for (uint8_t a = 0; a < 3; ++a) {
            if (sceneActorSunRig(world) && a == 2) {
                continue;
            }
            if (world.gizmo == 1) {
                float u[3];
                float v[3];
                if (!mat4BasisFromAxis(basis[a], u, v)) {
                    continue;
                }
                float ring = limit;
                float prevX = 0.0f;
                float prevY = 0.0f;
                bool prev = false;
                for (int s = 0; s <= 32; ++s) {
                    const float ang = static_cast<float>(s) * 0.19634954f;
                    const float point[3] = {
                        org[0] + (u[0] * std::cos(ang) + v[0] * std::sin(ang)) * span,
                        org[1] + (u[1] * std::cos(ang) + v[1] * std::sin(ang)) * span,
                        org[2] + (u[2] * std::cos(ang) + v[2] * std::sin(ang)) * span,
                    };
                    float sx = 0.0f;
                    float sy = 0.0f;
                    if (!actorProject(world, point, sx, sy)) {
                        prev = false;
                        continue;
                    }
                    if (prev) {
                        const float d = actorScreenDist(px, py, prevX, prevY, sx, sy);
                        if (d < ring) {
                            ring = d;
                        }
                    }
                    prevX = sx;
                    prevY = sy;
                    prev = true;
                }
                if (ring < best) {
                    best = ring;
                    axis = a;
                }
                continue;
            }
            const float lo = 0.18f;
            const float hi = 1.12f;
            const float a0[3] = {
                org[0] + basis[a][0] * span * lo,
                org[1] + basis[a][1] * span * lo,
                org[2] + basis[a][2] * span * lo,
            };
            const float a1[3] = {
                org[0] + basis[a][0] * span * hi,
                org[1] + basis[a][1] * span * hi,
                org[2] + basis[a][2] * span * hi,
            };
            float sx = 0.0f;
            float sy = 0.0f;
            float tx = 0.0f;
            float ty = 0.0f;
            if (!actorProject(world, a0, sx, sy) || !actorProject(world, a1, tx, ty)) {
                continue;
            }
            const float dist = actorScreenDist(px, py, sx, sy, tx, ty);
            if (dist < best) {
                best = dist;
                axis = a;
            }
        }
            if (axis != kSceneActorNone) {
                return world.selected;
            }
        }
    }
    uint8_t found = kSceneActorNone;
    float best = 1.0e9f;
    for (uint8_t i = 0; i < world.count; ++i) {
        const Xform* actor = actorXformOf(world, i);
        if (actor == nullptr) {
            continue;
        }
        float sx = 0.0f;
        float sy = 0.0f;
        if (!actorProject(world, actor->pos, sx, sy)) {
            continue;
        }
        float delta[3];
        vec3Sub(actor->pos, world.eye, delta);
        float radius = actorIconPx(vec3Len(delta)) * 0.5f;
        if (world.ent[i].has<ActorBounds>()) {
            const ActorBounds& box = world.ent[i].get<ActorBounds>();
            const float reach = std::max(box.half[0], std::max(box.half[1], box.half[2])) * actor->scale[0];
            const float corner[3] = {
                actor->pos[0] + world.right[0] * reach,
                actor->pos[1] + world.right[1] * reach,
                actor->pos[2] + world.right[2] * reach,
            };
            float tx = 0.0f;
            float ty = 0.0f;
            if (actorProject(world, corner, tx, ty)) {
                radius = std::sqrt((tx - sx) * (tx - sx) + (ty - sy) * (ty - sy));
                const float iconR = actorIconPx(vec3Len(delta)) * 0.5f;
                if (radius < iconR) {
                    radius = iconR;
                }
            }
        }
        const float dist = std::sqrt((px - sx) * (px - sx) + (py - sy) * (py - sy));
        if (dist < radius && dist < best) {
            best = dist;
            found = i;
        }
    }
    return found;
}

static void sceneActorGrab(SceneActors& world, const Xform& xform) {
    world.grabPos[0] = xform.pos[0];
    world.grabPos[1] = xform.pos[1];
    world.grabPos[2] = xform.pos[2];
    world.grabEuler[0] = xform.euler[0];
    world.grabEuler[1] = xform.euler[1];
    world.grabEuler[2] = xform.euler[2];
    world.grabScale[0] = xform.scale[0];
    world.grabScale[1] = xform.scale[1];
    world.grabScale[2] = xform.scale[2];
}

static void sceneActorScreenDelta(const SceneActors& world, float x, float y, float& alongRight, float& alongUp) {
    alongRight = 0.0f;
    alongUp = 0.0f;
    const float mpp = actorMetersPerPx(world, world.grabPos);
    const float rightPt[3] = {
        world.grabPos[0] + world.right[0] * mpp,
        world.grabPos[1] + world.right[1] * mpp,
        world.grabPos[2] + world.right[2] * mpp,
    };
    const float upPt[3] = {
        world.grabPos[0] + world.up[0] * mpp,
        world.grabPos[1] + world.up[1] * mpp,
        world.grabPos[2] + world.up[2] * mpp,
    };
    float ox = 0.0f;
    float oy = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    float ux = 0.0f;
    float uy = 0.0f;
    if (!actorProject(world, world.grabPos, ox, oy) || !actorProject(world, rightPt, rx, ry) || !actorProject(world, upPt, ux, uy)) {
        return;
    }
    const float srx = rx - ox;
    const float sry = ry - oy;
    const float sux = ux - ox;
    const float suy = uy - oy;
    const float det = srx * suy - sux * sry;
    if (std::fabs(det) < 1.0e-4f) {
        return;
    }
    const float mx = x - world.grabPx;
    const float my = y - world.grabPy;
    alongRight = (mx * suy - sux * my) / det * mpp;
    alongUp = (srx * my - mx * sry) / det * mpp;
}

void sceneActorPointer(SceneActors& world, float x, float y, uint8_t down, uint8_t pressed, uint8_t released) {
    if (world.camReady == 0) {
        return;
    }
    if (world.drag > 6) {
        sceneActorFitTool(world);
    }
    if (released != 0) {
        world.drag = kSceneActorNone;
    }
    uint8_t axis = kSceneActorNone;
    uint8_t hit = kSceneActorNone;
    if (world.drag > 6) {
        hit = sceneActorHit(world, x, y, axis);
        world.hot = axis <= 6 && hit == world.selected ? axis : kSceneActorNone;
    } else {
        world.hot = world.drag;
    }
    if (pressed != 0 && world.drag > 6) {
        const Xform* pressedX = actorXformOf(world, world.selected);
        if (axis <= 6 && hit == world.selected && pressedX != nullptr) {
            world.drag = axis;
            world.hot = axis;
            const Xform& xform = *pressedX;
            sceneActorGrab(world, xform);
            actorHandle(world, xform, world.selected, world.grabOrg);
            actorPivotLocal(world, world.selected, world.grabPivot);
            world.grabPx = x;
            world.grabPy = y;
            float basis[3][3];
            gizmoBasis(world, xform.euler, basis[0], basis[1], basis[2]);
            world.grabScreen = 0;
            if (axis < 3) {
                actorAlong(world, x, y, world.grabOrg, basis[axis], world.grabAlong, world.grabScreen);
                if (std::fabs(world.grabAlong) < 0.05f) {
                    world.grabAlong = world.grabAlong < 0.0f ? -0.05f : 0.05f;
                }
                if (world.gizmo == 1) {
                    uint8_t mode = 0xff;
                    world.grabAngle = actorRingAngle(world, x, y, world.grabOrg, basis[axis], mode);
                    world.grabScreen = mode;
                }
            } else if (axis >= 3 && axis <= 5) {
                const uint8_t missing = static_cast<uint8_t>(axis - 3);
                float ro[3];
                float rd[3];
                actorRay(world, x, y, ro, rd);
                float t = 0.0f;
                if (actorRayPlane(ro, rd, world.grabOrg, basis[missing], t)) {
                    world.grabHit[0] = ro[0] + rd[0] * t;
                    world.grabHit[1] = ro[1] + rd[1] * t;
                    world.grabHit[2] = ro[2] + rd[2] * t;
                } else {
                    world.grabHit[0] = world.grabOrg[0];
                    world.grabHit[1] = world.grabOrg[1];
                    world.grabHit[2] = world.grabOrg[2];
                }
                float rel[3];
                vec3Sub(world.grabHit, world.grabOrg, rel);
                const float nd = vec3Dot(rel, basis[missing]);
                rel[0] -= basis[missing][0] * nd;
                rel[1] -= basis[missing][1] * nd;
                rel[2] -= basis[missing][2] * nd;
                world.grabAlong = vec3Len(rel);
                if (world.grabAlong < 0.05f) {
                    world.grabAlong = 0.05f;
                }
            }
        } else if (hit != kSceneActorNone) {
            world.selected = hit;
            world.drag = kSceneActorNone;
            world.hot = kSceneActorNone;
            sceneActorFitTool(world);
        }
    }
    Xform* live = actorXformMut(world, world.selected);
    if (world.drag > 6 || live == nullptr || down == 0) {
        return;
    }
    Xform& xform = *live;
    auto commitPos = [&](float nx, float ny, float nz) {
        if (world.gizmo != 4) {
            xform.pos[0] = nx;
            xform.pos[1] = ny;
            xform.pos[2] = nz;
            return;
        }
        const float delta[3] = {nx - world.grabPos[0], ny - world.grabPos[1], nz - world.grabPos[2]};
        float bx[3]{};
        float by[3]{};
        float bz[3]{};
        gizmoBasis(world, world.grabEuler, bx, by, bz);
        const float sx = std::fabs(xform.scale[0]) > 1.0e-4f ? xform.scale[0] : 1.0f;
        const float sy = std::fabs(xform.scale[1]) > 1.0e-4f ? xform.scale[1] : 1.0f;
        const float sz = std::fabs(xform.scale[2]) > 1.0e-4f ? xform.scale[2] : 1.0f;
        const float local[3] = {
            world.grabPivot[0] + vec3Dot(delta, bx) / sx,
            world.grabPivot[1] + vec3Dot(delta, by) / sy,
            world.grabPivot[2] + vec3Dot(delta, bz) / sz,
        };
        actorWritePivot(world, world.selected, local);
    };
    auto keepHandle = [&]() {
        if (world.gizmo == 4) {
            return;
        }
        float local[3]{};
        actorPivotLocal(world, world.selected, local);
        if (std::fabs(local[0]) < 1.0e-6f && std::fabs(local[1]) < 1.0e-6f && std::fabs(local[2]) < 1.0e-6f) {
            return;
        }
        float bx[3]{};
        float by[3]{};
        float bz[3]{};
        gizmoBasis(world, xform.euler, bx, by, bz);
        xform.pos[0] = world.grabOrg[0] - (bx[0] * local[0] * xform.scale[0] + by[0] * local[1] * xform.scale[1] + bz[0] * local[2] * xform.scale[2]);
        xform.pos[1] = world.grabOrg[1] - (bx[1] * local[0] * xform.scale[0] + by[1] * local[1] * xform.scale[1] + bz[1] * local[2] * xform.scale[2]);
        xform.pos[2] = world.grabOrg[2] - (bx[2] * local[0] * xform.scale[0] + by[2] * local[1] * xform.scale[1] + bz[2] * local[2] * xform.scale[2]);
    };
    float basis[3][3];
    gizmoBasis(world, world.grabEuler, basis[0], basis[1], basis[2]);
    const uint8_t part = world.drag;
    const bool snapMove = world.snapOn[0] != 0;
    const bool snapRot = world.snapOn[1] != 0;
    const bool snapScale = world.snapOn[2] != 0;
    if (world.gizmo == 1 && part < 3) {
        uint8_t mode = world.grabScreen;
        const float ang = actorRingAngle(world, x, y, world.grabPos, basis[part], mode);
        float delta = ang - world.grabAngle;
        if (delta > 180.0f) {
            delta -= 360.0f;
        }
        if (delta < -180.0f) {
            delta += 360.0f;
        }
        if (snapRot) {
            delta = actorSnap(delta, world.snapStep[1]);
        }
        actorSpinAround(world.grabEuler, basis[part], delta, xform.euler);
        keepHandle();
        return;
    }
    if (part == 6 && (world.gizmo == 0 || world.gizmo == 4)) {
        float alongRight = 0.0f;
        float alongUp = 0.0f;
        sceneActorScreenDelta(world, x, y, alongRight, alongUp);
        if (snapMove) {
            alongRight = actorSnap(alongRight, world.snapStep[0]);
            alongUp = actorSnap(alongUp, world.snapStep[0]);
        }
        commitPos(
            world.grabPos[0] + world.right[0] * alongRight + world.up[0] * alongUp,
            world.grabPos[1] + world.right[1] * alongRight + world.up[1] * alongUp,
            world.grabPos[2] + world.right[2] * alongRight + world.up[2] * alongUp);
        return;
    }
    if (part == 6 && world.gizmo == 2) {
        float factor = 1.0f + (x - world.grabPx) / 140.0f;
        if (factor < 0.05f) {
            factor = 0.05f;
        }
        for (int i = 0; i < 3; ++i) {
            float next = world.grabScale[i] * factor;
            if (snapScale) {
                next = actorSnap(next, world.snapStep[2]);
            }
            xform.scale[i] = next < 0.05f ? 0.05f : next;
        }
        keepHandle();
        return;
    }
    if (part >= 3 && part <= 5) {
        const uint8_t missing = static_cast<uint8_t>(part - 3);
        float ro[3];
        float rd[3];
        actorRay(world, x, y, ro, rd);
        float t = 0.0f;
        if (!actorRayPlane(ro, rd, world.grabOrg, basis[missing], t)) {
            return;
        }
        const float hitPt[3] = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
        float rel[3];
        vec3Sub(hitPt, world.grabPos, rel);
        const float nd = vec3Dot(rel, basis[missing]);
        rel[0] -= basis[missing][0] * nd;
        rel[1] -= basis[missing][1] * nd;
        rel[2] -= basis[missing][2] * nd;
        if (world.gizmo == 2) {
            const float dist = vec3Len(rel);
            float factor = dist / world.grabAlong;
            if (factor < 0.05f) {
                factor = 0.05f;
            }
            for (uint8_t a = 0; a < 3; ++a) {
                if (a == missing) {
                    continue;
                }
                float next = world.grabScale[a] * factor;
                if (snapScale) {
                    next = actorSnap(next, world.snapStep[2]);
                }
                xform.scale[a] = next < 0.05f ? 0.05f : next;
            }
            keepHandle();
            return;
        }
        float delta[3] = {
            hitPt[0] - world.grabHit[0],
            hitPt[1] - world.grabHit[1],
            hitPt[2] - world.grabHit[2],
        };
        const float dn = vec3Dot(delta, basis[missing]);
        delta[0] -= basis[missing][0] * dn;
        delta[1] -= basis[missing][1] * dn;
        delta[2] -= basis[missing][2] * dn;
        float acc[3] = {};
        for (uint8_t a = 0; a < 3; ++a) {
            if (a == missing) {
                continue;
            }
            float along = vec3Dot(delta, basis[a]);
            if (snapMove) {
                along = actorSnap(along, world.snapStep[0]);
            }
            acc[0] += basis[a][0] * along;
            acc[1] += basis[a][1] * along;
            acc[2] += basis[a][2] * along;
        }
        commitPos(world.grabPos[0] + acc[0], world.grabPos[1] + acc[1], world.grabPos[2] + acc[2]);
        return;
    }
    if (part > 2) {
        return;
    }
    float along = 0.0f;
    uint8_t screen = 0;
    actorAlong(world, x, y, world.grabOrg, basis[part], along, screen, static_cast<int>(world.grabScreen));
    if (screen != world.grabScreen) {
        return;
    }
    if (world.gizmo == 2) {
        float next = world.grabScale[part] * (along / world.grabAlong);
        if (snapScale) {
            next = actorSnap(next, world.snapStep[2]);
        }
        xform.scale[part] = next < 0.05f ? 0.05f : next;
        keepHandle();
        return;
    }
    float delta = along - world.grabAlong;
    if (snapMove) {
        delta = actorSnap(delta, world.snapStep[0]);
    }
    commitPos(
        world.grabPos[0] + basis[part][0] * delta,
        world.grabPos[1] + basis[part][1] * delta,
        world.grabPos[2] + basis[part][2] * delta);
}

} // namespace burnhope
