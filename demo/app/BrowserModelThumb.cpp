#include "app/BrowserModelThumb.hpp"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "app/BrowserThumbs.hpp"
#include "asset/ModelMesh.hpp"
#include "io/File.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

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

struct ModelBaker {
    std::mutex lock;
    std::condition_variable wake;
    std::thread worker;
    bool quit = false;
    bool queued = false;   // a request waits for the worker
    bool working = false;  // the worker is baking it
    bool finished = false; // result below is unread
    std::string path;
    std::string file;
    uint64_t key = 0;
    int64_t mtime = 0;
    uint64_t doneKey = 0;
    int64_t doneMtime = 0;
    uint8_t pixels[kModelThumbBytes]{};

    void run() {
        std::unique_lock<std::mutex> hold(lock);
        while (true) {
            wake.wait(hold, [this] { return quit || queued; });
            if (quit) {
                return;
            }
            queued = false;
            working = true;
            const std::string model = path;
            const std::string cache = file;
            const uint64_t k = key;
            const int64_t m = mtime;
            hold.unlock();
            ModelMesh mesh;
            uint8_t rgba[kModelThumbBytes];
            const bool loaded = modelLoadMesh(model.c_str(), mesh);
            if (!loaded || !modelBakeThumb(mesh, kThumbPlate, rgba)) {
                spdlog::warn("model thumb: cannot preview {}", model);
                for (uint32_t i = 0; i < kModelThumbPx * kModelThumbPx; ++i) { // a failed model keeps a plain plate and is not retried
                    rgba[i * 4 + 0] = kThumbPlate[0];
                    rgba[i * 4 + 1] = kThumbPlate[1];
                    rgba[i * 4 + 2] = kThumbPlate[2];
                    rgba[i * 4 + 3] = 255;
                }
            }
            writeCache(cache, rgba);
            hold.lock();
            std::memcpy(pixels, rgba, kModelThumbBytes);
            doneKey = k;
            doneMtime = m;
            working = false;
            finished = true;
        }
    }
};

void ModelBakerDelete::operator()(ModelBaker* baker) const {
    if (baker == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> hold(baker->lock);
        baker->quit = true;
    }
    baker->wake.notify_all();
    baker->worker.join();
    delete baker;
}

ModelBaker* modelBakerCreate() {
    auto* baker = new ModelBaker();
    baker->worker = std::thread([baker] { baker->run(); });
    return baker;
}

ModelThumbState modelThumbFetch(ModelBaker& baker, const fs::path& root, const char* path, uint64_t key, int64_t mtime, uint8_t* rgba) {
    {
        std::lock_guard<std::mutex> hold(baker.lock);
        if (baker.finished) {
            const bool mine = baker.doneKey == key && baker.doneMtime == mtime;
            if (mine) {
                std::memcpy(rgba, baker.pixels, kModelThumbBytes);
            }
            baker.finished = false; // a result nobody asks for is already on disk
            if (mine) {
                return ModelThumbState::Ready;
            }
        }
        if ((baker.queued || baker.working) && baker.key == key && baker.mtime == mtime) {
            return ModelThumbState::Pending;
        }
    }
    const std::string file = cacheFile(root, key, mtime);
    if (readCache(file, rgba)) {
        return ModelThumbState::Ready;
    }
    std::lock_guard<std::mutex> hold(baker.lock);
    if (!baker.queued && !baker.working && !baker.finished) {
        baker.path = path;
        baker.file = file;
        baker.key = key;
        baker.mtime = mtime;
        baker.queued = true;
        baker.wake.notify_one();
    }
    return ModelThumbState::Pending;
}

} // namespace burnhope
