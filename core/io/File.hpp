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

[[nodiscard]] bool fileMapReadOnly(const char* path, MappedFile* out);
void fileUnmap(MappedFile* file);
// True when `path` names an existing regular file; nothing is opened or mapped.
[[nodiscard]] bool fileExists(const char* path);

} // namespace burnhope
