#pragma once
#include <flecs.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include "../Render/Light.hpp"
#include "../Render/Model.hpp"
#include "DirectXMathCompat.hpp"
#include "Utils.hpp"
#include "../Render/Material.hpp"

namespace burnhope
{
    /** MurmurHash3 64-bit entity GUID (editor may still use strings; runtime uses IDs only). */
    [[nodiscard]] inline uint64_t makeEntityId(std::string_view seed = {}) {
        if (seed.empty()) {
            static uint64_t counter = 1;
            return murmurHash3_64(std::span{reinterpret_cast<const std::byte*>(&counter), sizeof(counter)},
                                  counter++);
        }
        return hashString64(seed);
    }

    [[nodiscard]] inline uint64_t generateRandomID() { return makeEntityId(); }

    struct alignas(8) IDComponent {
        uint64_t ID{0};
        IDComponent() : ID(makeEntityId()) {}
        IDComponent(uint64_t id) : ID(id) {}
        IDComponent(std::string_view name) : ID(makeEntityId(name)) {}
    };

    class Transform
    {
    public:
        float3 position = float3{0.0f, 0.0f, 0.0f};
        float3 rotation = float3{0.0f, 0.0f, 0.0f};
        float3 scale = float3{1.0f, 1.0f, 1.0f};
        float4x4 matrix = MatrixIdentity();
        bool updatematrix = true;

        void updateMatrixIfNeeded()
        {
            if (updatematrix)
            {
                // Build transform matrix: T * R * S
                float4x4 trans = MatrixTranslation(position);
                float4x4 rotX = MatrixRotationX(Radians(rotation.x));
                float4x4 rotY = MatrixRotationY(Radians(rotation.y));
                float4x4 rotZ = MatrixRotationZ(Radians(rotation.z));
                float4x4 scl = MatrixScaling(scale);

                // Combine rotations: rotX * rotY * rotZ
                float4x4 rot = MatrixMultiply(rotX, rotY);
                rot = MatrixMultiply(rot, rotZ);

                // Combine all: trans * rot * scl
                float4x4 transform = MatrixMultiply(trans, rot);
                matrix = MatrixMultiply(transform, scl);

                updatematrix = false;
            }
        }
    };
    struct TagComponent
    {
        std::string name = "Entity";
        std::vector<std::string> tags;
        std::vector<std::string> layers;
        bool isPhantom = false;
    };
    struct TransformComponent
    {
        Transform transform;
    };
    struct MeshComponent
    {
        std::string modelPath = "";
        std::vector<std::string> materialPaths;
        std::shared_ptr<BurnhopeModel> model;
        std::vector<std::shared_ptr<Material>> materials;
        std::string skeletonPath = "";
        std::string animationPath = "";
        bool isStatic = false;
        bool isVisible = true;
        bool castShadow = true;
        float animationTime = 0.0f;
    };
    struct LightComponent
    {
        Light light;
        bool needsShadowUpdate = true;
    };
    struct HierarchyComponent
    {
        uint64_t parentID = 0;
        std::vector<uint64_t> childrenIDs;
    };

    struct ReflectionProbeComponent {
        float radius = 10.0f;
        int resolution = 256;
        bool updateNeeded = true;
        int textureIndex = -1; 
    };
    
    struct DecalComponent {
        std::string albedoPath = "";
        std::string normalPath = "";
        std::shared_ptr<BurnhopeTexture> albedoTex;
        std::shared_ptr<BurnhopeTexture> normalTex;
        float opacity = 1.0f;
        int albedoTexIdx = -1;
        int normalTexIdx = -1;
    };
    struct PortalComponent {
    flecs::entity targetPortal;

    static float4x4 getPortalTransform(const float4x4& srcMatrix, const float4x4& dstMatrix) {
        float4x4 flipY = MatrixRotationY(3.14159265f); // 180 degrees
        return MatrixMultiply(dstMatrix, MatrixMultiply(flipY, MatrixInverse(srcMatrix)));
    }
};
}