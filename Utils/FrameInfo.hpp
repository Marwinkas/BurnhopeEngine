#pragma once
#include "../Render/Camera.hpp"
#include "DirectXMathCompat.hpp"
#include <vulkan/vulkan.h>
namespace burnhope
{
class BindlessRegistry;
    struct GlobalUbo
    { 
        float4x4 projection;
        float4x4 invViewProj;
        float4x4 view;
        float3 camPos{};
        float zNear{0.1f};
        float _uboPad0{};
        float _std140PadSunDir[3]{}; // std140: float3 sunDir @ 224
        float3 sunDir{0.0f, -1.0f, 0.0f};
        float zFar{1000.0f};
        float4 screenSize;
        float4x4 sunLightSpaceMatrices[4];
        float4 cascadeSplits;
        uint32_t gridDimX{16};
        uint32_t gridDimY{9};
        uint32_t gridDimZ{24};
        uint32_t portalID{0};
        float lightSize{20.0f};
        float _std140PadSunColor[3]{}; // std140: float3 sunColor @ 560
        float3 sunColor;
        float sunIntensity;

        // --- Post Process & Lighting Params ---
        // SSCS: x = enable, y = rayLength, z = steps, w = thickness
        float4 sscsParams;
        // GTAO: x = enable, y = radius, z = falloff, w = intensity
        float4 gtaoParams;
        // Fog Params: x = enable, y = density, z = heightFalloff, w = baseHeight
        float4 fogParams;
        // Fog Color: rgb = color, a = inscatterIntensity
        float4 fogColor;
        // Inscatter Color: rgb = color, a = inscatterPower
        float4 inscatterColor;
        // Sky Params
        float4 skyZenithColor;
        float4 skyHorizonColor;
        float4 skySunParams; // x: size, y: glow, z: glow size
        float4 ssgiParams;   // x: enable, y: rayCount, z: stepSize, w: thickness
        float4 rtParams;     // x: enableRTRefl, y: maxBounces, z: enableRC, w: unused

        // --- Mega Post Process Params ---
        float4x4 prevViewProj;
        float4 ppExposureParams; // x: autoExposure, y: manualExposure, z: minBrightness, w: maxBrightness
        float4 ppColorBalance;   // x: temperature, y: contrast, z: saturation, w: gamma
        float4 ppBloomParams;    // x: enable, y: threshold, z: intensity, w: blurRadius
        float4 ppDoFParams;      // x: enable, y: focusDist, z: focusRange, w: bokehSize
        float4 ppVignetteGrain;  // x: vignetteInt, y: grainInt, z: sharpenInt, w: caInt
        float4 ppMotionBlur;     // x: enable, y: strength, z: time, w: padding
        float4 ppLensFlare;      // x: enable, y: intensity, z: dispersal, w: ghosts
        float4 ppTAA_CAS;        // x: enableTAA, y: taaBlend, z: enableCAS, w: casSharpness
        float4 ppLensAdvanced;   // x: haloWidth, y: chromaticDir, z: autoFocus, w: tonemapper
        float4 ppDistortionDirt; // x: enableDistortion, y: distortionStrength, z: enableDirt, w: dirtIntensity
        float4 ppDitherAniso;    // x: enableDithering, y: ditherStrength, z: enableScreenRefraction, w: refractionStrength
        float4 cgShadows;
        float4 cgMidtones;
        float4 cgHighlights;
    float4 ppRetroParams; // x: enable, y: scanlines, z: glitch, w: vhsNoise
        float4 ppRetroParams2; // x: pixelation, y: jitterRes, zw: unused
        float4 ppStylizedParams; // x: posterizationLevels, y: kuwaharaRadius, z: celShadingLevels, w: unused
        float4 ppOutlineParams; // x: enable, y: thickness, z: depthThresh, w: normalThresh
        float4 ppOutlineColor;  // rgb: color, w: unused
        float4 ppOutlineJitter; // x: enable, y: speed, z: strength, w: unused
        float4 ppWeatherSSR;    // x: weather, y: wIntens, z: ssr, w: ssrSteps
        float4 ppSSSS;          // x: ssss, y: ssssWidth, z: ssrThick, w: vrsMode
        float4 ppWeatherParams; // x: speed, y: size, z: density, w: distortion

        float4 cgGlobalLift{0.f, 0.f, 0.f, 0.f};
        float4 cgGlobalGamma{1.f, 1.f, 1.f, 1.f};
        float4 cgGlobalGain{1.f, 1.f, 1.f, 1.f};
        float4 cgGlobalOffset{0.f, 0.f, 0.f, 0.f};
        float4 cgShadowsLift{0.f, 0.f, 0.f, 0.f};
        float4 cgShadowsGamma{1.f, 1.f, 1.f, 1.f};
        float4 cgShadowsGain{1.f, 1.f, 1.f, 1.f};
        float4 cgShadowsOffset{0.f, 0.f, 0.f, 0.f};
        float4 cgMidtonesLift{0.f, 0.f, 0.f, 0.f};
        float4 cgMidtonesGamma{1.f, 1.f, 1.f, 1.f};
        float4 cgMidtonesGain{1.f, 1.f, 1.f, 1.f};
        float4 cgMidtonesOffset{0.f, 0.f, 0.f, 0.f};
        float4 cgHighlightsLift{0.f, 0.f, 0.f, 0.f};
        float4 cgHighlightsGamma{1.f, 1.f, 1.f, 1.f};
        float4 cgHighlightsGain{1.f, 1.f, 1.f, 1.f};
        float4 cgHighlightsOffset{0.f, 0.f, 0.f, 0.f};
        float4 cgRgbMixerRed{1.f,0.f,0.f,0.f};
        float4 cgRgbMixerGreen{0.f,1.f,0.f,0.f};
        float4 cgRgbMixerBlue{0.f,0.f,1.f,0.f};
        float4 ppBlurs{0.f, 0.f, 0.f, 0.f};
        float4 ppBlurCenter{0.5f, 0.5f, 0.f, 0.f};  
        float4 ppColorFX{0.f, 0.f, 0.f, 0.f};
        float4 ppFilmDamage{0.f, 0.f, 0.f, 0.f};
        float4 ppEdgeDetect{0.f, 0.f, 0.f, 0.f};
        float4 ppEdgeDetect2{0.f, 0.f, 0.f, 0.f};
        float4 ppEdgeColor{1.f, 1.f, 1.f, 1.f};
        float4 ppEmboss{0.f, 0.f, 0.f, 0.f};
        float4 ppSketch{0.f, 0.f, 0.f, 0.f};
        float4 ppSketch2{0.f, 0.f, 0.f, 0.f};
        float4 ppHalftone{0.f, 0.f, 0.f, 0.f};
        float4 ppDitherData{0.f, 0.f, 0.f, 0.f};
        float4 ditherShadow{0.f, 0.f, 0.f, 0.f};
        float4 ditherMid{0.f, 0.f, 0.f, 0.f};
        float4 ditherHighlight{0.f, 0.f, 0.f, 0.f};
        float4 ppWarp{0.f, 0.f, 0.f, 0.f};
        float4 ppWarp2{0.f, 0.f, 0.f, 0.f};
        float4 ppColorComp{0.f, 0.f, 0.f, 0.f};
        float4 shadowRampColor1{0.f, 0.f, 0.2f, 1.f};
        float4 shadowRampColor2{0.8f, 0.1f, 0.1f, 1.f};
        float4 ppBleedMosh{0.f, 0.f, 0.f, 0.f};
        float4 ppAsciiSort{0.f, 0.f, 0.f, 0.f};
        float4 ppImpact{0.f, 0.f, 0.f, 0.f};
        float4 ppTrails{0.f, 0.f, 0.f, 0.f};
        float4 ppPixelSort{0.f, 0.f, 0.f, 0.f};
        float4 ppArtistic{0.f, 0.f, 0.f, 0.f};
        float4 ppArtisticColor{1.f, 1.f, 1.f, 1.f};
        
        float4 ppStylized3{0.f, 0.f, 0.f, 0.f};
        float4 ppStylized4{0.f, 0.f, 0.f, 0.f};
        float4 ppLens3{0.f, 0.f, 0.f, 0.f};
        float4 ppLens4{0.f, 0.f, 0.f, 0.f};
        float4 ppGlitch3{0.f, 0.f, 0.f, 0.f};
        float4 ppGlitch4{0.f, 0.f, 0.f, 0.f};
        float4 gbColor1{0.f, 0.f, 0.f, 0.f};
        float4 gbColor2{0.f, 0.f, 0.f, 0.f};
        float4 gbColor3{0.f, 0.f, 0.f, 0.f};
        float4 gbColor4{0.f, 0.f, 0.f, 0.f};
        
        float4 ppSpeedLines{0.f, 0.f, 0.f, 0.f};
        float4 ppColorSplash{0.f, 0.f, 0.f, 0.f};
        float4 ppHeatFrost{0.f, 0.f, 0.f, 0.f};
        float4 ppDropsEcho{0.f, 0.f, 0.f, 0.f};
        float4 ppCanvasInk{0.f, 0.f, 0.f, 0.f};
        float4 ppWorldGlitter{0.f, 0.f, 0.f, 0.f};
        float4 ppCausticsBreath{0.f, 0.f, 0.f, 0.f};
        float4 ppCausticsScale{0.f, 0.f, 0.f, 0.f};
        
        float4 ppTransAnime{0.f, 0.f, 0.f, 0.f};
        float4 ppAstigDolly{0.f, 0.f, 0.f, 0.f};
        float4 ppSaccBurn{0.f, 0.f, 0.f, 0.f};
        float4 ppPhosASCII{0.f, 0.f, 0.f, 0.f};
        float4 ppGravVector{0.f, 0.f, 0.f, 0.f};
        float4 ppKMeansFeed{0.f, 0.f, 0.f, 0.f};
        float4 ppHatchAnalog{0.f, 0.f, 0.f, 0.f};
        float4 ppMoireTunnel{0.f, 0.f, 0.f, 0.f};
        float4 ppAfterBleed{0.f, 0.f, 0.f, 0.f};
        float4 ppFluidCMYK{0.f, 0.f, 0.f, 0.f};
        float4 ppCondenDust{0.f, 0.f, 0.f, 0.f};
        float4 ppEctoRolling{0.f, 0.f, 0.f, 0.f};
        float4 ppPurkinjeSlit{0.f, 0.f, 0.f, 0.f};
        float4 ppReactDroste{0.f, 0.f, 0.f, 0.f};
       
       float4 ppPsych1{0.f, 0.f, 0.f, 0.f}; // x: Blink, y: Floaters, z: TimeStutter, w: HollowFace
       float4 ppPsych2{0.f, 0.f, 0.f, 0.f}; // x: Melting, y: AntiLight, z: Trypo, w: ParallaxEye
       float4 ppPsych3{0.f, 0.f, 0.f, 0.f}; // x: Smudges, y: Haunting, z: Nystagmus, w: Purkinje
       float4 ppPsych4{0.f, 0.f, 0.f, 0.f}; // x: RodCone, y: FluidLens, z: unused, w: unused
       float4 ppPsych5{0.f, 0.f, 0.f, 0.f};
       float4 ppPsych6{0.f, 0.f, 0.f, 0.f};
       float4 ppPsych7{0.f, 0.f, 0.f, 0.f};
       float4 ppPsych8{0.f, 0.f, 0.f, 0.f};
       float4 ppTexIndices{0.f, 0.f, 0.f, 0.f}; // x: VectorField Tex, y: Caustics Tex, z: Canvas Tex
    };

struct FrameInfo final {
  int frameIndex{0};
  float frameTime{0.0f};
  VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
  Camera& camera;
  uint32_t globalUboHeap{0};
  BindlessRegistry& bindless;
};

} // namespace burnhope
