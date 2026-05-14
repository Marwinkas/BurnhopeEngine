#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : enable 

struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint64_t posBufferAddress;
    uint64_t attrBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    vec4 aabbMin;
    vec4 aabbMax;
};

layout (location = 0) in vec4 aPosAABB;
layout (location = 1) in vec2 aTex;
layout (location = 2) in vec4 aQTangent;

layout (location = 0) out vec3 outCrntPos;
layout (location = 1) out vec2 outTexCoord;
layout (location = 2) out mat3 outTBN;
layout (location = 5) flat out uint outMatID; 
layout (location = 6) out vec4 outColor;
layout (location = 7) out vec2 outUV2;
layout (location = 8) out float outThickness;

struct GlobalUboStruct {
    mat4 projection; mat4 invViewProj; mat4 view; vec3 camPos; float zNear;
    vec3 sunDir; float zFar; vec4 screenSize; mat4 sunLightSpaceMatrices[4];
    vec4 cascadeSplits; uint gridDimX; uint gridDimY; uint gridDimZ; uint portalID; float lightSize;
    vec3 sunColor; float sunIntensity;
    vec4 sscsParams; vec4 gtaoParams; vec4 fogParams; vec4 fogColor; vec4 inscatterColor;
    vec4 skyZenithColor; vec4 skyHorizonColor; vec4 skySunParams;
    vec4 ssgiParams; vec4 rtParams; mat4 prevViewProj;
    vec4 ppExposureParams; vec4 ppColorBalance; vec4 ppBloomParams; vec4 ppDoFParams; 
    vec4 ppVignetteGrain; vec4 ppMotionBlur; vec4 ppLensFlare; vec4 ppTAA_CAS;
    vec4 ppLensAdvanced; vec4 ppDistortionDirt; vec4 ppDitherAniso; vec4 cgShadows;
    vec4 cgMidtones; vec4 cgHighlights; vec4 ppRetroParams; vec4 ppRetroParams2;
    vec4 ppStylizedParams; vec4 ppOutlineParams; vec4 ppOutlineColor; vec4 ppOutlineJitter;
    vec4 ppWeatherSSR; vec4 ppSSSS; vec4 ppWeatherParams; vec4 cgGlobalLift;
    vec4 cgGlobalGamma; vec4 cgGlobalGain; vec4 cgGlobalOffset; vec4 cgShadowsLift;
    vec4 cgShadowsGamma; vec4 cgShadowsGain; vec4 cgShadowsOffset; vec4 cgMidtonesLift;
    vec4 cgMidtonesGamma; vec4 cgMidtonesGain; vec4 cgMidtonesOffset; vec4 cgHighlightsLift;
    vec4 cgHighlightsGamma; vec4 cgHighlightsGain; vec4 cgHighlightsOffset; vec4 cgRgbMixerRed;
    vec4 cgRgbMixerGreen; vec4 cgRgbMixerBlue; vec4 ppBlurs; vec4 ppBlurCenter;
    vec4 ppColorFX; vec4 ppFilmDamage; vec4 ppEdgeDetect; vec4 ppEdgeDetect2;
    vec4 ppEdgeColor; vec4 ppEmboss; vec4 ppSketch; vec4 ppSketch2; vec4 ppHalftone; 
    vec4 ppDitherData; vec4 ditherShadow; vec4 ditherMid; vec4 ditherHighlight;
    vec4 ppWarp; vec4 ppWarp2; vec4 ppColorComp; vec4 shadowRampColor1; vec4 shadowRampColor2;
    vec4 ppBleedMosh; vec4 ppAsciiSort; vec4 ppImpact; vec4 ppTrails; vec4 ppPixelSort;
    vec4 ppArtistic; vec4 ppArtisticColor; vec4 ppStylized3; vec4 ppStylized4;
    vec4 ppLens3; vec4 ppLens4; vec4 ppGlitch3; vec4 ppGlitch4; vec4 gbColor1;
    vec4 gbColor2; vec4 gbColor3; vec4 gbColor4; vec4 ppSpeedLines; vec4 ppColorSplash;
    vec4 ppHeatFrost; vec4 ppDropsEcho; vec4 ppCanvasInk; vec4 ppWorldGlitter;
    vec4 ppCausticsBreath; vec4 ppCausticsScale; vec4 ppTransAnime; vec4 ppAstigDolly;
    vec4 ppSaccBurn; vec4 ppPhosASCII; vec4 ppGravVector; vec4 ppKMeansFeed;
    vec4 ppHatchAnalog; vec4 ppMoireTunnel; vec4 ppAfterBleed; vec4 ppFluidCMYK;
    vec4 ppCondenDust; vec4 ppEctoRolling; vec4 ppPurkinjeSlit; vec4 ppReactDroste; 
    vec4 ppPsych1; vec4 ppPsych2; vec4 ppPsych3; vec4 ppPsych4; vec4 ppPsych5; 
    vec4 ppPsych6; vec4 ppPsych7; vec4 ppPsych8; vec4 ppTexIndices;
};

layout(set = 0, binding = 0) uniform GlobalSceneUbo {
    GlobalUboStruct ubo;
};






layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

layout(std430, set = 1, binding = 3) readonly buffer BoneMatricesBuffer {
    mat4 boneMatrices[];
};

struct PackedVertexAnim {
    uint16_t pivotX, pivotY, pivotZ, cloth_ao;
    uint boneIndices;
    uint boneWeights;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer ColorBuffer { uint c[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer UV2Buffer { uint u[]; };
layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer AnimBuffer { PackedVertexAnim a[]; };

void main() {
    uint globalIndex = gl_InstanceIndex; 
    ObjectData obj = objectBuffer.objects[globalIndex];
    mat4 modelMatrix = obj.modelMatrix;
    outMatID = obj.materialID;
    vec3 extent = obj.aabbMax.xyz - obj.aabbMin.xyz;
    vec3 localPos = obj.aabbMin.xyz + aPosAABB.xyz * extent;
    
    // Распаковка ветра и толщины из pad (w компонента)
    uint pad = uint(aPosAABB.w * 65535.0 + 0.5);
    float thickness = float((pad >> 8) & 0xFF) / 255.0;
    float windW = float(pad & 0xFF) / 255.0;
    outThickness = thickness;
    
    // Анимация ветра (Wind Sway)
    if (windW > 0.0) {
        float time = ubo.ppMotionBlur.z * 1.5;
        vec3 sway = vec3(sin(time + localPos.x), 0.0, cos(time + localPos.z)) * windW * 0.15;
        localPos += sway;
    }
    
   mat4 skinMat = mat4(1.0); 
    if (obj.boneOffset != 0xFFFFFFFF && obj.animBufferAddress != 0) {
        AnimBuffer anBuf = AnimBuffer(obj.animBufferAddress);
        PackedVertexAnim vtxAn = anBuf.a[gl_VertexIndex];
        uint bInd = vtxAn.boneIndices;
        uint bWgh = vtxAn.boneWeights;
        
        float w0 = float((bWgh >> 0) & 0xFF) / 255.0;
        float w1 = float((bWgh >> 8) & 0xFF) / 255.0;
        float w2 = float((bWgh >> 16) & 0xFF) / 255.0;
        float w3 = float((bWgh >> 24) & 0xFF) / 255.0;
          skinMat = mat4(0.0); // Reset for summation
        if (w0 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 0) & 0xFF)] * w0;
        if (w1 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 8) & 0xFF)] * w1;
        if (w2 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 16) & 0xFF)] * w2;
        if (w3 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 24) & 0xFF)] * w3;
    }

    vec4 worldPos = obj.boneOffset != 0xFFFFFFFF ? (modelMatrix * skinMat * vec4(localPos, 1.0)) : (modelMatrix * vec4(localPos, 1.0));
    outCrntPos = worldPos.xyz;
    outTexCoord = aTex;
    
    // Чтение Color и UV2 по Device Address
    uint globalVtxIdx = gl_VertexIndex;
    vec4 vColor = vec4(1.0);
    if (obj.colorBufferAddress != 0) {
        ColorBuffer cb = ColorBuffer(obj.colorBufferAddress);
        vColor = unpackUnorm4x8(cb.c[globalVtxIdx]);
    }
    outColor = vColor;

    vec2 vUV2 = vec2(0.0);
    if (obj.uv2BufferAddress != 0) {
        UV2Buffer u2b = UV2Buffer(obj.uv2BufferAddress);
        vUV2 = unpackHalf2x16(u2b.u[globalVtxIdx]);
    }
    outUV2 = vUV2;

    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    float qx = aQTangent.x; float qy = aQTangent.y; float qz = aQTangent.z;
    float qw = sqrt(max(0.0, 1.0 - qx*qx - qy*qy - qz*qz));
    vec3 T = vec3(1.0 - 2.0*qy*qy - 2.0*qz*qz, 2.0*qx*qy + 2.0*qz*qw, 2.0*qx*qz - 2.0*qy*qw);
    vec3 N = vec3(2.0*qx*qz + 2.0*qy*qw, 2.0*qy*qz - 2.0*qx*qw, 1.0 - 2.0*qx*qx - 2.0*qy*qy);
    float signT = aQTangent.w < 0.0 ? -1.0 : 1.0;
    vec3 B = cross(N, T) * signT;
    
    if (obj.boneOffset != 0xFFFFFFFF) {
        mat3 skinMat3 = mat3(skinMat);
        normalMatrix = transpose(inverse(mat3(modelMatrix) * skinMat3));
    }

    T = normalize(normalMatrix * T);
    B = normalize(normalMatrix * B);
    N = normalize(normalMatrix * N);
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
    
    // PS1 Affine Texture Mapping
    if (ubo.ppRetroParams2.y > 0.0) {
        float res = ubo.ppRetroParams2.y;
        gl_Position.xyz = gl_Position.xyz / gl_Position.w;
        gl_Position.xy = floor(gl_Position.xy * res) / res;
        gl_Position.xyz *= gl_Position.w;
    }
}
