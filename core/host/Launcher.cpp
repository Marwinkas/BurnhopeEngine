#include "host/Launcher.hpp"

#include "platform/PlatformUtil.hpp"
#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Nodes.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Label.hpp"
#include "ui/State.hpp"
#include "Str.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace burnhope {
namespace {

constexpr float kCardW = 560.0f;
constexpr float kRowH = 44.0f;
constexpr float kDateW = 112.0f;

UiLauncher* launcherOf(UiState& s, uint16_t id) {
    return id < s.count ? s.ent[id].try_get_mut<UiLauncher>() : nullptr;
}

void createClick(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiLauncher* box = launcherOf(s, s.parentOf[s.parentOf[s.parentOf[id]]]);
    if (box != nullptr && box->cfg.onCreate != nullptr) {
        box->cfg.onCreate(box->cfg.user);
    }
}

void openClick(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiLauncher* box = launcherOf(s, s.parentOf[s.parentOf[s.parentOf[id]]]);
    if (box != nullptr && box->cfg.onOpen != nullptr) {
        box->cfg.onOpen(box->cfg.user);
    }
}

void rowClick(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiLauncherRow* row = id < s.count ? s.ent[id].try_get<UiLauncherRow>() : nullptr;
    const UiLauncher* box = row != nullptr ? launcherOf(s, row->launcher) : nullptr;
    if (box != nullptr && box->cfg.onRecent != nullptr) {
        box->cfg.onRecent(box->cfg.user, row->index);
    }
}

void forgetClick(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const uint16_t rowId = id < s.count ? s.parentOf[id] : kUiNone;
    const UiLauncherRow* row = rowId < s.count ? s.ent[rowId].try_get<UiLauncherRow>() : nullptr;
    const UiLauncher* box = row != nullptr ? launcherOf(s, row->launcher) : nullptr;
    if (box != nullptr && box->cfg.onForget != nullptr) {
        box->cfg.onForget(box->cfg.user, row->index);
    }
}

uint16_t column(UiState& s, uint16_t parent, float width, float gap) {
    UiFlex flex{};
    flex.direction = UiFlexDir::Column;
    flex.gap = gap;
    flex.shrink = 0.0f;
    if (width > 0.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = width;
    }
    return uiPanel(s, parent, flex, paintClear());
}

uint16_t cell(UiState& s, uint16_t parent, float width, float height, UiTextAlign align, UiTextEllipsis ellipsis) {
    UiFlex flex{};
    if (width > 0.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = width;
        flex.shrink = 0.0f;
    } else {
        flex.grow = 1.0f;
        flex.shrink = 1.0f;
        flex.minW = 0.0f;
    }
    flex.heightMode = UiSize::Px;
    flex.height = height;
    const uint16_t id = uiPanel(s, parent, flex, paintLabel(s.look));
    uiTextStyle(s, id, align, ellipsis, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiSetPass(s, id);
    return id;
}

uint16_t spawnRow(UiState& s, uint16_t list, uint16_t launcher) {
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.widthMode = UiSize::Percent;
    line.width = 100.0f;
    line.heightMode = UiSize::Px;
    line.height = kRowH;
    line.padL = 10.0f;
    line.padR = 6.0f;
    line.gap = 8.0f;
    line.shrink = 0.0f;
    UiPaint paint = paintToken(s.look, UiToken::Item, s.look.radiusSm);
    paint.a = 0.0f;
    paint.hr = s.look.text[0];
    paint.hg = s.look.text[1];
    paint.hb = s.look.text[2];
    paint.ha = 0.10f;
    const uint16_t id = uiPanel(s, list, line, paint);
    if (id >= s.count) {
        return id;
    }
    UiLauncherRow row{};
    row.launcher = launcher;
    UiFlex stack{};
    stack.direction = UiFlexDir::Column;
    stack.grow = 1.0f;
    stack.shrink = 1.0f;
    stack.minW = 0.0f;
    const uint16_t text = uiPanel(s, id, stack, paintClear());
    uiSetPass(s, text);
    row.name = cell(s, text, 0.0f, 22.0f, UiTextAlign::Left, UiTextEllipsis::End);
    row.path = cell(s, text, 0.0f, 16.0f, UiTextAlign::Left, UiTextEllipsis::End);
    if (auto* paintPath = s.ent[row.path].try_get_mut<UiPaint>()) {
        tokenRgb(s.look, UiToken::Muted, paintPath->r, paintPath->g, paintPath->b);
    }
    row.date = cell(s, id, kDateW, 22.0f, UiTextAlign::Right, UiTextEllipsis::End);
    row.forget = uiIconBtn(s, id, static_cast<uint8_t>(UiIcon::Dismiss), 22.0f);
    if (row.forget < s.count) {
        uiBind(s, row.forget, forgetClick, &s);
    }
    s.ent[id].set<UiLauncherRow>(row);
    uiHover(s, id);
    uiSetFocusable(s, id);
    uiBind(s, id, rowClick, &s);
    uiShow(s, id, false);
    return id;
}

} // namespace

uint16_t uiLauncher(UiState& s, const LauncherConfig& cfg) {
    UiFlex veil{};
    veil.position = UiPosition::Absolute;
    veil.posX = 0.0f;
    veil.posY = 0.0f;
    veil.widthMode = UiSize::Percent;
    veil.width = 100.0f;
    veil.heightMode = UiSize::Percent;
    veil.height = 100.0f;
    veil.align = UiAlign::Center;
    veil.justify = UiJustify::Center;
    veil.shrink = 0.0f;
    UiPaint dim = paintToken(s.look, UiToken::Window, 0.0f);
    dim.a = 0.82f;
    dim.borderW = 0.0f;
    const uint16_t id = uiNode(s, kUiNone, veil, dim);
    if (id >= s.count) {
        return id;
    }
    uiSetFocusable(s, id);
    UiFlex cardFlex{};
    cardFlex.direction = UiFlexDir::Column;
    cardFlex.widthMode = UiSize::Px;
    cardFlex.width = kCardW;
    cardFlex.pad = 20.0f;
    cardFlex.gap = 14.0f;
    cardFlex.shrink = 0.0f;
    const uint16_t card = uiPanel(s, id, cardFlex, paintPopup(s.look));
    (void)uiLabel(s, card, "Burnhope");
    UiFlex buttons{};
    buttons.direction = UiFlexDir::Row;
    buttons.gap = 10.0f;
    buttons.shrink = 0.0f;
    const uint16_t bar = uiPanel(s, card, buttons, paintClear());
    (void)uiButton(s, bar, {.label = "Create Project", .width = 160.0f, .height = 32.0f, .onClick = createClick, .user = &s});
    (void)uiButton(s, bar, {.label = "Open Project", .width = 160.0f, .height = 32.0f, .onClick = openClick, .user = &s});
    UiLauncher box{};
    box.cfg = cfg;
    (void)uiLabel(s, card, "Recent Projects");
    const uint16_t list = column(s, card, 0.0f, 2.0f);
    box.empty = uiLabel(s, list, "No recent projects");
    for (uint16_t& row : box.row) {
        row = spawnRow(s, list, id);
    }
    s.ent[id].set<UiLauncher>(box);
    uiShow(s, id, false);
    return id;
}

void uiLauncherList(UiState& s, uint16_t launcher, const RecentList& list) {
    const UiLauncher* box = launcherOf(s, launcher);
    if (box == nullptr) {
        return;
    }
    uiShow(s, box->empty, list.count == 0);
    for (uint32_t i = 0; i < kRecentCap; ++i) {
        const uint16_t id = box->row[i];
        auto* row = id < s.count ? s.ent[id].try_get_mut<UiLauncherRow>() : nullptr;
        if (row == nullptr) {
            continue;
        }
        const bool live = i < list.count;
        uiShow(s, id, live);
        if (!live) {
            continue;
        }
        row->index = i;
        const RecentProject& entry = list.item[i];
        const char* leaf = strPathLeaf(entry.path);
        uiText(s, row->name, leaf[0] != '\0' ? leaf : entry.path);
        const size_t len = std::strlen(entry.path);
        char shown[96]{};
        if (len < sizeof(shown)) {
            std::snprintf(shown, sizeof(shown), "%s", entry.path);
        } else {
            std::snprintf(shown, sizeof(shown), "...%s", entry.path + len - (sizeof(shown) - 5u));
        }
        uiText(s, row->path, shown);
        const std::time_t stamp = static_cast<std::time_t>(entry.opened);
        std::tm local{};
        char when[64]{};
        if (entry.opened > 0 && platformLocalTime(&stamp, &local)) {
            std::snprintf(when, sizeof(when), "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
        }
        uiText(s, row->date, when);
    }
    uiNotifyLayoutDirty(s);
}

void uiLauncherShow(UiState& s, uint16_t launcher, bool on) {
    if (launcherOf(s, launcher) != nullptr) {
        uiShow(s, launcher, on);
    }
}

} // namespace burnhope
