#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace burnhope {

inline std::string executableDirectory() {
#ifdef _WIN32
  char buffer[MAX_PATH];
  GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  return std::filesystem::path(buffer).parent_path().string();
#else
  char buffer[PATH_MAX];
  const ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
  if (count == -1) {
    return {};
  }
  return std::filesystem::path(std::string(buffer, static_cast<std::size_t>(count)))
      .parent_path()
      .string();
#endif
}

} // namespace burnhope
