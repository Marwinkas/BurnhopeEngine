#include "host/Launcher.hpp"

#include "platform/PlatformUtil.hpp"
#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Nodes.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Template.hpp"
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

uint16_t spawnRow(UiState& s, uint16_t list, uint16_t launcher) {
    const UiTemplateId tpl = uiTemplateFind(s, "core/launcher_row");
    const uint16_t id = uiSpawnTemplate(s, list, tpl);
    if (id >= s.count) {
        return id;
    }
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paintHoverToken(s.look, *paint, UiToken::Text, 0.10f);
    }
    UiLauncherRow row{};
    row.launcher = launcher;
    row.name = uiTemplateSlot(s, tpl, id, "name");
    row.path = uiTemplateSlot(s, tpl, id, "path");
    row.date = uiTemplateSlot(s, tpl, id, "date");
    row.forget = uiButton(s, id, {.cls = "tool_button_sm", .icon = UiIcon::Dismiss});
    if (row.forget < s.count) {
        uiBind(s, row.forget, forgetClick, &s);
    }
    s.ent[id].set<UiLauncherRow>(row);
    uiBind(s, id, rowClick, &s);
    uiShow(s, id, false);
    return id;
}

} // namespace

uint16_t uiLauncher(UiState& s, const LauncherConfig& cfg) {
    // Layout only solves the tree under node 0, so the veil must hang from it (a parentless node is never laid out).
    const UiTemplateId tpl = uiTemplateFind(s, "core/launcher");
    const uint16_t id = uiSpawnTemplate(s, s.count > 0 ? 0 : kUiNone, tpl);
    if (id >= s.count) {
        return id;
    }
    uiPlace(s, id, true, 0.0f, 0.0f, 0.0f, 0.0f);
    const uint16_t card = uiTemplateSlot(s, tpl, id, "card");
    s.ent[card].set<UiPaint>(paintPopup(s.look));
    uiText(s, uiTemplateSlot(s, tpl, id, "title"), "Burnhope");
    uiText(s, uiTemplateSlot(s, tpl, id, "recent"), "Recent Projects");
    const uint16_t bar = uiTemplateSlot(s, tpl, id, "bar");
    (void)uiButton(s, bar, {.cls = "launcher_button", .label = "Create Project", .onClick = createClick, .user = &s});
    (void)uiButton(s, bar, {.cls = "launcher_button", .label = "Open Project", .onClick = openClick, .user = &s});
    UiLauncher box{};
    box.cfg = cfg;
    const uint16_t list = uiTemplateSlot(s, tpl, id, "list");
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
        char trimmed[sizeof(entry.path)]{};
        std::snprintf(trimmed, sizeof(trimmed), "%s", entry.path);
        size_t len = std::strlen(trimmed);
        while (len > 1 && (trimmed[len - 1] == '/' || trimmed[len - 1] == '\\')) {
            trimmed[--len] = '\0';
        }
        const char* leaf = strPathLeaf(trimmed);
        uiText(s, row->name, leaf[0] != '\0' ? leaf : trimmed);
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
