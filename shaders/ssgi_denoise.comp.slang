import common.Bindless;
import common.GlobalUbo;

struct SsgiDenoiseHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint gbufferAlbedo;
    uint hdrOutput;
    uint ssgiRaw;
    uint giDiffuse;
    uint defaultSampler;
};

[[vk::push_constant]] SsgiDenoiseHeapPC pc;

float getLinearDepth(GlobalUbo ubo, float d) {
    return ubo.zNear * ubo.zFar / (ubo.zFar + d * (ubo.zNear - ubo.zFar));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    Texture2D<float4> gNormal = heapTex2D(pc.gbufferNormal);
    Texture2D<float4> gAlbedo = heapTex2D(pc.gbufferAlbedo);
    Texture2D<float4> ssgiIn = heapTex2D(pc.ssgiRaw);
    RWTexture2D<float4> hdrOut = heapRWTex2D(pc.hdrOutput);

    int2 pixelCoords = int2(id.xy);
    if (pixelCoords.x >= int(ubo.screenSize.x) || pixelCoords.y >= int(ubo.screenSize.y)) return;

    float2 uv = (float2(pixelCoords) + 0.5) / ubo.screenSize.xy;
    float centerDepthRaw = gDepth.SampleLevel(sam, uv, 0.0).x;
    if (centerDepthRaw >= 1.0) return;

    float centerDepth = getLinearDepth(ubo, centerDepthRaw);
    float3 centerNormal = normalize(gNormal.SampleLevel(sam, uv, 0.0).xyz);

    float3 sumColor = float3(0.0);
    float sumWeight = 0.0;
    int radius = 2;
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            int2 sampleCoords = clamp(pixelCoords + int2(x, y), int2(0), int2(ubo.screenSize.xy) - 1);
            float2 sampleUv = (float2(sampleCoords) + 0.5) / ubo.screenSize.xy;
            float sampleDepthRaw = gDepth.SampleLevel(sam, sampleUv, 0.0).x;
            float sampleDepth = getLinearDepth(ubo, sampleDepthRaw);
            float3 sampleNormal = normalize(gNormal.SampleLevel(sam, sampleUv, 0.0).xyz);
            float3 sampleGI = ssgiIn[sampleCoords].rgb;
            float wSpatial = exp(-float(x * x + y * y) / (2.0 * float(radius * radius)));
            float wDepth = exp(-abs(centerDepth - sampleDepth) * 20.0);
            float wNormal = pow(max(dot(centerNormal, sampleNormal), 0.0), 32.0);
            float weight = wSpatial * wDepth * wNormal;
            sumColor += sampleGI * weight;
            sumWeight += weight;
        }
    }

    float3 blurredGI = sumColor / max(sumWeight, 0.0001);
    float3 albedo = gAlbedo.SampleLevel(sam, uv, 0.0).rgb;
    float3 directLight = hdrOut[pixelCoords].rgb;
    hdrOut[pixelCoords] = float4(directLight + blurredGI * albedo, 1.0);
}
