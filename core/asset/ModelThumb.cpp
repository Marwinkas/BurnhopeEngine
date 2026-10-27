#include "asset/ModelThumb.hpp"

#include <spdlog/spdlog.h>

#include "io/File.hpp"
#include "platform/Jobs.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace burnhope {
namespace {

constexpr float kYaw = 35.0f * 3.14159265f / 180.0f;
constexpr float kPitch = -25.0f * 3.14159265f / 180.0f;
constexpr uint32_t kSuper = 2;                         // samples per texel edge
constexpr uint32_t kSide = kModelThumbPx * kSuper;
constexpr float kFill = 0.9f;                          // share of the cell the model spans
constexpr std::array<float, 3> kSun = {-0.45f, 0.75f, 0.5f}; // towards the sun, view space (x right, y up, z to the viewer)
constexpr float kAlbedo[3] = {0.78f, 0.80f, 0.85f};
constexpr float kAmbientSky = 0.34f;
constexpr float kAmbientGround = 0.14f;
constexpr float kKey = 0.92f;

struct Vec3 {
    float x, y, z;
};

Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

uint8_t encode(float linear) {
    return static_cast<uint8_t>(std::lround(std::clamp(std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f));
}

void fillPlate(const uint8_t plate[3], uint8_t* rgba) {
    for (uint32_t i = 0; i < kModelThumbPx * kModelThumbPx; ++i) {
        rgba[i * 4 + 0] = plate[0];
        rgba[i * 4 + 1] = plate[1];
        rgba[i * 4 + 2] = plate[2];
        rgba[i * 4 + 3] = 255;
    }
}

} // namespace

bool modelBakeThumb(const ModelMesh& mesh, const uint8_t plate[3], uint8_t* rgba) {
    fillPlate(plate, rgba);
    const size_t verts = mesh.xyz.size() / 3;
    if (verts == 0 || mesh.idx.size() < 3) {
        return false;
    }
    float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float hi[3] = {-lo[0], -lo[1], -lo[2]};
    for (size_t v = 0; v < verts; ++v) {
        for (size_t a = 0; a < 3; ++a) {
            const float c = mesh.xyz[v * 3 + a];
            if (std::isfinite(c)) {
                lo[a] = std::min(lo[a], c);
                hi[a] = std::max(hi[a], c);
            }
        }
    }
    const float half = 0.5f * std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    if (!(half > 0.0f)) {
        return false;
    }
    // Centre, scale into [-1, 1], turn: yaw about Y, then pitch about X (so the tops show).
    std::vector<Vec3> view(verts);
    const float cy = std::cos(kYaw), sy = std::sin(kYaw), cp = std::cos(kPitch), sp = std::sin(-kPitch); // the camera pitches down: the world turns up
    float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
    for (size_t v = 0; v < verts; ++v) {
        const float x = (mesh.xyz[v * 3 + 0] - 0.5f * (lo[0] + hi[0])) / half;
        const float y = (mesh.xyz[v * 3 + 1] - 0.5f * (lo[1] + hi[1])) / half;
        const float z = (mesh.xyz[v * 3 + 2] - 0.5f * (lo[2] + hi[2])) / half;
        const float x1 = cy * x + sy * z;
        const float z1 = -sy * x + cy * z;
        const Vec3 p = {x1, cp * y - sp * z1, sp * y + cp * z1};
        view[v] = p;
        if (std::isfinite(p.x) && std::isfinite(p.y)) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    const float span = std::max(maxX - minX, maxY - minY);
    if (!(span > 0.0f)) {
        return false;
    }
    const float scale = kFill * static_cast<float>(kSide) / span;
    const float offX = 0.5f * static_cast<float>(kSide) - 0.5f * (minX + maxX) * scale;
    const float offY = 0.5f * static_cast<float>(kSide) + 0.5f * (minY + maxY) * scale; // screen y points down

    std::vector<float> depth(static_cast<size_t>(kSide) * kSide, -std::numeric_limits<float>::max());
    std::vector<float> shade(static_cast<size_t>(kSide) * kSide, -1.0f);
    const float sunLen = std::sqrt(kSun[0] * kSun[0] + kSun[1] * kSun[1] + kSun[2] * kSun[2]);
    for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
        const uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
        if (i0 >= verts || i1 >= verts || i2 >= verts) {
            continue;
        }
        const Vec3 a = view[i0], b = view[i1], c = view[i2];
        Vec3 n = cross(sub(b, a), sub(c, a));
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (!(len > 1e-12f) || !std::isfinite(len)) {
            continue;
        }
        n = {n.x / len, n.y / len, n.z / len};
        if (n.z < 0.0f) { // two-sided: the face turns to the viewer
            n = {-n.x, -n.y, -n.z};
        }
        const float diffuse = std::max(0.0f, (n.x * kSun[0] + n.y * kSun[1] + n.z * kSun[2]) / sunLen);
        const float ambient = kAmbientGround + (kAmbientSky - kAmbientGround) * (0.5f * n.y + 0.5f);
        const float light = ambient + kKey * diffuse;
        const float ax = a.x * scale + offX, ay = offY - a.y * scale;
        const float bx = b.x * scale + offX, by = offY - b.y * scale;
        const float cx = c.x * scale + offX, cy2 = offY - c.y * scale;
        const float area = edge(ax, ay, bx, by, cx, cy2);
        if (!(std::fabs(area) > 1e-9f)) {
            continue;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int x1 = std::min(static_cast<int>(kSide) - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy2}))));
        const int y1 = std::min(static_cast<int>(kSide) - 1, static_cast<int>(std::ceil(std::max({ay, by, cy2}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                const float w0 = edge(bx, by, cx, cy2, px, py) / area;
                const float w1 = edge(cx, cy2, ax, ay, px, py) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                const size_t at = static_cast<size_t>(y) * kSide + static_cast<size_t>(x);
                if (z > depth[at]) {
                    depth[at] = z;
                    shade[at] = light;
                }
            }
        }
    }
    bool any = false;
    for (uint32_t y = 0; y < kModelThumbPx; ++y) {
        for (uint32_t x = 0; x < kModelThumbPx; ++x) {
            float sum[3] = {};
            uint32_t covered = 0;
            for (uint32_t sy = 0; sy < kSuper; ++sy) {
                for (uint32_t sx = 0; sx < kSuper; ++sx) {
                    const float l = shade[static_cast<size_t>(y * kSuper + sy) * kSide + x * kSuper + sx];
                    if (l >= 0.0f) {
                        ++covered;
                        for (int ch = 0; ch < 3; ++ch) {
                            sum[ch] += static_cast<float>(encode(kAlbedo[ch] * l));
                        }
                    }
                }
            }
            if (covered == 0) {
                continue;
            }
            any = true;
            const float cover = static_cast<float>(covered) / static_cast<float>(kSuper * kSuper);
            uint8_t* dst = rgba + (static_cast<size_t>(y) * kModelThumbPx + x) * 4u;
            for (int ch = 0; ch < 3; ++ch) {
                const float color = sum[ch] / static_cast<float>(covered);
                dst[ch] = static_cast<uint8_t>(std::lround(color * cover + static_cast<float>(plate[ch]) * (1.0f - cover)));
            }
        }
    }
    return any;
}


namespace {

namespace fs = std::filesystem;

std::string cacheFile(const fs::path& root, uint64_t key, int64_t mtime) {
    char name[64];
    std::snprintf(name, sizeof(name), "%016llx_%lld.bin", static_cast<unsigned long long>(key), static_cast<long long>(mtime));
    return (root / "cache" / "thumbs" / name).string();
}

bool readCache(const std::string& file, uint8_t* rgba) {
    MappedFile map{};
    const bool ok = fileMapReadOnly(file.c_str(), &map) && map.data != nullptr && map.size == kModelThumbBytes;
    if (ok) {
        std::memcpy(rgba, map.data, kModelThumbBytes);
    }
    fileUnmap(&map);
    return ok;
}

void writeCache(const std::string& file, const uint8_t* rgba) {
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    if (FILE* out = std::fopen(file.c_str(), "wb")) {
        std::fwrite(rgba, 1, kModelThumbBytes, out);
        std::fclose(out);
    }
}

} // namespace

// One bake at a time on the engine job pool. `state` hands the request to the job (Working) and the result back to the
// main thread (Finished); the request fields are written before the release store of Working, the pixels before Finished.
enum BakeState : uint8_t { kBakeIdle = 0, kBakeWorking, kBakeFinished };

struct ModelBaker {
    JobPool& pool;
    uint8_t plate[3];
    std::atomic<uint8_t> state{kBakeIdle};
    std::string path;
    std::string file;
    uint64_t key = 0;
    int64_t mtime = 0;
    uint64_t doneKey = 0;
    int64_t doneMtime = 0;
    uint8_t pixels[kModelThumbBytes]{};
};

namespace {

void bakeJob(void* user) {
    auto& baker = *static_cast<ModelBaker*>(user);
    ModelMesh mesh;
    uint8_t rgba[kModelThumbBytes];
    const bool loaded = modelLoadMesh(baker.path.c_str(), mesh);
    if (!loaded || !modelBakeThumb(mesh, baker.plate, rgba)) {
        spdlog::warn("model thumb: cannot preview {}", baker.path);
        for (uint32_t i = 0; i < kModelThumbPx * kModelThumbPx; ++i) { // a failed model keeps a plain plate and is not retried
            rgba[i * 4 + 0] = baker.plate[0];
            rgba[i * 4 + 1] = baker.plate[1];
            rgba[i * 4 + 2] = baker.plate[2];
            rgba[i * 4 + 3] = 255;
        }
    }
    writeCache(baker.file, rgba);
    std::memcpy(baker.pixels, rgba, kModelThumbBytes);
    baker.doneKey = baker.key;
    baker.doneMtime = baker.mtime;
    baker.state.store(kBakeFinished, std::memory_order_release);
}

} // namespace

void ModelBakerDelete::operator()(ModelBaker* baker) const {
    if (baker == nullptr) {
        return;
    }
    jobsDrain(baker->pool); // the running bake reads the baker
    delete baker;
}

ModelBaker* assetModelBakerCreate(JobPool& pool, const uint8_t plate[3]) {
    return new ModelBaker{.pool = pool, .plate = {plate[0], plate[1], plate[2]}};
}

ModelThumbState assetFetchModelThumb(ModelBaker& baker, const fs::path& root, const char* path, uint64_t key, int64_t mtime, uint8_t* rgba) {
    uint8_t st = baker.state.load(std::memory_order_acquire);
    if (st == kBakeFinished) {
        const bool mine = baker.doneKey == key && baker.doneMtime == mtime;
        if (mine) {
            std::memcpy(rgba, baker.pixels, kModelThumbBytes);
        }
        baker.state.store(kBakeIdle, std::memory_order_relaxed); // a result nobody asks for is already on disk
        st = kBakeIdle;
        if (mine) {
            return ModelThumbState::Ready;
        }
    }
    if (st == kBakeWorking && baker.key == key && baker.mtime == mtime) {
        return ModelThumbState::Pending;
    }
    const std::string file = cacheFile(root, key, mtime);
    if (readCache(file, rgba)) {
        return ModelThumbState::Ready;
    }
    if (st == kBakeIdle) {
        baker.path = path;
        baker.file = file;
        baker.key = key;
        baker.mtime = mtime;
        baker.state.store(kBakeWorking, std::memory_order_release);
        if (!jobsRun(baker.pool, Job{.fn = bakeJob, .user = &baker})) {
            baker.state.store(kBakeIdle, std::memory_order_relaxed);
        }
    }
    return ModelThumbState::Pending;
}

} // namespace burnhope
