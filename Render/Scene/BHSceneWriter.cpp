#include "BHSceneWriter.hpp"
#include "BHSceneFormat.hpp"
#include "../../Utils/Components.hpp"

#include <fstream>
#include <vector>
#include <cstring>

namespace burnhope::scene {
namespace {

    // Growable byte buffer used to accumulate the blob pool / string pool /
    // component blocks before the final file is written out in one shot.
    struct ByteBuffer {
        std::vector<uint8_t> bytes;

        uint32_t Append(const void* data, size_t size) {
            uint32_t offset = static_cast<uint32_t>(bytes.size());
            const uint8_t* p = static_cast<const uint8_t*>(data);
            bytes.insert(bytes.end(), p, p + size);
            return offset;
        }

        template <typename T>
        uint32_t AppendValue(const T& value) {
            return Append(&value, sizeof(T));
        }
    };

    StrRef WriteString(ByteBuffer& stringPool, const std::string& s) {
        StrRef ref;
        ref.offset = static_cast<uint32_t>(stringPool.bytes.size());
        ref.length = static_cast<uint32_t>(s.size());
        stringPool.Append(s.data(), s.size());
        return ref;
    }

    template <typename T>
    struct ComponentBlock {
        std::vector<uint64_t> entityIDs;
        std::vector<T> records;

        void Push(uint64_t id, const T& record) {
            entityIDs.push_back(id);
            records.push_back(record);
        }
    };

    template <typename T>
    void EmitBlock(std::vector<BHComponentTableEntry>& table, ByteBuffer& blocks,
                    uint64_t typeHash, const ComponentBlock<T>& block) {
        if (block.entityIDs.empty()) return;

        BHComponentTableEntry entry{};
        entry.componentTypeHash = typeHash;
        entry.count = block.entityIDs.size();
        entry.stride = sizeof(T);
        entry.blockOffset = blocks.bytes.size();

        blocks.Append(block.entityIDs.data(), block.entityIDs.size() * sizeof(uint64_t));
        blocks.Append(block.records.data(), block.records.size() * sizeof(T));

        table.push_back(entry);
    }
}

bool BHSceneWriter::Save(flecs::world& world, const std::string& filepath) {
    ByteBuffer blocks;
    ByteBuffer blobPool;
    ByteBuffer stringPool;

    ComponentBlock<TagRecord> tags;
    ComponentBlock<TransformRecord> transforms;
    ComponentBlock<HierarchyRecord> hierarchies;
    ComponentBlock<MeshRecord> meshes;
    ComponentBlock<LightRecord> lights;
    ComponentBlock<ReflectionProbeRecord> probes;
    ComponentBlock<DecalRecord> decals;

    uint64_t entityCount = 0;

    world.each<IDComponent>([&](flecs::entity entity, IDComponent& idComp) {
        entityCount++;
        const uint64_t id = idComp.ID;

        if (const auto* tag = entity.get<TagComponent>()) {
            TagRecord rec{};
            rec.name = WriteString(stringPool, tag->name);
            tags.Push(id, rec);
        }

        if (entity.has<Position3>() && entity.has<RotationEuler>() && entity.has<Scale3>()) {
            const auto& pos = *entity.get<Position3>();
            const auto& rot = *entity.get<RotationEuler>();
            const auto& scale = *entity.get<Scale3>();
            TransformRecord rec{};
            rec.px = pos.x; rec.py = pos.y; rec.pz = pos.z;
            rec.rx = rot.x; rec.ry = rot.y; rec.rz = rot.z;
            rec.sx = scale.x; rec.sy = scale.y; rec.sz = scale.z;
            transforms.Push(id, rec);
        }

        if (const auto* hc = entity.get<HierarchyComponent>()) {
            HierarchyRecord rec{};
            rec.parentID = hc->parentID;
            rec.children.count = static_cast<uint32_t>(hc->childrenIDs.size());
            if (!hc->childrenIDs.empty()) {
                rec.children.offset = blobPool.Append(
                    hc->childrenIDs.data(), hc->childrenIDs.size() * sizeof(uint64_t));
            }
            hierarchies.Push(id, rec);
        }

        if (const auto* mc = entity.get<MeshComponent>()) {
            MeshRecord rec{};
            rec.modelPath = WriteString(stringPool, mc->modelPath);
            rec.flags = (mc->isStatic ? static_cast<uint8_t>(MeshFlags::Static) : 0)
                      | (mc->isVisible ? static_cast<uint8_t>(MeshFlags::Visible) : 0)
                      | (mc->castShadow ? static_cast<uint8_t>(MeshFlags::CastShadow) : 0);

            if (!mc->materialPaths.empty()) {
                std::vector<StrRef> matRefs;
                matRefs.reserve(mc->materialPaths.size());
                for (const auto& p : mc->materialPaths) matRefs.push_back(WriteString(stringPool, p));
                rec.materialPaths.count = static_cast<uint32_t>(matRefs.size());
                rec.materialPaths.offset = blobPool.Append(matRefs.data(), matRefs.size() * sizeof(StrRef));
            }
            meshes.Push(id, rec);
        }

        if (const auto* lc = entity.get<LightComponent>()) {
            LightRecord rec{};
            rec.enable = lc->light.enable ? 1 : 0;
            rec.type = static_cast<uint8_t>(lc->light.type);
            rec.castShadows = lc->light.castShadows ? 1 : 0;
            rec.mobility = static_cast<uint8_t>(lc->light.mobility);
            rec.colorR = lc->light.color.x; rec.colorG = lc->light.color.y; rec.colorB = lc->light.color.z;
            rec.intensity = lc->light.intensity;
            rec.radius = lc->light.radius;
            lights.Push(id, rec);
        }

        if (const auto* rpc = entity.get<ReflectionProbeComponent>()) {
            ReflectionProbeRecord rec{};
            rec.radius = rpc->radius;
            rec.resolution = rpc->resolution;
            probes.Push(id, rec);
        }

        if (const auto* dc = entity.get<DecalComponent>()) {
            DecalRecord rec{};
            rec.albedoPath = WriteString(stringPool, dc->albedoPath);
            rec.normalPath = WriteString(stringPool, dc->normalPath);
            rec.opacity = dc->opacity;
            decals.Push(id, rec);
        }
    });

    std::vector<BHComponentTableEntry> table;
    EmitBlock(table, blocks, kHashTag, tags);
    EmitBlock(table, blocks, kHashTransform, transforms);
    EmitBlock(table, blocks, kHashHierarchy, hierarchies);
    EmitBlock(table, blocks, kHashMesh, meshes);
    EmitBlock(table, blocks, kHashLight, lights);
    EmitBlock(table, blocks, kHashReflectionProbe, probes);
    EmitBlock(table, blocks, kHashDecal, decals);

    // Layout: header | component table | blocks | blob pool | string pool.
    BHSceneHeader header{};
    header.entityCount = entityCount;

    uint64_t cursor = sizeof(BHSceneHeader);
    header.componentTableOffset = cursor;
    header.componentTableCount = table.size();
    cursor += table.size() * sizeof(BHComponentTableEntry);

    // Block offsets recorded above are relative to the start of `blocks`;
    // rebase them onto the final file layout now that we know where the
    // blocks region starts.
    uint64_t blocksFileOffset = cursor;
    for (auto& entry : table) entry.blockOffset += blocksFileOffset;
    cursor += blocks.bytes.size();

    header.blobPoolOffset = cursor;
    header.blobPoolSize = blobPool.bytes.size();
    cursor += blobPool.bytes.size();

    header.stringPoolOffset = cursor;
    header.stringPoolSize = stringPool.bytes.size();

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(table.data()), table.size() * sizeof(BHComponentTableEntry));
    file.write(reinterpret_cast<const char*>(blocks.bytes.data()), blocks.bytes.size());
    file.write(reinterpret_cast<const char*>(blobPool.bytes.data()), blobPool.bytes.size());
    file.write(reinterpret_cast<const char*>(stringPool.bytes.data()), stringPool.bytes.size());

    return file.good();
}
}
