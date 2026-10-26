#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct MappedFile {
    const uint8_t* data = nullptr;
    size_t size = 0;
    int fd = -1;
    void* map = nullptr; // mapping base handed back to munmap
};

// Copies `path` into `out` without trailing CR, LF, spaces, tabs and quotes (CRLF-saved text files).
// Returns the trimmed length, or 0 when it does not fit `cap`.
size_t fileTrimPath(const char* path, char* out, size_t cap);

[[nodiscard]] bool fileMapReadOnly(const char* path, MappedFile* out);
void fileUnmap(MappedFile* file);
// True when `path` names an existing regular file; nothing is opened or mapped.
[[nodiscard]] bool fileExists(const char* path);

} // namespace burnhope
