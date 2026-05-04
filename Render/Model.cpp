    #include "Model.hpp"
    #include "../Utils/Utils.hpp"

    
    #define GLM_ENABLE_EXPERIMENTAL
    #include <glm/gtx/hash.hpp>

    
    #include <cassert>
    #include <cstring>
    #include <fstream>
    #include <iostream>

    #ifndef ENGINE_DIR
    #define ENGINE_DIR "../"
    #endif
    #include "ModelImporter.h"

    #include <filesystem>

    namespace burnhope {

        


        BurnhopeModel::~BurnhopeModel() {}

    
    std::unique_ptr<BurnhopeModel> BurnhopeModel::createModelFromFile(BurnhopeDevice& device, const std::string& filepath) {
        std::string finalPath = ENGINE_DIR + filepath;

        
        if (finalPath.ends_with(".obj") || finalPath.ends_with(".fbx") || finalPath.ends_with(".gltf")) {
            std::string bhmeshPath = finalPath.substr(0, finalPath.find_last_of('.')) + ".bhmesh";
            if (!std::filesystem::exists(bhmeshPath)) {
                std::cout << "Вижу новую модель! Конвертирую в .bhmesh...\n";
                ModelImporter::ImportModel(finalPath, bhmeshPath);
            }
            finalPath = bhmeshPath;
        }

        Builder builder{};
        
        builder.modelDir = finalPath.substr(0, finalPath.find_last_of('/') + 1);
        builder.loadModel(finalPath);

        return std::make_unique<BurnhopeModel>(device, builder);
    }


    
    BurnhopeModel::BurnhopeModel(BurnhopeDevice& device, const Builder& builder) : lveDevice{ device } {
        createVertexBuffers(builder.vertices);
        createIndexBuffers(builder.indices);
        this->subMeshes = builder.subMeshes;

        
    
        for (size_t i = 0; i < builder.materialPaths.size(); ++i) {
        const auto& paths = builder.materialPaths[i];
        auto mat = std::make_shared<Material>();

        std::cout << "Загрузка текстур для материала " << i << ":\n";

        
        auto resolvePath = [&](const std::string& rawPath) -> std::string {
            std::filesystem::path p(rawPath);
            
            
            if (std::filesystem::exists(p)) {
                return p.string();
            }
            
            
            
            std::filesystem::path localPath = std::filesystem::path(builder.modelDir) / p.filename();
            if (std::filesystem::exists(localPath)) {
                return localPath.string();
            }

            
            
            std::filesystem::path texFolder = std::filesystem::path(builder.modelDir).parent_path() / "textures" / p.filename();
            if (std::filesystem::exists(texFolder)) {
                return texFolder.string();
            }

            
            return localPath.string();
        };

        if (!paths.albedo.empty() && paths.albedo[0] != '*') {
            std::string fullPath = resolvePath(paths.albedo);
            std::cout << "  -> Albedo: " << fullPath << "\n";
            
            try {
                mat->setAlbedo(BurnhopeTexture::createTextureFromFile(lveDevice, fullPath));
            } catch (const std::exception& e) {
                std::cerr << "[WARNING] Не удалось загрузить Albedo: " << fullPath << "\n";
            }
        }
        
        if (!paths.normal.empty() && paths.normal[0] != '*') {
            std::string fullPath = resolvePath(paths.normal);
            std::cout << "  -> Normal: " << fullPath << "\n";
            
            try {
                mat->setNormal(BurnhopeTexture::createDataTextureFromFile(lveDevice, fullPath));
            } catch (const std::exception& e) {
                std::cerr << "[WARNING] Не удалось загрузить Normal: " << fullPath << "\n";
            }
        }
        
        if (!paths.orm.empty() && paths.orm[0] != '*') {
            std::string fullPath = resolvePath(paths.orm);
            std::cout << "  -> ORM: " << fullPath << "\n";
            
            try {
                std::shared_ptr<BurnhopeTexture> ormTex = BurnhopeTexture::createDataTextureFromFile(lveDevice, fullPath);
                if (ormTex) {
                    mat->setRoughness(ormTex); 
                    mat->setMetallic(ormTex);
                    mat->setAO(ormTex);
                    mat->isORM = true; 
                }
            } catch (const std::exception& e) {
                std::cerr << "[WARNING] Не удалось загрузить ORM: " << fullPath << "\n";
            }
        }

        loadedMaterials.push_back(mat);
    }
    }
    void BurnhopeModel::createVertexBuffers(const std::vector<Vertex>& vertices) {
            vertexCount = static_cast<uint32_t>(vertices.size());
            assert(vertexCount >= 3 && "Vertex count must be at least 3");
            VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
            uint32_t vertexSize = sizeof(vertices[0]);

            BurnhopeBuffer stagingBuffer{
                lveDevice,
                vertexSize,
                vertexCount,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            };


            stagingBuffer.map();
            stagingBuffer.writeToBuffer((void*)vertices.data());

            vertexBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice,
                vertexSize,
                vertexCount,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);


            lveDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
        }

        void BurnhopeModel::createIndexBuffers(const std::vector<uint32_t>& indices) {
            indexCount = static_cast<uint32_t>(indices.size());
            hasIndexBuffer = indexCount > 0;

            if (!hasIndexBuffer) {
                return;
            }

            VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
            uint32_t indexSize = sizeof(indices[0]);

            BurnhopeBuffer stagingBuffer{
                lveDevice,
                indexSize,
                indexCount,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            };


            stagingBuffer.map();
            stagingBuffer.writeToBuffer((void*)indices.data());

            indexBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice,
                indexSize,
                indexCount,
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);


            lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
        }
        void BurnhopeModel::bind(VkCommandBuffer commandBuffer) {
            VkBuffer buffers[] = { vertexBuffer->getBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

            if (hasIndexBuffer) {
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
            }
        }
        void BurnhopeModel::draw(VkCommandBuffer commandBuffer) {
            if (hasIndexBuffer) {
                vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
            }
            else {
                vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
            }
        }
        std::vector<VkVertexInputBindingDescription> Vertex::getBindingDescriptions() {
            std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
            bindingDescriptions[0].binding = 0;
            bindingDescriptions[0].stride = sizeof(Vertex);
            bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescriptions;
        }

        std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions() {
            std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

            
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, position);

            
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texUV);

            
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, tangent);

            
            attributeDescriptions[4].binding = 0;
            attributeDescriptions[4].location = 4;
            attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[4].offset = offsetof(Vertex, bitangent);

            return attributeDescriptions;
        }

        
        
        
        void Builder::loadModel(const std::string& filepath) {
            if (!filepath.ends_with(".bhmesh")) {
                throw std::runtime_error("[ERROR] Only .bhmesh files are supported! " + filepath);
            }

            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("[ERROR] Failed to open .bhmesh: " + filepath);
            }

            
            BHModelHeader header;
            file.read(reinterpret_cast<char*>(&header), sizeof(BHModelHeader));

            if (std::string(header.magic, 4) != "BHMD") {
                throw std::runtime_error("[ERROR] Not a valid BurnHope Model file!");
            }

            
        materialPaths.clear();
        for (uint32_t i = 0; i < header.materialCount; ++i) {
            BHMaterialData matData;
            file.read(reinterpret_cast<char*>(&matData), sizeof(BHMaterialData));
            
            MaterialPaths paths;
            paths.albedo = matData.albedoPath;
            paths.normal = matData.normalPath;
            paths.orm = matData.ormPath;
            materialPaths.push_back(paths);
        }

            vertices.clear();
            indices.clear();
            subMeshes.clear(); 
    uint32_t lastMeshVertexOffset = 0; 

        for (uint32_t m = 0; m < header.meshCount; ++m) {
            BHMeshHeader meshHeader;
            file.read(reinterpret_cast<char*>(&meshHeader), sizeof(BHMeshHeader));

            uint32_t vCount;
            file.read(reinterpret_cast<char*>(&vCount), sizeof(uint32_t));

            if (vCount > 10000000) {
                throw std::runtime_error("[FATAL] Mesh too large!");
            }

            uint32_t vertexOffset;

            if (vCount > 0) {
                
                vertexOffset = static_cast<uint32_t>(vertices.size());
                lastMeshVertexOffset = vertexOffset; 

                std::vector<Vertex> subVertices(vCount);
                file.read(reinterpret_cast<char*>(subVertices.data()), vCount * sizeof(Vertex));
                vertices.insert(vertices.end(), subVertices.begin(), subVertices.end());
            } else {
                
                vertexOffset = lastMeshVertexOffset;
                
            }
            SubMesh sub{};
            sub.materialIndex = meshHeader.materialIndex;
            sub.aabbMin = meshHeader.aabbMin;
            sub.aabbMax = meshHeader.aabbMax;
            sub.boundingRadius = meshHeader.boundingRadius;
            sub.lodCount = std::min(meshHeader.lodCount, 4u); 
            for (uint32_t l = 0; l < meshHeader.lodCount; ++l) {
                BHLodHeader lodHeader;
                file.read(reinterpret_cast<char*>(&lodHeader), sizeof(BHLodHeader));

                if (l < 4) { 
                        sub.indexCounts[l]  = lodHeader.indexCount;
                        sub.firstIndices[l] = static_cast<uint32_t>(indices.size());

                        
                        if (meshHeader.indexType == 0) {
                            std::vector<uint16_t> idx16(lodHeader.indexCount);
                            file.read(reinterpret_cast<char*>(idx16.data()), lodHeader.indexCount * sizeof(uint16_t));
                            for (uint16_t idx : idx16) indices.push_back(static_cast<uint32_t>(idx) + vertexOffset);
                        } else {
                            std::vector<uint32_t> idx32(lodHeader.indexCount);
                            file.read(reinterpret_cast<char*>(idx32.data()), lodHeader.indexCount * sizeof(uint32_t));
                            for (uint32_t idx : idx32) indices.push_back(idx + vertexOffset);
                        }
                    } else {
                        
                        uint32_t indexSize = (meshHeader.indexType == 0) ? 2 : 4;
                        file.seekg(lodHeader.indexCount * indexSize, std::ios::cur);
                    }
            }
            subMeshes.push_back(sub);
        }

        file.close();
        std::cout << "[SUCCESS] Loaded " << filepath << ": "
                << vertices.size() << " verts, "
                << subMeshes.size() << " submeshes\n";
    }
    }