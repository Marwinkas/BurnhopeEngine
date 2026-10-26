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
// Short localized type shown under a tile name: folder, image, 3D model, material, scene, document.
const char* fileSubLabel(const char* name, int isDir, int kind);

// Extension-driven icon class for files without a thumbnail. Indexes Browser::classSlot[class - 1].
enum class FileIconClass : uint8_t { None = 0, Scene = 1, Doc = 2, Project = 3 };
constexpr uint8_t kFileIconClasses = 3;
FileIconClass fileIconClass(const char* name);
// Asset path of the SVG for `cls`.
const char* fileIconClassPath(FileIconClass cls);
// glTF / glb / fbx / obj / bhop: files that get a baked mesh preview.
bool fileIsModel(const char* name);

} // namespace burnhope
