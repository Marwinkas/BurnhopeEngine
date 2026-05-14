#version 450
#extension GL_EXT_nonuniform_qualifier : require 

layout (location = 0) out vec4 gNormalRoughness;
layout (location = 1) out vec4 gAlbedoMetallic;
layout (location = 2) out vec4 gHeightAO; 
layout (location = 3) out vec4 gEmissive;
layout (location = 4) out uint o_PortalID;

layout (location = 0) in vec3 inCrntPos;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in mat3 inTBN;
layout (location = 5) flat in uint inMatID;
layout (location = 6) in vec4 inColor;
layout (location = 7) in vec2 inUV2;
layout (location = 8) in float inThickness;

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

struct MaterialData {
    int albedoAlphaIdx; int normalIdx; int ormxIdx; int emissiveIdx;
    int useTriplanar; int isTransparent; int repeatTexture; int pad1;
    vec2 uvScale; float triplanarScale; float emissiveIntensity;
    vec4 albedoColor;
    vec4 emissiveColor;
    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;
    float aoStrength;
    float pad2; float pad3; float pad4;
};

layout(std430, set = 1, binding = 1) readonly buffer MaterialBlock {
    MaterialData materials[];
} matBuffer;

layout(set = 2, binding = 0) uniform sampler2D allTextures[];

vec4 sampleMatTex(int texIdx, vec2 uv, vec2 dx, vec2 dy, int useTriplanar, vec3 pos, vec3 posDdx, vec3 posDdy, vec3 blend, float scale, int repeat) {
    if (useTriplanar == 1) {
        vec2 uvX = pos.zy * scale; vec2 uvY = pos.xz * scale; vec2 uvZ = pos.xy * scale;
        vec2 dxX = posDdx.zy * scale; vec2 dyX = posDdy.zy * scale;
        vec2 dxY = posDdx.xz * scale; vec2 dyY = posDdy.xz * scale;
        vec2 dxZ = posDdx.xy * scale; vec2 dyZ = posDdy.xy * scale;

        if (repeat == 0) {
            uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0);
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
    
    // --- Melting / Creeping Walls ---
    if (ubo.ppPsych2.x > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / ubo.screenSize.xy;
        float distFromCenter = distance(screenUV, vec2(0.5)) * 2.0; 
        float threshold = ubo.ppPsych5.x;
        float meltFactor = 1.0;
        if (threshold < 0.99) {
            meltFactor = smoothstep(1.0 - threshold, 1.0 - threshold + 0.2, distFromCenter);
        }

        if (meltFactor > 0.001) {
            float time = ubo.ppMotionBlur.z;
            float speed = ubo.ppPsych2.x;
            float noiseScale = max(ubo.ppPsych5.y, 0.1);
            
            float s = sin(inTexCoord.x * noiseScale * 15.0) * cos(inTexCoord.x * noiseScale * 5.0 + time * 0.5);
            float stripSpeed = mix(0.2, 1.0, smoothstep(-1.0, 1.0, s)); 
            
            scaledUV.y -= speed * time * stripSpeed * meltFactor; 
            scaledUV.x += sin(inTexCoord.y * noiseScale * 10.0 + time) * 0.01 * meltFactor * stripSpeed;
        }
    }

    vec3 posDdx = dFdx(inCrntPos);
    vec3 posDdy = dFdy(inCrntPos);
    vec2 finalUV = mat.repeatTexture == 1 ? scaledUV : clamp(scaledUV, 0.0, 1.0);
    
    vec3 N = normalize(inTBN[2]);
    vec3 T = normalize(inTBN[0]);
    vec3 dp = T - dot(T, N) * N;
    if (dot(dp, dp) > 0.0001) T = normalize(dp);
    else T = normalize(cross(N, abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
    
    float signT = dot(cross(N, T), normalize(inTBN[1])) < 0.0 ? -1.0 : 1.0;
    vec3 B = cross(N, T) * signT;
    mat3 finalTBN = mat3(T, B, N);

    vec3 blendWeights = abs(N);
    blendWeights = max(blendWeights - 0.2, 0.0);
    blendWeights /= max(dot(blendWeights, vec3(1.0)), 0.00001); 

    float height    = 0.0;
    vec3  albedo    = mat.albedoColor.rgb * inColor.rgb; // Применяем Vertex Color!
    float alpha     = mat.albedoColor.a * inColor.a;     // Применяем Vertex Alpha!
    vec3  emissive  = mat.emissiveColor.rgb * mat.emissiveIntensity;
    float metallic  = mat.metallicStrength;
    float roughness = mat.roughnessStrength; 
    float ao        = mat.aoStrength;        
    vec3  worldNormal = N;
    
    // --- Hollow-Face Illusion ---
    if (ubo.ppPsych1.w > 0.0) {
        worldNormal = -worldNormal;
        finalTBN[2] = -finalTBN[2];
        mat.heightStrength = -mat.heightStrength * ubo.ppPsych5.z; 
    }

    // ORMX MAP
    if (mat.ormxIdx >= 0) {
        vec4 ormx = sampleMatTex(mat.ormxIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture);
        ao        *= ormx.r; 
        roughness *= ormx.g;
        metallic  *= ormx.b;
        height     = ormx.a;
        
        if (mat.useTriplanar == 0 && mat.heightStrength > 0.0) {
            vec3 viewDirWorld   = normalize(ubo.camPos - inCrntPos);
            vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
            vec2 p = viewDirTangent.xy / max(viewDirTangent.z, 0.1);
            finalUV = scaledUV - p * (height * 0.02 * mat.heightStrength);
            if (mat.repeatTexture == 0) finalUV = clamp(finalUV, 0.0, 1.0);
            ormx = textureGrad(allTextures[nonuniformEXT(mat.ormxIdx)], finalUV, dx, dy);
            ao *= ormx.r; roughness *= ormx.g; metallic *= ormx.b; height = ormx.a;
        }
    }
    
    // Albedo
    if (mat.albedoAlphaIdx >= 0) {
        vec4 texColor = sampleMatTex(mat.albedoAlphaIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture);
        albedo *= texColor.rgb;
        alpha *= texColor.a;
    }

    // Screen-Door Dithering
    if (mat.isTransparent == 1) {
        if (alpha <= 0.01) discard; 
        int x = int(gl_FragCoord.x) % 4;
        int y = int(gl_FragCoord.y) % 4;
        const float bayer[16] = float[](
            0.0625, 0.5625, 0.1875, 0.6875, 0.8125, 0.3125, 0.9375, 0.4375,
            0.2500, 0.7500, 0.1250, 0.6250, 1.0000, 0.5000, 0.8750, 0.3750
        );
        if (alpha < bayer[y * 4 + x]) discard;
    } else {
        if (alpha < 0.5) discard;
    }
    
    // Emissive
    if (mat.emissiveIdx >= 0) {
        emissive = sampleMatTex(mat.emissiveIdx, finalUV, dx, dy, mat.useTriplanar, inCrntPos, posDdx, posDdy, blendWeights, mat.triplanarScale, mat.repeatTexture).rgb * mat.emissiveColor.rgb * mat.emissiveIntensity;
    }
    
    // Normal Map
    if (mat.normalIdx >= 0) {
        if (mat.useTriplanar == 1) {
            vec2 uvX = inCrntPos.zy * mat.triplanarScale; vec2 uvY = inCrntPos.xz * mat.triplanarScale; vec2 uvZ = inCrntPos.xy * mat.triplanarScale;
            vec2 dxX = posDdx.zy * mat.triplanarScale; vec2 dyX = posDdy.zy * mat.triplanarScale;
            vec2 dxY = posDdx.xz * mat.triplanarScale; vec2 dyY = posDdy.xz * mat.triplanarScale;
            vec2 dxZ = posDdx.xy * mat.triplanarScale; vec2 dyZ = posDdy.xy * mat.triplanarScale;
            
            if (mat.repeatTexture == 0) { uvX = clamp(uvX, 0.0, 1.0); uvY = clamp(uvY, 0.0, 1.0); uvZ = clamp(uvZ, 0.0, 1.0); }
            
            vec3 rgbX = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvX, dxX, dyX).rgb;
            vec3 rgbY = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvY, dxY, dyY).rgb;
            vec3 rgbZ = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], uvZ, dxZ, dyZ).rgb;
            
            vec2 tX_xy = rgbX.xy * 2.0 - 1.0; tX_xy *= mat.normalStrength;
            vec3 tX = vec3(tX_xy, sqrt(max(1.0 - dot(tX_xy, tX_xy), 0.0)));
            vec2 tY_xy = rgbY.xy * 2.0 - 1.0; tY_xy *= mat.normalStrength;
            vec3 tY = vec3(tY_xy, sqrt(max(1.0 - dot(tY_xy, tY_xy), 0.0)));
            vec2 tZ_xy = rgbZ.xy * 2.0 - 1.0; tZ_xy *= mat.normalStrength;
            vec3 tZ = vec3(tZ_xy, sqrt(max(1.0 - dot(tZ_xy, tZ_xy), 0.0)));
            
            vec3 nX = vec3(tX.z * sign(N.x), tX.y, -tX.x);
            vec3 nY = vec3(tY.x, tY.z * sign(N.y), -tY.y);
            vec3 nZ = vec3(tZ.x, tZ.y, tZ.z * sign(N.z));

            worldNormal = normalize(nX * blendWeights.x + nY * blendWeights.y + nZ * blendWeights.z);
        } else {
            vec3 rgb = textureGrad(allTextures[nonuniformEXT(mat.normalIdx)], finalUV, dx, dy).rgb;
            vec2 xy = rgb.xy * 2.0 - 1.0; xy *= mat.normalStrength;
            vec3 tangentNormal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
            worldNormal = normalize(finalTBN * tangentNormal);
        }
    }
    
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    
    float lightMask = 0.5; float rimMask = 1.0; 
    
    // --- Micro-trypophobia ---
    if (ubo.ppPsych2.z > 0.0) {
        float scale = ubo.ppPsych2.z;
        vec2 cell = fract(finalUV * scale) - 0.5;
        float dist = length(cell);
        if (dist < 0.3) {
            height -= 0.5 * mat.heightStrength; 
            vec3 holeNormal = normalize(vec3(-cell * 5.0, 1.0));
            worldNormal = normalize(finalTBN * holeNormal);
            albedo *= 0.2; 
        }
    }

    // --- Depth Parallax Anomalies ---
    if (ubo.ppPsych2.w > 0.0) {
        vec3 viewDirWorld   = normalize(ubo.camPos - inCrntPos);
        vec3 viewDirTangent = normalize(transpose(finalTBN) * viewDirWorld);
        vec2 p = viewDirTangent.xy / max(viewDirTangent.z, 0.1);
        vec2 deepUV = finalUV - p * 0.1; 
        
        float eyeShape = length(fract(deepUV * 5.0) - 0.5);
        if (eyeShape < 0.15) {
            albedo = mix(albedo, vec3(0.9, 0.1, 0.1), 0.8); 
            emissive += vec3(0.8, 0.0, 0.0);
        }
    }

    gNormalRoughness = vec4(worldNormal, roughness);
    gAlbedoMetallic  = vec4(albedo, metallic);
    gHeightAO        = vec4(height, ao, lightMask, rimMask);
    gEmissive        = vec4(emissive, 1.0);
    o_PortalID = ubo.portalID;
}
