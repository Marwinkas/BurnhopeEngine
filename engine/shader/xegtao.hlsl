#define VA_SATURATE saturate

#include "XeGTAO.hlsli"

cbuffer GTAOConstantBuffer : register(b0) {
    GTAOConstants g_GTAOConsts;
}

SamplerState g_samplerPointClamp : register(s0);

#ifndef XE_ENTRY
#define XE_ENTRY 0
#endif

#if XE_ENTRY == 0
Texture2D<float> g_srcRawDepth : register(t0);
RWTexture2D<float> g_outWorkingDepthMIP0 : register(u0);
RWTexture2D<float> g_outWorkingDepthMIP1 : register(u1);
RWTexture2D<float> g_outWorkingDepthMIP2 : register(u2);
RWTexture2D<float> g_outWorkingDepthMIP3 : register(u3);
RWTexture2D<float> g_outWorkingDepthMIP4 : register(u4);

[numthreads(8, 8, 1)]
void CSPrefilter(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp,
        g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2, g_outWorkingDepthMIP3, g_outWorkingDepthMIP4);
}
#endif

#if XE_ENTRY == 1
Texture2D<float> g_srcRawDepth : register(t0);
RWTexture2D<uint> g_outNormalmap : register(u0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSNormals(uint2 pixCoord : SV_DispatchThreadID) {
    float3 viewspaceNormal = XeGTAO_ComputeViewspaceNormal(pixCoord, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp);
    const float2 uv = (float2(pixCoord) + 0.5) * g_GTAOConsts.ViewportPixelSize;
    const float viewZ = max(XeGTAO_ScreenSpaceToViewSpaceDepth(g_srcRawDepth.Load(int3(pixCoord, 0)), g_GTAOConsts), 1e-3);
    const float3 viewVec = normalize(-XeGTAO_ComputeViewspacePosition(uv, viewZ, g_GTAOConsts));
    if (dot(viewspaceNormal, viewVec) < 0.0) {
        viewspaceNormal = -viewspaceNormal;
    }
    g_outNormalmap[pixCoord] = XeGTAO_FLOAT3_to_R11G11B10_UNORM(saturate(viewspaceNormal * 0.5 + 0.5));
}
#endif

#if XE_ENTRY == 2
Texture2D<float> g_srcWorkingDepth : register(t0);
Texture2D<uint> g_srcNormalmap : register(t1);
RWTexture2D<uint> g_outWorkingAOTerm : register(u0);
RWTexture2D<unorm float> g_outWorkingEdges : register(u1);

lpfloat3 LoadNormal(int2 pos) {
    uint packedInput = g_srcNormalmap.Load(int3(pos, 0)).x;
    float3 unpackedOutput = XeGTAO_R11G11B10_UNORM_to_FLOAT3(packedInput);
    return (lpfloat3)normalize(unpackedOutput * 2.0.xxx - 1.0.xxx);
}

lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex) {
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288u * (temporalIndex % 64u);
    return lpfloat2(frac(0.5 + float(index) * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOLow(uint2 pixCoord : SV_DispatchThreadID) {
    XeGTAO_MainPass(pixCoord, 1, 2, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), LoadNormal(pixCoord),
        g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOMedium(uint2 pixCoord : SV_DispatchThreadID) {
    XeGTAO_MainPass(pixCoord, 2, 2, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), LoadNormal(pixCoord),
        g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOHigh(uint2 pixCoord : SV_DispatchThreadID) {
    XeGTAO_MainPass(pixCoord, 3, 3, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), LoadNormal(pixCoord),
        g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOUltra(uint2 pixCoord : SV_DispatchThreadID) {
    XeGTAO_MainPass(pixCoord, 9, 3, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), LoadNormal(pixCoord),
        g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}
#endif

#if XE_ENTRY == 3
Texture2D<uint> g_srcWorkingAOTerm : register(t0);
Texture2D<float> g_srcWorkingEdges : register(t1);
RWTexture2D<uint> g_outFinalAOTerm : register(u0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoise(uint2 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp, g_outFinalAOTerm, false);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoiseLast(uint2 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp, g_outFinalAOTerm, true);
}
#endif

#if XE_ENTRY == 4
Texture2D<uint> g_srcWorkingAOTerm : register(t0);
RWTexture2D<float> g_ao : register(u0);

[numthreads(8, 8, 1)]
void CSResolve(uint2 pixCoord : SV_DispatchThreadID) {
    uint packed = g_srcWorkingAOTerm.Load(int3(pixCoord, 0)).x;
    g_ao[pixCoord] = saturate(float(packed) / 255.0);
}
#endif
