#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// 1. Главный заголовок файла модели
struct BHModelHeader {
    char magic[4] = { 'B', 'H', 'M', 'D' }; // BurnHope MoDel
    uint32_t version = 1;
    uint32_t meshCount = 0;
    uint32_t materialCount = 0;
    uint32_t reserved[4] = { 0 };
};

// 2. Заголовок для каждого саб-меша
struct BHMeshHeader {
    char name[64] = { 0 };       // Имя саб-меша
    uint32_t materialIndex = 0;  // Индекс материала в списке файла

    glm::vec3 aabbMin = glm::vec3(0.0f);
    glm::vec3 aabbMax = glm::vec3(0.0f);
    float boundingRadius = 0.0f;

    uint32_t lodCount = 0;
    uint32_t indexType = 0;      // 0 = 16-bit (uint16_t), 1 = 32-bit (uint32_t)
    uint32_t hasBones = 0;       // Задел под анимации
    uint32_t meshletCount = 0;   // Задел под Nanite-подобный рендер
    uint32_t reserved[4] = { 0 };
};

// 3. Заголовок для каждого LOD-а
struct BHLodHeader {
    uint32_t indexCount = 0;
    uint32_t reserved[3] = { 0 };
};

class ModelImporter {
public:
    // Главная функция: берет FBX и делает из него сжатый .bhmesh
    static bool ImportModel(const std::string& srcPath, const std::string& destPath, const std::string& projectRoot);

private:
    static uint32_t PackNormalTo10_10_10_2(glm::vec3 normal);
    static std::string ExtractMaterial(struct aiMaterial* mat, const std::string& directory, const std::string& projectRoot);
};