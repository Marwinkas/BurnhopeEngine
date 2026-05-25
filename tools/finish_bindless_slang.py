#!/usr/bin/env python3
"""Apply bindless heap bindings to ported GLSL->Slang shaders."""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"


def strip_preamble(text: str) -> str:
    text = re.sub(r"^#version\s+\d+.*\n", "", text, flags=re.M)
    text = re.sub(r"^#extension\s+.*\n", "", text, flags=re.M)
    return text


def fix_mat_mul(text: str) -> str:
    reps = [
        (r"(\b\w+\.(?:projection|view|invViewProj|prevViewProj|sunLightSpaceMatrices\[\w+\]))\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(currentUbo\.view)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(light\.lightSpaceMatrix)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(faceMat)\s*\*\s*(float4\([^)]+\))", r"mul(\1, \2)"),
        (r"(currentUbo\.sunLightSpaceMatrices\[\w+\])\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(currentUbo\.invViewProj)\s*\*\s*(\w+)", r"mul(\1, \2)"),
        (r"(g_ppUbo\.invViewProj)\s*\*\s*", r"mul(g_ppUbo.invViewProj, "),
        (r"(g_ppUbo\.prevViewProj)\s*\*\s*", r"mul(g_ppUbo.prevViewProj, "),
        (r"float2x2\(([^)]+)\)\s*\*\s*(\w+)", r"mul(float2x2(\1), \2)"),
    ]
    for a, b in reps:
        text = re.sub(a, b, text)
    return text


def finish_post_process(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = t.replace("half ", "float ").replace("half3", "float3").replace("half2", "float2")

    sam = "heapSampler(pc.defaultSampler)"
    hdr = f"heapTex2D(pc.hdrInput).SampleLevel({sam}"
    dep = f"heapTex2D_r(pc.depthInput).SampleLevel({sam}"
    hist = f"heapTex2D(pc.historyInput).SampleLevel({sam}"
    dirt = f"heapTex2D(pc.dirtInput).SampleLevel({sam}"
    norm = f"heapTex2D(pc.normalInput).SampleLevel({sam}"
    half = f"heapTex2D(pc.halftoneInput).SampleLevel({sam}"
    pal = f"heapTex2D(pc.paletteInput).SampleLevel({sam}"
    dith = f"heapTex2D(pc.ditherInput).SampleLevel({sam}"

    for old, new in [
        ("textureLod(hdrInput,", f"{hdr},"),
        ("textureLod(depthInput,", f"{dep},"),
        ("textureLod(historyInput,", f"{hist},"),
        ("textureLod(dirtInput,", f"{dirt},"),
        ("textureLod(normalInput,", f"{norm},"),
        ("textureLod(halftoneInput,", f"{half},"),
        ("textureLod(paletteInput,", f"{pal},"),
        ("textureLod(ditherInput,", f"{dith},"),
    ]:
        t = t.replace(old, new)

    t = re.sub(
        r"textureLod\(bindlessTextures\[nonuniformEXT\((\w+)\)\],\s*",
        rf"heapBindlessTex2D(\1).SampleLevel({sam}, ",
        t,
    )

    t = re.sub(
        r"imageStore\(outImage,\s*(\w+),\s*float4\(",
        r"heapRWTex2D(pc.hdrOutput)[\1] = float4(",
        t,
    )
    t = re.sub(
        r"imageStore\(outHistory,\s*(\w+),\s*float4\(",
        r"heapRWTex2D(pc.historyOutput)[\1] = float4(",
        t,
    )
    t = t.replace("adaptedLuminance", "(*DescriptorHandle<RWStructuredBuffer<float>>(pc.exposureBuffer))[0]")
    t = t.replace("subgroupAdd(", "WaveActiveSum(")
    t = t.replace("subgroupElect()", "WaveGetLaneIndex() == 0")
    t = re.sub(r"\bmod\(", "fmod(", t)

    t = t.replace("ubo.", "loadGlobalUbo(pc.globalUbo).")

    main_inj = """void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
"""
    t = re.sub(
        r"void main\(uint3 id : SV_DispatchThreadID\) \{\s*\n\s*int2 pixel",
        main_inj + "    int2 pixel",
        t,
        count=1,
    )
    # main body already uses loadGlobalUbo via ubo local — revert double load in main only
    t = t.replace(
        "    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);\n    int2 pixel",
        "    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);\n    int2 pixel",
    )
    t = fix_mat_mul(t)
    path.write_text(t, encoding="utf-8")
    print("post_process")


def finish_volumetric(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    t = re.sub(
        r"struct PointLightData \{[^}]+\};\s*struct LightGrid \{[^}]+\};\s*struct PointFaceMatrices \{[^}]+\};\s*",
        "",
        t,
    )
    t = re.sub(r"struct PortalUBO \{ GlobalUbo data\[10\]; \};\s*", "", t)

    sam = "heapSampler(pc.defaultSampler)"
    t = t.replace("textureLod(sunShadowMap,", f"heapTex2DArray(pc.shadowCsm).SampleLevel({sam},")
    t = t.replace("textureLod(vsmPhysicalAtlas,", f"heapTex2D_r(pc.vsmAtlas).SampleLevel({sam},")
    t = t.replace("textureLod(noiseTexture,", f"heapTex2D_r(pc.blueNoise).SampleLevel({sam},")
    t = t.replace("virtualPages[", "(*DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable))[")
    t = t.replace("texelFetch(gDepth,", "heapTex2D_r(pc.gbufferDepth).Load(int3(")
    t = t.replace("texelFetch(gPortalID,", "heapTex2D_u(pc.gbufferPortalId).Load(int3(")
    t = re.sub(r"Load\(int3\(\s*([^,)]+)\s*,\s*0\)\.r", r"Load(int3(\1, 0)).x", t)
    t = t.replace("imageSize(outVolumetric)", "loadGlobalUbo(pc.globalUbo).screenSize.xy * 0.5")
    t = re.sub(
        r"imageStore\(outVolumetric,\s*(\w+),\s*float4\(",
        r"heapRWTex2D(pc.volumetricOut)[\1] = float4(",
        t,
    )
    t = re.sub(
        r"(heapRWTex2D\(pc\.volumetricOut\)\[[^\]]+\] = float4\([^\)]+\))\);",
        r"\1);",
        t,
    )
    t = t.replace("GlobalUbo currentUbo = ubo;", "GlobalUbo currentUbo = loadGlobalUbo(pc.globalUbo);")
    t = t.replace(
        "if (portal_id > 0 && portal_id <= 10) currentUbo = portalUbos.data[portal_id - 1];",
        "if (portal_id > 0 && portal_id <= 10) currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];",
    )
    t = t.replace("ubo.gridDimX", "currentUbo.gridDimX").replace("ubo.gridDimY", "currentUbo.gridDimY")
    t = t.replace("lightGrid[", "(*DescriptorHandle<StructuredBuffer<LightGrid>>(pc.lightGrid))[")
    t = t.replace(
        "lightBlock.lights[",
        "(*DescriptorHandle<ConstantBuffer<LightUBOBlock>>(pc.lightBuffer)).lights[",
    )
    t = t.replace(
        "globalIndexList[",
        "(*DescriptorHandle<StructuredBuffer<uint>>(pc.lightIndexList))[",
    )
    t = t.replace(
        "faceMatricesBuffer.faceMatrices[",
        "(*DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices))[",
    )
    t = t.replace("textureSize(noiseTexture, 0)", "uint2(256, 256)")
    t = fix_mat_mul(t)
    path.write_text(t, encoding="utf-8")
    print("volumetric")


def finish_gbuffer(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    # remove duplicate MaterialData (keep first block only)
    first_end = t.find("struct FragOut")
    second_mat = t.find("struct MaterialData {", first_end)
    if second_mat > 0:
        third = t.find("float4 sampleMatTex", second_mat)
        t = t[:second_mat] + t[third:]

    sample = """
float4 sampleMatTex(int texIdx, float2 uv, float2 dx, float2 dy, int useTriplanar, float3 pos, float3 posDdx, float3 posDdy, float3 blend, float scale, int repeat) {
    if (texIdx < 0) return float4(0.0);
    Texture2D<float4> tex = heapBindlessTex2D(texIdx);
    SamplerState s = heapSampler(pc.defaultSampler);
    if (useTriplanar == 1) {
        float2 uvX = pos.zy * scale; float2 uvY = pos.xz * scale; float2 uvZ = pos.xy * scale;
        float2 dxX = posDdx.zy * scale; float2 dyX = posDdy.zy * scale;
        float2 dxY = posDdx.xz * scale; float2 dyY = posDdy.xz * scale;
        float2 dxZ = posDdx.xy * scale; float2 dyZ = posDdy.xy * scale;
        if (repeat == 0) { uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0); }
        float4 tx = tex.SampleGrad(s, uvX, dxX, dyX);
        float4 ty = tex.SampleGrad(s, uvY, dxY, dyY);
        float4 tz = tex.SampleGrad(s, uvZ, dxZ, dyZ);
        return tx * blend.x + ty * blend.y + tz * blend.z;
    }
    float2 finalUV = repeat == 1 ? uv : clamp(uv, 0.0, 1.0);
    return tex.SampleGrad(s, finalUV, dx, dy);
}
"""
    t = re.sub(r"float4 sampleMatTex\([\s\S]*?\n\}\n", sample + "\n", t, count=1)

    t = t.replace(
        "textureGrad(allTextures[nonuniformEXT(mat.ormxIdx)],",
        "heapBindlessTex2D(mat.ormxIdx).SampleGrad(heapSampler(pc.defaultSampler),",
    )
    t = t.replace("[discard]", "discard;")

    mat_block = """
struct MaterialBlock {
    MaterialData materials[];
};
"""
    if "struct MaterialBlock" not in t:
        t = t.replace("struct FragOut {", mat_block + "\nstruct FragOut {")

    main = """[shader("fragment")]
FragOut main(VSOut v) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    MaterialBlock matBuf = (*DescriptorHandle<StructuredBuffer<MaterialBlock>>(pc.materialStorage))[0];
    MaterialData mat = matBuf.materials[v.inMatID];
    float2 pixelPos = float2(0, 0);
"""
    t = re.sub(
        r'\[shader\("fragment"\)\]\s*void main\(\) \{\s*MaterialData mat = matBuffer\.materials\[inMatID\];',
        main,
        t,
        count=1,
    )
    main_idx = t.find("FragOut main(VSOut v)")
    if main_idx < 0:
        main_idx = t.find("void main()")
    head, body = t[:main_idx], t[main_idx:]
    for n in [
        "inTexCoord", "inCrntPos", "inTBN0", "inTBN1", "inTBN2", "inColor", "inUV2", "inThickness",
    ]:
        body = re.sub(rf"(?<!\.){n}\b", f"v.{n}", body)
    body = body.replace("inTBN[2]", "normalize(float3(v.inTBN0, v.inTBN1, v.inTBN2))")
    body = body.replace("inTBN[0]", "v.inTBN0")
    body = body.replace("inTBN[1]", "v.inTBN1")
    t = head + body

    if "FragOut o;" not in t:
        t = t.replace(
            "    gNormalRoughness = ",
            "    FragOut o;\n    o.gNormalRoughness = ",
            1,
        )
        t = t.replace("    gAlbedoMetallic  = ", "    o.gAlbedoMetallic  = ")
        t = t.replace("    gHeightAO        = ", "    o.gHeightAO        = ")
        t = t.replace("    gEmissive        = ", "    o.gEmissive        = ")
        t = t.replace("    o_PortalID = ubo.portalID;", "    o.o_PortalID = ubo.portalID;\n    return o;")

    path.write_text(t, encoding="utf-8")
    print("gbuffer")


def finish_lighting() -> None:
    subprocess.run(["python3", str(ROOT / "tools" / "gen_lighting_slang.py")], check=True)
    path = SHADERS / "lighting.comp.slang"
    t = path.read_text(encoding="utf-8")
    t = t.replace("ifloat2", "int2")
    t = t.replace("id.xy", "g_lightingThreadPx")
    t = t.replace("uv] = float", "uv, float")
    t = t.replace("tapPos * 2] = int2", "tapPos * 2, int2")
    t = re.sub(r"HDR_OUT\[\s*([^,\]]+)\s*,\s*float4\(", r"HDR_OUT[\1] = float4(", t)
    t = re.sub(r"(HDR_OUT\[[^\]]+\] = float4\([^\)]+\))\);", r"\1);", t)
    t = re.sub(
        r"(\n    uint portal_id[^\n]+\n)    GlobalUbo currentUbo = UBO;\n",
        r"\1",
        t,
        count=1,
    )
    t = t.replace(
        "(*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos)).data[portal_id - 1]",
        "(*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1]",
    )
    t = t.replace(
        "DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices).faceMatrices[",
        "(*DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices))[",
    )
    t = re.sub(r"GTAO_IMG\[ ifloat2\([^\)]+\)\)\.r", "GTAO_IMG[int2(uv * currentUbo.screenSize.xy * 0.5)].r", t)
    t = re.sub(r"GTAO_IMG\[ int2\(uv \* float2\(texSize\)\)\)\.r", "GTAO_IMG[int2(uv * currentUbo.screenSize.xy * 0.5)].r", t)
    t = t.replace("int2 texSize = GTAO_IMG.getDimensions();", "float2 texSize = currentUbo.screenSize.xy * 0.5;")
    t = re.sub(
        r"heapBindlessTex2D\((\w+)\)\],\s*",
        r"heapBindlessTex2D(\1).SampleLevel(SAM, ",
        t,
    )
    t = re.sub(
        r"return GTAO_IMG\[int2\(uv \* float2\(texSize\)\)\]\.r",
        "return GTAO_IMG[int2(uv * currentUbo.screenSize.xy * 0.5)].r",
        t,
    )
    t = t.replace(
        "currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[portal_id - 1].data[portal_id - 1];",
        "currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];",
    )
    t = fix_mat_mul(t)
    t = t.replace(
        "mul(rot, lightingPoissonDisk[i] * searchRadius * texelSize;",
        "mul(rot, lightingPoissonDisk[i]) * searchRadius * texelSize;",
    )
    t = t.replace(
        "mul(rot, lightingPoissonDisk[i] * filterRadius * texelSize;",
        "mul(rot, lightingPoissonDisk[i]) * filterRadius * texelSize;",
    )
    t = t.replace(
        "currentUbo.projection * mul(currentUbo.view,",
        "mul(currentUbo.projection, mul(currentUbo.view,",
    )
    t = re.sub(r"GTAO_IMG\[\s*tapPos\)", "GTAO_IMG[tapPos]", t)
    t = re.sub(r"GTAO_IMG\[\s*int2\([^\)]+\)\)\.r", "GTAO_IMG[int2(uv * currentUbo.screenSize.xy * 0.5)].r", t)
    path.write_text(t, encoding="utf-8")
    print("lighting")


def main() -> None:
    finish_post_process(SHADERS / "post_process.comp.slang")
    finish_volumetric(SHADERS / "volumetric.comp.slang")
    finish_gbuffer(SHADERS / "gbuffer.frag.slang")
    finish_lighting()


if __name__ == "__main__":
    main()
