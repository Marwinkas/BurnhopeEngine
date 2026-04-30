#version 430 core
#extension GL_ARB_bindless_texture : require 

layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;
layout (location = 2) out vec4 gHeightAO; 

in vec3 crntPos;
in vec2 texCoord;
in mat3 TBN;

// ЭТУ ПЕРЕМЕННУЮ ТЫ УЖЕ ПЕРЕДАЕШЬ ИЗ C++!
uniform vec3 camPos; 

struct MaterialData {
    sampler2D albedoHandle;
    sampler2D normalHandle;
    sampler2D heightHandle;
    sampler2D metallicHandle;
    sampler2D roughnessHandle;
    sampler2D aoHandle;
    int hasAlbedo;
    int hasNormal;
    int hasHeight;
    int hasMetallic;
    int hasRoughness;
    int hasAO;
    int useTriplanar; 
    float triplanarScale; 
    vec2 uvScale;  
    vec2 padding;  
};

layout(std430, binding = 2) readonly buffer MaterialBlock {
    MaterialData materials[];
};

flat in uint matID;

void main() {
    MaterialData mat = materials[matID];
    vec2 scaledUV = texCoord * mat.uvScale;
    vec2 finalUV  = scaledUV;

    vec3 N = normalize(TBN[2]);
    vec3 T = normalize(TBN[0]);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 finalTBN = mat3(T, B, N);

    float height   = 0.0;
    vec3  albedo   = vec3(0.8);
    float metallic = 0.0;
    float roughness= 0.9;
    float ao       = 1.0;
    vec3  worldNormal = N;

    if (mat.hasHeight == 1) {
        vec3 viewDirWorld   = normalize(camPos - crntPos);
        vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
        height  = texture(mat.heightHandle, scaledUV).r;
        finalUV = scaledUV - viewDirTangent.xy * (height * 0.02);
        height  = texture(mat.heightHandle, finalUV).r;
    }

    if (mat.hasAlbedo    == 1) albedo    = texture(mat.albedoHandle,    finalUV).rgb;
    if (mat.hasMetallic  == 1) metallic  = texture(mat.metallicHandle,  finalUV).r;
    if (mat.hasRoughness == 1) roughness = texture(mat.roughnessHandle, finalUV).r;
    if (mat.hasAO        == 1) ao        = texture(mat.aoHandle,        finalUV).r;

    if (mat.hasNormal == 1) {
        vec2 rg = texture(mat.normalHandle, finalUV).rg;
        vec3 tangentNormal;
        tangentNormal.xy = rg * 2.0 - 1.0;
        tangentNormal.z  = sqrt(max(0.0, 1.0 - dot(tangentNormal.xy, tangentNormal.xy)));
        worldNormal = normalize(finalTBN * tangentNormal);
    }

    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, 0.0, 1.0);
}