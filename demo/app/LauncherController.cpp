#include "app/LauncherController.hpp"

#include "app/BrowserController.hpp"
#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "asset/ProjectDoc.hpp"
#include "platform/PlatformUtil.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <initializer_list>

namespace burnhope {
namespace {

constexpr const char* kVeil = "launcher.veil";
constexpr const char* kProjectFile = "project.proj";

void rowPath(char* out, size_t cap, uint32_t i) {
    std::snprintf(out, cap, "launcher.r%u.open", i);
}

void showList(Engine& e) {
    LauncherState& l = e.launcher;
    char node[128];
    guiHostSetShown(e.gui, "launcher.empty", l.recent.count == 0);
    for (uint32_t i = 0; i < kRecentCap; ++i) {
        std::snprintf(node, sizeof(node), "launcher.r%u", i);
        const bool live = i < l.recent.count;
        guiHostSetShown(e.gui, node, live);
        if (!live) {
            continue;
        }
        const RecentProject& entry = l.recent.item[i];
        const char* leaf = strPathLeaf(entry.path);
        char when[40]{};
        const std::time_t stamp = static_cast<std::time_t>(entry.opened);
        std::tm local{};
        if (entry.opened > 0 && platformLocalTime(&stamp, &local)) {
            std::snprintf(when, sizeof(when), "  %04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                          local.tm_min);
        }
        char text[128]{};
        char openKey[40]{};
        std::snprintf(text, sizeof(text), "%s\n%.80s%s", leaf[0] != '\0' ? leaf : entry.path, entry.path, when);
        rowPath(openKey, sizeof(openKey), i);
        guiHostSetText(e.gui, openKey, text);
    }
}

void hide(Engine& e) {
    guiHostSetShown(e.gui, kVeil, false);
}

// Makes `dir` the project: the file browser roots there and it moves to the top of the recent list.
bool openProject(Engine& e, const std::filesystem::path& dir) {
    ProjectInfo info{};
    const std::string manifest = (dir / kProjectFile).string();
    if (!projectRead(manifest.c_str(), info)) {
        spdlog::warn("launcher: {} is not a project folder", dir.string());
        return false;
    }
    LauncherState& l = e.launcher;
    recentTouch(l.recent, dir.string().c_str(), static_cast<int64_t>(std::time(nullptr)));
    (void)recentSave(l.recent, l.recentFile);
    browserSetRoot(e, dir);
    showList(e);
    hide(e);
    spdlog::info("project opened: {} ({})", info.name, dir.string());
    return true;
}

void ask(Engine& e, uint8_t what) {
    LauncherState& l = e.launcher;
    if (l.asking != 0 || e.slot < 0) {
        return;
    }
    if (windowAskFile(e.host.view[e.slot].window, nullptr, 0, FileAsk::Folder)) {
        l.asking = what;
    }
}

void onClick(void* user, uint32_t action) {
    Engine& e = *static_cast<Engine*>(user);
    LauncherState& l = e.launcher;
    if (action == kActFileProjects) {
        launcherShow(e);
    } else if (action == kActLauncherClose) {
        hide(e);
    } else if (action == kActLauncherCreate) {
        ask(e, 1);
    } else if (action == kActLauncherOpen) {
        ask(e, 2);
    } else if (action >= kActLauncherRecent && action < kActLauncherRecent + kRecentCap) {
        const uint32_t i = action - kActLauncherRecent;
        if (i < l.recent.count && !openProject(e, l.recent.item[i].path)) {
            recentRemove(l.recent, i);
            (void)recentSave(l.recent, l.recentFile);
            showList(e);
        }
    } else if (action >= kActLauncherForget && action < kActLauncherForget + kRecentCap) {
        recentRemove(l.recent, action - kActLauncherForget);
        (void)recentSave(l.recent, l.recentFile);
        showList(e);
    }
}

} // namespace

bool launcherBind(Engine& e) {
    LauncherState& l = e.launcher;
    char screen[512]{};
    if (!platformResolveAsset("assets/gui/launcher.json", screen, sizeof(screen)) || !guiHostMount(e.gui, screen, "")) {
        spdlog::error("launcher: screen failed to mount");
        return false;
    }
    if (recentFile(l.recentFile, sizeof(l.recentFile))) {
        (void)recentLoad(l.recent, l.recentFile);
    }
    bool ok = true;
    for (uint32_t i = 0; i < kRecentCap; ++i) {
        ok = guiHostBindClick(e.gui, kActLauncherRecent + i, onClick, &e) && guiHostBindClick(e.gui, kActLauncherForget + i, onClick, &e) && ok;
    }
    for (const uint32_t action : std::initializer_list<uint32_t>{kActLauncherCreate, kActLauncherOpen, kActLauncherClose, kActFileProjects}) {
        ok = guiHostBindClick(e.gui, action, onClick, &e) && ok;
    }
    showList(e);
    return ok;
}

void launcherShow(Engine& e) {
    showList(e);
    guiHostSetShown(e.gui, kVeil, true);
}

void launcherSync(Engine& e) {
    LauncherState& l = e.launcher;
    char picked[512]{};
    if (l.asking == 0 || !windowTakeFile(picked, sizeof(picked))) {
        return;
    }
    const uint8_t what = l.asking;
    l.asking = 0;
    const std::filesystem::path dir(picked);
    if (what == 1) {
        ProjectInfo info{};
        (void)strCopyCap(info.name, sizeof(info.name), strPathLeaf(picked));
        if (!projectWrite(picked, info)) {
            spdlog::error("launcher: cannot create a project in {}", picked);
            return;
        }
    }
    (void)openProject(e, dir);
}

} // namespace burnhope
