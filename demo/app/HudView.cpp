#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "render/SceneFrame.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"

namespace burnhope {

void hudWire(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t statsBody, uint16_t files, uint16_t detailsInner) {
    if (hud == nullptr || canvas.state == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    if (detailsInner < state.count) {
        if (auto* flex = state.ent[detailsInner].try_get_mut<UiFlex>()) {
            flex->pad = 0.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
            flex->padT = 0.0f;
            flex->padB = 0.0f;
        }
    }
    hud->viewCap = uiFindChildText(state, hud->viewBtn);
    if (hud->viewCap != kUiNone) {
        uiTextStyle(state, hud->viewCap, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    float viewW = uiMeasure(state, "Lit", 3, 3) + 38.0f;
    for (const ViewMode& mode : kViews) {
        uint8_t len = 0;
        while (mode.name[len] != '\0' && len < 48) {
            ++len;
        }
        const float measured = uiMeasure(state, mode.name, len, len);
        if (measured + 52.0f > viewW) {
            viewW = measured + 52.0f;
        }
    }
    uiSetWidth(state, hud->viewBtn, uiMeasure(state, "Lit", 3, 3) + 38.0f);
    const float viewCount = static_cast<float>(sizeof(kViews) / sizeof(kViews[0]));
    hud->viewPopH = 16.0f + viewCount * 36.0f + (viewCount - 1.0f) * 2.0f;
    if (hud->viewPanel < state.count) {
        if (auto* flex = state.ent[hud->viewPanel].try_get_mut<UiFlex>()) {
            flex->width = viewW;
            flex->height = hud->viewPopH;
        }
    }
    uiButtonTintIcon(state, uiButtonIconChild(state, hud->camBtn), false);
    if (hud->tool.scene != nullptr) {
        sceneFrameActorGizmo(hud->tool.scene, hud->toolMode);
    }
    paintTools(*hud);
    const float moveSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    const float rotSteps[] = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    const float scaleSteps[] = {0.1f, 0.25f, 0.5f, 1.0f};
    const float* stepTable[] = {moveSteps, rotSteps, scaleSteps};
    const uint8_t stepCount[] = {7, 6, 4};
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const uint8_t picked = hud->snapPick[channel] < stepCount[channel] ? hud->snapPick[channel] : 0;
        hud->snapSent[channel] = stepTable[channel][picked];
        setSlide(canvas, hud->snapNum[channel], hud->snapSent[channel]);
    }
    cmdView(hud, hud->flags.viewMode);
    hud->stats.canvas = &canvas;
    hud->stats.open = 0;
    filesShutdown(state);
    uiFilesOwn(state, files);
    filesAttach(state);
    filesMount(state);
    filesDropOnScene(state, hud->viewPane, hudDropImport, hud);
    filesDropBadge(state, hudDragBadge);
    filesOnOpen(state, onFileOpen, hud);
    filesShow(state, true);
    hudRefreshActors(*hud);
    hudBuildInspector(canvas, hud, details, statsBody);
}

} // namespace burnhope
