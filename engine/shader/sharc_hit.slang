// Попадание в тот же TLAS, что и каскады: 48 байт на вершину в HeapBuf::Rc.

[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(25, 0)]] StructuredBuffer<LightGpu> gLights;
[[vk::binding(30, 0)]] ByteAddressBuffer gGeom;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;

struct LightGpu {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

struct SharcSurf {
    float3 position;
    float3 geoNormal;
    float3 albedo;
    float3 demod;
    float roughness;
    float metallic;
    float3 emissive;
    bool hit;
};

uint sharcRng(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float sharcRand(inout uint state) {
    return float(sharcRng(state) >> 8) * (1.0 / 16777216.0);
}

RaytracingAccelerationStructure sharcScene() {
    return RaytracingAccelerationStructure(uint64_t(gSharc.tlasLo) | (uint64_t(gSharc.tlasHi) << 32));
}

float3 sharcLoadPos(uint vertex) {
    uint o = gGeom.Load(12u) + vertex * 48u;
    return float3(asfloat(gGeom.Load(o)), asfloat(gGeom.Load(o + 4u)), asfloat(gGeom.Load(o + 8u)));
}

float2 sharcLoadUv(uint vertex) {
    uint o = gGeom.Load(12u) + vertex * 48u;
    return float2(asfloat(gGeom.Load(o + 12u)), asfloat(gGeom.Load(o + 28u)));
}

SharcSurf sharcMissSurf() {
    SharcSurf surf;
    surf.position = 0.0;
    surf.geoNormal = float3(0.0, 1.0, 0.0);
    surf.albedo = 0.0;
    surf.demod = 1.0;
    surf.roughness = 1.0;
    surf.metallic = 0.0;
    surf.emissive = 0.0;
    surf.hit = false;
    return surf;
}

SharcSurf sharcSurface(uint prim, float2 bary, float3 position, float3 view) {
    SharcSurf surf = sharcMissSurf();
    uint tris = gGeom.Load(168u);
    uint posByte = gGeom.Load(12u);
    if (prim >= tris || posByte < 48u) {
        return surf;
    }
    uint a = prim * 3u;
    float3 p0 = sharcLoadPos(a);
    float3 p1 = sharcLoadPos(a + 1u);
    float3 p2 = sharcLoadPos(a + 2u);
    float3 face = cross(p1 - p0, p2 - p0);
    if (dot(face, face) < 1e-12) {
        return surf;
    }
    face = normalize(face);
    if (dot(face, view) < 0.0) {
        face = -face;
    }
    float w0 = saturate(1.0 - bary.x - bary.y);
    float w1 = saturate(bary.x);
    float w2 = saturate(bary.y);
    float2 uv = sharcLoadUv(a) * w0 + sharcLoadUv(a + 1u) * w1 + sharcLoadUv(a + 2u) * w2;
    uint matId = gGeom.Load(posByte + a * 48u + 32u);
    uint matCount;
    uint matStride;
    gMat.GetDimensions(matCount, matStride);
    float3 albedo = 0.6;
    float rough = 0.8;
    float metal = 0.0;
    float3 emissive = 0.0;
    if (matCount > 0u) {
        MaterialGpu mat = gMat[min(matId, matCount - 1u)];
        albedo = max(mat.baseColor.rgb, 0.0);
        rough = saturate(mat.roughness);
        metal = saturate(mat.metallic);
        emissive = max(mat.baseColor.rgb, 0.0) * max(mat.emissive, 0.0) * 8.0;
        if (mat.albedo > 0u && mat.albedo < 768u) {
            float4 texel = gTex[NonUniformResourceIndex(mat.albedo)].SampleLevel(gWrap, uv, 2.0);
            albedo *= max(texel.rgb, 0.0);
        }
        if (mat.orm > 0u && mat.orm < 768u) {
            float3 orm = gTex[NonUniformResourceIndex(mat.orm)].SampleLevel(gWrap, uv, 2.0).rgb;
            rough = saturate(orm.g);
            metal = saturate(orm.b);
        }
        if (mat.emissiveTex > 0u && mat.emissiveTex < 768u) {
            emissive *= max(gTex[NonUniformResourceIndex(mat.emissiveTex)].SampleLevel(gWrap, uv, 2.0).rgb, 0.0);
        }
    }
    float3 diffuse = albedo * (1.0 - metal);
    surf.position = position;
    surf.geoNormal = face;
    surf.albedo = clamp(albedo, 0.02, 0.95);
    surf.demod = clamp(diffuse, float3(0.1, 0.1, 0.1), float3(1.0, 1.0, 1.0));
    surf.roughness = rough;
    surf.metallic = metal;
    surf.emissive = max(emissive, 0.0);
    surf.hit = true;
    return surf;
}

bool sharcTrace(float3 origin, float3 direction, float tMin, float tMax, out float hitT, out uint prim, out float2 bary) {
    hitT = 0.0;
    prim = 0u;
    bary = 0.0;
    if (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u) {
        return false;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = max(tMax, tMin + 1e-3);
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(sharcScene(), RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
    query.Proceed();
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return false;
    }
    hitT = query.CommittedRayT();
    prim = query.CommittedPrimitiveIndex();
    bary = query.CommittedTriangleBarycentrics();
    return true;
}

float sharcSunVis(float3 position, float3 normal, float3 sunDir, float ndl) {
    if ((gPass.passMask & 256u) == 0u) {
        return 0.0;
    }
    if (ndl <= 0.005) {
        return 0.0;
    }
    if (gFrame.disableShadow != 0u || (gFrame.debugPad0 & 0x80000000u) != 0u) {
        return 1.0;
    }
    float hitT = 0.0;
    uint prim = 0u;
    float2 bary = 0.0;
    bool blocked = sharcTrace(position + normal * 0.03, sunDir, 0.04, 150.0, hitT, prim, bary);
    return blocked ? 0.15 : 1.0;
}

float3 sharcSunDir() {
    float3 sun = gSun.sunDirIntensity.xyz;
    return dot(sun, sun) > 1e-8 ? normalize(sun) : float3(0.0, 1.0, 0.0);
}

float3 sharcSunEnergy(float3 sunCol) {
    return max(sunCol, 0.0);
}

float3 sharcSkyFill() {
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    float3 sky = float3(0.78, 0.82, 0.88) * (inten * 0.06);
    if ((gPass.passMask & 256u) == 0u) {
        return 0.0;
    }
    return sky;
}

float3 sharcBasis(float3 normal, float3 h) {
    float3 axis = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(axis, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * h.x + bitangent * h.y + normal * h.z);
}

float3 sharcCosine(float3 normal, float2 u) {
    float phi = 6.2831853 * u.x;
    float radius = sqrt(u.y);
    float z = sqrt(max(1.0 - u.y, 0.0));
    return sharcBasis(normal, float3(radius * cos(phi), radius * sin(phi), z));
}

// Полный расчёт прямого света в точке отскока: учитывает солнце + ВСЕ лампы в радиусе досягаемости (без мёртвых зон!)
float3 sharcDirectLite(SharcSurf surf, float3 view) {
    float3 sunDir = sharcSunDir();
    float ndl = saturate(dot(surf.geoNormal, sunDir));
    float ndv = saturate(dot(surf.geoNormal, view));
    float3 diffuse = surf.albedo * (1.0 - surf.metallic);
    float3 sunCol = gSun.sunColor.rgb;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.92, 0.76);
    }
    float inten = gSun.sunDirIntensity.w;
    if ((gPass.passMask & 256u) == 0u) {
        inten = 0.0;
    } else if (inten < 0.05) {
        inten = 8.0;
    }
    float vis = sharcSunVis(surf.position, surf.geoNormal, sunDir, ndl);
    float rough = max(surf.roughness, 0.045);
    float diff = hammonDiffuse(ndl, max(ndv, 0.15), dot(view, sunDir), rough);
    float3 direct = (diffuse * diff) * vis * sharcSunEnergy(sunCol) * max(gSun.pad.w, 0.0) * (inten > 0.0 ? 1.0 : 0.0);

    float wrap = saturate(surf.geoNormal.y * 0.4 + 0.6);
    direct += diffuse * sharcSkyFill() * wrap;

    if (gSharc.lightCount > 0u && (gPass.passMask & 1u) != 0u) {
        uint count = min(gSharc.lightCount, 64u);
        float3 lampSum = 0.0;
        [loop]
        for (uint i = 0u; i < count; ++i) {
            LightGpu lamp = gLights[i];
            uint kind = lamp.meta.x;
            float radius = lamp.posRadius.w;
            if (kind == 0u || radius <= 0.0) {
                continue;
            }
            float3 toLight = lamp.posRadius.xyz - surf.position;
            float dist2 = dot(toLight, toLight);
            if (dist2 >= radius * radius || dist2 <= 1e-6) {
                continue;
            }
            float dist = sqrt(dist2);
            float3 L = toLight / dist;
            float lampNdl = saturate(dot(surf.geoNormal, L));
            if (lampNdl <= 0.0) {
                continue;
            }
            float atten = filamentDistanceAttenuation(dist, radius);
            if (kind == 2u) {
                float cd = dot(L, -normalize(lamp.dirAngle.xyz));
                float outer = cos(max(lamp.dirAngle.w, 0.05));
                float inner = max(outer + 0.12, cos(max(lamp.dirAngle.w * 0.55, 0.02)));
                float cone = saturate((cd - outer) / max(inner - outer, 1e-3));
                atten *= cd < outer ? 0.0 : cone * cone;
            }
            float lampDiff = hammonDiffuse(lampNdl, max(ndv, 0.15), dot(view, L), rough);
            float3 lampCol = lamp.colorIntensity.rgb * candelaScale(lamp.colorIntensity.w);
            lampSum += diffuse * lampDiff * atten * lampCol;
        }
        direct += min(lampSum, float3(4.0, 4.0, 4.0));
    }
    float3 glow = max(surf.emissive, 0.0);
    float glowLuma = dot(glow, float3(0.2126, 0.7152, 0.0722));
    if (glowLuma > 8.0) {
        glow *= 8.0 / glowLuma;
    }
    return sharcClampFirefly(max(direct + glow, 0.0));
}

float3 sharcDirect(SharcSurf surf, float3 view, inout uint rng) {
    return sharcDirectLite(surf, view);
}

bool sharcSampleBounce(SharcSurf surf, float3 view, inout uint rng, out float3 direction, out float3 weight, out uint diffuseLobe) {
    float2 u = float2(sharcRand(rng), sharcRand(rng));
    direction = sharcCosine(surf.geoNormal, u);
    diffuseLobe = 1u;
    weight = surf.albedo * (1.0 - surf.metallic);
    return dot(surf.geoNormal, direction) > 0.02;
}