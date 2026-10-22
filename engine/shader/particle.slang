// Взрыв: искры, дым и осколки. Столкновение по глубине кадра, квад смотрит в камеру.
// Туман сюда не входит.

#include "frame.slang"
#include "camera.slang"

static const uint kParticleCap = 8192;
static const uint kSlice = 256 + 8192 * 64;

struct ParticlePush {
    uint flight;
    uint mode;
    float dt;
    uint pad;
};

[[vk::push_constant]]
ParticlePush gPush;

struct ParticleBurst {
    float4 posCount;
    float4 seedPad;
};

struct ParticleSpawn {
    uint4 head;
    ParticleBurst burst[32];
};

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(80, 0)]] RWByteAddressBuffer gPart;
[[vk::binding(81, 0)]] StructuredBuffer<ParticleSpawn> gSpawn;
[[vk::binding(82, 0)]] RWByteAddressBuffer gIndirect;

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

void storeParticle(uint slice, uint slot, float3 pos, float life, float3 vel, float maxLife, float4 color, float size, uint flags) {
    const uint o = slice + 256u + slot * 64u;
    gPart.Store(o + 0u, asuint(pos.x));
    gPart.Store(o + 4u, asuint(pos.y));
    gPart.Store(o + 8u, asuint(pos.z));
    gPart.Store(o + 12u, asuint(life));
    gPart.Store(o + 16u, asuint(vel.x));
    gPart.Store(o + 20u, asuint(vel.y));
    gPart.Store(o + 24u, asuint(vel.z));
    gPart.Store(o + 28u, asuint(maxLife));
    gPart.Store(o + 32u, asuint(color.x));
    gPart.Store(o + 36u, asuint(color.y));
    gPart.Store(o + 40u, asuint(color.z));
    gPart.Store(o + 44u, asuint(color.w));
    gPart.Store(o + 48u, asuint(size));
    gPart.Store(o + 52u, flags);
    gPart.Store(o + 56u, 0u);
    gPart.Store(o + 60u, 0u);
}

void loadParticle(uint slice, uint slot, out float3 pos, out float life, out float3 vel, out float maxLife, out float4 color, out float size, out uint flags) {
    const uint o = slice + 256u + slot * 64u;
    pos = float3(asfloat(gPart.Load(o + 0u)), asfloat(gPart.Load(o + 4u)), asfloat(gPart.Load(o + 8u)));
    life = asfloat(gPart.Load(o + 12u));
    vel = float3(asfloat(gPart.Load(o + 16u)), asfloat(gPart.Load(o + 20u)), asfloat(gPart.Load(o + 24u)));
    maxLife = asfloat(gPart.Load(o + 28u));
    color = float4(asfloat(gPart.Load(o + 32u)), asfloat(gPart.Load(o + 36u)), asfloat(gPart.Load(o + 40u)), asfloat(gPart.Load(o + 44u)));
    size = asfloat(gPart.Load(o + 48u));
    flags = gPart.Load(o + 52u);
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
    if (into <= 0.0 || into >= 0.28 || dot(vel, normal) >= 0.0) {
        return false;
    }
    pushOut = into + 0.02;
    return true;
}

void spawnOne(uint spawnIndex, uint dst) {
    const ParticleSpawn block = gSpawn[gPush.flight & 1u];
    uint left = spawnIndex;
    uint found = 0;
    ParticleBurst burst = block.burst[0];
    [loop]
    for (uint i = 0u; i < 32u; ++i) {
        if (i >= block.head.x) {
            break;
        }
        const uint n = uint(block.burst[i].posCount.w);
        if (left < n) {
            burst = block.burst[i];
            found = left;
            break;
        }
        left -= n;
    }
    const uint n = max(uint(burst.posCount.w), 1u);
    const uint fireN = n * 30u / 100u;
    const uint smokeN = n * 55u / 100u;
    uint flags = 4u;
    if (found < fireN) {
        flags = 2u;
    } else if (found < fireN + smokeN) {
        flags = 1u;
    }
    const uint h0 = hashU(asuint(burst.seedPad.x) ^ (found * 747796405u));
    const uint h1 = hashU(h0 ^ 0x9e3779b9u);
    const uint h2 = hashU(h1 ^ 0x85ebca6bu);
    const uint h3 = hashU(h2 ^ 0xc2b2ae35u);
    const float z = hashF(h0) * 2.0 - 1.0;
    const float ang = hashF(h1) * 6.2831853;
    const float ring = sqrt(max(1.0 - z * z, 0.0));
    float3 dir = float3(ring * cos(ang), abs(z) * 0.65 + 0.2, ring * sin(ang));
    dir = normalize(dir);
    float3 pos = burst.posCount.xyz + float3(0.0, 0.4, 0.0);
    float life = 1.0;
    float maxLife = 1.0;
    float size = 0.1;
    float3 vel = dir;
    float4 color = float4(1.0, 1.0, 1.0, 1.0);
    const float day = saturate(gSun.sunDirIntensity.y * 0.5 + 0.5);
    if (flags == 2u) {
        life = 0.35 + hashF(h2) * 0.5;
        maxLife = life;
        size = 0.1 + hashF(h3) * 0.22;
        vel = dir * (5.0 + hashF(h2) * 7.0) + float3(0.0, 2.0, 0.0);
        color = float4(1.0, 0.48 + hashF(h3) * 0.25, 0.08, 1.0);
    } else if (flags == 1u) {
        life = 2.4 + hashF(h2) * 2.6;
        maxLife = life;
        size = 0.35 + hashF(h3) * 0.55;
        vel = dir * (0.25 + hashF(h2) * 0.7) + float3(0.0, 1.1 + hashF(h3) * 1.4 + day * 0.3, 0.0);
        color = float4(0.11, 0.105, 0.095, 0.42);
    } else {
        life = 1.1 + hashF(h2) * 0.9;
        maxLife = life;
        size = 0.03 + hashF(h3) * 0.05;
        vel = dir * (3.0 + hashF(h2) * 6.0) + float3(0.0, 2.0 + hashF(h3) * 3.0, 0.0);
        color = float4(0.22, 0.12, 0.06, 0.85);
    }
    uint slot = 0;
    gPart.InterlockedAdd(dst, 1u, slot);
    if (slot >= kParticleCap) {
        return;
    }
    storeParticle(dst, slot, pos, life, vel, maxLife, color, size, flags);
}

void simOne(uint index, uint src, uint dst, float dt) {
    float3 pos;
    float life;
    float3 vel;
    float maxLife;
    float4 color;
    float size;
    uint flags;
    loadParticle(src, index, pos, life, vel, maxLife, color, size, flags);
    life -= dt;
    if (life <= 0.0) {
        return;
    }
    const float day = saturate(gSun.sunDirIntensity.y * 0.5 + 0.5);
    float3 acc = float3(0.0, -9.81, 0.0);
    float drag = 0.25;
    float rest = 0.35;
    if (flags == 1u) {
        acc.y += 12.0 + day;
        drag = 1.6;
        rest = 0.04;
        size = min(size + dt * 0.75, 2.6);
    } else if (flags == 2u) {
        acc.y = -3.2;
        drag = 0.7;
        rest = 0.12;
    }
    vel += acc * dt;
    vel *= exp(-drag * dt);
    pos += vel * dt;
    float3 normal;
    float pushOut;
    if (depthHit(pos, vel, normal, pushOut)) {
        const float vn = dot(vel, normal);
        vel = (vel - normal * vn * (1.0 + rest)) * (flags == 1u ? 0.35 : 0.72);
        if (flags == 1u && abs(vn) < 0.45) {
            vel -= normal * dot(vel, normal);
        }
        pos += normal * pushOut;
    }
    uint slot = 0;
    gPart.InterlockedAdd(dst, 1u, slot);
    if (slot >= kParticleCap) {
        return;
    }
    storeParticle(dst, slot, pos, life, vel, maxLife, color, size, flags);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void particleSim(uint3 id : SV_DispatchThreadID) {
    const uint dst = sliceOf(gPush.flight);
    const uint src = sliceOf(gPush.flight ^ 1u);
    if (gPush.mode == 0u) {
        if (id.x == 0u) {
            gPart.Store(dst, 0u);
        }
        return;
    }
    if (gPush.mode == 2u) {
        if (id.x == 0u) {
            const uint alive = min(gPart.Load(dst), kParticleCap);
            const uint groups = (alive + 31u) / 32u;
            const uint at = (gPush.flight & 1u) * 16u;
            gIndirect.Store(at, groups);
            gIndirect.Store(at + 4u, 1u);
            gIndirect.Store(at + 8u, 1u);
        }
        return;
    }
    const uint alive = gPart.Load(src);
    const uint spawn = gSpawn[gPush.flight & 1u].head.y;
    const uint n = alive + spawn;
    if (id.x >= n) {
        return;
    }
    const float dt = clamp(gPush.dt, 0.0, 0.05);
    if (id.x < alive) {
        simOne(id.x, src, dst, dt);
    } else {
        spawnOne(id.x - alive, dst);
    }
}

struct VertOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world : TEXCOORD2;
    float size : TEXCOORD3;
    float life : TEXCOORD4;
    nointerpolation uint flags : TEXCOORD5;
};

groupshared uint gCount;
groupshared float gPos[32 * 3];
groupshared float gLife[32];
groupshared float gMax[32];
groupshared float gColor[32 * 4];
groupshared float gSize[32];
groupshared uint gFlags[32];

void emitQuad(uint slot, float3 center, float size, float4 color, float lifeFrac, uint flags, out vertices VertOut verts[128], out indices uint3 prims[64]) {
    const float3 cam = gFrame.cameraPos.xyz;
    float3 toCam = cam - center;
    const float dist2 = dot(toCam, toCam);
    const float3 fwd = dist2 > 1e-8 ? toCam * rsqrt(dist2) : float3(0.0, 0.0, 1.0);
    const float3 worldUp = abs(fwd.y) > 0.98 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    const float3 right = normalize(cross(worldUp, fwd));
    const float3 up = cross(fwd, right);
    const float h = max(size, 0.01);
    const float3 c0 = center - right * h - up * h;
    const float3 c1 = center + right * h - up * h;
    const float3 c2 = center + right * h + up * h;
    const float3 c3 = center - right * h + up * h;
    const uint v = slot * 4u;
    const float3 corner[4] = {c0, c1, c2, c3};
    const float2 uv[4] = {float2(0.0, 1.0), float2(1.0, 1.0), float2(1.0, 0.0), float2(0.0, 0.0)};
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        VertOut o;
        o.pos = worldToClip(corner[i]);
        o.uv = uv[i];
        o.color = color;
        o.world = center;
        o.size = h;
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
void particleMesh(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, out vertices VertOut verts[128], out indices uint3 prims[64]) {
    if (tid.x == 0u) {
        gCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint slice = sliceOf(gPush.flight);
    const uint alive = min(gPart.Load(slice), kParticleCap);
    const uint index = gid.x * 32u + tid.x;
    if (index < alive) {
        float3 pos;
        float life;
        float3 vel;
        float maxLife;
        float4 color;
        float size;
        uint flags;
        loadParticle(slice, index, pos, life, vel, maxLife, color, size, flags);
        if (life > 0.0) {
            uint slot = 0u;
            InterlockedAdd(gCount, 1u, slot);
            gPos[slot * 3u] = pos.x;
            gPos[slot * 3u + 1u] = pos.y;
            gPos[slot * 3u + 2u] = pos.z;
            gLife[slot] = life;
            gMax[slot] = maxLife;
            gColor[slot * 4u] = color.x;
            gColor[slot * 4u + 1u] = color.y;
            gColor[slot * 4u + 2u] = color.z;
            gColor[slot * 4u + 3u] = color.w;
            gSize[slot] = size;
            gFlags[slot] = flags;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    SetMeshOutputCounts(gCount * 4u, gCount * 2u);
    if (tid.x < gCount) {
        const float3 pos = float3(gPos[tid.x * 3u], gPos[tid.x * 3u + 1u], gPos[tid.x * 3u + 2u]);
        const float4 color = float4(gColor[tid.x * 4u], gColor[tid.x * 4u + 1u], gColor[tid.x * 4u + 2u], gColor[tid.x * 4u + 3u]);
        const float frac = saturate(gLife[tid.x] / max(gMax[tid.x], 0.001));
        emitQuad(tid.x, pos, gSize[tid.x], color, frac, gFlags[tid.x], verts, prims);
    }
}

[shader("fragment")]
float4 particleFs(VertOut vin) : SV_Target0 {
    const float2 q = vin.uv * 2.0 - 1.0;
    const float r2 = dot(q, q);
    if (r2 > 1.0) {
        discard;
    }
    const float fall = exp(-r2 * 2.6);
    const uint2 px = uint2(vin.pos.xy);
    const float sceneD = gDepth.Load(int3(int2(px), 0));
    float soft = 1.0;
    if (sceneD > 0.0) {
        const float3 sceneP = reconstructWorld(px, sceneD);
        const float sceneDist = length(sceneP - gFrame.cameraPos.xyz);
        const float partDist = length(vin.world - gFrame.cameraPos.xyz);
        soft = saturate((sceneDist - partDist) / max(vin.size * 0.7, 0.04) + 0.25);
    }
    if (soft <= 0.001) {
        discard;
    }
    const float sunI = gSun.sunDirIntensity.w;
    const float3 sunDir = gSun.sunDirIntensity.xyz;
    const float3 view = normalize(gFrame.cameraPos.xyz - vin.world);
    const float wrap = saturate(dot(view, sunDir) * 0.35 + 0.62);
    if (vin.flags == 2u) {
        const float3 hot = vin.color.rgb * (0.6 + vin.life * 6.0) * fall * soft;
        const float a = fall * (0.05 + vin.life * 0.12) * soft;
        return float4(hot, a);
    }
    const float fade = saturate(vin.life * 4.0) * vin.life;
    float3 lit = vin.color.rgb * (gSun.sunColor.rgb * sunI * wrap * 0.2 + 0.045);
    float a = vin.color.a * fall * fade * soft;
    if (vin.flags == 4u) {
        a = min(a * 1.8, 0.85);
        lit *= 0.65;
    }
    return float4(lit * a, a);
}
