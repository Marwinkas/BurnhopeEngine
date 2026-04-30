#include "../Header/ModelImporter.h"
#include "../Header/Components.h" // Для struct Vertex
#include <iostream>
#include <fstream>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/packing.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Наш упаковщик из прошлых уроков
uint32_t ModelImporter::PackNormalTo10_10_10_2(glm::vec3 normal) {
    if (glm::length(normal) < 0.0001f) normal = glm::vec3(0.0f, 1.0f, 0.0f);
    else normal = glm::normalize(normal);
    return glm::packSnorm3x10_1x2(glm::vec4(normal, 0.0f));
}

// Извлечение и авто-создание .bhmat (взято из твоего старого Model.cpp)
std::string ModelImporter::ExtractMaterial(aiMaterial* mat, const std::string& directory, const std::string& projectRoot) {
    if (!mat) return "";
    aiString matName;
    mat->Get(AI_MATKEY_NAME, matName);
    std::string safeMatName = matName.length > 0 ? matName.C_Str() : "DefaultMaterial";
    std::replace(safeMatName.begin(), safeMatName.end(), '/', '_');
    std::replace(safeMatName.begin(), safeMatName.end(), '\\', '_');
    std::replace(safeMatName.begin(), safeMatName.end(), ':', '_');

    std::string finalMatPath = directory + "/" + safeMatName + ".bhmat";

    if (!fs::exists(finalMatPath)) {
        json j;
        j["name"] = safeMatName;
        j["textures"] = json::object();

        auto getTex = [&](aiTextureType type, const char* key) {
            if (mat->GetTextureCount(type) > 0) {
                aiString str; mat->GetTexture(type, 0, &str);
                if (str.length > 0) {
                    std::string filename = fs::path(str.C_Str()).filename().string();
                    size_t lastDot = filename.find_last_of(".");
                    if (lastDot != std::string::npos) filename = filename.substr(0, lastDot) + ".bhtex"; // Ищем сжатую версию!

                    fs::path fullTexPath = fs::absolute(fs::path(directory) / filename);
                    j["textures"][key] = fs::relative(fullTexPath, fs::absolute(projectRoot)).generic_string();
                }
            }
            };

        getTex(aiTextureType_DIFFUSE, "albedo");
        getTex(aiTextureType_NORMALS, "normal");
        if (!j["textures"].contains("normal")) getTex(aiTextureType_HEIGHT, "normal");
        getTex(aiTextureType_METALNESS, "metallic");
        getTex(aiTextureType_DIFFUSE_ROUGHNESS, "roughness");

        std::ofstream fileOut(finalMatPath);
        if (fileOut.is_open()) { fileOut << j.dump(4); fileOut.close(); }
    }
    return fs::relative(fs::absolute(finalMatPath), fs::absolute(projectRoot)).generic_string();
}

bool ModelImporter::ImportModel(const std::string& srcPath,
    const std::string& destPath,
    const std::string& projectRoot)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(srcPath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |   // жёсткие грани если нет нормалей
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "[ERROR] Assimp: " << importer.GetErrorString() << std::endl;
        return false;
    }

    std::string directory = srcPath.substr(0, srcPath.find_last_of("/\\"));

    std::ofstream outFile(destPath, std::ios::binary);
    if (!outFile.is_open()) return false;

    // Материалы
    std::vector<std::string> materialPaths;
    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
        materialPaths.push_back(ExtractMaterial(scene->mMaterials[i], directory, projectRoot));

    BHModelHeader modelHeader;
    modelHeader.meshCount = scene->mNumMeshes;
    modelHeader.materialCount = (uint32_t)materialPaths.size();
    outFile.write(reinterpret_cast<const char*>(&modelHeader), sizeof(BHModelHeader));

    for (const std::string& matPath : materialPaths) {
        char pathBuf[256] = { 0 };
        strcpy_s(pathBuf, sizeof(pathBuf), matPath.c_str());
        outFile.write(pathBuf, sizeof(pathBuf));
    }

    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* aimesh = scene->mMeshes[m];
        std::cout << "[Mesh] " << aimesh->mName.C_Str()
            << " Verts: " << aimesh->mNumVertices << std::endl;

        std::vector<Vertex>   vertices(aimesh->mNumVertices);
        std::vector<uint32_t> indices;

        glm::vec3 aabbMin(1e9f);
        glm::vec3 aabbMax(-1e9f);

        for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
            Vertex& v = vertices[i];

            // Позиция
            v.position = { aimesh->mVertices[i].x,
                           aimesh->mVertices[i].y,
                           aimesh->mVertices[i].z };
            aabbMin = glm::min(aabbMin, v.position);
            aabbMax = glm::max(aabbMax, v.position);

            // Нормаль
            if (aimesh->HasNormals())
                v.normal = glm::normalize(glm::vec3(
                    aimesh->mNormals[i].x,
                    aimesh->mNormals[i].y,
                    aimesh->mNormals[i].z));
            else
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);

            // UV
            if (aimesh->HasTextureCoords(0))
                v.texUV = { aimesh->mTextureCoords[0][i].x,
                            aimesh->mTextureCoords[0][i].y };
            else
                v.texUV = { 0.0f, 0.0f };

            // Тангенс / битангенс
            if (aimesh->HasTangentsAndBitangents()) {
                v.tangent = glm::normalize(glm::vec3(
                    aimesh->mTangents[i].x,
                    aimesh->mTangents[i].y,
                    aimesh->mTangents[i].z));
                v.bitangent = glm::normalize(glm::vec3(
                    aimesh->mBitangents[i].x,
                    aimesh->mBitangents[i].y,
                    aimesh->mBitangents[i].z));
            }
            else {
                // Математический фолбэк если нет UV-развёртки
                glm::vec3 up = std::abs(v.normal.y) < 0.999f
                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                    : glm::vec3(1.0f, 0.0f, 0.0f);
                v.tangent = glm::normalize(glm::cross(up, v.normal));
                v.bitangent = glm::cross(v.normal, v.tangent);
            }
        }

        for (unsigned int i = 0; i < aimesh->mNumFaces; i++)
            for (unsigned int j = 0; j < aimesh->mFaces[i].mNumIndices; j++)
                indices.push_back(aimesh->mFaces[i].mIndices[j]);

        for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
            if (aimesh->HasNormals()) {
                auto& n = aimesh->mNormals[i];
                // Для верхней грани куба должно быть Y близко к 1.0
                if (n.y > 0.9f) {
                    std::cout << "TOP normal: "
                        << n.x << " " << n.y << " " << n.z << std::endl;
                    break;
                }
            }
        }
        // Оптимизация буферов
        meshopt_optimizeVertexCache(indices.data(), indices.data(),
            indices.size(), vertices.size());
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(),
            &vertices[0].position.x, vertices.size(),
            sizeof(Vertex), 1.05f);
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(),
            indices.size(), vertices.data(),
            vertices.size(), sizeof(Vertex));

        // LOD
        std::vector<std::vector<uint32_t>> lods;
        lods.push_back(indices);
        if (!vertices.empty() && indices.size() >= 300) {
            float thresholds[2] = { 0.5f, 0.2f };
            for (int i = 0; i < 2; i++) {
                size_t target = size_t(lods[0].size() * thresholds[i]);
                if (target < 30) break;
                std::vector<uint32_t> lodIdx(lods[0].size());
                size_t newCount = meshopt_simplify(
                    lodIdx.data(), lods[0].data(), lods[0].size(),
                    &vertices[0].position.x, vertices.size(),
                    sizeof(Vertex), target, 0.05f);
                if (newCount == 0) break;
                lodIdx.resize(newCount);
                if (newCount < lods.back().size()) {
                    meshopt_optimizeVertexCache(lodIdx.data(), lodIdx.data(),
                        lodIdx.size(), vertices.size());
                    lods.push_back(lodIdx);
                }
                else break;
            }
        }

        BHMeshHeader meshHeader{};
        strcpy_s(meshHeader.name, sizeof(meshHeader.name),
            aimesh->mName.length > 0 ? aimesh->mName.C_Str() : "Mesh");
        meshHeader.materialIndex = aimesh->mMaterialIndex;
        meshHeader.aabbMin = aabbMin;
        meshHeader.aabbMax = aabbMax;
        meshHeader.boundingRadius = glm::distance(aabbMin, aabbMax) * 0.5f;
        meshHeader.lodCount = (uint32_t)lods.size();
        meshHeader.indexType = 1; // uint32

        outFile.write(reinterpret_cast<const char*>(&meshHeader), sizeof(BHMeshHeader));

        uint32_t vCount = (uint32_t)vertices.size();
        outFile.write(reinterpret_cast<const char*>(&vCount), sizeof(uint32_t));
        outFile.write(reinterpret_cast<const char*>(vertices.data()),
            vCount * sizeof(Vertex));

        for (const auto& lodIndices : lods) {
            BHLodHeader lodHeader{};
            lodHeader.indexCount = (uint32_t)lodIndices.size();
            outFile.write(reinterpret_cast<const char*>(&lodHeader), sizeof(BHLodHeader));
            outFile.write(reinterpret_cast<const char*>(lodIndices.data()),
                lodIndices.size() * sizeof(uint32_t));
        }
    }

    outFile.close();
    std::cout << "[SUCCESS] .bhmesh saved: " << destPath << std::endl;
    return true;
}