#version 450
#extension GL_EXT_nonuniform_qualifier : require 
layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;
layout (location = 2) out vec4 gHeightAO; 
layout (location = 3) out vec4 gEmissive;
layout (location = 0) in vec3 inCrntPos;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in mat3 inTBN;
layout (location = 5) flat in uint inMatID;
layout(set = 0, binding = 0) uniform GlobalSceneUbo {
    mat4 projection;
    mat4 invViewProj;
    mat4 view;
    vec3 camPos;
    float zNear;
    vec3 sunDir;
    float zFar;
    vec4 screenSize;
    mat4 sunLightSpaceMatrices[4];
     vec4 cascadeSplits;
    uint gridDimX;
    uint gridDimY;
    uint gridDimZ;
    float lightSize;
        vec3 sunColor;  
    float sunIntensity;  
    vec4 sscsParams;
    vec4 gtaoParams;
    vec4 fogParams;
    vec4 fogColor;
    vec4 inscatterColor;
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
    vec4 skySunParams;
    vec4 ssgiParams;
    vec4 rtParams;
} ubo;
struct MaterialData {
    int albedoIdx;
    int normalIdx;
    int heightIdx;
    int metallicIdx;
    int roughnessIdx;
    int aoIdx;
    int emissiveIdx;
    int hasAlbedo;
    int hasNormal;
    int hasHeight;
    int hasMetallic;
    int hasRoughness;
    int hasAO;
    int hasEmissive;
    int useTriplanar;
    float triplanarScale;
    vec2 uvScale;
    float emissiveIntensity;
    int useORM;
};
layout(std430, set = 1, binding = 1) readonly buffer MaterialBlock {
    MaterialData materials[];
} matBuffer;
layout(set = 2, binding = 0) uniform sampler2D allTextures[];
void main() {
    MaterialData mat = matBuffer.materials[inMatID];
    vec2 scaledUV = inTexCoord * mat.uvScale;
    vec2 finalUV  = scaledUV;
    vec3 N = normalize(inTBN[2]);
    vec3 T = normalize(inTBN[0]);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 finalTBN = mat3(T, B, N);
    float height    = 0.0;
    vec3  albedo    = vec3(1.0); 
    vec3  emissive  = vec3(0.0);
    float metallic  = 0.0;
    float roughness = 1.0;
    float ao        = 1.0;
    vec3  worldNormal = N;
    if (mat.hasHeight == 1) {
        vec3 viewDirWorld   = normalize(ubo.camPos - inCrntPos);
        vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
        height  = texture(allTextures[nonuniformEXT(mat.heightIdx)], scaledUV).r;
        finalUV = scaledUV - viewDirTangent.xy * (height * 0.02);
        height  = texture(allTextures[nonuniformEXT(mat.heightIdx)], finalUV).r; 
    }
    if (mat.hasEmissive == 1) {
        emissive = texture(allTextures[nonuniformEXT(mat.emissiveIdx)], finalUV).rgb;
        emissive *= mat.emissiveIntensity;
    }
    if (mat.hasAlbedo == 1) {
        albedo = texture(allTextures[nonuniformEXT(mat.albedoIdx)], finalUV).rgb;
    }
    if (mat.useORM == 1) {
        if (mat.hasRoughness == 1) { 
            vec3 orm = texture(allTextures[nonuniformEXT(mat.roughnessIdx)], finalUV).rgb;
            ao        = orm.r;
            roughness = orm.g;
            metallic  = orm.b;
        }
    } else {
        if (mat.hasMetallic == 1)  metallic  = texture(allTextures[nonuniformEXT(mat.metallicIdx)], finalUV).r;
        if (mat.hasRoughness == 1) roughness = texture(allTextures[nonuniformEXT(mat.roughnessIdx)], finalUV).r;
        if (mat.hasAO == 1)        ao        = texture(allTextures[nonuniformEXT(mat.aoIdx)], finalUV).r;
    }
    if (mat.hasNormal == 1) {
        vec2 rg = texture(allTextures[nonuniformEXT(mat.normalIdx)], finalUV).rg;
        vec3 tangentNormal;
        tangentNormal.xy = rg * 2.0 - 1.0;
        tangentNormal.z  = sqrt(max(0.0, 1.0 - dot(tangentNormal.xy, tangentNormal.xy))); 
        worldNormal = normalize(finalTBN * tangentNormal);
    }
    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, 0.0, 1.0);
    gEmissive        = vec4(emissive, 1.0);
}