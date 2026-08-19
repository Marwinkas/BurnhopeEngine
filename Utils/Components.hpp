#pragma once
#include <flecs.h>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "MurmurHash3.hpp"
#include "../Render/Light.hpp"
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"

namespace burnhope
{
    using Entity = flecs::entity;
    using World = flecs::world;
    inline constexpr flecs::entity_t kNullEntity = 0;

    // 64-bit GUID generation, per engine rules: MurmurHash3-based identity,
    // no runtime paths/pointers used as entity handles.
    inline uint64_t generateRandomID()
    {
        return burnhope::hash::GenerateGUID();
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

        inline glm::mat4 composeLocal(const Position3 &position, const RotationEuler &rotation, const Scale3 &scale) noexcept
        {
            return glm::translate(glm::mat4(1.0f), asVec3(position))
                 * rotationMatrix(rotation)
                 * glm::scale(glm::mat4(1.0f), asVec3(scale));
        }

        inline glm::mat4 composeLocal(flecs::entity entity) noexcept
        {
            if (!hasBundle(entity)) return glm::mat4(1.0f);
            return composeLocal(*entity.get<Position3>(), *entity.get<RotationEuler>(), *entity.get<Scale3>());
        }

        inline flecs::entity findEntityById(flecs::entity any, uint64_t id)
        {
            if (id == 0 || !any.is_alive()) return flecs::entity();
            flecs::entity result;
            any.world().each<IDComponent>([&](flecs::entity e, IDComponent &idComp) {
                if (idComp.ID == id) result = e;
            });
            return result;
        }

        inline glm::vec3 eulerDegFromMatrix(const glm::mat4 &m) noexcept
        {
            glm::vec3 sx = glm::vec3(m[0]);
            glm::vec3 sy = glm::vec3(m[1]);
            glm::vec3 sz = glm::vec3(m[2]);
            float lx = glm::length(sx), ly = glm::length(sy), lz = glm::length(sz);
            glm::mat3 R(
                lx > 1e-8f ? sx / lx : glm::vec3(1, 0, 0),
                ly > 1e-8f ? sy / ly : glm::vec3(0, 1, 0),
                lz > 1e-8f ? sz / lz : glm::vec3(0, 0, 1));
            float x = glm::degrees(std::atan2(R[1][2], R[2][2]));
            float y = glm::degrees(std::atan2(-R[0][2], std::sqrt(R[1][2] * R[1][2] + R[2][2] * R[2][2])));
            float z = glm::degrees(std::atan2(R[0][1], R[0][0]));
            return {x, y, z};
        }

        struct TRS
        {
            glm::vec3 pos{0.0f};
            glm::vec3 euler{0.0f};
            glm::vec3 scale{1.0f};
        };

        inline TRS decompose(const glm::mat4 &m) noexcept
        {
            TRS t;
            t.pos = glm::vec3(m[3]);
            t.scale = {
                glm::length(glm::vec3(m[0])),
                glm::length(glm::vec3(m[1])),
                glm::length(glm::vec3(m[2]))
            };
            t.euler = eulerDegFromMatrix(m);
            return t;
        }

        inline void writeLocal(flecs::entity entity, const glm::vec3 &pos, const glm::vec3 &euler, const glm::vec3 &scale)
        {
            if (!hasBundle(entity)) return;
            asVec3Mut(*entity.get_mut<Position3>()) = pos;
            asVec3Mut(*entity.get_mut<RotationEuler>()) = euler;
            asVec3Mut(*entity.get_mut<Scale3>()) = scale;
            markDirty(*entity.get_mut<LocalMatrix>());
        }

        inline void writeLocal(flecs::entity entity, const TRS &t)
        {
            writeLocal(entity, t.pos, t.euler, t.scale);
        }

        inline uint64_t parentId(flecs::entity entity) noexcept
        {
            if (!entity.is_alive() || !entity.has<HierarchyComponent>()) return 0;
            return entity.get<HierarchyComponent>()->parentID;
        }

        inline glm::mat4 worldMatrix(flecs::entity entity, int depth = 0)
        {
            glm::mat4 local = composeLocal(entity);
            if (depth > 64 || !entity.is_alive()) return local;
            uint64_t pid = parentId(entity);
            if (pid == 0) return local;
            if (entity.has<IDComponent>() && entity.get<IDComponent>()->ID == pid) return local;
            flecs::entity parent = findEntityById(entity, pid);
            if (!parent.is_alive()) return local;
            return worldMatrix(parent, depth + 1) * local;
        }
    }
}
