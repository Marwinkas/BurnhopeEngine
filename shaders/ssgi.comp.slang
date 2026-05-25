import common.Bindless;
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

float3 getWorldPos(GlobalUbo ubo, float depth, float2 uv) {
    float4 pos = float4(uv * 2.0 - 1.0, depth, 1.0);
    pos = mul(ubo.invViewProj, pos);
    return pos.xyz / pos.w;
}

float rand(float2 co) {
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float3x3 getTBN(float3 normal) {
    float3 tangent = normalize(abs(normal.y) > 0.99 ? float3(1, 0, 0) : cross(float3(0, 1, 0), normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    Texture2D<float4> gNormal = heapTex2D(pc.gbufferNormal);
    Texture2D<float4> blueNoise = heapTex2D(pc.blueNoise);
    RWTexture2D<float4> hdrOut = heapRWTex2D(pc.hdrOutput);
    RWTexture2D<float4> ssgiRaw = heapRWTex2D(pc.ssgiRaw);

    int2 pixelCoords = int2(id.xy);
    float2 uv = (float2(pixelCoords) + 0.5) / ubo.screenSize.xy;

    float depth = gDepth.SampleLevel(sam, uv, 0.0).x;
    if (depth >= 1.0 || ubo.ssgiParams.x < 0.5) {
        ssgiRaw[pixelCoords] = float4(0.0);
        return;
    }

    int SSGI_RAY_COUNT = int(ubo.ssgiParams.y);
    float SSGI_STEP_SIZE = ubo.ssgiParams.z;
    float SSGI_THICKNESS = ubo.ssgiParams.w;
    int SSGI_STEP_COUNT = 16;

    float4 normalRoughness = gNormal.SampleLevel(sam, uv, 0.0);
    float3 normal = normalize(normalRoughness.xyz);
    float3 worldPos = getWorldPos(ubo, depth, uv);
    float3 indirectLight = float3(0.0);
    float3x3 tbn = getTBN(normal);

    float2 noiseUV = frac(uv * 256.0);
    float4 noise = blueNoise.SampleLevel(sam, noiseUV, 0.0);

    for (int i = 0; i < SSGI_RAY_COUNT; ++i) {
        float r1 = (noise.r > 0.99 && noise.g > 0.99) ? rand(uv + float(i)) : frac(noise.r + float(i) * 0.618);
        float r2 = (noise.r > 0.99 && noise.g > 0.99) ? rand(uv + float(i) * 2.345) : frac(noise.g + float(i) * 0.382);

        float phi = 2.0 * 3.14159265 * r1;
        float cosTheta = sqrt(1.0 - r2);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 tangentRay = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        float3 rayDir = normalize(mul(tbn, tangentRay));

        float3 rayPos = worldPos + normal * 0.01;
        rayPos += rayDir * frac(noise.b + r1) * SSGI_STEP_SIZE;

        for (int j = 0; j < SSGI_STEP_COUNT; ++j) {
            float currentStep = SSGI_STEP_SIZE * (1.0 + float(j) * 0.2);
            rayPos += rayDir * currentStep;

            float4 clipPos = mul(ubo.projection, mul(ubo.view, float4(rayPos, 1.0)));
            float2 sampleUv = (clipPos.xy / clipPos.w) * 0.5 + 0.5;
            if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) break;

            float sceneDepth = gDepth.SampleLevel(sam, sampleUv, 0.0).x;
            float3 scenePos = getWorldPos(ubo, sceneDepth, sampleUv);
            float rayDepthView = mul(ubo.view, float4(rayPos, 1.0)).z;
            float sceneDepthView = mul(ubo.view, float4(scenePos, 1.0)).z;

            if (rayDepthView < sceneDepthView &&
                sceneDepthView - rayDepthView < SSGI_THICKNESS * (1.0 + float(j) * 0.5)) {
                int2 hitPx = int2(sampleUv * ubo.screenSize.xy);
                float3 hitColor = hdrOut[hitPx].rgb;
                float falloff = 1.0 / (1.0 + distance(worldPos, scenePos));
                indirectLight += hitColor * falloff;
                break;
            }
        }
    }

    indirectLight /= float(SSGI_RAY_COUNT);
    ssgiRaw[pixelCoords] = float4(indirectLight, 1.0);
}
