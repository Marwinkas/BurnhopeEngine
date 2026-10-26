#include "asset/MaterialBake.hpp"

#include "image/ImageDecode.hpp"

#include <algorithm>
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

namespace {

struct V3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 operator*(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) {
    const float len = std::sqrt(dot(a, a));
    return len > 1.0e-8f ? a * (1.0f / len) : V3{0.0f, 0.0f, 1.0f};
}
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

// Object <-> view rotation: object space is turned by yaw about Y, then pitch about X.
struct Spin {
    float cy;
    float sy;
    float cp;
    float sp;
    V3 toView(V3 v) const {
        const V3 a{v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy};
        return {a.x, a.y * cp - a.z * sp, a.y * sp + a.z * cp};
    }
    V3 toObject(V3 v) const {
        const V3 a{v.x, v.y * cp + v.z * sp, -v.y * sp + v.z * cp};
        return {a.x * cy - a.z * sy, a.y, a.x * sy + a.z * cy};
    }
};

struct Hit {
    bool hit = false;
    V3 n;
    V3 pos;
    float u = 0.0f;
    float v = 0.0f;
};

constexpr float kBoxHalf = 0.60f;
constexpr float kBoxRound = 0.14f;

float boxSdf(V3 p) {
    const V3 q{std::fabs(p.x) - kBoxHalf, std::fabs(p.y) - kBoxHalf, std::fabs(p.z) - kBoxHalf};
    const V3 o{std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)};
    return std::sqrt(dot(o, o)) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - kBoxRound;
}

V3 boxNormal(V3 p) {
    constexpr float e = 0.002f;
    return norm({boxSdf({p.x + e, p.y, p.z}) - boxSdf({p.x - e, p.y, p.z}),
                 boxSdf({p.x, p.y + e, p.z}) - boxSdf({p.x, p.y - e, p.z}),
                 boxSdf({p.x, p.y, p.z + e}) - boxSdf({p.x, p.y, p.z - e})});
}

Hit traceSphere(const Spin& spin, float nx, float ny) {
    Hit out;
    const float d2 = nx * nx + ny * ny;
    if (d2 > 1.0f) {
        return out;
    }
    const V3 nView{nx, ny, std::sqrt(1.0f - d2)};
    out.n = norm(spin.toObject(nView));
    out.pos = out.n;
    out.u = 0.5f + std::atan2(out.n.z, out.n.x) * 0.15915494f;
    out.v = 0.5f - std::asin(std::clamp(out.n.y, -1.0f, 1.0f)) * 0.31830988f;
    out.hit = true;
    return out;
}

Hit traceCube(const Spin& spin, float nx, float ny) {
    Hit out;
    const V3 origin = spin.toObject({nx * 1.2f, ny * 1.2f, 3.0f});
    const V3 dir = spin.toObject({0.0f, 0.0f, -1.0f});
    float t = 0.0f;
    V3 p = origin;
    for (int i = 0; i < 48; ++i) {
        p = origin + dir * t;
        const float d = boxSdf(p);
        if (d < 0.0008f) {
            out.hit = true;
            break;
        }
        t += d;
        if (t > 6.0f) {
            return out;
        }
    }
    if (!out.hit) {
        return out;
    }
    out.pos = p;
    out.n = boxNormal(p);
    const V3 a{std::fabs(out.n.x), std::fabs(out.n.y), std::fabs(out.n.z)};
    float s = p.x;
    float r = p.y;
    if (a.x >= a.y && a.x >= a.z) {
        s = p.z * (out.n.x > 0.0f ? -1.0f : 1.0f);
        r = p.y;
    } else if (a.y >= a.z) {
        s = p.x;
        r = p.z * (out.n.y > 0.0f ? 1.0f : -1.0f);
    } else {
        s = p.x * (out.n.z > 0.0f ? 1.0f : -1.0f);
        r = p.y;
    }
    out.u = s / (kBoxHalf * 2.0f + 0.0001f) + 0.5f;
    out.v = 0.5f - r / (kBoxHalf * 2.0f + 0.0001f);
    return out;
}

Hit tracePlane(const Spin& spin, float nx, float ny) {
    Hit out;
    const V3 origin = spin.toObject({0.0f, 0.0f, 2.6f});
    const V3 dir = spin.toObject(norm({nx * 0.62f, ny * 0.62f, -1.0f}));
    if (std::fabs(dir.z) < 1.0e-5f) {
        return out;
    }
    const float t = -origin.z / dir.z;
    if (t <= 0.0f) {
        return out;
    }
    const V3 p = origin + dir * t;
    constexpr float half = 1.0f;
    if (std::fabs(p.x) > half || std::fabs(p.y) > half) {
        return out;
    }
    out.hit = true;
    out.pos = p;
    out.n = {0.0f, 0.0f, 1.0f};
    out.u = p.x * 0.5f + 0.5f;
    out.v = 0.5f - p.y * 0.5f;
    return out;
}

const uint8_t* texel(const uint8_t* atlas, uint8_t cell, float u, float v) {
    u -= std::floor(u);
    v -= std::floor(v);
    const uint32_t sx = std::min(static_cast<uint32_t>(u * static_cast<float>(kMatCell)), kMatCell - 1u);
    const uint32_t sy = std::min(static_cast<uint32_t>(v * static_cast<float>(kMatCell)), kMatCell - 1u);
    const uint32_t col = cell % 4u;
    const uint32_t row = cell / 4u;
    return atlas + (static_cast<size_t>(row * kMatCell + sy) * kMatAtlasW + col * kMatCell + sx) * 4u;
}

V3 srgbLinear(const uint8_t* px) {
    const float r = px[0] / 255.0f;
    const float g = px[1] / 255.0f;
    const float b = px[2] / 255.0f;
    return {r * r, g * g, b * b};
}

V3 shade(const BhmatDoc& doc, const uint8_t* atlas, const Spin& spin, const Hit& hit, V3 view) {
    const float u = hit.u * doc.uvTiling[0] + doc.uvOffset[0];
    const float v = hit.v * doc.uvTiling[1] + doc.uvOffset[1];
    V3 albedo = srgbLinear(texel(atlas, 0, u, v)) * V3{doc.base[0], doc.base[1], doc.base[2]} * doc.albedoGain;
    if (doc.unlit != 0) {
        return albedo;
    }
    float rough = doc.roughness;
    float metal = doc.metallic;
    float ao = 1.0f;
    if (doc.mapPack != 0 && doc.ormPath[0] != '\0') {
        const uint8_t* orm = texel(atlas, 6, u, v);
        ao = orm[0] / 255.0f;
        rough *= orm[1] / 255.0f;
        metal *= orm[2] / 255.0f;
    } else {
        if (doc.roughPath[0] != '\0') {
            rough *= texel(atlas, 2, u, v)[0] / 255.0f;
        }
        if (doc.metalPath[0] != '\0') {
            metal *= texel(atlas, 1, u, v)[0] / 255.0f;
        }
        if (doc.aoPath[0] != '\0') {
            ao = texel(atlas, 5, u, v)[0] / 255.0f;
        }
    }
    ao = 1.0f + (ao - 1.0f) * clamp01(doc.aoStrength);
    rough = std::clamp(rough, 0.045f, 1.0f);
    metal = clamp01(metal);

    V3 n = hit.n;
    if (doc.normalPath[0] != '\0') {
        const uint8_t* np = texel(atlas, 3, u, v);
        const float scale = doc.normalScale * doc.normalGain;
        const float tx = (np[0] / 255.0f * 2.0f - 1.0f) * scale;
        float ty = (np[1] / 255.0f * 2.0f - 1.0f) * scale;
        if (doc.flipNormalY != 0) {
            ty = -ty;
        }
        const float tz = std::sqrt(std::max(0.0f, 1.0f - tx * tx - ty * ty));
        const V3 up = std::fabs(n.y) > 0.95f ? V3{1.0f, 0.0f, 0.0f} : V3{0.0f, 1.0f, 0.0f};
        const V3 tan = norm(cross(up, n));
        const V3 bit = cross(n, tan);
        n = norm(tan * tx + bit * ty + n * tz);
    }
    n = norm(spin.toView(n));

    const V3 l = norm({0.55f, 0.75f, 0.35f});
    const V3 h = norm(l + view);
    const float nol = clamp01(dot(n, l));
    const float nov = std::max(dot(n, view), 1.0e-3f);
    const float noh = clamp01(dot(n, h));
    const float voh = clamp01(dot(view, h));
    const float a = rough * rough;
    const float a2 = a * a;
    const float dd = noh * noh * (a2 - 1.0f) + 1.0f;
    const float distribution = a2 / (3.14159265f * dd * dd);
    const float k = a * 0.5f;
    const float vis = (nol / (nol * (1.0f - k) + k)) * (nov / (nov * (1.0f - k) + k));
    const float f0 = 0.16f * doc.reflectance * doc.reflectance;
    const V3 specColor = lerp({f0, f0, f0}, albedo, metal);
    const float fw = std::pow(1.0f - voh, 5.0f);
    const V3 fresnel = specColor + (V3{1.0f, 1.0f, 1.0f} - specColor) * fw;
    const V3 diffuseColor = albedo * (1.0f - metal);
    const V3 sun{3.5f, 3.2f, 2.9f};
    const V3 direct = (diffuseColor * (1.0f / 3.14159265f) * (V3{1.0f, 1.0f, 1.0f} - fresnel)
                       + fresnel * (distribution * vis / std::max(4.0f * nol * nov, 1.0e-4f)))
        * sun * nol;

    const V3 sky{0.35f, 0.4f, 0.5f};
    const V3 ground{0.15f, 0.12f, 0.1f};
    const V3 irradiance = lerp(ground, sky, n.y * 0.5f + 0.5f);
    const V3 refl = n * (2.0f * dot(n, view)) - view;
    const V3 dome = lerp(ground, sky, refl.y * 0.5f + 0.5f) * (1.0f - rough * 0.55f);
    const float fenv = std::pow(1.0f - nov, 5.0f);
    const V3 envSpec = (specColor + (V3{1.0f, 1.0f, 1.0f} - specColor) * (fenv * (1.0f - rough))) * dome;
    V3 color = direct + (diffuseColor * irradiance + envSpec) * ao;
    if (doc.emissive > 0.0f) {
        V3 glow{doc.emissiveColor[0], doc.emissiveColor[1], doc.emissiveColor[2]};
        if (doc.emissivePath[0] != '\0') {
            glow = glow * srgbLinear(texel(atlas, 7, u, v));
        }
        color = color + glow * doc.emissive;
    }
    return color;
}

uint8_t display(float linear) {
    const float mapped = linear / (1.0f + linear);
    return byteOf(std::pow(std::max(mapped, 0.0f) * 1.18f, 0.45454545f));
}

} // namespace

void materialBakePreview(const BhmatDoc& doc, const uint8_t* atlas, MatShape shape, float yaw, float pitch, uint8_t* dst) {
    const Spin spin{std::cos(yaw), std::sin(yaw), std::cos(pitch), std::sin(pitch)};
    constexpr float half = static_cast<float>(kMatSphereSide) * 0.5f;
    constexpr float radius = 70.0f;
    for (uint32_t y = 0; y < kMatSphereSide; ++y) {
        for (uint32_t x = 0; x < kMatSphereSide; ++x) {
            uint8_t* px = dst + (static_cast<size_t>(y) * kMatSphereSide + x) * 4u;
            const float nx = (static_cast<float>(x) + 0.5f - half) / radius;
            const float ny = (half - (static_cast<float>(y) + 0.5f)) / radius;
            Hit hit;
            V3 view{0.0f, 0.0f, 1.0f};
            switch (shape) {
            case MatShape::Sphere:
                hit = traceSphere(spin, nx, ny);
                break;
            case MatShape::Cube:
                hit = traceCube(spin, nx, ny);
                break;
            case MatShape::Plane:
                hit = tracePlane(spin, nx, ny);
                view = norm({-nx * 0.62f, -ny * 0.62f, 1.0f});
                break;
            }
            if (!hit.hit) {
                px[0] = 46;
                px[1] = 48;
                px[2] = 52;
                px[3] = 0;
                continue;
            }
            const V3 c = shade(doc, atlas, spin, hit, view);
            px[0] = display(c.x);
            px[1] = display(c.y);
            px[2] = display(c.z);
            px[3] = 255;
        }
    }
}

} // namespace burnhope
