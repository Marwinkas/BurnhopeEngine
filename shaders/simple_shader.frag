#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;
layout (location = 3) in vec2 fragUv;
layout (location = 4) in mat3 TBN;
layout (location = 0) out vec4 outColor;

struct PointLight {
  vec4 position; // ignore w
  vec4 color;    // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor; // w is intensity
  PointLight pointLights[10];
  int numLights;
} ubo;

layout(set = 1, binding = 1) uniform sampler2D diffuseMap;
layout(set = 1, binding = 2) uniform sampler2D NormalMap;
layout(set = 1, binding = 3) uniform sampler2D AOMap;
layout(set = 1, binding = 4) uniform sampler2D RoughnessMap;
layout(set = 1, binding = 5) uniform sampler2D MetallicMap;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float nom = a2;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom = PI * denom * denom;

  return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;

  float nom = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx1 = GeometrySchlickGGX(NdotV, roughness);
  float ggx2 = GeometrySchlickGGX(NdotL, roughness);
  return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0, float roughness) {
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}



vec3 ACESFittedTonemap(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}


void main() {
  vec3 albedo = texture(diffuseMap, fragUv).rgb;
  vec3 normalMapSample = texture(NormalMap, fragUv).rgb;
  vec3 normalTangent = normalMapSample * 2.0 - 1.0;
  vec3 N = normalize(TBN * normalTangent);

  float ao = texture(AOMap, fragUv).r;
  float roughness = max(texture(RoughnessMap, fragUv).r, 0.05); // было просто texture

  float metallic = texture(MetallicMap, fragUv).r;


  roughness = clamp(roughness, 0.05, 1.0);
  metallic = clamp(metallic, 0.0, 1.0);


  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  vec3 V = normalize(ubo.invView[3].xyz - fragPosWorld);
  vec3 ambient = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * ao * albedo;

  vec3 Lo = vec3(0.0);

  for (int i = 0; i < ubo.numLights; ++i) {
    PointLight light = ubo.pointLights[i];
    vec3 L = normalize(light.position.xyz - fragPosWorld);
    vec3 H = normalize(V + L);
    float distance = length(light.position.xyz - fragPosWorld);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = light.color.xyz * light.color.w * attenuation;

    // === Cook-Torrance BRDF ===
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0,roughness);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(max(dot(N, V), 0.05) * max(dot(N, L), 0.05), 0.01);
    vec3 specular     = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    Lo += (kD * albedo / PI + specular) * radiance * NdotL;
  }

  vec3 color = ambient + Lo;

  // ACES Filmic
  color = ACESFittedTonemap(color);

  // Гамма-коррекция (sRGB)
  color = pow(color, vec3(1.0 / 1.2));

  outColor = vec4(color, 1.0);
}
