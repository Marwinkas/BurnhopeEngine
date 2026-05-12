#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent; 
layout (location = 0) out vec3 outCrntPos;
layout (location = 1) out vec2 outTexCoord;
layout (location = 2) out mat3 outTBN;
layout (location = 5) flat out uint outMatID; 
layout(set = 0, binding = 0) uniform GlobalSceneUbo {
    mat4 projection; mat4 invViewProj; mat4 view; vec3 camPos; float zNear;
    vec3 sunDir; float zFar; vec4 screenSize; mat4 sunLightSpaceMatrices[4];
    vec4 cascadeSplits; uint gridDimX; uint gridDimY; uint gridDimZ; uint portalID; float lightSize;
    vec3 sunColor; float sunIntensity;
    vec4 sscsParams; vec4 gtaoParams; vec4 fogParams; vec4 fogColor; vec4 inscatterColor;
    vec4 skyZenithColor; vec4 skyHorizonColor; vec4 skySunParams;
    vec4 ssgiParams;
    vec4 rtParams;
    
    mat4 prevViewProj;
    vec4 ppExposureParams;
    vec4 ppColorBalance;
    vec4 ppBloomParams;
    vec4 ppDoFParams;
    vec4 ppVignetteGrain;
    vec4 ppMotionBlur;
    vec4 ppLensFlare;
    vec4 ppTAA_CAS;       // ДОБАВЛЕНО
    vec4 ppLensAdvanced;  // x: halo, y: caDir, z: autoFocus, w: tonemapper
    vec4 ppDistortionDirt;
    vec4 ppDitherAniso;
    vec4 cgShadows;
    vec4 cgMidtones;
    vec4 cgHighlights;
    vec4 ppRetroParams;
    vec4 ppRetroParams2;
    vec4 ppStylizedParams;
    vec4 ppOutlineParams;
    vec4 ppOutlineColor;
    vec4 ppOutlineJitter;
       vec4 ppWeatherSSR;
    vec4 ppSSSS;
} ubo;
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint pad0;
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t pad1; // ДОБАВИТЬ
};
layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;
void main() {
    uint globalIndex = gl_InstanceIndex; 
ObjectData obj = objectBuffer.objects[globalIndex];
    mat4 modelMatrix = obj.modelMatrix;
    outMatID = obj.materialID;
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    outCrntPos = worldPos.xyz;
    outTexCoord = aTex;
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent);
    T = normalize(T - dot(T, N) * N); 
    vec3 B = cross(N, T);
    outTBN = mat3(T, B, N);
    gl_Position = ubo.projection * ubo.view * worldPos;
    
    // PS1 Affine Texture Mapping (Vertex Wobble/Jitter)
    if (ubo.ppRetroParams2.y > 0.0) {
        float res = ubo.ppRetroParams2.y;
        // Привязываем координаты к целочисленной сетке перед перспективным делением
        gl_Position.xyz = gl_Position.xyz / gl_Position.w;
        gl_Position.xy = floor(gl_Position.xy * res) / res;
        gl_Position.xyz *= gl_Position.w;
    }
}