#include "ModelImporter.h"
#include "Model.hpp" // Подключаем, чтобы взять оттуда BurnhopeModel::Vertex

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
modelHeader.meshCount     = 0;
modelHeader.materialCount = scene->mNumMaterials; // ВОЗВРАЩАЕМ МАТЕРИАЛЫ!
outFile.write(reinterpret_cast<const char*>(&modelHeader), sizeof(BHModelHeader));

// ЧИТАЕМ И ПИШЕМ МАТЕРИАЛЫ СРАЗУ ПОСЛЕ ЗАГОЛОВКА
for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
    aiMaterial* mat = scene->mMaterials[i];
    BHMaterialData matData{};
    memset(&matData, 0, sizeof(BHMaterialData));

    aiString path;
    
    // 1. Albedo (Цвет)
    aiString albedoPath, normalPath, ormPath;

    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &albedoPath) == AI_SUCCESS) {
        strncpy(matData.albedoPath, albedoPath.C_Str(), sizeof(matData.albedoPath) - 1);
    } else if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &albedoPath) == AI_SUCCESS) {
        strncpy(matData.albedoPath, albedoPath.C_Str(), sizeof(matData.albedoPath) - 1);
    }

    // Normal
    if (mat->GetTexture(aiTextureType_NORMALS, 0, &normalPath) == AI_SUCCESS) {
        strncpy(matData.normalPath, normalPath.C_Str(), sizeof(matData.normalPath) - 1);
    } else if (mat->GetTexture(aiTextureType_HEIGHT, 0, &normalPath) == AI_SUCCESS) {
        strncpy(matData.normalPath, normalPath.C_Str(), sizeof(matData.normalPath) - 1);
    }

    // ORM — для glTF это всегда UNKNOWN slot 0
    // Metalness и Roughness в glTF ВСЕГДА одна текстура, не склеиваем!
    // ORM для glTF — Roughness и Metalness это ОДНА текстура (metallicRoughnessTexture)
// Assimp кладёт её в UNKNOWN slot 0
    if (mat->GetTexture(aiTextureType_UNKNOWN, 0, &path) == AI_SUCCESS) {
        strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
    }
    // Для НЕ-glTF форматов (OBJ, FBX) — ищем отдельно, но берём ТОЛЬКО ОДИН путь
    else if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path) == AI_SUCCESS) {
        strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
    }
    else if (mat->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS) {
        strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
    }
// НЕ делаем двойной strncpy подряд — это и было причиной склейки!
    // ВЫВОДИМ В КОНСОЛЬ, ЧТОБЫ ВИДЕТЬ, ЧТО НАШЛИ
    if (strlen(matData.ormPath) > 0 && 
        strcmp(matData.albedoPath, matData.ormPath) == 0) {
        std::cerr << "[WARNING] Material " << i 
                << ": albedo и ORM указывают на одну текстуру! Скорее всего ошибка импорта.\n";
        memset(matData.ormPath, 0, sizeof(matData.ormPath));
    }

    std::cout << "[Material " << i << "]\n"
            << "  Albedo : " << (matData.albedoPath[0] ? matData.albedoPath : "NONE") << "\n"
            << "  Normal : " << (matData.normalPath[0] ? matData.normalPath : "NONE") << "\n"  
          << "  ORM    : " << (matData.ormPath[0] ? matData.ormPath : "NONE") << "\n";

    outFile.write(reinterpret_cast<const char*>(&matData), sizeof(BHMaterialData));
}
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
            if (aimesh->mNumVertices > CLUSTER_SPLIT_THRESHOLD) {
                writeMeshAsCluster(outFile, vertices, indices, 
                                aimesh, aabbMin, aabbMax,
                                modelHeader.meshCount);  // ← передаём счётчик
            } else {
                writeMeshWithLODs(outFile, vertices, indices, aimesh, aabbMin, aabbMax);
                modelHeader.meshCount++;
            }
        }
        outFile.seekp(0);
        outFile.write(reinterpret_cast<const char*>(&modelHeader), sizeof(BHModelHeader));
        outFile.close();
        std::cout << "[SUCCESS] .bhmesh saved: " << destPath << std::endl;
        return true;
    }

 // ModelImporter.cpp

void ModelImporter::writeMeshWithLODs(
    std::ofstream& outFile,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    aiMesh* aimesh,
    glm::vec3 aabbMin, glm::vec3 aabbMax)
{
    // --- Старый код генерации LOD из предыдущей версии ---
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
                &vertices[0].position.x, vertices.size(), sizeof(Vertex),
                target, 0.05f);
            if (newCount == 0) break;
            lodIdx.resize(newCount);
            if (newCount < lods.back().size()) {
                meshopt_optimizeVertexCache(lodIdx.data(), lodIdx.data(), 
                                            lodIdx.size(), vertices.size());
                lods.push_back(lodIdx);
            } else break;
        }
    }

    BHMeshHeader meshHeader{};
    strncpy(meshHeader.name, 
            aimesh->mName.length > 0 ? aimesh->mName.C_Str() : "Mesh",
            sizeof(meshHeader.name) - 1);
    meshHeader.name[sizeof(meshHeader.name) - 1] = '\0';
    meshHeader.materialIndex  = aimesh->mMaterialIndex;
    meshHeader.aabbMin        = aabbMin;
    meshHeader.aabbMax        = aabbMax;
    meshHeader.boundingRadius = glm::distance(aabbMin, aabbMax) * 0.5f;
    meshHeader.lodCount       = (uint32_t)lods.size();
    meshHeader.indexType      = 1;

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

void ModelImporter::writeMeshAsCluster(
    std::ofstream& outFile,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    aiMesh* aimesh,
    glm::vec3 aabbMin, glm::vec3 aabbMax,
    uint32_t& meshCountOut)
{
    size_t maxMeshlets = meshopt_buildMeshletsBound(
        indices.size(), CLUSTER_MAX_TRIANGLES, 64);
    
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<uint32_t>        meshletVertices(maxMeshlets * 64);
    std::vector<uint8_t>         meshletTriangles(maxMeshlets * CLUSTER_MAX_TRIANGLES * 3);
    
    size_t meshletCount = meshopt_buildMeshlets(
        meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
        indices.data(), indices.size(),
        &vertices[0].position.x, vertices.size(), sizeof(Vertex),
        CLUSTER_MAX_TRIANGLES, 64, 0.0f);
    
    std::cout << "[Cluster] " << aimesh->mName.C_Str() 
              << " -> " << meshletCount << " кластеров\n";

    for (size_t i = 0; i < meshletCount; i++) {
        const auto& m = meshlets[i];

        std::vector<uint32_t> clusterIndices;
        clusterIndices.reserve(m.triangle_count * 3);
        for (uint32_t t = 0; t < m.triangle_count; t++)
            for (int k = 0; k < 3; k++) {
                uint8_t lv = meshletTriangles[m.triangle_offset + t * 3 + k];
                clusterIndices.push_back(meshletVertices[m.vertex_offset + lv]);
            }

        glm::vec3 cMin(1e9f), cMax(-1e9f);
        for (uint32_t vi = 0; vi < m.vertex_count; vi++) {
            uint32_t gi = meshletVertices[m.vertex_offset + vi];
            cMin = glm::min(cMin, vertices[gi].position);
            cMax = glm::max(cMax, vertices[gi].position);
        }

        BHMeshHeader meshHeader{};
        snprintf(meshHeader.name, sizeof(meshHeader.name),
                 "%s_c%zu", aimesh->mName.C_Str(), i);
        meshHeader.materialIndex  = aimesh->mMaterialIndex;
        meshHeader.aabbMin        = cMin;
        meshHeader.aabbMax        = cMax;
        meshHeader.boundingRadius = glm::distance(cMin, cMax) * 0.5f;
        meshHeader.lodCount       = 1;
        meshHeader.indexType      = 1;

        outFile.write(reinterpret_cast<const char*>(&meshHeader), sizeof(BHMeshHeader));

        if (i == 0) {
            uint32_t vCount = (uint32_t)vertices.size();
            outFile.write(reinterpret_cast<const char*>(&vCount), sizeof(uint32_t));
            outFile.write(reinterpret_cast<const char*>(vertices.data()),
                          vCount * sizeof(Vertex));
        } else {
            uint32_t zero = 0;
            outFile.write(reinterpret_cast<const char*>(&zero), sizeof(uint32_t));
        }

        BHLodHeader lodHeader{};
        lodHeader.indexCount = (uint32_t)clusterIndices.size();
        outFile.write(reinterpret_cast<const char*>(&lodHeader), sizeof(BHLodHeader));
        outFile.write(reinterpret_cast<const char*>(clusterIndices.data()),
                      clusterIndices.size() * sizeof(uint32_t));

        meshCountOut++;  // ← безопасно увеличиваем счётчик
    }
}
}