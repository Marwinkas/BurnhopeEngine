#include "core/math.slang"

float D_GGX(float NdotH, float alpha) {
    const float oneMinus = 1.0 - NdotH * NdotH;
    const float a = NdotH * alpha;
    const float k = min(alpha / max(oneMinus + a * a, 1e-7), 453.5);
    return k * k * (1.0 / kPi);
}

float computeDielectricF0(float reflectance) {
    return 0.16 * reflectance * reflectance;
}

float3 computeF0(float3 baseColor, float metallic, float reflectance) {
    return baseColor * metallic + reflectance * (1.0 - metallic);
}

float3 computeDiffuseColor(float3 baseColor, float metallic) {
    return baseColor * (1.0 - metallic);
}

float3 f0ClearCoatToSurface(float3 f0) {
    return saturate(f0 * (f0 * (0.941892 - 0.263008 * f0) + 0.346479) - 0.0285998);
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
float Fd_Lambert() {
    return 1.0 / kPi;
}

float schlickWeight(float f0, float f90, float cosine) {
    const float x = 1.0 - saturate(cosine);
    const float x2 = x * x;
    return f0 + (f90 - f0) * x2 * x2 * x;
}

float Fd_Burley(float roughness, float NdotV, float NdotL, float LoH) {
    const float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    return schlickWeight(1.0, f90, NdotL) * schlickWeight(1.0, f90, NdotV) / kPi;
}

float filamentDistanceAttenuation(float dist, float radius) {
    const float dist2 = max(dist * dist, 1e-4);
    const float falloff = 1.0 / max(radius * radius, 1e-4);
    const float factor = dist2 * falloff;
    const float smoothFactor = saturate(1.0 - factor * factor);
    return (smoothFactor * smoothFactor) / dist2;
}

float D_Charlie(float roughness, float NdotH) {
    const float invAlpha = 1.0 / max(roughness, 0.045);
    const float cos2h = NdotH * NdotH;
    const float sin2h = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * kPi);
}

float V_Neubelt(float NdotV, float NdotL) {
    return 1.0 / max(4.0 * (NdotL + NdotV - NdotL * NdotV), 1e-4);
}

float3 clothSheen(float roughness, float3 sheenColor, float NdotV, float NdotL, float NdotH) {
    return D_Charlie(roughness, NdotH) * V_Neubelt(NdotV, NdotL) * sheenColor;
}

float D_GGXAniso(float at, float ab, float TdotH, float BdotH, float NdotH) {
    const float a2 = at * ab;
    const float3 d = float3(ab * TdotH, at * BdotH, a2 * NdotH);
    const float d2 = max(dot(d, d), 1e-8);
    const float b2 = a2 / d2;
    return a2 * b2 * b2 / kPi;
}

float3 coloredPenumbra(float3 diffuse, float3 albedo, float occlusion) {
    const float3 penumbra = min(float3(1.0 / kPi, 1.0 / kPi, 1.0 / kPi), diffuse / max(2.0 * (1.0 - albedo), float3(1e-3, 1e-3, 1e-3)));
    const float edge = pow(saturate(occlusion), 4.0);
    return lerp(penumbra, diffuse, edge);
}

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