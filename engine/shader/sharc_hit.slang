// Попадание в тот же TLAS, что и каскады: 48 байт на вершину в HeapBuf::Rc.
// Прямой свет — солнце с теневым лучом, эмиссия и один аналитический источник, как в сэмпле SHaRC.

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
    float emissive;
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
    float emissive = 0.0;
    if (matCount > 0u) {
        MaterialGpu mat = gMat[min(matId, matCount - 1u)];
        albedo = max(mat.baseColor.rgb, 0.0);
        rough = saturate(mat.roughness);
        metal = saturate(mat.metallic);
        emissive = max(mat.emissive, 0.0);
        if (mat.albedo > 0u && mat.albedo < 768u) {
            float4 texel = gTex[NonUniformResourceIndex(mat.albedo)].SampleLevel(gWrap, uv, 0.0);
            albedo *= max(texel.rgb, 0.0);
        }
        if (mat.orm > 0u && mat.orm < 768u) {
            float3 orm = gTex[NonUniformResourceIndex(mat.orm)].SampleLevel(gWrap, uv, 0.0).rgb;
            rough = saturate(orm.g);
            metal = saturate(orm.b);
        }
        if (mat.emissiveTex > 0u && mat.emissiveTex < 768u) {
            emissive *= max(gTex[NonUniformResourceIndex(mat.emissiveTex)].SampleLevel(gWrap, uv, 0.0).r, 0.0);
        }
    }
    float3 diffuse = albedo * (1.0 - metal);
    float3 f0 = computeF0(albedo, metal, computeDielectricF0(0.5));
    float3 fAvg = f0 + (1.0 - f0) * (1.0 / 21.0);
    surf.position = position;
    surf.geoNormal = face;
    surf.albedo = albedo;
    surf.demod = max(diffuse, float3(0.05, 0.05, 0.05)) + max(f0, float3(0.02, 0.02, 0.02)) * sharcLuma(fAvg);
    surf.roughness = rough;
    surf.metallic = metal;
    surf.emissive = emissive;
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
    if (ndl <= 0.005 || gFrame.disableShadow != 0u || (gPass.passMask & 256u) == 0u) {
        return ndl > 0.005 && (gPass.passMask & 256u) != 0u ? 1.0 : 0.0;
    }
    float hitT = 0.0;
    uint prim = 0u;
    float2 bary = 0.0;
    bool blocked = sharcTrace(position + normal * 0.02, sunDir, 0.05, 500.0, hitT, prim, bary);
    return blocked ? 0.0 : 1.0;
}

float3 sharcSunDir() {
    float3 sun = gSun.sunDirIntensity.xyz;
    return dot(sun, sun) > 1e-8 ? normalize(sun) : float3(0.0, 1.0, 0.0);
}

float3 sharcSky(float3 direction) {
    if ((gPass.passMask & 1024u) == 0u) {
        return float3(0.03, 0.035, 0.045);
    }
    float3 sky = max(gSkyView.SampleLevel(gClamp, atmSkyUv(direction), 0.0).rgb, 0.0);
    if ((gPass.passMask & 256u) == 0u) {
        sky *= 0.03;
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

float3 sharcDirect(SharcSurf surf, float3 view, inout uint rng) {
    float3 sunDir = sharcSunDir();
    float ndl = saturate(dot(surf.geoNormal, sunDir));
    float ndv = saturate(dot(surf.geoNormal, view));
    float3 diffuse = surf.albedo * (1.0 - surf.metallic);
    float3 f0 = computeF0(surf.albedo, surf.metallic, computeDielectricF0(0.5));
    float3 sunCol = gSun.sunColor.rgb;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.97, 0.92);
    }
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    float vis = sharcSunVis(surf.position, surf.geoNormal, sunDir, ndl);
    float rough = max(surf.roughness, 0.045);
    float diff = hammonDiffuse(ndl, max(ndv, 0.15), dot(view, sunDir), rough);
    float3 H = normalize(sunDir + view);
    float a = max(rough * rough, 0.002);
    float spec = D_GGX(saturate(dot(surf.geoNormal, H)), a)
        * V_SmithGGXCorrelated(ndv, ndl, a)
        * sharcLuma(F_Schlick(saturate(dot(view, H)), f0));
    float3 direct = (diffuse * diff + f0 * spec * ndl) * vis * sunCol * inten;
    direct += surf.albedo * surf.emissive;
    if (gSharc.lightCount > 0u && (gPass.passMask & 1u) != 0u) {
        uint index = sharcRng(rng) % gSharc.lightCount;
        LightGpu lamp = gLights[index];
        uint kind = lamp.meta.x;
        float3 toLight = lamp.posRadius.xyz - surf.position;
        float dist = length(toLight);
        float radius = lamp.posRadius.w;
        if (kind != 0u && radius > 0.0 && dist < radius && dist > 1e-3) {
            float3 L = toLight / dist;
            float lampNdl = saturate(dot(surf.geoNormal, L));
            float atten = filamentDistanceAttenuation(dist, radius);
            if (kind == 2u) {
                float cd = dot(L, -normalize(lamp.dirAngle.xyz));
                float outer = cos(max(lamp.dirAngle.w, 0.05));
                float inner = max(outer + 0.12, cos(max(lamp.dirAngle.w * 0.55, 0.02)));
                float cone = saturate((cd - outer) / max(inner - outer, 1e-3));
                atten *= cd < outer ? 0.0 : cone * cone;
            }
            float lampVis = 1.0;
            if (lampNdl > 0.005 && gFrame.disableShadow == 0u) {
                float lampT = 0.0;
                uint lampPrim = 0u;
                float2 lampBary = 0.0;
                if (sharcTrace(surf.position + surf.geoNormal * 0.02, L, 0.02, max(dist - 0.04, 0.05), lampT, lampPrim, lampBary)) {
                    lampVis = 0.0;
                }
            }
            float lampDiff = hammonDiffuse(lampNdl, max(ndv, 0.15), dot(view, L), rough);
            float3 lampCol = lamp.colorIntensity.rgb * lamp.colorIntensity.w;
            float3 lampRad = diffuse * lampDiff * atten * lampVis * lampCol * float(gSharc.lightCount);
            float peak = max(lampRad.r, max(lampRad.g, lampRad.b));
            if (peak > 8.0) {
                lampRad *= 8.0 / peak;
            }
            direct += lampRad;
        }
    }
    return max(direct, 0.0);
}

bool sharcSampleBounce(SharcSurf surf, float3 view, inout uint rng, out float3 direction, out float3 weight, out float lobeRough) {
    direction = surf.geoNormal;
    weight = 0.0;
    lobeRough = 1.0;
    float3 f0 = computeF0(surf.albedo, surf.metallic, computeDielectricF0(0.5));
    float specProb = saturate(max(f0.r, max(f0.g, f0.b)));
    specProb *= saturate(1.0 - surf.roughness);
    float2 u = float2(sharcRand(rng), sharcRand(rng));
    if (sharcRand(rng) < specProb) {
        direction = ggxReflect(surf.geoNormal, view, surf.roughness, u);
        lobeRough = surf.roughness;
        float ndl = saturate(dot(surf.geoNormal, direction));
        if (ndl < 0.02) {
            return false;
        }
        float3 H = normalize(direction + view);
        weight = F_Schlick(saturate(dot(view, H)), f0) / max(specProb, 1e-3);
        return true;
    }
    direction = sharcCosine(surf.geoNormal, u);
    lobeRough = 1.0;
    weight = surf.albedo * (1.0 - surf.metallic) / max(1.0 - specProb, 1e-3);
    return dot(surf.geoNormal, direction) > 0.02;
}
