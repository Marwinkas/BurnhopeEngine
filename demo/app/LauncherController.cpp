#include "app/LauncherController.hpp"

#include "gui/GuiHost.hpp"
#include "platform/Dialog.hpp"
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

void showList(LauncherState& l) {
    char node[128];
    guiHostSetShown(l.gui, "launcher.empty", l.recent.count == 0);
    for (uint32_t i = 0; i < kRecentCap; ++i) {
        std::snprintf(node, sizeof(node), "launcher.r%u", i);
        const bool live = i < l.recent.count;
        guiHostSetShown(l.gui, node, live);
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
        guiHostSetText(l.gui, openKey, text);
    }
}

void hide(LauncherState& l) {
    guiHostSetShown(l.gui, kVeil, false);
}

// Makes `dir` the project: the file browser roots there and it moves to the top of the recent list.
bool openProject(LauncherState& l, const std::filesystem::path& dir) {
    ProjectInfo info{};
    const std::string manifest = (dir / kProjectFile).string();
    if (!projectRead(manifest.c_str(), info)) {
        spdlog::warn("launcher: {} is not a project folder", dir.string());
        return false;
    }
    recentTouch(l.recent, dir.string().c_str(), static_cast<int64_t>(std::time(nullptr)));
    (void)recentSave(l.recent, l.recentFile);
    l.openRoot(l.app, dir);
    showList(l);
    hide(l);
    spdlog::info("project opened: {} ({})", info.name, dir.string());
    return true;
}

void ask(LauncherState& l, uint8_t what) {
    if (l.asking != 0 || l.window == nullptr) {
        return;
    }
    if (windowAskFile(*l.window, nullptr, 0, FileAsk::Folder)) {
        l.asking = what;
    }
}

void onClick(void* user, uint32_t action) {
    LauncherState& l = *static_cast<LauncherState*>(user);
    if (action == kActFileProjects) {
        launcherShow(l);
    } else if (action == kActLauncherClose) {
        hide(l);
    } else if (action == kActLauncherCreate) {
        ask(l, 1);
    } else if (action == kActLauncherOpen) {
        ask(l, 2);
    } else if (action >= kActLauncherRecent && action < kActLauncherRecent + kRecentCap) {
        const uint32_t i = action - kActLauncherRecent;
        if (i < l.recent.count && !openProject(l, l.recent.item[i].path)) {
            recentRemove(l.recent, i);
            (void)recentSave(l.recent, l.recentFile);
            showList(l);
        }
    } else if (action >= kActLauncherForget && action < kActLauncherForget + kRecentCap) {
        recentRemove(l.recent, action - kActLauncherForget);
        (void)recentSave(l.recent, l.recentFile);
        showList(l);
    }
}

} // namespace

bool launcherBind(LauncherState& l, GuiHost& gui, void* app, LauncherOpenFn openRoot) {
    l.gui = &gui;
    l.app = app;
    l.openRoot = openRoot;
    char screen[512]{};
    if (!platformResolveAsset("assets/gui/launcher.json", screen, sizeof(screen)) || !guiHostSpawnSnippet(l.gui, "", screen)) {
        spdlog::error("launcher: screen failed to mount");
        return false;
    }
    if (recentFile(l.recentFile, sizeof(l.recentFile))) {
        (void)recentLoad(l.recent, l.recentFile);
    }
    bool ok = true;
    for (uint32_t i = 0; i < kRecentCap; ++i) {
        ok = guiHostBindClick(l.gui, kActLauncherRecent + i, onClick, &l) && guiHostBindClick(l.gui, kActLauncherForget + i, onClick, &l) && ok;
    }
    for (const uint32_t action : std::initializer_list<uint32_t>{kActLauncherCreate, kActLauncherOpen, kActLauncherClose, kActFileProjects}) {
        ok = guiHostBindClick(l.gui, action, onClick, &l) && ok;
    }
    showList(l);
    return ok;
}

void launcherShow(LauncherState& l) {
    showList(l);
    guiHostSetShown(l.gui, kVeil, true);
}

void launcherSync(LauncherState& l, Window* window) {
    l.window = window;
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
    (void)openProject(l, dir);
}

} // namespace burnhope
