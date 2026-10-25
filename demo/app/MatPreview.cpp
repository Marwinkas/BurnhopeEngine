#include "app/HudBuild.hpp"

#include "image/ImageDecode.hpp"
#include "ui/Canvas.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kPreview = 168;
constexpr uint32_t kCell = 64;
constexpr uint32_t kAtlasW = 256;
constexpr uint32_t kAtlasH = 128;

void park(const char* matPath, char* slot) {
    if (slot == nullptr || slot[0] == '\0' || matPath == nullptr || matPath[0] == '\0') {
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

bool ormOwned(const HudContext& hud) {
    if (hud.matDoc.ormPath[0] == '\0') {
        return true;
    }
    if (hud.matPath[0] == '\0') {
        return false;
    }
    const fs::path mat(hud.matPath);
    const std::string want = mat.stem().string() + "_ormh.png";
    return fs::path(hud.matDoc.ormPath).filename().string() == want;
}

void fillCell(uint8_t* atlas, uint8_t cell, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t col = cell % 4u;
    const uint32_t row = cell / 4u;
    for (uint32_t y = 0; y < kCell; ++y) {
        uint8_t* dst = atlas + ((row * kCell + y) * kAtlasW + col * kCell) * 4u;
        for (uint32_t x = 0; x < kCell; ++x) {
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 255;
            dst += 4;
        }
    }
}

void blitCell(uint8_t* atlas, uint8_t cell, const uint8_t* src, uint32_t sw, uint32_t sh) {
    if (src == nullptr || sw == 0 || sh == 0) {
        return;
    }
    const uint32_t col = cell % 4u;
    const uint32_t row = cell / 4u;
    for (uint32_t y = 0; y < kCell; ++y) {
        const uint32_t sy = y * sh / kCell;
        uint8_t* dst = atlas + ((row * kCell + y) * kAtlasW + col * kCell) * 4u;
        for (uint32_t x = 0; x < kCell; ++x) {
            const uint32_t sx = x * sw / kCell;
            const uint8_t* p = src + (static_cast<size_t>(sy) * sw + sx) * 4u;
            dst[0] = p[0];
            dst[1] = p[1];
            dst[2] = p[2];
            dst[3] = 255;
            dst += 4;
        }
    }
}

bool loadMap(const char* path, const char* matPath, ImageCpu& cpu) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (imageLoadFile(cpu, path) && cpu.rgba != nullptr) {
        return true;
    }
    imageCpuFree(cpu);
    if (matPath == nullptr || matPath[0] == '\0') {
        return false;
    }
    const fs::path full = fs::path(matPath).parent_path() / path;
    return imageLoadFile(cpu, full.string().c_str()) && cpu.rgba != nullptr;
}

void paintCell(uint8_t* atlas, uint8_t cell, const char* path, const char* matPath, uint8_t r, uint8_t g, uint8_t b) {
    ImageCpu cpu{};
    if (loadMap(path, matPath, cpu)) {
        blitCell(atlas, cell, cpu.rgba, cpu.width, cpu.height);
    } else {
        fillCell(atlas, cell, r, g, b);
    }
    imageCpuFree(cpu);
}

uint8_t byteOf(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return static_cast<uint8_t>(v * 255.0f);
}

void buildAtlas(HudContext& hud) {
    uint8_t* atlas = hud.atlasBuf.data();
    const BhmatDoc& doc = hud.matDoc;
    const char* mat = hud.matPath;
    paintCell(atlas, 0, doc.albedoPath, mat, 255, 255, 255);
    paintCell(atlas, 1, doc.metalPath, mat, 0, 0, 0);
    paintCell(atlas, 2, doc.roughPath, mat, 128, 128, 128);
    paintCell(atlas, 3, doc.normalPath, mat, 128, 128, 255);
    paintCell(atlas, 4, doc.heightPath, mat, 200, 200, 200);
    paintCell(atlas, 5, doc.aoPath, mat, 255, 255, 255);
    paintCell(atlas, 6, doc.ormPath, mat, 40, 40, 44);
    paintCell(atlas, 7, doc.emissivePath, mat, byteOf(doc.emissiveColor[0]), byteOf(doc.emissiveColor[1]), byteOf(doc.emissiveColor[2]));
}

void buildSphere(HudContext& hud) {
    const uint8_t* atlas = hud.atlasBuf.data();
    uint8_t* dst = hud.previewBuf.data();
    const float yaw = hud.previewYaw;
    const float pitch = hud.previewPitch;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float br = hud.matDoc.base[0];
    const float bg = hud.matDoc.base[1];
    const float bb = hud.matDoc.base[2];
    constexpr float cx = 84.0f;
    constexpr float cy0 = 84.0f;
    constexpr float radius = 70.0f;
    for (uint32_t y = 0; y < kPreview; ++y) {
        for (uint32_t x = 0; x < kPreview; ++x) {
            uint8_t* px = dst + (static_cast<size_t>(y) * kPreview + x) * 4u;
            const float nx = (static_cast<float>(x) + 0.5f - cx) / radius;
            const float ny = (cy0 - (static_cast<float>(y) + 0.5f)) / radius;
            const float d2 = nx * nx + ny * ny;
            if (d2 > 1.0f) {
                px[0] = 46;
                px[1] = 48;
                px[2] = 52;
                px[3] = 255;
                continue;
            }
            const float nz = std::sqrt(1.0f - d2);
            const float y1 = ny * cp - nz * sp;
            const float z1 = ny * sp + nz * cp;
            const float x2 = nx * cy + z1 * sy;
            const float z2 = -nx * sy + z1 * cy;
            float u = std::atan2(x2, z2) * 0.15915494f + 0.5f;
            float v = std::acos(y1 < -1.0f ? -1.0f : (y1 > 1.0f ? 1.0f : y1)) * 0.31830988f;
            if (u < 0.0f) {
                u = 0.0f;
            }
            if (u > 0.999f) {
                u = 0.999f;
            }
            if (v < 0.0f) {
                v = 0.0f;
            }
            if (v > 0.999f) {
                v = 0.999f;
            }
            const uint32_t sx = static_cast<uint32_t>(u * static_cast<float>(kCell));
            const uint32_t sy = static_cast<uint32_t>(v * static_cast<float>(kCell));
            const uint8_t* sample = atlas + (static_cast<size_t>(sy) * kAtlasW + sx) * 4u;
            float ndl = nx * 0.32f + ny * 0.82f + nz * 0.48f;
            if (ndl < 0.0f) {
                ndl = 0.0f;
            }
            const float shade = 0.22f + 0.78f * ndl;
            px[0] = byteOf(sample[0] / 255.0f * br * shade);
            px[1] = byteOf(sample[1] / 255.0f * bg * shade);
            px[2] = byteOf(sample[2] / 255.0f * bb * shade);
            px[3] = 255;
        }
    }
}

Canvas* previewCanvas(HudContext& hud) {
    if (hud.tool.canvas != nullptr && hud.tool.canvas->device != nullptr) {
        return hud.tool.canvas;
    }
    if (hud.host != nullptr && hud.slot >= 0) {
        return &hud.host->view[hud.slot].canvas;
    }
    return nullptr;
}

} // namespace

void hudCommitMaps(HudContext& hud, uint8_t channel) {
    char* slot[] = {
        hud.matDoc.albedoPath, hud.matDoc.normalPath, hud.matDoc.ormPath, hud.matDoc.emissivePath,
        hud.matDoc.roughPath, hud.matDoc.metalPath, hud.matDoc.aoPath, hud.matDoc.heightPath};
    for (char* one : slot) {
        park(hud.matPath, one);
    }
    if (channel == 2u || !ormOwned(hud)) {
        return;
    }
    const bool maps = hud.matDoc.roughPath[0] != '\0' || hud.matDoc.metalPath[0] != '\0' || hud.matDoc.aoPath[0] != '\0'
        || hud.matDoc.heightPath[0] != '\0';
    if (!maps) {
        hud.matDoc.ormPath[0] = '\0';
        return;
    }
    if (hud.matPath[0] == '\0') {
        return;
    }
    const fs::path mat(hud.matPath);
    const fs::path dst = mat.parent_path() / (mat.stem().string() + "_ormh.png");
    if (!imagePackOrmh(hud.matDoc.aoPath, hud.matDoc.roughPath, hud.matDoc.metalPath, hud.matDoc.heightPath, dst.string().c_str())) {
        return;
    }
    std::snprintf(hud.matDoc.ormPath, sizeof(hud.matDoc.ormPath), "%s", dst.string().c_str());
    if (hud.matDoc.heightPath[0] != '\0' && hud.matDoc.heightScale < 0.001f) {
        hud.matDoc.heightScale = 1.0f;
        if (UiState* state = hudUi(hud)) {
            hud.bind.write(*state, kScMatHeight, hud.matDoc.heightScale);
        }
    }
}

void hudBakeMaterialView(HudContext& hud) {
    UiState* state = hud.tool.canvas != nullptr ? hud.tool.canvas->state.get() : nullptr;
    if (state != nullptr && state->capture != hud.matPreview) {
        hud.previewDown = 0;
    }
    if (hud.previewTint[0] != hud.matDoc.base[0] || hud.previewTint[1] != hud.matDoc.base[1] || hud.previewTint[2] != hud.matDoc.base[2]) {
        hud.previewTint[0] = hud.matDoc.base[0];
        hud.previewTint[1] = hud.matDoc.base[1];
        hud.previewTint[2] = hud.matDoc.base[2];
        hud.previewDirty = 1;
    }
    if (hud.atlasDirty == 0 && hud.previewDirty == 0) {
        return;
    }
    if (hud.atlasBuf.size() != kAtlasW * kAtlasH * 4u) {
        hud.atlasBuf.resize(kAtlasW * kAtlasH * 4u);
    }
    if (hud.previewBuf.size() != kPreview * kPreview * 4u) {
        hud.previewBuf.resize(kPreview * kPreview * 4u);
    }
    Canvas* canvas = previewCanvas(hud);
    if (canvas == nullptr || canvas->device == nullptr) {
        return;
    }
    if (hud.atlasDirty != 0) {
        buildAtlas(hud);
        if (canvasUploadPhoto(*canvas, 9, hud.atlasBuf.data(), kAtlasW, kAtlasH)) {
            hud.atlasDirty = 0;
            hud.previewDirty = 1;
        }
    }
    if (hud.previewDirty != 0) {
        buildSphere(hud);
        if (canvasUploadPhoto(*canvas, 8, hud.previewBuf.data(), kPreview, kPreview)) {
            hud.previewDirty = 0;
        }
    }
}

} // namespace burnhope
