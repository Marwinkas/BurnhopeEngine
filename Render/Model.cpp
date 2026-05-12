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
namespace burnhope
{
    BurnhopeModel::~BurnhopeModel() {
        if (blasHandle != VK_NULL_HANDLE) {
            auto pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkDestroyAccelerationStructureKHR");
            if (pfnDestroyAccelerationStructureKHR) {
                pfnDestroyAccelerationStructureKHR(lveDevice.device(), blasHandle, nullptr);
            }
        }
    }
    std::unique_ptr<BurnhopeModel> BurnhopeModel::createModelFromFile(BurnhopeDevice &device, const std::string &filepath)
    {
        
        std::string finalPath = filepath;
        if (!std::filesystem::exists(finalPath)) {
            finalPath = std::string(ENGINE_DIR) + filepath;
        }
        if (finalPath.ends_with(".obj") || finalPath.ends_with(".fbx") || finalPath.ends_with(".gltf"))
        {
            std::string bhmeshPath = finalPath.substr(0, finalPath.find_last_of('.')) + ".bhmesh";
            if (!std::filesystem::exists(bhmeshPath))
            {
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
    BurnhopeModel::BurnhopeModel(BurnhopeDevice &device, const Builder &builder) : lveDevice{device}
    {
        createVertexBuffers(builder.vertices);
        createIndexBuffers(builder.indices);
        
        this->subMeshes = builder.subMeshes;
        createBLAS();

       /* for (const auto& paths : builder.materialPaths) {
        auto mat = std::make_shared<Material>();
        
        // Загружаем основной цвет (Albedo)
        if (!paths.albedo.empty()) {
            std::string fullPath = builder.modelDir + paths.albedo;
            mat->setAlbedo(BurnhopeTexture::createTextureFromFile(lveDevice, fullPath), paths.albedo);
        }
        
        // Загружаем рельеф (Normal)
        if (!paths.normal.empty()) {
            std::string fullPath = builder.modelDir + paths.normal;
            mat->setNormal(BurnhopeTexture::createDataTextureFromFile(lveDevice, fullPath), paths.normal);
        }
        
        // Обрабатываем ORM (Occlusion, Roughness, Metallic)
        if (!paths.orm.empty()) {
            std::string fullPath = builder.modelDir + paths.orm;
            
            // Включаем флажок для шейдера
            mat->isORM = true; 
            
            // Так как твой шейдер читает ORM из roughnessIdx, 
            // мы бережно кладем нашу картинку именно в этот слот
            mat->setRoughness(BurnhopeTexture::createDataTextureFromFile(lveDevice, fullPath), paths.orm);
        }
        
        // Сохраняем собранный материал в нашу модель
        materials.push_back(mat);
        
    }
    */
       
    }
    void BurnhopeModel::createVertexBuffers(const std::vector<Vertex> &vertices)
    {
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
        stagingBuffer.writeToBuffer((void *)vertices.data());
        vertexBuffer = std::make_unique<BurnhopeBuffer>(
        lveDevice,
        vertexSize,
        vertexCount,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        lveDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
    }
    void BurnhopeModel::createIndexBuffers(const std::vector<uint32_t> &indices)
    {
        indexCount = static_cast<uint32_t>(indices.size());
        hasIndexBuffer = indexCount > 0;
        if (!hasIndexBuffer)
        {
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
        stagingBuffer.writeToBuffer((void *)indices.data());
        indexBuffer = std::make_unique<BurnhopeBuffer>(
        lveDevice,
        indexSize,
        indexCount,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
    }
    void BurnhopeModel::createBLAS()
    {
        // 1. ПУЛЕНЕПРОБИВАЕМАЯ ЗАГРУЗКА ФУНКЦИЙ
        auto pfnCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkCreateAccelerationStructureKHR");
        auto pfnGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkGetAccelerationStructureBuildSizesKHR");
        auto pfnCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkCmdBuildAccelerationStructuresKHR");
        auto pfnGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkGetAccelerationStructureDeviceAddressKHR");

        if (!pfnCreateAccelerationStructureKHR || !pfnGetAccelerationStructureBuildSizesKHR || 
            !pfnCmdBuildAccelerationStructuresKHR || !pfnGetAccelerationStructureDeviceAddressKHR) {
            throw std::runtime_error("[FATAL] Указатели на RT функции равны nullptr! Проверь, добавил ли ты расширения в deviceExtensions в Device.hpp");
        }

        // 2. Получаем адреса буферов (В Vulkan 1.3 это стандартная функция без KHR)
        VkBufferDeviceAddressInfo vertexAddressInfo{};
        vertexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        vertexAddressInfo.buffer = vertexBuffer->getBuffer();
        vertexBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &vertexAddressInfo);

        indexBufferAddress = 0;
        if (hasIndexBuffer) {
            VkBufferDeviceAddressInfo indexAddressInfo{};
            indexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            indexAddressInfo.buffer = indexBuffer->getBuffer();
            indexBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &indexAddressInfo);
        }

        if (vertexBufferAddress == 0) {
            throw std::runtime_error("[FATAL] Адрес буфера вершин = 0!");
        }

        // 3. Подготавливаем описание геометрии
        std::vector<VkAccelerationStructureGeometryKHR> geometries;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges;
        std::vector<uint32_t> maxPrimitiveCounts;

        for (const auto& sub : subMeshes) {
            VkAccelerationStructureGeometryKHR geom{};
            geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geom.geometry.triangles.vertexData.deviceAddress = vertexBufferAddress;
            geom.geometry.triangles.vertexStride = sizeof(Vertex);
            geom.geometry.triangles.maxVertex = vertexCount - 1;
            geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geom.geometry.triangles.indexData.deviceAddress = indexBufferAddress;

            geometries.push_back(geom);

            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = sub.indexCounts[0] / 3; 
            range.primitiveOffset = sub.firstIndices[0] * sizeof(uint32_t); 
            range.firstVertex = 0;
            range.transformOffset = 0;

            buildRanges.push_back(range);
            maxPrimitiveCounts.push_back(range.primitiveCount);
        }

        // 4. Узнаем размер памяти для BLAS
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        pfnGetAccelerationStructureBuildSizesKHR(lveDevice.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, maxPrimitiveCounts.data(), &sizeInfo);

        // 5. Создаем буфер для BLAS
        blasBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeInfo.accelerationStructureSize,
            1,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        // 6. Создаем объект BLAS
        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blasBuffer->getBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (pfnCreateAccelerationStructureKHR(lveDevice.device(), &createInfo, nullptr, &blasHandle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create BLAS!");
        }

        // 7. Scratch буфер
        BurnhopeBuffer scratchBuffer(
            lveDevice,
            sizeInfo.buildScratchSize,
            1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = scratchBuffer.getBuffer();
        buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddress(lveDevice.device(), &scratchAddressInfo);

        // 8. Билд на GPU
        buildInfo.dstAccelerationStructure = blasHandle;
        VkCommandBuffer commandBuffer = lveDevice.beginSingleTimeCommands();
        
        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRanges = buildRanges.data();
        pfnCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pBuildRanges);
        
        lveDevice.endSingleTimeCommands(commandBuffer);

        // 9. Получаем адрес
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = blasHandle;
        blasAddress = pfnGetAccelerationStructureDeviceAddressKHR(lveDevice.device(), &addressInfo);

        std::cout << "[RT] Successfully built BLAS for model!" << std::endl;
        std::cout << "[RT] BLAS built! VertexAddress: " << vertexBufferAddress 
              << " | IndexAddress: " << indexBufferAddress 
              << " | BLAS Address: " << blasAddress << std::endl;
    }
    void BurnhopeModel::bind(VkCommandBuffer commandBuffer)
    {
        VkBuffer buffers[] = {vertexBuffer->getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
        if (hasIndexBuffer)
        {
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }
    }
    void BurnhopeModel::draw(VkCommandBuffer commandBuffer)
    {
        if (hasIndexBuffer)
        {
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
        }
        else
        {
            vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        }
    }
    std::vector<VkVertexInputBindingDescription> Vertex::getBindingDescriptions()
    {
        std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
        bindingDescriptions[0].binding = 0;
        bindingDescriptions[0].stride = sizeof(Vertex);
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescriptions;
    }
    std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions()
    {
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
    void Builder::loadModel(const std::string &filepath)
    {
        if (!filepath.ends_with(".bhmesh"))
        {
            throw std::runtime_error("[ERROR] Only .bhmesh files are supported! " + filepath);
        }
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("[ERROR] Failed to open .bhmesh: " + filepath);
        }
        BHModelHeader header;
        file.read(reinterpret_cast<char *>(&header), sizeof(BHModelHeader));
        if (std::string(header.magic, 4) != "BHMD")
        {
            throw std::runtime_error("[ERROR] Not a valid BurnHope Model file!");
        }
        materialPaths.clear();
        for (uint32_t i = 0; i < header.materialCount; ++i)
        {
            BHMaterialData matData;
            file.read(reinterpret_cast<char *>(&matData), sizeof(BHMaterialData));
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
        for (uint32_t m = 0; m < header.meshCount; ++m)
        {
            BHMeshHeader meshHeader;
            file.read(reinterpret_cast<char *>(&meshHeader), sizeof(BHMeshHeader));
            uint32_t vCount;
            file.read(reinterpret_cast<char *>(&vCount), sizeof(uint32_t));
            if (vCount > 10000000)
            {
                throw std::runtime_error("[FATAL] Mesh too large!");
            }
            uint32_t vertexOffset;
            if (vCount > 0)
            {
                vertexOffset = static_cast<uint32_t>(vertices.size());
                lastMeshVertexOffset = vertexOffset;
                std::vector<Vertex> subVertices(vCount);
                file.read(reinterpret_cast<char *>(subVertices.data()), vCount * sizeof(Vertex));
                vertices.insert(vertices.end(), subVertices.begin(), subVertices.end());
            }
            else
            {
                vertexOffset = lastMeshVertexOffset;
            }
            SubMesh sub{};
            sub.materialIndex = meshHeader.materialIndex;
            sub.aabbMin = meshHeader.aabbMin;
            sub.aabbMax = meshHeader.aabbMax;
            sub.boundingRadius = meshHeader.boundingRadius;
            sub.lodCount = std::min(meshHeader.lodCount, 4u);
            for (uint32_t l = 0; l < meshHeader.lodCount; ++l)
            {
                BHLodHeader lodHeader;
                file.read(reinterpret_cast<char *>(&lodHeader), sizeof(BHLodHeader));
                if (l < 4)
                {
                    sub.indexCounts[l] = lodHeader.indexCount;
                    sub.firstIndices[l] = static_cast<uint32_t>(indices.size());
                    if (meshHeader.indexType == 0)
                    {
                        std::vector<uint16_t> idx16(lodHeader.indexCount);
                        file.read(reinterpret_cast<char *>(idx16.data()), lodHeader.indexCount * sizeof(uint16_t));
                        for (uint16_t idx : idx16)
                            indices.push_back(static_cast<uint32_t>(idx) + vertexOffset);
                    }
                    else
                    {
                        std::vector<uint32_t> idx32(lodHeader.indexCount);
                        file.read(reinterpret_cast<char *>(idx32.data()), lodHeader.indexCount * sizeof(uint32_t));
                        for (uint32_t idx : idx32)
                            indices.push_back(idx + vertexOffset);
                    }
                }
                else
                {
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