#!/usr/bin/env python3
"""Port GLSL shaders to bindless Slang and fix compile errors."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"


def strip_preamble(text: str) -> str:
    text = re.sub(r"^#version\s+\d+.*\n", "", text, flags=re.M)
    text = re.sub(r"^#extension\s+.*\n", "", text, flags=re.M)
    text = re.sub(r"^taskPayloadSharedEXT\s+", "groupshared ", text, flags=re.M)
    return text


def dedupe_struct(text: str, name: str) -> str:
    parts = text.split(f"struct {name} {{")
    if len(parts) <= 2:
        return text
    first = parts[0]
    body_end = parts[1].split("};", 1)[0]
    rest = parts[1].split("};", 1)[1]
    for extra in parts[2:]:
        rest = extra.split("};", 1)[1]
    return first + f"struct {name} {{" + body_end + "};" + rest


def fix_mat_mul(text: str) -> str:
    reps = [
        (
            r"(\b\w+\.(?:projection|view|invViewProj|prevViewProj|sunLightSpaceMatrices\[\w+\]))\s*\*\s*(float4\([^)]+\))",
            r"mul(\1, \2)",
        ),
        (r"(currentUbo\.view)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(light\.lightSpaceMatrix)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(faceMat)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(decal\.invModelMatrix)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(obj\.modelMatrix)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(modelMatrix)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(skinMat)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(ubo\.projection)\s*\*\s*(ubo\.view)", r"mul(\1, \2)"),
        (r"(push\.viewProj)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(pc\.viewProj)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(inverse\([^)]+\))\s*\*\s*", r"mul(\1, "),
    ]
    for a, b in reps:
        text = re.sub(a, b, text)
    # broken mul from bad port: mul(A,  B; rest
    text = re.sub(
        r"mul\(([^,]+),\s*([^;)]+);\s*(\w+)\s*/=",
        r"float4 \3 = mul(\1, \2);\n    \3 /=",
        text,
    )
    text = re.sub(
        r"mul\(([^,]+),\s*([^;)]+);\s*(\w+)\s*/=",
        r"float4 \3 = mul(\1, \2);\n    \3 /=",
        text,
    )
    return text


def inject_main_ubo(text: str, body: str) -> str:
    if "loadGlobalUbo(pc.globalUbo)" in text or "GlobalUbo ubo = loadGlobalUbo" in text:
        return text
    return re.sub(
        r"(void main\([^{]+\)\s*\{)",
        r"\1\n" + body,
        text,
        count=1,
    )


def write_culling() -> None:
    path = SHADERS / "culling.comp.slang"
    path.write_text(
        """import common.Bindless;

struct CullPushConstants {
    uint objectHeap;
    uint subMeshHeap;
    uint drawCmdHeap;
    uint hiZHeap;
    float4x4 viewProj;
    float4 frustumPlanes[6];
    float3 camPos;
    uint objectCount;
    float zNear;
    float pad0;
    float pad1;
};

[[vk::push_constant]] CullPushConstants pc;

import common.SceneGpu;

bool isVisible(float3 worldMin, float3 worldMax) {
    for (int i = 0; i < 6; i++) {
        float3 n = pc.frustumPlanes[i].xyz;
        float d = pc.frustumPlanes[i].w;
        float3 positiveVertex = float3(
            n.x >= 0.0 ? worldMax.x : worldMin.x,
            n.y >= 0.0 ? worldMax.y : worldMin.y,
            n.z >= 0.0 ? worldMax.z : worldMin.z
        );
        if (dot(n, positiveVertex) + d < 0.0) return false;
    }
    return true;
}

bool isOccluded(float3 corners[8], Texture2D<float> hiZ, SamplerState sam) {
    float2 minXY = float2(1.0);
    float2 maxXY = float2(0.0);
    float minZ = 1.0;
    for (int i = 0; i < 8; i++) {
        float4 clip = mul(pc.viewProj, float4(corners[i], 1.0));
        if (clip.w <= 0.0) return false;
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        minXY = min(minXY, uv);
        maxXY = max(maxXY, uv);
        minZ = min(minZ, ndc.z);
    }
    if (minXY.x > 1.0 || maxXY.x < 0.0 || minXY.y > 1.0 || maxXY.y < 0.0) return true;
    minXY = clamp(minXY, 0.0, 1.0);
    maxXY = clamp(maxXY, 0.0, 1.0);
    uint2 hiZSize;
    hiZ.GetDimensions(hiZSize.x, hiZSize.y);
    float2 size = (maxXY - minXY) * float2(hiZSize);
    float lod = max(0.0, ceil(log2(max(size.x, size.y))) + 1.0);
    float d1 = hiZ.SampleLevel(sam, float2(minXY.x, minXY.y), lod).x;
    float d2 = hiZ.SampleLevel(sam, float2(maxXY.x, minXY.y), lod).x;
    float d3 = hiZ.SampleLevel(sam, float2(minXY.x, maxXY.y), lod).x;
    float d4 = hiZ.SampleLevel(sam, float2(maxXY.x, maxXY.y), lod).x;
    return minZ > max(max(d1, d2), max(d3, d4));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= pc.objectCount) return;

    SubMeshInfo sub = (*DescriptorHandle<StructuredBuffer<SubMeshInfo>>(pc.subMeshHeap))[idx];
    ObjectData obj = (*DescriptorHandle<StructuredBuffer<ObjectData>>(pc.objectHeap))[idx];
    RWStructuredBuffer<DrawCommand> drawCommands =
        *DescriptorHandle<RWStructuredBuffer<DrawCommand>>(pc.drawCmdHeap);

    float3 corners[8];
    corners[0] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMin.y, sub.aabbMin.z, 1.0)).xyz;
    corners[1] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMin.y, sub.aabbMin.z, 1.0)).xyz;
    corners[2] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMax.y, sub.aabbMin.z, 1.0)).xyz;
    corners[3] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMax.y, sub.aabbMin.z, 1.0)).xyz;
    corners[4] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMin.y, sub.aabbMax.z, 1.0)).xyz;
    corners[5] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMin.y, sub.aabbMax.z, 1.0)).xyz;
    corners[6] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMax.y, sub.aabbMax.z, 1.0)).xyz;
    corners[7] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMax.y, sub.aabbMax.z, 1.0)).xyz;

    float3 worldMin = corners[0];
    float3 worldMax = corners[0];
    for (int i = 1; i < 8; i++) {
        worldMin = min(worldMin, corners[i]);
        worldMax = max(worldMax, corners[i]);
    }

    Texture2D<float> hiZ = heapTex2D_r(pc.hiZHeap);
    SamplerState sam = heapSampler(0); // clamp sampler at heap slot 0 if bound globally
    bool visible = isVisible(worldMin, worldMax);
    if (visible) visible = !isOccluded(corners, hiZ, sam);

    uint selectedLod = 0;
    if (visible) {
        float3 center = (worldMin + worldMax) * 0.5;
        float dist = distance(pc.camPos, center);
        if (dist > 20.0) selectedLod = 1;
        if (dist > 50.0) selectedLod = 2;
        if (dist > 100.0) selectedLod = 3;
        selectedLod = min(selectedLod, sub.lodCount - 1);
    }

    drawCommands[idx].indexCount = visible ? sub.indexCounts[selectedLod] : 0;
    drawCommands[idx].instanceCount = visible ? 1 : 0;
    drawCommands[idx].firstIndex = sub.firstIndices[selectedLod];
    drawCommands[idx].vertexOffset = 0;
    drawCommands[idx].firstInstance = idx;
}
""",
        encoding="utf-8",
    )
    print("wrote culling.comp.slang")


def write_depth_reset() -> None:
    (SHADERS / "depth_reset.frag.slang").write_text(
        """struct DepthOut {
    float depth : SV_Depth;
};

[shader("fragment")]
DepthOut main() {
    DepthOut o;
    o.depth = 1.0;
    return o;
}
""",
        encoding="utf-8",
    )
    print("wrote depth_reset.frag.slang")


def fix_gtao(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = dedupe_struct(t, "GtaoHeapPC") if "struct GtaoHeapPC" in t else t
    if "struct GtaoHeapPC" not in t:
        t = """import common.Bindless;
import common.GlobalUbo;

struct GtaoHeapPC {
    uint globalUbo;
    uint depthTex;
    uint normalTex;
    uint gtaoOutput;
    uint defaultSampler;
};

[[vk::push_constant]] GtaoHeapPC pc;

""" + t
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> depthMap = heapTex2D_r(pc.depthTex);
    Texture2D<float4> normalRoughnessMap = heapTex2D(pc.normalTex);
    RWTexture2D<float4> outGTAO = heapRWTex2D(pc.gtaoOutput);
""",
    )
    t = t.replace("ubo.", "ubo.").replace("textureLod(depthMap,", "depthMap.SampleLevel(sam,")
    t = t.replace("textureLod(normalRoughnessMap,", "normalRoughnessMap.SampleLevel(sam,")
    t = re.sub(r"imageSize\(outGTAO\)", "int2(ubo.screenSize.xy * 0.5)", t)
    t = re.sub(r"imageStore\(outGTAO,\s*(\w+),\s*float4\(", r"outGTAO[\1] = float4(", t)
    t = re.sub(r"(ubo\.view)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)", t)
    t = re.sub(r"ubo\.invViewProj\s*\*\s*clipSpace", r"mul(ubo.invViewProj, clipSpace)", t)
    t = re.sub(r"float3x3\(ubo\.view\)", r"(float3x3)ubo.view", t)
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed gtao")


def fix_hiz(path: Path) -> None:
    path.write_text(
        """import common.Bindless;

struct HiZHeapPC {
    uint depthIn;
    uint hiZOut;
    float2 invSize;
    uint defaultSampler;
};

[[vk::push_constant]] HiZHeapPC pc;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 pos = int2(id.xy);
    Texture2D<float> inDepth = heapTex2D_r(pc.depthIn);
    RWTexture2D<float4> outDepth = heapRWTex2D(pc.hiZOut);
    SamplerState sam = heapSampler(pc.defaultSampler);
    uint2 outSize;
    outDepth.GetDimensions(outSize.x, outSize.y);
    if (pos.x >= int(outSize.x) || pos.y >= int(outSize.y)) return;
    float2 uv = (float2(pos) + 0.5) / float2(outSize);
    float d1 = inDepth.SampleLevel(sam, uv + float2(-0.25, -0.25) * pc.invSize, 0.0).x;
    float d2 = inDepth.SampleLevel(sam, uv + float2(0.25, -0.25) * pc.invSize, 0.0).x;
    float d3 = inDepth.SampleLevel(sam, uv + float2(-0.25, 0.25) * pc.invSize, 0.0).x;
    float d4 = inDepth.SampleLevel(sam, uv + float2(0.25, 0.25) * pc.invSize, 0.0).x;
    float maxD = max(max(d1, d2), max(d3, d4));
    outDepth[pos] = float4(maxD, 0, 0, 0);
}
""",
        encoding="utf-8",
    )
    print("fixed hiz")


def fix_cascade_merge(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = t.replace("imageSize(currentCascade)", "int2(rc.params.xy)")
    t = t.replace("imageLoad(currentCascade,", "heapRWTex2D(rc.currentCascade)[")
    t = t.replace("imageLoad(currentCascade, pixel)", "heapRWTex2D(rc.currentCascade)[pixel]")
    t = re.sub(
        r"float4 near = imageLoad\(currentCascade, pixel\);",
        "float4 near = heapRWTex2D(rc.currentCascade)[pixel];",
        t,
    )
    t = t.replace("texelFetch(nextCascade,", "heapTex2D(rc.nextCascade).Load(int3(")
    t = re.sub(r"Load\(int3\(([^,]+),\s*0\)\.rgb", r"Load(int3(\1, 0)).rgb", t)
    t = re.sub(
        r"texelFetch\(nextCascade, nextPixel, 0\)",
        "heapTex2D(rc.nextCascade).Load(int3(nextPixel, 0))",
        t,
    )
    t = re.sub(
        r"imageStore\(currentCascade,\s*pixel,\s*float4\(",
        r"heapRWTex2D(rc.currentCascade)[pixel] = float4(",
        t,
    )
    # params.xy holds cascade texture size when pushed from CPU
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed cascade_merge")


def fix_generic_compute(name: str, ubo_inject: str, replacements: list[tuple[str, str]]) -> None:
    path = SHADERS / f"{name}.comp.slang"
    if not path.exists():
        return
    t = strip_preamble(path.read_text(encoding="utf-8"))
    for a, b in replacements:
        t = t.replace(a, b)
    t = inject_main_ubo(t, ubo_inject)
    t = fix_mat_mul(t)
    path.write_text(t, encoding="utf-8")
    print(f"fixed {name}")


def fix_gi_sample(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    header = """import common.Bindless;
import common.GlobalUbo;

struct GiSampleHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint giCascade0;
    uint giDiffuseOut;
    uint giSpecularOut;
    float4 probeGridMin;
    float4 probeGridMax;
    int4 probeCount;
    float4 params;
};

[[vk::push_constant]] GiSampleHeapPC pc;

#define rc pc

"""
    if "struct GiSampleHeapPC" not in t:
        t = header + t
    else:
        t = re.sub(r"struct GiSampleHeapPC \{[^}]+\};", header.strip().split("[[vk::push_constant]]")[0].strip().split("struct GiSampleHeapPC")[1], t, count=0)
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    Texture2D<float4> cascade0 = heapTex2D(pc.giCascade0);
    RWTexture2D<float4> outDiffuseGI = heapRWTex2D(pc.giDiffuseOut);
    RWTexture2D<float4> outSpecularGI = heapRWTex2D(pc.giSpecularOut);
""",
    )
    t = t.replace("texelFetch(cascade0,", "cascade0.Load(int3(")
    t = re.sub(r"Load\(int3\(([^,]+),\s*0\)\.rgb", r"Load(int3(\1, 0)).rgb", t)
    t = re.sub(r"textureLod\(gNormalRoughness,", "heapTex2D(pc.gbufferNormal).SampleLevel(heapSampler(0),", t)
    t = re.sub(r"textureLod\(gDepth,", "heapTex2D_r(pc.gbufferDepth).SampleLevel(heapSampler(0),", t)
    t = re.sub(
        r"imageStore\(outDiffuseGI,\s*(\w+),\s*float4\(",
        r"outDiffuseGI[\1] = float4(",
        t,
    )
    t = re.sub(
        r"imageStore\(outSpecularGI,\s*(\w+),\s*float4\(",
        r"outSpecularGI[\1] = float4(",
        t,
    )
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed gi_sample")


def fix_light_culling(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = re.sub(
        r"struct PointLightData \{[^}]+\};\s*struct LightGrid \{[^}]+\};\s*",
        "",
        t,
    )
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    ConstantBuffer<LightUBOBlock> lightBlock = *DescriptorHandle<ConstantBuffer<LightUBOBlock>>(pc.lightBuffer);
    RWStructuredBuffer<LightGrid> lightGrid = *DescriptorHandle<RWStructuredBuffer<LightGrid>>(pc.lightGrid);
    RWStructuredBuffer<uint> globalIndexList = *DescriptorHandle<RWStructuredBuffer<uint>>(pc.lightIndexList);
""",
    )
    t = t.replace("lightBlock.", "lightBlock.")
    t = t.replace("lightGrid[", "lightGrid[")
    t = fix_mat_mul(t)
    path.write_text(t, encoding="utf-8")
    print("fixed light_culling")


def fix_mat_preview(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = inject_main_ubo(
        t,
        """    SamplerState sam = heapSampler(pc.defaultSampler);
    RWTexture2D<float4> outImage = heapRWTex2D(pc.outImage);
""",
    )
    t = t.replace("sampler2D", "Texture2D<float4>")
    t = re.sub(
        r"float4 sampleTex\(Texture2D<float4> tex,",
        "float4 sampleTex(Texture2D<float4> tex,",
        t,
    )
    t = t.replace("texture(tex,", "tex.Sample(sam,")
    t = t.replace("textureLod(texHDR,", "heapTex2D(pc.texHDR).SampleLevel(sam,")
    for name, heap in [
        ("texAlbedo", "pc.texAlbedo"),
        ("texORM", "pc.texORM"),
        ("texNormal", "pc.texNormal"),
    ]:
        t = t.replace(f"sampleTex({name},", f"sampleTex(heapTex2D({heap}),")
    t = re.sub(r"imageSize\(outImage\)", "int2(512, 512) /* preview size */", t)
    t = re.sub(r"imageStore\(outImage,\s*(\w+),\s*float4\(", r"outImage[\1] = float4(", t)
    path.write_text(t, encoding="utf-8")
    print("fixed mat_preview")


def fix_ssgi(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    Texture2D<float4> gNormal = heapTex2D(pc.gbufferNormal);
    Texture2D<float4> blueNoise = heapTex2D(pc.blueNoise);
    RWTexture2D<float4> ssgiRaw = heapRWTex2D(pc.ssgiRaw);
""",
    )
    for old, new in [
        ("textureLod(gDepth,", "gDepth.SampleLevel(sam,"),
        ("textureLod(gNormal,", "gNormal.SampleLevel(sam,"),
        ("textureLod(blueNoise,", "blueNoise.SampleLevel(sam,"),
        ("texelFetch(gDepth,", "gDepth.Load(int3("),
        ("texelFetch(gNormal,", "gNormal.Load(int3("),
    ]:
        t = t.replace(old, new)
    t = re.sub(r"Load\(int3\(\s*([^,)]+)\s*,\s*0\)", r"Load(int3(\1, 0))", t)
    t = re.sub(r"imageStore\(ssgiRaw,\s*(\w+),\s*float4\(", r"ssgiRaw[\1] = float4(", t)
    t = re.sub(r"ubo\.invViewProj\s*\*\s*", r"mul(ubo.invViewProj, ", t)
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed ssgi")


def fix_ssgi_denoise(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    Texture2D<float4> ssgiIn = heapTex2D(pc.ssgiRaw);
    RWTexture2D<float4> giOut = heapRWTex2D(pc.giDiffuse);
""",
    )
    t = re.sub(r"imageStore\(giDiffuse,\s*(\w+),\s*float4\(", r"giOut[\1] = float4(", t)
    t = re.sub(r"textureLod\(ssgiRaw,", "ssgiIn.SampleLevel(heapSampler(0),", t)
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed ssgi_denoise")


def fix_vsm_mark(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = inject_main_ubo(
        t,
        """    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
""",
    )
    t = t.replace("texelFetch(gDepth,", "gDepth.Load(int3(")
    t = re.sub(r"Load\(int3\(\s*([^,)]+)\s*,\s*0\)\.r", r"Load(int3(\1, 0)).x", t)
    t = re.sub(r"ubo\.invViewProj\s*\*\s*clip", r"mul(ubo.invViewProj, clip", t)
    t = re.sub(r"ubo\.view\s*\*\s*float4", r"mul(ubo.view, float4", t)
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print("fixed vsm_mark")


def fix_rt_probe_shaders(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = dedupe_struct(t, "ObjectData")
    t = inject_main_ubo(
        t,
        "    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);\n",
    )
    t = t.replace("ubo.", "ubo.")
    path.write_text(fix_mat_mul(t), encoding="utf-8")
    print(f"fixed {path.name}")


def fix_portal_shaders() -> None:
    pv = SHADERS / "portal.vert.slang"
    t = strip_preamble(pv.read_text(encoding="utf-8"))
    t = inject_main_ubo(t, "    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);\n")
    t = re.sub(r"gl_Position\s*=", "outPos =", t)
    if "struct VSOut" not in t:
        t = t.replace(
            "[shader(\"vertex\")]\nvoid main()",
            """struct VSOut { float4 pos : SV_Position; };
[shader("vertex")]
VSOut main() {
    VSOut o;
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
""",
        )
        t += "\n    return o;\n}"
    pv.write_text(fix_mat_mul(t), encoding="utf-8")

    pf = SHADERS / "portal.frag.slang"
    ft = strip_preamble(pf.read_text(encoding="utf-8"))
    if "FragOut main" not in ft:
        ft = ft.replace(
            '[shader("fragment")]\nvoid main()',
            '[shader("fragment")]\nFragOut main() {\n    FragOut o;\n',
        )
        if "return o" not in ft:
            ft += "\n    return o;\n}"
    pf.write_text(ft, encoding="utf-8")
    print("fixed portal")


def compile_all() -> tuple[list[str], list[tuple[str, str]]]:
    stages = {
        ".comp.slang": "compute",
        ".vert.slang": "vertex",
        ".frag.slang": "fragment",
        ".task.slang": "task",
        ".mesh.slang": "mesh",
    }
    files = sorted(SHADERS.glob("*.comp.slang"))
    files += sorted(SHADERS.glob("*.vert.slang"))
    files += sorted(SHADERS.glob("*.frag.slang"))
    files += sorted(SHADERS.glob("*.task.slang"))
    files += sorted(SHADERS.glob("*.mesh.slang"))
    ok, fail = [], []
    for f in files:
        stage = next(v for k, v in stages.items() if f.name.endswith(k))
        cmd = [
            "slangc",
            "-lang",
            "slang",
            "-target",
            "spirv",
            "-profile",
            "spirv_1_6",
            "-entry",
            "main",
            "-stage",
            stage,
            "-I",
            str(SHADERS),
            str(f),
            "-o",
            "/tmp/t.spv",
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            ok.append(str(f))
        else:
            fail.append((str(f), r.stderr[:1500]))
    return ok, fail


def main() -> None:
    sys.path.insert(0, str(ROOT / "tools"))
    import port_glsl_to_slang as port

    for glsl in sorted(SHADERS.iterdir()):
        if glsl.suffix in port.STAGE and not glsl.name.endswith(".spv"):
            port.port_file(glsl)

    write_culling()
    write_depth_reset()
    fix_hiz(SHADERS / "hiz_downsample.comp.slang")
    fix_gtao(SHADERS / "gtao.comp.slang")
    fix_cascade_merge(SHADERS / "cascade_merge.comp.slang")
    fix_gi_sample(SHADERS / "gi_sample.comp.slang")
    fix_light_culling(SHADERS / "light_culling.comp.slang")
    fix_mat_preview(SHADERS / "mat_preview.comp.slang")
    fix_ssgi(SHADERS / "ssgi.comp.slang")
    fix_ssgi_denoise(SHADERS / "ssgi_denoise.comp.slang")
    fix_vsm_mark(SHADERS / "vsm_mark_pages.comp.slang")
    for n in ["rt_reflections", "probe_render", "probe_update"]:
        fix_rt_probe_shaders(SHADERS / f"{n}.comp.slang")

    subprocess.run([sys.executable, str(ROOT / "tools" / "gen_lighting_slang.py")], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "finish_bindless_slang.py")], check=True)

    # lighting syntax fixes
    lp = SHADERS / "lighting.comp.slang"
    lt = lp.read_text(encoding="utf-8")
    lt = lt.replace(
        "mul(currentUbo.projection, mul(currentUbo.view, float4(rayPos, 1.0));",
        "mul(currentUbo.projection, mul(currentUbo.view, float4(rayPos, 1.0)));",
    )
    lp.write_text(fix_mat_mul(lt), encoding="utf-8")

    ok, fail = compile_all()
    print(f"OK={len(ok)} FAIL={len(fail)}")
    for f, err in fail:
        print(f"\n=== {f} ===\n{err[:800]}")


if __name__ == "__main__":
    main()
