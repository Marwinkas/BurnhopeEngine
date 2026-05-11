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
    // Block 1: 16 bytes
    int albedoIdx; int normalIdx; int heightIdx; int metallicIdx;
    
    // Block 2: 16 bytes
    int roughnessIdx; int aoIdx; int emissiveIdx; int hasAlbedo;
    
    // Block 3: 16 bytes
    int hasNormal; int hasHeight; int hasMetallic; int hasRoughness;
    
    // Block 4: 16 bytes
    int hasAO; int hasEmissive; int useTriplanar; float triplanarScale;
    
    // Block 5: 16 bytes (vec2 = 8, float = 4, int = 4)
    vec2 uvScale; 
    float emissiveIntensity; 
    int useORM;
    
    // Block 6: 16 bytes
    vec4 albedoColor;
    
    // Block 7: 16 bytes
    vec4 emissiveColor;

    // Block 8: 16 bytes
    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;

    // Block 9: 16 bytes
    float aoStrength;
    int repeatTexture;
    int pad1;
    int pad2;
};

layout(std430, set = 1, binding = 1) readonly buffer MaterialBlock {
    MaterialData materials[];
} matBuffer;

layout(set = 2, binding = 0) uniform sampler2D allTextures[];

vec4 sampleMatTex(int texIdx, vec2 uv, vec2 dx, vec2 dy, int useTriplanar, vec3 pos, vec3 posDdx, vec3 posDdy, vec3 blend, float scale, int repeat) {
    if (useTriplanar == 1) {
        vec2 uvX = pos.zy * scale;
        vec2 uvY = pos.xz * scale;
        vec2 uvZ = pos.xy * scale;
        
        vec2 dxX = posDdx.zy * scale; vec2 dyX = posDdy.zy * scale;
        vec2 dxY = posDdx.xz * scale; vec2 dyY = posDdy.xz * scale;
        vec2 dxZ = posDdx.xy * scale; vec2 dyZ = posDdy.xy * scale;

        if (repeat == 0) {
            uvX = clamp(uvX, 0.0, 1.0);
            uvY = clamp(uvY, 0.0, 1.0);
            uvZ = clamp(uvZ, 0.0, 1.0);
        }
        vec4 tx = textureGrad(allTextures[nonuniformEXT(texIdx)], uvX, dxX, dyX);
        vec4 ty = textureGrad(allTextures[nonuniformEXT(texIdx)], uvY, dxY, dyY);
        vec4 tz = textureGrad(allTextures[nonuniformEXT(texIdx)], uvZ, dxZ, dyZ);
        return tx * blend.x + ty * blend.y + tz * blend.z;
    } else {
        vec2 finalUV = repeat == 1 ? uv : clamp(uv, 0.0, 1.0);
        return textureGrad(allTextures[nonuniformEXT(texIdx)], finalUV, dx, dy);
    }
}

void main() {
    MaterialData mat = matBuffer.materials[inMatID];
    
    vec2 scaledUV = inTexCoord * mat.uvScale;
    vec2 dx = dFdx(scaledUV);
    vec2 dy = dFdy(scaledUV);
    
    vec3 posDdx = dFdx(inCrntPos);
    vec3 posDdy = dFdy(inCrntPos);
    
    vec2 finalUV = mat.repeatTexture == 1 ? scaledUV : clamp(scaledUV, 0.0, 1.0);
    
    vec3 N = normalize(inTBN[2]);
    vec3 T = normalize(inTBN[0]);
    
    // Защита от NaN при коллинеарности T и N (Грам-Шмидт)
    vec3 dp = T - dot(T, N) * N;
    if (dot(dp, dp) > 0.0001) T = normalize(dp);
    else T = normalize(cross(N, abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
    
    vec3 B = cross(N, T);
    mat3 finalTBN = mat3(T, B, N);

    vec3 blendWeights = abs(N);
    blendWeights = max(blendWeights - 0.2, 0.0);
    blendWeights /= max(dot(blendWeights, vec3(1.0)), 0.00001); // Защита от деления на ноль

    float height    = 0.0;
    vec3  albedo    = mat.albedoColor.rgb; 
    vec3  emissive  = mat.emissiveColor.rgb * mat.emissiveIntensity;
    float metallic  = mat.metallicStrength;
    float roughness = mat.roughnessStrength; // Базовое значение шероховатости
    float ao        = mat.aoStrength;        // Базовая сила AO
    vec3  worldNormal = N;
    
    // Parallax Mapping (Height Map)
    if (mat.hasHeight == 1) {
        if (mat.useTriplanar == 0) {
            vec3 viewDirWorld   = normalize(ubo.camPos - inCrntPos);
            vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
            
            vec2 p = viewDirTangent.xy / max(viewDirTangent.z, 0.1);
            height  = textureGrad(allTextures[nonuniformEXT(mat.heightIdx)], finalUV, dx, dy).r;
            finalUV = scaledUV - p * (height * 0.02 * mat.heightStrength);
            
            if (mat.repeatTexture == 0) finalUV = clamp(finalUV, 0.0, 1.0);
            
            height  = textureGrad(allTextures[nonuniformEXT(mat.heightIdx)], finalUV, dx, dy).r * mat.heightStrength; 
        } else {
            height = sampleMatTex(mat.heightIdx, finalUV, dx, dy, 1, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).r * mat.heightStrength;
        }
    }
    
    // Albedo
    if (mat.hasAlbedo == 1) {
        albedo *= sampleMatTex(mat.albedoIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).rgb;
    }
    
    // Emissive
    if (mat.hasEmissive == 1) {
        emissive = sampleMatTex(mat.emissiveIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).rgb;
    }
    
    // ORM Map / Separate Metallic, Roughness, AO
    if (mat.useORM == 1) {
        if (mat.hasRoughness == 1) { 
            vec3 orm = sampleMatTex(mat.roughnessIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).rgb;
            ao        *= orm.r * mat.aoStrength; // Канал R из ORM - это AO, умножаем на силу
            roughness *= orm.g * mat.roughnessStrength;
            metallic  *= orm.b * mat.metallicStrength;
        }
    } else {
        if (mat.hasMetallic == 1)  metallic  *= sampleMatTex(mat.metallicIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).r;
        if (mat.hasRoughness == 1) roughness *= sampleMatTex(mat.roughnessIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).r;
        if (mat.hasAO == 1)        ao         *= sampleMatTex(mat.aoIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).r;
    }
    
    // Normal Map
    if (mat.hasNormal == 1) {
        if (mat.useTriplanar == 1) {
            vec2 uvX = inCrntPos.zy * mat.triplanarScale;
            vec2 uvY = inCrntPos.xz * mat.triplanarScale;
            vec2 uvZ = inCrntPos.xy * mat.triplanarScale;
            if (mat.repeatTexture == 0) {
                uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0);
            }
            
            vec3 rgbX = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvX, posDdx.zy * mat.triplanarScale, posDdy.zy * mat.triplanarScale).rgb;
            vec3 rgbY = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvY, posDdx.xz * mat.triplanarScale, posDdy.xz * mat.triplanarScale).rgb;
            vec3 rgbZ = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvZ, posDdx.xy * mat.triplanarScale, posDdy.xy * mat.triplanarScale).rgb;
            
            vec3 tX = rgbX * 2.0 - 1.0; tX.xy *= mat.normalStrength; tX.z = sqrt(max(0.0, 1.0 - dot(tX.xy, tX.xy)));
            vec3 tY = rgbY * 2.0 - 1.0; tY.xy *= mat.normalStrength; tY.z = sqrt(max(0.0, 1.0 - dot(tY.xy, tY.xy)));
            vec3 tZ = rgbZ * 2.0 - 1.0; tZ.xy *= mat.normalStrength; tZ.z = sqrt(max(0.0, 1.0 - dot(tZ.xy, tZ.xy)));

            vec3 nX = vec3(tX.z * sign(N.x), tX.y, -tX.x);
            vec3 nY = vec3(tY.x, tY.z * sign(N.y), -tY.y);
            vec3 nZ = vec3(tZ.x, tZ.y, tZ.z * sign(N.z));

            worldNormal = normalize(nX * blendWeights.x + nY * blendWeights.y + nZ * blendWeights.z);
        } else {
            vec3 rgb = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], finalUV, dx, dy).rgb;
            vec3 tangentNormal = rgb * 2.0 - 1.0;
            tangentNormal.xy *= mat.normalStrength;
            tangentNormal.z = sqrt(max(0.0, 1.0 - dot(tangentNormal.xy, tangentNormal.xy)));
            worldNormal = normalize(finalTBN * tangentNormal);
        }
    }
    
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, 0.0, 1.0);
    gEmissive        = vec4(emissive, 1.0);

    
}