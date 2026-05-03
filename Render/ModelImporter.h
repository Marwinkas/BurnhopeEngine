#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Model.hpp"
// 1. Главный заголовок файла модели
namespace burnhope {
    // ModelImporter.h — добавляем константу
    static constexpr uint32_t CLUSTER_SPLIT_THRESHOLD = 1382236160; // было 10000
    static constexpr uint32_t CLUSTER_MAX_TRIANGLES   = 128;
    struct BHModelHeader {
        char magic[4] = { 'B', 'H', 'M', 'D' }; // BurnHope MoDel
        uint32_t version = 1;
        uint32_t meshCount = 0;
        uint32_t materialCount = 0; // Теперь всегда будет 0
        uint32_t reserved[4] = { 0 };
    };
    struct BHMaterialData {
        char albedoPath[128];
        char normalPath[128];
        char ormPath[128]; // Occlusion, Roughness, Metallic (В glTF это обычно одна текстура)
    };
    // 2. Заголовок для каждого саб-меша
    struct BHMeshHeader {
        char name[64] = { 0 };
        uint32_t materialIndex = 0;

        glm::vec3 aabbMin = glm::vec3(0.0f);
        glm::vec3 aabbMax = glm::vec3(0.0f);
        float boundingRadius = 0.0f;

        uint32_t lodCount = 0;
        uint32_t indexType = 0;
        uint32_t hasBones = 0;
        uint32_t meshletCount = 0;
        uint32_t reserved[4] = { 0 };
    };

    // 3. Заголовок для каждого LOD-а
    struct BHLodHeader {
        uint32_t indexCount = 0;
        uint32_t reserved[3] = { 0 };
    };

    class ModelImporter {
    public:
        // Теперь нам нужен только путь к исходнику и путь сохранения
        static bool ImportModel(const std::string& srcPath, const std::string& destPath);
        static void writeMeshWithLODs(                   // ← добавить объявление
            std::ofstream& outFile,
            const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            aiMesh* aimesh,
            glm::vec3 aabbMin, glm::vec3 aabbMax);

        static void writeMeshAsCluster(
            std::ofstream& outFile,
            const std::vector<Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            aiMesh* aimesh,
            glm::vec3 aabbMin, glm::vec3 aabbMax,
            uint32_t& meshCountOut);
    };
}