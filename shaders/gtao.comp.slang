import common.Bindless;
import common.GlobalUbo;

struct GtaoHeapPC {
    uint globalUbo;
    uint depthTex;
    uint normalTex;
    uint gtaoOutput;
    uint defaultSampler;
};

[[vk::push_constant]] GtaoHeapPC pc;




static const int   STEPS = 3;            // ОПТИМИЗАЦИЯ: 3 шага
static const int   DIRECTIONS = 4;       // ОПТИМИЗАЦИЯ: 4 направления (итого 12 выборок)
static const float PI = 3.14159265359;

float3 reconstructWorldPos(GlobalUbo ubo, float2 uv, float depth) {
    float4 clipSpace = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldSpace = mul(ubo.invViewProj, clipSpace);
    return worldSpace.xyz / worldSpace.w;
}

float interleavedGradientNoise(float2 pixelCoord) {
    return frac(52.9829189 * frac(dot(pixelCoord, float2(0.06711056, 0.00583715))));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> depthMap = heapTex2D_r(pc.depthTex);
    Texture2D<float4> normalRoughnessMap = heapTex2D(pc.normalTex);
    RWTexture2D<float4> outGTAO = heapRWTex2D(pc.gtaoOutput);

    int2 pixelCoord = int2(id.xy);
    int2 texSize = int2(ubo.screenSize.xy * 0.5);
    if (pixelCoord.x >= texSize.x || pixelCoord.y >= texSize.y) return;
    
    if (ubo.gtaoParams.x < 0.5) {
        outGTAO[pixelCoord] = float4(1.0);
        return;
    }
    
    float2 uv = (float2(pixelCoord) + 0.5) / float2(texSize);
    float depth = depthMap.SampleLevel(sam, uv, 0.0).x;
    
    if (depth >= 1.0) {
        outGTAO[pixelCoord] = float4(1.0);
        return;
    }
    
    float3 viewPos = mul(ubo.view, float4(reconstructWorldPos(ubo, uv, depth), 1.0)).xyz;
    float3 worldNormal = normalRoughnessMap.SampleLevel(sam, uv, 0.0).rgb;
    float3 viewNormal = normalize(mul((float3x3)ubo.view, worldNormal));
    
    float noise = interleavedGradientNoise(float2(pixelCoord));
    float angleOffset = noise * PI;
    float2 texelSize = 1.0 / float2(texSize);
    
    float radius = ubo.gtaoParams.y;
    float bias = ubo.gtaoParams.z;
    float screenRadius = (radius / abs(viewPos.z)) * abs(ubo.projection[1][1]); 
    screenRadius = max(screenRadius, 2.0 * texelSize.y); 
    
    float visibility = 0.0;
    for (int i = 0; i < DIRECTIONS; ++i) {
        float angle = angleOffset + (float(i) / float(DIRECTIONS)) * PI;
        float2 dir = float2(cos(angle), sin(angle));
        
        float maxElevation1 = 0.0; 
        float maxElevation2 = 0.0;
        
        for (int j = 1; j <= STEPS; ++j) {
            float t = (float(j) - 0.5 + noise) / float(STEPS); 
            float2 stepUV = dir * screenRadius * t;
            
            // Вперед
            float2 uv1 = uv + stepUV;
            if (uv1.x > 0.0 && uv1.x < 1.0 && uv1.y > 0.0 && uv1.y < 1.0) {
                float d1 = depthMap.SampleLevel(sam, uv1, 0.0).x;
                float3 p1 = mul(ubo.view, float4(reconstructWorldPos(ubo, uv1, d1), 1.0)).xyz;
                float3 v1 = p1 - viewPos;
                float lenV1 = length(v1);
                float weight = max(0.0, 1.0 - (lenV1 * lenV1) / (radius * radius));
                if(lenV1 > 0.001) {
                    float elevation = dot(v1 / lenV1, viewNormal) - bias;
                    maxElevation1 = max(maxElevation1, elevation * weight);
                }
            }
            
            // Назад
            float2 uv2 = uv - stepUV;
            if (uv2.x > 0.0 && uv2.x < 1.0 && uv2.y > 0.0 && uv2.y < 1.0) {
                float d2 = depthMap.SampleLevel(sam, uv2, 0.0).x;
                float3 p2 = mul(ubo.view, float4(reconstructWorldPos(ubo, uv2, d2), 1.0)).xyz;
                float3 v2 = p2 - viewPos;
                float lenV2 = length(v2);
                float weight = max(0.0, 1.0 - (lenV2 * lenV2) / (radius * radius));
                if(lenV2 > 0.001) {
                    float elevation = dot(v2 / lenV2, viewNormal) - bias;
                    maxElevation2 = max(maxElevation2, elevation * weight);
                }
            }
        }
        visibility += (1.0 - maxElevation1) * (1.0 - maxElevation2);
    }
    
    visibility /= float(DIRECTIONS);
    visibility = pow(visibility, ubo.gtaoParams.w); // Усиливаем контраст теней
    
    outGTAO[pixelCoord] = float4(visibility, visibility, visibility, 1.0);
}