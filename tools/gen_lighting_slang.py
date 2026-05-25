#!/usr/bin/env python3
"""Generate lighting.comp.slang from lighting.comp with bindless heap bindings."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GLSL = ROOT / "shaders" / "lighting.comp"
OUT = ROOT / "shaders" / "lighting.comp.slang"

HEADER = r'''import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;
import common.VsmSample;

struct LightingHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferAlbedo;
    uint gbufferHeightAo;
    uint gbufferDepth;
    uint gbufferEmissive;
    uint gbufferPortalId;
    uint shadowCsm;
    uint shadowAtlas;
    uint blueNoise;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint decalBuffer;
    uint hdrOutput;
    uint giDiffuse;
    uint giSpecular;
    uint gtaoOutput;
    uint rtReflections;
    uint vsmAtlas;
    uint vsmPageTable;
    uint portalUbos;
    uint volumetric;
    uint defaultSampler;
};

[[vk::push_constant]] LightingHeapPC pc;

static uint2 g_lightingThreadPx;

static const float PI = 3.14159265359;
static const float atlasResolution = 4096.0;

static const float2 lightingPoissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

#define UBO loadGlobalUbo(pc.globalUbo)
#define G_NORMAL heapTex2D(pc.gbufferNormal)
#define G_ALBEDO heapTex2D(pc.gbufferAlbedo)
#define G_HEIGHT heapTex2D(pc.gbufferHeightAo)
#define G_DEPTH heapTex2D(pc.gbufferDepth)
#define G_EMISSIVE heapTex2D(pc.gbufferEmissive)
#define G_PORTAL heapTex2D_u(pc.gbufferPortalId)
#define SUN_SHADOW heapTex2DArray(pc.shadowCsm)
#define SHADOW_ATLAS heapTex2D_r(pc.shadowAtlas)
#define NOISE_TEX heapTex2D_r(pc.blueNoise)
#define GI_DIFF heapTex2D(pc.giDiffuse)
#define GI_SPEC heapTex2D(pc.giSpecular)
#define VOL_FOG heapTex2D(pc.volumetric)
#define VSM_ATLAS heapTex2D_r(pc.vsmAtlas)
#define HDR_OUT heapRWTex2D(pc.hdrOutput)
#define GTAO_IMG heapRWTex2D_r(pc.gtaoOutput)
#define RT_REFL heapRWTex2D(pc.rtReflections)
#define SAM heapSampler(pc.defaultSampler)

'''

def main():
    body = GLSL.read_text(encoding='utf-8')
    # strip through first void main or after poisson disk
    idx = body.find('float DistributionGGX')
    if idx < 0:
        idx = body.find('void main()')
    body = body[idx:]

    reps = [
        ('GlobalUboStruct', 'GlobalUbo'),
        ('vec2', 'float2'), ('vec3', 'float3'), ('vec4', 'float4'),
        ('ivec2', 'int2'), ('ivec3', 'int3'), ('ivec4', 'int4'),
        ('mat2', 'float2x2'), ('mat3', 'float3x3'), ('mat4', 'float4x4'),
        ('mix(', 'lerp('),
        ('fract(', 'frac('),
        ('poissonDisk[', 'lightingPoissonDisk['),
        ('rot * lightingPoissonDisk[i]', 'mul(rot, lightingPoissonDisk[i])'),
        ('gl_GlobalInvocationID.xy', 'g_lightingThreadPx'),
        ('gl_GlobalInvocationID', 'id'),
        ('ubo.', 'UBO.'),
        ('currentUbo.', 'currentUbo.'),
        ('textureLod(shadowAtlas,', 'SHADOW_ATLAS.SampleLevel(SAM,'),
        ('textureLod(sunShadowMap,', 'SUN_SHADOW.SampleLevel(SAM,'),
        ('textureLod(noiseTexture,', 'NOISE_TEX.SampleLevel(SAM,'),
        ('textureLod(vsmPhysicalAtlas,', 'VSM_ATLAS.SampleLevel(SAM,'),
        ('textureLod(volumetricFogTex,', 'VOL_FOG.SampleLevel(SAM,'),
        ('textureLod(diffuseGITex,', 'GI_DIFF.SampleLevel(SAM,'),
        ('textureLod(specularGITex,', 'GI_SPEC.SampleLevel(SAM,'),
        ('textureLod(gNormalRoughness,', 'G_NORMAL.SampleLevel(SAM,'),
        ('textureLod(gDepth,', 'G_DEPTH.SampleLevel(SAM,'),
        ('textureLod(gEmissive,', 'G_EMISSIVE.SampleLevel(SAM,'),
        ('textureLod(bindlessTextures[nonuniformEXT(', 'heapBindlessTex2D('),
        ('textureSize(sunShadowMap, 0)', 'uint2(0,0) /* sun shadow size */'),
        ('textureSize(diffuseGITex, 0)', 'GI_DIFF.getDimensions()'),
        ('textureSize(volumetricFogTex, 0)', 'VOL_FOG.getDimensions()'),
        ('texelFetch(gPortalID,', 'G_PORTAL.Load(int3('),
        ('texelFetch(gNormalRoughness,', 'G_NORMAL.Load(int3('),
        ('texelFetch(gAlbedoMetallic,', 'G_ALBEDO.Load(int3('),
        ('texelFetch(gHeightAO,', 'G_HEIGHT.Load(int3('),
        ('texelFetch(gDepth,', 'G_DEPTH.Load(int3('),
        ('imageStore(outColor,', 'HDR_OUT['),
        ('imageLoad(gtaoMap,', 'GTAO_IMG['),
        ('imageLoad(rtReflectionsMap,', 'RT_REFL['),
        ('imageSize(gtaoMap)', 'GTAO_IMG.getDimensions()'),
        ('layout(std140, set = 3, binding = 0) uniform LightBlock', '// LightBlock'),
        ('lightBlock', '(*DescriptorHandle<ConstantBuffer<LightUBOBlock>>(pc.lightBuffer))'),
        ('faceMatricesBuffer', 'DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices)'),
        ('decalBuffer', '(*DescriptorHandle<StructuredBuffer<DecalBlock>>(pc.decalBuffer))'),
        ('portalUbos', '(*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))'),
        ('lightGrid', 'DescriptorHandle<StructuredBuffer<LightGrid>>(pc.lightGrid)'),
        ('globalIndexList', 'DescriptorHandle<StructuredBuffer<uint>>(pc.lightIndexList)'),
        ('virtualPages', 'DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable)'),
    ]
    for a, b in reps:
        body = body.replace(a, b)

    # Fix texelFetch closing - crude: pixelCoords, 0) -> pixelCoords))
    body = body.replace(', 0).r', ')).x')
    body = body.replace(', 0).rgb', ')).rgb')
    body = body.replace(', 0).r;', ')).x;')

    # imageStore: imageStore(outColor, pixelCoords, vec4(...) -> HDR_OUT[pixelCoords] = float4(
    import re
    body = re.sub(r'HDR_OUT\[\s*(\w+)\s*,\s*vec4\(', r'HDR_OUT[\1] = float4(', body)
    body = re.sub(r'GTAO_IMG\[\s*(\w+)\s*\]\.r', r'GTAO_IMG[\1]', body)

    # Remove SampleVirtualShadowMap duplicate - use import
    body = re.sub(
        r'float SampleVirtualShadowMap\(vec3 projCoords, uint lightIndex, uint face, int encodedSlot\) \{[\s\S]*?return shadow / 16\.0;\n\}',
        '',
        body,
        count=1,
    )

    # Wrap Spot/Point VSM to use SampleVirtualShadowMapPCF
    body = body.replace(
        'return SampleVirtualShadowMap(projCoords, uint(light.lightIndex), 0, light.shadowSlot);',
        'return SampleVirtualShadowMapPCF(projCoords, uint(light.lightIndex), 0, light.shadowSlot, id.xy, VSM_ATLAS, DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable), NOISE_TEX);',
    )
    body = body.replace(
        'return SampleVirtualShadowMap(projCoords, uint(light.lightIndex), uint(face), light.shadowSlot);',
        'return SampleVirtualShadowMapPCF(projCoords, uint(light.lightIndex), uint(face), light.shadowSlot, id.xy, VSM_ATLAS, DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable), NOISE_TEX);',
    )

    main_hdr = '''
[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    g_lightingThreadPx = id.xy;
    GlobalUbo currentUbo = UBO;
'''

    body = body.replace('void main() {', main_hdr, 1)
    body = body.replace(
        'GlobalUbo currentUbo = ubo;',
        'GlobalUbo currentUbo = UBO;',
    )
    body = body.replace(
        'if (portal_id > 0 && portal_id <= 10) {\n        currentUbo = portalUbos.data[portal_id - 1];',
        'if (portal_id > 0 && portal_id <= 10) {\n        currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];',
    )

    OUT.write_text(HEADER + body, encoding='utf-8')
    print(f'Wrote {OUT} ({len(OUT.read_text())} bytes)')


if __name__ == '__main__':
    main()
