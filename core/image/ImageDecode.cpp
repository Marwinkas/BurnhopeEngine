#include "image/ImageDecode.hpp"

#include "io/File.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#undef STBI_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace burnhope {
namespace {

constexpr uint32_t kMaxEdge = 4096;

bool isGif(const uint8_t* data, size_t n) {
    return n >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F';
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

void imageCpuFree(ImageCpu& img) {
    std::free(img.rgba);
    std::free(img.rgb);
    img.rgba = nullptr;
    img.rgb = nullptr;
    img.width = 0;
    img.height = 0;
    img.frames = 0;
    img.hdr = 0;
}

bool imageLoadFile(ImageCpu& img, const char* path) {
    imageCpuFree(img);
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr || file.size == 0 || file.size > 64u * 1024u * 1024u) {
        fileUnmap(&file);
        spdlog::error("image read failed: {}", path);
        return false;
    }
    const int fileBytes = static_cast<int>(file.size);
    int w = 0;
    int h = 0;
    int comp = 0;
    int frames = 1;
    int* delays = nullptr;
    stbi_uc* pixels = nullptr;
    if (stbi_is_hdr_from_memory(file.data, fileBytes) != 0) {
        float* hdr = stbi_loadf_from_memory(file.data, fileBytes, &w, &h, &comp, 3);
        fileUnmap(&file);
        if (hdr == nullptr || w <= 0 || h <= 0) {
            spdlog::error("hdr decode failed: {} ({})", path, stbi_failure_reason());
            std::free(hdr);
            return false;
        }
        if (static_cast<uint32_t>(w) > kMaxEdge || static_cast<uint32_t>(h) > kMaxEdge) {
            spdlog::error("image too large: {}x{}", w, h);
            std::free(hdr);
            return false;
        }
        img.rgb = hdr;
        img.width = static_cast<uint32_t>(w);
        img.height = static_cast<uint32_t>(h);
        img.frames = 1;
        img.hdr = 1;
        img.delayMs[0] = 100;
        return true;
    }
    if (isGif(file.data, file.size)) {
        int z = 0;
        pixels = stbi_load_gif_from_memory(file.data, fileBytes, &delays, &w, &h, &z, &comp, 4);
        frames = z;
    } else {
        pixels = stbi_load_from_memory(file.data, fileBytes, &w, &h, &comp, 4);
    }
    fileUnmap(&file);
    if (pixels == nullptr || w <= 0 || h <= 0 || frames <= 0) {
        spdlog::error("image decode failed: {} ({})", path, stbi_failure_reason());
        std::free(delays);
        std::free(pixels);
        return false;
    }
    if (static_cast<uint32_t>(w) > kMaxEdge || static_cast<uint32_t>(h) > kMaxEdge) {
        spdlog::error("image too large: {}x{}", w, h);
        std::free(delays);
        std::free(pixels);
        return false;
    }
    if (frames > static_cast<int>(kImageFrames)) {
        frames = static_cast<int>(kImageFrames);
    }
    const uint64_t bytes = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * static_cast<uint64_t>(frames) * 4u;
    if (bytes > static_cast<uint64_t>(kMaxEdge) * kMaxEdge * 4u) {
        spdlog::error("image too large after frames");
        std::free(delays);
        std::free(pixels);
        return false;
    }
    img.rgba = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(bytes)));
    if (img.rgba == nullptr) {
        std::free(delays);
        std::free(pixels);
        return false;
    }
    std::memcpy(img.rgba, pixels, static_cast<size_t>(bytes));
    std::free(pixels);
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.frames = static_cast<uint32_t>(frames);
    for (int i = 0; i < frames; ++i) {
        int ms = delays != nullptr ? delays[i] : 100;
        if (ms < 20) {
            ms = 100;
        }
        if (ms > 10000) {
            ms = 10000;
        }
        img.delayMs[i] = static_cast<uint16_t>(ms);
    }
    std::free(delays);
    return true;
}

bool imageParseDds(const uint8_t* data, size_t size, DdsView& out) {
    out = {};
    if (data == nullptr || size < 128 || std::memcmp(data, "DDS ", 4) != 0) {
        return false;
    }
    const uint32_t height = readU32(data + 12);
    const uint32_t width = readU32(data + 16);
    uint32_t mips = readU32(data + 28);
    const uint8_t* four = data + 84;
    uint32_t blockBytes = 0;
    if (std::memcmp(four, "DXT1", 4) == 0) {
        out.block = ImageBlock::Bc1;
        blockBytes = 8;
    } else if (std::memcmp(four, "DXT5", 4) == 0) {
        out.block = ImageBlock::Bc3;
        blockBytes = 16;
    } else if (std::memcmp(four, "ATI2", 4) == 0) {
        out.block = ImageBlock::Bc5;
        blockBytes = 16;
    } else if (std::memcmp(four, "BC7 ", 4) == 0) {
        out.block = ImageBlock::Bc7;
        blockBytes = 16;
    } else {
        return false;
    }
    if (width == 0 || height == 0) {
        return false;
    }
    if (mips < 1) {
        mips = 1;
    }
    if (mips > 16) {
        mips = 16;
    }
    uint64_t cursor = 128;
    uint32_t w = width;
    uint32_t ht = height;
    uint32_t count = 0;
    for (uint32_t i = 0; i < mips; ++i) {
        const uint32_t bx = (w + 3u) / 4u;
        const uint32_t by = (ht + 3u) / 4u;
        const uint64_t bytes = static_cast<uint64_t>(bx) * by * blockBytes;
        if (cursor + bytes > size) {
            break;
        }
        out.levels[count].width = w;
        out.levels[count].height = ht;
        out.levels[count].offset = cursor;
        out.levels[count].bytes = bytes;
        cursor += bytes;
        count += 1;
        w = w > 1 ? w / 2 : 1;
        ht = ht > 1 ? ht / 2 : 1;
    }
    if (count == 0) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.levelCount = count;
    out.base = data;
    return true;
}

void writeBlockDds(
    const char* path,
    uint32_t width,
    uint32_t height,
    uint32_t mips,
    ImageBlock format,
    const uint8_t* data,
    size_t bytes) {
    if (path == nullptr || path[0] == '\0' || data == nullptr || bytes == 0) {
        return;
    }
    const char* four = nullptr;
    if (format == ImageBlock::Bc1) {
        four = "DXT1";
    } else if (format == ImageBlock::Bc3) {
        four = "DXT5";
    } else if (format == ImageBlock::Bc5) {
        four = "ATI2";
    } else if (format == ImageBlock::Bc7) {
        four = "BC7 ";
    }
    if (four == nullptr) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories("cache/textures", ec);
    if (ec) {
        return;
    }
    uint8_t hdr[128]{};
    std::memcpy(hdr, "DDS ", 4);
    const uint32_t size = 124;
    const uint32_t flags = 0x000A1007u;
    const uint32_t pfSize = 32;
    const uint32_t pfFlags = 4;
    const uint32_t caps = mips > 1 ? 0x00401008u : 0x00001000u;
    std::memcpy(hdr + 4, &size, 4);
    std::memcpy(hdr + 8, &flags, 4);
    std::memcpy(hdr + 12, &height, 4);
    std::memcpy(hdr + 16, &width, 4);
    std::memcpy(hdr + 28, &mips, 4);
    std::memcpy(hdr + 76, &pfSize, 4);
    std::memcpy(hdr + 80, &pfFlags, 4);
    std::memcpy(hdr + 84, four, 4);
    std::memcpy(hdr + 108, &caps, 4);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(hdr, 1, 128, file);
    std::fwrite(data, 1, bytes, file);
    std::fclose(file);
}

bool imagePackOrm(const char* aoPath, const char* roughPath, const char* metalPath, const char* outPath) {
    if (aoPath == nullptr || roughPath == nullptr || metalPath == nullptr || outPath == nullptr) {
        return false;
    }
    ImageCpu ao{};
    ImageCpu rough{};
    ImageCpu metal{};
    const bool ok = imageLoadFile(ao, aoPath) && imageLoadFile(rough, roughPath) && imageLoadFile(metal, metalPath)
        && ao.rgba != nullptr && rough.rgba != nullptr && metal.rgba != nullptr && ao.width > 0 && ao.height > 0;
    if (!ok) {
        imageCpuFree(ao);
        imageCpuFree(rough);
        imageCpuFree(metal);
        return false;
    }
    const uint32_t w = ao.width < rough.width ? ao.width : rough.width;
    const uint32_t w2 = w < metal.width ? w : metal.width;
    const uint32_t h = ao.height < rough.height ? ao.height : rough.height;
    const uint32_t h2 = h < metal.height ? h : metal.height;
    uint8_t* packed = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w2) * h2 * 4u));
    if (packed == nullptr) {
        imageCpuFree(ao);
        imageCpuFree(rough);
        imageCpuFree(metal);
        return false;
    }
    for (uint32_t y = 0; y < h2; ++y) {
        for (uint32_t x = 0; x < w2; ++x) {
            const uint32_t i = (y * w2 + x) * 4u;
            packed[i] = ao.rgba[(y * ao.width + x) * 4u];
            packed[i + 1u] = rough.rgba[(y * rough.width + x) * 4u + 1u];
            packed[i + 2u] = metal.rgba[(y * metal.width + x) * 4u + 2u];
            packed[i + 3u] = 255;
        }
    }
    const int wrote = stbi_write_png(outPath, static_cast<int>(w2), static_cast<int>(h2), 4, packed, static_cast<int>(w2 * 4u));
    std::free(packed);
    imageCpuFree(ao);
    imageCpuFree(rough);
    imageCpuFree(metal);
    return wrote != 0;
}

namespace {

uint8_t mapPeak(const ImageCpu* img, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t fallback) {
    if (img == nullptr || img->rgba == nullptr || img->width == 0 || img->height == 0 || w == 0 || h == 0) {
        return fallback;
    }
    const uint32_t sx = x * img->width / w;
    const uint32_t sy = y * img->height / h;
    const uint32_t cx = sx < img->width ? sx : img->width - 1u;
    const uint32_t cy = sy < img->height ? sy : img->height - 1u;
    const uint8_t* p = img->rgba + (static_cast<size_t>(cy) * img->width + cx) * 4u;
    uint8_t peak = p[0];
    if (p[1] > peak) {
        peak = p[1];
    }
    if (p[2] > peak) {
        peak = p[2];
    }
    return peak;
}

void mapSize(const ImageCpu& img, uint32_t& w, uint32_t& h) {
    if (img.rgba == nullptr || img.width == 0 || img.height == 0) {
        return;
    }
    if (w == 0 || img.width < w) {
        w = img.width;
    }
    if (h == 0 || img.height < h) {
        h = img.height;
    }
}

} // namespace

bool imagePackOrmh(
    const char* aoPath,
    const char* roughPath,
    const char* metalPath,
    const char* heightPath,
    const char* outPath) {
    if (outPath == nullptr || outPath[0] == '\0') {
        return false;
    }
    const char* paths[4] = {aoPath, roughPath, metalPath, heightPath};
    ImageCpu img[4]{};
    uint32_t live = 0;
    for (uint32_t i = 0; i < 4u; ++i) {
        if (paths[i] != nullptr && paths[i][0] != '\0' && imageLoadFile(img[i], paths[i]) && img[i].rgba != nullptr) {
            live += 1u;
        }
    }
    if (live == 0u) {
        for (ImageCpu& one : img) {
            imageCpuFree(one);
        }
        return false;
    }
    uint32_t w = 0;
    uint32_t h = 0;
    for (const ImageCpu& one : img) {
        if (w == 0 && one.rgba != nullptr) {
            w = one.width;
            h = one.height;
        }
        mapSize(one, w, h);
    }
    if (w == 0 || h == 0 || w > 4096u || h > 4096u) {
        for (ImageCpu& one : img) {
            imageCpuFree(one);
        }
        return false;
    }
    uint8_t* packed = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * h * 4u));
    if (packed == nullptr) {
        for (ImageCpu& one : img) {
            imageCpuFree(one);
        }
        return false;
    }
    const uint8_t fallback[4] = {255, 128, 0, 255};
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* dst = packed + (static_cast<size_t>(y) * w + x) * 4u;
            for (uint32_t c = 0; c < 4u; ++c) {
                dst[c] = mapPeak(img[c].rgba != nullptr ? &img[c] : nullptr, x, y, w, h, fallback[c]);
            }
        }
    }
    const int wrote = stbi_write_png(outPath, static_cast<int>(w), static_cast<int>(h), 4, packed, static_cast<int>(w * 4u));
    std::free(packed);
    for (ImageCpu& one : img) {
        imageCpuFree(one);
    }
    return wrote != 0;
}

} // namespace burnhope
