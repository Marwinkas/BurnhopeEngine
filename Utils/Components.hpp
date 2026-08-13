#pragma once
#include <flecs.h>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../Render/Light.hpp"
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"

namespace burnhope
{
    using Entity = flecs::entity;
    using World = flecs::world;
    inline constexpr flecs::entity_t kNullEntity = 0;

    inline uint64_t generateRandomID()
    {
        static std::random_device rd;
        static std::mt19937_64 eng(rd());
        static std::uniform_int_distribution<uint64_t> dist(1);
        return dist(eng);
    }

    struct alignas(16) Position3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct alignas(16) RotationEuler
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct alignas(16) Scale3
    {
        float x = 1.0f;
        float y = 1.0f;
        float z = 1.0f;
    };

    struct alignas(64) LocalMatrix
    {
        glm::mat4 value = glm::mat4(1.0f);
        uint8_t dirty = 1;
        uint8_t _pad[3]{};
    };

    struct alignas(64) TransformHistory
    {
        glm::mat4 current = glm::mat4(1.0f);
        glm::mat4 previous = glm::mat4(1.0f);
    };

    struct Static {};
    struct Active {};
    struct Visible {};
    struct TransformChanged {};

    struct IDComponent
    {
        uint64_t ID;

        IDComponent() : ID(generateRandomID()) {}
        explicit IDComponent(uint64_t id) : ID(id) {}
    };

    struct TagComponent
    {
        std::string name = "Entity";
        std::vector<std::string> tags;
        std::vector<std::string> layers;
        bool isPhantom = false;
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

    struct ReflectionProbeComponent
    {
        float radius = 10.0f;
        int resolution = 256;
        bool updateNeeded = true;
        int textureIndex = -1;
    };

    struct DecalComponent
    {
        std::string albedoPath = "";
        std::string normalPath = "";
        std::shared_ptr<BurnhopeTexture> albedoTex;
        std::shared_ptr<BurnhopeTexture> normalTex;
        float opacity = 1.0f;
        int albedoTexIdx = -1;
        int normalTexIdx = -1;
    };

    struct PortalComponent
    {
        flecs::entity targetPortal{};

        static glm::mat4 getPortalTransform(const glm::mat4 &srcMatrix, const glm::mat4 &dstMatrix)
        {
            glm::mat4 flipY = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0, 1, 0));
            return dstMatrix * flipY * glm::inverse(srcMatrix);
        }
    };

    namespace transform
    {
        inline glm::vec3 asVec3(const Position3 &p) noexcept { return {p.x, p.y, p.z}; }
        inline glm::vec3 asVec3(const RotationEuler &r) noexcept { return {r.x, r.y, r.z}; }
        inline glm::vec3 asVec3(const Scale3 &s) noexcept { return {s.x, s.y, s.z}; }

        inline glm::vec3 &asVec3Mut(Position3 &p) noexcept { return *reinterpret_cast<glm::vec3 *>(&p.x); }
        inline glm::vec3 &asVec3Mut(RotationEuler &r) noexcept { return *reinterpret_cast<glm::vec3 *>(&r.x); }
        inline glm::vec3 &asVec3Mut(Scale3 &s) noexcept { return *reinterpret_cast<glm::vec3 *>(&s.x); }

        inline glm::mat4 rotationMatrix(const RotationEuler &rotation) noexcept
        {
            glm::mat4 rot = glm::mat4(1.0f);
            rot = glm::rotate(rot, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            return rot;
        }

        inline glm::vec3 rotateVector(const RotationEuler &rotation, glm::vec3 localDir) noexcept
        {
            return glm::normalize(glm::vec3(rotationMatrix(rotation) * glm::vec4(localDir, 0.0f)));
        }

        inline void updateMatrixIfNeeded(Position3 &position, RotationEuler &rotation, Scale3 &scale, LocalMatrix &matrix) noexcept
        {
            if (!matrix.dirty)
            {
                return;
            }
            matrix.value = glm::translate(glm::mat4(1.0f), asVec3(position))
                         * rotationMatrix(rotation)
                         * glm::scale(glm::mat4(1.0f), asVec3(scale));
            matrix.dirty = 0;
        }

        inline void markDirty(LocalMatrix &matrix) noexcept { matrix.dirty = 1; }

        inline void addBundle(flecs::entity entity)
        {
            entity.set<Position3>({});
            entity.set<RotationEuler>({});
            entity.set<Scale3>({});
            entity.set<LocalMatrix>({});
        }

        inline void copyBundle(flecs::entity source, flecs::entity destination)
        {
            if (const Position3 *p = source.get<Position3>())
            {
                destination.set<Position3>(*p);
            }
            if (const RotationEuler *r = source.get<RotationEuler>())
            {
                destination.set<RotationEuler>(*r);
            }
            if (const Scale3 *s = source.get<Scale3>())
            {
                destination.set<Scale3>(*s);
            }
            if (const LocalMatrix *m = source.get<LocalMatrix>())
            {
                destination.set<LocalMatrix>(*m);
            }
        }

        inline bool hasBundle(flecs::entity entity) noexcept
        {
            return entity.has<Position3>() && entity.has<RotationEuler>() && entity.has<Scale3>() && entity.has<LocalMatrix>();
        }

        inline void updateMatrixIfNeeded(flecs::entity entity) noexcept
        {
            Position3 *position = entity.get_mut<Position3>();
            RotationEuler *rotation = entity.get_mut<RotationEuler>();
            Scale3 *scale = entity.get_mut<Scale3>();
            LocalMatrix *matrix = entity.get_mut<LocalMatrix>();
            if (position && rotation && scale && matrix)
            {
                updateMatrixIfNeeded(*position, *rotation, *scale, *matrix);
            }
        }
    }
}
