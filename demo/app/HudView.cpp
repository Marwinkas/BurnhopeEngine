#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Canvas.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/UiPhoto.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/TreeView.hpp"
#include "Str.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void hudBuildView(Canvas& canvas, HudContext* hud, uint16_t root, uint16_t row, uint16_t viewCol, const UiLook& look) {
    auto hiddenPaint = []() {
        UiPaint paint{};
        paint.a = 0.0f;
        return paint;
    };
    auto dockPaint = [&]() {
        UiPaint paint = paintToken(look, UiToken::Content, 0.0f);
        paint.a = 0.94f;
        paint.borderW = 0.0f;
        return paint;
    };
    auto veilPaint = [&]() {
        UiPaint paint = paintToken(look, UiToken::Content, look.radiusSm);
        paint.a = 0.72f;
        paint.borderW = 1.0f;
        tokenRgb(look, UiToken::Muted, paint.br, paint.bg, paint.bb);
        return paint;
    };
    auto splitBar = [&](uint16_t parent, bool vertical) {
        return uiSplitBar(*canvas.state, parent, vertical);
    };
    auto scrollPage = [&](uint16_t parent, bool show) {
        UiFlex viewFlex{};
        viewFlex.grow = 1.0f;
        viewFlex.shrink = 1.0f;
        viewFlex.minH = 0.0f;
        viewFlex.overflow = UiOverflow::Scroll;
        UiPaint scroll = paintToken(look, UiToken::Content, 0.0f);
        scroll.a = 1.0f;
        const uint16_t view = uiPanel(*canvas.state, parent, viewFlex, scroll);
        UiFlex column{};
        column.gap = 4.0f;
        column.pad = 8.0f;
        column.shrink = 0.0f;
        const uint16_t inner = uiPanel(*canvas.state, view, column, hiddenPaint());
        if (canvas.state != nullptr) {
            uiBindScroll(*canvas.state, view, inner);
        }
        if (!show) {
            uiShow(*canvas.state, view, false);
        }
        return inner;
    };

    UiFlex camFlex{};
    camFlex.position = UiPosition::Absolute;
    camFlex.direction = UiFlexDir::Column;
    camFlex.widthMode = UiSize::Px;
    camFlex.width = 400.0f;
    camFlex.heightMode = UiSize::Px;
    camFlex.height = 500.0f;
    camFlex.pad = 8.0f;
    camFlex.gap = 6.0f;
    camFlex.shrink = 0.0f;
    const uint16_t camPanel = uiPanel(*canvas.state, root, camFlex, veilPaint());
    hud->camPanel = camPanel;
    canvas.state->ent[camPanel].add<UiMenuSlide>();
    uiShow(*canvas.state, camPanel, false);
    auto rule = [&]() {
        UiFlex line{};
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 1.0f;
        line.shrink = 0.0f;
        UiPaint ink = paintToken(look, UiToken::Muted, 0.0f);
        ink.a = 0.45f;
        return uiPanel(*canvas.state, camPanel, line, ink);
    };
    const char* freeViews[] = {"Perspective", "Orthographic"};
    const char* axisViews[] = {"Top", "Bottom", "Left", "Right", "Front", "Back"};
    for (uint8_t i = 0; i < 2; ++i) {
        hud->camProj[i] = uiCheck(*canvas.state, camPanel, freeViews[i], 1, 9, i == 0);
    }
    (void)rule();
    for (uint8_t i = 0; i < 6; ++i) {
        hud->camAxis[i] = uiCheck(*canvas.state, camPanel, axisViews[i], 1, 10, false);
    }
    for (uint16_t id : hud->camProj) {
        if (id < canvas.state->count) {
            if (auto* paint = canvas.state->ent[id].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
        }
    }
    for (uint16_t id : hud->camAxis) {
        if (id < canvas.state->count) {
            if (auto* paint = canvas.state->ent[id].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
            }
        }
    }
    auto labeledNumber = [&](const char* text, float value, float minV, float maxV, float step) {
        UiFlex line{};
        line.direction = UiFlexDir::Row;
        line.align = UiAlign::Center;
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 32.0f;
        line.gap = 8.0f;
        line.shrink = 0.0f;
        const uint16_t host = uiPanel(*canvas.state, camPanel, line, hiddenPaint());
        (void)uiLabel(*canvas.state, host, text);
        UiFlex gap{};
        gap.grow = 1.0f;
        gap.shrink = 1.0f;
        gap.widthMode = UiSize::Px;
        gap.width = 8.0f;
        gap.heightMode = UiSize::Px;
        gap.height = 1.0f;
        (void)uiSpacer(*canvas.state, host, gap);
        const uint16_t num = uiNumber(*canvas.state, host, value, minV, maxV, step);
        uiSetHeight(*canvas.state, num, 28.0f);
        if (num < canvas.state->count) {
            if (auto* flex = canvas.state->ent[num].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 72.0f;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
        return num;
    };
    (void)rule();
    {
        UiFlex line{};
        line.direction = UiFlexDir::Row;
        line.align = UiAlign::Center;
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.heightMode = UiSize::Px;
        line.height = 32.0f;
        line.gap = 8.0f;
        line.shrink = 0.0f;
        const uint16_t host = uiPanel(*canvas.state, camPanel, line, hiddenPaint());
        (void)uiLabel(*canvas.state, host, "Field of View");
        hud->camFov = uiSlider(*canvas.state, host, 60.0f, 10.0f, 120.0f, nullptr, nullptr, 32.0f);
        if (hud->camFov < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->camFov].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 32.0f;
                flex->grow = 1.0f;
                flex->shrink = 1.0f;
                flex->minW = 24.0f;
            }
        }
        hud->camFovNum = uiNumber(*canvas.state, host, 60.0f, 10.0f, 120.0f, 1.0f);
        uiSetHeight(*canvas.state, hud->camFovNum, 28.0f);
        if (hud->camFovNum < canvas.state->count) {
            if (auto* flex = canvas.state->ent[hud->camFovNum].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = 72.0f;
                flex->grow = 0.0f;
                flex->shrink = 0.0f;
            }
        }
    }
    (void)rule();
    hud->camNear = labeledNumber("Near View Plane", 0.1f, 0.01f, 10.0f, 0.01f);
    hud->camFar = labeledNumber("Far View Plane", 8000.0f, 100.0f, 100000.0f, 10.0f);
    (void)rule();
    hud->camSpeed = labeledNumber("Camera Speed", 4.0f, 0.05f, 200.0f, 0.1f);
    UiFlex viewFlex{};
    viewFlex.grow = 1.0f;
    viewFlex.shrink = 1.0f;
    viewFlex.minH = 0.0f;
    const uint16_t viewPort = uiPanel(*canvas.state, viewCol, viewFlex, hiddenPaint());
    UiFlex over{};
    over.position = UiPosition::Absolute;
    over.direction = UiFlexDir::Column;
    over.widthMode = UiSize::Px;
    over.width = 240.0f;
    over.heightMode = UiSize::Auto;
    over.pad = 8.0f;
    over.gap = 2.0f;
    over.shrink = 0.0f;
    over.posX = 8.0f;
    over.posY = 80.0f;
    UiPaint veil = paintToken(look, UiToken::Content, look.radiusSm);
    veil.a = 0.72f;
    hud->overlay = uiPanel(*canvas.state, root, over, veil);
    hud->viewPane = viewPort;
    if (hud->overlay < canvas.state->count) {
        canvas.state->ent[hud->overlay].add<UiPass>();
    }
    uiShow(*canvas.state, hud->overlay, false);
    for (uint8_t i = 0; i < 6; ++i) {
        const uint16_t line = uiLabel(*canvas.state, hud->overlay, "");
        hud->statLine[i] = line;
        if (line < canvas.state->count) {
            if (auto* paint = canvas.state->ent[line].try_get_mut<UiPaint>()) {
                *paint = paintToken(look, UiToken::Accent, 0.0f);
                paint->a = 0.0f;
            }
        }
        uiShow(*canvas.state, line, false);
    }
    (void)splitBar(row, true);
    UiFlex dockFlex{};
    dockFlex.direction = UiFlexDir::Row;
    dockFlex.widthMode = UiSize::Px;
    dockFlex.width = 300.0f;
    dockFlex.shrink = 0.0f;
    dockFlex.heightMode = UiSize::Percent;
    dockFlex.height = 100.0f;
    dockFlex.minH = 72.0f;
    const uint16_t dock = uiPanel(*canvas.state, row, dockFlex, hiddenPaint());
    UiFlex statsFlex{};
    statsFlex.direction = UiFlexDir::Column;
    statsFlex.widthMode = UiSize::Px;
    statsFlex.width = 420.0f;
    statsFlex.shrink = 0.0f;
    statsFlex.minW = 360.0f;
    const uint16_t statsPane = uiPanel(*canvas.state, dock, statsFlex, dockPaint());
    uiShow(*canvas.state, statsPane, false);
    UiFlex statsCol{};
    statsCol.gap = 4.0f;
    statsCol.pad = 8.0f;
    statsCol.shrink = 0.0f;
    const uint16_t stats = uiPanel(*canvas.state, statsPane, statsCol, hiddenPaint());
    hud->stats.canvas = &canvas;
    hud->stats.page = statsPane;
    hud->stats.dock = dock;
    hud->stats.open = 0;
    UiFlex sideFlex{};
    sideFlex.direction = UiFlexDir::Column;
    sideFlex.grow = 1.0f;
    sideFlex.shrink = 1.0f;
    sideFlex.minW = 180.0f;
    sideFlex.minH = 72.0f;
    const uint16_t side = uiPanel(*canvas.state, dock, sideFlex, hiddenPaint());
    UiFlex hierFlex{};
    hierFlex.grow = 1.0f;
    hierFlex.shrink = 1.0f;
    hierFlex.minH = 40.0f;
    UiPaint hierScroll = paintToken(look, UiToken::Content, 0.0f);
    hierScroll.a = 1.0f;
    const uint16_t hierView = uiPanel(*canvas.state, side, hierFlex, hierScroll);
    UiFlex hierCol{};
    hierCol.gap = 4.0f;
    hierCol.pad = 8.0f;
    hierCol.shrink = 0.0f;
    hierCol.widthMode = UiSize::Percent;
    hierCol.width = 100.0f;
    const uint16_t objects = uiPanel(*canvas.state, hierView, hierCol, hiddenPaint());
    if (canvas.state != nullptr) {
        uiBindScroll(*canvas.state, hierView, objects);
    }
    const uint16_t hierarchy = uiLabel(*canvas.state, objects, "Hierarchy");
    if (auto* paint = canvas.state->ent[hierarchy].try_get_mut<UiPaint>()) {
        paint->token = UiToken::Custom;
        paint->r = look.accent[0][0];
        paint->g = look.accent[0][1];
        paint->b = look.accent[0][2];
        paint->a = 0.0f;
    }
    hud->tool.objects = objects;
    (void)splitBar(side, false);
    UiFlex inspFlex{};
    inspFlex.direction = UiFlexDir::Column;
    inspFlex.overflow = UiOverflow::Hidden;
    inspFlex.heightMode = UiSize::Px;
    inspFlex.height = 420.0f;
    inspFlex.minH = 40.0f;
    inspFlex.shrink = 1.0f;
    const uint16_t insp = uiPanel(*canvas.state, side, inspFlex, hiddenPaint());
    (void)uiPropFilterBar(*canvas.state, insp);
    const uint16_t detailsInner = scrollPage(insp, true);
    if (canvas.state != nullptr && detailsInner < canvas.state->count) {
        if (auto* flex = canvas.state->ent[detailsInner].try_get_mut<UiFlex>()) {
            flex->pad = 0.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
            flex->padT = 0.0f;
            flex->padB = 0.0f;
        }
    }
    uint16_t details = uiPropHost(*canvas.state, detailsInner);
    const uint16_t detailsView = uiParent(*canvas.state, detailsInner);
    hud->tool.propPage = detailsView;
    if (canvas.state != nullptr) {
        uint16_t cubeBtn = uiButton(*canvas.state, objects, { .label = "Куб", .fillWidth = true });
        uiSetHeight(*canvas.state, cubeBtn, 28.0f);
        if (cubeBtn != kUiNone) {
            uiBind(*canvas.state, cubeBtn, onSpawnCube, hud);
        }
    }
    if (hud->tool.scene != nullptr) {
        const uint8_t count = sceneFrameActorCount(hud->tool.scene);
        for (uint8_t i = 0; i < count; ++i) {
            char name[24];
            uint8_t kind = 0;
            sceneFrameActorName(hud->tool.scene, i, name, sizeof(name), &kind);
            (void)addActorButton(*hud, objects, i, name, kind);
        }
    }
    (void)splitBar(root, false);
    UiFlex fileFlex{};
    fileFlex.direction = UiFlexDir::Column;
    fileFlex.heightMode = UiSize::Px;
    fileFlex.height = 200.0f;
    fileFlex.minH = 120.0f;
    fileFlex.shrink = 1.0f;
    const uint16_t files = uiPanel(*canvas.state, root, fileFlex, dockPaint());
    filesShutdown(*canvas.state);
    uiFilesOwn(*canvas.state, files);
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesDropOnScene(*canvas.state, hud->viewPane, hudDropImport, hud);
    filesDropBadge(*canvas.state, hudDragBadge);
    filesOnOpen(*canvas.state, onFileOpen, hud);
    filesShow(*canvas.state, true);
    hudBuildInspector(canvas, hud, details, stats, hiddenPaint());
}
} // namespace burnhope
