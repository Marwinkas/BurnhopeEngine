#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace burnhope {
    using json = nlohmann::json;

    class PropertiesWindow : public IUIWindow {
    public:
        PropertiesWindow() : IUIWindow("Properties") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);
            
            if (ImGui::BeginTabBar("PropsTabs")) {
                if (ImGui::BeginTabItem("Render")) {
                    DrawRenderSettings(context.renderSettings);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("💾 SAVE SETTINGS", ImVec2(-1, 40))) {
                SaveRenderSettings(context);
            }

            ImGui::End();
        }

    private:
        void DrawRenderSettings(RenderSettings& renderSettings) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Render & Post-Processing Settings");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTabBar("PP_Tabs")) {
                
                // --- GLOBAL ILLUMINATION ---
                if (ImGui::BeginTabItem("Global Illumination")) {
                    if (ImGui::CollapsingHeader("SSGI (Global Illumination)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable SSGI", &renderSettings.enableSSGI);
                        if (renderSettings.enableSSGI) {
                            ImGui::SliderInt("Ray Count", &renderSettings.ssgiRayCount, 1, 32);
                            ImGui::SliderFloat("Step Size", &renderSettings.ssgiStepSize, 0.05f, 2.0f);
                            ImGui::SliderFloat("Thickness", &renderSettings.ssgiThickness, 0.01f, 2.0f);
                            ImGui::SliderInt("Blur Range", &renderSettings.blurRange, 1, 10);
                        }
                    }
                    if (ImGui::CollapsingHeader("Radiance Cascades (RT GI)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable RT GI", &renderSettings.enableRadianceCascades);
                        if (renderSettings.enableRadianceCascades) {
                            ImGui::SliderInt("Probe Grid X", &renderSettings.rcProbeGridX, 1, 32);
                            ImGui::SliderInt("Probe Grid Y", &renderSettings.rcProbeGridY, 1, 32);
                            ImGui::SliderInt("Probe Grid Z", &renderSettings.rcProbeGridZ, 1, 32);
                            ImGui::SliderFloat("Ray Length", &renderSettings.rcBaseRayLength, 0.1f, 5.0f);
                            ImGui::SliderInt("Octahedron Size", &renderSettings.rcOctaSize, 4, 16);
                        }
                    }
                    ImGui::EndTabItem();
                }

                // --- REFLECTIONS ---
                if (ImGui::BeginTabItem("Reflections")) {
                    if (ImGui::CollapsingHeader("Ray Traced Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable RT Reflections", &renderSettings.enableRTReflections);
                        if (renderSettings.enableRTReflections) {
                            ImGui::SliderInt("Max Bounces", &renderSettings.rtMaxBounces, 1, 5);
                        }
                    }
                    ImGui::EndTabItem();
                }

                // --- SHADOWS & AO ---
                if (ImGui::BeginTabItem("Shadows & AO")) {
                    if (ImGui::CollapsingHeader("GTAO (Ambient Occlusion)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable GTAO", &renderSettings.enableSSAO);
                        if (renderSettings.enableSSAO) {
                            ImGui::SliderFloat("Radius##ssao", &renderSettings.ssaoRadius, 0.1f, 3.0f);
                            ImGui::SliderFloat("Bias##ssao", &renderSettings.ssaoBias, 0.001f, 0.2f);
                            ImGui::SliderFloat("Intensity##ssao", &renderSettings.ssaoIntensity, 0.1f, 10.0f);
                            ImGui::SliderFloat("Power##ssao", &renderSettings.ssaoPower, 1.0f, 8.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("SSCS (Contact Shadows)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable SSCS", &renderSettings.enableContactShadows);
                        if (renderSettings.enableContactShadows) {
                            ImGui::SliderFloat("Ray Length", &renderSettings.contactShadowLength, 0.01f, 0.5f);
                            ImGui::SliderInt("Ray Steps", &renderSettings.contactShadowSteps, 4, 64);
                            ImGui::SliderFloat("Ray Thickness", &renderSettings.contactShadowThickness, 0.01f, 0.5f);
                        }
                    }
                    ImGui::EndTabItem();
                }

                // --- CAMERA & LENS ---
                if (ImGui::BeginTabItem("Camera & Lens")) {
                    if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Auto Exposure", &renderSettings.autoExposure);
                        if (renderSettings.autoExposure) {
                            ImGui::SliderFloat("Compensation", &renderSettings.exposureCompensation, 0.1f, 5.0f);
                            ImGui::SliderFloat("Min Brightness", &renderSettings.minBrightness, 0.01f, 2.0f);
                            ImGui::SliderFloat("Max Brightness", &renderSettings.maxBrightness, 1.0f, 10.0f);
                        }
                        else {
                            ImGui::SliderFloat("Manual Exp", &renderSettings.manualExposure, 0.1f, 10.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable DoF", &renderSettings.enableDoF);
                        if (renderSettings.enableDoF) {
                            ImGui::SliderFloat("Focus Dist", &renderSettings.focusDistance, 0.1f, 100.0f);
                            ImGui::SliderFloat("Focus Range", &renderSettings.focusRange, 0.1f, 50.0f);
                            ImGui::SliderFloat("Bokeh Size", &renderSettings.bokehSize, 0.0f, 10.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("Bloom & Lens Flares", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable Bloom", &renderSettings.enableBloom);
                        if (renderSettings.enableBloom) {
                            ImGui::SliderFloat("Threshold##bloom", &renderSettings.bloomThreshold, 0.0f, 5.0f);
                            ImGui::SliderFloat("Intensity##bloom", &renderSettings.bloomIntensity, 0.0f, 5.0f);
                            ImGui::SliderInt("Blur Iterations", &renderSettings.bloomBlurIterations, 1, 15);
                        }
                        ImGui::Separator();
                        ImGui::Checkbox("Enable Lens Flares", &renderSettings.enableLensFlares);
                        if (renderSettings.enableLensFlares) {
                            ImGui::SliderFloat("Flare Intensity", &renderSettings.flareIntensity, 0.0f, 5.0f);
                            ImGui::SliderFloat("Ghost Dispersal", &renderSettings.ghostDispersal, 0.01f, 1.0f);
                            ImGui::SliderInt("Ghosts Count", &renderSettings.ghosts, 1, 10);
                        }
                    }
                    if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable Motion Blur", &renderSettings.enableMotionBlur);
                        if (renderSettings.enableMotionBlur)
                            ImGui::SliderFloat("Strength", &renderSettings.mbStrength, 0.0f, 2.0f);
                    }
                    ImGui::EndTabItem();
                }

                // --- ENVIRONMENT ---
                if (ImGui::BeginTabItem("Environment")) {
                    if (ImGui::CollapsingHeader("Procedural Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::ColorEdit3("Zenith Color", renderSettings.skyZenithColor);
                        ImGui::ColorEdit3("Horizon Color", renderSettings.skyHorizonColor);
                        ImGui::SliderFloat("Sun Size", &renderSettings.sunSize, 0.001f, 0.1f);
                        ImGui::SliderFloat("Sun Glow", &renderSettings.sunGlow, 0.0f, 10.0f);
                        ImGui::SliderFloat("Sun Glow Size", &renderSettings.sunGlowSize, 0.01f, 1.0f);
                    }
                    if (ImGui::CollapsingHeader("Atmospheric Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enable Fog", &renderSettings.enableFog);
                        if (renderSettings.enableFog) {
                            ImGui::ColorEdit3("Fog Color", renderSettings.fogColor);
                            ImGui::ColorEdit3("Sun Inscatter Color", renderSettings.inscatterColor);
                            ImGui::SliderFloat("Density", &renderSettings.fogDensity, 0.001f, 0.2f);
                            ImGui::SliderFloat("Height Falloff", &renderSettings.fogHeightFalloff, 0.01f, 1.0f);
                            ImGui::SliderFloat("Base Height", &renderSettings.fogBaseHeight, -50.0f, 50.0f);
                            ImGui::SliderFloat("Sun Inscatter Power", &renderSettings.inscatterPower, 1.0f, 32.0f);
                            ImGui::SliderFloat("Sun Inscatter Int", &renderSettings.inscatterIntensity, 0.0f, 5.0f);
                        }
                    }
                    ImGui::EndTabItem();
                }

                // --- COLOR GRADING ---
                if (ImGui::BeginTabItem("Color Grading")) {
                    if (ImGui::CollapsingHeader("Color Corrections", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::SliderFloat("Contrast", &renderSettings.contrast, 0.5f, 2.0f);
                        ImGui::SliderFloat("Saturation", &renderSettings.saturation, 0.0f, 2.0f);
                        ImGui::SliderFloat("Color Temp (K)", &renderSettings.temperature, 2000.0f, 12000.0f);
                        ImGui::SliderFloat("Gamma", &renderSettings.gamma, 1.0f, 2.8f);
                    }
                    if (ImGui::CollapsingHeader("Screen FX", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Film Grain", &renderSettings.enableFilmGrain);
                        if (renderSettings.enableFilmGrain)
                            ImGui::SliderFloat("Grain Strength", &renderSettings.grainIntensity, 0.0f, 0.2f);
                        
                        ImGui::Checkbox("Vignette", &renderSettings.enableVignette);
                        if (renderSettings.enableVignette)
                            ImGui::SliderFloat("Vignette Intensity", &renderSettings.vignetteIntensity, 0.1f, 2.0f);
                        
                        ImGui::Checkbox("Chromatic Aberration", &renderSettings.enableChromaticAberration);
                        if (renderSettings.enableChromaticAberration)
                            ImGui::SliderFloat("CA Intensity", &renderSettings.caIntensity, 0.001f, 0.55f);
                        
                        ImGui::Checkbox("Enable Sharpen", &renderSettings.enableSharpen);
                        if (renderSettings.enableSharpen)
                            ImGui::SliderFloat("Sharpness", &renderSettings.sharpenIntensity, 0.0f, 2.0f);
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        void SaveRenderSettings(UIContext& context) {
            std::string path = context.projectDirectory.string() + "/rendersettings.json";
            json j;
            auto& rs = context.renderSettings;

            // --- Ray Tracing & GI ---
            j["rtMaxBounces"] = rs.rtMaxBounces;
            j["enableRTReflections"] = rs.enableRTReflections;
            j["enableRadianceCascades"] = rs.enableRadianceCascades;
            j["rcProbeGridX"] = rs.rcProbeGridX;
            j["rcProbeGridY"] = rs.rcProbeGridY;
            j["rcProbeGridZ"] = rs.rcProbeGridZ;
            j["rcBaseRayLength"] = rs.rcBaseRayLength;
            j["rcOctaSize"] = rs.rcOctaSize;

            // --- Shadows & AO ---
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

            // --- Exposure & Color ---
            j["autoExposure"] = rs.autoExposure;
            j["manualExposure"] = rs.manualExposure;
            j["exposureCompensation"] = rs.exposureCompensation;
            j["minBrightness"] = rs.minBrightness;
            j["maxBrightness"] = rs.maxBrightness;
            j["contrast"] = rs.contrast;
            j["saturation"] = rs.saturation;
            j["temperature"] = rs.temperature;
            j["gamma"] = rs.gamma;

            // --- Screen FX ---
            j["enableVignette"] = rs.enableVignette;
            j["vignetteIntensity"] = rs.vignetteIntensity;
            j["enableChromaticAberration"] = rs.enableChromaticAberration;
            j["caIntensity"] = rs.caIntensity;
            
            j["enableBloom"] = rs.enableBloom;
            j["bloomThreshold"] = rs.bloomThreshold;
            j["bloomIntensity"] = rs.bloomIntensity;
            j["bloomBlurIterations"] = rs.bloomBlurIterations;
            
            j["enableLensFlares"] = rs.enableLensFlares;
            j["flareIntensity"] = rs.flareIntensity;
            j["ghostDispersal"] = rs.ghostDispersal;
            j["ghosts"] = rs.ghosts;

            j["enableFilmGrain"] = rs.enableFilmGrain;
            j["grainIntensity"] = rs.grainIntensity;
            j["enableSharpen"] = rs.enableSharpen;
            j["sharpenIntensity"] = rs.sharpenIntensity;

            // --- Camera ---
            j["enableDoF"] = rs.enableDoF;
            j["focusDistance"] = rs.focusDistance;
            j["focusRange"] = rs.focusRange;
            j["bokehSize"] = rs.bokehSize;
            
            j["enableMotionBlur"] = rs.enableMotionBlur;
            j["mbStrength"] = rs.mbStrength;

            // --- Environment ---
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