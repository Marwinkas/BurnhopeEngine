#pragma once
#include "IUIWindow.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <glm/glm.hpp>

// Mechanical port from ImGui: tabs/CollapsingHeader flatten into TreeNode
// section rows (UIWidgets::TreeNode always renders expanded — there is no
// per-frame collapse state in this widget set, matching the old
// `ImGuiTreeNodeFlags_DefaultOpen` sections which is the overwhelming
// majority of this panel anyway). Slider min/max clamps from the ImGui
// version are dropped in favor of UIWidgets' drag-float semantics
// (DrawFloatControl/DrawIntControl), consistent with every other ported
// panel in this pass.
namespace burnhope {
    using json = nlohmann::json;

    class PropertiesWindow : public IUIWindow {
    public:
        PropertiesWindow() : IUIWindow("Properties") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            ui::Panel panel(widgets, m_Name, contentRect);
            DrawRenderSettings(context, widgets);
            widgets.Separator();
            if (widgets.Button("SAVE SETTINGS", {200, 34})) SaveRenderSettings(context);
        }

    private:
        static glm::vec3& Col3(float* a) { return *reinterpret_cast<glm::vec3*>(a); }
        static glm::vec2& Vec2(float* a) { return *reinterpret_cast<glm::vec2*>(a); }

        void Header(ui::UIWidgets& w, const std::string& name) {
            w.Separator();
            w.Text(name, {0.0f, 0.62f, 0.95f, 1.0f});
        }

        void DrawRenderSettings(UIContext& context, ui::UIWidgets& w) {
            RenderSettings& rs = context.renderSettings;
            w.Text("Render & Post-Processing Settings", {0.0f, 0.45f, 0.85f, 1.0f});

            // ---------------- Lighting & GI ----------------
            Header(w, "Lighting & GI");
            if (w.Button("FORCE GLOBAL RT REBUILD", {260, 28})) {
                context.world->each<MeshComponent>([&](flecs::entity, MeshComponent& mc) {
                    if (mc.model && !mc.model->storedPositions.empty()) mc.model->createBLAS(mc.model->storedPositions);
                });
                context.needsRTRebuild = true;
            }
            w.DrawCheckboxControl("Enable SSGI", &rs.enableSSGI);
            if (rs.enableSSGI) { w.DrawIntControl("Ray Count", &rs.ssgiRayCount); w.DrawFloatControl("Step Size", &rs.ssgiStepSize); w.DrawFloatControl("Thickness", &rs.ssgiThickness); w.DrawIntControl("Blur Range", &rs.blurRange); }
            w.DrawCheckboxControl("Enable RT GI (Radiance Cascades)", &rs.enableRadianceCascades);
            if (rs.enableRadianceCascades) { w.DrawIntControl("Probe Grid X", &rs.rcProbeGridX); w.DrawIntControl("Probe Grid Y", &rs.rcProbeGridY); w.DrawIntControl("Probe Grid Z", &rs.rcProbeGridZ); w.DrawFloatControl("Ray Length", &rs.rcBaseRayLength); w.DrawIntControl("Octahedron Size", &rs.rcOctaSize); }
            w.DrawCheckboxControl("Enable SSSS (Subsurface)", &rs.enableSSSS);
            if (rs.enableSSSS) w.DrawFloatControl("Scatter Width", &rs.ssssWidth);
            w.DrawCheckboxControl("Enable Shadow Ramp", &rs.enableShadowRamp);
            if (rs.enableShadowRamp) { w.DrawColorControl("Shadow Color", Col3(rs.shadowRampColor1)); w.DrawColorControl("Light-Transition Color", Col3(rs.shadowRampColor2)); }

            // ---------------- Reflections & AO ----------------
            Header(w, "Reflections & AO");
            w.DrawCheckboxControl("Enable RT Reflections", &rs.enableRTReflections);
            if (rs.enableRTReflections) w.DrawIntControl("Max Bounces", &rs.rtMaxBounces);
            w.DrawCheckboxControl("Enable GTAO", &rs.enableSSAO);
            if (rs.enableSSAO) { w.DrawFloatControl("AO Radius", &rs.ssaoRadius); w.DrawFloatControl("AO Bias", &rs.ssaoBias); w.DrawFloatControl("AO Intensity", &rs.ssaoIntensity); w.DrawFloatControl("AO Power", &rs.ssaoPower); }
            w.DrawCheckboxControl("Enable Contact Shadows", &rs.enableContactShadows);
            if (rs.enableContactShadows) { w.DrawFloatControl("Ray Length", &rs.contactShadowLength); w.DrawIntControl("Ray Steps", &rs.contactShadowSteps); w.DrawFloatControl("Ray Thickness", &rs.contactShadowThickness); }
            w.DrawCheckboxControl("Enable SSR", &rs.enableSSR);
            if (rs.enableSSR) { w.DrawFloatControl("SSR Steps", &rs.ssrSteps); w.DrawFloatControl("SSR Thickness", &rs.ssrThickness); }

            // ---------------- Environment ----------------
            Header(w, "Environment");
            w.DrawColorControl("Sky Zenith Color", Col3(rs.skyZenithColor));
            w.DrawColorControl("Sky Horizon Color", Col3(rs.skyHorizonColor));
            w.DrawFloatControl("Sun Size", &rs.sunSize); w.DrawFloatControl("Sun Glow", &rs.sunGlow); w.DrawFloatControl("Sun Glow Size", &rs.sunGlowSize);
            w.DrawCheckboxControl("Enable Fog", &rs.enableFog);
            if (rs.enableFog) {
                w.DrawColorControl("Fog Color", Col3(rs.fogColor)); w.DrawColorControl("Sun Inscatter Color", Col3(rs.inscatterColor));
                w.DrawFloatControl("Density", &rs.fogDensity); w.DrawFloatControl("Height Falloff", &rs.fogHeightFalloff); w.DrawFloatControl("Base Height", &rs.fogBaseHeight);
                w.DrawFloatControl("Sun Inscatter Power", &rs.inscatterPower); w.DrawFloatControl("Sun Inscatter Int", &rs.inscatterIntensity);
            }
            w.DrawCheckboxControl("Enable Rain Weather", &rs.enableWeather);
            if (rs.enableWeather) { w.DrawFloatControl("Weather Intensity", &rs.weatherIntensity); w.DrawFloatControl("Weather Speed", &rs.weatherSpeed); w.DrawFloatControl("Weather Size", &rs.weatherSize); w.DrawFloatControl("Weather Density", &rs.weatherDensity); w.DrawFloatControl("Weather Distortion", &rs.weatherDistortion); }
            w.DrawCheckboxControl("Enable Water Drops", &rs.enableWaterDrops);
            if (rs.enableWaterDrops) w.DrawFloatControl("Drop Refraction", &rs.dropRefraction);
            w.DrawCheckboxControl("Lens Condensation / Humidity", &rs.enableCondensation);
            if (rs.enableCondensation) w.DrawFloatControl("Condensation Amount", &rs.condensationAmount);

            // ---------------- Camera & Lens ----------------
            Header(w, "Camera & Lens");
            w.DrawCheckboxControl("Auto Exposure", &rs.autoExposure);
            if (rs.autoExposure) { w.DrawFloatControl("Exposure Compensation", &rs.exposureCompensation); w.DrawFloatControl("Min Brightness", &rs.minBrightness); w.DrawFloatControl("Max Brightness", &rs.maxBrightness); }
            else w.DrawFloatControl("Manual Exposure", &rs.manualExposure);
            w.DrawCheckboxControl("Enable DoF", &rs.enableDoF);
            if (rs.enableDoF) {
                w.DrawCheckboxControl("Auto-Focus", &rs.autoFocus);
                if (!rs.autoFocus) w.DrawFloatControl("Focus Distance", &rs.focusDistance);
                w.DrawFloatControl("Focus Range", &rs.focusRange); w.DrawFloatControl("Bokeh Size", &rs.bokehSize); w.DrawIntControl("Bokeh Shape", &rs.bokehShape);
                if (rs.bokehShape > 0) w.DrawFloatControl("Bokeh Rotation", &rs.bokehAngle);
                w.DrawCheckboxControl("Lens Breathing", &rs.enableLensBreathing);
                if (rs.enableLensBreathing) w.DrawFloatControl("Breathing Scale", &rs.lensBreathingScale);
            }
            w.DrawCheckboxControl("Enable Tilt-Shift", &rs.enableTiltShift);
            if (rs.enableTiltShift) { w.DrawIntControl("Tilt-Shift Mode", &rs.tiltShiftMode); w.DrawFloatControl("Tilt Amount", &rs.tiltShiftAmount); w.DrawFloatControl("Tilt Falloff", &rs.tiltShiftFalloff); }
            w.DrawCheckboxControl("Dolly Zoom (Vertigo)", &rs.enableDollyZoom);
            if (rs.enableDollyZoom) { w.DrawCheckboxControl("Invert Direction", &rs.dollyZoomInvert); w.DrawFloatControl("Zoom Speed", &rs.dollyZoomSpeed); w.DrawFloatControl("Zoom Intensity", &rs.dollyZoomIntensity); }
            w.DrawCheckboxControl("Enable Bloom", &rs.enableBloom);
            if (rs.enableBloom) { w.DrawFloatControl("Bloom Threshold", &rs.bloomThreshold); w.DrawFloatControl("Bloom Intensity", &rs.bloomIntensity); w.DrawIntControl("Blur Iterations", &rs.bloomBlurIterations); }
            w.DrawCheckboxControl("Enable Lens Flares", &rs.enableLensFlares);
            if (rs.enableLensFlares) { w.DrawFloatControl("Flare Intensity", &rs.flareIntensity); w.DrawFloatControl("Ghost Dispersal", &rs.ghostDispersal); w.DrawIntControl("Ghosts Count", &rs.ghosts); w.DrawFloatControl("Halo Width", &rs.flareHaloWidth); w.DrawFloatControl("Chromatic Dispersal", &rs.flareChromaticDir); w.DrawCheckboxControl("Anamorphic Blue Flares", &rs.enableAnamorphic); }
            w.DrawCheckboxControl("Enable Star Filter", &rs.enableStarFilter);
            if (rs.enableStarFilter) { w.DrawFloatControl("Star Threshold", &rs.starFilterThreshold); w.DrawFloatControl("Star Length", &rs.starFilterLength); }

            // ---------------- Color Grading ----------------
            Header(w, "Color Grading");
            w.DrawIntControl("Tonemapper (0=Linear 1=Reinhard 2=ACES)", &rs.tonemapper);
            w.DrawFloatControl("Contrast", &rs.contrast); w.DrawFloatControl("Saturation", &rs.saturation); w.DrawFloatControl("Color Temp (K)", &rs.temperature); w.DrawFloatControl("Gamma", &rs.gamma);
            w.DrawVec4Control("Global Lift", *reinterpret_cast<glm::vec4*>(rs.cgGlobalLift)); w.DrawVec4Control("Global Gamma", *reinterpret_cast<glm::vec4*>(rs.cgGlobalGamma)); w.DrawVec4Control("Global Gain", *reinterpret_cast<glm::vec4*>(rs.cgGlobalGain)); w.DrawVec4Control("Global Offset", *reinterpret_cast<glm::vec4*>(rs.cgGlobalOffset));
            w.DrawColorControl("Shadows Tint", Col3(rs.cgShadows)); w.DrawColorControl("Midtones Tint", Col3(rs.cgMidtones)); w.DrawColorControl("Highlights Tint", Col3(rs.cgHighlights));
            w.DrawColorControl("Red Channel Mixer", Col3(rs.cgRgbMixerRed)); w.DrawColorControl("Green Channel Mixer", Col3(rs.cgRgbMixerGreen)); w.DrawColorControl("Blue Channel Mixer", Col3(rs.cgRgbMixerBlue));
            w.DrawCheckboxControl("Enable Color Compression", &rs.enableColorComp);
            if (rs.enableColorComp) w.DrawFloatControl("Color Levels", &rs.colorCompLevels);
            w.DrawCheckboxControl("Selective Color (Splash)", &rs.enableColorSplash);
            if (rs.enableColorSplash) { w.DrawFloatControl("Target Hue", &rs.splashHue); w.DrawFloatControl("Hue Range", &rs.splashRange); }
            TexPicker(context, w, "Standard 3D LUT", rs.palettePath, [&](const std::string& p) { if (rs.paletteTex) context.safeDeleteQueue.push_back(rs.paletteTex); rs.palettePath = p; rs.paletteTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; });

            // ---------------- Filters & FX ----------------
            Header(w, "Filters & FX");
            w.DrawIntControl("Blur Mode (0=Off 1=Box 2=Gauss 3=Radial 4=Mosaic)", &rs.blurMode);
            if (rs.blurMode != 0) { w.DrawFloatControl("Blur Strength", &rs.blurStrength); w.DrawFloatControl("Blur Radius", &rs.blurRadius); if (rs.blurMode == 3) w.DrawVec2Control("Radial Center", Vec2(rs.radialBlurCenter)); }
            w.DrawCheckboxControl("Enable Lens Distortion", &rs.enableLensDistortion);
            if (rs.enableLensDistortion) w.DrawFloatControl("Distortion Strength", &rs.lensDistortionStrength);
            w.DrawCheckboxControl("Enable Lens Dirt", &rs.enableLensDirt);
            if (rs.enableLensDirt) { w.DrawFloatControl("Dirt Intensity", &rs.lensDirtIntensity); TexPicker(context, w, "Dirt Texture", rs.lensDirtPath, [&](const std::string& p) { if (rs.lensDirtTex) context.safeDeleteQueue.push_back(rs.lensDirtTex); rs.lensDirtPath = p; rs.lensDirtTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; }); }
            w.DrawCheckboxControl("Enable Retro/CRT", &rs.enableRetroCRT);
            if (rs.enableRetroCRT) { w.DrawFloatControl("Scanlines", &rs.crtScanlines); w.DrawFloatControl("Glitch", &rs.glitchIntensity); w.DrawFloatControl("VHS Noise", &rs.vhsNoise); w.DrawIntControl("Pixelation", &rs.pixelation); w.DrawCheckboxControl("PS1 Vertex Jitter", &rs.enableVertexJitter); if (rs.enableVertexJitter) w.DrawFloatControl("Wobble Resolution", &rs.vertexJitterResolution); }
            w.DrawCheckboxControl("Enable Film Damage", &rs.enableFilmDamage);
            if (rs.enableFilmDamage) { w.DrawFloatControl("Yellowing Intensity", &rs.filmDamageIntensity); w.DrawFloatControl("Scratches", &rs.filmDamageScratches); }
            w.DrawCheckboxControl("Film Grain", &rs.enableFilmGrain); if (rs.enableFilmGrain) w.DrawFloatControl("Grain Strength", &rs.grainIntensity);
            w.DrawCheckboxControl("Vignette", &rs.enableVignette); if (rs.enableVignette) w.DrawFloatControl("Vignette Intensity", &rs.vignetteIntensity);
            w.DrawCheckboxControl("Chromatic Aberration", &rs.enableChromaticAberration); if (rs.enableChromaticAberration) w.DrawFloatControl("CA Intensity", &rs.caIntensity);
            w.DrawCheckboxControl("Film Light Leaks", &rs.enableLightLeaks); if (rs.enableLightLeaks) w.DrawFloatControl("Leaks Intensity", &rs.lightLeakIntensity);
            w.DrawCheckboxControl("Enable Texture Warping", &rs.enableTexWarp);
            if (rs.enableTexWarp) { w.DrawFloatControl("Warp Strength", &rs.texWarpStrength); w.DrawFloatControl("Warp Speed", &rs.texWarpSpeed); }
            w.DrawCheckboxControl("Enable Edge Detect", &rs.enableEdgeDetect);
            if (rs.enableEdgeDetect) { w.DrawFloatControl("Width", &rs.edgeWidth); w.DrawFloatControl("Brightness", &rs.edgeBrightness); w.DrawFloatControl("Gamma", &rs.edgeGamma); w.DrawFloatControl("Edge Blur", &rs.edgeBlur); w.DrawIntControl("Color Mode", &rs.edgeColorMode); if (rs.edgeColorMode == 1) w.DrawColorControl("Custom Color", Col3(rs.edgeCustomColor)); }
            w.DrawCheckboxControl("Enable Emboss", &rs.enableEmboss);
            if (rs.enableEmboss) { w.DrawFloatControl("Strength", &rs.embossStrength); w.DrawFloatControl("Angle", &rs.embossAngle); w.DrawIntControl("Style", &rs.embossStyle); }
            w.DrawCheckboxControl("Enable Pencil Sketch", &rs.enableSketch);
            if (rs.enableSketch) { w.DrawFloatControl("Stroke Strength", &rs.sketchStrokeStrength); w.DrawFloatControl("Stroke Length", &rs.sketchStrokeLength); w.DrawFloatControl("Threshold", &rs.sketchThreshold); w.DrawFloatControl("Shadow Level", &rs.sketchShadowLevel); w.DrawFloatControl("Shadows Weight", &rs.sketchShadowsWeight); w.DrawFloatControl("Midtones Weight", &rs.sketchMidtonesWeight); w.DrawFloatControl("Highlights Weight", &rs.sketchHighlightsWeight); }
            w.DrawCheckboxControl("Enable Halftone", &rs.enableHalftone);
            if (rs.enableHalftone) { w.DrawFloatControl("Scale", &rs.halftoneScale); w.DrawFloatControl("Contrast", &rs.halftoneContrast); TexPicker(context, w, "Pattern Texture", rs.halftonePath, [&](const std::string& p) { if (rs.halftoneTex) context.safeDeleteQueue.push_back(rs.halftoneTex); rs.halftonePath = p; rs.halftoneTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; }); }
            w.DrawCheckboxControl("Enable Screen Refraction", &rs.enableScreenRefraction);
            if (rs.enableScreenRefraction) { w.DrawFloatControl("Refraction Strength", &rs.refractionStrength); w.DrawFloatControl("Refraction Speed", &rs.refractionSpeed); }
            w.DrawCheckboxControl("Enable Outline", &rs.enableOutline);
            if (rs.enableOutline) {
                w.DrawIntControl("Outline Mode", &rs.outlineMode); w.DrawColorControl("Outline Color", Col3(rs.outlineColor)); w.DrawFloatControl("Thickness", &rs.outlineThickness);
                w.DrawFloatControl("Depth Threshold", &rs.outlineThresholdDepth); w.DrawFloatControl("Normal Threshold", &rs.outlineThresholdNormal);
                w.DrawCheckboxControl("Enable Outline Jitter", &rs.enableOutlineJitter);
                if (rs.enableOutlineJitter) { w.DrawFloatControl("Jitter FPS", &rs.outlineJitterSpeed); w.DrawFloatControl("Jitter Strength", &rs.outlineJitterStrength); }
            }
            w.DrawCheckboxControl("Enable Cel-Shading (Toon)", &rs.enableCelShading); if (rs.enableCelShading) w.DrawFloatControl("Cel Levels", &rs.celShadingLevels);
            w.DrawCheckboxControl("Enable Kuwahara (Oil Paint)", &rs.enableKuwahara); if (rs.enableKuwahara) w.DrawIntControl("Brush Radius", &rs.kuwaharaRadius);
            w.DrawCheckboxControl("Enable Posterization", &rs.enablePosterization); if (rs.enablePosterization) w.DrawFloatControl("Color Levels", &rs.posterizationLevels);
            w.DrawCheckboxControl("Enable Watercolor Filter", &rs.enableWatercolor); if (rs.enableWatercolor) w.DrawFloatControl("Watercolor Radius", &rs.watercolorRadius);
            w.DrawCheckboxControl("Enable Pointillism", &rs.enablePointillism); if (rs.enablePointillism) { w.DrawFloatControl("Point Size", &rs.pointillismSize); w.DrawFloatControl("Point Density", &rs.pointillismDensity); }
            w.DrawCheckboxControl("Enable Manga Screentones", &rs.enableScreentones); if (rs.enableScreentones) { w.DrawFloatControl("Screentone Size", &rs.screentoneSize); w.DrawFloatControl("Screentone Darkness", &rs.screentoneDarkness); }
            w.DrawCheckboxControl("Enable Voronoi Stained Glass", &rs.enableVoronoi); if (rs.enableVoronoi) w.DrawFloatControl("Voronoi Scale", &rs.voronoiScale);
            w.DrawFloatControl("Object-Space Hatching Scale", &rs.objHatchingScale);
            w.DrawCheckboxControl("Artistic Contour (Rim Light)", &rs.enableRimLight);
            if (rs.enableRimLight) { w.DrawColorControl("Rim Color", Col3(rs.rimColor)); w.DrawFloatControl("Rim Power", &rs.rimPower); w.DrawFloatControl("Rim Thickness", &rs.rimThickness); }
            w.DrawCheckboxControl("Game Boy / 8-bit Palette", &rs.enableGameBoy);
            if (rs.enableGameBoy) { w.DrawColorControl("Color 1", Col3(rs.gbColor1)); w.DrawColorControl("Color 2", Col3(rs.gbColor2)); w.DrawColorControl("Color 3", Col3(rs.gbColor3)); w.DrawColorControl("Color 4", Col3(rs.gbColor4)); }
            w.DrawCheckboxControl("JPEG/MPEG Compression Artifacts", &rs.enableJpegArtifacts); if (rs.enableJpegArtifacts) { w.DrawFloatControl("Block Size", &rs.jpegBlockSize); w.DrawFloatControl("Quality", &rs.jpegQuality); }
            w.DrawCheckboxControl("Screen Tear / Glitch Slice", &rs.enableScreenTear); if (rs.enableScreenTear) { w.DrawFloatControl("Tear Frequency", &rs.screenTearFrequency); w.DrawFloatControl("Tear Intensity", &rs.screenTearIntensity); }
            w.DrawCheckboxControl("Speed Lines (Anime)", &rs.enableSpeedLines); if (rs.enableSpeedLines) w.DrawFloatControl("Speed Intensity", &rs.speedLinesIntensity);
            w.DrawCheckboxControl("Heat Shimmer (Mirage)", &rs.enableHeatShimmer); if (rs.enableHeatShimmer) w.DrawFloatControl("Heat Intensity", &rs.heatIntensity);
            w.DrawCheckboxControl("Vector Field Flow", &rs.enableVectorFlow);
            if (rs.enableVectorFlow) { w.DrawFloatControl("Flow Strength", &rs.vectorFlowStrength); w.DrawFloatControl("Texture Scale", &rs.vectorFieldScale); TexPicker(context, w, "Vector Texture", rs.vectorTexPath, [&](const std::string& p) { rs.vectorTexPath = p; rs.vectorTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; }); }
            w.DrawCheckboxControl("Temporal Echo (Psychedelic)", &rs.enableTemporalEcho); if (rs.enableTemporalEcho) w.DrawFloatControl("Echo Fade Speed", &rs.echoFade);
            w.DrawCheckboxControl("Screen-Space Canvas", &rs.enableCanvas); if (rs.enableCanvas) w.DrawFloatControl("Canvas Bump", &rs.canvasIntensity);
            w.DrawCheckboxControl("Ink Bleed", &rs.enableInkBleed); if (rs.enableInkBleed) w.DrawFloatControl("Ink Radius", &rs.inkRadius);
            w.DrawCheckboxControl("ASCII Art", &rs.enableAscii); if (rs.enableAscii) { w.DrawIntControl("ASCII Pattern", &rs.asciiMode); w.DrawFloatControl("ASCII Scale", &rs.asciiScale); }
            w.DrawCheckboxControl("Optical Soup (Edge Bleed)", &rs.enableOpticalSoup); if (rs.enableOpticalSoup) w.DrawFloatControl("Bleed Radius", &rs.opticalSoupRadius);
            w.DrawCheckboxControl("Pixel Sorting", &rs.enablePixelSort);
            if (rs.enablePixelSort) { w.DrawFloatControl("Sort Luma Threshold", &rs.pixelSortThreshold); w.DrawFloatControl("Sort Angle", &rs.pixelSortAngle); w.DrawFloatControl("Sort Length", &rs.pixelSortLength); w.DrawFloatControl("Sort Speed", &rs.pixelSortTime); }
            w.DrawCheckboxControl("Datamoshing", &rs.enableDatamosh); if (rs.enableDatamosh) w.DrawFloatControl("Mosh Tolerance", &rs.datamoshThreshold);
            w.DrawCheckboxControl("Impact Frame", &rs.enableImpactFrame);
            if (rs.enableImpactFrame) { w.DrawFloatControl("Impact Lines", &rs.impactSize); w.DrawFloatControl("Impact Power", &rs.impactPower); w.DrawFloatControl("Impact Speed", &rs.impactTime); }
            w.DrawCheckboxControl("Smear / Ribbon Trails", &rs.enableSmearTrails); if (rs.enableSmearTrails) { w.DrawFloatControl("Smear Length", &rs.smearLength); w.DrawFloatControl("Smear Luma Threshold", &rs.smearThreshold); }
            w.DrawCheckboxControl("World Curvature (Inception)", &rs.enableWorldCurve); if (rs.enableWorldCurve) w.DrawFloatControl("Curve Amount", &rs.curveAmount);
            w.DrawCheckboxControl("Environment Breathing", &rs.enableBreathing); if (rs.enableBreathing) { w.DrawFloatControl("Breath Amplitude", &rs.breathAmplitude); w.DrawFloatControl("Breath Speed", &rs.breathSpeed); }
            w.DrawCheckboxControl("Gravitational Lensing", &rs.enableGravityLensing); if (rs.enableGravityLensing) w.DrawFloatControl("Mass", &rs.gravityMass);
            w.DrawCheckboxControl("Micro-facet Glitter", &rs.enableGlitter); if (rs.enableGlitter) w.DrawFloatControl("Glitter Threshold", &rs.glitterThreshold);
            w.DrawCheckboxControl("Caustics", &rs.enableCaustics);
            if (rs.enableCaustics) { w.DrawFloatControl("Caustics Speed", &rs.causticsSpeed); w.DrawFloatControl("Caustics Scale", &rs.causticsScale); w.DrawFloatControl("Caustics Strength", &rs.causticsStrength); TexPicker(context, w, "Caustics Texture", rs.causticsTexPath, [&](const std::string& p) { rs.causticsTexPath = p; rs.causticsTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; }); }
            w.DrawCheckboxControl("Invert Colors", &rs.enableColorInvert);
            w.DrawCheckboxControl("False Color (Luma)", &rs.enableFalseColor);
            w.DrawCheckboxControl("Depth View", &rs.enableDepthView);

            // ---------------- Optimization ----------------
            Header(w, "Optimization");
            w.DrawIntControl("Adaptive Shading (VRS)", &rs.vrsMode);
            w.DrawFloatControl("Anisotropy", &BurnhopeTexture::GlobalAnisotropy);
            if (context.currentScenePath.empty()) w.Text("Save scene first to apply anisotropy!", {1.0f, 0.6f, 0.0f, 1.0f});
            w.DrawCheckboxControl("Enable TAA", &rs.enableTAA); if (rs.enableTAA) w.DrawFloatControl("TAA Blend Factor", &rs.taaBlendFactor);
            w.DrawCheckboxControl("Enable CAS (Sharpen)", &rs.enableCAS); if (rs.enableCAS) w.DrawFloatControl("CAS Sharpness", &rs.casSharpness);
            w.DrawCheckboxControl("Enable CMAA", &rs.enableCMAA);
            w.DrawCheckboxControl("Enable Dithering", &rs.enableDithering);
            if (rs.enableDithering) {
                w.DrawIntControl("Dither Mode", &rs.ditherMode); w.DrawFloatControl("Pixel Scale", &rs.ditherScale);
                if (rs.ditherMode == 0) w.DrawFloatControl("Dither Strength", &rs.ditherStrength);
                else {
                    if (rs.ditherMode == 2) TexPicker(context, w, "Dither Pattern", rs.ditherTexPath, [&](const std::string& p) { if (rs.ditherTex) context.safeDeleteQueue.push_back(rs.ditherTex); rs.ditherTexPath = p; rs.ditherTex = p.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, p); context.needsRebuild = true; });
                    w.DrawColorControl("Shadow Color", Col3(rs.ditherShadowColor)); w.DrawColorControl("Midtone Color", Col3(rs.ditherMidColor)); w.DrawColorControl("Highlight Color", Col3(rs.ditherHighlightColor));
                }
            }
            w.DrawCheckboxControl("Enable Motion Blur", &rs.enableMotionBlur);
            if (rs.enableMotionBlur) { w.DrawFloatControl("MB Strength", &rs.mbStrength); w.DrawFloatControl("Artistic Trails", &rs.mbTrails); }

            // ---------------- Psychological & Horror ----------------
            Header(w, "Psychological & Horror");
            w.DrawCheckboxControl("Eye Blinking", &rs.enableBlinking); if (rs.enableBlinking) w.DrawFloatControl("Blink Freq", &rs.blinkFrequency);
            w.DrawCheckboxControl("Eye Floaters", &rs.enableFloaters); if (rs.enableFloaters) w.DrawFloatControl("Floaters Opacity", &rs.floatersOpacity);
            w.DrawCheckboxControl("Stress Eye Jitter (Nystagmus)", &rs.enableNystagmus); if (rs.enableNystagmus) w.DrawFloatControl("Severity", &rs.nystagmusSeverity);
            w.DrawCheckboxControl("Purkinje Tree (Veins)", &rs.enablePurkinje);
            if (rs.enablePurkinje) { w.DrawFloatControl("Veins Scale", &rs.purkinjeScale); w.DrawFloatControl("Thickness", &rs.purkinjeThickness); w.DrawFloatControl("Speed", &rs.purkinjeSpeed); w.DrawColorControl("Blood Color", Col3(rs.purkinjeColor)); }
            w.DrawCheckboxControl("Rod/Cone Transition", &rs.enableRodCone); if (rs.enableRodCone) { w.DrawFloatControl("Darkness Threshold", &rs.rodConeThreshold); w.DrawColorControl("Night Vision Tint", Col3(rs.rodConeColor)); }
            w.DrawCheckboxControl("Time Stutter", &rs.enableTimeStutter); if (rs.enableTimeStutter) { w.DrawFloatControl("Severity", &rs.stutterSeverity); w.DrawFloatControl("Speed", &rs.stutterSpeed); }
            w.DrawCheckboxControl("Melting Walls", &rs.enableMelting); if (rs.enableMelting) { w.DrawFloatControl("Melt Speed", &rs.meltSpeed); w.DrawFloatControl("Threshold", &rs.meltThreshold); w.DrawFloatControl("Noise Scale", &rs.meltNoiseScale); }
            w.DrawCheckboxControl("Hollow-Face Illusion", &rs.enableHollowFace); if (rs.enableHollowFace) w.DrawFloatControl("Depth Amount", &rs.hollowFaceDepth);
            w.DrawCheckboxControl("Trypophobia Generator", &rs.enableTrypo); if (rs.enableTrypo) w.DrawFloatControl("Hole Scale", &rs.trypoScale);
            w.DrawCheckboxControl("Depth Parallax Eyes", &rs.enableParallaxEye);
            w.DrawCheckboxControl("Negative Light (Anti-Light)", &rs.enableAntiLight);
            w.DrawCheckboxControl("Inside Screen Smudges", &rs.enableInsideSmudges); if (rs.enableInsideSmudges) w.DrawFloatControl("Intensity", &rs.smudgeIntensity);
            w.DrawCheckboxControl("Frame Buffer Haunting", &rs.enableHaunting); if (rs.enableHaunting) w.DrawFloatControl("Trail Length", &rs.hauntingTrail);
            w.DrawCheckboxControl("Navier-Stokes Fluid Drops", &rs.enableFluidLens); if (rs.enableFluidLens) w.DrawFloatControl("Viscosity", &rs.fluidViscosity);
        }

        template <typename Setter>
        void TexPicker(UIContext& context, ui::UIWidgets& w, const std::string& label, const std::string& currentPath, Setter setter) {
            w.PushID(label);
            w.Text(label);
            std::string btnLabel = currentPath.empty() ? "None" : std::filesystem::path(currentPath).filename().string();
            if (w.Button(btnLabel, {220, 24})) w.OpenPopup("TexPickerPopup");
            if (w.BeginPopup("TexPickerPopup")) {
                if (w.Selectable("None", false)) { setter(""); w.CloseCurrentPopup(); }
                for (const auto& t : context.GetProjectAssets({".png", ".jpg", ".jpeg"})) {
                    if (w.Selectable(std::filesystem::path(t).filename().string(), false)) { setter(t); w.CloseCurrentPopup(); }
                }
                w.EndPopup();
            }
            w.PopID();
        }

        void SaveRenderSettings(UIContext& context) {
            std::string path = context.projectDirectory.string() + "/rendersettings.json";
            json j;
            auto& rs = context.renderSettings;

            j["rtMaxBounces"] = rs.rtMaxBounces;
            j["enableRTReflections"] = rs.enableRTReflections;
            j["enableRadianceCascades"] = rs.enableRadianceCascades;
            j["rcProbeGridX"] = rs.rcProbeGridX;
            j["rcProbeGridY"] = rs.rcProbeGridY;
            j["rcProbeGridZ"] = rs.rcProbeGridZ;
            j["rcBaseRayLength"] = rs.rcBaseRayLength;
            j["rcOctaSize"] = rs.rcOctaSize;

            j["enableSSAO"] = rs.enableSSAO;
            j["ssaoRadius"] = rs.ssaoRadius;
            j["ssaoBias"] = rs.ssaoBias;
            j["ssaoIntensity"] = rs.ssaoIntensity;
            j["ssaoPower"] = rs.ssaoPower;

            j["enableSSGI"] = rs.enableSSGI;
            j["ssgiRayCount"] = rs.ssgiRayCount;
            j["ssgiStepSize"] = rs.ssgiStepSize;
            j["ssgiThickness"] = rs.ssgiThickness;
            j["blurRange"] = rs.blurRange;

            j["enableContactShadows"] = rs.enableContactShadows;
            j["contactShadowLength"] = rs.contactShadowLength;
            j["contactShadowThickness"] = rs.contactShadowThickness;
            j["contactShadowSteps"] = rs.contactShadowSteps;

            j["autoExposure"] = rs.autoExposure;
            j["manualExposure"] = rs.manualExposure;
            j["exposureCompensation"] = rs.exposureCompensation;
            j["minBrightness"] = rs.minBrightness;
            j["maxBrightness"] = rs.maxBrightness;
            j["contrast"] = rs.contrast;
            j["saturation"] = rs.saturation;
            j["temperature"] = rs.temperature;
            j["gamma"] = rs.gamma;

            j["enableLensDistortion"] = rs.enableLensDistortion;
            j["lensDistortionStrength"] = rs.lensDistortionStrength;
            j["enableLensDirt"] = rs.enableLensDirt;
            j["lensDirtIntensity"] = rs.lensDirtIntensity;
            j["enableDithering"] = rs.enableDithering;
            j["ditherStrength"] = rs.ditherStrength;
            j["enableScreenRefraction"] = rs.enableScreenRefraction;
            j["refractionStrength"] = rs.refractionStrength;
            j["refractionSpeed"] = rs.refractionSpeed;
            j["anisotropicFiltering"] = BurnhopeTexture::GlobalAnisotropy;

            j["enableRetroCRT"] = rs.enableRetroCRT;
            j["crtScanlines"] = rs.crtScanlines;
            j["glitchIntensity"] = rs.glitchIntensity;
            j["vhsNoise"] = rs.vhsNoise;
            j["pixelation"] = rs.pixelation;
            j["enableVertexJitter"] = rs.enableVertexJitter;
            j["vertexJitterResolution"] = rs.vertexJitterResolution;
            j["enablePosterization"] = rs.enablePosterization;
            j["posterizationLevels"] = rs.posterizationLevels;
            j["enableKuwahara"] = rs.enableKuwahara;
            j["kuwaharaRadius"] = rs.kuwaharaRadius;
            j["enableCelShading"] = rs.enableCelShading;
            j["celShadingLevels"] = rs.celShadingLevels;
            j["enableVoronoi"] = rs.enableVoronoi;
            j["voronoiScale"] = rs.voronoiScale;
            j["objHatchingScale"] = rs.objHatchingScale;

            j["vrsMode"] = rs.vrsMode;
            j["enableOutline"] = rs.enableOutline;
            j["outlineMode"] = rs.outlineMode;
            j["outlineThickness"] = rs.outlineThickness;
            j["outlineThresholdDepth"] = rs.outlineThresholdDepth;
            j["outlineThresholdNormal"] = rs.outlineThresholdNormal;
            j["outlineColor"] = {rs.outlineColor[0], rs.outlineColor[1], rs.outlineColor[2]};
            j["enableOutlineJitter"] = rs.enableOutlineJitter;
            j["outlineJitterSpeed"] = rs.outlineJitterSpeed;
            j["outlineJitterStrength"] = rs.outlineJitterStrength;

            j["enableVignette"] = rs.enableVignette;
            j["vignetteIntensity"] = rs.vignetteIntensity;
            j["enableChromaticAberration"] = rs.enableChromaticAberration;
            j["caIntensity"] = rs.caIntensity;

            j["enableEdgeDetect"] = rs.enableEdgeDetect;
            j["edgeWidth"] = rs.edgeWidth;
            j["edgeBrightness"] = rs.edgeBrightness;
            j["edgeGamma"] = rs.edgeGamma;
            j["edgeBlur"] = rs.edgeBlur;
            j["edgeColorMode"] = rs.edgeColorMode;
            j["edgeCustomColor"] = {rs.edgeCustomColor[0], rs.edgeCustomColor[1], rs.edgeCustomColor[2]};
            j["enableEmboss"] = rs.enableEmboss;
            j["embossStrength"] = rs.embossStrength;
            j["embossAngle"] = rs.embossAngle;
            j["embossStyle"] = rs.embossStyle;
            j["enableSketch"] = rs.enableSketch;
            j["sketchStrokeStrength"] = rs.sketchStrokeStrength;
            j["sketchStrokeLength"] = rs.sketchStrokeLength;
            j["sketchThreshold"] = rs.sketchThreshold;
            j["sketchShadowLevel"] = rs.sketchShadowLevel;
            j["sketchShadowsWeight"] = rs.sketchShadowsWeight;
            j["sketchMidtonesWeight"] = rs.sketchMidtonesWeight;
            j["sketchHighlightsWeight"] = rs.sketchHighlightsWeight;

            j["lensDirtPath"] = rs.lensDirtPath;
            j["enableHalftone"] = rs.enableHalftone;
            j["halftoneScale"] = rs.halftoneScale;
            j["halftoneContrast"] = rs.halftoneContrast;
            j["halftonePath"] = rs.halftonePath;
            j["ditherMode"] = rs.ditherMode;
            j["ditherTexPath"] = rs.ditherTexPath;
            j["ditherScale"] = rs.ditherScale;
            j["ditherShadowColor"] = {rs.ditherShadowColor[0], rs.ditherShadowColor[1], rs.ditherShadowColor[2]};
            j["ditherMidColor"] = {rs.ditherMidColor[0], rs.ditherMidColor[1], rs.ditherMidColor[2]};
            j["ditherHighlightColor"] = {rs.ditherHighlightColor[0], rs.ditherHighlightColor[1], rs.ditherHighlightColor[2]};
            j["mbTrails"] = rs.mbTrails;
            j["enableTexWarp"] = rs.enableTexWarp;
            j["texWarpStrength"] = rs.texWarpStrength;
            j["texWarpSpeed"] = rs.texWarpSpeed;
            j["enableVtxWarp"] = rs.enableVtxWarp;
            j["vtxWarpStrength"] = rs.vtxWarpStrength;
            j["vtxWarpSpeed"] = rs.vtxWarpSpeed;
            j["vtxWarpScale"] = rs.vtxWarpScale;
            j["enableColorComp"] = rs.enableColorComp;
            j["colorCompLevels"] = rs.colorCompLevels;
            j["enableCMAA"] = rs.enableCMAA;
            j["palettePath"] = rs.palettePath;
            j["enableShadowRamp"] = rs.enableShadowRamp;
            j["shadowRampColor1"] = {rs.shadowRampColor1[0], rs.shadowRampColor1[1], rs.shadowRampColor1[2]};
            j["shadowRampColor2"] = {rs.shadowRampColor2[0], rs.shadowRampColor2[1], rs.shadowRampColor2[2]};

            j["enableOpticalSoup"] = rs.enableOpticalSoup;
            j["opticalSoupRadius"] = rs.opticalSoupRadius;
            j["enableDatamosh"] = rs.enableDatamosh;
            j["datamoshThreshold"] = rs.datamoshThreshold;
            j["enableAscii"] = rs.enableAscii;
            j["asciiMode"] = rs.asciiMode;
            j["asciiScale"] = rs.asciiScale;
            j["enablePixelSort"] = rs.enablePixelSort;
            j["pixelSortThreshold"] = rs.pixelSortThreshold;
            j["pixelSortAngle"] = rs.pixelSortAngle;
            j["pixelSortLength"] = rs.pixelSortLength;
            j["pixelSortTime"] = rs.pixelSortTime;
            j["enableImpactFrame"] = rs.enableImpactFrame;
            j["impactSize"] = rs.impactSize; j["impactPower"] = rs.impactPower; j["impactTime"] = rs.impactTime;
            j["enableSmearTrails"] = rs.enableSmearTrails;
            j["smearLength"] = rs.smearLength; j["smearThreshold"] = rs.smearThreshold;
            j["enableRimLight"] = rs.enableRimLight;
            j["rimColor"] = {rs.rimColor[0], rs.rimColor[1], rs.rimColor[2]};
            j["rimPower"] = rs.rimPower; j["rimThickness"] = rs.rimThickness;

            j["enableScreentones"] = rs.enableScreentones; j["screentoneSize"] = rs.screentoneSize; j["screentoneDarkness"] = rs.screentoneDarkness;
            j["enableWatercolor"] = rs.enableWatercolor; j["watercolorRadius"] = rs.watercolorRadius;
            j["enablePointillism"] = rs.enablePointillism; j["pointillismSize"] = rs.pointillismSize; j["pointillismDensity"] = rs.pointillismDensity;
            j["enableTiltShift"] = rs.enableTiltShift; j["tiltShiftMode"] = rs.tiltShiftMode; j["tiltShiftAmount"] = rs.tiltShiftAmount; j["tiltShiftFalloff"] = rs.tiltShiftFalloff;
            j["enableLensBreathing"] = rs.enableLensBreathing; j["lensBreathingScale"] = rs.lensBreathingScale;
            j["enableStarFilter"] = rs.enableStarFilter; j["starFilterThreshold"] = rs.starFilterThreshold; j["starFilterLength"] = rs.starFilterLength;
            j["enableLightLeaks"] = rs.enableLightLeaks; j["lightLeakIntensity"] = rs.lightLeakIntensity;
            j["enableJpegArtifacts"] = rs.enableJpegArtifacts; j["jpegBlockSize"] = rs.jpegBlockSize; j["jpegQuality"] = rs.jpegQuality;
            j["enableGameBoy"] = rs.enableGameBoy;
            j["gbColor1"] = {rs.gbColor1[0], rs.gbColor1[1], rs.gbColor1[2]}; j["gbColor2"] = {rs.gbColor2[0], rs.gbColor2[1], rs.gbColor2[2]};
            j["gbColor3"] = {rs.gbColor3[0], rs.gbColor3[1], rs.gbColor3[2]}; j["gbColor4"] = {rs.gbColor4[0], rs.gbColor4[1], rs.gbColor4[2]};
            j["enableScreenTear"] = rs.enableScreenTear; j["screenTearFrequency"] = rs.screenTearFrequency; j["screenTearIntensity"] = rs.screenTearIntensity;

            j["enableSpeedLines"] = rs.enableSpeedLines; j["speedLinesIntensity"] = rs.speedLinesIntensity;
            j["enableColorSplash"] = rs.enableColorSplash; j["splashHue"] = rs.splashHue; j["splashRange"] = rs.splashRange;
            j["enableHeatShimmer"] = rs.enableHeatShimmer; j["heatIntensity"] = rs.heatIntensity;
            j["enableFrost"] = rs.enableFrost; j["frostIntensity"] = rs.frostIntensity;
            j["enableWaterDrops"] = rs.enableWaterDrops; j["dropRefraction"] = rs.dropRefraction;
            j["enableTemporalEcho"] = rs.enableTemporalEcho; j["echoFade"] = rs.echoFade;
            j["enableCanvas"] = rs.enableCanvas; j["canvasIntensity"] = rs.canvasIntensity;
            j["enableInkBleed"] = rs.enableInkBleed; j["inkRadius"] = rs.inkRadius;
            j["enableGlitter"] = rs.enableGlitter; j["glitterThreshold"] = rs.glitterThreshold;
            j["enableCaustics"] = rs.enableCaustics; j["causticsSpeed"] = rs.causticsSpeed; j["causticsScale"] = rs.causticsScale; j["causticsStrength"] = rs.causticsStrength;
            j["enableWorldCurve"] = rs.enableWorldCurve; j["curveAmount"] = rs.curveAmount;
            j["enableBreathing"] = rs.enableBreathing; j["breathAmplitude"] = rs.breathAmplitude; j["breathSpeed"] = rs.breathSpeed;

            j["enableBloom"] = rs.enableBloom;
            j["bloomThreshold"] = rs.bloomThreshold;
            j["bloomIntensity"] = rs.bloomIntensity;
            j["bloomBlurIterations"] = rs.bloomBlurIterations;

            j["enableBlinking"] = rs.enableBlinking; j["blinkFrequency"] = rs.blinkFrequency;
            j["enableFloaters"] = rs.enableFloaters; j["floatersOpacity"] = rs.floatersOpacity;
            j["enableTimeStutter"] = rs.enableTimeStutter; j["stutterSeverity"] = rs.stutterSeverity; j["stutterSpeed"] = rs.stutterSpeed;
            j["enableHollowFace"] = rs.enableHollowFace; j["hollowFaceDepth"] = rs.hollowFaceDepth;
            j["enableMelting"] = rs.enableMelting; j["meltSpeed"] = rs.meltSpeed; j["meltThreshold"] = rs.meltThreshold; j["meltNoiseScale"] = rs.meltNoiseScale;
            j["enableAntiLight"] = rs.enableAntiLight;
            j["enableTrypo"] = rs.enableTrypo; j["trypoScale"] = rs.trypoScale;
            j["enableParallaxEye"] = rs.enableParallaxEye;
            j["enableInsideSmudges"] = rs.enableInsideSmudges; j["smudgeIntensity"] = rs.smudgeIntensity;
            j["enableHaunting"] = rs.enableHaunting; j["hauntingTrail"] = rs.hauntingTrail;
            j["enableNystagmus"] = rs.enableNystagmus; j["nystagmusSeverity"] = rs.nystagmusSeverity;
            j["enablePurkinje"] = rs.enablePurkinje; j["purkinjeScale"] = rs.purkinjeScale;
            j["enableRodCone"] = rs.enableRodCone; j["rodConeThreshold"] = rs.rodConeThreshold; j["rodConeColor"] = {rs.rodConeColor[0], rs.rodConeColor[1], rs.rodConeColor[2]};
            j["enableFluidLens"] = rs.enableFluidLens; j["fluidViscosity"] = rs.fluidViscosity;
            j["purkinjeIntensity"] = rs.purkinjeIntensity; j["purkinjeColor"] = {rs.purkinjeColor[0], rs.purkinjeColor[1], rs.purkinjeColor[2]}; j["purkinjeThickness"] = rs.purkinjeThickness; j["purkinjeSpeed"] = rs.purkinjeSpeed;
            j["speedLinesCount"] = rs.speedLinesCount; j["speedLinesLength"] = rs.speedLinesLength;

            j["enableRecursiveFeedback"] = rs.enableRecursiveFeedback; j["feedbackZoom"] = rs.feedbackZoom; j["feedbackAngle"] = rs.feedbackAngle;
            j["enableAnalogNoise"] = rs.enableAnalogNoise; j["analogSyncLoss"] = rs.analogSyncLoss;
            j["enableScanlineMoire"] = rs.enableScanlineMoire; j["moireScale"] = rs.moireScale;
            j["enableTunnelVision"] = rs.enableTunnelVision; j["tunnelIntensity"] = rs.tunnelIntensity;
            j["enableAfterimage"] = rs.enableAfterimage; j["afterimageFade"] = rs.afterimageFade;
            j["enableTemporalBleed"] = rs.enableTemporalBleed; j["bleedSpeed"] = rs.bleedSpeed;
            j["enableFluidSim"] = rs.enableFluidSim; j["fluidSpeed"] = rs.fluidSpeed;
            j["enableCMYK"] = rs.enableCMYK; j["cmykOffset"] = rs.cmykOffset;
            j["enableCondensation"] = rs.enableCondensation; j["condensationAmount"] = rs.condensationAmount;
            j["enableDustMotes"] = rs.enableDustMotes; j["dustIntensity"] = rs.dustIntensity;
            j["enableEctoplasm"] = rs.enableEctoplasm; j["ectoplasmColor"] = {rs.ectoplasmColor[0], rs.ectoplasmColor[1], rs.ectoplasmColor[2]};
            j["enableRollingShutter"] = rs.enableRollingShutter; j["rollingShutterSpeed"] = rs.rollingShutterSpeed;
            j["enableSlitScan"] = rs.enableSlitScan; j["slitScanSpeed"] = rs.slitScanSpeed;
            j["enableReactionDiffusion"] = rs.enableReactionDiffusion; j["rdSpeed"] = rs.rdSpeed;
            j["enableDroste"] = rs.enableDroste; j["drosteScale"] = rs.drosteScale;
            j["enableCrosshatchLight"] = rs.enableCrosshatchLight; j["bayerWorldSpace"] = rs.bayerWorldSpace;
            j["vectorTexPath"] = rs.vectorTexPath;
            j["causticsTexPath"] = rs.causticsTexPath;
            j["canvasTexPath"] = rs.canvasTexPath;
            j["enableLensFlares"] = rs.enableLensFlares;
            j["flareIntensity"] = rs.flareIntensity;
            j["ghostDispersal"] = rs.ghostDispersal;
            j["ghosts"] = rs.ghosts;
            j["enableAnamorphic"] = rs.enableAnamorphic;

            j["enableFilmGrain"] = rs.enableFilmGrain;
            j["grainIntensity"] = rs.grainIntensity;
            j["enableSharpen"] = rs.enableSharpen;
            j["sharpenIntensity"] = rs.sharpenIntensity;

            j["tonemapper"] = rs.tonemapper;
            j["cgShadows"] = {rs.cgShadows[0], rs.cgShadows[1], rs.cgShadows[2]};
            j["cgMidtones"] = {rs.cgMidtones[0], rs.cgMidtones[1], rs.cgMidtones[2]};
            j["cgHighlights"] = {rs.cgHighlights[0], rs.cgHighlights[1], rs.cgHighlights[2]};

            j["enableDoF"] = rs.enableDoF;
            j["focusDistance"] = rs.focusDistance;
            j["focusRange"] = rs.focusRange;
            j["bokehSize"] = rs.bokehSize;
            j["bokehShape"] = rs.bokehShape;
            j["bokehAngle"] = rs.bokehAngle;

            j["enableMotionBlur"] = rs.enableMotionBlur;
            j["mbStrength"] = rs.mbStrength;

            j["enableFog"] = rs.enableFog;
            j["fogDensity"] = rs.fogDensity;
            j["fogHeightFalloff"] = rs.fogHeightFalloff;
            j["fogBaseHeight"] = rs.fogBaseHeight;
            j["inscatterPower"] = rs.inscatterPower;
            j["inscatterIntensity"] = rs.inscatterIntensity;

            j["fogColor"] = { rs.fogColor[0], rs.fogColor[1], rs.fogColor[2] };
            j["inscatterColor"] = { rs.inscatterColor[0], rs.inscatterColor[1], rs.inscatterColor[2] };
            j["skyZenithColor"] = { rs.skyZenithColor[0], rs.skyZenithColor[1], rs.skyZenithColor[2] };
            j["skyHorizonColor"] = { rs.skyHorizonColor[0], rs.skyHorizonColor[1], rs.skyHorizonColor[2] };

            j["sunSize"] = rs.sunSize;
            j["sunGlow"] = rs.sunGlow;
            j["sunGlowSize"] = rs.sunGlowSize;

            std::ofstream file(path);
            if (file.is_open()) {
                file << j.dump(4);
            }
        }
    };
}
