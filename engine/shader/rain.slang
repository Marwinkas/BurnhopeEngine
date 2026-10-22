// Ближний дождь: вытянутые капли в коробке камеры и короткий всплеск на ударе.
// Дальняя пелена живёт в тумане, влажность — в shade_hit.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

static const uint kRainCap = 4096;
static const uint kSlice = 4096 * 64;

struct RainPush {
    uint flight;
    uint mode;
    float dt;
    uint pad;
};

[[vk::push_constant]]
RainPush gPush;

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(83, 0)]] RWByteAddressBuffer gDrop;

uint sliceOf(uint flight) {
    return (flight & 1u) * kSlice;
}

uint hashU(uint x) {
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

float hashF(uint x) {
    return float(hashU(x) & 0x00ffffffu) / float(0x00ffffffu);
}

float miePhase(float cosT) {
    const float g = 0.78;
    const float g2 = g * g;
    return (1.0 / (4.0 * 3.14159265)) * (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-3), 1.5);
}

float lampAtt(float dist, float radius) {
    const float dist2 = max(dist * dist, 1e-4);
    const float falloff = 1.0 / max(radius * radius, 1e-4);
    const float factor = dist2 * falloff;
    const float smoothFactor = saturate(1.0 - factor * factor);
    return (smoothFactor * smoothFactor) / dist2;
}

void storeDrop(uint slice, uint slot, float3 pos, float life, float3 vel, float maxLife, float size, uint flags, uint seed) {
    const uint o = slice + slot * 64u;
    gDrop.Store(o + 0u, asuint(pos.x));
    gDrop.Store(o + 4u, asuint(pos.y));
    gDrop.Store(o + 8u, asuint(pos.z));
    gDrop.Store(o + 12u, asuint(life));
    gDrop.Store(o + 16u, asuint(vel.x));
    gDrop.Store(o + 20u, asuint(vel.y));
    gDrop.Store(o + 24u, asuint(vel.z));
    gDrop.Store(o + 28u, asuint(maxLife));
    gDrop.Store(o + 48u, asuint(size));
    gDrop.Store(o + 52u, flags);
    gDrop.Store(o + 56u, seed);
}

void loadDrop(uint slice, uint slot, out float3 pos, out float life, out float3 vel, out float maxLife, out float size, out uint flags, out uint seed) {
    const uint o = slice + slot * 64u;
    pos = float3(asfloat(gDrop.Load(o + 0u)), asfloat(gDrop.Load(o + 4u)), asfloat(gDrop.Load(o + 8u)));
    life = asfloat(gDrop.Load(o + 12u));
    vel = float3(asfloat(gDrop.Load(o + 16u)), asfloat(gDrop.Load(o + 20u)), asfloat(gDrop.Load(o + 24u)));
    maxLife = asfloat(gDrop.Load(o + 28u));
    size = asfloat(gDrop.Load(o + 48u));
    flags = gDrop.Load(o + 52u);
    seed = gDrop.Load(o + 56u);
}

bool depthHit(float3 pos, float3 vel, out float3 normal, out float pushOut) {
    normal = float3(0.0, 1.0, 0.0);
    pushOut = 0.0;
    const float4 clip = worldToClip(pos);
    if (clip.w < 0.2) {
        return false;
    }
    const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
        return false;
    }
    const uint2 px = uint2(uv * float2(gFrame.extent.xy));
    const float sceneD = gDepth.Load(int3(int2(px), 0));
    if (sceneD <= 0.0) {
        return false;
    }
    const float3 sceneP = reconstructWorld(px, sceneD);
    normal = reconstructNormalFromDepth(gDepth, px, sceneP);
    const float into = dot(sceneP - pos, normal);
    if (into <= 0.0 || into >= 0.22 || dot(vel, normal) >= 0.0) {
        return false;
    }
    pushOut = into + 0.015;
    return true;
}

RaytracingAccelerationStructure rainScene() {
    return RaytracingAccelerationStructure(uint64_t(asuint(gFrame.viewTail0.x)) | (uint64_t(asuint(gFrame.viewTail0.y)) << 32));
}

bool rainRoof(float3 pos) {
    if (asuint(gFrame.viewTail0.x) == 0u && asuint(gFrame.viewTail0.y) == 0u) {
        return false;
    }
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = float3(0.0, 1.0, 0.0);
    ray.TMin = 0.2;
    ray.TMax = 22.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(rainScene(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

float3 rainLook() {
    const float3 cam = gFrame.cameraPos.xyz;
    const float4 farC = mul(gFrame.invViewProj, float4(0.0, 0.0, 0.0, 1.0));
    const float4 nearC = mul(gFrame.invViewProj, float4(0.0, 0.0, 1.0, 1.0));
    const float3 farP = farC.xyz / max(abs(farC.w), 1e-5);
    const float3 nearP = nearC.xyz / max(abs(nearC.w), 1e-5);
    const float3 tip = dot(farP - cam, farP - cam) > dot(nearP - cam, nearP - cam) ? farP : nearP;
    const float3 dir = tip - cam;
    const float len = length(dir);
    return len > 1e-3 ? dir / len : float3(0.0, 0.0, 1.0);
}

float3 rainRight(float3 look) {
    const float3 side = cross(float3(0.0, 1.0, 0.0), look);
    const float len = length(side);
    return len > 1e-3 ? side / len : float3(1.0, 0.0, 0.0);
}

float3 rainFlat(float3 look) {
    float3 flat = look;
    flat.y = 0.0;
    const float len = length(flat);
    return len > 1e-3 ? flat / len : float3(0.0, 0.0, 1.0);
}

bool rainInside(float3 pos, float3 cam, float3 look, float3 right) {
    (void)right;
    const float3 flat = rainFlat(look);
    const float3 rel = pos - (cam + flat * 4.0);
    const float horiz = length(float2(rel.x, rel.z));
    const float y = pos.y - cam.y;
    return horiz <= 10.0 && y >= -3.0 && y <= 20.0;
}

void respawn(uint slot, uint dst, uint seed, float3 cam, float3 look, float3 right) {
    (void)right;
    seed += 1u;
    const uint h0 = hashU(slot * 747796405u ^ seed * 2246822519u);
    const uint h1 = hashU(h0 ^ 0x9e3779b9u);
    const uint h2 = hashU(h1 ^ 0x85ebca6bu);
    const float3 flat = rainFlat(look);
    const float3 side = rainRight(flat);
    const float ang = hashF(h0) * 6.2831853;
    const float rad = sqrt(hashF(h1)) * 9.0;
    const float y = -0.4 + hashF(h2) * 18.0;
    const float3 pos = cam + flat * (4.0 + cos(ang) * rad) + side * (sin(ang) * rad) + float3(0.0, y, 0.0);
    const float3 vel = float3(gAtm.reserved0 + (hashF(h0) - 0.5) * 0.5, -13.0 - hashF(h1) * 2.0, gAtm.reserved1 + (hashF(h2) - 0.5) * 0.5);
    if (rainRoof(pos)) {
        storeDrop(dst, slot, pos, 0.0, vel, 1.0, 0.008, 0u, seed);
        return;
    }
    storeDrop(dst, slot, pos, 1.0, vel, 1.0, 0.008, 1u, seed);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void rainSim(uint3 id : SV_DispatchThreadID) {
    if (id.x >= kRainCap) {
        return;
    }
    const uint dst = sliceOf(gPush.flight);
    const uint src = sliceOf(gPush.flight ^ 1u);
    const float dt = clamp(gPush.dt, 0.0, 0.05);
    const float3 cam = gFrame.cameraPos.xyz;
    const float3 look = rainLook();
    const float3 right = rainRight(look);
    float3 pos;
    float life;
    float3 vel;
    float maxLife;
    float size;
    uint flags;
    uint seed;
    loadDrop(src, id.x, pos, life, vel, maxLife, size, flags, seed);
    const float amt = saturate(gFrame.viewPad.w * 0.25);
    if (hashF(id.x * 747796405u) > amt) {
        storeDrop(dst, id.x, pos, 0.0, vel, 1.0, 0.008, 0u, seed);
        return;
    }
    if (life <= 0.0 || flags == 0u) {
        respawn(id.x, dst, seed, cam, look, right);
        return;
    }
    if (flags == 2u || flags == 5u) {
        life -= dt;
        vel *= exp(-6.0 * dt);
        pos += vel * dt;
        if (life <= 0.0) {
            respawn(id.x, dst, seed, cam, look, right);
            return;
        }
        storeDrop(dst, id.x, pos, life, vel, maxLife, size, 5u, seed);
        return;
    }
    if (flags == 3u) {
        life -= dt;
        vel *= exp(-6.0 * dt);
        pos += vel * dt;
        if (life <= 0.0) {
            const float radius = lerp(0.035, 0.06, hashF(seed));
            storeDrop(dst, id.x, pos + float3(0.0, 0.03, 0.0), 1.05, 0.0, 1.05, radius, 4u, seed);
            return;
        }
        storeDrop(dst, id.x, pos, life, vel, maxLife, size, 3u, seed);
        return;
    }
    if (flags == 4u) {
        life -= dt;
        if (life <= 0.0 || !rainInside(pos, cam, look, right)) {
            respawn(id.x, dst, seed, cam, look, right);
            return;
        }
        storeDrop(dst, id.x, pos, life, 0.0, maxLife, size, 4u, seed);
        return;
    }
    if (!rainInside(pos, cam, look, right) || rainRoof(pos)) {
        respawn(id.x, dst, seed, cam, look, right);
        return;
    }
    const uint h = hashU(id.x * 1973u ^ asuint(floor(pos.x * 2.0)) ^ asuint(floor(pos.z * 2.0)) ^ asuint(gFrame.viewPad.z));
    const float3 turb = float3(hashF(h) - 0.5, (hashF(hashU(h)) - 0.5) * 0.35, hashF(hashU(h ^ 17u)) - 0.5) * 2.8;
    const float3 wind = float3(gAtm.reserved0, -13.0, gAtm.reserved1);
    vel = lerp(vel, wind, 1.0 - exp(-1.6 * dt));
    vel += turb * dt;
    pos += vel * dt;
    if (rainRoof(pos)) {
        respawn(id.x, dst, seed, cam, look, right);
        return;
    }
    float3 normal;
    float pushOut;
    if (depthHit(pos, vel, normal, pushOut)) {
        pos += normal * pushOut;
        float3 slide = vel - normal * dot(vel, normal);
        slide *= 0.18;
        const float fade = 0.26;
        const uint next = normal.y > 0.4 ? 3u : 5u;
        storeDrop(dst, id.x, pos, fade, slide, fade, size, next, seed);
        return;
    }
    storeDrop(dst, id.x, pos, 1.0, vel, 1.0, 0.008, 1u, seed);
}

struct RainOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
    float3 vel : TEXCOORD2;
    float size : TEXCOORD3;
    float life : TEXCOORD4;
    nointerpolation uint flags : TEXCOORD5;
};

groupshared uint gCount;
groupshared float gPos[32 * 3];
groupshared float gVel[32 * 3];
groupshared float gLife[32];
groupshared float gMax[32];
groupshared float gSize[32];
groupshared uint gFlags[32];

void emitQuad(uint slot, float3 center, float3 vel, float size, float lifeFrac, uint flags, out vertices RainOut verts[128], out indices uint3 prims[64]) {
    const float3 cam = gFrame.cameraPos.xyz;
    float3 toCam = cam - center;
    const float dist2 = max(dot(toCam, toCam), 1e-8);
    toCam *= rsqrt(dist2);
    float3 axis = float3(0.0, -1.0, 0.0);
    float halfL = 0.08;
    float halfW = 0.0035;
    bool flat = false;
    if (flags == 4u) {
        flat = true;
        const float grow = lerp(0.4, 1.0, saturate((1.0 - lifeFrac) / 0.18));
        halfL = max(size, 0.03) * grow;
        halfW = halfL;
        axis = float3(0.0, 1.0, 0.0);
    } else {
        const float speed = length(vel);
        if (speed > 0.2) {
            axis = vel / speed;
        }
        const float shrink = (flags == 3u || flags == 5u) ? max(lifeFrac, 0.02) : 1.0;
        halfL = clamp(speed * 0.007, 0.05, 0.11) * shrink;
        halfW = 0.004 * shrink;
    }
    float3 right = cross(axis, toCam);
    if (dot(right, right) < 1e-6) {
        right = cross(axis, float3(1.0, 0.0, 0.0));
    }
    right = normalize(right);
    const float3 up = flat ? normalize(cross(right, axis)) : axis;
    const float3 c0 = center - right * halfW - up * halfL;
    const float3 c1 = center + right * halfW - up * halfL;
    const float3 c2 = center + right * halfW + up * halfL;
    const float3 c3 = center - right * halfW + up * halfL;
    const uint v = slot * 4u;
    const float3 corner[4] = {c0, c1, c2, c3};
    const float2 uv[4] = {float2(0.0, 1.0), float2(1.0, 1.0), float2(1.0, 0.0), float2(0.0, 0.0)};
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        RainOut o;
        o.pos = worldToClip(corner[i]);
        o.uv = uv[i];
        o.world = center;
        o.vel = vel;
        o.size = halfL;
        o.life = lifeFrac;
        o.flags = flags;
        verts[v + i] = o;
    }
    prims[slot * 2u] = uint3(v, v + 1u, v + 2u);
    prims[slot * 2u + 1u] = uint3(v, v + 2u, v + 3u);
}

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void rainMesh(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, out vertices RainOut verts[128], out indices uint3 prims[64]) {
    if (tid.x == 0u) {
        gCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint slice = sliceOf(gPush.flight);
    const uint index = gid.x * 32u + tid.x;
    if (index < kRainCap) {
        float3 pos;
        float life;
        float3 vel;
        float maxLife;
        float size;
        uint flags;
        uint seed;
        loadDrop(slice, index, pos, life, vel, maxLife, size, flags, seed);
        if (life > 0.0 && flags != 0u) {
            uint slot = 0u;
            InterlockedAdd(gCount, 1u, slot);
            gPos[slot * 3u] = pos.x;
            gPos[slot * 3u + 1u] = pos.y;
            gPos[slot * 3u + 2u] = pos.z;
            gVel[slot * 3u] = vel.x;
            gVel[slot * 3u + 1u] = vel.y;
            gVel[slot * 3u + 2u] = vel.z;
            gLife[slot] = life;
            gMax[slot] = maxLife;
            gSize[slot] = size;
            gFlags[slot] = flags;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    SetMeshOutputCounts(gCount * 4u, gCount * 2u);
    if (tid.x < gCount) {
        const float3 pos = float3(gPos[tid.x * 3u], gPos[tid.x * 3u + 1u], gPos[tid.x * 3u + 2u]);
        const float3 vel = float3(gVel[tid.x * 3u], gVel[tid.x * 3u + 1u], gVel[tid.x * 3u + 2u]);
        const float fracLife = saturate(gLife[tid.x] / max(gMax[tid.x], 0.001));
        emitQuad(tid.x, pos, vel, gSize[tid.x], fracLife, gFlags[tid.x], verts, prims);
    }
}

float3 dropLight(float3 world, float3 fallDir) {
    const float3 outV = normalize(gFrame.cameraPos.xyz - world);
    const float3 sunDir = gSun.sunDirIntensity.xyz;
    const float sunLen = length(sunDir);
    float3 col = 0.0;
    if (sunLen > 1e-4) {
        col += gSun.sunColor.rgb * gSun.sunDirIntensity.w * miePhase(dot(outV, sunDir / sunLen)) * 0.035;
    }
    [unroll]
    for (int i = 0; i < 3; ++i) {
        const float4 lamp = gSun.pointPos[i];
        const float4 color = gSun.pointColor[i];
        if (color.w < 1e-4 || lamp.w < 0.05) {
            continue;
        }
        const float3 toL = lamp.xyz - world;
        const float dist = length(toL);
        if (dist < 1e-3) {
            continue;
        }
        const float3 ldir = toL / dist;
        col += color.rgb * color.w * lampAtt(dist, lamp.w) * miePhase(dot(outV, ldir));
    }
    return col * saturate(0.35 + dot(outV, -fallDir) * 0.4);
}

[shader("fragment")]
float4 rainFs(RainOut vin) : SV_Target0 {
    const float2 q = vin.uv * 2.0 - 1.0;
    float body = 0.0;
    if (vin.flags == 4u) {
        const float r = length(q);
        if (r > 1.0) {
            discard;
        }
        const float disc = saturate(1.0 - r * r);
        const float rim = exp(-pow((r - 0.62) * 7.0, 2.0));
        body = disc * 0.35 + rim;
    } else {
        if (abs(q.x) > 0.45 || abs(q.y) > 1.0) {
            discard;
        }
        body = exp(-q.x * q.x * 22.0) * exp(-q.y * q.y * 1.6);
    }
    const uint2 px = uint2(vin.pos.xy);
    const float sceneD = gDepth.Load(int3(int2(px), 0));
    float soft = 1.0;
    if (sceneD > 0.0) {
        const float3 sceneP = reconstructWorld(px, sceneD);
        const float sceneDist = length(sceneP - gFrame.cameraPos.xyz);
        const float partDist = length(vin.world - gFrame.cameraPos.xyz);
        soft = saturate((sceneDist - partDist) / 0.2 + 0.35);
    }
    const float camDist = length(vin.world - gFrame.cameraPos.xyz);
    soft *= saturate((camDist - 0.15) / 0.25);
    if (soft <= 0.001 || body <= 0.001) {
        discard;
    }
    const float3 fall = length(vin.vel) > 0.2 ? normalize(vin.vel) : float3(0.0, -1.0, 0.0);
    float3 col = dropLight(vin.world, fall);
    col = max(col, float3(0.82, 0.86, 0.9) * 0.55);
    float gain = body * soft * 1.8;
    if (vin.flags == 3u || vin.flags == 5u) {
        gain *= vin.life * vin.life;
    }
    if (vin.flags == 4u) {
        col = float3(0.62, 0.66, 0.7) * 0.16 + col * 0.25;
        gain *= smoothstep(0.0, 0.4, vin.life) * 0.7;
    }
    return float4(col * gain, 0.0);
}
