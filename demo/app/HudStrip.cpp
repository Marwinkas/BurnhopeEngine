#include "app/HudBuild.hpp"

#include "gpu_scene/SceneObject.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Slider.hpp"

namespace burnhope {

void hudBuildStrip(Canvas& canvas, HudContext* hud, uint16_t toolbar, uint16_t root, const UiLook& look) {
    auto hiddenPaint = []() {
        UiPaint paint{};
        paint.a = 0.0f;
        return paint;
    };
    auto veilPaint = [&]() {
        UiPaint paint = paintToken(look, UiToken::Content, look.radiusSm);
        paint.a = 0.72f;
        paint.borderW = 1.0f;
        tokenRgb(look, UiToken::Muted, paint.br, paint.bg, paint.bb);
        return paint;
    };

    UiFlex toolLeftFlex{};
    toolLeftFlex.direction = UiFlexDir::Row;
    toolLeftFlex.grow = 1.0f;
    toolLeftFlex.shrink = 1.0f;
    toolLeftFlex.minW = 0.0f;
    toolLeftFlex.align = UiAlign::Center;
    toolLeftFlex.self = UiAlignSelf::Center;
    toolLeftFlex.gap = 4.0f;
    toolLeftFlex.overflow = UiOverflow::Hidden;
    const uint16_t toolLeft = uiPanel(*canvas.state, toolbar, toolLeftFlex, hiddenPaint());
    UiFlex toolRightFlex{};
    toolRightFlex.direction = UiFlexDir::Row;
    toolRightFlex.shrink = 0.0f;
    toolRightFlex.align = UiAlign::Center;
    toolRightFlex.self = UiAlignSelf::Center;
    toolRightFlex.gap = 4.0f;
    const uint16_t toolRight = uiPanel(*canvas.state, toolbar, toolRightFlex, hiddenPaint());
    auto toolSep = [&](uint16_t parent) {
        UiPaint ink = paintToken(look, UiToken::Muted, 0.0f);
        ink.a = 0.55f;
        return uiPanel(*canvas.state, parent, px(1.0f, 18.0f), ink);
    };
    {
        uint16_t plus = uiIconBtn(*canvas.state, toolLeft, 16, 28.0f);
        hud->spawnBtn = plus;
        uint16_t menu = uiMakeMenu(*canvas.state);
        if (menu != kUiNone) {
            canvas.state->ent[menu].add<UiMenuSlide>();
        }
        const char* names[] = {"Cube", "Sphere", "Plane", "Cylinder", "Directional Light", "Point Light", "Empty Actor"};
        for (uint8_t i = 0; i < 7; ++i) {
            hud->spawns.push_back({});
            SpawnHit& hit = hud->spawns.back();
            hit.hud = hud;
            hit.kind = i;
            if (menu != kUiNone) {
                (void)uiCommandAddTo(*canvas.state, menu, names[i], onSpawnKind, &hit);
            }
        }
        uiSetContextMenu(*canvas.state, plus, menu);
        uiMarkMenuBar(*canvas.state, plus);
    }
    (void)toolSep(toolLeft);
    const uint8_t toolModes[5] = {3, 0, 1, 2, 4};
    const uint8_t toolIcons[4] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) {
        hud->toolBtn[i] = uiSlotBtn(*canvas.state, toolLeft, toolIcons[i], 28.0f, &hud->toolIcon[i]);
        const uint16_t toolBtn = hud->toolBtn[i];
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = toolModes[i];
        uiBind(*canvas.state, toolBtn, onGizmoMode, &gizmo);
    }
    {
        const uint16_t toolBtn = uiIconBtn(*canvas.state, toolLeft, 15, 28.0f, &hud->toolIcon[4]);
        hud->toolBtn[4] = toolBtn;
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = 4;
        uiBind(*canvas.state, toolBtn, onGizmoMode, &gizmo);
    }
    if (hud->tool.scene != nullptr) {
        sceneFrameActorGizmo(hud->tool.scene, hud->toolMode);
    }
    paintTools(*hud);
    (void)toolSep(toolLeft);
    const char* spaceLabels[] = {"Local", "World"};
    const float spaceW = uiChoiceWidth(*canvas.state, spaceLabels, 2);
    uiChoiceFit(*canvas.state, uiChoice(*canvas.state, toolLeft, {
        .labels = spaceLabels,
        .count = 2,
        .selected = hud->space,
        .width = spaceW,
        .onPick = onSpace,
        .user = hud,
    }), spaceW);
    const char* pivotLabels[] = {"Pivot", "Center"};
    const float pivotW = uiChoiceWidth(*canvas.state, pivotLabels, 2);
    uiChoiceFit(*canvas.state, uiChoice(*canvas.state, toolLeft, {
        .labels = pivotLabels,
        .count = 2,
        .selected = hud->pivotSpace,
        .width = pivotW,
        .onPick = onPivotSpace,
        .user = hud,
    }), pivotW);
    (void)toolSep(toolLeft);
    const float moveSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    const float rotSteps[] = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    const float scaleSteps[] = {0.1f, 0.25f, 0.5f, 1.0f};
    const char* moveNames[] = {"0.1", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0"};
    const char* rotNames[] = {"5", "10", "15", "30", "45", "90"};
    const char* scaleNames[] = {"0.1", "0.25", "0.5", "1.0"};
    const float* stepTable[] = {moveSteps, rotSteps, scaleSteps};
    const char* const* nameTable[] = {moveNames, rotNames, scaleNames};
    const uint8_t stepCount[] = {7, 6, 4};
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const uint8_t snapIcons[3] = {3, 4, 5};
        const uint16_t toggle = uiSlotBtn(*canvas.state, toolLeft, snapIcons[channel], 28.0f, &hud->snapIcon[channel]);
        hud->snapBtn[channel] = toggle;
        hud->gizmos.push_back({});
        GizmoHit& gizmo = hud->gizmos.back();
        gizmo.tool = &hud->tool;
        gizmo.hud = hud;
        gizmo.mode = channel;
        uiBind(*canvas.state, toggle, onSnapToggle, &gizmo);
        const uint8_t picked = hud->snapPick[channel] < stepCount[channel] ? hud->snapPick[channel] : 0;
        hud->snapSent[channel] = stepTable[channel][picked];
        const uint16_t num = uiNumber(*canvas.state, toolLeft, hud->snapSent[channel], 0.001f, 10000.0f, channel == 1 ? 1.0f : 0.01f);
        uiSetHeight(*canvas.state, num, 28.0f);
        hud->snapNum[channel] = num;
        uiNumberFill(*canvas.state, num, false);
        uiSetWidth(*canvas.state, num, 108.0f);
        uint16_t menu = uiMakeMenu(*canvas.state);
        if (menu != kUiNone) {
            canvas.state->ent[menu].add<UiMenuSlide>();
            uiSetWidth(*canvas.state, menu, 108.0f);
        }
        uint16_t arrow = uiButton(*canvas.state, toolLeft, { .label = "", .width = 28.0f, .height = 28.0f });
        if (arrow < canvas.state->count) {
            if (auto* paint = canvas.state->ent[arrow].try_get_mut<UiPaint>()) {
                paint->icon = 11;
            }
            uiSetContextMenu(*canvas.state, arrow, menu);
            uiMarkMenuBar(*canvas.state, arrow);
        }
        for (uint8_t step = 0; step < stepCount[channel]; ++step) {
            hud->snaps.push_back({});
            SnapHit& pick = hud->snaps.back();
            pick.hud = hud;
            pick.channel = channel;
            pick.index = step;
            pick.step = stepTable[channel][step];
            if (menu != kUiNone) {
                (void)uiCommandAddTo(*canvas.state, menu, nameTable[channel][step], onSnapPick, &pick);
            }
        }
    }
    (void)toolSep(toolLeft);
    hud->playBtn = uiIconBtn(*canvas.state, toolLeft, 17, 28.0f, &hud->playGlyph);
    uiBind(*canvas.state, hud->playBtn, onPlay, hud);
    hud->pauseBtn = uiIconBtn(*canvas.state, toolLeft, 18, 28.0f, nullptr);
    uiBind(*canvas.state, hud->pauseBtn, onPause, hud);
    uiShow(*canvas.state, hud->pauseBtn, false);
    paintTools(*hud);
    uint16_t camBtn = uiButton(*canvas.state, toolRight, { .label = "", .width = 52.0f, .height = 28.0f, .iconSlot = 6 });
    hud->camBtn = camBtn;
    if (camBtn < canvas.state->count) {
        if (auto* flex = canvas.state->ent[camBtn].try_get_mut<UiFlex>()) {
            flex->direction = UiFlexDir::Row;
            flex->align = UiAlign::Center;
            flex->justify = UiJustify::Center;
            flex->gap = 2.0f;
        }
        UiFlex mark{};
        mark.widthMode = UiSize::Px;
        mark.width = 14.0f;
        mark.heightMode = UiSize::Px;
        mark.height = 14.0f;
        mark.shrink = 0.0f;
        UiPaint glyph = paintClear();
        glyph.icon = 11;
        glyph.r = look.text[0];
        glyph.g = look.text[1];
        glyph.b = look.text[2];
        (void)uiPanel(*canvas.state, camBtn, mark, glyph);
    }
    uiButtonTintIcon(*canvas.state, uiButtonIconChild(*canvas.state, camBtn), false);
    uiBind(*canvas.state, camBtn, onCameraButton, hud);
    uint16_t viewBtn = uiMenuButton(*canvas.state, toolRight, "Lit", kUiNone);
    hud->viewBtn = viewBtn;
    uiBind(*canvas.state, viewBtn, onViewButton, hud);
    const uint16_t viewCap = uiFindChildText(*canvas.state, viewBtn);
    hud->viewCap = viewCap;
    float viewW = uiMeasure(*canvas.state, "Lit", 3, 3) + 38.0f;
    for (const ViewMode& mode : kViews) {
        uint8_t len = 0;
        while (mode.name[len] != '\0' && len < 48) {
            ++len;
        }
        const float measured = uiMeasure(*canvas.state, mode.name, len, len);
        if (measured + 52.0f > viewW) {
            viewW = measured + 52.0f;
        }
    }
    uiSetWidth(*canvas.state, viewBtn, uiMeasure(*canvas.state, "Lit", 3, 3) + 38.0f);
    if (viewCap != kUiNone) {
        uiTextStyle(*canvas.state, viewCap, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    const float viewH = 16.0f + static_cast<float>(sizeof(kViews) / sizeof(kViews[0])) * 36.0f
        + static_cast<float>(sizeof(kViews) / sizeof(kViews[0]) - 1) * 2.0f;
    hud->viewPopH = viewH;
    UiFlex viewPopFlex{};
    viewPopFlex.position = UiPosition::Absolute;
    viewPopFlex.direction = UiFlexDir::Column;
    viewPopFlex.widthMode = UiSize::Px;
    viewPopFlex.width = viewW;
    viewPopFlex.heightMode = UiSize::Px;
    viewPopFlex.height = viewH;
    viewPopFlex.pad = 8.0f;
    viewPopFlex.gap = 2.0f;
    viewPopFlex.shrink = 0.0f;
    const uint16_t viewPop = uiPanel(*canvas.state, root, viewPopFlex, veilPaint());
    hud->viewPanel = viewPop;
    canvas.state->ent[viewPop].add<UiMenuSlide>();
    uiShow(*canvas.state, viewPop, false);
    for (const ViewMode& mode : kViews) {
        hud->modes.push_back({});
        ModeHit& hit = hud->modes.back();
        hit.canvas = &canvas;
        hit.hud = hud;
        hit.index = mode.index;
        hit.label = viewCap;
        const uint16_t radio = uiCheck(*canvas.state, viewPop, mode.name, 1, 12, mode.index == 0);
        hit.radio = radio;
        if (radio < canvas.state->count) {
            if (auto* paint = canvas.state->ent[radio].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
            uiBind(*canvas.state, radio, onViewRadio, &hit);
        }
    }
    {
        uint16_t gear = uiButton(*canvas.state, toolRight, { .label = "", .width = 28.0f, .height = 28.0f });
        hud->gearBtn = gear;
        if (gear < canvas.state->count) {
            if (auto* paint = canvas.state->ent[gear].try_get_mut<UiPaint>()) {
                paint->icon = 20;
            }
        }
        uiBind(*canvas.state, gear, onGear, hud);
        UiFlex pop{};
        pop.position = UiPosition::Absolute;
        pop.direction = UiFlexDir::Column;
        pop.widthMode = UiSize::Px;
        pop.width = 280.0f;
        pop.heightMode = UiSize::Px;
        pop.height = 252.0f;
        pop.pad = 8.0f;
        pop.gap = 4.0f;
        pop.shrink = 0.0f;
        const uint16_t statsPop = uiPanel(*canvas.state, root, pop, veilPaint());
        hud->statsPanel = statsPop;
        canvas.state->ent[statsPop].add<UiMenuSlide>();
        uiShow(*canvas.state, statsPop, false);
        const char* statNames[] = {
            "Show FPS / Frame Time",
            "Show GPU Time",
            "Show Draw Calls / Triangle Count",
            "Show Physics Time (Jolt)",
            "Show Memory",
            "Show VRAM Memory",
        };
        for (uint8_t i = 0; i < 6; ++i) {
            hud->statCheck[i] = uiCheck(*canvas.state, statsPop, statNames[i], 0, 0, false);
        }
    }
    hud->toolBar = toolbar;
    if (canvas.state != nullptr) {
        uiCenterChildren(*canvas.state, toolbar);
        uiCenterChildren(*canvas.state, toolLeft);
        uiCenterChildren(*canvas.state, toolRight);
    }
}

} // namespace burnhope
