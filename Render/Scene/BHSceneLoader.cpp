#include "BHSceneLoader.hpp"
#include "BHSceneFormat.hpp"
#include "../../Utils/Components.hpp"

#include <cstring>
#include <iostream>
#include <vector>
#include <unordered_map>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace burnhope::scene {
namespace {

    // Thin cross-platform read-only memory mapping. On POSIX this is a real
    // zero-copy mmap; on Windows it falls back to a MapViewOfFile mapping
    // (still zero-copy). Either way there is no text/JSON parsing pass.
    struct MappedFile {
        const uint8_t* data = nullptr;
        size_t size = 0;

#if defined(_WIN32)
        HANDLE hFile = INVALID_HANDLE_VALUE;
        HANDLE hMap = nullptr;
#else
        int fd = -1;
#endif

        bool Open(const std::string& path) {
#if defined(_WIN32)
            hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(hFile, &fileSize)) { Close(); return false; }
            size = static_cast<size_t>(fileSize.QuadPart);
            hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!hMap) { Close(); return false; }
            data = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, size));
            if (!data) { Close(); return false; }
            return true;
#else
            fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0) return false;
            struct stat st{};
            if (fstat(fd, &st) != 0) { Close(); return false; }
            size = static_cast<size_t>(st.st_size);
            if (size == 0) { Close(); return false; }
            void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped == MAP_FAILED) { Close(); return false; }
            data = static_cast<const uint8_t*>(mapped);
            return true;
#endif
        }

        void Close() {
#if defined(_WIN32)
            if (data) UnmapViewOfFile(data);
            if (hMap) CloseHandle(hMap);
            if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
            data = nullptr; hMap = nullptr; hFile = INVALID_HANDLE_VALUE;
#else
            if (data) munmap(const_cast<uint8_t*>(data), size);
            if (fd >= 0) ::close(fd);
            data = nullptr; fd = -1;
#endif
        }

        ~MappedFile() { Close(); }
    };

    std::string ReadStr(const MappedFile& file, const BHSceneHeader& header, StrRef ref) {
        if (ref.length == 0) return {};
        const char* base = reinterpret_cast<const char*>(file.data + header.stringPoolOffset);
        return std::string(base + ref.offset, ref.length);
    }

    template <typename T>
    const T* BlockData(const MappedFile& file, const BHComponentTableEntry& entry) {
        return reinterpret_cast<const T*>(file.data + entry.blockOffset + entry.count * sizeof(uint64_t));
    }

    const uint64_t* BlockIDs(const MappedFile& file, const BHComponentTableEntry& entry) {
        return reinterpret_cast<const uint64_t*>(file.data + entry.blockOffset);
    }

    const BHComponentTableEntry* FindEntry(const MappedFile& file, const BHSceneHeader& header, uint64_t typeHash) {
        const auto* table = reinterpret_cast<const BHComponentTableEntry*>(file.data + header.componentTableOffset);
        for (uint64_t i = 0; i < header.componentTableCount; ++i) {
            if (table[i].componentTypeHash == typeHash) return &table[i];
        }
        return nullptr;
    }
}

bool BHSceneLoader::Load(const std::string& filepath, flecs::world& world, BurnhopeDevice* device) {
    MappedFile file;
    if (!file.Open(filepath)) {
        std::cerr << "[BHSceneLoader] Failed to open " << filepath << "\n";
        return false;
    }
    if (file.size < sizeof(BHSceneHeader)) return false;

    BHSceneHeader header;
    std::memcpy(&header, file.data, sizeof(BHSceneHeader));
    if (header.magic != kMagic) {
        std::cerr << "[BHSceneLoader] Bad magic in " << filepath << "\n";
        return false;
    }
    if (header.version != kVersion) {
        std::cerr << "[BHSceneLoader] Unsupported version " << header.version << " in " << filepath << "\n";
        return false;
    }

    // Pre-create every entity keyed by GUID so hierarchy/foreign references
    // (parentID, children) can resolve regardless of block iteration order.
    std::unordered_map<uint64_t, flecs::entity> byId;
    auto ensureEntity = [&](uint64_t id) -> flecs::entity {
        auto it = byId.find(id);
        if (it != byId.end()) return it->second;
        flecs::entity e = world.entity();
        e.set<IDComponent>(IDComponent(id));
        byId.emplace(id, e);
        return e;
    };

    if (const auto* entry = FindEntry(file, header, kHashTag)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<TagRecord>(file, *entry);
        for (uint64_t i = 0; i < entry->count; ++i) {
            ensureEntity(ids[i]).set<TagComponent>({ReadStr(file, header, records[i].name)});
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashTransform)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<TransformRecord>(file, *entry);
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];
            e.set<Position3>({r.px, r.py, r.pz});
            e.set<RotationEuler>({r.rx, r.ry, r.rz});
            e.set<Scale3>({r.sx, r.sy, r.sz});
            LocalMatrix lm{};
            lm.dirty = 1;
            e.set<LocalMatrix>(lm);
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashHierarchy)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<HierarchyRecord>(file, *entry);
        const uint8_t* blobBase = file.data + header.blobPoolOffset;
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];
            HierarchyComponent hc;
            hc.parentID = r.parentID;
            if (r.children.count > 0) {
                const uint64_t* childIds = reinterpret_cast<const uint64_t*>(blobBase + r.children.offset);
                hc.childrenIDs.assign(childIds, childIds + r.children.count);
            }
            e.set<HierarchyComponent>(std::move(hc));
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashMesh)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<MeshRecord>(file, *entry);
        const uint8_t* blobBase = file.data + header.blobPoolOffset;
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];

            MeshComponent mc;
            mc.modelPath = ReadStr(file, header, r.modelPath);
            mc.isStatic = (r.flags & static_cast<uint8_t>(MeshFlags::Static)) != 0;
            mc.isVisible = (r.flags & static_cast<uint8_t>(MeshFlags::Visible)) != 0;
            mc.castShadow = (r.flags & static_cast<uint8_t>(MeshFlags::CastShadow)) != 0;

            if (r.materialPaths.count > 0) {
                const auto* matRefs = reinterpret_cast<const StrRef*>(blobBase + r.materialPaths.offset);
                for (uint32_t m = 0; m < r.materialPaths.count; ++m) {
                    std::string p = ReadStr(file, header, matRefs[m]);
                    mc.materialPaths.push_back(p);
                    mc.materials.push_back((device && !p.empty()) ? Material::loadFromJson(*device, p) : nullptr);
                }
            }

            if (!mc.modelPath.empty() && device) {
                try {
                    mc.model = BurnhopeModel::createModelFromFile(*device, mc.modelPath);
                    if (mc.materials.size() < mc.model->getSubMeshes().size())
                        mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                    if (mc.materialPaths.size() < mc.model->getSubMeshes().size())
                        mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                } catch (const std::exception& ex) {
                    std::cerr << "[BHSceneLoader] Failed to load model: " << ex.what() << "\n";
                }
            }
            e.set<MeshComponent>(std::move(mc));
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashLight)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<LightRecord>(file, *entry);
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];
            LightComponent lc;
            lc.needsShadowUpdate = true;
            lc.light.enable = r.enable != 0;
            lc.light.type = static_cast<LightType>(r.type);
            lc.light.castShadows = r.castShadows != 0;
            lc.light.mobility = static_cast<LightMobility>(r.mobility);
            lc.light.color = glm::vec3(r.colorR, r.colorG, r.colorB);
            lc.light.intensity = r.intensity;
            lc.light.radius = r.radius;
            e.set<LightComponent>(std::move(lc));
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashReflectionProbe)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<ReflectionProbeRecord>(file, *entry);
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];
            ReflectionProbeComponent rpc;
            rpc.radius = r.radius;
            rpc.resolution = r.resolution;
            rpc.updateNeeded = true;
            e.set<ReflectionProbeComponent>(std::move(rpc));
        }
    }

    if (const auto* entry = FindEntry(file, header, kHashDecal)) {
        const uint64_t* ids = BlockIDs(file, *entry);
        const auto* records = BlockData<DecalRecord>(file, *entry);
        for (uint64_t i = 0; i < entry->count; ++i) {
            flecs::entity e = ensureEntity(ids[i]);
            const auto& r = records[i];
            DecalComponent dc;
            dc.albedoPath = ReadStr(file, header, r.albedoPath);
            dc.normalPath = ReadStr(file, header, r.normalPath);
            dc.opacity = r.opacity;
            if (device && !dc.albedoPath.empty()) dc.albedoTex = BurnhopeTexture::createTextureFromFile(*device, dc.albedoPath);
            if (device && !dc.normalPath.empty()) dc.normalTex = BurnhopeTexture::createDataTextureFromFile(*device, dc.normalPath);
            e.set<DecalComponent>(std::move(dc));
        }
    }

    return true;
}
}
