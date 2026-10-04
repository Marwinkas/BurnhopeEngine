#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct MappedFile {
    const uint8_t* data = nullptr;
    size_t size = 0;
    int fd = -1;
};

[[nodiscard]] bool fileMapReadOnly(const char* path, MappedFile* out);
void fileUnmap(MappedFile* file);

} // namespace burnhope
