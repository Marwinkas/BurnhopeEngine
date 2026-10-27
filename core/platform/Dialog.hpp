#pragma once

#include <cstdint>

namespace burnhope {

struct Window;

// Native open / save / folder dialogs. The dialog runs beside the frame loop; the result is taken in a later frame.
enum class FileAsk : uint8_t { Open = 0, Save = 1, Folder = 2, OpenMany = 3 };

struct FileFilter {
    const char* name = nullptr;
    const char* pattern = nullptr;
};

// Фильтры копируются. Пока диалог открыт, второй вызов возвращает false.
[[nodiscard]] bool windowAskFile(Window& w, const FileFilter* filters, int count, FileAsk ask);
[[nodiscard]] bool windowTakeFile(char* out, int cap);
// Результат FileAsk::OpenMany: пути подряд, каждый завершён '\0'. Возвращает число путей; windowTakeFile такой результат не забирает.
[[nodiscard]] int windowTakeFiles(char* out, int cap);

} // namespace burnhope
