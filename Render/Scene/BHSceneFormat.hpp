#pragma once
#include <cstdint>
#include "../../Utils/MurmurHash3.hpp"

// Binary scene format: Header | component table | SoA component blocks | blob
// pool (variable-length arrays) | string pool (paths / names). The whole
// file is mmap'd and cast directly onto POD structs below — no per-field
// runtime parsing, per engine rules (`.bhscene` in main-rule.mdc).
namespace burnhope::scene {

    inline constexpr uint32_t kMagic = 0x43534842u; // "BHSC" little-endian
    inline constexpr uint32_t kVersion = 1;

    // Offset+length reference into the trailing string pool (UTF-8, not
    // null-terminated in-place; length is explicit).
    struct StrRef {
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    // Offset+count reference into the trailing blob pool. `offset` is a byte
    // offset from the start of the blob pool; `count` is the element count,
    // element size implied by the field using the ref (documented per use).
    struct ArrayRef {
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    struct BHSceneHeader {
        uint32_t magic = kMagic;
        uint32_t version = kVersion;
        uint64_t entityCount = 0;

        uint64_t componentTableOffset = 0;
        uint64_t componentTableCount = 0;

        uint64_t blobPoolOffset = 0;
        uint64_t blobPoolSize = 0;

        uint64_t stringPoolOffset = 0;
        uint64_t stringPoolSize = 0;
    };

    // One entry per component type present in the scene. `blockOffset` points
    // to `count` entries of: uint64_t entityID[count] followed immediately by
    // `count` records of `stride` bytes (the packed component data).
    struct BHComponentTableEntry {
        uint64_t componentTypeHash = 0;
        uint64_t blockOffset = 0;
        uint64_t count = 0;
        uint64_t stride = 0;
    };

    // --- Packed component records (std430-ish, explicit padding) ---

    struct TagRecord {
        StrRef name;
    };

    struct TransformRecord {
        float px = 0, py = 0, pz = 0;
        float rx = 0, ry = 0, rz = 0;
        float sx = 1, sy = 1, sz = 1;
        float _pad = 0;
    };

    struct HierarchyRecord {
        uint64_t parentID = 0;
        ArrayRef children; // elements are uint64_t entity IDs in the blob pool
        uint32_t _pad = 0;
    };

    enum class MeshFlags : uint8_t {
        None = 0,
        Static = 1u << 0,
        Visible = 1u << 1,
        CastShadow = 1u << 2,
    };

    struct MeshRecord {
        StrRef modelPath;
        ArrayRef materialPaths; // elements are StrRef in the blob pool
        uint8_t flags = 0;
        uint8_t _pad[3]{};
    };

    struct LightRecord {
        uint8_t enable = 1;
        uint8_t type = 0;
        uint8_t castShadows = 1;
        uint8_t mobility = 0;
        float colorR = 1, colorG = 1, colorB = 1;
        float intensity = 1;
        float radius = 10;
    };

    struct ReflectionProbeRecord {
        float radius = 10.0f;
        int32_t resolution = 256;
    };

    struct DecalRecord {
        StrRef albedoPath;
        StrRef normalPath;
        float opacity = 1.0f;
    };

    // Compile-time type hashes for the component table lookup. Kept as plain
    // functions (not consteval) because HashStringLiteral folds to a compile
    // time constant only when compilers choose to constant fold, and being
    // consteval-callable-in-non-consteval-context needs a wrapper here.
    template <typename Tag>
    inline uint64_t ComponentTypeHash(const char* name, size_t len) {
        return burnhope::hash::HashStringLiteral(name, len);
    }

#define BH_COMPONENT_HASH(name) (::burnhope::hash::HashStringLiteral(#name, sizeof(#name) - 1))

    inline constexpr uint64_t kHashTag = BH_COMPONENT_HASH(TagComponent);
    inline constexpr uint64_t kHashTransform = BH_COMPONENT_HASH(TransformComponent);
    inline constexpr uint64_t kHashHierarchy = BH_COMPONENT_HASH(HierarchyComponent);
    inline constexpr uint64_t kHashMesh = BH_COMPONENT_HASH(MeshComponent);
    inline constexpr uint64_t kHashLight = BH_COMPONENT_HASH(LightComponent);
    inline constexpr uint64_t kHashReflectionProbe = BH_COMPONENT_HASH(ReflectionProbeComponent);
    inline constexpr uint64_t kHashDecal = BH_COMPONENT_HASH(DecalComponent);

#undef BH_COMPONENT_HASH
}
