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
        if (loadThread.joinable()) loadThread.join();
        vkDeviceWaitIdle(lveDevice.device()); // Защита от удаления старой модели во время рендера
        if (blasHandle != VK_NULL_HANDLE) {
            auto pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkDestroyAccelerationStructureKHR");
            if (pfnDestroyAccelerationStructureKHR) {
                pfnDestroyAccelerationStructureKHR(lveDevice.device(), blasHandle, nullptr);
            }
        }
    }
    std::unique_ptr<BurnhopeModel> BurnhopeModel::createModelFromFile(BurnhopeDevice &device, const std::string &filepath)
    {
        auto model = std::make_unique<BurnhopeModel>(device);
        model->pendingBuilder = std::make_unique<Builder>();
        
        std::string finalPath = filepath;
        if (!std::filesystem::exists(finalPath)) {
            finalPath = std::string(ENGINE_DIR) + filepath;
        }

        model->loadThread = std::thread([model_ptr = model.get(), finalPath]() {
            try {
                std::string actualPath = finalPath;
                if (actualPath.ends_with(".obj") || actualPath.ends_with(".fbx") || actualPath.ends_with(".gltf"))
                {
                    std::string bhmeshPath = actualPath.substr(0, actualPath.find_last_of('.')) + ".bhmesh";
                    if (std::filesystem::exists(bhmeshPath)) {
                        std::ifstream file(bhmeshPath, std::ios::binary);
                        if(file.is_open()){
                            uint32_t magic; file.read((char*)&magic, 4);
                            uint32_t version; file.read((char*)&version, 4);
                            if(version != 12) {
                                file.close();
                                std::filesystem::remove(bhmeshPath);
                            }
                        }
                    }
                    if (!std::filesystem::exists(bhmeshPath))
                    {
                        std::cout << "Вижу новую модель! Конвертирую в .bhmesh в фоне...\n";
                        ModelImporter::ImportModel(actualPath, bhmeshPath);
                    }
                    actualPath = bhmeshPath;
                }
                model_ptr->pendingBuilder->modelDir = actualPath.substr(0, actualPath.find_last_of('/') + 1);
                model_ptr->pendingBuilder->loadModel(actualPath);
            } catch(const std::exception& e) {
                std::cerr << "[ERROR] Async Model Load: " << e.what() << "\n";
            }
            model_ptr->cpuDataReady = true;
        });

        return model;
    }
    BurnhopeModel::BurnhopeModel(BurnhopeDevice &device) : lveDevice{device} {}

    void BurnhopeModel::finishGpuUpload() {
        if (!cpuDataReady || gpuDataReady) return;
        if (loadThread.joinable()) loadThread.join();
        if (!pendingBuilder) {
            gpuDataReady = true;
            return;
        }

        const Builder& builder = *pendingBuilder;
        if (builder.positions.empty()) {
            pendingBuilder.reset();
            gpuDataReady = true;
            return;
        }
        this->storedPositions = builder.positions;

        materialCount = static_cast<uint32_t>(builder.materialPaths.size());
        createVertexBuffers(builder.positions, builder.attributes, builder.animations, builder.isDynamic);
        createIndexBuffers(builder.indices);
        
        this->subMeshes = builder.subMeshes;
        
        VkBufferDeviceAddressInfo vertexAddressInfo{};
        vertexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        vertexAddressInfo.buffer = posBuffer->getBuffer();
        posBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &vertexAddressInfo);

        VkBufferDeviceAddressInfo attrAddressInfo{};
        attrAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        attrAddressInfo.buffer = attrBuffer->getBuffer();
        attrBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &attrAddressInfo);

        VkBufferDeviceAddressInfo animAddressInfo{};
        animAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        animAddressInfo.buffer = animBuffer->getBuffer();
        animBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &animAddressInfo);

        indexBufferAddress = 0;
        if (indexBuffer) {
            VkBufferDeviceAddressInfo indexAddressInfo{};
            indexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            indexAddressInfo.buffer = indexBuffer->getBuffer();
            indexBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &indexAddressInfo);
        }
        
        if (!builder.colors.empty()) {
            colorBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), builder.colors.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), builder.colors.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.colors.data());
            lveDevice.copyBuffer(stg.getBuffer(), colorBuffer->getBuffer(), builder.colors.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = colorBuffer->getBuffer();
            colorBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint32_t> dummy(vertexCount > 0 ? vertexCount : 1, 0xFFFFFFFF);
            colorBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), colorBuffer->getBuffer(), dummy.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = colorBuffer->getBuffer();
            colorBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.uv2.empty()) {
            uv2Buffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), builder.uv2.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), builder.uv2.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.uv2.data());
            lveDevice.copyBuffer(stg.getBuffer(), uv2Buffer->getBuffer(), builder.uv2.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = uv2Buffer->getBuffer();
            uv2BufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint32_t> dummy(vertexCount > 0 ? vertexCount : 1, 0);
            uv2Buffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), uv2Buffer->getBuffer(), dummy.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = uv2Buffer->getBuffer();
            uv2BufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.cdfs.empty()) {
            cdfBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(float), builder.cdfs.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(float), builder.cdfs.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.cdfs.data());
            lveDevice.copyBuffer(stg.getBuffer(), cdfBuffer->getBuffer(), builder.cdfs.size() * sizeof(float));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = cdfBuffer->getBuffer();
            cdfBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<float> dummy(indexCount / 3 > 0 ? indexCount / 3 : 1, 0.0f);
            cdfBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(float), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(float), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), cdfBuffer->getBuffer(), dummy.size() * sizeof(float));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = cdfBuffer->getBuffer();
            cdfBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.surfaceTags.empty()) {
            surfaceTagsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint8_t), builder.surfaceTags.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint8_t), builder.surfaceTags.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.surfaceTags.data());
            lveDevice.copyBuffer(stg.getBuffer(), surfaceTagsBuffer->getBuffer(), builder.surfaceTags.size() * sizeof(uint8_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = surfaceTagsBuffer->getBuffer();
            surfaceTagsBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint8_t> dummy(indexCount / 3 > 0 ? indexCount / 3 : 1, 0);
            surfaceTagsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint8_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint8_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), surfaceTagsBuffer->getBuffer(), dummy.size() * sizeof(uint8_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = surfaceTagsBuffer->getBuffer();
            surfaceTagsBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.hairStrands.empty()) {
            hairStrandsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(BHHairStrand), builder.hairStrands.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(BHHairStrand), builder.hairStrands.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.hairStrands.data());
            lveDevice.copyBuffer(stg.getBuffer(), hairStrandsBuffer->getBuffer(), builder.hairStrands.size() * sizeof(BHHairStrand));
        }
        if (!builder.hairPoints.empty()) {
            hairPointsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(BHHairPoint), builder.hairPoints.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(BHHairPoint), builder.hairPoints.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.hairPoints.data());
            lveDevice.copyBuffer(stg.getBuffer(), hairPointsBuffer->getBuffer(), builder.hairPoints.size() * sizeof(BHHairPoint));
        }

        acousticAbsorption = builder.acousticAbsorption;
        acousticReflection = builder.acousticReflection;
        impostorOffset = builder.impostorOffset;
        maxWindSway = builder.maxWindSway;
        centerOfMass = builder.centerOfMass;
        totalMass = builder.totalMass;
        inertiaTensor = builder.inertiaTensor;
        physicsPrimitives = builder.physicsPrimitives;
        destructionBonds = builder.destructionBonds;
        centerOfBuoyancy = builder.centerOfBuoyancy;
        volume = builder.volume;
        probeAnchors = builder.probeAnchors;
        tetraNodes = builder.tetraNodes;
        tetrahedrons = builder.tetrahedrons;
        sockets = builder.sockets;
        navMeshCarvers = builder.navMeshCarvers;
        morphTargets = builder.morphTargets;
        morphDeltas = builder.morphDeltas;
        vatTexturePath = builder.vatTexturePath;
        vatFrameCount = builder.vatFrameCount;
        vatDuration = builder.vatDuration;
        vatMinBounds = builder.vatMinBounds;
        vatMaxBounds = builder.vatMaxBounds;
        globalAabbMin = builder.globalAabbMin;
        globalAabbMax = builder.globalAabbMax;

        if (builder.buildRT) {
            createBLAS(builder.positions);
        }
        pendingBuilder.reset();
        gpuDataReady = true;
    }

    BurnhopeModel::BurnhopeModel(BurnhopeDevice &device, const Builder &builder) : lveDevice{device}
    {
        cpuDataReady = true;
        gpuDataReady = true;
        this->storedPositions = builder.positions;
        materialCount = static_cast<uint32_t>(builder.materialPaths.size());
        createVertexBuffers(builder.positions, builder.attributes, builder.animations, builder.isDynamic);
        createIndexBuffers(builder.indices);
        
        this->subMeshes = builder.subMeshes;
        
        VkBufferDeviceAddressInfo vertexAddressInfo{};
        vertexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        vertexAddressInfo.buffer = posBuffer->getBuffer();
        posBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &vertexAddressInfo);

        VkBufferDeviceAddressInfo attrAddressInfo{};
        attrAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        attrAddressInfo.buffer = attrBuffer->getBuffer();
        attrBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &attrAddressInfo);

        VkBufferDeviceAddressInfo animAddressInfo{};
        animAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        animAddressInfo.buffer = animBuffer->getBuffer();
        animBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &animAddressInfo);

        indexBufferAddress = 0;
        if (indexBuffer) {
            VkBufferDeviceAddressInfo indexAddressInfo{};
            indexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            indexAddressInfo.buffer = indexBuffer->getBuffer();
            indexBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &indexAddressInfo);
        }
        
        if (!builder.colors.empty()) {
            colorBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), builder.colors.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), builder.colors.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.colors.data());
            lveDevice.copyBuffer(stg.getBuffer(), colorBuffer->getBuffer(), builder.colors.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = colorBuffer->getBuffer();
            colorBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint32_t> dummy(vertexCount > 0 ? vertexCount : 1, 0xFFFFFFFF);
            colorBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), colorBuffer->getBuffer(), dummy.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = colorBuffer->getBuffer();
            colorBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.uv2.empty()) {
            uv2Buffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), builder.uv2.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), builder.uv2.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.uv2.data());
            lveDevice.copyBuffer(stg.getBuffer(), uv2Buffer->getBuffer(), builder.uv2.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = uv2Buffer->getBuffer();
            uv2BufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint32_t> dummy(vertexCount > 0 ? vertexCount : 1, 0);
            uv2Buffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint32_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), uv2Buffer->getBuffer(), dummy.size() * sizeof(uint32_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = uv2Buffer->getBuffer();
            uv2BufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.cdfs.empty()) {
            cdfBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(float), builder.cdfs.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(float), builder.cdfs.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.cdfs.data());
            lveDevice.copyBuffer(stg.getBuffer(), cdfBuffer->getBuffer(), builder.cdfs.size() * sizeof(float));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = cdfBuffer->getBuffer();
            cdfBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<float> dummy(indexCount / 3 > 0 ? indexCount / 3 : 1, 0.0f);
            cdfBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(float), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(float), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), cdfBuffer->getBuffer(), dummy.size() * sizeof(float));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = cdfBuffer->getBuffer();
            cdfBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.surfaceTags.empty()) {
            surfaceTagsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint8_t), builder.surfaceTags.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint8_t), builder.surfaceTags.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.surfaceTags.data());
            lveDevice.copyBuffer(stg.getBuffer(), surfaceTagsBuffer->getBuffer(), builder.surfaceTags.size() * sizeof(uint8_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = surfaceTagsBuffer->getBuffer();
            surfaceTagsBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        } else {
            std::vector<uint8_t> dummy(indexCount / 3 > 0 ? indexCount / 3 : 1, 0);
            surfaceTagsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(uint8_t), dummy.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(uint8_t), dummy.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)dummy.data());
            lveDevice.copyBuffer(stg.getBuffer(), surfaceTagsBuffer->getBuffer(), dummy.size() * sizeof(uint8_t));
            VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; ai.buffer = surfaceTagsBuffer->getBuffer();
            surfaceTagsBufferAddress = vkGetBufferDeviceAddress(lveDevice.device(), &ai);
        }
        if (!builder.hairStrands.empty()) {
            hairStrandsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(BHHairStrand), builder.hairStrands.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(BHHairStrand), builder.hairStrands.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.hairStrands.data());
            lveDevice.copyBuffer(stg.getBuffer(), hairStrandsBuffer->getBuffer(), builder.hairStrands.size() * sizeof(BHHairStrand));
        }
        if (!builder.hairPoints.empty()) {
            hairPointsBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(BHHairPoint), builder.hairPoints.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            BurnhopeBuffer stg(lveDevice, sizeof(BHHairPoint), builder.hairPoints.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stg.map(); stg.writeToBuffer((void*)builder.hairPoints.data());
            lveDevice.copyBuffer(stg.getBuffer(), hairPointsBuffer->getBuffer(), builder.hairPoints.size() * sizeof(BHHairPoint));
        }

        acousticAbsorption = builder.acousticAbsorption;
        acousticReflection = builder.acousticReflection;
        impostorOffset = builder.impostorOffset;
        maxWindSway = builder.maxWindSway;
        centerOfMass = builder.centerOfMass;
        totalMass = builder.totalMass;
        inertiaTensor = builder.inertiaTensor;
        physicsPrimitives = builder.physicsPrimitives;
        destructionBonds = builder.destructionBonds;
        centerOfBuoyancy = builder.centerOfBuoyancy;
        volume = builder.volume;
        probeAnchors = builder.probeAnchors;
        tetraNodes = builder.tetraNodes;
        tetrahedrons = builder.tetrahedrons;
        sockets = builder.sockets;
        navMeshCarvers = builder.navMeshCarvers;
        morphTargets = builder.morphTargets;
        morphDeltas = builder.morphDeltas;
        vatTexturePath = builder.vatTexturePath;
        vatFrameCount = builder.vatFrameCount;
        vatDuration = builder.vatDuration;
        vatMinBounds = builder.vatMinBounds;
        vatMaxBounds = builder.vatMaxBounds;
        globalAabbMin = builder.globalAabbMin;
        globalAabbMax = builder.globalAabbMax;

        if (builder.buildRT) {
            createBLAS(builder.positions);
        }

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
    void BurnhopeModel::createVertexBuffers(const std::vector<PackedVertexPos> &positions, const std::vector<PackedVertexAttr>& attributes, const std::vector<PackedVertexAnim>& animations, bool isDynamic)
    {
        vertexCount = static_cast<uint32_t>(positions.size());
        assert(vertexCount >= 3 && "Vertex count must be at least 3");
        VkDeviceSize posBufferSize = sizeof(positions[0]) * vertexCount;
        VkDeviceSize attrBufferSize = sizeof(PackedVertexAttr) * vertexCount;
        VkDeviceSize animBufferSize = sizeof(PackedVertexAnim) * vertexCount;

        std::vector<PackedVertexAttr> dummyAttr;
        const void* attrData = attributes.data();
        if (attributes.size() < vertexCount) {
            dummyAttr.resize(vertexCount);
            attrData = dummyAttr.data();
        }

        std::vector<PackedVertexAnim> dummyAnim;
        const void* animData = animations.data();
        if (animations.size() < vertexCount) {
            dummyAnim.resize(vertexCount);
            animData = dummyAnim.data();
        }

         if (isDynamic) {
            posBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice,
                sizeof(positions[0]),
                vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            posBuffer->map();
            posBuffer->writeToBuffer((void *)positions.data());

            attrBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice,
                sizeof(attributes[0]),
                vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            attrBuffer->map();
            attrBuffer->writeToBuffer((void *)attrData);

            animBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice,
                sizeof(PackedVertexAnim),
                vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            animBuffer->map();
            animBuffer->writeToBuffer((void *)animData);
        } else {
            BurnhopeBuffer stagingBufferPos{ lveDevice, sizeof(positions[0]), vertexCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };
            stagingBufferPos.map(); stagingBufferPos.writeToBuffer((void *)positions.data());
            posBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(positions[0]), vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            lveDevice.copyBuffer(stagingBufferPos.getBuffer(), posBuffer->getBuffer(), posBufferSize);

            BurnhopeBuffer stagingBufferAttr{ lveDevice, sizeof(PackedVertexAttr), vertexCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };
            stagingBufferAttr.map(); stagingBufferAttr.writeToBuffer((void *)attrData);
            attrBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(PackedVertexAttr), vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            lveDevice.copyBuffer(stagingBufferAttr.getBuffer(), attrBuffer->getBuffer(), attrBufferSize);

            BurnhopeBuffer stagingBufferAnim{ lveDevice, sizeof(PackedVertexAnim), vertexCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };
            stagingBufferAnim.map(); stagingBufferAnim.writeToBuffer((void *)animData);
            animBuffer = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(PackedVertexAnim), vertexCount,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            lveDevice.copyBuffer(stagingBufferAnim.getBuffer(), animBuffer->getBuffer(), animBufferSize);
        }
    }
    
    void BurnhopeModel::updateVertices(const std::vector<PackedVertexPos>& newPos, const std::vector<PackedVertexAttr>& newAttr, const std::vector<PackedVertexAnim>& newAnim) {
        if (posBuffer && posBuffer->getMappedMemory()) {
            posBuffer->writeToBuffer((void*)newPos.data(), sizeof(newPos[0]) * newPos.size());
        }
        if (attrBuffer && attrBuffer->getMappedMemory()) {
            attrBuffer->writeToBuffer((void*)newAttr.data(), sizeof(newAttr[0]) * newAttr.size());
        }
        if (!newAnim.empty() && animBuffer && animBuffer->getMappedMemory()) {
            animBuffer->writeToBuffer((void*)newAnim.data(), sizeof(newAnim[0]) * newAnim.size());
        }
    }
    void BurnhopeModel::createIndexBuffers(const std::vector<uint32_t> &indices)
    {
        indexCount = static_cast<uint32_t>(indices.size());
        hasIndexBuffer = indexCount > 0;
        
        indexType = VK_INDEX_TYPE_UINT32; // Принудительно 32-бита для прямого доступа из Mesh/RT шейдеров
        uint32_t indexSize = sizeof(uint32_t);
        
        std::vector<uint32_t> uploadIndices = indices;
        if (uploadIndices.empty()) uploadIndices.push_back(0); // Dummy для предотвращения page fault

        VkDeviceSize bufferSize = indexSize * uploadIndices.size();
        
        BurnhopeBuffer stagingBuffer{
            lveDevice,
            indexSize,
            static_cast<uint32_t>(uploadIndices.size()),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        stagingBuffer.map();
        
        stagingBuffer.writeToBuffer((void *)uploadIndices.data());
        
        indexBuffer = std::make_unique<BurnhopeBuffer>(
        lveDevice,
        indexSize,
        static_cast<uint32_t>(uploadIndices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
    }
    void BurnhopeModel::createBLAS(const std::vector<PackedVertexPos>& cpuPositions)
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

        if (posBufferAddress == 0) {
            throw std::runtime_error("[FATAL] Адрес буфера вершин = 0!");
        }

        // Очистка старых ресурсов RT и зануление адреса, чтобы рейтрейсер не читал битую память
        if (blasHandle != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(lveDevice.device());
            auto pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkDestroyAccelerationStructureKHR");
            blasAddress = 0; 
            blasHandle = VK_NULL_HANDLE;
            if (pfnDestroyAccelerationStructureKHR) {
                pfnDestroyAccelerationStructureKHR(lveDevice.device(), blasHandle, nullptr);
            }
        }

        // FIX: Создаем Float32 буфер для RT, так как UNORM16 крашит аппаратное ускорение лучей!
        std::vector<glm::vec3> rtPos(vertexCount);
        glm::vec3 ext = globalAabbMax - globalAabbMin;
        for(size_t i = 0; i < vertexCount; ++i) {
            glm::vec3 normPos = glm::vec3(cpuPositions[i].x, cpuPositions[i].y, cpuPositions[i].z) / 65535.0f;
            rtPos[i] = normPos * ext + globalAabbMin;
        }
        rtPosBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(glm::vec3), vertexCount,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        BurnhopeBuffer stg(lveDevice, sizeof(glm::vec3), vertexCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stg.map(); stg.writeToBuffer((void*)rtPos.data());
        lveDevice.copyBuffer(stg.getBuffer(), rtPosBuffer->getBuffer(), vertexCount * sizeof(glm::vec3));
        VkBufferDeviceAddressInfo rtAddrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, rtPosBuffer->getBuffer()};
        VkDeviceAddress rtPosAddr = vkGetBufferDeviceAddress(lveDevice.device(), &rtAddrInfo);

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
            geom.geometry.triangles.vertexData.deviceAddress = rtPosAddr;
            geom.geometry.triangles.vertexStride = sizeof(glm::vec3);
            geom.geometry.triangles.maxVertex = vertexCount - 1;
            geom.geometry.triangles.indexType = indexType;
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

        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(lveDevice.getPhysicalDevice(), &props2);
        uint32_t scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;

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
            sizeInfo.buildScratchSize + scratchAlignment,
            1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = scratchBuffer.getBuffer();
        VkDeviceAddress rawAddress = vkGetBufferDeviceAddress(lveDevice.device(), &scratchAddressInfo);
        buildInfo.scratchData.deviceAddress = (rawAddress + scratchAlignment - 1) & ~(uint64_t(scratchAlignment) - 1);

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
        std::cout << "[RT] BLAS built! PosAddress: " << posBufferAddress 
              << " | IndexAddress: " << indexBufferAddress 
              << " | BLAS Address: " << blasAddress << std::endl;
    }
    void BurnhopeModel::bind(VkCommandBuffer commandBuffer)
    {
        if (!gpuDataReady) return;
        VkBuffer buffers[] = {posBuffer->getBuffer(), attrBuffer->getBuffer()};
        VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
        if (hasIndexBuffer)
        {
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, indexType);
        }
    }
    void BurnhopeModel::draw(VkCommandBuffer commandBuffer)
    {
        if (!gpuDataReady) return;
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
        std::vector<VkVertexInputBindingDescription> bindingDescriptions(2);
        bindingDescriptions[0].binding = 0;
        bindingDescriptions[0].stride = sizeof(PackedVertexPos);
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions[1].binding = 1;
        bindingDescriptions[1].stride = sizeof(PackedVertexAttr);
        bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescriptions;
    }
    std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R16G16B16A16_UNORM;
        attributeDescriptions[0].offset = offsetof(PackedVertexPos, x);
        attributeDescriptions[1].binding = 1;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R16G16_UNORM;
        attributeDescriptions[1].offset = offsetof(PackedVertexAttr, texUV);
        attributeDescriptions[2].binding = 1;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        attributeDescriptions[2].offset = offsetof(PackedVertexAttr, qTangent);
        
        // ЗАГЛУШКА: Предотвращает краш старых шейдеров (например portal.vert), которые все еще ждут location = 3.
        attributeDescriptions[3].binding = 1;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        attributeDescriptions[3].offset = offsetof(PackedVertexAttr, qTangent);
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
        if (header.version != 12) throw std::runtime_error("[ERROR] Unsupported .bhmesh version. Please delete the cache file and let the engine re-import it.");
        
        acousticAbsorption = header.acousticAbsorption;
        acousticReflection = header.acousticReflection;
        impostorOffset = header.impostorOffset;
        maxWindSway = header.maxWindSway;
        centerOfMass = header.centerOfMass;
        totalMass = header.totalMass;
        centerOfBuoyancy = header.centerOfBuoyancy;
        volume = header.volume;
        inertiaTensor[0] = glm::vec3(header.inertiaTensorRow0.x, header.inertiaTensorRow0.y, header.inertiaTensorRow0.z);
        inertiaTensor[1] = glm::vec3(header.inertiaTensorRow1.x, header.inertiaTensorRow1.y, header.inertiaTensorRow1.z);
        inertiaTensor[2] = glm::vec3(header.inertiaTensorRow2.x, header.inertiaTensorRow2.y, header.inertiaTensorRow2.z);
        vatTexturePath = std::string(header.vatTexturePath);
        vatFrameCount = header.vatFrameCount;
        vatDuration = header.vatDuration;
        vatMinBounds = header.vatMinBounds;
        vatMaxBounds = header.vatMaxBounds;
        globalAabbMin = header.globalAabbMin;
        globalAabbMax = header.globalAabbMax;

        std::vector<BHChunkHeader> chunks(header.chunkCount);
        if (header.chunkCount > 0) {
            file.read((char*)chunks.data(), header.chunkCount * sizeof(BHChunkHeader));
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
        positions.clear();
        attributes.clear();
        indices.clear();
        subMeshes.clear();
        
        uint32_t currentFirstIndex = 0;
        for (uint32_t m = 0; m < header.subMeshCount; ++m)
        {
            BHMeshHeader mh; file.read((char*)&mh, sizeof(BHMeshHeader));
            BHLodHeader lh; file.read((char*)&lh, sizeof(BHLodHeader));
            SubMesh sub{};
            sub.materialIndex = mh.materialIndex;
            sub.aabbMin = mh.aabbMin;
            sub.aabbMax = mh.aabbMax;
            sub.boundingRadius = mh.boundingRadius;
            sub.vrsRate = mh.vrsRate;
            sub.lodCount = 1;
            sub.indexCounts[0] = lh.indexCount;
            sub.firstIndices[0] = currentFirstIndex;
            currentFirstIndex += lh.indexCount;
            subMeshes.push_back(sub);
        }

        physicsPrimitives.resize(header.physicsPrimitiveCount);
        if (header.physicsPrimitiveCount > 0) {
            file.read((char*)physicsPrimitives.data(), physicsPrimitives.size() * sizeof(PhysicsPrimitive));
        }
        destructionBonds.resize(header.destructionBondCount);
        if (header.destructionBondCount > 0) {
            file.read((char*)destructionBonds.data(), destructionBonds.size() * sizeof(DestructionBond));
        }
        
        probeAnchors.resize(header.probeAnchorCount);
        if (header.probeAnchorCount > 0) {
            file.read((char*)probeAnchors.data(), probeAnchors.size() * sizeof(glm::vec3));
        }
        tetraNodes.resize(header.tetraNodeCount);
        if (header.tetraNodeCount > 0) {
            file.read((char*)tetraNodes.data(), tetraNodes.size() * sizeof(glm::vec4));
        }
        tetrahedrons.resize(header.tetraCount);
        if (header.tetraCount > 0) {
            file.read((char*)tetrahedrons.data(), tetrahedrons.size() * sizeof(glm::uvec4));
        }
        
        file.seekg(header.proxyIndexCount * sizeof(uint32_t), std::ios::cur);
        
        morphTargets.resize(header.morphTargetCount);
        if (header.morphTargetCount > 0) file.read((char*)morphTargets.data(), morphTargets.size() * sizeof(BHMorphTarget));

        sockets.resize(header.socketCount);
        if (header.socketCount > 0) file.read((char*)sockets.data(), sockets.size() * sizeof(BHSocket));

        navMeshCarvers.resize(header.carverCount);
        if (header.carverCount > 0) file.read((char*)navMeshCarvers.data(), navMeshCarvers.size() * sizeof(BHNavMeshCarver));
        
        if (chunks.size() > 1) {
            file.seekg(chunks[1].offset, std::ios::beg); // Stream straight to Cold Data
        }

        positions.resize(header.totalVertexCount);
        attributes.resize(header.totalVertexCount);
        animations.resize(header.totalVertexCount);
        indices.resize(header.totalIndexCount);
        file.read((char*)positions.data(), positions.size() * sizeof(PackedVertexPos));
        file.read((char*)attributes.data(), attributes.size() * sizeof(PackedVertexAttr));
        file.read((char*)animations.data(), animations.size() * sizeof(PackedVertexAnim));
        file.read((char*)indices.data(), indices.size() * sizeof(uint32_t));

        colors.resize(header.colorCount);
        uv2.resize(header.uv2Count);
        cdfs.resize(header.cdfCount);
        surfaceTags.resize(header.surfaceTagCount);
        hairStrands.resize(header.hairStrandCount);
        hairPoints.resize(header.hairPointCount);
        morphDeltas.resize(header.morphDeltaCount);

        if(header.colorCount > 0) file.read((char*)colors.data(), colors.size() * sizeof(uint32_t));
        if(header.uv2Count > 0) file.read((char*)uv2.data(), uv2.size() * sizeof(uint32_t));
        if(header.cdfCount > 0) file.read((char*)cdfs.data(), cdfs.size() * sizeof(float));
        if(header.surfaceTagCount > 0) file.read((char*)surfaceTags.data(), surfaceTags.size() * sizeof(uint8_t));
        if(header.hairStrandCount > 0) file.read((char*)hairStrands.data(), hairStrands.size() * sizeof(BHHairStrand));
        if(header.hairPointCount > 0) file.read((char*)hairPoints.data(), hairPoints.size() * sizeof(BHHairPoint));
        if(header.morphDeltaCount > 0) file.read((char*)morphDeltas.data(), morphDeltas.size() * sizeof(BHMorphDelta));
        // Здесь в будущем можно считывать m_errors, m_bCenterX и т.д. для передачи в Compute Shader
        
        file.close();
        std::cout << "[SUCCESS] Loaded " << filepath << ": "
                  << positions.size() << " verts, "
                  << subMeshes.size() << " submeshes\n";
    }
}