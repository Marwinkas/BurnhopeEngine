#include "asset/ProjectDoc.hpp"

#include "Str.hpp"
#include "image/ImageDecode.hpp"
#include "io/Json.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {
namespace {

void copyCap(char* dst, uint32_t cap, const char* src, uint32_t len) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    std::memcpy(dst, src, len);
    dst[len] = '\0';
}

void copyJson(const JsonDocument& doc, uint32_t parent, const char* key, char* dst, uint32_t cap) {
    const uint32_t id = jsonFind(doc, parent, key);
    const char* data = nullptr;
    uint32_t len = 0;
    if (id != kJsonNone && jsonString(doc, id, data, len)) {
        copyCap(dst, cap, data, len);
    }
}

float numOr(const JsonDocument& doc, uint32_t parent, const char* key, float fallback) {
    const uint32_t id = jsonFind(doc, parent, key);
    double value = 0.0;
    if (id != kJsonNone && jsonNumber(doc, id, value)) {
        return static_cast<float>(value);
    }
    return fallback;
}

void quote(std::FILE* out, const char* text) {
    std::fputc('"', out);
    if (text != nullptr) {
        for (const char* p = text; *p != '\0'; ++p) {
            if (*p == '"' || *p == '\\') {
                std::fputc('\\', out);
            }
            if (*p != '\n' && *p != '\r') {
                std::fputc(*p, out);
            }
        }
    }
    std::fputc('"', out);
}

} // namespace

bool projectWrite(const char* dir, const ProjectInfo& info) {
    if (dir == nullptr || dir[0] == '\0') {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char path[512]{};
    std::snprintf(path, sizeof(path), "%s/project.proj", dir);
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
        return false;
    }
    std::fputs("{\"version\":", out);
    std::fprintf(out, "%u", info.version == 0 ? kEngineVersion : info.version);
    std::fputs(",\"name\":", out);
    quote(out, info.name[0] != '\0' ? info.name : "Untitled");
    std::fputs(",\"description\":", out);
    quote(out, info.about);
    std::fputs("}\n", out);
    std::fclose(out);
    return true;
}

bool projectRead(const char* path, ProjectInfo& info) {
    info = {};
    info.version = kEngineVersion;
    JsonDocument doc{};
    const uint32_t root = jsonLoadFile(doc, path);
    if (root == kJsonNone) {
        jsonRelease(doc);
        return false;
    }
    info.version = static_cast<uint32_t>(numOr(doc, root, "version", static_cast<float>(kEngineVersion)));
    copyJson(doc, root, "name", info.name, sizeof(info.name));
    copyJson(doc, root, "description", info.about, sizeof(info.about));
    jsonRelease(doc);
    return info.name[0] != '\0' || info.version != 0;
}

bool sceneDocWrite(const char* path, const SceneObjectRec* recs, uint32_t count) {
    if (path == nullptr || (recs == nullptr && count != 0)) {
        return false;
    }
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
        return false;
    }
    std::fprintf(out, "{\"version\":%u,\"objects\":[", kEngineVersion);
    for (uint32_t i = 0; i < count; ++i) {
        const SceneObjectRec& r = recs[i];
        if (i != 0) {
            std::fputc(',', out);
        }
        std::fputs("{\"name\":", out);
        quote(out, r.name);
        std::fprintf(out, ",\"kind\":%u", r.kind);
        std::fprintf(out, ",\"px\":%.5g,\"py\":%.5g,\"pz\":%.5g", static_cast<double>(r.pos[0]), static_cast<double>(r.pos[1]), static_cast<double>(r.pos[2]));
        std::fprintf(out, ",\"ex\":%.5g,\"ey\":%.5g,\"ez\":%.5g", static_cast<double>(r.euler[0]), static_cast<double>(r.euler[1]), static_cast<double>(r.euler[2]));
        std::fprintf(out, ",\"sx\":%.5g,\"sy\":%.5g,\"sz\":%.5g", static_cast<double>(r.scale[0]), static_cast<double>(r.scale[1]), static_cast<double>(r.scale[2]));
        std::fprintf(out, ",\"r\":%.5g,\"g\":%.5g,\"b\":%.5g", static_cast<double>(r.base[0]), static_cast<double>(r.base[1]), static_cast<double>(r.base[2]));
        std::fprintf(out, ",\"rough\":%.5g,\"metal\":%.5g,\"emit\":%.5g", static_cast<double>(r.roughness), static_cast<double>(r.metallic), static_cast<double>(r.emissive));
        std::fputs(",\"mesh\":", out);
        quote(out, r.mesh);
        std::fputs(",\"mat\":", out);
        quote(out, r.material);
        std::fputc('}', out);
    }
    std::fputs("]}\n", out);
    std::fclose(out);
    return true;
}

bool sceneDocRead(const char* path, SceneObjectRec* recs, uint32_t cap, uint32_t& count) {
    count = 0;
    if (recs == nullptr || cap == 0) {
        return false;
    }
    JsonDocument doc{};
    const uint32_t root = jsonLoadFile(doc, path);
    if (root == kJsonNone) {
        jsonRelease(doc);
        return false;
    }
    const uint32_t list = jsonFind(doc, root, "objects");
    for (uint32_t i = 0; i < cap; ++i) {
        const uint32_t id = jsonAt(doc, list, i);
        if (id == kJsonNone) {
            break;
        }
        SceneObjectRec rec{};
        rec.scale[0] = rec.scale[1] = rec.scale[2] = 1.0f;
        rec.base[0] = rec.base[1] = rec.base[2] = 1.0f;
        copyJson(doc, id, "name", rec.name, sizeof(rec.name));
        copyJson(doc, id, "mesh", rec.mesh, sizeof(rec.mesh));
        copyJson(doc, id, "mat", rec.material, sizeof(rec.material));
        rec.kind = static_cast<uint8_t>(numOr(doc, id, "kind", 0.0f));
        rec.pos[0] = numOr(doc, id, "px", 0.0f);
        rec.pos[1] = numOr(doc, id, "py", 0.0f);
        rec.pos[2] = numOr(doc, id, "pz", 0.0f);
        rec.euler[0] = numOr(doc, id, "ex", 0.0f);
        rec.euler[1] = numOr(doc, id, "ey", 0.0f);
        rec.euler[2] = numOr(doc, id, "ez", 0.0f);
        rec.scale[0] = numOr(doc, id, "sx", 1.0f);
        rec.scale[1] = numOr(doc, id, "sy", 1.0f);
        rec.scale[2] = numOr(doc, id, "sz", 1.0f);
        rec.base[0] = numOr(doc, id, "r", 1.0f);
        rec.base[1] = numOr(doc, id, "g", 1.0f);
        rec.base[2] = numOr(doc, id, "b", 1.0f);
        rec.roughness = numOr(doc, id, "rough", 0.5f);
        rec.metallic = numOr(doc, id, "metal", 0.0f);
        rec.emissive = numOr(doc, id, "emit", 0.0f);
        recs[count] = rec;
        count += 1u;
    }
    jsonRelease(doc);
    return true;
}

namespace fs = std::filesystem;

namespace {

bool skipDir(const fs::path& dir) {
    const std::string name = dir.filename().string();
    return name == "cache" || name == "build" || name == "build-core" || name == ".git";
}

bool textureFile(const char* name) {
    return strIsImagePath(name) || strExtIs(name, ".dds");
}

// Returns false once `take` asks to stop. Files whose names `want` accepts are reported.
bool walkFiles(const fs::path& dir, int depth, bool (*want)(const char* name), ProjectMaterialFn take, void* user) {
    if (depth > 4 || skipDir(dir)) {
        return true;
    }
    std::error_code ec;
    for (const fs::directory_entry& ent : fs::directory_iterator(dir, ec)) {
        if (ec) {
            return true;
        }
        if (ent.is_directory(ec)) {
            if (!walkFiles(ent.path(), depth + 1, want, take, user)) {
                return false;
            }
            continue;
        }
        if (!ent.is_regular_file(ec)) {
            continue;
        }
        const std::string name = ent.path().filename().string();
        if (want(name.c_str()) && !take(user, name.c_str(), ent.path().string().c_str())) {
            return false;
        }
    }
    return true;
}

// Copies the map into the folder of the material so the project stays self-contained.
void parkMap(const char* matPath, char* slot) {
    if (slot[0] == '\0' || matPath[0] == '\0') {
        return;
    }
    std::error_code ec;
    const fs::path src(slot);
    if (!fs::is_regular_file(src, ec)) {
        return;
    }
    const fs::path dir = fs::path(matPath).parent_path();
    if (src.parent_path() == dir) {
        return;
    }
    fs::create_directories(dir, ec);
    const fs::path dst = dir / src.filename();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        std::snprintf(slot, 260, "%s", dst.string().c_str());
    }
}

// The ORMH pack belongs to the material when none is named or the named one is the material's own.
bool ormOwned(const BhmatDoc& doc, const char* matPath) {
    if (doc.ormPath[0] == '\0') {
        return true;
    }
    if (matPath[0] == '\0') {
        return false;
    }
    const std::string want = fs::path(matPath).stem().string() + "_ormh.png";
    return fs::path(doc.ormPath).filename().string() == want;
}

} // namespace

void projectListMaterials(const char* root, ProjectMaterialFn take, void* user) {
    if (root == nullptr || root[0] == '\0' || take == nullptr) {
        return;
    }
    (void)walkFiles(fs::path(root), 0, [](const char* name) { return strExtIs(name, ".bhmat"); }, take, user);
}

void projectListTextures(const char* root, ProjectMaterialFn take, void* user) {
    if (root == nullptr || root[0] == '\0' || take == nullptr) {
        return;
    }
    (void)walkFiles(fs::path(root), 0, textureFile, take, user);
}

bool materialCommitMaps(BhmatDoc& doc, const char* matPath, uint8_t editedChannel) {
    if (matPath == nullptr) {
        matPath = "";
    }
    char* slot[] = {doc.albedoPath, doc.normalPath, doc.ormPath, doc.emissivePath, doc.roughPath, doc.metalPath,
        doc.aoPath, doc.heightPath, doc.alphaPath};
    for (char* one : slot) {
        parkMap(matPath, one);
    }
    if (doc.mapPack == 0u || editedChannel == 2u || !ormOwned(doc, matPath)) {
        return false;
    }
    const bool maps = doc.roughPath[0] != '\0' || doc.metalPath[0] != '\0' || doc.aoPath[0] != '\0' || doc.heightPath[0] != '\0';
    if (!maps) {
        doc.ormPath[0] = '\0';
        return false;
    }
    if (matPath[0] == '\0') {
        return false;
    }
    const fs::path mat(matPath);
    const fs::path dst = mat.parent_path() / (mat.stem().string() + "_ormh.png");
    if (!imagePackOrmh(doc.aoPath, doc.roughPath, doc.metalPath, doc.heightPath, dst.string().c_str())) {
        return false;
    }
    std::snprintf(doc.ormPath, sizeof(doc.ormPath), "%s", dst.string().c_str());
    if (doc.heightPath[0] != '\0' && doc.heightScale < 0.001f) {
        doc.heightScale = 1.0f;
    }
    return true;
}

} // namespace burnhope
