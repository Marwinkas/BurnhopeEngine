#include "asset/MaterialBake.hpp"

#include "image/ImageDecode.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>

namespace burnhope {

namespace fs = std::filesystem;

namespace {

void fillCell(uint8_t* atlas, uint8_t cell, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t col = cell % 4u;
    const uint32_t row = cell / 4u;
    for (uint32_t y = 0; y < kMatCell; ++y) {
        uint8_t* dst = atlas + ((row * kMatCell + y) * kMatAtlasW + col * kMatCell) * 4u;
        for (uint32_t x = 0; x < kMatCell; ++x) {
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
    for (uint32_t y = 0; y < kMatCell; ++y) {
        const uint32_t sy = y * sh / kMatCell;
        uint8_t* dst = atlas + ((row * kMatCell + y) * kMatAtlasW + col * kMatCell) * 4u;
        for (uint32_t x = 0; x < kMatCell; ++x) {
            const uint32_t sx = x * sw / kMatCell;
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

} // namespace

void materialBakeAtlas(const BhmatDoc& doc, const char* matPath, uint8_t* atlas) {
    const char* mat = matPath != nullptr ? matPath : "";
    paintCell(atlas, 0, doc.albedoPath, mat, 255, 255, 255);
    paintCell(atlas, 1, doc.metalPath, mat, 0, 0, 0);
    paintCell(atlas, 2, doc.roughPath, mat, 128, 128, 128);
    paintCell(atlas, 3, doc.normalPath, mat, 128, 128, 255);
    paintCell(atlas, 4, doc.heightPath, mat, 200, 200, 200);
    paintCell(atlas, 5, doc.aoPath, mat, 255, 255, 255);
    paintCell(atlas, 6, doc.ormPath, mat, 40, 40, 44);
    paintCell(atlas, 7, doc.emissivePath, mat, byteOf(doc.emissiveColor[0]), byteOf(doc.emissiveColor[1]), byteOf(doc.emissiveColor[2]));
    paintCell(atlas, 8, doc.alphaPath, mat, byteOf(doc.baseA), byteOf(doc.baseA), byteOf(doc.baseA));
}

void materialBakeSphere(const BhmatDoc& doc, const uint8_t* atlas, float yaw, float pitch, uint8_t* dst) {
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float br = doc.base[0];
    const float bg = doc.base[1];
    const float bb = doc.base[2];
    constexpr float cx = 84.0f;
    constexpr float cy0 = 84.0f;
    constexpr float radius = 70.0f;
    for (uint32_t y = 0; y < kMatSphereSide; ++y) {
        for (uint32_t x = 0; x < kMatSphereSide; ++x) {
            uint8_t* px = dst + (static_cast<size_t>(y) * kMatSphereSide + x) * 4u;
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
            u = u < 0.0f ? 0.0f : (u > 0.999f ? 0.999f : u);
            v = v < 0.0f ? 0.0f : (v > 0.999f ? 0.999f : v);
            const uint32_t sx = static_cast<uint32_t>(u * static_cast<float>(kMatCell));
            const uint32_t sv = static_cast<uint32_t>(v * static_cast<float>(kMatCell));
            const uint8_t* sample = atlas + (static_cast<size_t>(sv) * kMatAtlasW + sx) * 4u;
            float ndl = nx * 0.32f + ny * 0.82f + nz * 0.48f;
            ndl = ndl < 0.0f ? 0.0f : ndl;
            const float shade = 0.22f + 0.78f * ndl;
            px[0] = byteOf(sample[0] / 255.0f * br * shade);
            px[1] = byteOf(sample[1] / 255.0f * bg * shade);
            px[2] = byteOf(sample[2] / 255.0f * bb * shade);
            px[3] = 255;
        }
    }
}

} // namespace burnhope
