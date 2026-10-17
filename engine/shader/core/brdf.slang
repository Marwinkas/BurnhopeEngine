#include "core/math.slang"

float D_GGX(float NdotH, float a) {
    const float a2 = a * a;
    const float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

// Height-Correlated Smith GGX visibility function V = G / (4 * NdotV * NdotL)
float V_SmithGGXCorrelated(float NdotV, float NdotL, float a) {
    const float a2 = a * a;
    const float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    const float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

// Backwards-compatible separable Smith G (fixed to convert alpha back to roughness for Disney k)
float G_Smith(float NdotV, float NdotL, float a) {
    const float rough = sqrt(saturate(a));
    const float r = rough + 1.0;
    const float k = (r * r) / 8.0;
    const float gv = NdotV / (NdotV * (1.0 - k) + k);
    const float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 f0) {
    const float f90 = saturate(dot(f0, float3(50.0 * 0.33, 50.0 * 0.33, 50.0 * 0.33)));
    const float x = 1.0 - saturate(VdotH);
    const float x2 = x * x;
    const float f = x2 * x2 * x;
    return f0 + (f90 - f0) * f;
}

float specOcclusion(float NdotV, float ao, float roughness) {
    return saturate(pow(max(NdotV + ao, 1e-4), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// Jiménez et al. 2016: Practical Real-Time Strategies for Accurate Indirect Occlusion
// Multi-bounce colored ambient occlusion to prevent gray/muddy crevices
float3 gtaoMultiBounce(float visibility, float3 albedo) {
    const float3 a =  2.0404 * albedo - 0.3324;
    const float3 b = -4.7951 * albedo + 0.6417;
    const float3 c =  2.7552 * albedo + 0.6903;
    const float v = saturate(visibility);
    return max(float3(v, v, v), ((v * a + b) * v + c) * v);
}

float2 dfgEnv(float NdotV, float roughness) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float ltcForm(float NdotL) {
    const float c = saturate(NdotL);
    return c * c * 0.5;
}

float ggxCone(float roughness) {
    const float r = saturate(roughness);
    return r * r * 0.35;
}

float3 ggxReflect(float3 N, float3 V, float roughness, float2 u) {
    const float a = max(roughness * roughness, 0.002);
    const float phi = 6.2831853 * u.x;
    const float cosTheta = sqrt(saturate((1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y)));
    const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 t = normalize(cross(up, N));
    const float3 b = cross(N, t);
    const float3 H = normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + N * cosTheta);
    return reflect(-V, H);
}

// Earl Hammon Jr. 2017: PBR Diffuse Lighting for GGX+Smith Microsurfaces
float hammonDiffuse(float NdotL, float NdotV, float VdotL, float roughness) {
    const float nl = saturate(NdotL);
    const float nv = max(NdotV, 1e-4);
    const float facing = 0.5 + 0.5 * VdotL;
    const float rough = saturate(roughness);
    const float smoothTerm = 1.05 * (1.0 - pow(1.0 - nl, 5.0)) * (1.0 - pow(1.0 - nv, 5.0));
    const float roughTerm = rough * (0.9 - 0.4 * facing) * ((0.5 + nl) / max(nl, 0.1));
    const float multi = 0.1159 * rough;
    return ((1.0 - rough) * smoothTerm + rough * roughTerm + multi) / kPi;
}

float coatLeft(float NdotV, float coat) {
    const float fresnel = pow(1.0 - saturate(NdotV), 5.0);
    const float f = 0.04 + 0.96 * fresnel;
    return 1.0 - f * saturate(coat);
}

float clearcoatLobe(float NdotH, float LdotH, float coat, float coatRoughness, out float fCoat) {
    const float c = saturate(coat);
    if (c <= 1e-4) {
        fCoat = 0.0;
        return 0.0;
    }
    const float a = max(coatRoughness * coatRoughness, 0.002);
    const float d = D_GGX(NdotH, a);
    const float x = 1.0 - saturate(LdotH);
    const float f = 0.04 + 0.96 * (x * x * x * x * x);
    // Kelemen geometry approximation for clearcoat
    const float v = 0.25 / max(LdotH * LdotH, 1e-4);
    fCoat = f * c;
    return d * v * fCoat;
}

float3 energyCompensation(float3 f0, float NdotV, float roughness) {
    const float2 dfg = dfgEnv(NdotV, roughness);
    return min(1.0 + f0 * (1.0 / max(dfg.y, 1e-3) - 1.0), float3(3.0, 3.0, 3.0));
}