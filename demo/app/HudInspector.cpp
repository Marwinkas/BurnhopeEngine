#include "app/HudBuild.hpp"

#include "render/SceneFrame.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <cstdio>

namespace burnhope {

void hudShowBodies(HudContext& hud, UiState& s) {
    const HudFlags& f = hud.flags;
    uiShow(s, hud.diBody, f.directMode == 1);
    uiShow(s, hud.indBody, f.giMode == 3);
    uiShow(s, hud.reflBody, f.reflectMode != 0);
    uiShow(s, hud.rtBody, f.reflectMode == 2);
    uiShow(s, hud.aoBody, f.aoMode == 1);
    uiShow(s, hud.fsrBody, f.aa == 3);
    uiShow(s, hud.bloomBody, f.bloomen != 0);
    uiShow(s, hud.sscsBody, f.contacten != 0);
    uiShow(s, hud.fogBody, f.fogen != 0);
}

void onHudPick(void* user, uint16_t) {
    auto* hit = static_cast<HudPick*>(user);
    if (hit == nullptr || hit->hud == nullptr) {
        return;
    }
    HudContext& hud = *hit->hud;
    switch (hit->kind) {
    case kMenuTone: hud.flags.tone = hit->value; break;
    case kMenuAa: hud.flags.aa = hit->value; break;
    case kMenuFsr: hud.flags.fsrQ = hit->value; break;
    case kMenuGi: hud.flags.giMode = hit->value; break;
    case kMenuDirect: hud.flags.directMode = hit->value; break;
    case kMenuReflect: hud.flags.reflectMode = hit->value; break;
    case kMenuAo: hud.flags.aoMode = hit->value; break;
    case kMenuSun: hud.flags.sunShadow = hit->value; break;
    default: break;
    }
    if (hud.tool.canvas != nullptr && hud.tool.canvas->state != nullptr) {
        UiState& s = *hud.tool.canvas->state;
        uiMenuCaption(s, hud.menuBtn[hit->kind], hit->label);
        hudShowBodies(hud, s);
    }
}

uint16_t hudDrop(HudContext& hud, UiState& s, uint16_t panel, const char* label, uint8_t kind, const HudMenuItem* items, uint8_t count, uint8_t selected) {
    const uint16_t menu = uiMakeMenu(s);
    const char* caption = items != nullptr && count > 0 ? items[0].label : "";
    for (uint8_t i = 0; i < count; ++i) {
        if (items[i].value == selected) {
            caption = items[i].label;
        }
        hud.picks.push_back({});
        HudPick& pick = hud.picks.back();
        pick.hud = &hud;
        pick.kind = kind;
        pick.value = items[i].value;
        pick.label = items[i].label;
        (void)uiCommandAddTo(s, menu, items[i].label, onHudPick, &pick);
    }
    const uint16_t button = uiPropMenu(s, panel, label, caption, menu);
    if (kind < 8) {
        hud.menuBtn[kind] = button;
    }
    return button;
}


void onHudSwatch(void* user, uint16_t id) {
    auto* sw = static_cast<HudSwatch*>(user);
    if (sw == nullptr || sw->hud == nullptr || sw->hud->tool.canvas == nullptr || sw->hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& s = *sw->hud->tool.canvas->state;
    ColorBook* book = uiColor(s).book;
    if (book != nullptr && sw->r != nullptr && sw->g != nullptr && sw->b != nullptr) {
        book->r = *sw->r;
        book->g = *sw->g;
        book->b = *sw->b;
        book->a = 1.0f;
        book->gen += 1;
    }
    colorOnOpen(&s, id);
}

uint16_t bindSwatch(HudContext& hud, UiState& s, uint16_t panel, const char* label, float* r, float* g, float* b, float r0, float g0, float b0) {
    const uint16_t id = hud.bind.swatch(s, panel, label, r, g, b, r0, g0, b0);
    hud.swatches.push_back({});
    HudSwatch& sw = hud.swatches.back();
    sw.hud = &hud;
    sw.r = r;
    sw.g = g;
    sw.b = b;
    uiBind(s, id, onHudSwatch, &sw);
    return id;
}


void hudBuildInspector(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t stats, UiPaint hidden) {
    auto hiddenPaint = [hidden]() { return hidden; };
    uint16_t panel = stats;
    HudActor& actor = hud->actor;
    hud->bind.clear();
    hud->picks.clear();
    hud->swatches.clear();
    hud->ids.statsLbl = uiLabel(*canvas.state, panel, "fps");
    hud->ids.memLbl = uiLabel(*canvas.state, panel, "mem");
    hud->ids.loadLbl = uiLabel(*canvas.state, panel, "gpu");
    auto tick = [&](const char* label, auto* dst, bool on) {
        hud->bind.check(*canvas.state, panel, label, dst, on);
    };
    auto vec = [&](uint16_t host, const char* label, float* v, float x, float y, float z, float minV, float maxV, float step, int slot) {
        const UiPropVec3 row = uiPropVec3(*canvas.state, host, label, x, y, z, minV, maxV, step);
        hud->bind.watch(row.x, v, x);
        hud->bind.watch(row.y, v + 1, y);
        hud->bind.watch(row.z, v + 2, z);
        if (slot >= 0 && slot < 4) {
            hud->xformLock[slot] = row.lock;
        }
    };
    const uint8_t catGeneral = static_cast<uint8_t>(UiPropCat::General);
    const uint8_t catActor = static_cast<uint8_t>(UiPropCat::Actor);
    panel = uiPropSection(*canvas.state, details, "Transform", catGeneral, kSecTransform);
    constexpr float kOpen = 3.4028234e+38f;
    vec(panel, "Location", actor.pos, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 0);
    vec(panel, "Rotation", actor.euler, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.5f, 1);
    vec(panel, "Scale", actor.scale, 1.0f, 1.0f, 1.0f, -kOpen, kOpen, 0.05f, 2);
    vec(panel, "Pivot", actor.pivot, 0.0f, 0.0f, 0.0f, -kOpen, kOpen, 0.05f, 3);
    tick("Static", &actor.isStatic, false);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMaterial);
    hudBuildActorMaterial(canvas, hud, panel);
    hudBuildPost(canvas, hud, details, hidden);
    panel = uiPropSection(*canvas.state, details, "Material", catActor, kSecMatFile);
    {
        MaterialInfo& doc = hud->matDoc;
        const UiSlideSpec spec[] = {
            {"Color R", &doc.base[0], 1.0f, 0.0f, 1.0f},
            {"Color G", &doc.base[1], 1.0f, 0.0f, 1.0f},
            {"Color B", &doc.base[2], 1.0f, 0.0f, 1.0f},
            {"Roughness", &doc.roughness, 0.5f, 0.0f, 1.0f},
            {"Metallic", &doc.metallic, 0.0f, 0.0f, 1.0f},
            {"Emissive", &doc.emissive, 0.0f, 0.0f, 8.0f},
            {"Albedo Gain", &doc.albedoGain, 1.0f, 0.0f, 4.0f},
            {"Normal Gain", &doc.normalGain, 1.0f, 0.0f, 4.0f},
            {"ORM Gain", &doc.ormGain, 1.0f, 0.0f, 4.0f},
            {"Emissive Gain", &doc.emissiveGain, 0.0f, 0.0f, 50.0f},
            {"Reflectance", &doc.reflectance, 0.5f, 0.0f, 1.0f},
            {"AO", &doc.aoStrength, 1.0f, 0.0f, 1.0f},
            {"Normal Strength", &doc.normalScale, 1.0f, 0.0f, 3.0f},
            {"Clearcoat", &doc.clearcoat, 0.0f, 0.0f, 1.0f},
            {"Clearcoat Roughness", &doc.clearcoatRoughness, 0.04f, 0.04f, 1.0f},
            {"World Grid Size", &doc.worldGridSize, 1.0f, 0.05f, 25.0f},
            {"Triplanar Sharpness", &doc.triplanarSharpness, 4.0f, 1.0f, 16.0f},
            {"UV Tiling X", &doc.uvTiling[0], 1.0f, 0.05f, 32.0f},
            {"UV Tiling Y", &doc.uvTiling[1], 1.0f, 0.05f, 32.0f},
            {"UV Rotation", &doc.uvRotation, 0.0f, -3.14159265f, 3.14159265f},
        };
        hud->bind.slides(*canvas.state, panel, spec, static_cast<uint16_t>(sizeof spec / sizeof spec[0]));
    }
    hud->bind.check(*canvas.state, panel, "World-Aligned", &hud->matDoc.worldAligned, false);
    hud->bind.check(*canvas.state, panel, "Double Sided", &hud->matDoc.doubleSided, false);
    hud->bind.check(*canvas.state, panel, "Alpha Cutout", &hud->matDoc.alphaCut, false);
    const char* texLabel[] = {"Albedo", "Normal", "ORM", "Emissive"};
    for (uint8_t kind = 0; kind < 4; ++kind) {
        const uint16_t id = uiButton(*canvas.state, panel, {.label = texLabel[kind], .fillWidth = true});
        if (id == kUiNone) {
            continue;
        }
        uiSetHeight(*canvas.state, id, 28.0f);
        hud->matTex[kind] = id;
        hud->matHit[kind].hud = hud;
        hud->matHit[kind].kind = static_cast<uint8_t>(kind + 1u);
        uiBind(*canvas.state, id, onMaterialAct, &hud->matHit[kind]);
    }
    panel = stats;
    UiFlex statScrollFlex{};
    statScrollFlex.grow = 1.0f;
    statScrollFlex.shrink = 1.0f;
    statScrollFlex.minH = 240.0f;
    statScrollFlex.heightMode = UiSize::Px;
    statScrollFlex.height = 640.0f;
    const uint16_t statView = uiPanel(*canvas.state, panel, statScrollFlex, hiddenPaint());
    UiFlex statLinesFlex{};
    statLinesFlex.gap = 2.0f;
    statLinesFlex.shrink = 0.0f;
    const uint16_t statLines = uiPanel(*canvas.state, statView, statLinesFlex, hiddenPaint());
    if (canvas.state != nullptr) {
        uiBindScroll(*canvas.state, statView, statLines);
    }
    for (int i = 0; i < 40; ++i) {
        hud->ids.stat[i] = uiLabel(*canvas.state, statLines, "");
    }
    if (canvas.state != nullptr) {
        hudShowBodies(*hud, *canvas.state);
        uint8_t mask = kSecTransform;
        if (hud->tool.scene != nullptr) {
            const uint8_t sel = sceneFrameActorSelected(hud->tool.scene);
            if (sel != kSceneActorNone) {
                mask = sceneFrameActorSections(hud->tool.scene, sel);
            }
        }
        uiPropShowMask(*canvas.state, mask);
        uiEnsureColorPopup(*canvas.state);
    }

}
} // namespace burnhope
