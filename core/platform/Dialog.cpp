#include "platform/Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace burnhope {
namespace {

constexpr int kFilterCap = 8;

std::atomic<int> gBusy{0};
std::atomic<uint32_t> gGen{0};
uint32_t gSeen = 0;
char gPath[512]{};
char gDir[512]{};
SDL_DialogFileFilter gFilters[kFilterCap]{};
bool gDirLoaded = false;

void loadDir() {
    if (gDirLoaded) {
        return;
    }
    gDirLoaded = true;
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/.cache/burnhope-dialog.txt", home);
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return;
    }
    if (std::fgets(gDir, sizeof(gDir), file) != nullptr) {
        size_t n = std::strlen(gDir);
        while (n > 0 && (gDir[n - 1] == '\n' || gDir[n - 1] == '\r')) {
            gDir[--n] = '\0';
        }
    }
    std::fclose(file);
}

void rememberFolder(const char* file) {
    const char* slash = std::strrchr(file, '/');
    if (slash == nullptr || slash == file) {
        return;
    }
    const size_t n = static_cast<size_t>(slash - file);
    if (n >= sizeof(gDir)) {
        return;
    }
    std::memcpy(gDir, file, n);
    gDir[n] = '\0';
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    char path[640]{};
    std::snprintf(path, sizeof(path), "%s/.cache/burnhope-dialog.txt", home);
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
        return;
    }
    std::fputs(gDir, out);
    std::fputc('\n', out);
    std::fclose(out);
}

void SDLCALL onChosen(void*, const char* const* list, int) {
    gPath[0] = '\0';
    if (list != nullptr && list[0] != nullptr) {
        std::strncpy(gPath, list[0], sizeof(gPath) - 1);
        gPath[sizeof(gPath) - 1] = '\0';
        rememberFolder(gPath);
    }
    gGen.fetch_add(1, std::memory_order_release);
    gBusy.store(0, std::memory_order_release);
}

} // namespace

bool windowAskFile(Window& w, const FileFilter* filters, int count, FileAsk ask) {
    if (w.handle == nullptr) {
        return false;
    }
    if (gBusy.exchange(1, std::memory_order_acq_rel) != 0) {
        return false;
    }
    loadDir();
    int n = 0;
    if (filters != nullptr) {
        if (count > kFilterCap) {
            count = kFilterCap;
        }
        for (int i = 0; i < count; ++i) {
            if (filters[i].name == nullptr || filters[i].pattern == nullptr) {
                continue;
            }
            gFilters[n].name = filters[i].name;
            gFilters[n].pattern = filters[i].pattern;
            ++n;
        }
    }
    const char* folder = gDir[0] != '\0' ? gDir : nullptr;
    const SDL_DialogFileFilter* list = n > 0 ? gFilters : nullptr;
    if (ask == FileAsk::Save) {
        SDL_ShowSaveFileDialog(onChosen, nullptr, w.handle, list, n, folder);
    } else if (ask == FileAsk::Folder) {
        SDL_ShowOpenFolderDialog(onChosen, nullptr, w.handle, folder, false);
    } else {
        SDL_ShowOpenFileDialog(onChosen, nullptr, w.handle, list, n, folder, false);
    }
    return true;
}

bool windowTakeFile(char* out, int cap) {
    const uint32_t gen = gGen.load(std::memory_order_acquire);
    if (gen == gSeen || out == nullptr || cap <= 0) {
        return false;
    }
    gSeen = gen;
    std::strncpy(out, gPath, static_cast<size_t>(cap - 1));
    out[cap - 1] = '\0';
    return out[0] != '\0';
}

} // namespace burnhope
