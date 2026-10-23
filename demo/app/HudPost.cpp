#include "app/HudBuild.hpp"

#include "gpu_scene/SceneObject.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Label.hpp"

namespace burnhope {

void hudBuildPost(Canvas& canvas, HudContext* hud, uint16_t details, UiPaint hidden) {
    auto hiddenPaint = [hidden]() { return hidden; };
    uint16_t panel = details;
    auto slide = [&](uint16_t host, const char* label, float* dst, float def, float minV, float maxV) {
        hud->bind.slide(*canvas.state, host, label, dst, def, minV, maxV);
    };
    auto tick = [&](const char* label, auto* dst, bool on) {
        hud->bind.check(*canvas.state, panel, label, dst, on);
    };
    auto group = [&](uint16_t parent) {
        UiFlex col{};
        col.direction = UiFlexDir::Column;
        col.shrink = 0.0f;
        col.widthMode = UiSize::Percent;
        col.width = 100.0f;
        return uiPanel(*canvas.state, parent, col, hiddenPaint());
    };
    constexpr float kOpen = 3.4028234e+38f;
    const uint8_t catLod = static_cast<uint8_t>(UiPropCat::Lod);
    const uint8_t catMisc = static_cast<uint8_t>(UiPropCat::Misc);
    const uint8_t catRender = static_cast<uint8_t>(UiPropCat::Rendering);
    const uint8_t catStream = static_cast<uint8_t>(UiPropCat::Streaming);

    const uint16_t postBody = uiPropSection(*canvas.state, details, "PostProcessing", catRender, kSecPost);
    const uint16_t giBody = uiFoldBody(*canvas.state, postBody, "GI");
    const uint16_t directBody = uiFoldBody(*canvas.state, giBody, "Direct Illumination");
    {
        const HudMenuItem items[] = {{"None", 0}, {"Restir", 1}};
        (void)hudDrop(*hud, *canvas.state, directBody, "Mode", kMenuDirect, items, 2, hud->flags.directMode);
    }
    hud->diBody = group(directBody);
    panel = hud->diBody;
    tick("Temporal", &hud->tweaks.restirTemporal, true);
    tick("Spatial", &hud->tweaks.restirSpatialOn, true);
    tick("Boil", &hud->tweaks.restirBoilOn, true);
    tick("Visibility", &hud->tweaks.restirInitVis, false);
    tick("Permutation", &hud->tweaks.restirPerm, true);
    tick("Shortcut", &hud->tweaks.restirShortcut, false);
    tick("Material", &hud->tweaks.restirMaterial, true);
    tick("Naive", &hud->tweaks.restirNaive, true);
    tick("Sun ray", &hud->tweaks.restirSunOn, true);
    {
        const UiSlideSpec spec[] = {
            {"Candidates", &hud->tweaks.restirLocal, 8.0f, 0.0f, 16.0f},
            {"History", &hud->tweaks.restirHistory, 20.0f, 1.0f, 40.0f},
            {"Temporal depth", &hud->tweaks.restirTemporalDepth, 0.1f, 0.01f, 0.5f},
            {"Temporal normal", &hud->tweaks.restirTemporalNormal, 0.9f, 0.0f, 1.0f},
            {"Temporal ray", &hud->tweaks.restirTemporalBias, 1.0f, 0.0f, 2.0f},
            {"Permute threshold", &hud->tweaks.restirPermTh, 0.9f, 0.0f, 1.0f},
            {"Boil", &hud->tweaks.restirBoil, 0.2f, 0.0f, 1.0f},
            {"Neighbors", &hud->tweaks.restirSpatial, 3.0f, 0.0f, 8.0f},
            {"Radius", &hud->tweaks.restirRadius, 4.0f, 1.0f, 32.0f},
            {"Spatial depth", &hud->tweaks.restirSpatialDepth, 0.1f, 0.01f, 0.5f},
            {"Spatial normal", &hud->tweaks.restirSpatialNormal, 0.9f, 0.0f, 1.0f},
            {"Spatial ray", &hud->tweaks.restirSpatialBias, 3.0f, 0.0f, 3.0f},
            {"Boost", &hud->tweaks.restirSpatialBoost, 3.0f, 0.0f, 16.0f},
            {"Target", &hud->tweaks.restirTarget, 20.0f, 1.0f, 40.0f},
            {"Sun disk", &hud->tweaks.restirSunDisk, 0.5f, 0.0f, 4.0f},
            {"Sun rays", &hud->tweaks.restirSunRays, 1.0f, 1.0f, 8.0f},
            {"Shadow rays", &hud->tweaks.restirShadowRays, 1.0f, 1.0f, 8.0f},
            {"Shadow range m", &hud->tweaks.restirShadowRange, 0.0f, 0.0f, 200.0f},
            {"Shadow bias m", &hud->tweaks.restirShadowBias, 0.05f, 0.02f, 2.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t indirectBody = uiFoldBody(*canvas.state, giBody, "Indirect Illumination");
    {
        const HudMenuItem items[] = {{"None", 0}, {"Sharc", 3}};
        (void)hudDrop(*hud, *canvas.state, indirectBody, "Mode", kMenuGi, items, 2, hud->flags.giMode);
    }
    hud->indBody = group(indirectBody);
    panel = hud->indBody;
    tick("Denoise", &hud->tweaks.sharcDenoise, true);
    slide(panel, "GI brightness", &hud->tweaks.giGain, 2.0f, 0.0f, 6.0f);
    const uint16_t reflectFold = uiFoldBody(*canvas.state, giBody, "Reflection");
    {
        const HudMenuItem items[] = {{"None", 0}, {"SSSR", 1}, {"SSSR + RT", 2}};
        (void)hudDrop(*hud, *canvas.state, reflectFold, "Mode", kMenuReflect, items, 3, hud->flags.reflectMode);
    }
    hud->reflBody = group(reflectFold);
    panel = hud->reflBody;
    {
        const UiSlideSpec spec[] = {
            {"Roughness", &hud->tweaks.ssrCutoff, 0.82f, 0.05f, 1.0f},
            {"Steps", &hud->tweaks.ssrSteps, 64.0f, 16.0f, 96.0f},
            {"Thickness", &hud->tweaks.ssrThick, 0.02f, 0.02f, 2.0f},
            {"Distance", &hud->tweaks.ssrMax, 80.0f, 4.0f, 200.0f},
            {"Blur", &hud->tweaks.ssrStride, 1.0f, 0.0f, 3.0f},
            {"Metal", &hud->tweaks.ssrMetal, 0.35f, 0.0f, 1.0f},
            {"Edge px", &hud->tweaks.ssrEdge, 24.0f, 0.0f, 64.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    hud->rtBody = group(reflectFold);
    panel = hud->rtBody;
    slide(panel, "Ray roughness", &hud->tweaks.ssrRt, 0.45f, 0.0f, 1.0f);
    const uint16_t effects = uiFoldBody(*canvas.state, postBody, "Effects");
    {
        const HudMenuItem items[] = {{"None", 0}, {"XeGTAO", 1}};
        (void)hudDrop(*hud, *canvas.state, effects, "Ambient Occlusion", kMenuAo, items, 2, hud->flags.aoMode);
    }
    hud->aoBody = group(effects);
    panel = hud->aoBody;
    {
        const UiSlideSpec spec[] = {
            {"Radius", &hud->tweaks.gtaoRadius, 0.5f, 0.05f, 4.0f},
            {"Slices", &hud->tweaks.gtaoSlices, 3.0f, 1.0f, 9.0f},
            {"Steps", &hud->tweaks.gtaoSteps, 3.0f, 1.0f, 4.0f},
            {"Power", &hud->tweaks.gtaoPower, 2.2f, 0.5f, 4.0f},
            {"Falloff", &hud->tweaks.gtaoFalloff, 0.615f, 0.05f, 1.0f},
            {"Denoise", &hud->tweaks.gtaoDenoise, 1.0f, 0.0f, 3.0f},
            {"Strength", &hud->post.gtao, 1.0f, 0.0f, 1.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t sscsFold = uiFoldBody(*canvas.state, effects, "SSCS");
    panel = sscsFold;
    tick("Enabled", &hud->flags.contacten, false);
    hud->sscsBody = group(sscsFold);
    panel = hud->sscsBody;
    {
        const UiSlideSpec spec[] = {
            {"Strength", &hud->post.contact, 1.0f, 0.0f, 1.0f},
            {"Radius m", &hud->tweaks.sscsReach, 1.0f, 0.05f, 8.0f},
            {"Steps", &hud->tweaks.sscsSteps, 8.0f, 1.0f, 32.0f},
            {"Thickness", &hud->tweaks.sscsThick, 0.02f, 0.0f, 1.0f},
            {"Pixels", &hud->tweaks.sscsPx, 128.0f, 1.0f, 256.0f},
            {"Spec occlusion", &hud->post.specOcc, 1.0f, 0.0f, 1.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t bloomFold = uiFoldBody(*canvas.state, effects, "Bloom");
    panel = bloomFold;
    tick("Enabled", &hud->flags.bloomen, true);
    hud->bloomBody = group(bloomFold);
    panel = hud->bloomBody;
    slide(panel, "Strength", &hud->post.bloom, 0.15f, 0.0f, 0.80f);
    slide(panel, "Threshold", &hud->tweaks.bloomThreshold, 0.35f, 0.0f, 2.0f);
    const uint16_t lens = uiFoldBody(*canvas.state, effects, "Lens");
    panel = lens;
    {
        const UiSlideSpec spec[] = {
            {"Focus m", &hud->post.dofFocus, 8.0f, 0.2f, 80.0f},
            {"Bokeh", &hud->post.dofAmount, 0.0f, 0.0f, 1.0f},
            {"Flare", &hud->post.flare, 0.0f, 0.0f, 1.0f},
            {"Grain", &hud->post.grain, 0.0f, 0.0f, 0.12f},
            {"Vignette", &hud->post.vignette, 0.35f, 0.0f, 2.0f},
            {"Chromatic px", &hud->post.ca, 2.0f, 0.0f, 4.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t toneBody = uiFoldBody(*canvas.state, postBody, "Tonemapping");
    panel = toneBody;
    {
        const HudMenuItem items[] = {{"AgX", 0}, {"ACES", 1}, {"Filmic", 2}, {"Linear", 3}, {"PBR Neutral", 4}, {"GT7", 5}, {"Generic", 6}, {"Reinhard", 7}};
        (void)hudDrop(*hud, *canvas.state, panel, "Operator", kMenuTone, items, 8, hud->flags.tone);
    }
    tick("Luminance", &hud->flags.lumScale, false);
    tick("Gamut", &hud->flags.gamut, false);
    {
        const UiSlideSpec spec[] = {
            {"Brightness", &hud->post.brightness, 1.0f, 0.05f, 4.0f},
            {"Exposure EV", &hud->post.exposure, -1.0f, -8.0f, 8.0f},
            {"Temperature", &hud->post.temperature, 0.0f, -1.0f, 1.0f},
            {"Tint", &hud->post.tint, 0.0f, -1.0f, 1.0f},
            {"Night", &hud->post.night, 0.0f, 0.0f, 1.0f},
            {"Contrast", &hud->post.contrast, 1.0f, 0.5f, 1.5f},
            {"Saturation", &hud->post.saturation, 1.0f, 0.0f, 2.0f},
            {"Mix", &hud->post.mix, 0.0f, 0.0f, 1.0f},
            {"Vibrance", &hud->post.vibrance, 1.0f, 0.0f, 2.0f},
            {"Generic contrast", &hud->post.genContrast, 1.0f, 0.5f, 2.0f},
            {"Generic mid in", &hud->post.midIn, 0.18f, 0.01f, 1.0f},
            {"Generic mid out", &hud->post.midOut, 0.18f, 0.01f, 1.0f},
            {"Generic HDR", &hud->post.hdrMax, 10.0f, 1.0f, 64.0f},
            {"Shadows", &hud->post.shadows, 1.0f, 0.0f, 2.0f},
            {"Midtones", &hud->post.midtones, 1.0f, 0.0f, 2.0f},
            {"Highlights", &hud->post.highlights, 1.0f, 0.0f, 2.0f},
            {"CDL slope", &hud->post.slope, 1.0f, 0.0f, 2.0f},
            {"CDL offset", &hud->post.cdlOffset, 0.0f, -0.5f, 0.5f},
            {"CDL power", &hud->post.power, 1.0f, 0.2f, 2.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t finalBody = uiFoldBody(*canvas.state, postBody, "Final");
    panel = finalBody;
    {
        const HudMenuItem items[] = {{"Off", 0}, {"SMAA", 1}, {"CMAA", 2}, {"FSR", 3}, {"TAA", 4}, {"FXAA", 5}};
        (void)hudDrop(*hud, *canvas.state, panel, "Anti-aliasing", kMenuAa, items, 6, hud->flags.aa);
    }
    hud->fsrBody = group(finalBody);
    panel = hud->fsrBody;
    {
        const HudMenuItem items[] = {{"Native", 0}, {"Quality", 1}, {"Balanced", 2}, {"Performance", 3}, {"Ultra", 4}};
        (void)hudDrop(*hud, *canvas.state, panel, "FSR scale", kMenuFsr, items, 5, hud->flags.fsrQ);
    }
    slide(panel, "Sharpness", &hud->tweaks.fsrSharpness, 0.2f, 0.0f, 1.0f);
    const uint16_t atmo = uiPropSection(*canvas.state, details, "Atmosphere", catRender, kSecAtmo);
    const uint16_t fogFold = uiFoldBody(*canvas.state, atmo, "Fog");
    panel = fogFold;
    tick("Enabled", &hud->flags.fogen, false);
    hud->fogBody = group(fogFold);
    panel = hud->fogBody;
    slide(panel, "Fog Density", &hud->tweaks.fogDensity, 0.06f, 0.0f, 0.5f);
    slide(panel, "Fog Falloff", &hud->tweaks.fogFalloff, 0.08f, 0.0f, 1.5f);
    {
        hud->fogSwatch = bindSwatch(*hud, *canvas.state, panel, "Fog Color", &hud->tweaks.fogR, &hud->tweaks.fogG, &hud->tweaks.fogB, 0.62f, 0.72f, 0.86f);
    }
    {
        const UiSlideSpec spec[] = {
            {"Fog Height", &hud->tweaks.fogHeight, 0.0f, -40.0f, 40.0f},
            {"Fog Start", &hud->tweaks.fogStart, 0.0f, 0.0f, 80.0f},
            {"Fog Opacity", &hud->tweaks.fogOpacity, 0.92f, 0.0f, 1.0f},
            {"Fog Luminance", &hud->tweaks.fogLum, 1.4f, 0.0f, 8.0f},
            {"Sun Shafts", &hud->tweaks.fogScatter, 40.0f, 0.0f, 128.0f},
            {"Shaft Start", &hud->tweaks.fogInStart, 0.0f, 0.0f, 80.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t weather = uiPropSection(*canvas.state, details, "Weather", catRender, kSecWeather);
    const uint16_t sunFold = uiFoldBody(*canvas.state, weather, "Sun");
    panel = sunFold;
    tick("Enabled", &hud->flags.sun, true);
    tick("Cast Shadows", &hud->flags.sunCast, true);
    {
        const HudMenuItem items[] = {{"ReSTIR DI Shadows", 0}, {"Cascaded Shadow Maps", 1}};
        (void)hudDrop(*hud, *canvas.state, sunFold, "Shadows", kMenuSun, items, 2, hud->flags.sunShadow);
    }
    {
        hud->sunSwatch = bindSwatch(*hud, *canvas.state, sunFold, "Color", &hud->post.sunR, &hud->post.sunG, &hud->post.sunB, 1.0f, 0.92f, 0.76f);
    }
    {
        const UiSlideSpec spec[] = {
            {"Intensity", &hud->post.sunGain, 1.0f, 0.0f, 4.0f},
            {"Temperature", &hud->tweaks.sunKelvin, 5600.0f, 1500.0f, 12000.0f},
            {"Sun Angle", &hud->tweaks.sunAngle, 0.53f, 0.05f, 8.0f},
            {"Soft Angle", &hud->tweaks.sunSoft, 0.5f, 0.0f, 8.0f},
            {"Indirect Light", &hud->tweaks.sunIndirect, 1.0f, 0.0f, 4.0f},
        };
        hud->bind.slides(*canvas.state, sunFold, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    {
        const UiPropVec2 row = uiPropVec2(*canvas.state, sunFold, "Rotation", 35.0f, 30.0f, -kOpen, kOpen, 0.5f);
        hud->bind.watch(row.x, &hud->tweaks.sunZenith, 35.0f);
        hud->bind.watch(row.y, &hud->tweaks.sunAzimuth, 30.0f);
    }
    tick("Sun Follows Clock", &hud->flags.wxClock, false);
    panel = weather;
    {
        const uint16_t menu = uiMakeMenu(*canvas.state);
        uiCommandAddTo(*canvas.state, menu, "Clear", wxClear, hud);
        uiCommandAddTo(*canvas.state, menu, "Overcast", wxOvercast, hud);
        uiCommandAddTo(*canvas.state, menu, "Rain", wxRain, hud);
        uiCommandAddTo(*canvas.state, menu, "Storm", wxStorm, hud);
        hud->wxBtn = uiPropMenu(*canvas.state, weather, "Preset", "Rain", menu);
    }
    {
        const UiSlideSpec spec[] = {
            {"Time of day", &hud->tweaks.timeOfDay, 11.4f, 0.0f, 24.0f},
            {"Day length", &hud->tweaks.dayLength, 1200.0f, 60.0f, 7200.0f},
            {"Wind", &hud->tweaks.windSpeed, 6.0f, 0.0f, 40.0f},
            {"Wind yaw", &hud->tweaks.windYaw, 23.0f, 0.0f, 360.0f},
            {"Temperature", &hud->tweaks.temperatureC, 12.0f, -20.0f, 45.0f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    const uint16_t rainFold = uiFoldBody(*canvas.state, weather, "Rain");
    panel = rainFold;
    slide(panel, "Amount", &hud->tweaks.rainAmount, 3.0f, 0.0f, 4.0f);
    slide(panel, "Wetness", &hud->tweaks.rainWet, 1.0f, 0.0f, 1.0f);
    slide(panel, "Ripples", &hud->tweaks.rainRipple, 0.85f, 0.0f, 1.0f);
    const uint16_t cloudFold = uiFoldBody(*canvas.state, weather, "Clouds");
    panel = cloudFold;
    tick("Enabled", &hud->flags.clouden, true);
    tick("Sky", &hud->flags.sky, true);
    slide(panel, "Coverage", &hud->tweaks.cloudCoverage, 0.42f, 0.0f, 0.95f);
    slide(panel, "Density", &hud->tweaks.cloudDensity, 0.08f, 0.0f, 0.3f);
    panel = uiPropSection(*canvas.state, details, "Lights", catRender, kSecPost);
    tick("Cluster", &hud->flags.cluster, true);
    tick("Points", &hud->flags.points, true);
    tick("Spots", &hud->flags.spots, true);
    tick("Area", &hud->flags.area, true);
    slide(panel, "Candles", &hud->post.lamp, 1.0f, 0.0f, 1.0f);
    panel = uiPropSection(*canvas.state, details, "LOD", catLod, kSecPost);
    tick("Freeze Frustum", &hud->tweaks.freeze, false);
    tick("Cone", &hud->flags.cone, true);
    tick("Hi-Z", &hud->flags.hiz, true);
    tick("LOD", &hud->flags.lod, true);
    hud->bind.slideStep(*canvas.state, panel, "Force LOD", &hud->flags.forceLod, -1.0f, -1.0f, 3.0f, 1.0f);
    tick("Frustum", &hud->flags.frust, true);
    tick("Small", &hud->flags.small, true);
    tick("Backface", &hud->flags.backf, true);
    tick("Screen", &hud->flags.scrn, true);
    tick("Behind", &hud->flags.behind, true);
    panel = uiPropSection(*canvas.state, details, "Streaming", catStream, kSecPost);
    tick("Virtual textures", &hud->flags.vt, false);
    panel = uiPropSection(*canvas.state, details, "Shading", catMisc, kSecPost);
    slide(panel, "Toksvig", &hud->post.toksvig, 1.0f, 0.0f, 1.0f);
    slide(panel, "Parallax", &hud->post.pom, 0.35f, 0.0f, 1.0f);
    slide(panel, "Multi-scatter", &hud->post.ggx, 1.0f, 0.0f, 1.0f);
}

} // namespace burnhope
