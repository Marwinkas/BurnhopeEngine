#pragma once

#include "Str.hpp"
#include "Units.hpp"

#include <cstdint>

namespace burnhope {

void joinPath(char* dst, int cap, const char* dir, const char* name);
void parentOf(const char* path, char* dst, int cap);
void uniquePath(char* dst, int cap, const char* dir, const char* stem, const char* ext);
void splitStem(const char* name, char* stem, int stemCap, char* ext, int extCap);
int fileKind(const char* name, int isDir);
const char* fileTypeName(int isDir, int kind);

} // namespace burnhope
