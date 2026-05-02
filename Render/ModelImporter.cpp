#include "ModelImporter.h"
#include "Model.hpp" // Подключаем, чтобы взять оттуда BurnhopeModel::Vertex
#include <iostream>
#include <fstream>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>
#include <cstring>
namespace fs = std::filesystem;
namespace burnhope {
    bool ModelImporter::ImportModel(const std::string& srcPath, const std::string& destPath)
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

        std::ofstream outFile(destPath, std::ios::binary);
        if (!outFile.is_open()) return false;

        BHModelHeader modelHeader;
        modelHeader.meshCount = scene->mNumMeshes;
        modelHeader.materialCount = 0; // БОЛЬШЕ НИКАКИХ ПУТЕЙ В ФАЙЛЕ!
        outFile.write(reinterpret_cast<const char*>(&modelHeader), sizeof(BHModelHeader));

        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh* aimesh = scene->mMeshes[m];
            std::cout << "[Mesh] " << aimesh->mName.C_Str() << " Verts: " << aimesh->mNumVertices << std::endl;

            std::vector<Vertex>   vertices(aimesh->mNumVertices);
            std::vector<uint32_t> indices;

            glm::vec3 aabbMin(1e9f);
            glm::vec3 aabbMax(-1e9f);

            for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
                Vertex& v = vertices[i];

                // Позиция
                v.position = { aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z };
                aabbMin = glm::min(aabbMin, v.position);
                aabbMax = glm::max(aabbMax, v.position);

                // Нормаль
                if (aimesh->HasNormals())
                    v.normal = glm::normalize(glm::vec3(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z));
                else
                    v.normal = glm::vec3(0.0f, 1.0f, 0.0f);

                // UV
                if (aimesh->HasTextureCoords(0))
                    v.texUV = { aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y };
                else
                    v.texUV = { 0.0f, 0.0f };

                // Тангенс / битангенс
                if (aimesh->HasTangentsAndBitangents()) {
                    v.tangent = glm::normalize(glm::vec3(aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z));
                    v.bitangent = glm::normalize(glm::vec3(aimesh->mBitangents[i].x, aimesh->mBitangents[i].y, aimesh->mBitangents[i].z));
                }
                else {
                    glm::vec3 up = std::abs(v.normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                    v.tangent = glm::normalize(glm::cross(up, v.normal));
                    v.bitangent = glm::cross(v.normal, v.tangent);
                }
            }

            for (unsigned int i = 0; i < aimesh->mNumFaces; i++)
                for (unsigned int j = 0; j < aimesh->mFaces[i].mNumIndices; j++)
                    indices.push_back(aimesh->mFaces[i].mIndices[j]);

            // Оптимизация буферов (Meshoptimizer)
            meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
            meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), 1.05f);
            meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));

            // LOD
            std::vector<std::vector<uint32_t>> lods;
            lods.push_back(indices);
            if (!vertices.empty() && indices.size() >= 300) {
                float thresholds[2] = { 0.5f, 0.2f };
                for (int i = 0; i < 2; i++) {
                    size_t target = size_t(lods[0].size() * thresholds[i]);
                    if (target < 30) break;
                    std::vector<uint32_t> lodIdx(lods[0].size());
                    size_t newCount = meshopt_simplify(lodIdx.data(), lods[0].data(), lods[0].size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), target, 0.05f);
                    if (newCount == 0) break;
                    lodIdx.resize(newCount);
                    if (newCount < lods.back().size()) {
                        meshopt_optimizeVertexCache(lodIdx.data(), lodIdx.data(), lodIdx.size(), vertices.size());
                        lods.push_back(lodIdx);
                    }
                    else break;
                }
            }

            BHMeshHeader meshHeader{};
            strncpy(meshHeader.name, aimesh->mName.length > 0 ? aimesh->mName.C_Str() : "Mesh", sizeof(meshHeader.name) - 1);
            meshHeader.name[sizeof(meshHeader.name) - 1] = '\0';
            meshHeader.materialIndex = 0; // Больше не нужно
            meshHeader.aabbMin = aabbMin;
            meshHeader.aabbMax = aabbMax;
            meshHeader.boundingRadius = glm::distance(aabbMin, aabbMax) * 0.5f;
            meshHeader.lodCount = (uint32_t)lods.size();
            meshHeader.indexType = 1; // uint32

            outFile.write(reinterpret_cast<const char*>(&meshHeader), sizeof(BHMeshHeader));

            uint32_t vCount = (uint32_t)vertices.size();
            outFile.write(reinterpret_cast<const char*>(&vCount), sizeof(uint32_t));
            outFile.write(reinterpret_cast<const char*>(vertices.data()), vCount * sizeof(Vertex));

            for (const auto& lodIndices : lods) {
                BHLodHeader lodHeader{};
                lodHeader.indexCount = (uint32_t)lodIndices.size();
                outFile.write(reinterpret_cast<const char*>(&lodHeader), sizeof(BHLodHeader));
                outFile.write(reinterpret_cast<const char*>(lodIndices.data()), lodIndices.size() * sizeof(uint32_t));
            }
        }

        outFile.close();
        std::cout << "[SUCCESS] .bhmesh saved: " << destPath << std::endl;
        return true;
    }
}