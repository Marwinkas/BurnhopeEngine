#!/usr/bin/env python3
"""Convert restored GLSL shaders to Slang (strip layout lines, rename types, keep logic)."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"

SHADER_HEADERS: dict[str, str] = {
    "post_process.comp": '''import common.Bindless;
import common.GlobalUbo;

struct PostProcessHeapPC {
    uint globalUbo;
    uint hdrOutput;
    uint hdrInput;
    uint depthInput;
    uint historyInput;
    uint historyOutput;
    uint exposureBuffer;
    uint dirtInput;
    uint normalInput;
    uint halftoneInput;
    uint paletteInput;
    uint ditherInput;
    uint defaultSampler;
};

[[vk::push_constant]] PostProcessHeapPC pc;

''',
    "gtao.comp": '''import common.Bindless;
import common.GlobalUbo;

struct GtaoHeapPC {
    uint globalUbo;
    uint depthTex;
    uint normalTex;
    uint gtaoOutput;
    uint defaultSampler;
};

[[vk::push_constant]] GtaoHeapPC pc;

''',
    "ssgi.comp": '''import common.Bindless;
import common.GlobalUbo;

struct SsgiHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint hdrOutput;
    uint ssgiRaw;
    uint blueNoise;
    uint defaultSampler;
};

[[vk::push_constant]] SsgiHeapPC pc;

''',
    "ssgi_denoise.comp": '''import common.Bindless;
import common.GlobalUbo;

struct SsgiDenoiseHeapPC {
    uint globalUbo;
    uint ssgiRaw;
    uint giDiffuse;
};

[[vk::push_constant]] SsgiDenoiseHeapPC pc;

''',
    "light_culling.comp": '''import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;

struct LightCullHeapPC {
    uint globalUbo;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
};

[[vk::push_constant]] LightCullHeapPC pc;

''',
    "hiz_downsample.comp": '''import common.Bindless;

struct HiZHeapPC {
    uint depthIn;
    uint hiZOut;
    float2 invSize;
    uint defaultSampler;
};

[[vk::push_constant]] HiZHeapPC pc;

''',
    "volumetric.comp": '''import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;
import common.VsmSample;

struct VolumetricHeapPC {
    uint globalUbo;
    uint gbufferDepth;
    uint gbufferPortalId;
    uint shadowCsm;
    uint shadowAtlas;
    uint blueNoise;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint vsmAtlas;
    uint vsmPageTable;
    uint volumetricOut;
    uint portalUbos;
    uint defaultSampler;
};

[[vk::push_constant]] VolumetricHeapPC pc;

''',
    "vsm_mark_pages.comp": '''import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;

struct VsmMarkHeapPC {
    uint globalUbo;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint vsmAllocator;
};

[[vk::push_constant]] VsmMarkHeapPC pc;

''',
    "gi_sample.comp": '''import common.Bindless;
import common.GlobalUbo;

struct GiSampleHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint giCascade0;
};

[[vk::push_constant]] GiSampleHeapPC pc;

''',
    "cascade_merge.comp": '''import common.Bindless;

struct CascadeMergePC {
    uint currentCascade;
    uint nextCascade;
    float4 probeGridMin;
    float4 probeGridMax;
    int4 probeCount;
    float4 params;
};

[[vk::push_constant]] CascadeMergePC rc;

''',
    "rt_reflections.comp": '''import common.Bindless;
import common.GlobalUbo;

struct RtReflectionHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint rtTlas;
    uint objectStorage;
    uint materialStorage;
    uint giDiffuse;
    uint rtReflections;
};

[[vk::push_constant]] RtReflectionHeapPC pc;

''',
    "probe_render.comp": '''import common.Bindless;
import common.GlobalUbo;

struct ProbeRenderHeapPC {
    uint globalUbo;
    uint giCascade0;
};

[[vk::push_constant]] ProbeRenderHeapPC pc;

''',
    "probe_update.comp": '''import common.Bindless;
import common.GlobalUbo;

struct ProbeUpdateHeapPC {
    uint globalUbo;
    uint giCascade0;
};

[[vk::push_constant]] ProbeUpdateHeapPC pc;

''',
    "mat_preview.comp": '''import common.Bindless;

struct MatPreviewPC {
    float4 albedoColor;
    float4 emissiveColor;
    float4 matParams;
    float4 uvScale_triSc;
    float4 camPos_time;
    int4 flags;
    uint outImage;
    uint texAlbedo;
    uint texORM;
    uint texNormal;
    uint texHDR;
    uint defaultSampler;
};

[[vk::push_constant]] MatPreviewPC pc;

''',
    "gbuffer.frag": '''import common.Bindless;
import common.GlobalUbo;

struct GfxHeapPC {
    uint globalUbo;
    uint materialStorage;
    uint defaultSampler;
};

[[vk::push_constant]] GfxHeapPC pc;

struct MaterialData {
    int albedoAlphaIdx;
    int normalIdx;
    int ormxIdx;
    int emissiveIdx;
    int useTriplanar;
    int isTransparent;
    int repeatTexture;
    int pad1;
    float2 uvScale;
    float triplanarScale;
    float emissiveIntensity;
    float4 albedoColor;
    float4 emissiveColor;
    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;
    float aoStrength;
    float pad2, pad3, pad4;
};

struct VSOut {
    float3 inCrntPos : TEXCOORD0;
    float2 inTexCoord : TEXCOORD1;
    float3 inTBN0 : TEXCOORD2;
    float3 inTBN1 : TEXCOORD3;
    float3 inTBN2 : TEXCOORD4;
    nointerpolation uint inMatID : TEXCOORD5;
    float4 inColor : TEXCOORD6;
    float2 inUV2 : TEXCOORD7;
    float inThickness : TEXCOORD8;
};

struct FragOut {
    float4 gNormalRoughness : SV_Target0;
    float4 gAlbedoMetallic : SV_Target1;
    float4 gHeightAO : SV_Target2;
    float4 gEmissive : SV_Target3;
    uint o_PortalID : SV_Target4;
};

''',
    "gbuffer.vert": '''import common.Bindless;
import common.GlobalUbo;

struct GfxHeapPC {
    uint globalUbo;
    uint objectStorage;
    uint boneStorage;
};

[[vk::push_constant]] GfxHeapPC pc;

struct ObjectData {
    float4x4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint64_t posBufferAddress;
    uint64_t attrBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    float4 aabbMin;
    float4 aabbMax;
};

struct PackedVertexAnim {
    uint16_t pivotX, pivotY, pivotZ, cloth_ao;
    uint boneIndices;
    uint boneWeights;
};

struct VSOut {
    float3 outCrntPos : TEXCOORD0;
    float2 outTexCoord : TEXCOORD1;
    float3 outTBN0 : TEXCOORD2;
    float3 outTBN1 : TEXCOORD3;
    float3 outTBN2 : TEXCOORD4;
    nointerpolation uint outMatID : TEXCOORD5;
    float4 outColor : TEXCOORD6;
    float2 outUV2 : TEXCOORD7;
    float outThickness : TEXCOORD8;
    float4 pos : SV_Position;
};

''',
    "shadow.vert": '''import common.Bindless;

struct ShadowVertPC {
    float4x4 lightSpaceMatrix;
    uint objectStorage;
    uint boneStorage;
};

[[vk::push_constant]] ShadowVertPC pc;

struct ObjectData {
    float4x4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint64_t posBufferAddress;
    uint64_t attrBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    float4 aabbMin;
    float4 aabbMax;
};

struct PackedVertexAnim {
    uint16_t pivotX, pivotY, pivotZ, cloth_ao;
    uint boneIndices;
    uint boneWeights;
};

''',
    "shadow.frag": '''[shader("fragment")]
void main() {}
''',
    "depth_reset.frag": '''[shader("fragment")]
void main() { SV_Depth = 1.0; }
''',
    "portal.vert": '''import common.GlobalUbo;

struct PortalVertPC {
    float4x4 modelMatrix;
    uint portalID;
    uint globalUbo;
};

[[vk::push_constant]] PortalVertPC pc;

''',
    "portal.frag": '''struct PortalFragPC { float4x4 modelMatrix; uint portalID; };
[[vk::push_constant]] PortalFragPC pc;
struct FragOut {
    float4 outNormalRoughness : SV_Target0;
    float4 outAlbedoMetallic : SV_Target1;
    float4 outExtra : SV_Target2;
    float4 outEmissive : SV_Target3;
    uint o_PortalID : SV_Target4;
};
''',
}

SKIP_BODY = {"shadow.frag", "depth_reset.frag", "lighting.comp"}
STAGE = {
    ".comp": ("compute", "[numthreads(16, 16, 1)]", "uint3 id : SV_DispatchThreadID"),
    ".vert": ("vertex", "", ""),
    ".frag": ("fragment", "", ""),
    ".task": ("amplification", "[numthreads(32, 1, 1)]", "uint3 gtid : SV_GroupThreadID, uint gid : SV_GroupID"),
    ".mesh": ("mesh", "[numthreads(128, 1, 1)]", "uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID"),
}

SKIP_LINE = re.compile(
    r"^\s*(#version|#extension|layout\s*\(|taskPayloadSharedEXT)",
    re.MULTILINE,
)


def strip_layout_blocks(src: str) -> str:
    """Remove layout(...) declarations including braced uniform blocks."""
    out: list[str] = []
    i = 0
    lines = src.splitlines(keepends=True)
    while i < len(lines):
        line = lines[i]
        if re.match(r"^\s*layout\s*\(", line):
            if "{" in line and "}" not in line:
                i += 1
                while i < len(lines) and "}" not in lines[i]:
                    i += 1
                i += 1
                continue
            i += 1
            continue
        if re.match(r"^\s*struct\s+GlobalUboStruct\b", line):
            i += 1
            while i < len(lines) and "};" not in lines[i]:
                i += 1
            i += 1
            continue
        out.append(line)
        i += 1
    return "".join(out)


def convert_types(body: str) -> str:
    reps = [
        (r"\bvec2\b", "float2"), (r"\bvec3\b", "float3"), (r"\bvec4\b", "float4"),
        (r"\bivec2\b", "int2"), (r"\bivec3\b", "int3"), (r"\bivec4\b", "int4"),
        (r"\buvec2\b", "uint2"), (r"\buvec3\b", "uint3"), (r"\buvec4\b", "uint4"),
        (r"\bmat2\b", "float2x2"), (r"\bmat3\b", "float3x3"), (r"\bmat4\b", "float4x4"),
        (r"\bf16vec3\b", "half3"), (r"\bf16vec2\b", "half2"), (r"\bfloat16_t\b", "half"),
        (r"\bmix\s*\(", "lerp("), (r"\bGlobalUboStruct\b", "GlobalUbo"), (r"\bfract\s*\(", "frac("),
        (r"\bgl_GlobalInvocationID\.xy\b", "id.xy"),
        (r"\bgl_GlobalInvocationID\b", "id"),
        (r"\bgl_LocalInvocationID\b", "gtid"),
        (r"\bgl_WorkGroupID\.x\b", "gid.x"),
        (r"\bgl_WorkGroupID\b", "gid"),
        (r"\bgl_InstanceIndex\b", "SV_InstanceID"),
        (r"\bgl_VertexIndex\b", "SV_VertexID"),
        (r"\bgl_FragCoord\.xy\b", "pixelPos.xy"),
        (r"\bgl_FragDepth\b", "SV_Depth"),
        (r"\bdiscard\b", "[discard]"),
    ]
    for a, b in reps:
        body = re.sub(a, b, body)
    return body


def port_file(path: Path) -> Path:
    name = path.name
    if name in SKIP_BODY:
        out = SHADERS / (name + ".slang")
        out.write_text(SHADER_HEADERS.get(name, "") + "\n", encoding="utf-8")
        return out

    raw = path.read_text(encoding="utf-8")
    body = strip_layout_blocks(raw)
    body = convert_types(body)

    ext = path.suffix
    stage, threads, sig = STAGE.get(ext, STAGE[".comp"])
    header = SHADER_HEADERS.get(name, "import common.Bindless;\nimport common.GlobalUbo;\n\n")

    if ext == ".comp":
        body = re.sub(
            r"void\s+main\s*\(\s*\)",
            f'[shader("compute")]\n{threads}\nvoid main({sig})',
            body,
            count=1,
        )
    elif ext == ".frag":
        body = re.sub(r"void\s+main\s*\(\s*\)", '[shader("fragment")]\nvoid main()', body, count=1)
    elif ext == ".vert":
        body = re.sub(r"void\s+main\s*\(\s*\)", '[shader("vertex")]\nvoid main()', body, count=1)

    out = path.with_suffix(path.suffix + ".slang")
    out.write_text(header + body, encoding="utf-8")
    return out


def main() -> None:
    for glsl in sorted(SHADERS.iterdir()):
        if glsl.suffix in STAGE and not glsl.name.endswith(".spv"):
            p = port_file(glsl)
            print(f"{p.name}: {sum(1 for _ in open(p))} lines")


if __name__ == "__main__":
    main()
