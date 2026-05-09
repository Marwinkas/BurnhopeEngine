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
        float lightSize{20.0f};

        glm::vec3 sunColor;     // Цвет главного направленного света
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