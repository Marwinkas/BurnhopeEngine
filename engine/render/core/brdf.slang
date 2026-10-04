#include "core/math.slang"

float D_GGX(float NdotH, float a) {
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float G_Smith(float NdotV, float NdotL, float a) {
    const float r = a + 1.0;
    const float k = (r * r) / 8.0;
    const float gv = NdotV / (NdotV * (1.0 - k) + k);
    const float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);
}

float specOcclusion(float NdotV, float ao, float roughness) {
    return saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
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

float hammonDiffuse(float NdotL, float NdotV, float VdotL, float roughness) {
    const float facing = 0.5 + 0.5 * VdotL;
    const float rough = saturate(roughness);
    const float single = (1.05 / kPi) * (1.0 - pow(1.0 - saturate(NdotL), 5.0)) * (1.0 - pow(1.0 - saturate(NdotV), 5.0));
    const float retro = rough * rough * facing / kPi;
    return single * (1.0 - rough) + retro;
}

float coatLeft(float NdotV, float coat) {
    const float fresnel = pow(1.0 - saturate(NdotV), 5.0);
    const float f = 0.04 + 0.96 * fresnel;
    return 1.0 - f * saturate(coat);
}

float3 energyCompensation(float3 f0, float NdotV, float roughness) {
    const float2 dfg = dfgEnv(NdotV, roughness);
    return min(1.0 + f0 * (1.0 / max(dfg.y, 1e-3) - 1.0), float3(4.0, 4.0, 4.0));
}
