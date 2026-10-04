#include "io/File.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace burnhope {

bool fileMapReadOnly(const char* path, MappedFile* out) {
    if (path == nullptr || out == nullptr) {
        return false;
    }
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < 0) {
        ::close(fd);
        return false;
    }
    out->fd = fd;
    out->size = static_cast<size_t>(st.st_size);
    out->data = nullptr;
    if (st.st_size == 0) {
        return true;
    }
    void* mapped = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        out->fd = -1;
        out->size = 0;
        return false;
    }
    out->data = static_cast<const uint8_t*>(mapped);
    return true;
}

void fileUnmap(MappedFile* file) {
    if (file == nullptr) {
        return;
    }
    if (file->data != nullptr && file->size > 0) {
        ::munmap(const_cast<uint8_t*>(file->data), static_cast<size_t>(file->size));
    }
    if (file->fd >= 0) {
        ::close(file->fd);
    }
    file->data = nullptr;
    file->size = 0;
    file->fd = -1;
}

} // namespace burnhope
