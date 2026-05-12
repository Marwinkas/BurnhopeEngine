#pragma once
#include "../Render/Camera.hpp"
#include "Descriptors.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
namespace burnhope
{
    struct GlobalUbo
    { 
        glm::mat4 projection{1.f};
        glm::mat4 invViewProj{1.f};
        glm::mat4 view{1.f};
        alignas(16) glm::vec3 camPos;
        float zNear{0.1f};
        alignas(16) glm::vec3 sunDir{0.0f, -1.0f, 0.0f};
        float zFar{1000.0f};
        glm::vec4 screenSize;
        glm::mat4 sunLightSpaceMatrices[4];
        glm::vec4 cascadeSplits;
        uint32_t gridDimX{16};
        uint32_t gridDimY{9};
        uint32_t gridDimZ{24};
        uint32_t portalID{0};
        float lightSize{20.0f};

        alignas(16) glm::vec3 sunColor;     // Цвет главного направленного света
        float sunIntensity;     // Его интенсивность

        // --- Post Process & Lighting Params ---
        // SSCS: x = enable, y = rayLength, z = steps, w = thickness
        glm::vec4 sscsParams;
        // GTAO: x = enable, y = radius, z = falloff, w = intensity
        glm::vec4 gtaoParams;
        // Fog Params: x = enable, y = density, z = heightFalloff, w = baseHeight
        glm::vec4 fogParams;
        // Fog Color: rgb = color, a = inscatterIntensity
        glm::vec4 fogColor;
        // Inscatter Color: rgb = color, a = inscatterPower
        glm::vec4 inscatterColor;
        // Sky Params
        glm::vec4 skyZenithColor;
        glm::vec4 skyHorizonColor;
        glm::vec4 skySunParams; // x: size, y: glow, z: glow size
        glm::vec4 ssgiParams;   // x: enable, y: rayCount, z: stepSize, w: thickness
        glm::vec4 rtParams;     // x: enableRTRefl, y: maxBounces, z: enableRC, w: unused

        // --- Mega Post Process Params ---
        glm::mat4 prevViewProj;
        glm::vec4 ppExposureParams; // x: autoExposure, y: manualExposure, z: minBrightness, w: maxBrightness
        glm::vec4 ppColorBalance;   // x: temperature, y: contrast, z: saturation, w: gamma
        glm::vec4 ppBloomParams;    // x: enable, y: threshold, z: intensity, w: blurRadius
        glm::vec4 ppDoFParams;      // x: enable, y: focusDist, z: focusRange, w: bokehSize
        glm::vec4 ppVignetteGrain;  // x: vignetteInt, y: grainInt, z: sharpenInt, w: caInt
        glm::vec4 ppMotionBlur;     // x: enable, y: strength, z: time, w: padding
        glm::vec4 ppLensFlare;      // x: enable, y: intensity, z: dispersal, w: ghosts
        glm::vec4 ppTAA_CAS;        // x: enableTAA, y: taaBlend, z: enableCAS, w: casSharpness
        glm::vec4 ppLensAdvanced;   // x: haloWidth, y: chromaticDir, z: autoFocus, w: tonemapper
        glm::vec4 ppDistortionDirt; // x: enableDistortion, y: distortionStrength, z: enableDirt, w: dirtIntensity
        glm::vec4 ppDitherAniso;    // x: enableDithering, y: ditherStrength, z: enableScreenRefraction, w: refractionStrength
        glm::vec4 cgShadows;
        glm::vec4 cgMidtones;
        glm::vec4 cgHighlights;
    glm::vec4 ppRetroParams; // x: enable, y: scanlines, z: glitch, w: vhsNoise
        glm::vec4 ppRetroParams2; // x: pixelation, y: jitterRes, zw: unused
        glm::vec4 ppStylizedParams; // x: posterizationLevels, y: kuwaharaRadius, z: celShadingLevels, w: unused
        glm::vec4 ppOutlineParams; // x: enable, y: thickness, z: depthThresh, w: normalThresh
        glm::vec4 ppOutlineColor;  // rgb: color, w: unused
        glm::vec4 ppOutlineJitter; // x: enable, y: speed, z: strength, w: unused
        glm::vec4 ppWeatherSSR;    // x: weather, y: wIntens, z: ssr, w: ssrSteps
        glm::vec4 ppSSSS;          // x: ssss, y: ssssWidth, z: ssrThick, w: vrsMode
        glm::vec4 ppWeatherParams; // x: speed, y: size, z: density, w: distortion

        glm::vec4 cgGlobalLift{0.f};
        glm::vec4 cgGlobalGamma{1.f};
        glm::vec4 cgGlobalGain{1.f};
        glm::vec4 cgGlobalOffset{0.f};
        glm::vec4 cgShadowsLift{0.f};
        glm::vec4 cgShadowsGamma{1.f};
        glm::vec4 cgShadowsGain{1.f};
        glm::vec4 cgShadowsOffset{0.f};
        glm::vec4 cgMidtonesLift{0.f};
        glm::vec4 cgMidtonesGamma{1.f};
        glm::vec4 cgMidtonesGain{1.f};
        glm::vec4 cgMidtonesOffset{0.f};
        glm::vec4 cgHighlightsLift{0.f};
        glm::vec4 cgHighlightsGamma{1.f};
        glm::vec4 cgHighlightsGain{1.f};
        glm::vec4 cgHighlightsOffset{0.f};
        glm::vec4 cgRgbMixerRed{1.f,0.f,0.f,0.f};
        glm::vec4 cgRgbMixerGreen{0.f,1.f,0.f,0.f};
        glm::vec4 cgRgbMixerBlue{0.f,0.f,1.f,0.f};
        glm::vec4 ppBlurs{0.f};       
        glm::vec4 ppBlurCenter{0.5f, 0.5f, 0.f, 0.f};  
        glm::vec4 ppColorFX{0.f};     
        glm::vec4 ppFilmDamage{0.f};  
        glm::vec4 ppEdgeDetect{0.f};
        glm::vec4 ppEdgeDetect2{0.f};
        glm::vec4 ppEdgeColor{1.f};
        glm::vec4 ppEmboss{0.f};
        glm::vec4 ppSketch{0.f};
        glm::vec4 ppSketch2{0.f};
        glm::vec4 ppHalftone{0.f}; 
        glm::vec4 ppDitherData{0.f};
        glm::vec4 ditherShadow{0.f};
        glm::vec4 ditherMid{0.f};
        glm::vec4 ditherHighlight{0.f};
        glm::vec4 ppWarp{0.f};
        glm::vec4 ppWarp2{0.f};
        glm::vec4 ppColorComp{0.f};
        glm::vec4 shadowRampColor1{0.f, 0.f, 0.2f, 1.f};
        glm::vec4 shadowRampColor2{0.8f, 0.1f, 0.1f, 1.f};
    };
    struct FrameInfo
    {
        int frameIndex;
        float frameTime;
        VkCommandBuffer commandBuffer;
        Camera &camera;
        VkDescriptorSet globalDescriptorSet;
        BurnhopeDescriptorPool &frameDescriptorPool;
    };
}