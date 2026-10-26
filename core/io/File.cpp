#include "io/File.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace burnhope {

bool fileMapReadOnly(const char* path, MappedFile* out) {
    if (path == nullptr || out == nullptr) {
        return false;
    }
    out->data = nullptr;
    out->size = 0;
    out->fd = -1;
    out->map = nullptr;

#if defined(_WIN32)
    const HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart < 0) {
        CloseHandle(hFile);
        return false;
    }
    if (sz.QuadPart == 0) {
        CloseHandle(hFile);
        return true;
    }
    const HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (hMap == nullptr) {
        CloseHandle(hFile);
        return false;
    }
    void* mapped = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    CloseHandle(hFile);
    if (mapped == nullptr) {
        return false;
    }
    out->data = static_cast<const uint8_t*>(mapped);
    out->size = static_cast<size_t>(sz.QuadPart);
    return true;
#else
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
    out->map = mapped;
    return true;
#endif
}

bool fileExists(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st {};
    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

void fileUnmap(MappedFile* file) {
    if (file == nullptr) {
        return;
    }
#if defined(_WIN32)
    if (file->map != nullptr) {
        UnmapViewOfFile(file->map);
    }
#else
    if (file->map != nullptr && file->size > 0) {
        ::munmap(file->map, file->size);
    }
    if (file->fd >= 0) {
        ::close(file->fd);
    }
#endif
    file->data = nullptr;
    file->size = 0;
    file->fd = -1;
    file->map = nullptr;
}

} // namespace burnhope
