#!/usr/bin/env python3
"""Fix common Slang port artifacts in lighting / post_process / volumetric / gbuffer."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"


def strip_glsl_preamble(text: str) -> str:
    text = re.sub(r"^#version\s+\d+.*\n", "", text, flags=re.M)
    text = re.sub(r"^#extension\s+.*\n", "", text, flags=re.M)
    return text


def fix_mat_mul(text: str) -> str:
    # matrix * vector -> mul(matrix, vector)
    patterns = [
        (r"(\b(?:currentUbo|ubo|UBO)\.(?:projection|view|invViewProj|prevViewProj))\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(currentUbo\.sunLightSpaceMatrices\[\w+\])\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(decal\.invModelMatrix)\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(light\.lightSpaceMatrix)\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(currentUbo\.view)\s*\*\s*(float4\([^)]+\))",
         r"mul(\1, \2)"),
        (r"(inverse\(decal\.invModelMatrix\))\s*\*\s*",
         r"mul(\1, "),
    ]
    for pat, repl in patterns:
        text = re.sub(pat, repl, text)
    text = re.sub(
        r"(\bcurrentUbo\.invViewProj)\s*\*\s*(\w+);",
        r"mul(\1, \2);",
        text,
    )
    return text


def fix_lighting(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = text.replace(
        "#define UBO ((*heapGlobalUbo(pc.globalUbo)))",
        "#define UBO loadGlobalUbo(pc.globalUbo)",
    )
    text = text.replace("ifloat2", "int2")
    text = text.replace("ifloat2", "int2")
    text = re.sub(r"GTAO_IMG\[\s*([^,\]]+)\s*,", r"GTAO_IMG[\1] =", text)
    text = re.sub(r"GTAO_IMG\[\s*([^,\]]+)\s*\)", r"GTAO_IMG[\1]", text)
    text = re.sub(r"HDR_OUT\[\s*([^,\]]+)\s*,\s*float4\(", r"HDR_OUT[\1] = float4(", text)
    text = re.sub(r"HDR_OUT\[\s*([^,\]]+)\s*,\s*float4\(", r"HDR_OUT[\1] = float4(", text)
    text = re.sub(r"Load\(int3\(\s*([^,)]+)\s*,\s*0\)", r"Load(int3(\1, 0))", text)
    text = text.replace(
        "currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos)).data[portal_id - 1];",
        "currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];",
    )
    text = re.sub(
        r"heapBindlessTex2D\((\w+)\)\],\s*",
        r"heapBindlessTex2D(\1).SampleLevel(SAM, ",
        text,
    )
    text = re.sub(
        r"GTAO_IMG\.getDimensions\(\)",
        "uint2(0,0) /* gtao size via screen */",
        text,
    )
    text = re.sub(
        r"float2 texSize = float2\(VOL_FOG\.getDimensions\(\)\);",
        "float2 texSize = currentUbo.screenSize.xy * 0.5;",
        text,
    )
    # Remove duplicate currentUbo = UBO in main
    text = re.sub(
        r"(GlobalUbo currentUbo = UBO;\s*\n\s*int2 pixelCoords[^\n]+\n\s*uint portal_id[^\n]+\n\s*)GlobalUbo currentUbo = UBO;\n",
        r"\1",
        text,
    )
    text = fix_mat_mul(text)
    path.write_text(text, encoding="utf-8")
    print(f"fixed {path.name}")


def fix_post_process(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = strip_glsl_preamble(text)

    # Module-scope PP bindings (set in main)
    inject = """
static GlobalUbo g_ppUbo;
static SamplerState g_ppSam;
static Texture2D<float4> g_hdrIn;
static Texture2D<float> g_depthIn;
static Texture2D<float4> g_histIn;
static RWTexture2D<float4> g_ppOut;
static RWTexture2D<float4> g_histOut;
static Texture2D<float4> g_dirtIn;
static Texture2D<float4> g_normIn;
static Texture2D<float4> g_halfIn;
static Texture2D<float4> g_palIn;
static Texture2D<float4> g_dithIn;
static RWStructuredBuffer<float> g_exposure;

"""
    if "static GlobalUbo g_ppUbo" not in text:
        text = text.replace("[[vk::push_constant]] PostProcessHeapPC pc;\n\n", "[[vk::push_constant]] PostProcessHeapPC pc;\n" + inject)

    text = text.replace("half ", "float ")
    text = text.replace("half3", "float3")
    text = text.replace("half2", "float2")

    text = text.replace("textureLod(hdrInput,", "g_hdrIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(depthInput,", "g_depthIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(historyInput,", "g_histIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(halftoneInput,", "g_halfIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(paletteInput,", "g_palIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(dirtInput,", "g_dirtIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(normalInput,", "g_normIn.SampleLevel(g_ppSam,")
    text = text.replace("textureLod(ditherInput,", "g_dithIn.SampleLevel(g_ppSam,")
    text = text.replace("texture(", "g_hdrIn.SampleLevel(g_ppSam,")

    text = re.sub(r"imageStore\(outImage,\s*(\w+),\s*float4\(", r"g_ppOut[\1] = float4(", text)
    text = re.sub(r"imageStore\(outHistory,\s*(\w+),\s*float4\(", r"g_histOut[\1] = float4(", text)

    text = text.replace("ubo.", "g_ppUbo.")
    text = text.replace("adaptedLuminance", "g_exposure[0]")

    text = text.replace("if (gid.x == 0 && gid.y == 0", "if (id.x == 0 && id.y == 0")
    text = text.replace("float2(gtid.xy)", "float2(id.xy)")

    main_open = """void main(uint3 id : SV_DispatchThreadID) {
    g_ppUbo = loadGlobalUbo(pc.globalUbo);
    g_ppSam = heapSampler(pc.defaultSampler);
    g_hdrIn = heapTex2D(pc.hdrInput);
    g_depthIn = heapTex2D_r(pc.depthInput);
    g_histIn = heapTex2D(pc.historyInput);
    g_histOut = heapRWTex2D(pc.historyOutput);
    g_ppOut = heapRWTex2D(pc.hdrOutput);
    g_dirtIn = heapTex2D(pc.dirtInput);
    g_normIn = heapTex2D(pc.normalInput);
    g_halfIn = heapTex2D(pc.halftoneInput);
    g_palIn = heapTex2D(pc.paletteInput);
    g_dithIn = heapTex2D(pc.ditherInput);
    g_exposure = *DescriptorHandle<RWStructuredBuffer<float>>(pc.exposureBuffer);
"""
    text = re.sub(
        r"void main\(uint3 id : SV_DispatchThreadID\) \{\s*\n\s*int2 pixel",
        main_open + "    int2 pixel",
        text,
        count=1,
    )

    path.write_text(text, encoding="utf-8")
    print(f"fixed {path.name}")


def fix_volumetric(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = strip_glsl_preamble(text)

    # Remove duplicate local structs (use LightTypes)
    text = re.sub(
        r"struct PointLightData \{[^}]+\};\s*struct LightGrid \{[^}]+\};\s*struct PointFaceMatrices \{[^}]+\};\s*",
        "",
        text,
    )
    text = re.sub(r"struct PortalUBO \{ GlobalUbo data\[10\]; \};\s*", "", text)

    binds = """
#define SAM heapSampler(pc.defaultSampler)
#define SUN_SHADOW heapTex2DArray(pc.shadowCsm)
#define NOISE_TEX heapTex2D_r(pc.blueNoise)
#define G_DEPTH heapTex2D_r(pc.gbufferDepth)
#define G_PORTAL heapTex2D_u(pc.gbufferPortalId)
#define VSM_ATLAS heapTex2D_r(pc.vsmAtlas)
#define VSM_PAGES heapTex2D_u(pc.vsmPageTable)
#define VOL_OUT heapRWTex2D(pc.volumetricOut)

"""
    if "#define SAM heapSampler" not in text:
        text = text.replace("[[vk::push_constant]] VolumetricHeapPC pc;\n\n", "[[vk::push_constant]] VolumetricHeapPC pc;\n" + binds)

    text = text.replace("textureLod(sunShadowMap,", "SUN_SHADOW.SampleLevel(SAM,")
    text = text.replace("textureLod(vsmPhysicalAtlas,", "VSM_ATLAS.SampleLevel(SAM,")
    text = text.replace("textureLod(noiseTexture,", "NOISE_TEX.SampleLevel(SAM,")
    text = text.replace("virtualPages[", "(*DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable))[")
    text = text.replace("texelFetch(gDepth,", "G_DEPTH.Load(int3(")
    text = text.replace("texelFetch(gPortalID,", "G_PORTAL.Load(int3(")
    text = re.sub(r"Load\(int3\(\s*([^,)]+)\s*,\s*0\)\.r", r"Load(int3(\1, 0)).x", text)
    text = text.replace("imageSize(outVolumetric)", "uint2(0,0) /* vol size */")
    text = re.sub(r"imageStore\(outVolumetric,\s*(\w+),\s*float4\(", r"VOL_OUT[\1] = float4(", text)

    text = text.replace("GlobalUbo currentUbo = ubo;", "GlobalUbo currentUbo = loadGlobalUbo(pc.globalUbo);")
    text = text.replace(
        "if (portal_id > 0 && portal_id <= 10) currentUbo = portalUbos.data[portal_id - 1];",
        "if (portal_id > 0 && portal_id <= 10) currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];",
    )
    text = text.replace("ubo.gridDimX", "currentUbo.gridDimX")
    text = text.replace("ubo.gridDimY", "currentUbo.gridDimY")
    text = text.replace("lightGrid[", "(*DescriptorHandle<StructuredBuffer<LightGrid>>(pc.lightGrid))[")
    text = text.replace(
        "lightBlock.lights[",
        "(*DescriptorHandle<ConstantBuffer<LightUBOBlock>>(pc.lightBuffer)).lights[",
    )
    text = text.replace(
        "globalIndexList[",
        "(*DescriptorHandle<StructuredBuffer<uint>>(pc.lightIndexList))[",
    )
    text = text.replace("textureSize(noiseTexture, 0)", "uint2(256, 256)")
    text = fix_mat_mul(text)

    path.write_text(text, encoding="utf-8")
    print(f"fixed {path.name}")


def fix_gbuffer(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = strip_glsl_preamble(text)

    # Drop duplicate MaterialData block (second one)
    parts = text.split("struct MaterialData {", 2)
    if len(parts) == 3:
        text = parts[0] + "struct MaterialData {" + parts[1].split("};", 1)[1] + parts[2].split("};", 1)[1]

    text = re.sub(
        r"struct MaterialData \{[^}]+\};\s*struct MaterialData \{[^}]+\};\s*",
        lambda m: m.group(0).split("struct MaterialData {", 1)[0] + "struct MaterialData {" + m.group(0).split("struct MaterialData {", 2)[1].split("};", 1)[0] + "};\n\n",
        text,
        count=1,
    )

    mat_block = """
struct MaterialBlock {
    MaterialData materials[];
};

"""
    if "struct MaterialBlock" not in text:
        text = text.replace("struct FragOut {", mat_block + "struct FragOut {")

    sample_fn = r"""
float4 sampleMatTex(int texIdx, float2 uv, float2 dx, float2 dy, int useTriplanar, float3 pos, float3 posDdx, float3 posDdy, float3 blend, float scale, int repeat) {
    if (texIdx < 0) return float4(0.0);
    Texture2D<float4> t = heapBindlessTex2D(texIdx);
    SamplerState s = heapSampler(pc.defaultSampler);
    if (useTriplanar == 1) {
        float2 uvX = pos.zy * scale; float2 uvY = pos.xz * scale; float2 uvZ = pos.xy * scale;
        float2 dxX = posDdx.zy * scale; float2 dyX = posDdy.zy * scale;
        float2 dxY = posDdx.xz * scale; float2 dyY = posDdy.xz * scale;
        float2 dxZ = posDdx.xy * scale; float2 dyZ = posDdy.xy * scale;
        if (repeat == 0) { uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0); }
        float4 tx = t.SampleGrad(s, uvX, dxX, dyX);
        float4 ty = t.SampleGrad(s, uvY, dxY, dyY);
        float4 tz = t.SampleGrad(s, uvZ, dxZ, dyZ);
        return tx * blend.x + ty * blend.y + tz * blend.z;
    }
    float2 finalUV = repeat == 1 ? uv : clamp(uv, 0.0, 1.0);
    return t.SampleGrad(s, finalUV, dx, dy);
}
"""
    text = re.sub(r"float4 sampleMatTex\([\s\S]*?\n\}\n", sample_fn + "\n", text, count=1)

    text = text.replace("textureGrad(allTextures[nonuniformEXT(mat.ormxIdx)],", "heapBindlessTex2D(mat.ormxIdx).SampleGrad(heapSampler(pc.defaultSampler),")
    text = text.replace("[discard]", "discard;")

    main_sig = """[shader("fragment")]
FragOut main(VSOut v) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    MaterialBlock matBuf = (*DescriptorHandle<StructuredBuffer<MaterialBlock>>(pc.materialStorage))[0];
    MaterialData mat = matBuf.materials[v.inMatID];
    float2 pixelPos = float2(v.inCrntPos.xy); // placeholder for psych effects using screen
"""
    text = re.sub(
        r'\[shader\("fragment"\)\]\s*void main\(\) \{\s*MaterialData mat = matBuffer\.materials\[inMatID\];',
        main_sig,
        text,
        count=1,
    )

    # Remap legacy varyings to v.*
    for name in [
        "inMatID", "inTexCoord", "inCrntPos", "inTBN0", "inTBN1", "inTBN2",
        "inColor", "inUV2", "inThickness",
    ]:
        text = re.sub(rf"\b{re.escape(name)}\b", f"v.{name}", text)
    # Fix double v.v.
    text = text.replace("v.v.", "v.")

    text = text.replace("inTBN[2]", "normalize(float3(v.inTBN0, v.inTBN1, v.inTBN2))")
    text = text.replace("inTBN[0]", "normalize(v.inTBN0)")
    text = text.replace("inTBN[1]", "normalize(v.inTBN1)")

    text = re.sub(
        r"void main\(uint3 id : SV_DispatchThreadID\)",
        "FragOut main(VSOut v)",
        text,
    )

    # Return struct outputs
    text = re.sub(
        r"(\s+)gNormalRoughness = ",
        r"\1FragOut o;\n\1o.gNormalRoughness = ",
        text,
        count=1,
    )
    text = text.replace("gAlbedoMetallic  = ", "o.gAlbedoMetallic  = ")
    text = text.replace("gHeightAO        = ", "o.gHeightAO        = ")
    text = text.replace("gEmissive        = ", "o.gEmissive        = ")
    text = text.replace("o_PortalID = ubo.portalID;", "o.o_PortalID = ubo.portalID;\n    return o;")
    text = text.replace("gl_FragCoord", "/*fragcoord*/ float2(0,0) /* use v */")

    path.write_text(text, encoding="utf-8")
    print(f"fixed {path.name}")


def main() -> None:
    fix_lighting(SHADERS / "lighting.comp.slang")
    fix_post_process(SHADERS / "post_process.comp.slang")
    fix_volumetric(SHADERS / "volumetric.comp.slang")
    fix_gbuffer(SHADERS / "gbuffer.frag.slang")


if __name__ == "__main__":
    main()
