#pragma once
#include "../Utils/Buffer.hpp"
#include "../Utils/Device.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include "Material.hpp"
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>
#include <atomic>
#include <thread>
namespace burnhope
{
    struct PackedVertexPos {
        uint16_t x, y, z, pad;
    };
    struct PackedVertexAttr {
        uint32_t texUV;
        uint32_t qTangent;
    };
    struct PackedVertexAnim {
        uint16_t pivotX, pivotY, pivotZ; // Pivot Painter Data (16-bit unorm)
        uint8_t clothMaxDistance;        // Max Distance Sphere for Cloth
        uint8_t vertexAO;                // Baked Vertex Ambient Occlusion / Cavity
        uint8_t localBoneIndices[4];     // Bone Palette local indices (0-7)
        uint8_t boneWeights[4];          // Normalized 0-255
    };
    class Vertex {
    public:
        static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
    };
    struct SubMesh
    {
        uint32_t lodCount;
        uint32_t indexCounts[4];
        uint32_t firstIndices[4];
        uint32_t materialIndex;
        float3 aabbMin = float3{0.0f, 0.0f, 0.0f};
        float3 aabbMax = float3{0.0f, 0.0f, 0.0f};
        float boundingRadius = 0.0f;
        uint32_t vrsRate = 0;
    };
    struct MaterialPaths
    {
        std::string albedo;
        std::string normal;
        std::string orm;
    };
    struct PhysicsPrimitive {
        uint32_t type;
        float3 position;
        float4 rotationQuat;
        float3 dimensions;
    };
    struct DestructionBond {
        uint32_t pieceA, pieceB;
        float breakForce;
    };
    struct BHSocket {
        char name[64];
        float4x4 transform;
    };
    struct BHNavMeshCarver {
        float2 points[4];
    };
    struct BHHairPoint {
        uint16_t x, y, z, pad;
    };
    struct BHHairStrand {
        float3 rootPosition;
        float thickness;
        uint32_t color; 
        uint32_t pointCount;
        uint32_t firstPoint;
        uint32_t pad;
    };
    struct BHMorphDelta {
        uint32_t vertexIndex;
        uint32_t packedDelta; 
    };
    struct BHMorphTarget {
        char name[64];
        uint32_t deltaCount;
        uint32_t firstDelta;
    };
    
    // Ключевой кадр анимации
    struct BHKeyframeCompressed {
        float time;
        uint32_t packedRotation; 
        uint16_t posX, posY, posZ; 
        uint16_t scaleX, scaleY, scaleZ;
    };

    struct BHRootMotionKey {
        float time;
        float3 deltaPosition;
        float4 deltaRotation;
    };

    struct Builder
    {
        std::vector<PackedVertexPos> positions{};
        std::vector<PackedVertexAttr> attributes{};
        std::vector<PackedVertexAnim> animations{};
        std::vector<uint32_t> indices{};
        std::vector<SubMesh> subMeshes{};
        std::vector<MaterialPaths> materialPaths;
        std::string modelDir;
        bool isDynamic = false;

        std::vector<uint32_t> colors;
        std::vector<uint32_t> uv2;
        std::vector<float> cdfs;
        std::vector<uint8_t> surfaceTags;
        std::vector<BHHairStrand> hairStrands;
        std::vector<BHHairPoint> hairPoints;
        std::vector<BHMorphTarget> morphTargets;
        std::vector<BHMorphDelta> morphDeltas;
        bool buildRT = true;
        float acousticAbsorption = 0.5f;
        float acousticReflection = 0.5f;
        uint32_t impostorOffset = 0;
        float maxWindSway = 1.0f;
        float3 centerOfMass = float3{0.0f, 0.0f, 0.0f};
        float totalMass = 0.0f;
        float3 centerOfBuoyancy = float3{0.0f, 0.0f, 0.0f};
        float volume = 0.0f;
        float4x4 inertiaTensor = MatrixIdentity();
        std::vector<PhysicsPrimitive> physicsPrimitives;
        std::vector<DestructionBond> destructionBonds;
        
        std::vector<float3> probeAnchors;
        std::vector<float4> tetraNodes; // xyz - pos, w - mass
        std::vector<ufloat4> tetrahedrons; // indices
        std::vector<BHSocket> sockets;
        std::vector<BHNavMeshCarver> navMeshCarvers;
        
        std::string vatTexturePath = "";
        uint32_t vatFrameCount = 0;
        float vatDuration = 0.0f;
        float3 vatMinBounds = float3{0.0f, 0.0f, 0.0f};
        float3 vatMaxBounds = float3{0.0f, 0.0f, 0.0f};
        float3 globalAabbMin = float3{0.0f, 0.0f, 0.0f};
        float3 globalAabbMax = float3{0.0f, 0.0f, 0.0f};

        void loadModel(const std::string &filepath);
    };
    class BurnhopeModel
    {
    public:
        BurnhopeModel(BurnhopeDevice &device);
        uint32_t getMaterialCount() const { return materialCount; }
        const std::vector<SubMesh> &getSubMeshes() const { return subMeshes; }
        std::vector<SubMesh> &getSubMeshesModifiable() { return subMeshes; }
        BurnhopeModel(BurnhopeDevice &device, const Builder &builder);
        ~BurnhopeModel();
        BurnhopeModel(const BurnhopeModel &) = delete;
        BurnhopeModel &operator=(const BurnhopeModel &) = delete;
        static std::unique_ptr<BurnhopeModel> createModelFromFile(
            BurnhopeDevice &device, const std::string &filepath);
        void finishGpuUpload();

        std::atomic<bool> cpuDataReady{false};
        std::atomic<bool> gpuDataReady{false};
        std::atomic<bool> blasBuildPending{false};
        std::unique_ptr<Builder> pendingBuilder;
        std::thread loadThread;
        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);
        void updateVertices(const std::vector<PackedVertexPos>& newPos, const std::vector<PackedVertexAttr>& newAttr, const std::vector<PackedVertexAnim>& newAnim = {});
        std::vector<PackedVertexPos> storedPositions;
        std::unique_ptr<BurnhopeBuffer> posBuffer;
        std::unique_ptr<BurnhopeBuffer> attrBuffer;
        std::unique_ptr<BurnhopeBuffer> animBuffer;
        uint32_t vertexCount = 0;
        [[nodiscard]] uint32_t getVertexCount() const { return vertexCount; }
        [[nodiscard]] VkBuffer getPosVkBuffer() const {
            return posBuffer ? posBuffer->getBuffer() : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkBuffer getAttrVkBuffer() const {
            return attrBuffer ? attrBuffer->getBuffer() : VK_NULL_HANDLE;
        }
        [[nodiscard]] VkBuffer getIndexVkBuffer() const {
            return indexBuffer ? indexBuffer->getBuffer() : VK_NULL_HANDLE;
        }
        bool hasIndexBuffer = false;
        std::unique_ptr<BurnhopeBuffer> indexBuffer;
        uint32_t indexCount = 0;
        std::vector<std::shared_ptr<Material>> materials;
        VkDeviceAddress getBLASAddress() const { return blasAddress; }
        float acousticAbsorption = 0.5f;
        float acousticReflection = 0.5f;
        uint32_t impostorOffset = 0;
        float maxWindSway = 1.0f;
        float3 centerOfMass = float3{0.0f, 0.0f, 0.0f};
        float totalMass = 0.0f;
        float3 centerOfBuoyancy = float3{0.0f, 0.0f, 0.0f};
        float volume = 0.0f;
        float4x4 inertiaTensor = MatrixIdentity();
        std::vector<PhysicsPrimitive> physicsPrimitives;
        std::vector<DestructionBond> destructionBonds;
        
        std::vector<float3> probeAnchors;
        std::vector<float4> tetraNodes;
        std::vector<UInt4> tetrahedrons;
        std::vector<BHSocket> sockets;
        std::vector<BHNavMeshCarver> navMeshCarvers;
        std::vector<BHMorphTarget> morphTargets;
        std::vector<BHMorphDelta> morphDeltas;

        float3 globalAabbMin = float3{0.0f, 0.0f, 0.0f};
        float3 globalAabbMax = float3{0.0f, 0.0f, 0.0f};
        std::unique_ptr<BurnhopeBuffer> colorBuffer;
        std::unique_ptr<BurnhopeBuffer> uv2Buffer;
        std::unique_ptr<BurnhopeBuffer> cdfBuffer;
        std::unique_ptr<BurnhopeBuffer> surfaceTagsBuffer;
        std::unique_ptr<BurnhopeBuffer> hairStrandsBuffer;
        std::unique_ptr<BurnhopeBuffer> hairPointsBuffer;
        
        std::string vatTexturePath = "";
        uint32_t vatFrameCount = 0;
        float vatDuration = 0.0f;
        float3 vatMinBounds = float3{0.0f, 0.0f, 0.0f};
        float3 vatMaxBounds = float3{0.0f, 0.0f, 0.0f};

        VkIndexType indexType = VK_INDEX_TYPE_UINT32;
        // Добавь эти поля в private или protected
VkDeviceAddress getPosBufferAddress() const { return posBufferAddress; }
VkDeviceAddress getAttrBufferAddress() const { return attrBufferAddress; }
VkDeviceAddress getAnimBufferAddress() const { return animBufferAddress; }
    VkDeviceAddress getIndexBufferAddress() const { return indexBufferAddress; }
VkDeviceAddress getColorBufferAddress() const { return colorBufferAddress; }
VkDeviceAddress getUV2BufferAddress() const { return uv2BufferAddress; }
VkDeviceAddress getCdfBufferAddress() const { return cdfBufferAddress; }
VkDeviceAddress getSurfaceTagsBufferAddress() const { return surfaceTagsBufferAddress; }
    // Добавь этот метод в public
    void createBLAS(const std::vector<PackedVertexPos>& cpuPositions);
    bool consumeBlasBuildPending();
    VkAccelerationStructureKHR getBLAS() const { return blasHandle; }
    std::unique_ptr<BurnhopeBuffer> rtPosBuffer;
    private:
        uint32_t materialCount = 0;
        std::vector<SubMesh> subMeshes;
        void createVertexBuffers(const std::vector<PackedVertexPos>& positions, const std::vector<PackedVertexAttr>& attributes, const std::vector<PackedVertexAnim>& animations, bool isDynamic);
        void createIndexBuffers(const std::vector<uint32_t> &indices);
        BurnhopeDevice &lveDevice;
        std::unique_ptr<BurnhopeBuffer> blasBuffer;
        VkAccelerationStructureKHR blasHandle = VK_NULL_HANDLE;
        VkDeviceAddress blasAddress = 0;
VkDeviceAddress posBufferAddress = 0;
VkDeviceAddress attrBufferAddress = 0;
VkDeviceAddress animBufferAddress = 0;
    VkDeviceAddress indexBufferAddress = 0;
VkDeviceAddress colorBufferAddress = 0;
VkDeviceAddress uv2BufferAddress = 0;
VkDeviceAddress cdfBufferAddress = 0;
VkDeviceAddress surfaceTagsBufferAddress = 0;
    };
}