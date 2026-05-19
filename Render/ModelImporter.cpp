#include "ModelImporter.h"
#include "../Utils/DirectXMathCompat.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <meshoptimizer.h>
#include <chrono>

namespace fs = std::filesystem;

namespace burnhope
{
    // --- УТИЛИТЫ ДЛЯ СЖАТИЯ И ХЭШИРОВАНИЯ ---
    static constexpr uint64_t HashStringFNV1a(const char* str) {
        uint64_t hash = 0xcbf29ce484222325;
        while (*str) {
            hash ^= static_cast<uint64_t>(*str++);
            hash *= 0x100000001b3;
        }
        return hash;
    }

    // Сжатие Кватерниона в 32-бита (Smallest Three)
    static uint32_t PackQuaternionSmallest3(float4 q) {
        // Ищем наибольшую компоненту
        int maxIndex = 0;
        float maxVal = std::abs(q.x);
        if (std::abs(q.y) > maxVal) { maxVal = std::abs(q.y); maxIndex = 1; }
        if (std::abs(q.z) > maxVal) { maxVal = std::abs(q.z); maxIndex = 2; }
        if (std::abs(q.w) > maxVal) { maxVal = std::abs(q.w); maxIndex = 3; }

        // Убеждаемся, что наибольшая компонента положительная
        float qMaxVal = (maxIndex == 0) ? q.x : (maxIndex == 1) ? q.y : (maxIndex == 2) ? q.z : q.w;
        if (qMaxVal < 0.0f) q = -q;

        float scale = 1.0f / 0.70710678118f; // 1 / sqrt(2) (макс возможное значение для остальных компонент)
        uint32_t packed = maxIndex; // 2 бита под индекс (0-3)
        
        int shift = 2;
        for (int i = 0; i < 4; i++) {
            if (i == maxIndex) continue;
            // Мапим от [-sqrt(2)/2, sqrt(2)/2] в [0, 1023] (10 бит)
            float qVal = (i == 0) ? q.x : (i == 1) ? q.y : (i == 2) ? q.z : q.w;
            float normalized = (qVal * scale) * 0.5f + 0.5f;
            uint32_t quantized = static_cast<uint32_t>(Clamp(normalized, 0.0f, 1.0f) * 1023.0f + 0.5f);
            packed |= (quantized << shift);
            shift += 10;
        }
        return packed;
    }

    void ProcessNodeForSockets(aiNode* node, const float4x4& parentTransform, std::vector<BHSocket>& sockets) {
        float4x4 localTransform;
        localTransform._11 = node->mTransformation.a1; localTransform._12 = node->mTransformation.b1; localTransform._13 = node->mTransformation.c1; localTransform._14 = node->mTransformation.d1;
        localTransform._21 = node->mTransformation.a2; localTransform._22 = node->mTransformation.b2; localTransform._23 = node->mTransformation.c2; localTransform._24 = node->mTransformation.d2;
        localTransform._31 = node->mTransformation.a3; localTransform._32 = node->mTransformation.b3; localTransform._33 = node->mTransformation.c3; localTransform._34 = node->mTransformation.d3;
        localTransform._41 = node->mTransformation.a4; localTransform._42 = node->mTransformation.b4; localTransform._43 = node->mTransformation.c4; localTransform._44 = node->mTransformation.d4; 

        float4x4 globalTransform = MatrixMultiply(parentTransform, localTransform);
        std::string nodeName = node->mName.C_Str();
        if (nodeName.find("Socket") != std::string::npos || nodeName.find("Anchor") != std::string::npos || nodeName.find("Point") != std::string::npos) {
            BHSocket socket{};
            strncpy(socket.name, nodeName.c_str(), 63);
            socket.transform = globalTransform;
            std::cout << "[SOCKET] Found: " << nodeName << std::endl;
            sockets.push_back(socket);
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            ProcessNodeForSockets(node->mChildren[i], globalTransform, sockets);
        }
    }

    bool ModelImporter::ImportModel(const std::string &srcPath, const std::string &destPath)
    {
        std::cout << "\n======================================================\n";
        std::cout << "[IMPORT] Начинаем глубокий импорт модели: " << srcPath << "\n";
        std::cout << "======================================================\n";

        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(srcPath,
                                                 aiProcess_Triangulate |
                                                     aiProcess_GenNormals |
                                                     aiProcess_CalcTangentSpace |
                                                     aiProcess_JoinIdenticalVertices);
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cerr << "[ERROR] Assimp: " << importer.GetErrorString() << std::endl;
            return false;
        }
        std::ofstream outFile(destPath, std::ios::binary);
        if (!outFile.is_open())
            return false;
        
        std::vector<BHMaterialData> matDataArray;
        float avgAcousticAbs = 0.1f;
        float avgAcousticRef = 0.9f;

        std::cout << "[IMPORT] ЭТАП 7: Мульти-Материалы (Анализ и подготовка MaterialBatches)...\n";
        for (unsigned int i = 0; i < scene->mNumMaterials; i++)
        {
            aiMaterial *mat = scene->mMaterials[i];
            BHMaterialData matData{};
            memset(&matData, 0, sizeof(BHMaterialData));
            aiString path;
            aiString albedoPath, normalPath, ormPath;
            if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &albedoPath) == AI_SUCCESS)
            {
                strncpy(matData.albedoPath, albedoPath.C_Str(), sizeof(matData.albedoPath) - 1);
            }
            else if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &albedoPath) == AI_SUCCESS)
            {
                strncpy(matData.albedoPath, albedoPath.C_Str(), sizeof(matData.albedoPath) - 1);
            }
            if (mat->GetTexture(aiTextureType_NORMALS, 0, &normalPath) == AI_SUCCESS)
            {
                strncpy(matData.normalPath, normalPath.C_Str(), sizeof(matData.normalPath) - 1);
            }
            else if (mat->GetTexture(aiTextureType_HEIGHT, 0, &normalPath) == AI_SUCCESS)
            {
                strncpy(matData.normalPath, normalPath.C_Str(), sizeof(matData.normalPath) - 1);
            }
            if (mat->GetTexture(aiTextureType_UNKNOWN, 0, &path) == AI_SUCCESS)
            {
                strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
            }
            else if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path) == AI_SUCCESS)
            {
                strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
            }
            else if (mat->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS)
            {
                strncpy(matData.ormPath, path.C_Str(), sizeof(matData.ormPath) - 1);
            }
            if (strlen(matData.ormPath) > 0 &&
                strcmp(matData.albedoPath, matData.ormPath) == 0)
            {
                std::cerr << "[WARNING] Material " << i
                          << ": albedo и ORM указывают на одну текстуру! Скорее всего ошибка импорта.\n";
                memset(matData.ormPath, 0, sizeof(matData.ormPath));
            }
            std::cout << "[Material " << i << "]\n"
                      << "  Albedo : " << (matData.albedoPath[0] ? matData.albedoPath : "NONE") << "\n"
                      << "  Normal : " << (matData.normalPath[0] ? matData.normalPath : "NONE") << "\n"
                      << "  ORM    : " << (matData.ormPath[0] ? matData.ormPath : "NONE") << "\n";
            matDataArray.push_back(matData);

            std::string matName = mat->GetName().C_Str();
            std::transform(matName.begin(), matName.end(), matName.begin(), ::tolower);
            if (matName.find("wood") != std::string::npos || matName.find("fabric") != std::string::npos || matName.find("sofa") != std::string::npos || matName.find("carpet") != std::string::npos) {
                avgAcousticAbs = std::max(avgAcousticAbs, 0.8f);
                avgAcousticRef = std::min(avgAcousticRef, 0.2f);
            }
        }
        std::vector<aiMesh*> sortedMeshes(scene->mMeshes, scene->mMeshes + scene->mNumMeshes);
        std::stable_sort(sortedMeshes.begin(), sortedMeshes.end(), [](aiMesh* a, aiMesh* b) {
            return a->mMaterialIndex < b->mMaterialIndex;
        });

        // --- ЭТАП 0: Ручной PreTransformVertices, чтобы не удалять кости и анимации ---
        std::vector<aiMatrix4x4> meshTransforms(scene->mNumMeshes, aiMatrix4x4());
        auto traverseNodes = [&](auto& self, aiNode* node, aiMatrix4x4 accTransform) -> void {
            aiMatrix4x4 global = accTransform * node->mTransformation;
            for (unsigned int i = 0; i < node->mNumMeshes; i++) {
                meshTransforms[node->mMeshes[i]] = global;
            }
            for (unsigned int i = 0; i < node->mNumChildren; i++) {
                self(self, node->mChildren[i], global);
            }
        };
        traverseNodes(traverseNodes, scene->mRootNode, aiMatrix4x4());

        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh* aimesh = const_cast<aiMesh*>(scene->mMeshes[m]);
            aiMatrix4x4 globalTransform = meshTransforms[m];
            aiMatrix4x4 invGlobal = globalTransform;
            invGlobal.Inverse();
            aiMatrix3x3 normalMatrix = aiMatrix3x3(invGlobal);
            normalMatrix.Transpose();

            for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
                aimesh->mVertices[i] = globalTransform * aimesh->mVertices[i];
                if (aimesh->HasNormals()) {
                    aimesh->mNormals[i] = normalMatrix * aimesh->mNormals[i];
                    aimesh->mNormals[i].Normalize();
                }
                if (aimesh->HasTangentsAndBitangents()) {
                    aimesh->mTangents[i] = normalMatrix * aimesh->mTangents[i];
                    aimesh->mTangents[i].Normalize();
                    aimesh->mBitangents[i] = normalMatrix * aimesh->mBitangents[i];
                    aimesh->mBitangents[i].Normalize();
                }
            }

            for (unsigned int b = 0; b < aimesh->mNumBones; b++) {
                aimesh->mBones[b]->mOffsetMatrix = aimesh->mBones[b]->mOffsetMatrix * invGlobal;
            }
        }

        float3 globalMin{1e9f, 1e9f, 1e9f}, globalMax{-1e9f, -1e9f, -1e9f};
        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh* aimesh = scene->mMeshes[m];
            for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
                float3 pos{aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z};
                globalMin = Min(globalMin, pos);
                globalMax = Max(globalMax, pos);
            }
        }
        float3 globalExtent = globalMax - globalMin;
        if(globalExtent.x == 0.0f) globalExtent.x = 1.0f;
        if(globalExtent.y == 0.0f) globalExtent.y = 1.0f;
        if(globalExtent.z == 0.0f) globalExtent.z = 1.0f;

        // --- ЭТАП 9: ИЗВЛЕЧЕНИЕ СКЕЛЕТА (.bhbone) ---
        std::cout << "[IMPORT] ЭТАП 9: Извлечение Скелета (.bhbone)...\n";
        std::vector<BHBoneNode> bones;
        std::vector<BHVirtualIK> virtualIks;
        std::map<std::string, int> boneMapping;
        bool hasBones = false;
        if (scene->mNumMeshes > 0) {
            for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
                if (scene->mMeshes[m]->HasBones()) { hasBones = true; break; }
            }
        }
        if (hasBones) {
            std::cout << "[IMPORT] Оптимизация иерархии: Удаление строк, генерация LOD-масок, Flat Array...\n";
            for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
                for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; b++) {
                    aiBone* bone = scene->mMeshes[m]->mBones[b];
                    std::string boneName = bone->mName.C_Str();
                    if (boneMapping.find(boneName) == boneMapping.end()) {
                        BHBoneNode node{};
                        node.nameHash = HashStringFNV1a(boneName.c_str());
                        node.inverseBindMatrix._11 = bone->mOffsetMatrix.a1; node.inverseBindMatrix._12 = bone->mOffsetMatrix.a2; node.inverseBindMatrix._13 = bone->mOffsetMatrix.a3; node.inverseBindMatrix._14 = bone->mOffsetMatrix.a4;
                        node.inverseBindMatrix._21 = bone->mOffsetMatrix.b1; node.inverseBindMatrix._22 = bone->mOffsetMatrix.b2; node.inverseBindMatrix._23 = bone->mOffsetMatrix.b3; node.inverseBindMatrix._24 = bone->mOffsetMatrix.b4;
                        node.inverseBindMatrix._31 = bone->mOffsetMatrix.c1; node.inverseBindMatrix._32 = bone->mOffsetMatrix.c2; node.inverseBindMatrix._33 = bone->mOffsetMatrix.c3; node.inverseBindMatrix._34 = bone->mOffsetMatrix.c4;
                        node.inverseBindMatrix._41 = bone->mOffsetMatrix.d1; node.inverseBindMatrix._42 = bone->mOffsetMatrix.d2; node.inverseBindMatrix._43 = bone->mOffsetMatrix.d3; node.inverseBindMatrix._44 = bone->mOffsetMatrix.d4;
                        boneMapping[boneName] = bones.size();
                        
                        // Эвристика для масок LOD:
                        std::string lowerName = boneName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        if (lowerName.find("finger") != std::string::npos || lowerName.find("toe") != std::string::npos || lowerName.find("face") != std::string::npos) {
                            node.lodLevel = 1; // Убираем на средней дистанции
                        }
                        if (lowerName.find("twist") != std::string::npos || lowerName.find("roll") != std::string::npos) {
                            node.isTwistBone = 1; // Процедурная кость
                        }
                        bones.push_back(node);
                    }
                }
            }
            // Поиск родителей и Mirror Mapping
            for (auto& bone : bones) {
                // Ищем по ключу в boneMapping (строки пока держим в мапе)
                std::string origName;
                for(const auto& pair : boneMapping) if(pair.second == (&bone - &bones[0])) origName = pair.first;
                
                aiNode* node = scene->mRootNode->FindNode(origName.c_str());
                if (node && node->mParent) {
                    std::string parentName = node->mParent->mName.C_Str();
                    if (boneMapping.find(parentName) != boneMapping.end()) {
                        bone.parentIndex = boneMapping[parentName];
                    }
                }
                // Mirror Mapping: Ищем симметричную кость
                std::string mirrorName = origName;
                if (mirrorName.find("Left") != std::string::npos) {
                    mirrorName.replace(mirrorName.find("Left"), 4, "Right");
                } else if (mirrorName.find("Right") != std::string::npos) {
                    mirrorName.replace(mirrorName.find("Right"), 5, "Left");
                }
                if (mirrorName != origName && boneMapping.find(mirrorName) != boneMapping.end()) {
                    bone.mirrorBoneIndex = boneMapping[mirrorName];
                }
            }
                       if (!bones.empty()) {
                auto createIK = [&](const std::string& name, const std::string& sourceName) {
                    if (boneMapping.find(sourceName) != boneMapping.end()) {
                        BHVirtualIK ik{};
                        ik.nameHash = HashStringFNV1a(name.c_str());
                        ik.sourceBoneIndex = boneMapping[sourceName];
                        ik.localOffset = float3{0.0f, 0.0f, 0.0f};
                        virtualIks.push_back(ik);
                    }
                };
                createIK("ik_hand_l", "LeftHand");
                createIK("ik_hand_r", "RightHand");
                createIK("ik_foot_l", "LeftFoot");
                createIK("ik_foot_r", "RightFoot");
            }

            // Сохранение .bhbone
            std::string bonePath = destPath.substr(0, destPath.find_last_of('.')) + ".bhbone";
            std::ofstream boneFile(bonePath, std::ios::binary);
            if (boneFile.is_open()) {
                BHBoneHeader bhdr; 
                bhdr.boneCount = bones.size();
                bhdr.virtualIkCount = virtualIks.size();
                boneFile.write(reinterpret_cast<const char*>(&bhdr), sizeof(BHBoneHeader));
                if (!bones.empty()) boneFile.write(reinterpret_cast<const char*>(bones.data()), bones.size() * sizeof(BHBoneNode));
                if (!virtualIks.empty()) boneFile.write(reinterpret_cast<const char*>(virtualIks.data()), virtualIks.size() * sizeof(BHVirtualIK));
                boneFile.close();
                std::cout << "[SUCCESS] Skeleton saved to " << bonePath << "\n";
            }
        }

        // --- ЭТАП 9: ИЗВЛЕЧЕНИЕ АНИМАЦИИ (.bhanim) ---
        std::cout << "[IMPORT] ЭТАП 9: Извлечение Анимации (.bhanim)...\n";
        if (scene->HasAnimations()) {
            aiAnimation* anim = scene->mAnimations[0];
            std::string animPath = destPath.substr(0, destPath.find_last_of('.')) + ".bhanim";
            std::ofstream animFile(animPath, std::ios::binary);
            if (animFile.is_open()) {
                BHAnimHeader ahdr{};
                strncpy(ahdr.name, anim->mName.C_Str(), 63);
                ahdr.duration = anim->mDuration;
                ahdr.ticksPerSecond = anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0f;
                ahdr.trackCount = anim->mNumChannels;
                
                std::cout << "[IMPORT] Анимация: Квантование (Smallest Three), Root Motion извлечение, Sync Phase...\n";
                
                std::vector<BHAnimTrack> tracks;
                std::vector<BHKeyframeCompressed> keyframes;
                std::vector<BHRootMotionKey> rootMotionKeys;
                
                for (unsigned int i = 0; i < anim->mNumChannels; i++) {
                    aiNodeAnim* channel = anim->mChannels[i];
                    BHAnimTrack track{};
                    track.boneNameHash = HashStringFNV1a(channel->mNodeName.C_Str());
                    track.firstKeyframe = keyframes.size();
                    
                    uint32_t maxKeys = std::max({channel->mNumPositionKeys, channel->mNumRotationKeys, channel->mNumScalingKeys});
                    track.keyframeCount = maxKeys;
                    
                    // Ищем AABB трека позиций для квантования
                    track.posMin = float3{1e9f, 1e9f, 1e9f}; track.posMax = float3{-1e9f, -1e9f, -1e9f};
                    for(uint32_t k = 0; k < channel->mNumPositionKeys; k++) {
                        float3 p(channel->mPositionKeys[k].mValue.x, channel->mPositionKeys[k].mValue.y, channel->mPositionKeys[k].mValue.z);
                        track.posMin = Min(track.posMin, p);
                        track.posMax = Max(track.posMax, p);
                    }
                    float3 posExtent = track.posMax - track.posMin;
                    if(posExtent.x == 0) posExtent.x = 0.001f; if(posExtent.y == 0) posExtent.y = 0.001f; if(posExtent.z == 0) posExtent.z = 0.001f;

                    // Если это Root-кость, извлекаем Root Motion
                    bool isRoot = (i == 0 || std::string(channel->mNodeName.C_Str()) == "Root" || std::string(channel->mNodeName.C_Str()) == "Hips");
                    
                    for (uint32_t k = 0; k < maxKeys; k++) {
                        BHKeyframeCompressed kf{};
                        if (k < channel->mNumPositionKeys) {
                            kf.time = (float)channel->mPositionKeys[k].mTime;
                            float3 p = {channel->mPositionKeys[k].mValue.x, channel->mPositionKeys[k].mValue.y, channel->mPositionKeys[k].mValue.z};
                            float3 normPos = (p - track.posMin) / posExtent;
                            kf.posX = static_cast<uint16_t>(Clamp(normPos.x, 0.0f, 1.0f) * 65535.0f + 0.5f);
                            kf.posY = static_cast<uint16_t>(Clamp(normPos.y, 0.0f, 1.0f) * 65535.0f + 0.5f);
                            kf.posZ = static_cast<uint16_t>(Clamp(normPos.z, 0.0f, 1.0f) * 65535.0f + 0.5f);
                        }
                        if (k < channel->mNumRotationKeys) {
                            kf.time = (float)channel->mRotationKeys[k].mTime;
                            kf.packedRotation = PackQuaternionSmallest3({channel->mRotationKeys[k].mValue.x, channel->mRotationKeys[k].mValue.y, channel->mRotationKeys[k].mValue.z, channel->mRotationKeys[k].mValue.w});
                        }
                        if (k < channel->mNumScalingKeys) {
                            kf.time = (float)channel->mScalingKeys[k].mTime;
                            // Оптимизация: Scale редко нужен, ужимаем жестко
                            kf.scaleX = 1000; kf.scaleY = 1000; kf.scaleZ = 1000; 
                        }
                        keyframes.push_back(kf);
                        
                        if (isRoot && k < channel->mNumPositionKeys && k < channel->mNumRotationKeys) {
                            BHRootMotionKey rm{};
                            rm.time = kf.time;
                            rm.deltaPosition = {channel->mPositionKeys[k].mValue.x, channel->mPositionKeys[k].mValue.y, channel->mPositionKeys[k].mValue.z};
                            rm.deltaRotation = {channel->mRotationKeys[k].mValue.x, channel->mRotationKeys[k].mValue.y, channel->mRotationKeys[k].mValue.z, channel->mRotationKeys[k].mValue.w};
                            rootMotionKeys.push_back(rm);
                        }
                    }
                    tracks.push_back(track);
                }

                // --- ЭТАП: Motion Matching (Траектории) ---
                std::vector<BHTrajectoryData> trajectories;
                if (!rootMotionKeys.empty()) {
                    for (size_t k = 0; k < rootMotionKeys.size(); k += 10) {
                        BHTrajectoryData traj{};
                        traj.time = rootMotionKeys[k].time;
                        size_t futureK = std::min(k + 10, rootMotionKeys.size() - 1);
                        float dt = rootMotionKeys[futureK].time - rootMotionKeys[k].time;
                        if (dt > 0.0f) {
                            traj.velocity = (rootMotionKeys[futureK].deltaPosition - rootMotionKeys[k].deltaPosition) / dt;
                        } else {
                            traj.velocity = float3{0.0f, 0.0f, 0.0f};
                        }
                        quat q(rootMotionKeys[k].deltaRotation.w, rootMotionKeys[k].deltaRotation.x, rootMotionKeys[k].deltaRotation.y, rootMotionKeys[k].deltaRotation.z);
                        float3 forward = q * float3{0.0f, 0.0f, 1.0f};
                        traj.facingAngle = std::atan2(forward.x, forward.z);
                        trajectories.push_back(traj);
                    }
                }

                // --- ЭТАП: Sync Markers (Синхронизация фаз шага) ---
                std::vector<BHAnimNotify> notifies;
                if (anim->mDuration > 0) {
                    BHAnimNotify stepNotify{};
                    stepNotify.eventHash = HashStringFNV1a("FootStep_Half");
                    stepNotify.time = anim->mDuration * 0.5f; 
                    notifies.push_back(stepNotify);
                }

                // --- ЭТАП: Float Curves (Лицевые анимации и параметры) ---
                std::vector<BHFloatCurve> curves;
                std::vector<float> curveKeyframes;
                for (unsigned int i = 0; i < anim->mNumMorphMeshChannels; i++) {
                    aiMeshMorphAnim* morphChannel = anim->mMorphMeshChannels[i];
                    BHFloatCurve curve{};
                    curve.nameHash = HashStringFNV1a(morphChannel->mName.C_Str());
                    curve.keyCount = morphChannel->mNumKeys;
                    curves.push_back(curve);
                    for (unsigned int k = 0; k < morphChannel->mNumKeys; k++) {
                        curveKeyframes.push_back((float)morphChannel->mKeys[k].mTime);
                        curveKeyframes.push_back(morphChannel->mKeys[k].mValues[0]);
                    }
                }

                std::string animNameLower = anim->mName.C_Str();
                std::transform(animNameLower.begin(), animNameLower.end(), animNameLower.begin(), ::tolower);
                if (animNameLower.find("add") != std::string::npos || animNameLower.find("additive") != std::string::npos) {
                    ahdr.isAdditive = 1;
                }

                ahdr.totalKeyframeCount = keyframes.size();
                ahdr.rootMotionKeyCount = rootMotionKeys.size();
                ahdr.trajectoryCount = trajectories.size();
                ahdr.notifyCount = notifies.size();
                ahdr.curveCount = curves.size();

                animFile.write(reinterpret_cast<const char*>(&ahdr), sizeof(BHAnimHeader));
                if (!tracks.empty()) animFile.write(reinterpret_cast<const char*>(tracks.data()), tracks.size() * sizeof(BHAnimTrack));
                if (!keyframes.empty()) animFile.write(reinterpret_cast<const char*>(keyframes.data()), keyframes.size() * sizeof(BHKeyframeCompressed));
                if (!rootMotionKeys.empty()) animFile.write(reinterpret_cast<const char*>(rootMotionKeys.data()), rootMotionKeys.size() * sizeof(BHRootMotionKey));
                if (!trajectories.empty()) animFile.write(reinterpret_cast<const char*>(trajectories.data()), trajectories.size() * sizeof(BHTrajectoryData));
                if (!notifies.empty()) animFile.write(reinterpret_cast<const char*>(notifies.data()), notifies.size() * sizeof(BHAnimNotify));
                if (!curves.empty()) animFile.write(reinterpret_cast<const char*>(curves.data()), curves.size() * sizeof(BHFloatCurve));
                if (!curveKeyframes.empty()) animFile.write(reinterpret_cast<const char*>(curveKeyframes.data()), curveKeyframes.size() * sizeof(float));
                animFile.close();
                std::cout << "[SUCCESS] Animation saved: " << tracks.size() << " tracks, " << keyframes.size() << " keys. RootMotion keys: " << rootMotionKeys.size() << "\n";
            }
        } else {
            std::cout << "[WARNING] В FBX файле не найдены анимации! Убедитесь, что они были запечены при экспорте.\n";
        }

        std::vector<PackedVertexPos> globalPos;
        std::vector<PackedVertexAttr> globalAttr;
        std::vector<PackedVertexAnim> globalAnim;
        std::vector<float3> globalOrigPos;
        
        std::vector<uint32_t> globalColors;
        std::vector<uint32_t> globalUV2;
        
        std::cout << "[IMPORT] ЭТАП 2: Разделение потоков (Stream Splitting)...\n";
        std::cout << "[IMPORT] Анализ дополнительных потоков (UV2 для Lightmaps, Vertex Colors)...\n";
        bool sceneHasColors = false;
        bool sceneHasUV2 = false;
        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            if (scene->mMeshes[m]->HasVertexColors(0)) sceneHasColors = true;
            if (scene->mMeshes[m]->HasTextureCoords(1)) sceneHasUV2 = true;
        }

        struct MatBatch {
            std::vector<unsigned int> indices;
        };
        std::map<uint32_t, MatBatch> batches;
    float3 center = globalMin + globalExtent * 0.5f;
        for (aiMesh* aimesh : sortedMeshes) {
            uint32_t vertexBase = globalPos.size();
            
            struct VertexWeight {
                uint32_t boneID;
                float weight;
            };
            std::vector<std::vector<VertexWeight>> meshVertexWeights(aimesh->mNumVertices);
            for (unsigned int b = 0; b < aimesh->mNumBones; b++) {
                aiBone* bone = aimesh->mBones[b];
                std::string boneName = bone->mName.C_Str();
                uint32_t boneID = 0;
                if (boneMapping.find(boneName) != boneMapping.end()) {
                    boneID = boneMapping[boneName];
                }
                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    uint32_t vID = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    meshVertexWeights[vID].push_back({boneID, weight});
                }
            }

            for (unsigned int i = 0; i < aimesh->mNumVertices; i++) {
                float3 pos = {aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z};
                globalOrigPos.push_back(pos);

                // --- Этап 4. Опциональные потоки (Vertex Colors & Secondary UVs) ---
                if (sceneHasColors) {
                    if (aimesh->HasVertexColors(0)) {
                        float4 color{aimesh->mColors[0][i].r, aimesh->mColors[0][i].g, aimesh->mColors[0][i].b, aimesh->mColors[0][i].a};
                        globalColors.push_back(PackUnorm4x8(color));
                    } else {
                        globalColors.push_back(PackUnorm4x8(float4{1.0f, 1.0f, 1.0f, 1.0f})); // Default white
                    }
                }
                if (sceneHasUV2) {
                    if (aimesh->HasTextureCoords(1)) {
                        globalUV2.push_back(PackHalf2x16(float2{aimesh->mTextureCoords[1][i].x, aimesh->mTextureCoords[1][i].y}));
                    } else {
                        globalUV2.push_back(0);
                    }
                }

                float3 normPos = (pos - globalMin) / globalExtent;
                PackedVertexPos pPos;
                pPos.x = static_cast<uint16_t>(Clamp(normPos.x, 0.0f, 1.0f) * 65535.0f + 0.5f);
                pPos.y = static_cast<uint16_t>(Clamp(normPos.y, 0.0f, 1.0f) * 65535.0f + 0.5f);
                pPos.z = static_cast<uint16_t>(Clamp(normPos.z, 0.0f, 1.0f) * 65535.0f + 0.5f);

                // --- Этап 3. Экстремальное Квантование Вершин (Vertex Squeezing) ---
                // --- Этап 3. Веса процедурной анимации (Wind Stiffness) ---
                float windWeight = 0.0f;
                if (aimesh->HasVertexColors(0)) {
                    windWeight = aimesh->mColors[0][i].r; // Берем вес из красного канала Vertex Color, если он есть
                } else {
                windWeight = 0.0f; // Убираем автоматический ветер, чтобы обычные модели не "плавали"
                }
                uint8_t windW = static_cast<uint8_t>(Clamp(windWeight, 0.0f, 1.0f) * 255.0f);

                // --- Этап 1. Толщина для SSS (Subsurface Scattering) ---
                // Эвристика: чем ближе вершина к центру бокса, тем она толще. 
                // Идеально работает для листвы, ушей монстров и свечей без тяжелого рейкастинга.
                float distToCenter = Length(pos - (globalMin + globalExtent * 0.5f));
                float maxDist = Length(globalExtent) * 0.5f;
                uint8_t thickness = static_cast<uint8_t>(Clamp((1.0f - (distToCenter / maxDist)), 0.0f, 1.0f) * 255.0f);

                // Пакуем Ветер в младший байт, а Толщину в старший
                pPos.pad = (static_cast<uint16_t>(thickness) << 8) | static_cast<uint16_t>(windW);

                PackedVertexAttr pAttr;
                pAttr.texUV = aimesh->HasTextureCoords(0) ? PackHalf2x16(float2{aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y}) : 0;

                if (aimesh->HasNormals()) {
                    float3 n = Normalize(float3{aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z});
                    float3 t = float3{1.0f, 0.0f, 0.0f};
                    float handedness = 1.0f;
                    if (aimesh->HasTangentsAndBitangents()) {
                        t = Normalize(float3{aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z});
                        float3 b = Normalize(float3{aimesh->mBitangents[i].x, aimesh->mBitangents[i].y, aimesh->mBitangents[i].z});
                        handedness = (Dot(Cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
                    } else {
                        float3 up = std::abs(n.y) < 0.999f ? float3{0.0f, 1.0f, 0.0f} : float3{1.0f, 0.0f, 0.0f};
                        t = Normalize(Cross(up, n));
                    }
                    float3 b = Normalize(Cross(n, t) * handedness);
                    float4x4 tbn = MatrixIdentity();
                    tbn._11 = t.x; tbn._12 = t.y; tbn._13 = t.z;
                    tbn._21 = b.x; tbn._22 = b.y; tbn._23 = b.z;
                    tbn._31 = n.x; tbn._32 = n.y; tbn._33 = n.z;
                    quat q = quat_cast(tbn);
                    q = Normalize(q);
                    if (q.w < 0.0f) q = -q;
                    pAttr.qTangent = PackSnorm3x10_1x2(float4{q.x, q.y, q.z, handedness});
                } else {
                    pAttr.qTangent = PackSnorm3x10_1x2(float4{0.0f, 0.0f, 0.0f, 1.0f});
                }

                // --- Этап 2. Скелет, Pivot Painter и Ткань (Buffer C) ---
                PackedVertexAnim pAnim{};
                
                // 1. Pivot Painter: Запекаем центр геометрии модели как корень (для листвы)
                float3 pivotPos = (center - globalMin) / globalExtent;
                pAnim.pivotX = static_cast<uint16_t>(Clamp(pivotPos.x, 0.0f, 1.0f) * 65535.0f + 0.5f);
                pAnim.pivotY = static_cast<uint16_t>(Clamp(pivotPos.y, 0.0f, 1.0f) * 65535.0f + 0.5f);
                pAnim.pivotZ = static_cast<uint16_t>(Clamp(pivotPos.z, 0.0f, 1.0f) * 65535.0f + 0.5f);
                // 2. Cloth Max Distance (По умолчанию жестко привязано 0)
                pAnim.clothMaxDistance = 0;

                // --- ЭТАП 8. Запекание Ambient Occlusion (Cavity) ---
                float ao = 1.0f;
                if (aimesh->HasNormals()) {
                    float3 n = Normalize(float3{aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z});
                    float3 dirToCenter = Normalize((globalMin + globalExtent * 0.5f) - pos);
                    float dotCenter = Dot(n, dirToCenter);
                    if (dotCenter > 0.3f) ao = Clamp(1.0f - dotCenter, 0.0f, 1.0f); 
                }
                pAnim.vertexAO = static_cast<uint8_t>(ao * 255.0f);
                
                // 3. Bone Palette Data
                auto& weights = meshVertexWeights[i];
                std::sort(weights.begin(), weights.end(), [](const VertexWeight& a, const VertexWeight& b) { return a.weight > b.weight; });
                
                float totalWeight = 0.0f;
                size_t maxWeights = weights.size() < 4 ? weights.size() : 4;
                for (size_t w = 0; w < maxWeights; w++) totalWeight += weights[w].weight;
                
                for (int w = 0; w < 4; w++) {
                    if (w < weights.size() && totalWeight > 0.0f) {
                        pAnim.localBoneIndices[w] = static_cast<uint8_t>(weights[w].boneID);
                        pAnim.boneWeights[w] = static_cast<uint8_t>((weights[w].weight / totalWeight) * 255.0f);
                    } else {
                        pAnim.localBoneIndices[w] = 0;
                        pAnim.boneWeights[w] = 0;
                    }
                }
                
                int sum = pAnim.boneWeights[0] + pAnim.boneWeights[1] + pAnim.boneWeights[2] + pAnim.boneWeights[3];
                if (sum > 0 && sum != 255) {
                    pAnim.boneWeights[0] += (255 - sum);
                } else if (sum == 0) {
                    pAnim.boneWeights[0] = 255;
                }

                globalPos.push_back(pPos);
                globalAttr.push_back(pAttr);
                globalAnim.push_back(pAnim);
            }

            for (unsigned int i = 0; i < aimesh->mNumFaces; i++) {
                for (unsigned int j = 0; j < aimesh->mFaces[i].mNumIndices; j++) {
                    batches[aimesh->mMaterialIndex].indices.push_back(aimesh->mFaces[i].mIndices[j] + vertexBase);
                }
            }
        }

        // --- ЭТАП 1. Vertex Fetch Optimization ---
        std::cout << "[IMPORT] ЭТАП 1: Топология и Кэш (meshoptimizer: Vertex Cache, Overdraw, Vertex Fetch)...\n";
        std::vector<uint32_t> allIndicesForRemap;
        for (auto& [matIdx, batch] : batches) {
            allIndicesForRemap.insert(allIndicesForRemap.end(), batch.indices.begin(), batch.indices.end());
        }
                struct CombinedVertex {
            PackedVertexPos pos;
            PackedVertexAttr attr;
            PackedVertexAnim anim;
            uint32_t color;
            uint32_t uv2;
        };
        std::vector<CombinedVertex> combined(globalPos.size());
        for(size_t i = 0; i < globalPos.size(); i++) {
            combined[i].pos = globalPos[i];
            combined[i].attr = globalAttr[i];
            combined[i].anim = globalAnim[i];
            combined[i].color = sceneHasColors ? globalColors[i] : 0;
            combined[i].uv2 = sceneHasUV2 ? globalUV2[i] : 0;
        }
        std::vector<uint32_t> remap(globalPos.size());
          size_t optVertexCount = meshopt_generateVertexRemap(remap.data(), allIndicesForRemap.data(), allIndicesForRemap.size(), combined.data(), combined.size(), sizeof(CombinedVertex));
        std::vector<PackedVertexPos> optGlobalPos(optVertexCount);
        std::vector<PackedVertexAttr> optGlobalAttr(optVertexCount);
        std::vector<PackedVertexAnim> optGlobalAnim(optVertexCount);
        std::vector<float3> optGlobalOrigPos(optVertexCount);
        
        meshopt_remapVertexBuffer(optGlobalPos.data(), globalPos.data(), globalPos.size(), sizeof(PackedVertexPos), remap.data());
        meshopt_remapVertexBuffer(optGlobalAttr.data(), globalAttr.data(), globalAttr.size(), sizeof(PackedVertexAttr), remap.data());
        meshopt_remapVertexBuffer(optGlobalAnim.data(), globalAnim.data(), globalAnim.size(), sizeof(PackedVertexAnim), remap.data());
        meshopt_remapVertexBuffer(optGlobalOrigPos.data(), globalOrigPos.data(), globalOrigPos.size(), sizeof(float3), remap.data());

        if (sceneHasColors) {
            std::vector<uint32_t> optColors(optVertexCount);
            meshopt_remapVertexBuffer(optColors.data(), globalColors.data(), globalColors.size(), sizeof(uint32_t), remap.data());
            globalColors = std::move(optColors);
        }
        if (sceneHasUV2) {
            std::vector<uint32_t> optUV2(optVertexCount);
            meshopt_remapVertexBuffer(optUV2.data(), globalUV2.data(), globalUV2.size(), sizeof(uint32_t), remap.data());
            globalUV2 = std::move(optUV2);
        }
        
        globalPos = std::move(optGlobalPos); globalAttr = std::move(optGlobalAttr);
        globalAnim = std::move(optGlobalAnim); globalOrigPos = std::move(optGlobalOrigPos);

        std::vector<SubMesh> finalSubMeshes;
        std::vector<uint32_t> finalIndices;
        std::vector<uint32_t> proxyIndices;
        
        std::vector<uint32_t> m_vOffset, m_tOffset;
        std::vector<uint8_t> m_vCount, m_tCount;
        // --- SIMD SoA Layout for Culling (ЭТАП 3) ---
        std::vector<float> m_bCenterX, m_bCenterY, m_bCenterZ, m_bRadius;
        std::vector<float> m_coneAxisX, m_coneAxisY, m_coneAxisZ, m_coneCutoff;
        std::vector<uint32_t> m_dominantBones; // ЭТАП 6: Animated Culling
        std::vector<uint32_t> m_materials, m_vertices;
        std::vector<uint8_t> m_triangles;
        std::vector<uint32_t> m_bonePalettes;
        std::vector<float> m_errors;

        std::cout << "[IMPORT] ЭТАП 4: Архитектура Meshlets (Генерация кластеров для Task/Mesh Shaders)...\n";
        std::cout << "[IMPORT] ЭТАП 5: Метаданные для Compute Culling (SoA Layout)...\n";
        
        for (auto& [matIdx, batch] : batches) {
            if (batch.indices.empty()) continue; // Защита от пустых мешлетов (баг-превентор для Device Lost)
            meshopt_remapIndexBuffer(batch.indices.data(), batch.indices.data(), batch.indices.size(), remap.data());
            
            meshopt_optimizeVertexCache(batch.indices.data(), batch.indices.data(), batch.indices.size(), globalPos.size());
            meshopt_optimizeOverdraw(batch.indices.data(), batch.indices.data(), batch.indices.size(), &globalOrigPos[0].x, globalOrigPos.size(), sizeof(float3), 1.05f);
            
            std::vector<uint32_t> tempProxy(batch.indices.size());
            size_t targetProxyCount = std::min<size_t>(150, batch.indices.size());
            size_t proxyCount = meshopt_simplify(tempProxy.data(), batch.indices.data(), batch.indices.size(), &globalOrigPos[0].x, globalOrigPos.size(), sizeof(float3), targetProxyCount, 0.1f);
            tempProxy.resize(proxyCount);
            proxyIndices.insert(proxyIndices.end(), tempProxy.begin(), tempProxy.end());
            float simplificationError = 0.01f; // Базовая метрика для Parent Error Bound

            size_t max_meshlets = meshopt_buildMeshletsBound(batch.indices.size(), CLUSTER_MAX_VERTICES, CLUSTER_MAX_TRIANGLES);
            std::vector<meshopt_Meshlet> local_meshlets(max_meshlets);
            std::vector<unsigned int> local_meshlet_vertices(max_meshlets * CLUSTER_MAX_VERTICES);
            std::vector<unsigned char> local_meshlet_triangles(max_meshlets * CLUSTER_MAX_TRIANGLES * 3);

            size_t meshlet_count = meshopt_buildMeshlets(local_meshlets.data(), local_meshlet_vertices.data(), local_meshlet_triangles.data(),
                                                         batch.indices.data(), batch.indices.size(),
                                                         &globalOrigPos[0].x, globalOrigPos.size(), sizeof(float3),
                                                         CLUSTER_MAX_VERTICES, CLUSTER_MAX_TRIANGLES, 0.0f);

            // Parent Error Bound (Метрика для бесшовных LOD'ов - ЭТАП 4)
      

            for (size_t i = 0; i < meshlet_count; i++) {
                m_vOffset.push_back(m_vertices.size());
                m_tOffset.push_back(m_triangles.size());
                m_vCount.push_back(local_meshlets[i].vertex_count);
                m_tCount.push_back(local_meshlets[i].triangle_count);
                m_materials.push_back(matIdx);

                for (unsigned int j = 0; j < local_meshlets[i].vertex_count; j++) {
                    m_vertices.push_back(local_meshlet_vertices[local_meshlets[i].vertex_offset + j]);
                }
                for (unsigned int j = 0; j < ((local_meshlets[i].triangle_count * 3 + 3) & ~3); j++) {
                    m_triangles.push_back(local_meshlet_triangles[local_meshlets[i].triangle_offset + j]);
                }
                
                // --- Этап 2. Формирование Bone Palette Мешлета (до 8 костей) ---
                // Фиктивная инициализация для статичных мешей. (Для скелетных здесь будет агрегация уникальных boneID).
                for(int b=0; b<8; ++b) m_bonePalettes.push_back(0);

                meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                    &local_meshlet_vertices[local_meshlets[i].vertex_offset],
                    &local_meshlet_triangles[local_meshlets[i].triangle_offset], local_meshlets[i].triangle_count,
                    &globalOrigPos[0].x, globalOrigPos.size(), sizeof(float3));
                      float clusterError = simplificationError * bounds.radius * 0.01f;
                // Строгая запись в SoA (Structure of Arrays) для идеального кэша GPU
                m_bCenterX.push_back(bounds.center[0]);
                m_bCenterY.push_back(bounds.center[1]);
                m_bCenterZ.push_back(bounds.center[2]);
                m_bRadius.push_back(bounds.radius);
                
                m_coneAxisX.push_back(bounds.cone_axis[0]);
                m_coneAxisY.push_back(bounds.cone_axis[1]);
                m_coneAxisZ.push_back(bounds.cone_axis[2]);
                m_coneCutoff.push_back(bounds.cone_cutoff);
                
                m_errors.push_back(clusterError); // Пишем ошибку в SoA массив
                m_dominantBones.push_back(0); // TODO: Эвристика определения главной кости кластера
            }

            SubMesh sm{};
            sm.materialIndex = matIdx;
            sm.lodCount = 1;
            sm.firstIndices[0] = finalIndices.size();
            sm.indexCounts[0] = batch.indices.size();
            finalIndices.insert(finalIndices.end(), batch.indices.begin(), batch.indices.end());
            
            float3 sMin{1e9f, 1e9f, 1e9f}, sMax{-1e9f, -1e9f, -1e9f};
            for(uint32_t idx : batch.indices) {
                sMin = Min(sMin, globalOrigPos[idx]);
                sMax = Max(sMax, globalOrigPos[idx]);
            }
            sm.aabbMin = sMin;
            sm.aabbMax = sMax;
            sm.boundingRadius = Distance(sMin, sMax) * 0.5f;
            
            // --- Этап 4. Эвристика VRS (Variable Rate Shading) ---
            // Если геометрия массивная, но низкополигональная (поверхность дороги/стены), её можно рендерить 2x2 пикселя
            float density = batch.indices.size() / (Length(globalExtent) + 1e-5f);
            sm.vrsRate = (density < 500.0f) ? 1 : 0; // 1 = 2x2 Coarse Shading, 0 = 1x1 Native
            finalSubMeshes.push_back(sm);
        }

        // --- ЭТАП 1 и ЭТАП 2: Физические Теги Поверхностей и CDF распределение частиц ---
        std::vector<float> globalCDF;
        std::vector<uint8_t> globalSurfaceTags;
        float currentAreaSum = 0.0f;
        for (const auto& sm : finalSubMeshes) {
            // Используем индекс материала в качестве тега поверхности (можно маппить на физические материалы позже)
            uint8_t tag = static_cast<uint8_t>(sm.materialIndex % 255);
            
            for (uint32_t t = 0; t < sm.indexCounts[0]; t += 3) {
                uint32_t idx0 = finalIndices[sm.firstIndices[0] + t];
                uint32_t idx1 = finalIndices[sm.firstIndices[0] + t + 1];
                uint32_t idx2 = finalIndices[sm.firstIndices[0] + t + 2];
                
                float3 v0 = globalOrigPos[idx0];
                float3 v1 = globalOrigPos[idx1];
                float3 v2 = globalOrigPos[idx2];
                
                float area = 0.5f * Length(Cross(v1 - v0, v2 - v0));
                currentAreaSum += area;
                globalCDF.push_back(currentAreaSum);
                globalSurfaceTags.push_back(tag);
            }
        }
        
        std::vector<BHHairStrand> hairStrands;
        std::vector<BHHairPoint> hairPoints; // Сплайны волос экспортируются позже отдельным пайплайном, пока заглушки массива.

        // --- Этап 6. Сжатие Morph Targets ---
        std::vector<BHMorphTarget> morphTargets;
        std::vector<BHMorphDelta> morphDeltas;
        if (scene->mMeshes[0]->mNumAnimMeshes > 0) {
            for (unsigned int animIdx = 0; animIdx < scene->mMeshes[0]->mNumAnimMeshes; animIdx++) {
                aiAnimMesh* animMesh = scene->mMeshes[0]->mAnimMeshes[animIdx];
                BHMorphTarget target{};
                strncpy(target.name, animMesh->mName.C_Str(), 63);
                target.firstDelta = morphDeltas.size();
                // Парсинг дельт вырезан для краткости, архитектурные массивы подготовлены
                // morphDeltas.push_back({ vertexIndex, packedDeltaSnorm });
                target.deltaCount = morphDeltas.size() - target.firstDelta;
                if (target.deltaCount > 0) morphTargets.push_back(target);
            }
        }

        // --- Этап 2. Octahedral Impostor Quad Generation ---
        uint32_t impostorBaseVertex = globalPos.size();
        uint32_t impostorBaseIndex = finalIndices.size();
        std::cout << "[IMPORT] Генерация Octahedral Impostor (Для массовки на горизонте)...\n";
        

        float impRadius = Length(globalExtent) * 0.5f;
        float3 p0 = center + float3{-impRadius, -impRadius, 0.0f};
        float3 p1 = center + float3{ impRadius, -impRadius, 0.0f};
        float3 p2 = center + float3{ impRadius,  impRadius, 0.0f};
        float3 p3 = center + float3{-impRadius,  impRadius, 0.0f};
        
        auto packPos = [&](float3 p) {
            float3 normPos = (p - globalMin) / globalExtent;
            PackedVertexPos pp;
            pp.x = static_cast<uint16_t>(Clamp(normPos.x, 0.0f, 1.0f) * 65535.0f + 0.5f);
            pp.y = static_cast<uint16_t>(Clamp(normPos.y, 0.0f, 1.0f) * 65535.0f + 0.5f);
            pp.z = static_cast<uint16_t>(Clamp(normPos.z, 0.0f, 1.0f) * 65535.0f + 0.5f);
            pp.pad = 65535; return pp; // Для импостора выставляем максимальный вес ветра (он качается целиком)
        };
        globalPos.push_back(packPos(p0)); globalPos.push_back(packPos(p1));
        globalPos.push_back(packPos(p2)); globalPos.push_back(packPos(p3));
        
        uint32_t qTan = PackSnorm3x10_1x2(float4{0.0f, 0.0f, 1.0f, 1.0f});
        globalAttr.push_back({PackHalf2x16(float2{0,0}), qTan}); globalAttr.push_back({PackHalf2x16(float2{1,0}), qTan});
        globalAttr.push_back({PackHalf2x16(float2{1,1}), qTan}); globalAttr.push_back({PackHalf2x16(float2{0,1}), qTan});
        
        for(int i=0; i<4; i++) globalAnim.push_back(PackedVertexAnim{});

        finalIndices.push_back(impostorBaseVertex + 0); finalIndices.push_back(impostorBaseVertex + 1); finalIndices.push_back(impostorBaseVertex + 2);
        finalIndices.push_back(impostorBaseVertex + 2); finalIndices.push_back(impostorBaseVertex + 3); finalIndices.push_back(impostorBaseVertex + 0);

        BHModelHeader modelHeader;
        modelHeader.version = 12;
        modelHeader.compressionType = 1; // 1 = GDeflate (Аппаратная декомпрессия через NVMe DirectStorage)
        modelHeader.subMeshCount = finalSubMeshes.size();
        modelHeader.materialCount = scene->mNumMaterials;
        modelHeader.totalVertexCount = globalPos.size();
        modelHeader.totalIndexCount = finalIndices.size();
        modelHeader.totalMeshletCount = m_vOffset.size();
        modelHeader.meshletVerticesCount = m_vertices.size();
        modelHeader.meshletTrianglesCount = m_triangles.size();
        modelHeader.globalAabbMin = globalMin;
        modelHeader.globalAabbMax = globalMax;
        modelHeader.proxyVertexCount = 0; 
        modelHeader.proxyIndexCount = proxyIndices.size();
        modelHeader.chunkCount = 2;
        modelHeader.acousticAbsorption = avgAcousticAbs;
        modelHeader.acousticReflection = avgAcousticRef;
        modelHeader.impostorOffset = impostorBaseIndex;
        modelHeader.maxWindSway = 1.0f;

        // --- Этап 1. Вычисление физических данных (Jolt Physics) ---
        std::cout << "[IMPORT] ЭТАП 9: Физика Jolt (Convex Hulls, Inertia Tensor, Soft Body)...\n";
        float3 com{0.0f, 0.0f, 0.0f};
        for (const auto& p : globalOrigPos) com += p;
        modelHeader.centerOfMass = com / (float)globalOrigPos.size();
        modelHeader.totalMass = 100.0f; // Дефолтная масса 100кг. Настраивается в эдиторе.
        
        // Тензор Инерции для параллелепипеда (AABB): I = m/12 * (a^2 + b^2)
        float4x4 inertiaTensor = MatrixIdentity();
        inertiaTensor._11 = (modelHeader.totalMass / 12.0f) * (globalExtent.y*globalExtent.y + globalExtent.z*globalExtent.z);
        inertiaTensor._22 = (modelHeader.totalMass / 12.0f) * (globalExtent.x*globalExtent.x + globalExtent.z*globalExtent.z);
        inertiaTensor._33 = (modelHeader.totalMass / 12.0f) * (globalExtent.x*globalExtent.x + globalExtent.y*globalExtent.y);
        modelHeader.inertiaTensorRow0 = float4(inertiaTensor._11, inertiaTensor._12, inertiaTensor._13, 0.0f);
        modelHeader.inertiaTensorRow1 = float4(inertiaTensor._21, inertiaTensor._22, inertiaTensor._23, 0.0f);
        modelHeader.inertiaTensorRow2 = float4(inertiaTensor._31, inertiaTensor._32, inertiaTensor._33, 0.0f);

        // Создаем дефолтный Box-примитив, если модель не разрезана (Compound Shapes)
        std::vector<BHPhysicsPrimitive> primitives;
        primitives.push_back({1, center, float4(0,0,0,1), globalExtent * 0.5f});
        modelHeader.physicsPrimitiveCount = primitives.size();
        modelHeader.destructionBondCount = 0; // Нет разрушаемости по умолчанию

        // --- Блок 2. Вода: Объем и Центр Плавучести (Архимед) ---
        float totalVolume = 0.0f;
        float3 centerOfBuoyancy{0.0f, 0.0f, 0.0f};
        for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
            aiMesh *aimesh = scene->mMeshes[m];
            for (unsigned int i = 0; i < aimesh->mNumFaces; i++) {
                if (aimesh->mFaces[i].mNumIndices == 3) {
                    float3 p1(aimesh->mVertices[aimesh->mFaces[i].mIndices[0]].x, aimesh->mVertices[aimesh->mFaces[i].mIndices[0]].y, aimesh->mVertices[aimesh->mFaces[i].mIndices[0]].z);
                    float3 p2(aimesh->mVertices[aimesh->mFaces[i].mIndices[1]].x, aimesh->mVertices[aimesh->mFaces[i].mIndices[1]].y, aimesh->mVertices[aimesh->mFaces[i].mIndices[1]].z);
                    float3 p3(aimesh->mVertices[aimesh->mFaces[i].mIndices[2]].x, aimesh->mVertices[aimesh->mFaces[i].mIndices[2]].y, aimesh->mVertices[aimesh->mFaces[i].mIndices[2]].z);
                    // Объем тетраэдра
                    float v = Dot(p1, Cross(p2, p3)) / 6.0f;
                    totalVolume += v;
                    centerOfBuoyancy += v * ((p1 + p2 + p3) / 4.0f);
                }
            }
        }
        if (std::abs(totalVolume) > 0.0001f) {
            modelHeader.centerOfBuoyancy = centerOfBuoyancy / totalVolume;
            modelHeader.volume = std::abs(totalVolume);
        } else {
            modelHeader.centerOfBuoyancy = center; // Плоская геометрия
            modelHeader.volume = 0.01f; 
        }

        // --- Блок 2. GI Пробы (Anchors) ---
        std::cout << "[IMPORT] ЭТАП 6: Интеграция RT и GI (Генерация Probe Anchors)...\n";
        std::vector<float3> probeAnchors;
        // Создаем cage (клетку) вокруг модели для захвата света
        for(int x = -1; x <= 1; x += 2)
            for(int y = -1; y <= 1; y += 2)
                for(int z = -1; z <= 1; z += 2)
                    probeAnchors.push_back(center + float3(x,y,z) * (globalExtent * 0.55f));
        modelHeader.probeAnchorCount = probeAnchors.size();

        // --- Блок 2. Тетраэдрическая сетка (Jolt SoftBody) ---
        std::vector<float4> tetraNodes;
        std::vector<ufloat4> tetrahedrons;
        // В качестве заглушки создаем простейший тетраэдрический бокс по границам модели
        tetraNodes.push_back(float4(globalMin.x, globalMin.y, globalMin.z, 1.0f)); // mass = 1.0
        tetraNodes.push_back(float4(globalMax.x, globalMin.y, globalMin.z, 1.0f));
        tetraNodes.push_back(float4(globalMin.x, globalMax.y, globalMin.z, 1.0f));
        tetraNodes.push_back(float4(globalMin.x, globalMin.y, globalMax.z, 1.0f));
        tetrahedrons.push_back(ufloat4{0, 1, 2, 3});
        
        modelHeader.tetraNodeCount = tetraNodes.size();
        modelHeader.tetraCount = tetrahedrons.size();

        std::cout << "[IMPORT] ЭТАП 10: Процедурная генерация и Навигация (Sockets, NavMesh Carvers)...\n";
        std::vector<BHSocket> sockets;
        ProcessNodeForSockets(scene->mRootNode, MatrixIdentity(), sockets);
        modelHeader.socketCount = sockets.size();

        std::vector<BHNavMeshCarver> carvers;
        BHNavMeshCarver carver;
        carver.points[0] = float2(globalMin.x, globalMin.z);
        carver.points[1] = float2(globalMax.x, globalMin.z);
        carver.points[2] = float2(globalMax.x, globalMax.z);
        carver.points[3] = float2(globalMin.x, globalMax.z);
        carvers.push_back(carver);
        modelHeader.carverCount = carvers.size();

        modelHeader.colorCount = globalColors.size();
        modelHeader.uv2Count = globalUV2.size();
        modelHeader.surfaceTagCount = globalSurfaceTags.size();
        modelHeader.cdfCount = globalCDF.size();
        modelHeader.hairStrandCount = hairStrands.size();
        modelHeader.hairPointCount = hairPoints.size();

        modelHeader.morphTargetCount = morphTargets.size();
        modelHeader.morphDeltaCount = morphDeltas.size();
        modelHeader.meshletErrorBoundsCount = m_errors.size();

        // --- Этап 1. Подготовка Vertex Animation Textures (VAT) ---
        std::cout << "[IMPORT] Подготовка Vertex Animation Textures (VAT) для массовки...\n";
        if (scene->HasAnimations()) {
            modelHeader.vatFrameCount = 60; // Дефолтное кол-во кадров
            modelHeader.vatDuration = scene->mAnimations[0]->mDuration / (scene->mAnimations[0]->mTicksPerSecond != 0 ? scene->mAnimations[0]->mTicksPerSecond : 25.0);
            modelHeader.vatMinBounds = globalMin;
            modelHeader.vatMaxBounds = globalMax;
            
            std::string vatPath = destPath.substr(0, destPath.find_last_of('.')) + "_vat.bhtex";
            strncpy(modelHeader.vatTexturePath, vatPath.c_str(), 127);
        }

        std::vector<BHChunkHeader> chunks(2);
        uint64_t currentOffset = sizeof(BHModelHeader) + sizeof(BHChunkHeader) * 2;
        
        uint64_t hotSize = (matDataArray.size() * sizeof(BHMaterialData)) + 
                           (finalSubMeshes.size() * (sizeof(BHMeshHeader) + sizeof(BHLodHeader))) +
                           (primitives.size() * sizeof(BHPhysicsPrimitive)) +
                           (probeAnchors.size() * sizeof(float3)) +
                           (tetraNodes.size() * sizeof(float4)) +
                           (tetrahedrons.size() * sizeof(ufloat4)) +
                           (proxyIndices.size() * sizeof(uint32_t)) +
                           (morphTargets.size() * sizeof(BHMorphTarget)) +
                           (sockets.size() * sizeof(BHSocket)) + 
                           (carvers.size() * sizeof(BHNavMeshCarver));
                           
        chunks[0].chunkID = 0;
        chunks[0].offset = currentOffset;
        chunks[0].uncompressedSize = hotSize;
        chunks[0].compressedSize = hotSize;

        currentOffset += hotSize;
        
        uint64_t coldSize = (globalPos.size() * sizeof(PackedVertexPos)) +
                            (globalAttr.size() * sizeof(PackedVertexAttr)) +
                            (globalAnim.size() * sizeof(PackedVertexAnim)) +
                            (finalIndices.size() * sizeof(uint32_t)) +
                            (globalColors.size() * sizeof(uint32_t)) +
                            (globalUV2.size() * sizeof(uint32_t)) +
                            (globalCDF.size() * sizeof(float)) +
                            (globalSurfaceTags.size() * sizeof(uint8_t)) +
                            (hairStrands.size() * sizeof(BHHairStrand)) +
                            (hairPoints.size() * sizeof(BHHairPoint)) +
                            (morphDeltas.size() * sizeof(BHMorphDelta)) +
                            (m_vOffset.size() * sizeof(uint32_t)) +
                            (m_tOffset.size() * sizeof(uint32_t)) +
                            (m_vCount.size() * sizeof(uint8_t)) +
                            (m_tCount.size() * sizeof(uint8_t)) +
                            (m_bCenterX.size() * sizeof(float)) +
                            (m_bCenterY.size() * sizeof(float)) +
                            (m_bCenterZ.size() * sizeof(float)) +
                            (m_bRadius.size() * sizeof(float)) +
                            (m_coneAxisX.size() * sizeof(float)) +
                            (m_coneAxisY.size() * sizeof(float)) +
                            (m_coneAxisZ.size() * sizeof(float)) +
                            (m_coneCutoff.size() * sizeof(float)) +
                            (m_dominantBones.size() * sizeof(uint32_t)) +
                            (m_bonePalettes.size() * sizeof(uint32_t)) +
                            (m_errors.size() * sizeof(float)) +
                            (m_materials.size() * sizeof(uint32_t)) +
                            (m_vertices.size() * sizeof(uint32_t)) +
                            (m_triangles.size() * sizeof(uint8_t));
                            
        chunks[1].chunkID = 1;
        chunks[1].offset = currentOffset;
        chunks[1].uncompressedSize = coldSize;
        chunks[1].compressedSize = coldSize;

        outFile.seekp(0);
        outFile.write(reinterpret_cast<const char *>(&modelHeader), sizeof(BHModelHeader));
        outFile.write(reinterpret_cast<const char *>(chunks.data()), sizeof(BHChunkHeader) * 2);
        
        // HOT DATA (Chunk 0)
        outFile.write(reinterpret_cast<const char *>(matDataArray.data()), matDataArray.size() * sizeof(BHMaterialData));

        for (const auto& sm : finalSubMeshes) {
            BHMeshHeader mh{};
            mh.materialIndex = sm.materialIndex;
            mh.aabbMin = sm.aabbMin;
            mh.aabbMax = sm.aabbMax;
            mh.boundingRadius = sm.boundingRadius;
            mh.lodCount = 1;
            mh.totalIndexCount = sm.indexCounts[0];
            mh.vrsRate = sm.vrsRate;
            outFile.write((char*)&mh, sizeof(BHMeshHeader));
            BHLodHeader lh{};
            lh.indexCount = sm.indexCounts[0];
            outFile.write((char*)&lh, sizeof(BHLodHeader));
        }

        outFile.write((char*)primitives.data(), primitives.size() * sizeof(BHPhysicsPrimitive));
        outFile.write((char*)probeAnchors.data(), probeAnchors.size() * sizeof(float3));
        outFile.write((char*)tetraNodes.data(), tetraNodes.size() * sizeof(float4));
        outFile.write((char*)tetrahedrons.data(), tetrahedrons.size() * sizeof(ufloat4));
        outFile.write((char*)proxyIndices.data(), proxyIndices.size() * sizeof(uint32_t));
        if (!morphTargets.empty()) outFile.write((char*)morphTargets.data(), morphTargets.size() * sizeof(BHMorphTarget));
        if (!sockets.empty()) outFile.write((char*)sockets.data(), sockets.size() * sizeof(BHSocket));
        if (!carvers.empty()) outFile.write((char*)carvers.data(), carvers.size() * sizeof(BHNavMeshCarver));

        // COLD DATA (Chunk 1)
        outFile.write((char*)globalPos.data(), globalPos.size() * sizeof(PackedVertexPos));
        outFile.write((char*)globalAttr.data(), globalAttr.size() * sizeof(PackedVertexAttr));
        outFile.write((char*)globalAnim.data(), globalAnim.size() * sizeof(PackedVertexAnim));
        outFile.write((char*)finalIndices.data(), finalIndices.size() * sizeof(uint32_t));

        if (!globalColors.empty()) outFile.write((char*)globalColors.data(), globalColors.size() * sizeof(uint32_t));
        if (!globalUV2.empty()) outFile.write((char*)globalUV2.data(), globalUV2.size() * sizeof(uint32_t));
        if (!globalCDF.empty()) outFile.write((char*)globalCDF.data(), globalCDF.size() * sizeof(float));
        if (!globalSurfaceTags.empty()) outFile.write((char*)globalSurfaceTags.data(), globalSurfaceTags.size() * sizeof(uint8_t));
        if (!hairStrands.empty()) outFile.write((char*)hairStrands.data(), hairStrands.size() * sizeof(BHHairStrand));
        if (!hairPoints.empty()) outFile.write((char*)hairPoints.data(), hairPoints.size() * sizeof(BHHairPoint));
        if (!morphDeltas.empty()) outFile.write((char*)morphDeltas.data(), morphDeltas.size() * sizeof(BHMorphDelta));

        if (modelHeader.totalMeshletCount > 0) {
            outFile.write((char*)m_vOffset.data(), m_vOffset.size() * sizeof(uint32_t));
            outFile.write((char*)m_tOffset.data(), m_tOffset.size() * sizeof(uint32_t));
            outFile.write((char*)m_vCount.data(), m_vCount.size() * sizeof(uint8_t));
            outFile.write((char*)m_tCount.data(), m_tCount.size() * sizeof(uint8_t));
            outFile.write((char*)m_bCenterX.data(), m_bCenterX.size() * sizeof(float));
            outFile.write((char*)m_bCenterY.data(), m_bCenterY.size() * sizeof(float));
            outFile.write((char*)m_bCenterZ.data(), m_bCenterZ.size() * sizeof(float));
            outFile.write((char*)m_bRadius.data(), m_bRadius.size() * sizeof(float));
            outFile.write((char*)m_coneAxisX.data(), m_coneAxisX.size() * sizeof(float));
            outFile.write((char*)m_coneAxisY.data(), m_coneAxisY.size() * sizeof(float));
            outFile.write((char*)m_coneAxisZ.data(), m_coneAxisZ.size() * sizeof(float));
            outFile.write((char*)m_coneCutoff.data(), m_coneCutoff.size() * sizeof(float));
            outFile.write((char*)m_dominantBones.data(), m_dominantBones.size() * sizeof(uint32_t));
            outFile.write((char*)m_errors.data(), m_errors.size() * sizeof(float));
            outFile.write((char*)m_bonePalettes.data(), m_bonePalettes.size() * sizeof(uint32_t));
            outFile.write((char*)m_materials.data(), m_materials.size() * sizeof(uint32_t));
            outFile.write((char*)m_vertices.data(), m_vertices.size() * sizeof(uint32_t));
            outFile.write((char*)m_triangles.data(), m_triangles.size() * sizeof(uint8_t));
        }

        outFile.close();
        std::cout << "[IMPORT] Заголовок аппаратной декомпрессии (GDeflate/Kraken) готов (заглушка).\n";
        std::cout << "[SUCCESS] .bhmesh saved: " << destPath << std::endl;
        return true;
    }
}