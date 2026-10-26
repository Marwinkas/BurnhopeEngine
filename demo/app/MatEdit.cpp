#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"
#include "app/SceneBlockFill.hpp"

#include "color/Color.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/Nodes.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {

UiState* matPageState(HudContext& hud) {
    if (hud.tool.canvas != nullptr && hud.tool.canvas->state != nullptr) {
        return hud.tool.canvas->state;
    }
    return hudUi(hud);
}

void matApplyFlag(HudContext& hud, UiState& s, uint16_t index, uint8_t value) {
    if (index < 64u) {
        hud.block.flags[index] = value;
    }
    switch (index) {
    case kFlMatWorld: hud.matDoc.worldAligned = value; break;
    case kFlMatDouble: hud.matDoc.doubleSided = value; break;
    case kFlMatFlip: hud.matDoc.flipNormalY = value; break;
    case kFlMatUnlit: hud.matDoc.unlit = value; break;
    case kFlMatRepeat: hud.matDoc.repeat = value; break;
    case kFlMatPack: hud.matDoc.mapPack = value; break;
    default: break;
    }
    uiCheckAssign(s, hud.bind.idOf(index, kBindFlag), value);
    if (index == kFlMatUnlit) {
        uiChoiceSelect(s, hud.matType, value, value != 0 ? "Unlit" : "Lit");
    }
    if (index == kFlMatPack) {
        uiChoiceSelect(s, hud.matPack, value, value != 0 ? "ORMH" : "Classic");
        if (value == 0) {
            hud.matDoc.ormPath[0] = '\0';
        } else {
            hudCommitMaps(hud, 255);
        }
        hud.atlasDirty = 1;
    }
}

void matOnClear(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->kind < 1) {
        return;
    }
    const uint8_t channel = static_cast<uint8_t>(hit->kind - 1u);
    hudDocPath(hit->hud->matDoc, channel)[0] = '\0';
    hudCommitMaps(*hit->hud, channel);
    hit->hud->atlasDirty = 1;
    hit->hud->previewDirty = 1;
    hudPaintMaterial(*hit->hud);
    saveOpenMaterial(*hit->hud);
}

void matOnPath(void* user, const char* path) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || path == nullptr || hit->kind < 1) {
        return;
    }
    hudTakeTexture(*hit->hud, static_cast<uint8_t>(hit->kind - 1u), path);
}

void matOnReset(void* user, UiState& s, const UiResetHome& home) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    switch (static_cast<UiReset>(home.kind)) {
    case UiReset::Flag:
        matApplyFlag(*hud, s, home.index, home.home >= 0.5f ? 1u : 0u);
        break;
    case UiReset::Custom:
        hudDocPath(hud->matDoc, static_cast<uint8_t>(home.index))[0] = '\0';
        hudCommitMaps(*hud, static_cast<uint8_t>(home.index));
        hud->atlasDirty = 1;
        hud->previewDirty = 1;
        break;
    case UiReset::Rgb:
        hud->previewDirty = 1;
        hud->atlasDirty = 1;
        break;
    default:
        break;
    }
    hudPaintMaterial(*hud);
    saveOpenMaterial(*hud);
}

} // namespace burnhope
