import common.Bindless;
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

static const float PI = 3.14159265359;

float2 octaEncode(float3 dir) {
    dir = normalize(dir);
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));
    if (dir.z < 0.0) dir.xy = (1.0 - abs(dir.yx)) * sign(dir.xy);
    return dir.xy * 0.5 + 0.5;
}

float3 sampleProbe(int3 probeIdx, float3 dir) {
    int octaSize = int(pc.params.w);
    int3 pCount = pc.probeCount.xyz;
    float2 octaUV = octaEncode(dir);
    
    int baseX = probeIdx.x * octaSize;
    int baseY = (probeIdx.z * pCount.y + probeIdx.y) * octaSize;

    float2 p = clamp(octaUV * float(octaSize) - 0.5, float2(0.0), float2(float(octaSize - 1)));
    int2 i0 = int2(floor(p));
    int2 i1 = min(i0 + int2(1), int2(octaSize - 1));
    float2 f = frac(p);

    int2 t00 = int2(baseX, baseY) + i0;
    int2 t10 = int2(baseX, baseY) + int2(i1.x, i0.y);
    int2 t01 = int2(baseX, baseY) + int2(i0.x, i1.y);
    int2 t11 = int2(baseX, baseY) + i1;

    Texture2D<float4> cascade0 = heapTex2D(pc.giCascade0);
    float3 c00 = cascade0.Load(int3(t00, 0)).rgb;
    float3 c10 = cascade0.Load(int3(t10, 0)).rgb;
    float3 c01 = cascade0.Load(int3(t01, 0)).rgb;
    float3 c11 = cascade0.Load(int3(t11, 0)).rgb;

    float3 cx0 = lerp(c00, c10, f.x);
    float3 cx1 = lerp(c01, c11, f.x);
    return lerp(cx0, cx1, f.y);
}

float3 sampleRadiance(float3 worldPos, float3 N, float3 dir) {
    float3 gridSize = pc.probeGridMax.xyz - pc.probeGridMin.xyz;
    int3 pCount  = pc.probeCount.xyz;
    // Уменьшаем смещение с 0.5 до 0.1. Огромный bias "пробивал" тонкие стены насквозь!
    float3 biasedPos = worldPos + N * 0.1; 
    float3 localPos = (biasedPos - pc.probeGridMin.xyz) / gridSize;
    
    float3 probeF = clamp(localPos * float3(pCount) - 0.5, float3(0.0), float3(pCount - int3(1)));
    int3 p000 = int3(floor(probeF));
    int3 p111 = min(p000 + 1, pCount - int3(1));
    float3 t = frac(probeF);

    float3 radAccum = float3(0.0);
    float weightSum = 0.0;
    
    for (int ix = 0; ix < 2; ix++)
    for (int iy = 0; iy < 2; iy++)
    for (int iz = 0; iz < 2; iz++) {
        int3 idx = int3(ix == 0 ? p000.x : p111.x, iy == 0 ? p000.y : p111.y, iz == 0 ? p000.z : p111.z);
        
        float lerpW = (ix == 0 ? (1.0 - t.x) : t.x) *
                      (iy == 0 ? (1.0 - t.y) : t.y) *
                      (iz == 0 ? (1.0 - t.z) : t.z);
        
        float3 probeWorldPos = pc.probeGridMin.xyz + gridSize * (float3(idx) + 0.5) / float3(pCount);
        float3 toProbe = normalize(probeWorldPos - worldPos);
        float NdotP = dot(N, toProbe);
        
        // Жестко отсекаем зонды, которые находятся "позади" поверхности (чтобы не тянуть свет с улицы)
        float weight = max(0.00001, pow(max(0.0, NdotP), 4.0));
        float finalW = lerpW * weight;

        radAccum += sampleProbe(idx, dir) * finalW;
        weightSum += finalW;
    }
    return radAccum / weightSum;
}

float3 getOrthoVec(float3 n) { return abs(n.x) > 0.9 ? float3(0, 1, 0) : float3(1, 0, 0); }

float2 hammersley(uint i, uint N) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10;
    return float2(float(i)/float(N), rdi);
}

float3 importanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(0u);
    Texture2D<float4> gNormalRoughness = heapTex2D(pc.gbufferNormal);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    RWTexture2D<float4> outDiffuseGI = heapRWTex2D(pc.giDiffuseOut);
    RWTexture2D<float4> outSpecularGI = heapRWTex2D(pc.giSpecularOut);

    int2 pixel = int2(id.xy);
    if (pixel.x >= int(pc.params.y) || pixel.y >= int(pc.params.z)) return;

    float2 uv = (float2(pixel) + 0.5) / float2(pc.params.y, pc.params.z);
    float depth = gDepth.SampleLevel(sam, uv, 0.0).x;

    if (depth >= 1.0) {
        outDiffuseGI[pixel] = float4(0.0);
        outSpecularGI[pixel] = float4(0.0);
        return;
    }
    if (ubo.rtParams.z < 0.5) {
        outDiffuseGI[pixel] = float4(0.0);
        outSpecularGI[pixel] = float4(0.0);
        return;
    }

    float4 normRough = gNormalRoughness.SampleLevel(sam, uv, 0.0);
    float3 N = normalize(normRough.rgb);
    float roughness = clamp(normRough.a, 0.05, 1.0);
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldRaw = mul(ubo.invViewProj, ndc);
    float3 worldPos = worldRaw.xyz / worldRaw.w;
    float3 V = normalize(ubo.camPos - worldPos);

    float3 tangent = normalize(cross(N, getOrthoVec(N)));
    float3 bitangent = cross(N, tangent);
    
    // --- 1. Diffuse GI (Hemisphere Cosine-weighted) ---
    float3 diffuseGI = float3(0.0);
    const int NUM_DIFFUSE = 8;
    for (int d = 0; d < NUM_DIFFUSE; d++) {
        float i_f = float(d) + 0.5;
        float phi = 2.39996323 * i_f;
        float cosTheta = sqrt(1.0 - i_f / float(NUM_DIFFUSE));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 dir = (cos(phi) * sinTheta) * tangent + (sin(phi) * sinTheta) * bitangent + cosTheta * N;
        diffuseGI += sampleRadiance(worldPos, N, dir);
    }
    diffuseGI /= float(NUM_DIFFUSE);

    // --- 2. Specular GI (GGX Importance Sampling) ---
    float3 specularGI = float3(0.0);
    float specWeight = 0.0;
    const uint NUM_SPECULAR = 8u;
    for (uint i = 0u; i < NUM_SPECULAR; i++) {
        float2 Xi = hammersley(i, NUM_SPECULAR);
        float3 H = importanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(reflect(-V, H));
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            specularGI += sampleRadiance(worldPos, N, L) * NdotL;
            specWeight += NdotL;
        }
    }
    if (specWeight > 0.0) specularGI /= specWeight;

    outDiffuseGI[pixel] = float4(diffuseGI, 1.0);
    outSpecularGI[pixel] = float4(specularGI, 1.0);
}