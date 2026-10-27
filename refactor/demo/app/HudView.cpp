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
#include "ui/Nodes.hpp"
#include "ui/StyleSheet.hpp"

#include <algorithm>

namespace burnhope {

void hudWire(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t statsBody, uint16_t files, uint16_t detailsInner) {
    if (hud == nullptr || canvas.state == nullptr) {
        return;
    }
    UiState& state = *canvas.state;
    uiSetPad(state, detailsInner, 0.0f);
    hud->viewCap = uiFindChildText(state, hud->viewBtn);
    if (hud->viewCap != kUiNone) {
        uiTextStyle(state, hud->viewCap, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    const float buttonExtra = uiStyle(state, "view_button", UiStyleKey::Chrome);
    const float itemExtra = uiStyle(state, "view_popup", UiStyleKey::Chrome);
    float viewW = uiButtonTextWidth(state, "Lit", buttonExtra);
    for (const ViewMode& mode : kViews) {
        viewW = std::max(viewW, uiButtonTextWidth(state, mode.name, itemExtra));
    }
    uiButtonFitText(state, hud->viewBtn, "Lit", buttonExtra);
    const float viewCount = static_cast<float>(sizeof(kViews) / sizeof(kViews[0]));
    hud->viewPopH = uiStyle(state, "view_popup", UiStyleKey::Pad) * 2.0f + viewCount * uiStyle(state, "view_popup", UiStyleKey::Height) +
        (viewCount - 1.0f) * uiStyle(state, "view_popup", UiStyleKey::Gap);
    uiSetSize(state, hud->viewPanel, viewW, hud->viewPopH);
    if (hud->tool.scene != nullptr) {
        sceneFrameActorGizmo(hud->tool.scene, hud->toolMode);
    }
    paintTools(*hud);
    for (uint8_t channel = 0; channel < 3; ++channel) {
        applySnap(*hud, channel, hud->snapPick[channel]);
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
    filesOnImportAsk(state, hudAskImport, hud);
    matThumbsAttach(hud->thumbs, canvas);
    filesShow(state, true);
    hudRefreshActors(*hud);
    hudBuildInspector(canvas, hud, details, statsBody);
}

} // namespace burnhope
