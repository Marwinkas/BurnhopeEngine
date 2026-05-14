#pragma once
#include <entt/entt.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "Components.hpp"

namespace burnhope {
    namespace fs = std::filesystem;

    struct RenderSettings {
        int rtMaxBounces = 1;
        bool enableRTReflections = true;
        bool enableRadianceCascades = true;
        int rcProbeGridX = 16, rcProbeGridY = 9, rcProbeGridZ = 24, rcOctaSize = 8;
        float rcBaseRayLength = 1.0f;

        bool enableSSAO = true;
        float ssaoRadius = 0.5f, ssaoBias = 0.025f, ssaoIntensity = 2.0f, ssaoPower = 2.0f;
        
        bool enableSSGI = false;
        int ssgiRayCount = 8, blurRange = 4;
        float ssgiStepSize = 0.4f, ssgiThickness = 0.5f;

        bool autoExposure = true;
        float manualExposure = 1.0f, exposureCompensation = 1.0f, minBrightness = 0.5f, maxBrightness = 3.0f;
        float contrast = 1.0f, saturation = 1.0f, gamma = 2.2f, temperature = 8000.0f;

        bool enableLensDistortion = false;
        float lensDistortionStrength = 0.5f;
        bool enableLensDirt = false;
        float lensDirtIntensity = 1.0f;
        bool enableScreenRefraction = false;
        float refractionStrength = 0.05f;
        float refractionSpeed = 1.0f;
        bool enableDithering = true;
        float ditherStrength = 1.0f;
        
        bool enableRetroCRT = false;
        float crtScanlines = 1.0f;
        float glitchIntensity = 1.0f;
        float vhsNoise = 1.0f;
        int pixelation = 1;
        bool enableVertexJitter = false;
        float vertexJitterResolution = 240.0f;
        bool enablePosterization = false;
        float posterizationLevels = 8.0f;
        bool enableKuwahara = false;
        int kuwaharaRadius = 4;
        bool enableCelShading = false;
        float celShadingLevels = 4.0f;
        bool enableOutline = false;
        int outlineMode = 0; // 0: Depth+Normal, 1: Silhouette, 2: Inner
        float outlineThickness = 1.0f;
        float outlineThresholdDepth = 0.05f;
        float outlineThresholdNormal = 0.5f;
        float outlineColor[3] = {0.0f, 0.0f, 0.0f};
        bool enableOutlineJitter = false;
        float outlineJitterSpeed = 12.0f;
        float outlineJitterStrength = 1.5f;
        bool enableVoronoi = false;
        float voronoiScale = 20.0f;
        float objHatchingScale = 0.0f;
        
        bool enableSSR = false;
        float ssrSteps = 32.0f, ssrThickness = 1.0f;
        bool enableSSSS = false;
        float ssssWidth = 0.015f;
        bool enableWeather = false;
        float weatherIntensity = 1.0f;
        float weatherSpeed = 1.0f;
        float weatherSize = 20.0f;
        float weatherDensity = 0.6f;
        float weatherDistortion = 0.1f;
        bool enableVignette = false, enableChromaticAberration = false, enableBloom = true, enableLensFlares = false;
        bool enableAnamorphic = false;
        float vignetteIntensity = 0.5f, caIntensity = 0.005f, bloomThreshold = 1.0f, bloomIntensity = 1.5f;
        int bloomBlurIterations = 10, ghosts = 4;
        float flareIntensity = 0.5f, ghostDispersal = 0.3f, flareHaloWidth = 0.2f, flareChromaticDir = 0.02f;

        bool enableTAA = false, enableCAS = false;
        
        int vrsMode = 0; // 0: 1x1, 1: 2x2, 2: 4x4
        float taaBlendFactor = 0.1f, casSharpness = 0.5f;

        int tonemapper = 2; // 0: Linear, 1: Reinhard, 2: ACES
        float cgShadows[3] = {1.0f, 1.0f, 1.0f};
        float cgMidtones[3] = {1.0f, 1.0f, 1.0f};
        float cgHighlights[3] = {1.0f, 1.0f, 1.0f};

        bool enableDoF = false, enableMotionBlur = false, enableFilmGrain = false, enableFog = false;
        bool autoFocus = false;
        float focusDistance = 10.0f, focusRange = 3.0f, bokehSize = 2.0f, mbStrength = 0.5f, grainIntensity = 0.05f;
        int bokehShape = 0; // 0: Circle, 1: Hexagon, 2: Octagon, 3: Triangle
        float bokehAngle = 0.0f;
        float fogDensity = 0.02f, fogHeightFalloff = 0.2f, fogBaseHeight = 0.0f;
        float fogColor[3] = {0.5f, 0.6f, 0.7f}, inscatterColor[3] = {1.0f, 0.8f, 0.5f};
        float inscatterPower = 8.0f, inscatterIntensity = 1.0f;

        float skyZenithColor[3] = {0.15f, 0.35f, 0.75f}, skyHorizonColor[3] = {0.6f, 0.7f, 0.8f};
        float sunSize = 0.005f, sunGlow = 1.5f, sunGlowSize = 0.1f;

        bool enableContactShadows = true;
        float contactShadowLength = 0.05f, contactShadowThickness = 0.1f;
        int contactShadowSteps = 16;

        float sharpenIntensity = 1.0f;
        bool enableSharpen = false;
        float anisotropicFiltering = 1.0f;

        float cgGlobalLift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgGlobalGamma[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgGlobalGain[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgGlobalOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgShadowsLift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgShadowsGamma[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgShadowsGain[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgShadowsOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgMidtonesLift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgMidtonesGamma[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgMidtonesGain[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgMidtonesOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgHighlightsLift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgHighlightsGamma[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgHighlightsGain[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float cgHighlightsOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float cgRgbMixerRed[3] = {1.0f, 0.0f, 0.0f};
        float cgRgbMixerGreen[3] = {0.0f, 1.0f, 0.0f};
        float cgRgbMixerBlue[3] = {0.0f, 0.0f, 1.0f};

        int blurMode = 0; 
        float blurStrength = 1.0f;
        float blurRadius = 5.0f;
        float radialBlurCenter[2] = {0.5f, 0.5f};

        bool enableColorInvert = false;
        bool enableFalseColor = false;
        bool enableDepthView = false;

        bool enableFilmDamage = false;
        float filmDamageIntensity = 1.0f;
        float filmDamageScratches = 1.0f;

        bool enableEdgeDetect = false;
        float edgeWidth = 1.0f;
        float edgeBrightness = 1.0f;
        float edgeGamma = 1.0f;
        float edgeBlur = 0.0f;
        int edgeColorMode = 0;
        float edgeCustomColor[3] = {1.0f, 1.0f, 1.0f};
        bool enableEmboss = false;
        float embossStrength = 1.0f;
        float embossAngle = 45.0f;
        int embossStyle = 0;
        bool enableSketch = false;
        float sketchStrokeStrength = 1.0f;
        float sketchStrokeLength = 2.0f;
        float sketchThreshold = 0.5f;
        float sketchShadowLevel = 0.5f;
        float sketchShadowsWeight = 1.0f;
        float sketchMidtonesWeight = 1.0f;
        float sketchHighlightsWeight = 1.0f;

        float mbTrails = 0.0f;
        std::string lensDirtPath = "";
        std::shared_ptr<BurnhopeTexture> lensDirtTex = nullptr;
        bool enableHalftone = false;
        float halftoneScale = 1.0f;
        float halftoneContrast = 1.0f;
        std::string halftonePath = "";
        std::shared_ptr<BurnhopeTexture> halftoneTex = nullptr;
        int ditherMode = 0;
        std::string ditherTexPath = "";
        std::shared_ptr<BurnhopeTexture> ditherTex = nullptr;
        float ditherScale = 1.0f;
        float ditherShadowColor[3] = {0.0f, 0.0f, 0.0f};
        float ditherMidColor[3] = {0.8f, 0.2f, 0.2f};
        float ditherHighlightColor[3] = {1.0f, 0.8f, 0.8f};

        bool enableTexWarp = false;
        float texWarpStrength = 0.01f;
        float texWarpSpeed = 1.0f;
        bool enableVtxWarp = false;
        float vtxWarpStrength = 0.1f;
        float vtxWarpSpeed = 1.0f;
        float vtxWarpScale = 1.0f;
        bool enableColorComp = false;
        float colorCompLevels = 16.0f;
        bool enableCMAA = false;
        std::string palettePath = "";
        std::shared_ptr<BurnhopeTexture> paletteTex = nullptr;
        bool enableShadowRamp = false;
        float shadowRampColor1[3] = {0.0f, 0.0f, 0.2f};
        float shadowRampColor2[3] = {0.8f, 0.1f, 0.1f};

        bool enableOpticalSoup = false;
        float opticalSoupRadius = 5.0f;
        bool enableDatamosh = false;
        float datamoshThreshold = 0.01f;
        bool enableAscii = false;
        int asciiMode = 0;
        float asciiScale = 8.0f;
        bool enablePixelSort = false;
        float pixelSortThreshold = 0.8f;
        float pixelSortAngle = 90.0f;
        float pixelSortLength = 0.1f;
        float pixelSortTime = 1.0f;
        bool enableImpactFrame = false;
        float impactSize = 10.0f, impactPower = 1.0f, impactTime = 10.0f;
        bool enableSmearTrails = false;
        float smearLength = 0.9f, smearThreshold = 0.8f;
        bool enableRimLight = false;
        float rimColor[3] = {1.0f, 1.0f, 1.0f};
        float rimPower = 4.0f;
        float rimThickness = 0.5f;

        bool enableScreentones = false;
        float screentoneSize = 4.0f;
        float screentoneDarkness = 1.0f;
        bool enableWatercolor = false;
        float watercolorRadius = 3.0f;
        bool enablePointillism = false;
        float pointillismSize = 5.0f;
        float pointillismDensity = 0.8f;
        bool enableTiltShift = false;
        int tiltShiftMode = 0; // 0: Top/Bottom, 1: Left/Right, 2: Radial (4 sides)
        float tiltShiftAmount = 5.0f, tiltShiftFalloff = 0.5f;
        bool enableLensBreathing = false;
        float lensBreathingScale = 1.0f;
        bool enableStarFilter = false;
        float starFilterThreshold = 2.0f, starFilterLength = 1.5f;
        bool enableLightLeaks = false;
        float lightLeakIntensity = 1.0f;
        bool enableJpegArtifacts = false;
        float jpegBlockSize = 8.0f, jpegQuality = 16.0f;
        bool enableGameBoy = false;
        float gbColor1[3] = {0.06f, 0.22f, 0.06f}, gbColor2[3] = {0.19f, 0.38f, 0.19f}, gbColor3[3] = {0.55f, 0.67f, 0.06f}, gbColor4[3] = {0.60f, 0.74f, 0.06f};
        bool enableScreenTear = false;
        float screenTearFrequency = 5.0f, screenTearIntensity = 0.05f;

        bool enableSpeedLines = false; float speedLinesIntensity = 1.0f;
        bool enableColorSplash = false; float splashHue = 0.0f, splashRange = 0.1f;
        bool enableHeatShimmer = false; float heatIntensity = 0.01f;
        bool enableTemporalEcho = false; float echoFade = 0.05f;
        bool enableCanvas = false; float canvasIntensity = 1.0f;
        std::string canvasTexPath = ""; std::shared_ptr<BurnhopeTexture> canvasTex = nullptr;
        bool enableInkBleed = false; float inkRadius = 1.0f;
        
        bool enableGlitter = false; float glitterThreshold = 0.95f;
        bool enableCaustics = false; float causticsSpeed = 2.0f, causticsScale = 10.0f, causticsStrength = 0.5f;
        std::string causticsTexPath = ""; std::shared_ptr<BurnhopeTexture> causticsTex = nullptr;
        int causticsTexIdx = -1;
        
        bool enableWorldCurve = false; float curveAmount = 0.001f;
        bool enableBreathing = false; float breathAmplitude = 0.05f, breathSpeed = 2.0f;

        bool enableTranslucency = false; float translucencyStrength = 1.0f;
        bool enableAnimeSpecular = false; float animeSpecBands = 3.0f;
        bool enableAstigmatism = false; float astigmatismLength = 0.05f; float astigmatismAngle = 45.0f;
        bool enableDollyZoom = false; float dollyZoomSpeed = 1.0f; float dollyZoomIntensity = 20.0f; bool dollyZoomInvert = false;
        bool enableSaccadicMasking = false; float saccadicThreshold = 0.05f;
        bool enableBurningFilm = false; float burningTime = 0.0f;
        bool enablePhosphor = false; float phosphorFade = 0.1f;
        bool enableWorldASCII = false; float worldAsciiScale = 10.0f;
        bool enableGravityLensing = false; float gravityMass = 0.1f;
        bool enableVectorFlow = false; float vectorFlowStrength = 0.05f;
        float vectorFieldScale = 1.0f; int vectorTexIdx = -1;
        std::string vectorTexPath = ""; std::shared_ptr<BurnhopeTexture> vectorTex = nullptr;
        bool enableKMeans = false; float kMeansColors = 5.0f;
        bool enableRecursiveFeedback = false; float feedbackZoom = 0.99f; float feedbackAngle = 1.0f;
        bool enableCrosshatchLight = false;
        bool bayerWorldSpace = false;
        bool enableAnalogNoise = false; float analogSyncLoss = 0.1f;
        bool enableScanlineMoire = false; float moireScale = 400.0f;
        bool enableTunnelVision = false; float tunnelIntensity = 0.5f;
        bool enableAfterimage = false; float afterimageFade = 0.05f;
        bool enableTemporalBleed = false; float bleedSpeed = 0.8f;
        bool enableFluidSim = false; float fluidSpeed = 1.0f;
        bool enableCMYK = false; float cmykOffset = 0.005f;
        bool enableCondensation = false; float condensationAmount = 0.5f;
        bool enableDustMotes = false; float dustIntensity = 1.0f;
        bool enableEctoplasm = false; float ectoplasmColor[3] = {0.2f, 1.0f, 0.2f};
        bool enableRollingShutter = false; float rollingShutterSpeed = 0.1f;
        bool enablePurkinje = false; float purkinjeIntensity = 1.0f; float purkinjeColor[3] = {0.4f, 0.0f, 0.0f}; float purkinjeThickness = 0.1f; float purkinjeSpeed = 1.0f;
        bool enableSlitScan = false; float slitScanSpeed = 1.0f;
        bool enableReactionDiffusion = false; float rdSpeed = 1.0f;
        bool enableDroste = false; float drosteScale = 0.5f;
        bool enableFrost = false;
    float frostIntensity = 1.0f;
    
    bool enableWaterDrops = false;
    float dropRefraction = 0.5f;
    
    // --- Psychological & Horror FX ---
    bool enableBlinking = false; float blinkFrequency = 1.0f;
    bool enableFloaters = false; float floatersOpacity = 0.5f;
    bool enableTimeStutter = false; float stutterSeverity = 0.5f; float stutterSpeed = 1.0f;
    bool enableHollowFace = false; // Инверсия глубины/нормалей
    float hollowFaceDepth = 1.0f;
    bool enableMelting = false; float meltSpeed = 1.0f; float meltThreshold = 0.8f; float meltNoiseScale = 12.0f;
    bool enableAntiLight = false; // Разрешить отрицательный свет
    bool enableTrypo = false; float trypoScale = 100.0f;
    bool enableParallaxEye = false; 
    bool enableInsideSmudges = false; float smudgeIntensity = 0.5f;
    bool enableHaunting = false; float hauntingTrail = 0.9f;
    bool enableNystagmus = false; float nystagmusSeverity = 0.05f;
     float purkinjeScale = 1.0f;
    bool enableRodCone = false; float rodConeThreshold = 0.15f; float rodConeColor[3] = {0.1f, 0.4f, 0.8f};
    bool enableFluidLens = false; float fluidViscosity = 0.9f;
    float speedLinesCount = 50.0f; float speedLinesLength = 0.8f;
    int canvasTexIdx = 0;
    };

    struct SceneSnapshot {
        std::shared_ptr<entt::registry> regCopy;
        entt::entity selectedEntity;
    };

    struct PendingDeletion {
        std::vector<std::shared_ptr<void>> objects;
        int framesRemaining;
    };

    class UIContext {
    public:
        class BurnhopeDevice* device = nullptr;
        VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
        entt::registry* registry = nullptr;
        entt::entity selectedEntity = entt::null;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        std::string currentScenePath = "";

        fs::path projectDirectory;
        fs::path exeDirectory;
        fs::path currentDirectory;
        std::vector<fs::path> dirHistory;
        int dirHistoryIndex = -1;

        std::vector<std::string> selectedAssets;
        std::vector<std::string> clipboardPaths;
        bool isCut = false;
        std::string renamingPath = "";

        RenderSettings renderSettings;
        bool needsRebuild = false;
        bool needsRTRebuild = false;
        bool pendingNewScene = false;
        std::string pendingSceneLoadPath = "";
        std::string pendingModelLoadPath = "";
        entt::entity pendingModelEntity = entt::null;
        std::string pendingMatLoadPath = "";
        uint32_t pendingMatSlot = 0;
        entt::entity pendingMatEntity = entt::null;
        std::vector<std::shared_ptr<void>> safeDeleteQueue;
        std::vector<PendingDeletion> pendingDeletions;
        std::vector<SceneSnapshot> undoStack;
        std::vector<SceneSnapshot> redoStack;
        
#define L_BOOL(name) if(j.contains(#name)) renderSettings.name = j[#name].get<bool>()
#define L_FLOAT(name) if(j.contains(#name)) renderSettings.name = j[#name].get<float>()
#define L_INT(name) if(j.contains(#name)) renderSettings.name = j[#name].get<int>()
#define L_STR(name) if(j.contains(#name)) renderSettings.name = j[#name].get<std::string>()
#define L_ARR3(name) if(j.contains(#name)) { renderSettings.name[0] = j[#name][0]; renderSettings.name[1] = j[#name][1]; renderSettings.name[2] = j[#name][2]; }

        void LoadRenderSettings(const std::string& filepath) {
            std::ifstream file(filepath);
            if (!file.is_open()) return;
            json j;
            try { file >> j; } catch(...) { return; }

            L_BOOL(enableBlinking); L_FLOAT(blinkFrequency);
            L_BOOL(enableFloaters); L_FLOAT(floatersOpacity);
            L_BOOL(enableTimeStutter); L_FLOAT(stutterSeverity); L_FLOAT(stutterSpeed);
            L_BOOL(enableHollowFace); L_FLOAT(hollowFaceDepth);
            L_BOOL(enableMelting); L_FLOAT(meltSpeed); L_FLOAT(meltThreshold); L_FLOAT(meltNoiseScale);
            L_BOOL(enableAntiLight); L_BOOL(enableTrypo); L_FLOAT(trypoScale);
            L_BOOL(enableParallaxEye); L_BOOL(enableInsideSmudges); L_FLOAT(smudgeIntensity);
            L_BOOL(enableHaunting); L_FLOAT(hauntingTrail); L_BOOL(enableNystagmus); L_FLOAT(nystagmusSeverity);
            L_BOOL(enablePurkinje); L_FLOAT(purkinjeScale); L_BOOL(enableRodCone); L_FLOAT(rodConeThreshold); L_ARR3(rodConeColor);
            L_BOOL(enableFluidLens); L_FLOAT(fluidViscosity); L_FLOAT(purkinjeIntensity); L_ARR3(purkinjeColor); L_FLOAT(purkinjeThickness); L_FLOAT(purkinjeSpeed);
            L_FLOAT(speedLinesCount); L_FLOAT(speedLinesLength);
            L_BOOL(enableRecursiveFeedback); L_FLOAT(feedbackZoom); L_FLOAT(feedbackAngle);
            L_BOOL(enableAnalogNoise); L_FLOAT(analogSyncLoss); L_BOOL(enableScanlineMoire); L_FLOAT(moireScale);
            L_BOOL(enableTunnelVision); L_FLOAT(tunnelIntensity); L_BOOL(enableAfterimage); L_FLOAT(afterimageFade);
            L_BOOL(enableTemporalBleed); L_FLOAT(bleedSpeed); L_BOOL(enableFluidSim); L_FLOAT(fluidSpeed);
            L_BOOL(enableCMYK); L_FLOAT(cmykOffset); L_BOOL(enableCondensation); L_FLOAT(condensationAmount);
            L_BOOL(enableDustMotes); L_FLOAT(dustIntensity); L_BOOL(enableEctoplasm); L_ARR3(ectoplasmColor);
            L_BOOL(enableRollingShutter); L_FLOAT(rollingShutterSpeed); L_BOOL(enableSlitScan); L_FLOAT(slitScanSpeed);
            L_BOOL(enableReactionDiffusion); L_FLOAT(rdSpeed); L_BOOL(enableDroste); L_FLOAT(drosteScale);
            L_BOOL(enableCrosshatchLight); L_BOOL(bayerWorldSpace);
            L_STR(vectorTexPath); L_STR(causticsTexPath); L_STR(canvasTexPath);
            L_BOOL(enableBloom); L_FLOAT(bloomThreshold); L_FLOAT(bloomIntensity); L_INT(bloomBlurIterations);
            L_BOOL(enableLensFlares); L_FLOAT(flareIntensity); L_FLOAT(ghostDispersal); L_INT(ghosts); L_BOOL(enableAnamorphic);
            L_BOOL(enableFilmGrain); L_FLOAT(grainIntensity); L_BOOL(enableSharpen); L_FLOAT(sharpenIntensity);
            L_INT(tonemapper); L_ARR3(cgShadows); L_ARR3(cgMidtones); L_ARR3(cgHighlights);
            L_BOOL(enableDoF); L_FLOAT(focusDistance); L_FLOAT(focusRange); L_FLOAT(bokehSize); L_INT(bokehShape); L_FLOAT(bokehAngle);
            L_BOOL(enableMotionBlur); L_FLOAT(mbStrength);
            L_BOOL(enableFog); L_FLOAT(fogDensity); L_FLOAT(fogHeightFalloff); L_FLOAT(fogBaseHeight); L_FLOAT(inscatterPower); L_FLOAT(inscatterIntensity);
            L_ARR3(fogColor); L_ARR3(inscatterColor); L_ARR3(skyZenithColor); L_ARR3(skyHorizonColor);
            L_FLOAT(sunSize); L_FLOAT(sunGlow); L_FLOAT(sunGlowSize);
            L_BOOL(enableSSAO); L_FLOAT(ssaoRadius); L_FLOAT(ssaoBias); L_FLOAT(ssaoIntensity); L_FLOAT(ssaoPower);
            L_BOOL(enableSSGI); L_INT(ssgiRayCount); L_FLOAT(ssgiStepSize); L_FLOAT(ssgiThickness); L_INT(blurRange);
            L_BOOL(enableContactShadows); L_FLOAT(contactShadowLength); L_FLOAT(contactShadowThickness); L_INT(contactShadowSteps);
            L_BOOL(autoExposure); L_FLOAT(manualExposure); L_FLOAT(exposureCompensation); L_FLOAT(minBrightness); L_FLOAT(maxBrightness);
            L_FLOAT(contrast); L_FLOAT(saturation); L_FLOAT(temperature); L_FLOAT(gamma);
            L_BOOL(enableLensDistortion); L_FLOAT(lensDistortionStrength); L_BOOL(enableLensDirt); L_FLOAT(lensDirtIntensity);
            L_BOOL(enableDithering); L_FLOAT(ditherStrength); L_BOOL(enableScreenRefraction); L_FLOAT(refractionStrength); L_FLOAT(refractionSpeed);
            L_BOOL(enableRetroCRT); L_FLOAT(crtScanlines); L_FLOAT(glitchIntensity); L_FLOAT(vhsNoise); L_INT(pixelation);
            L_BOOL(enableVertexJitter); L_FLOAT(vertexJitterResolution); L_BOOL(enablePosterization); L_FLOAT(posterizationLevels);
            L_BOOL(enableKuwahara); L_INT(kuwaharaRadius); L_BOOL(enableCelShading); L_FLOAT(celShadingLevels);
            L_BOOL(enableVoronoi); L_FLOAT(voronoiScale); L_FLOAT(objHatchingScale);
            L_INT(vrsMode); L_BOOL(enableOutline); L_INT(outlineMode); L_FLOAT(outlineThickness); L_FLOAT(outlineThresholdDepth); L_FLOAT(outlineThresholdNormal);
            L_ARR3(outlineColor); L_BOOL(enableOutlineJitter); L_FLOAT(outlineJitterSpeed); L_FLOAT(outlineJitterStrength);
            L_BOOL(enableVignette); L_FLOAT(vignetteIntensity); L_BOOL(enableChromaticAberration); L_FLOAT(caIntensity);
            L_BOOL(enableEdgeDetect); L_FLOAT(edgeWidth); L_FLOAT(edgeBrightness); L_FLOAT(edgeGamma); L_FLOAT(edgeBlur); L_INT(edgeColorMode); L_ARR3(edgeCustomColor);
            L_BOOL(enableEmboss); L_FLOAT(embossStrength); L_FLOAT(embossAngle); L_INT(embossStyle);
            L_BOOL(enableSketch); L_FLOAT(sketchStrokeStrength); L_FLOAT(sketchStrokeLength); L_FLOAT(sketchThreshold); L_FLOAT(sketchShadowLevel); L_FLOAT(sketchShadowsWeight); L_FLOAT(sketchMidtonesWeight); L_FLOAT(sketchHighlightsWeight);
            L_STR(lensDirtPath); L_BOOL(enableHalftone); L_FLOAT(halftoneScale); L_FLOAT(halftoneContrast); L_STR(halftonePath);
            L_INT(ditherMode); L_STR(ditherTexPath); L_FLOAT(ditherScale); L_ARR3(ditherShadowColor); L_ARR3(ditherMidColor); L_ARR3(ditherHighlightColor);
            L_FLOAT(mbTrails); L_BOOL(enableTexWarp); L_FLOAT(texWarpStrength); L_FLOAT(texWarpSpeed);
            L_BOOL(enableVtxWarp); L_FLOAT(vtxWarpStrength); L_FLOAT(vtxWarpSpeed); L_FLOAT(vtxWarpScale);
            L_BOOL(enableColorComp); L_FLOAT(colorCompLevels); L_BOOL(enableCMAA); L_STR(palettePath);
            L_BOOL(enableShadowRamp); L_ARR3(shadowRampColor1); L_ARR3(shadowRampColor2);
            L_BOOL(enableOpticalSoup); L_FLOAT(opticalSoupRadius); L_BOOL(enableDatamosh); L_FLOAT(datamoshThreshold);
            L_BOOL(enableAscii); L_INT(asciiMode); L_FLOAT(asciiScale); L_BOOL(enablePixelSort); L_FLOAT(pixelSortThreshold); L_FLOAT(pixelSortAngle); L_FLOAT(pixelSortLength); L_FLOAT(pixelSortTime);
            L_BOOL(enableImpactFrame); L_FLOAT(impactSize); L_FLOAT(impactPower); L_FLOAT(impactTime);
            L_BOOL(enableSmearTrails); L_FLOAT(smearLength); L_FLOAT(smearThreshold);
            L_BOOL(enableRimLight); L_ARR3(rimColor); L_FLOAT(rimPower); L_FLOAT(rimThickness);
            L_BOOL(enableScreentones); L_FLOAT(screentoneSize); L_FLOAT(screentoneDarkness);
            L_BOOL(enableWatercolor); L_FLOAT(watercolorRadius); L_BOOL(enablePointillism); L_FLOAT(pointillismSize); L_FLOAT(pointillismDensity);
            L_BOOL(enableTiltShift); L_INT(tiltShiftMode); L_FLOAT(tiltShiftAmount); L_FLOAT(tiltShiftFalloff);
            L_BOOL(enableLensBreathing); L_FLOAT(lensBreathingScale); L_BOOL(enableStarFilter); L_FLOAT(starFilterThreshold); L_FLOAT(starFilterLength);
            L_BOOL(enableLightLeaks); L_FLOAT(lightLeakIntensity); L_BOOL(enableJpegArtifacts); L_FLOAT(jpegBlockSize); L_FLOAT(jpegQuality);
            L_BOOL(enableGameBoy); L_ARR3(gbColor1); L_ARR3(gbColor2); L_ARR3(gbColor3); L_ARR3(gbColor4);
            L_BOOL(enableScreenTear); L_FLOAT(screenTearFrequency); L_FLOAT(screenTearIntensity);
            L_BOOL(enableSpeedLines); L_FLOAT(speedLinesIntensity); L_BOOL(enableColorSplash); L_FLOAT(splashHue); L_FLOAT(splashRange);
            L_BOOL(enableHeatShimmer); L_FLOAT(heatIntensity); L_BOOL(enableFrost); L_FLOAT(frostIntensity);
            L_BOOL(enableWaterDrops); L_FLOAT(dropRefraction); L_BOOL(enableTemporalEcho); L_FLOAT(echoFade);
            L_BOOL(enableCanvas); L_FLOAT(canvasIntensity); L_BOOL(enableInkBleed); L_FLOAT(inkRadius);
            L_BOOL(enableGlitter); L_FLOAT(glitterThreshold); L_BOOL(enableCaustics); L_FLOAT(causticsSpeed); L_FLOAT(causticsScale); L_FLOAT(causticsStrength);
            L_BOOL(enableWorldCurve); L_FLOAT(curveAmount); L_BOOL(enableBreathing); L_FLOAT(breathAmplitude); L_FLOAT(breathSpeed);
            
            if (j.contains("anisotropicFiltering")) BurnhopeTexture::GlobalAnisotropy = j["anisotropicFiltering"].get<float>();
            
            // Load textures if device is valid and paths are not empty
            if(device != nullptr) {
                if (!renderSettings.vectorTexPath.empty() && fs::exists(renderSettings.vectorTexPath)) renderSettings.vectorTex = BurnhopeTexture::createDataTextureFromFile(*device, renderSettings.vectorTexPath);
                if (!renderSettings.causticsTexPath.empty() && fs::exists(renderSettings.causticsTexPath)) renderSettings.causticsTex = BurnhopeTexture::createDataTextureFromFile(*device, renderSettings.causticsTexPath);
                if (!renderSettings.canvasTexPath.empty() && fs::exists(renderSettings.canvasTexPath)) renderSettings.canvasTex = BurnhopeTexture::createDataTextureFromFile(*device, renderSettings.canvasTexPath);
            }
        }

        // Утилита для получения списка файлов нужного типа
        std::vector<std::string> GetProjectAssets(const std::vector<std::string>& extensions) {
            std::vector<std::string> result;
            if (!fs::exists(projectDirectory)) return result;
            
            for (const auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    for (const auto& e : extensions) {
                        if (ext == e) {
                            result.push_back(entry.path().string());
                            break;
                        }
                    }
                }
            }
            return result;
        }

        // Вспомогательные методы, перенесенные из твоего старого UI
        void SaveState() {
            auto snapReg = std::make_shared<entt::registry>();
            CopyRegistry(*registry, *snapReg);
            undoStack.push_back({snapReg, selectedEntity});
            redoStack.clear();
            if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
        }

        void CopyRegistry(entt::registry& src, entt::registry& dst) {
            dst.clear();
            src.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
                entt::entity newEnt = dst.create(entity); 
                dst.emplace<TagComponent>(newEnt, tag);
                if (src.all_of<IDComponent>(entity)) dst.emplace<IDComponent>(newEnt, src.get<IDComponent>(entity));
                if (src.all_of<TransformComponent>(entity)) dst.emplace<TransformComponent>(newEnt, src.get<TransformComponent>(entity));
                if (src.all_of<MeshComponent>(entity)) dst.emplace<MeshComponent>(newEnt, src.get<MeshComponent>(entity));
                if (src.all_of<LightComponent>(entity)) dst.emplace<LightComponent>(newEnt, src.get<LightComponent>(entity));
                if (src.all_of<HierarchyComponent>(entity)) dst.emplace<HierarchyComponent>(newEnt, src.get<HierarchyComponent>(entity)); 
                if (src.all_of<ReflectionProbeComponent>(entity)) dst.emplace<ReflectionProbeComponent>(newEnt, src.get<ReflectionProbeComponent>(entity));
            });
        }

        entt::entity FindEntityByID(uint64_t id) {
            if (id == 0) return entt::null;
            for (auto e : registry->view<IDComponent>()) {
                if (registry->get<IDComponent>(e).ID == id) return e;
            }
            return entt::null;
        }

        void DetachFromParent(entt::entity child) {
            if (!registry->all_of<HierarchyComponent>(child) || !registry->all_of<IDComponent>(child)) return;
            auto& hc = registry->get<HierarchyComponent>(child);
            if (hc.parentID == 0) return;
            uint64_t myID = registry->get<IDComponent>(child).ID;
            entt::entity parentEnt = FindEntityByID(hc.parentID);
            if (parentEnt != entt::null && registry->all_of<HierarchyComponent>(parentEnt)) {
                auto& phc = registry->get<HierarchyComponent>(parentEnt);
                phc.childrenIDs.erase(std::remove(phc.childrenIDs.begin(), phc.childrenIDs.end(), myID), phc.childrenIDs.end());
            }
            hc.parentID = 0;
        }

        void AttachToParent(entt::entity child, entt::entity newParent) {
            if (child == newParent) return;
            DetachFromParent(child);
            if (newParent == entt::null) return;
            if (!registry->all_of<HierarchyComponent>(child)) registry->emplace<HierarchyComponent>(child);
            if (!registry->all_of<HierarchyComponent>(newParent)) registry->emplace<HierarchyComponent>(newParent);
            
            uint64_t myID = registry->get<IDComponent>(child).ID;
            uint64_t pid = registry->get<IDComponent>(newParent).ID;

            registry->get<HierarchyComponent>(child).parentID = pid;
            registry->get<HierarchyComponent>(newParent).childrenIDs.push_back(myID);
        }

        entt::entity CreateBaseEntity(const std::string& name) {
            entt::entity e = registry->create();
            registry->emplace<IDComponent>(e); 
            registry->emplace<TagComponent>(e, name);
            registry->emplace<TransformComponent>(e);
            registry->emplace<HierarchyComponent>(e);
            return e;
        }

        void DeleteEntityRecursive(entt::entity target) {
            if (!registry->valid(target)) return;
            
            if (device) vkDeviceWaitIdle(device->device());
            
            if (registry->all_of<HierarchyComponent>(target)) {
                auto children = registry->get<HierarchyComponent>(target).childrenIDs;
                for (uint64_t childID : children) DeleteEntityRecursive(FindEntityByID(childID));
            }
            DetachFromParent(target);
            if (selectedEntity == target) selectedEntity = entt::null;
            registry->destroy(target);
        }
    };
}