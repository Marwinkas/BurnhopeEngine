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
    vec4 ppWeatherParams;

    vec4 cgGlobalLift;
    vec4 cgGlobalGamma;
    vec4 cgGlobalGain;
    vec4 cgGlobalOffset;
    vec4 cgShadowsLift;
    vec4 cgShadowsGamma;
    vec4 cgShadowsGain;
    vec4 cgShadowsOffset;
    vec4 cgMidtonesLift;
    vec4 cgMidtonesGamma;
    vec4 cgMidtonesGain;
    vec4 cgMidtonesOffset;
    vec4 cgHighlightsLift;
    vec4 cgHighlightsGamma;
    vec4 cgHighlightsGain;
    vec4 cgHighlightsOffset;
    vec4 cgRgbMixerRed;
    vec4 cgRgbMixerGreen;
    vec4 cgRgbMixerBlue;
    vec4 ppBlurs;
    vec4 ppBlurCenter;
    vec4 ppColorFX;
    vec4 ppFilmDamage;
    vec4 ppEdgeDetect;
    vec4 ppEdgeDetect2;
    vec4 ppEdgeColor;
    vec4 ppEmboss;
    vec4 ppSketch;
    vec4 ppSketch2;
    vec4 ppHalftone; 
    vec4 ppDitherData;
    vec4 ditherShadow;
    vec4 ditherMid;
    vec4 ditherHighlight;
    vec4 ppWarp;
    vec4 ppWarp2;
    vec4 ppColorComp;
    vec4 shadowRampColor1;
    vec4 shadowRampColor2;
    vec4 ppBleedMosh;
    vec4 ppAsciiSort;
    vec4 ppImpact;
    vec4 ppTrails;
    vec4 ppPixelSort;
    vec4 ppArtistic;
    vec4 ppArtisticColor;
    vec4 ppStylized3;
    vec4 ppStylized4;
    vec4 ppLens3;
    vec4 ppLens4;
    vec4 ppGlitch3;
    vec4 ppGlitch4;
    vec4 gbColor1;
    vec4 gbColor2;
    vec4 gbColor3;
    vec4 gbColor4;
    vec4 ppSpeedLines;
    vec4 ppColorSplash;
    vec4 ppHeatFrost;
    vec4 ppDropsEcho;
    vec4 ppCanvasInk;
    vec4 ppWorldGlitter;
    vec4 ppCausticsBreath;
    vec4 ppCausticsScale; 
    vec4 ppTransAnime;
    vec4 ppAstigDolly;
    vec4 ppSaccBurn;
    vec4 ppPhosASCII;
    vec4 ppGravVector;
    vec4 ppKMeansFeed;
    vec4 ppHatchAnalog;
    vec4 ppMoireTunnel;
    vec4 ppAfterBleed;
    vec4 ppFluidCMYK;
    vec4 ppCondenDust;
    vec4 ppEctoRolling;
    vec4 ppPurkinjeSlit;
    vec4 ppReactDroste; 
    vec4 ppPsych1; 
    vec4 ppPsych2; 
    vec4 ppPsych3; 
    vec4 ppPsych4; 
       vec4 ppPsych5; 
       vec4 ppPsych6; 
       vec4 ppPsych7; 
       vec4 ppPsych8; 
       vec4 ppTexIndices;
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
    
    // Environment Breathing
    if (ubo.ppCausticsBreath.y > 0.5) {
        float time = ubo.ppMotionBlur.z * ubo.ppCausticsBreath.w;
        float amp = ubo.ppCausticsBreath.z;
        float wave = sin(worldPos.x * 2.0 + time) * cos(worldPos.z * 2.0 + time);
        worldPos.xyz += N * wave * amp;
    }
    
    gl_Position = ubo.projection * ubo.view * worldPos;
    
    // World Curvature
    if (ubo.ppCanvasInk.y > 0.5) {
        float curve = ubo.ppCanvasInk.z;
        float distSq = dot(gl_Position.xz, gl_Position.xz);
        gl_Position.y -= distSq * curve;
    }

    if (ubo.ppWarp.w > 0.5) {
        float time = ubo.ppMotionBlur.z * ubo.ppWarp2.y;
        float scale = ubo.ppWarp2.z;
        float noise = fract(sin(dot(worldPos.xyz * scale, vec3(12.9898, 78.233, 45.164)) + time) * 43758.5453);
        worldPos.xyz += N * (noise - 0.5) * ubo.ppWarp2.x;
        gl_Position = ubo.projection * ubo.view * worldPos;
    }
    
    // PS1 Affine Texture Mapping (Vertex Wobble/Jitter)
    if (ubo.ppRetroParams2.y > 0.0) {
        float res = ubo.ppRetroParams2.y;
        // Привязываем координаты к целочисленной сетке перед перспективным делением
        gl_Position.xyz = gl_Position.xyz / gl_Position.w;
        gl_Position.xy = floor(gl_Position.xy * res) / res;
        gl_Position.xyz *= gl_Position.w;
    }
}