#include "image/ImageDecode.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace burnhope {
namespace {

struct Rgba8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

uint32_t read16(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

Rgba8 expand565(uint32_t c) {
    const uint32_t r = (c >> 11) & 31u;
    const uint32_t g = (c >> 5) & 63u;
    const uint32_t b = c & 31u;
    return {static_cast<uint8_t>((r << 3) | (r >> 2)), static_cast<uint8_t>((g << 2) | (g >> 4)), static_cast<uint8_t>((b << 3) | (b >> 2)), 255};
}

uint8_t mix(uint32_t a, uint32_t b, uint32_t wa, uint32_t wb, uint32_t div) {
    return static_cast<uint8_t>((a * wa + b * wb + div / 2) / div);
}

// Eight-step (BC4 style) channel: 16 values from two endpoints and 3-bit selectors.
void decodeChannel(const uint8_t* block, uint8_t out[16]) {
    uint8_t pal[8];
    pal[0] = block[0];
    pal[1] = block[1];
    if (pal[0] > pal[1]) {
        for (uint32_t i = 1; i < 7; ++i) {
            pal[i + 1] = mix(pal[0], pal[1], 7 - i, i, 7);
        }
    } else {
        for (uint32_t i = 1; i < 5; ++i) {
            pal[i + 1] = mix(pal[0], pal[1], 5 - i, i, 5);
        }
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8u * i);
    }
    for (uint32_t i = 0; i < 16; ++i) {
        out[i] = pal[(bits >> (3u * i)) & 7u];
    }
}

void decodeBc1(const uint8_t* block, Rgba8 out[16]) {
    const uint32_t c0 = read16(block);
    const uint32_t c1 = read16(block + 2);
    Rgba8 pal[4];
    pal[0] = expand565(c0);
    pal[1] = expand565(c1);
    if (c0 > c1) {
        pal[2] = {mix(pal[0].r, pal[1].r, 2, 1, 3), mix(pal[0].g, pal[1].g, 2, 1, 3), mix(pal[0].b, pal[1].b, 2, 1, 3), 255};
        pal[3] = {mix(pal[0].r, pal[1].r, 1, 2, 3), mix(pal[0].g, pal[1].g, 1, 2, 3), mix(pal[0].b, pal[1].b, 1, 2, 3), 255};
    } else {
        pal[2] = {mix(pal[0].r, pal[1].r, 1, 1, 2), mix(pal[0].g, pal[1].g, 1, 1, 2), mix(pal[0].b, pal[1].b, 1, 1, 2), 255};
        pal[3] = {0, 0, 0, 0};
    }
    const uint32_t bits = read16(block + 4) | (read16(block + 6) << 16);
    for (uint32_t i = 0; i < 16; ++i) {
        out[i] = pal[(bits >> (2u * i)) & 3u];
    }
}

void decodeBc3(const uint8_t* block, Rgba8 out[16]) {
    uint8_t alpha[16];
    decodeChannel(block, alpha);
    decodeBc1(block + 8, out);
    for (uint32_t i = 0; i < 16; ++i) {
        out[i].a = alpha[i];
    }
}

void decodeBc5(const uint8_t* block, Rgba8 out[16]) {
    uint8_t x[16];
    uint8_t y[16];
    decodeChannel(block, x);
    decodeChannel(block + 8, y);
    for (uint32_t i = 0; i < 16; ++i) {
        const float nx = static_cast<float>(x[i]) / 127.5f - 1.0f;
        const float ny = static_cast<float>(y[i]) / 127.5f - 1.0f;
        const float nz = std::sqrt(std::fmax(0.0f, 1.0f - nx * nx - ny * ny));
        out[i] = {x[i], y[i], static_cast<uint8_t>((nz * 0.5f + 0.5f) * 255.0f + 0.5f), 255};
    }
}

struct BitReader {
    uint64_t lo = 0;
    uint64_t hi = 0;
    uint32_t at = 0;

    explicit BitReader(const uint8_t* p) {
        std::memcpy(&lo, p, 8);
        std::memcpy(&hi, p + 8, 8);
    }
    uint32_t take(uint32_t n) {
        uint32_t v = 0;
        for (uint32_t i = 0; i < n; ++i, ++at) {
            const uint64_t word = at < 64 ? lo : hi;
            v |= static_cast<uint32_t>((word >> (at & 63u)) & 1u) << i;
        }
        return v;
    }
};

constexpr uint8_t kWeight2[4] = {0, 21, 43, 64};
constexpr uint8_t kWeight3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr uint8_t kWeight4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

uint8_t lerpWeight(uint32_t a, uint32_t b, uint32_t w) {
    return static_cast<uint8_t>((a * (64u - w) + b * w + 32u) >> 6);
}

uint8_t weightOf(uint32_t bits, uint32_t index) {
    return bits == 2 ? kWeight2[index] : (bits == 3 ? kWeight3[index] : kWeight4[index]);
}

uint8_t widen(uint32_t v, uint32_t bits) {
    return static_cast<uint8_t>((v << (8u - bits)) | (v >> (2u * bits - 8u)));
}

// Modes 4, 5 and 6: one subset, so the whole block decodes exactly.
void decodeBc7Single(const uint8_t* block, uint32_t mode, Rgba8 out[16]) {
    BitReader in(block);
    in.at = mode + 1;
    uint32_t rotation = 0;
    uint32_t idxMode = 0;
    uint32_t ep[2][4]{};
    uint32_t colorBits = 2;
    uint32_t alphaBits = 2;
    if (mode == 6) {
        uint32_t raw[8];
        for (uint32_t& v : raw) {
            v = in.take(7);
        }
        const uint32_t p0 = in.take(1);
        const uint32_t p1 = in.take(1);
        for (uint32_t c = 0; c < 4; ++c) {
            ep[0][c] = (raw[c * 2] << 1) | p0;
            ep[1][c] = (raw[c * 2 + 1] << 1) | p1;
        }
        colorBits = alphaBits = 4;
    } else {
        rotation = in.take(2);
        if (mode == 4) {
            idxMode = in.take(1);
        }
        const uint32_t bits = mode == 4 ? 5u : 7u;
        uint32_t raw[6];
        for (uint32_t& v : raw) {
            v = in.take(bits);
        }
        const uint32_t aBits = mode == 4 ? 6u : 8u;
        const uint32_t a0 = in.take(aBits);
        const uint32_t a1 = in.take(aBits);
        for (uint32_t c = 0; c < 3; ++c) {
            ep[0][c] = widen(raw[c * 2], bits);
            ep[1][c] = widen(raw[c * 2 + 1], bits);
        }
        ep[0][3] = aBits == 8 ? a0 : widen(a0, aBits);
        ep[1][3] = aBits == 8 ? a1 : widen(a1, aBits);
        colorBits = (mode == 4 && idxMode != 0) ? 3 : 2;
        alphaBits = (mode == 4 && idxMode == 0) ? 3 : 2;
    }
    // The stream holds the 2-bit selector set first and, in mode 4, the 3-bit set second. idxMode picks which one shades colour.
    const uint32_t firstBits = mode == 6 ? 4u : 2u;
    const uint32_t secondBits = mode == 4 ? 3u : 2u;
    uint32_t first[16];
    uint32_t second[16];
    for (uint32_t i = 0; i < 16; ++i) {
        first[i] = in.take(i == 0 ? firstBits - 1 : firstBits);
    }
    if (mode == 6) {
        std::memcpy(second, first, sizeof(second));
    } else {
        for (uint32_t i = 0; i < 16; ++i) {
            second[i] = in.take(i == 0 ? secondBits - 1 : secondBits);
        }
    }
    const uint32_t* colorIdx = idxMode != 0 ? second : first;
    const uint32_t* alphaIdx = idxMode != 0 ? first : second;
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t wc = weightOf(colorBits, colorIdx[i]);
        const uint32_t wa = weightOf(alphaBits, alphaIdx[i]);
        uint8_t px[4];
        for (uint32_t c = 0; c < 3; ++c) {
            px[c] = lerpWeight(ep[0][c], ep[1][c], wc);
        }
        px[3] = lerpWeight(ep[0][3], ep[1][3], wa);
        if (rotation != 0) {
            const uint8_t t = px[3];
            px[3] = px[rotation - 1];
            px[rotation - 1] = t;
        }
        out[i] = {px[0], px[1], px[2], px[3]};
    }
}

// Modes 0-3 and 7 split the block into 2-3 subsets whose layout needs the BC7 partition tables, which this
// decoder does not carry. Thumbnails shrink the image by many blocks, so the block's mean endpoint colour stands in.
void decodeBc7Flat(const uint8_t* block, uint32_t mode, Rgba8 out[16]) {
    struct Layout {
        uint8_t subsets;
        uint8_t partBits;
        uint8_t colorBits;
        uint8_t alphaBits;
        uint8_t pbits;
    };
    static constexpr Layout kLayout[8] = {
        {3, 4, 4, 0, 6}, {2, 6, 6, 0, 2}, {3, 6, 5, 0, 0}, {2, 6, 7, 0, 4},
        {}, {}, {}, {2, 6, 5, 5, 4},
    };
    const Layout& lay = kLayout[mode];
    BitReader in(block);
    in.at = mode + 1 + lay.partBits;
    const uint32_t ends = lay.subsets * 2u;
    const uint32_t channels = lay.alphaBits != 0 ? 4u : 3u;
    uint32_t raw[6][4]{};
    for (uint32_t c = 0; c < channels; ++c) {
        const uint32_t bits = c < 3 ? lay.colorBits : lay.alphaBits;
        for (uint32_t e = 0; e < ends; ++e) {
            raw[e][c] = in.take(bits);
        }
    }
    uint32_t pbit[6]{};
    for (uint32_t e = 0; e < lay.pbits; ++e) {
        pbit[e] = in.take(1);
    }
    uint32_t sum[4]{};
    for (uint32_t e = 0; e < ends; ++e) {
        for (uint32_t c = 0; c < channels; ++c) {
            const uint32_t bits = c < 3 ? lay.colorBits : lay.alphaBits;
            uint32_t v = raw[e][c];
            uint32_t width = bits;
            if (lay.pbits != 0) {
                v = (v << 1) | pbit[lay.pbits == ends ? e : e / 2];
                width += 1;
            }
            sum[c] += width >= 8 ? v : ((v << (8u - width)) | (v >> (2u * width - 8u)));
        }
    }
    Rgba8 flat{static_cast<uint8_t>(sum[0] / ends), static_cast<uint8_t>(sum[1] / ends), static_cast<uint8_t>(sum[2] / ends),
        channels == 4 ? static_cast<uint8_t>(sum[3] / ends) : uint8_t{255}};
    for (uint32_t i = 0; i < 16; ++i) {
        out[i] = flat;
    }
}

void decodeBc7(const uint8_t* block, Rgba8 out[16]) {
    if (block[0] == 0) {
        for (uint32_t i = 0; i < 16; ++i) {
            out[i] = {0, 0, 0, 0};
        }
        return;
    }
    uint32_t mode = 0;
    while (((block[0] >> mode) & 1u) == 0) {
        ++mode;
    }
    if (mode >= 4 && mode <= 6) {
        decodeBc7Single(block, mode, out);
    } else {
        decodeBc7Flat(block, mode, out);
    }
}

} // namespace

bool imageDecodeDdsLevel(const DdsView& view, uint32_t level, uint8_t* rgba) {
    if (rgba == nullptr || view.base == nullptr || level >= view.levelCount) {
        return false;
    }
    const ImageMip& mip = view.levels[level];
    const uint32_t blockBytes = view.block == ImageBlock::Bc1 ? 8u : 16u;
    const uint32_t bw = (mip.width + 3u) / 4u;
    const uint32_t bh = (mip.height + 3u) / 4u;
    if (mip.bytes < static_cast<uint64_t>(bw) * bh * blockBytes) {
        return false;
    }
    const uint8_t* src = view.base + mip.offset;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* block = src + (static_cast<uint64_t>(by) * bw + bx) * blockBytes;
            Rgba8 px[16];
            switch (view.block) {
            case ImageBlock::Bc1:
                decodeBc1(block, px);
                break;
            case ImageBlock::Bc3:
                decodeBc3(block, px);
                break;
            case ImageBlock::Bc5:
                decodeBc5(block, px);
                break;
            case ImageBlock::Bc7:
                decodeBc7(block, px);
                break;
            default:
                return false;
            }
            for (uint32_t y = 0; y < 4 && by * 4 + y < mip.height; ++y) {
                for (uint32_t x = 0; x < 4 && bx * 4 + x < mip.width; ++x) {
                    const Rgba8& c = px[y * 4 + x];
                    uint8_t* dst = rgba + (static_cast<uint64_t>(by * 4 + y) * mip.width + bx * 4 + x) * 4u;
                    dst[0] = c.r;
                    dst[1] = c.g;
                    dst[2] = c.b;
                    dst[3] = c.a;
                }
            }
        }
    }
    return true;
}

} // namespace burnhope
