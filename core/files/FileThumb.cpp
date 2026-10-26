#include "files/FileThumb.hpp"

namespace burnhope {
namespace {

uint64_t hashPath(const char* path) {
    uint64_t h = 1469598103934665603ull;
    for (; *path != '\0'; ++path) {
        h = (h ^ static_cast<uint8_t>(*path)) * 1099511628211ull;
    }
    return h != 0 ? h : 1;
}

uint8_t pickCell(const FileThumbs& cache) {
    uint8_t best = 0;
    for (uint8_t i = 0; i < kThumbCells; ++i) {
        if (cache.key[i] == 0) {
            return i;
        }
        if (cache.used[i] < cache.used[best]) {
            best = i;
        }
    }
    return best;
}

} // namespace

uint8_t thumbCell(FileThumbs& cache, const char* path, uint64_t mtime) {
    if (cache.bake == nullptr || cache.slot == kThumbNone || path == nullptr) {
        return kThumbNone;
    }
    const uint64_t key = hashPath(path);
    uint8_t cell = kThumbNone;
    for (uint8_t i = 0; i < kThumbCells; ++i) {
        if (cache.key[i] == key) {
            cell = i;
            break;
        }
    }
    if (cell != kThumbNone && cache.stamp[cell] == mtime) {
        cache.used[cell] = ++cache.clock;
        return cache.ok[cell] != 0 ? cell : kThumbNone;
    }
    if (cache.budget == 0) {
        cache.pending = 1;
        return cell != kThumbNone && cache.ok[cell] != 0 ? cell : kThumbNone;
    }
    --cache.budget;
    if (cell == kThumbNone) {
        cell = pickCell(cache);
    }
    cache.key[cell] = key;
    cache.stamp[cell] = mtime;
    cache.used[cell] = ++cache.clock;
    cache.ok[cell] = cache.bake(cache.user, path, cell) ? 1 : 0;
    return cache.ok[cell] != 0 ? cell : kThumbNone;
}

void thumbReset(FileThumbs& cache) {
    for (uint8_t i = 0; i < kThumbCells; ++i) {
        cache.key[i] = 0;
        cache.stamp[i] = 0;
        cache.used[i] = 0;
        cache.ok[i] = 0;
    }
    cache.clock = 0;
    cache.budget = kThumbPerFrame;
    cache.pending = 0;
}

bool thumbFrame(FileThumbs& cache) {
    cache.budget = kThumbPerFrame;
    const bool due = cache.pending != 0;
    cache.pending = 0;
    return due;
}

void thumbInvalidate(FileThumbs& cache, const char* path) {
    const uint64_t key = path != nullptr ? hashPath(path) : 0;
    bool found = false;
    for (uint8_t i = 0; i < kThumbCells && key != 0; ++i) {
        if (cache.key[i] == key) {
            cache.stamp[i] = kThumbStale;
            found = true;
        }
    }
    if (found) {
        return;
    }
    for (uint8_t i = 0; i < kThumbCells; ++i) {
        if (cache.key[i] != 0) {
            cache.stamp[i] = kThumbStale;
        }
    }
}

} // namespace burnhope
