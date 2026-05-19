#pragma once
#include <string>
#include <vector>
#include "Model.hpp"
namespace burnhope
{
    static constexpr uint32_t CLUSTER_SPLIT_THRESHOLD = 2048;
    static constexpr uint32_t CLUSTER_MAX_TRIANGLES = 126; // Meshlet standard
    static constexpr uint32_t CLUSTER_MAX_VERTICES = 64;   // Meshlet standard
    struct BHChunkHeader
    {
        uint32_t chunkID; // 0 = Hot (Headers, Proxies), 1 = Cold (Geometry)
        uint32_t compressedSize;
        uint64_t offset;
        uint64_t uncompressedSize;
    };
    struct BHModelHeader
    {
        char magic[4] = {'B', 'H', 'M', 'D'};
        uint32_t version = 12; // V12: SIMD SoA Culling, Morph Targets, Animated Culling (Dominant Bone)
        uint32_t compressionType = 0; // 0 = None, 1 = GDeflate, 2 = Kraken
        uint32_t subMeshCount = 0;
        uint32_t materialCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;
        uint32_t totalMeshletCount = 0;
        uint32_t meshletVerticesCount = 0;
        uint32_t meshletTrianglesCount = 0;
        float3 globalAabbMin = float3{0.0f, 0.0f, 0.0f};
        float3 globalAabbMax = float3{0.0f, 0.0f, 0.0f};
        uint32_t proxyVertexCount = 0;
        uint32_t proxyIndexCount = 0;
        uint32_t chunkCount = 0;
        float acousticAbsorption = 0.5f;
        float acousticReflection = 0.5f;
        uint32_t impostorOffset = 0;
        float maxWindSway = 1.0f;
        
        // Jolt Physics Metadata
        float3 centerOfMass = float3{0.0f, 0.0f, 0.0f};
        float totalMass = 0.0f;
        
        float3 centerOfBuoyancy = float3{0.0f, 0.0f, 0.0f};
        float volume = 0.0f;
        
        float4 inertiaTensorRow0 = float4{1.0f, 0.0f, 0.0f, 0.0f};
        float4 inertiaTensorRow1 = float4{0.0f, 1.0f, 0.0f, 0.0f};
        float4 inertiaTensorRow2 = float4{0.0f, 0.0f, 1.0f, 0.0f};
        
        uint32_t physicsPrimitiveCount = 0;
        uint32_t destructionBondCount = 0;
        
        uint32_t probeAnchorCount = 0;
        uint32_t tetraNodeCount = 0;
        uint32_t tetraCount = 0;
        
        // VAT Metadata
        char vatTexturePath[128] = {0};
        uint32_t vatFrameCount = 0;
        float vatDuration = 0.0f;
        float3 vatMinBounds = float3{0.0f, 0.0f, 0.0f};
        float3 vatMaxBounds = float3{0.0f, 0.0f, 0.0f};
        
        uint32_t socketCount = 0;
        uint32_t carverCount = 0;

        uint32_t colorCount = 0;
        uint32_t uv2Count = 0;
        uint32_t surfaceTagCount = 0;
        uint32_t cdfCount = 0;
        uint32_t hairStrandCount = 0;
        uint32_t hairPointCount = 0;
        uint32_t meshletErrorBoundsCount = 0; // Для бесшовных LOD'ов

        uint32_t morphTargetCount = 0;
        uint32_t morphDeltaCount = 0;
        uint32_t hasOpacityMicromaps = 0;
        uint32_t hasDisplacementMicromaps = 0;
    };
    struct BHMaterialData
    {
        char albedoPath[128];
        char normalPath[128];
        char ormPath[128];
    };
    struct BHMeshHeader
    {
        char name[64] = {0};
        uint32_t materialIndex = 0;
        float3 aabbMin = float3{0.0f, 0.0f, 0.0f};
        float3 aabbMax = float3{0.0f, 0.0f, 0.0f};
        float boundingRadius = 0.0f;
        uint32_t lodCount = 0;
        uint32_t totalIndexCount = 0;
        uint32_t vrsRate = 0; // 0 = 1x1 (Native), 1 = 2x2 (Coarse)
        uint32_t reserved[5] = {0};
    };
    struct BHLodHeader
    {
        uint32_t indexCount = 0;
        uint32_t reserved[3] = {0};
    };
    struct BHPhysicsPrimitive {
        uint32_t type; // 0 = Sphere, 1 = Box, 2 = Capsule
        float3 position;
        float4 rotationQuat;
        float3 dimensions; // radius, halfExtents
    };
    struct BHDestructionBond {
        uint32_t pieceA, pieceB;
        float breakForce;
    };

    // --- СТРУКТУРЫ ДЛЯ .BHBONE ---
    struct BHBoneHeader {
        char magic[4] = {'B', 'H', 'B', 'N'};
        uint32_t version = 2; // V2: Flat Array, Hash Names, LODs, Mirroring
        uint32_t boneCount = 0;
        uint32_t virtualIkCount = 0;
    };

    struct BHBoneNode {
        uint64_t nameHash;       // 8 байт: Murmur/FNV хэш имени (строки удалены)
        int32_t parentIndex = -1;
        int32_t mirrorBoneIndex = -1; // Для отзеркаливания анимаций
        uint8_t lodLevel = 0;         // 0 = Всегда, 1 = Близко, 2 = Только LOD0
        uint8_t isTwistBone = 0;      // 1 = Процедурная кость скручивания
        int16_t twistTarget = -1;     // Индекс кости, от которой берем вращение
        float twistWeight = 0.5f;
        float4x4 inverseBindMatrix;
        float4x4 localTransform;
    };

    struct BHVirtualIK {
        uint64_t nameHash;
        int32_t sourceBoneIndex;
        float3 localOffset;
    };

    // --- СТРУКТУРЫ ДЛЯ .BHANIM ---
    struct BHAnimHeader {
        char magic[4] = {'B', 'H', 'A', 'N'};
        uint32_t version = 2; // V2: Smallest-Three compression, Root Motion, Events
        char name[64];
        float duration = 0.0f;
        float ticksPerSecond = 0.0f;
        uint32_t trackCount = 0; // Количество костей с анимацией
        uint32_t totalKeyframeCount = 0;
        uint32_t rootMotionKeyCount = 0;
        uint32_t trajectoryCount = 0;
        uint32_t notifyCount = 0;
        uint32_t curveCount = 0;
        uint8_t isAdditive = 0;
        uint8_t reserved[3] = {0};
    };

    struct BHAnimTrack {
        uint64_t boneNameHash;
        uint32_t keyframeCount;
        uint32_t firstKeyframe;
        float3 posMin; // Для 16-битного декодирования
        float3 posMax;
    };



    struct BHTrajectoryData {
        float time;
        float3 velocity;
        float facingAngle;
    };

    struct BHAnimNotify {
        uint64_t eventHash;
        float time;
    };

    struct BHFloatCurve {
        uint64_t nameHash;
        uint32_t keyCount;
        // Далее в файле пойдут массивы {float time, float value}
    };

    class ModelImporter
    {
    public:
        static bool ImportModel(const std::string &srcPath, const std::string &destPath);
    };
}