#pragma once

#include "platform/Input.hpp"

#include <cstdint>
#include <span>

struct SDL_Window;
struct SDL_Cursor;
struct VkInstance_T;
struct VkSurfaceKHR_T;

namespace burnhope {

constexpr int kWindowCap = 8;

// kind: 0 обычное, 1 utility, 2 tooltip, 3 popup. x и y < 0 — положение выбирает система.
struct WindowConfig {
    const char* title = "Burnhope";
    int x = -1;
    int y = -1;
    int width = 1600;
    int height = 900;
    int minWidth = 720;
    int minHeight = 480;
    int maxWidth = 0;
    int maxHeight = 0;
    bool bordered = false;
    bool resizable = true;
    bool hidden = false;
    bool alwaysOnTop = false;
    bool fullscreen = false;
    bool maximized = false;
    bool focusable = true;
    bool modal = false;
    bool pixelDensity = false;
    bool transparent = false;
    int kind = 0;
    float opacity = 1.0f;
    float aspectMin = 0.0f;
    float aspectMax = 0.0f;
    struct Window* parent = nullptr;
};

// Поля < 0 не трогают окно. flash: 0 нет, 1 коротко, 2 пока не фокус, -1 снять.
struct WindowCommand {
    bool minimize = false;
    bool toggleMaximize = false;
    bool raise = false;
    int alwaysOnTop = -1;
    int bordered = -1;
    int resizable = -1;
    int fullscreen = -1;
    int flash = 0;
    float opacity = -1.0f;
};

struct WindowState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    uint32_t pixelW = 0;
    uint32_t pixelH = 0;
    float opacity = 1.0f;
    float pixelDensity = 1.0f;
    float displayScale = 1.0f;
    int safeX = 0;
    int safeY = 0;
    int safeW = 0;
    int safeH = 0;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
    bool hidden = false;
    bool focused = false;
    bool mouseFocus = false;
    bool alwaysOnTop = false;
    bool bordered = false;
    bool resizable = false;
};

struct DisplayInfo {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int usableX = 0;
    int usableY = 0;
    int usableW = 0;
    int usableH = 0;
    float scale = 1.0f;
    int theme = 0;
};

struct Window {
    SDL_Window* handle = nullptr;
    uint32_t pixelW = 0;
    uint32_t pixelH = 0;
    bool closeRequested = false;
    bool resized = false;
    bool moved = false;
    float titleBarPx = 40.0f;
    float resizeBorderPx = 6.0f;
    uint32_t lastLeftMs = 0;
    uint8_t lastClickN = 0;
    float lastClickX = 0;
    float lastClickY = 0;
    float dpi = 1;
    float dpiApplied = 1;
    bool dpiChanged = false;
    bool hidden = false;
    bool focused = true;
    bool relOn = false;
    // Кадр, когда захват только включился: прыжок курсора в центр не крутит взгляд.
    bool relEat = false;
    bool smooth = false;
    float relX = 0;
    float relY = 0;
    // Сумма xrel/yrel за кадр. В захвате курсор прыгает в центр, позиция для поворота не годится.
    float motionDx = 0;
    float motionDy = 0;
    float smoothDx = 0;
    float smoothDy = 0;
    uint16_t holdMs[512]{};
    SDL_Cursor* colorCursor = nullptr;
    int cursorKind = -1;
    SDL_Cursor* cursors[12]{};
    InputFrame input{};
};

enum class FileAsk : uint8_t { Open = 0, Save = 1, Folder = 2, OpenMany = 3 };

struct FileFilter {
    const char* name = nullptr;
    const char* pattern = nullptr;
};

[[nodiscard]] bool windowCreate(Window& w, const WindowConfig& config);
[[nodiscard]] bool windowCreate(Window& w, int width, int height, const char* title);
void windowDestroy(Window& w);
void windowPump(Window& w);
void windowsPump(Window& primary, Window* extra = nullptr, Window* third = nullptr);
void windowsPump(Window** list, int count);
void windowSetCursor(Window& w, int kind);
void windowRefreshSize(Window& w);
void windowChrome(Window& w, float titleBarPx, float resizeBorderPx);

bool windowTitle(Window& w, const char* title);
bool windowPosition(Window& w, int x, int y);
bool windowSize(Window& w, int width, int height);
bool windowMinSize(Window& w, int width, int height);
bool windowMaxSize(Window& w, int width, int height);
bool windowBordered(Window& w, bool bordered);
bool windowResizable(Window& w, bool resizable);
bool windowOpacity(Window& w, float opacity);
bool windowAlwaysOnTop(Window& w, bool onTop);
bool windowFullscreen(Window& w, bool fullscreen);
bool windowMaximize(Window& w);
bool windowMinimize(Window& w);
bool windowRestore(Window& w);
bool windowToggleMaximize(Window& w);
bool windowShow(Window& w);
bool windowHide(Window& w);
bool windowRaise(Window& w);
bool windowParent(Window& w, Window* parent);
bool windowModal(Window& w, bool modal);
bool windowFocusable(Window& w, bool focusable);
bool windowGrab(Window& w, bool keyboard, bool mouse);
bool windowRelativeMouse(Window& w, bool on);
bool windowAspect(Window& w, float minAspect, float maxAspect);
bool windowFlash(Window& w, int op);
bool windowProgress(Window& w, int state, float value);
bool windowFillDocument(Window& w, bool fill);
bool windowSync(Window& w);
bool windowSystemMenu(Window& w, int x, int y);
bool windowMouseRect(Window& w, int x, int y, int width, int height);
bool windowIcon(Window& w, int width, int height, const uint8_t* rgba);
bool windowShape(Window& w, int width, int height, const uint8_t* rgba);
void windowApply(Window& w, const WindowCommand& command);
[[nodiscard]] bool windowQuery(const Window& w, WindowState& out);
[[nodiscard]] bool windowDisplay(const Window& w, DisplayInfo& out);

void windowText(Window& w, bool on);
void windowTextArea(Window& w, int x, int y, int width, int height);
// Инстанс-расширения Vulkan для поверхности; пусто, если видео-драйвер не поднят.
[[nodiscard]] std::span<const char* const> windowVulkanExtensions();
[[nodiscard]] bool windowCreateSurface(Window& w, VkInstance_T* instance, VkSurfaceKHR_T*& out);
void platformSleep(int ms);
void platformPace(int hz, uint64_t& nextTick);
[[nodiscard]] uint16_t windowHoldMs(const Window& w, int scancode);
bool windowFullscreenKind(Window& w, int kind);
bool windowCenter(Window& w);
bool windowToDisplay(Window& w, int displayIndex);
bool windowClickThrough(Window& w, bool on);
bool windowMouseSmooth(Window& w, bool on);
bool windowCursorRgba(Window& w, int width, int height, const uint8_t* rgba, int hotX, int hotY);
void windowRemember(const Window& w, const char* name);
void windowRecall(WindowConfig& config, const char* name);
void windowClipboardSet(const char* text);
[[nodiscard]] bool windowClipboardGet(char* out, int cap);

// Фильтры копируются. Пока диалог открыт, второй вызов возвращает false.
[[nodiscard]] bool windowAskFile(Window& w, const FileFilter* filters, int count, FileAsk ask);
[[nodiscard]] bool windowTakeFile(char* out, int cap);
// Результат FileAsk::OpenMany: пути подряд, каждый завершён '\0'. Возвращает число путей; windowTakeFile такой результат не забирает.
[[nodiscard]] int windowTakeFiles(char* out, int cap);

[[nodiscard]] const char* platformError();
[[nodiscard]] int platformTheme();
void platformScreensaver(bool enabled);
void platformShutdown();

} // namespace burnhope
