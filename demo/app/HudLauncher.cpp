#include "app/Engine.hpp"
#include "app/HudBuild.hpp"

#include "host/Launcher.hpp"
#include "ui/rhi/Canvas.hpp"

#include <filesystem>

namespace burnhope {

namespace {

void saveRecent(HudContext& hud) {
    char file[512]{};
    if (recentFile(file, sizeof(file))) {
        (void)recentSave(hud.recent, file);
    }
    hud.launcherDirty = 1;
}

void onCreate(void* user) { cmdFileNew(user, 0); }
void onOpen(void* user) { cmdFileOpen(user, 0); }

void onRecent(void* user, uint32_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || index >= hud->recent.count) {
        return;
    }
    char folder[260]{};
    std::snprintf(folder, sizeof(folder), "%s", hud->recent.item[index].path);
    std::error_code gone;
    if (!std::filesystem::is_directory(folder, gone)) {
        recentRemove(hud->recent, index);
        saveRecent(*hud);
        return;
    }
    projectTake(*hud, folder, 2);
}

void onForget(void* user, uint32_t index) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud != nullptr && index < hud->recent.count) {
        recentRemove(hud->recent, index);
        saveRecent(*hud);
    }
}

} // namespace

void hudBuildLauncher(Canvas& canvas, HudContext* hud) {
    if (canvas.state == nullptr || hud == nullptr) {
        return;
    }
    char file[512]{};
    if (recentFile(file, sizeof(file))) {
        (void)recentLoad(hud->recent, file);
    }
    hud->launcher = uiLauncher(*canvas.state, {
        .onCreate = onCreate,
        .onOpen = onOpen,
        .onRecent = onRecent,
        .onForget = onForget,
        .user = hud,
    });
    hud->launcherDirty = 1;
}

void hudLauncherSync(Engine& e) {
    HudContext& hud = e.hud;
    UiState* state = hudUi(hud);
    if (state == nullptr || hud.launcher == kUiNone) {
        return;
    }
    if (hud.launcherDirty != 0) {
        hud.launcherDirty = 0;
        uiLauncherList(*state, hud.launcher, hud.recent);
    }
    uiLauncherShow(*state, hud.launcher, hud.projectRoot[0] == '\0');
}

} // namespace burnhope
