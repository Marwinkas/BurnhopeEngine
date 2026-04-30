#include "../Header/Model.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include "../Header/ModelImporter.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
std::map<std::string, std::vector<Mesh>> Model::globalMeshCache;
std::map<std::string, std::vector<std::string>> Model::globalMaterialCache;
Model::Model(std::string const& path, std::string const& root)
{
    this->projectRoot = root; // Запоминаем ПЕРЕД загрузкой
    loadModel(path);
}

void Model::loadModel(std::string const& path)
{
    if (!path.ends_with(".bhmesh")) {
        std::cerr << "[ERROR] Engine now only supports .bhmesh files! Please convert: " << path << std::endl;
        return;
    }

    directory = path.substr(0, path.find_last_of("/\\"));

    // ===============================================================
    // 1. СУПЕР-ОПТИМИЗАЦИЯ: ПРОВЕРКА КЭША
    // Если модель уже грузили, берем данные из RAM/VRAM и выходим!
    // ===============================================================
    if (globalMeshCache.find(path) != globalMeshCache.end()) {
        this->meshes = globalMeshCache[path];
        this->loadedMaterialPaths = globalMaterialCache[path];
        std::cout << "[CACHE] Instanced .bhmesh loaded from memory: " << path << std::endl;
        return;
    }

    // ===============================================================
    // 2. ЕСЛИ В КЭШЕ НЕТ, ЧИТАЕМ С ДИСКА (Твой оригинальный код)
    // ===============================================================
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Failed to open mesh: " << path << std::endl;
        return;
    }

    BHModelHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(BHModelHeader));

    loadedMaterialPaths.clear();
    meshes.clear();

    // Читаем пути к материалам
    for (uint32_t i = 0; i < header.materialCount; ++i) {
        char pathBuf[256];
        file.read(pathBuf, sizeof(pathBuf));
        loadedMaterialPaths.push_back(std::string(pathBuf));
    }

    // Читаем все меши
    for (uint32_t m = 0; m < header.meshCount; ++m) {
        BHMeshHeader meshHeader;
        file.read(reinterpret_cast<char*>(&meshHeader), sizeof(BHMeshHeader));

        uint32_t vCount;
        file.read(reinterpret_cast<char*>(&vCount), sizeof(uint32_t));

        std::vector<Vertex> vertices(vCount);
        file.read(reinterpret_cast<char*>(vertices.data()), vCount * sizeof(Vertex));

        std::vector<LODLevel> lodLevels;
        std::vector<GLuint> allIndices;

        for (uint32_t l = 0; l < meshHeader.lodCount; ++l) {
            BHLodHeader lodHeader;
            file.read(reinterpret_cast<char*>(&lodHeader), sizeof(BHLodHeader));

            uint32_t offset = allIndices.size();

            if (meshHeader.indexType == 0) {
                std::vector<uint16_t> idx16(lodHeader.indexCount);
                file.read(reinterpret_cast<char*>(idx16.data()), lodHeader.indexCount * sizeof(uint16_t));
                for (uint16_t idx : idx16) allIndices.push_back((GLuint)idx);
            }
            else {
                std::vector<uint32_t> idx32(lodHeader.indexCount);
                file.read(reinterpret_cast<char*>(idx32.data()), lodHeader.indexCount * sizeof(uint32_t));
                for (uint32_t idx : idx32) allIndices.push_back(idx);
            }

            lodLevels.push_back({ lodHeader.indexCount, offset });
        }

        Mesh newMesh(vertices, allIndices);
        newMesh.name = meshHeader.name;
        newMesh.lods = lodLevels;
        newMesh.boundingRadius = meshHeader.boundingRadius;

        meshes.push_back(newMesh);
    }

    file.close();

    // ===============================================================
    // 3. СОХРАНЯЕМ В КЭШ ДЛЯ СЛЕДУЮЩИХ КОПИЙ
    // ===============================================================
    globalMeshCache[path] = this->meshes;
    globalMaterialCache[path] = this->loadedMaterialPaths;

    std::cout << "[SUCCESS] Loaded new .bhmesh to VRAM: " << path << std::endl;
}


std::string Model::loadMaterialTexture(aiMaterial* mat, aiTextureType type, std::string typeName)
{
    if (!mat) return "";

    if (mat->GetTextureCount(type) > 0)
    {
        aiString str;
        mat->GetTexture(type, 0, &str);
        if (str.length == 0) return "";

        fs::path p(str.C_Str());
        std::string filename = p.filename().string();

        // ==========================================
        // МАГИЯ: АВТО-ПОДМЕНА РАСШИРЕНИЯ НА .bhtex
        // ==========================================
        size_t lastDot = filename.find_last_of(".");
        if (lastDot != std::string::npos) {
            // Отрезаем старое расширение (.png/.jpg) и приклеиваем наше!
            filename = filename.substr(0, lastDot) + ".bhtex";
        }

        // Теперь движок будет искать именно скомпилированный файл!
        fs::path fullTexPath = fs::absolute(fs::path(directory) / filename);
        if (fs::exists(fullTexPath)) {
            if (!projectRoot.empty()) {
                return fs::relative(fullTexPath, fs::absolute(projectRoot)).generic_string();
            }
            else {
                return (fs::path(directory) / filename).generic_string();
            }
        }
        else {
            std::cout << "[WARNING] Texture not found: " << fullTexPath << std::endl;
        }
    }
    return "";
}

