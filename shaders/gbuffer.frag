#version 450
#extension GL_EXT_nonuniform_qualifier : require 

layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;
layout (location = 2) out vec4 gHeightAO; 

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
    float cascadeSplits[4];
    uint gridDimX;
    uint gridDimY;
    uint gridDimZ;
    float lightSize;
} ubo;

struct MaterialData {
    int albedoIdx;
    int normalIdx;
    int heightIdx;
    int metallicIdx;
    int roughnessIdx;
    int aoIdx;
    
    int hasAlbedo, hasNormal, hasHeight, hasMetallic, hasRoughness, hasAO;
    
    int useTriplanar;
    float triplanarScale;
    vec2 uvScale;
    
    int useORM;
    int pad0, pad1, pad2; // Совпадает с C++
};
layout(std430, set = 1, binding = 1) readonly buffer MaterialBlock {
    MaterialData materials[];
} matBuffer;

layout(set = 2, binding = 0) uniform sampler2D allTextures[];

void main() {
    MaterialData mat = matBuffer.materials[inMatID];
    vec2 scaledUV = inTexCoord * mat.uvScale;
    vec2 finalUV  = scaledUV;
    
    // Gram-Schmidt ортогонализация
    vec3 N = normalize(inTBN[2]);
    vec3 T = normalize(inTBN[0]);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 finalTBN = mat3(T, B, N);

    // Дефолтные значения (защита от темноты, если текстур нет)
    float height    = 0.0;
    vec3  albedo    = vec3(1.0);
    float metallic  = 0.0;
    float roughness = 1.0;
    float ao        = 1.0;
    vec3  worldNormal = N;

    // 1. Parallax / Height
    if (mat.hasHeight == 1) {
        vec3 viewDirWorld   = normalize(ubo.camPos - inCrntPos);
        vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
        
        height  = texture(allTextures[nonuniformEXT(mat.heightIdx)], scaledUV).r;
        finalUV = scaledUV - viewDirTangent.xy * (height * 0.02);
        height  = texture(allTextures[nonuniformEXT(mat.heightIdx)], finalUV).r; 
    }

    // 2. Чтение Albedo
    if (mat.hasAlbedo == 1) {
        albedo = texture(allTextures[nonuniformEXT(mat.albedoIdx)], finalUV).rgb;
    }

    // 3. Чтение PBR: Выбираем режим ORM или Раздельный
    if (mat.useORM == 1) {
        // Режим glTF: читаем одну текстуру и бьем по каналам
        if (mat.hasRoughness == 1) { // Индекс обычно хранится тут
            vec3 orm = texture(allTextures[nonuniformEXT(mat.roughnessIdx)], finalUV).rgb;
            ao        = orm.r;
            roughness = orm.g;
            metallic  = orm.b;
        }
    } else {
        // Режим раздельных текстур
        if (mat.hasMetallic == 1)  metallic  = texture(allTextures[nonuniformEXT(mat.metallicIdx)], finalUV).r;
        if (mat.hasRoughness == 1) roughness = texture(allTextures[nonuniformEXT(mat.roughnessIdx)], finalUV).r;
        if (mat.hasAO == 1)        ao        = texture(allTextures[nonuniformEXT(mat.aoIdx)], finalUV).r;
    }

    // 4. Реконструкция нормали
    if (mat.hasNormal == 1) {
        vec2 rg = texture(allTextures[nonuniformEXT(mat.normalIdx)], finalUV).rg;
        vec3 tangentNormal;
        tangentNormal.xy = rg * 2.0 - 1.0;
        tangentNormal.z  = sqrt(max(0.0, 1.0 - dot(tangentNormal.xy, tangentNormal.xy))); // Восстанавливаем Z
        worldNormal = normalize(finalTBN * tangentNormal);
    }

    // Вывод в G-Buffer
    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, 0.0, 1.0);
}