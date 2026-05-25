#include "Engine.hpp"
#include "EngineMath.hpp"
#include "../Utils/GpuAddress.hpp"
#include "../Render/RenderDebug.hpp"
#include "../Render/Core/SceneGpuTypes.hpp"
#include "../Render/Model.hpp"
#include <iomanip>
#include <iostream>
#include <map>

namespace burnhope {

void Engine::buildPendingBlas(flecs::world& world) {
  world.each([&](MeshComponent& meshComp) {
    if (!meshComp.model || !meshComp.model->gpuDataReady) {
      return;
    }
    if (meshComp.model->consumeBlasBuildPending()) {
      meshComp.model->createBLAS(meshComp.model->storedPositions);
    }
  });
}

void Engine::rebuildBatches(flecs::world& world, GeometryRenderSystem&) {
  refreshBindlessSlots(*this);
  const uint32_t defaultWhiteHeap = bindless_.textures().heapIndex(defaultWhiteTex);
  const uint32_t defaultNormalHeap = bindless_.textures().heapIndex(defaultNormalTex);

  std::vector<ObjectData> objDataList;
  std::vector<MaterialData> matDataList;
  std::map<Material*, uint32_t> matToIndex;
  uint32_t globalMatIndex = 0;

  /** Absolute resource-heap slot (stable across refresh via refreshHeapIndices). */
  auto getTexHeap = [&](TextureHandle handle, TextureHandle defaultHandle) -> int32_t {
    if (handle == kInvalidTextureHandle) {
      handle = defaultHandle;
    }
    return static_cast<int32_t>(bindless_.textures().heapIndex(handle));
  };

  auto getTexHeapShared = [&](const std::shared_ptr<BurnhopeTexture>& tex, uint32_t defaultHeap) -> int32_t {
    if (!tex) {
      return static_cast<int32_t>(defaultHeap);
    }
    for (uint32_t h = 0; h < bindless_.textures().count(); ++h) {
      if (bindless_.textures().resolve(static_cast<TextureHandle>(h)) == tex.get()) {
        return static_cast<int32_t>(bindless_.textures().heapIndex(static_cast<TextureHandle>(h)));
      }
    }
    return static_cast<int32_t>(defaultHeap);
  };

  world.each([&](DecalComponent& decal) {
    decal.albedoTexIdx = getTexHeapShared(decal.albedoTex, defaultWhiteHeap);
    decal.normalTexIdx = getTexHeapShared(decal.normalTex, defaultNormalHeap);
  });

  auto& rs = uiManager->GetContext().renderSettings;
  rs.vectorTexIdx = getTexHeapShared(rs.vectorTex, defaultWhiteHeap);
  rs.causticsTexIdx = getTexHeapShared(rs.causticsTex, defaultWhiteHeap);
  rs.canvasTexIdx = getTexHeapShared(rs.canvasTex, defaultWhiteHeap);

  world.each([&](TransformComponent& transformComp, MeshComponent& meshComp) {
    if (!meshComp.model || !meshComp.isVisible || !meshComp.model->gpuDataReady) {
      return;
    }
    const auto& subMeshes = meshComp.model->getSubMeshes();
    for (uint32_t i = 0; i < subMeshes.size(); i++) {
      std::shared_ptr<Material> currentMat =
          (i < meshComp.materials.size() && meshComp.materials[i]) ? meshComp.materials[i]
                                                                   : defaultWhiteMaterial;
      uint32_t currentMatID = 0;
      if (auto it = matToIndex.find(currentMat.get()); it == matToIndex.end()) {
        currentMatID = globalMatIndex++;
        matToIndex[currentMat.get()] = currentMatID;
        MaterialData matData{};
        matData.albedoAlphaIdx =
            currentMat->packedAlbedoAlpha != kInvalidTextureHandle
                ? getTexHeap(currentMat->packedAlbedoAlpha, defaultWhiteTex)
                : getTexHeap(currentMat->albedoMap, defaultWhiteTex);
        matData.normalIdx = currentMat->packedNormal != kInvalidTextureHandle
                                ? getTexHeap(currentMat->packedNormal, defaultNormalTex)
                                : getTexHeap(currentMat->normalMap, defaultNormalTex);
        matData.ormxIdx = currentMat->packedORMX != kInvalidTextureHandle
                              ? getTexHeap(currentMat->packedORMX, defaultWhiteTex)
                              : getTexHeap(currentMat->ormMap, defaultWhiteTex);
        matData.emissiveIdx = currentMat->packedEmissive != kInvalidTextureHandle
                                  ? getTexHeap(currentMat->packedEmissive, defaultWhiteTex)
                                  : getTexHeap(currentMat->emissiveMap, defaultWhiteTex);
        matData.useTriplanar = currentMat->useTriplanar ? 1 : 0;
        matData.isTransparent = currentMat->isTransparent ? 1 : 0;
        matData.repeatTexture = currentMat->repeatTexture ? 1 : 0;
        if constexpr (kMinimalRenderPath) {
          if (currentMat.get() == defaultWhiteMaterial.get()) {
            matData.useTriplanar = 0;
            matData.ormxIdx = -1;
            matData.normalIdx = -1;
            matData.emissiveIdx = -1;
          }
        }
        matData.uvScale = currentMat->uvScale;
        matData.triplanarScale = currentMat->triplanarScale;
        matData.emissiveIntensity = currentMat->emissiveIntensity;
        matData.albedoColor = currentMat->albedoColor;
        matData.emissiveColor =
            float4{currentMat->emissiveColor.x, currentMat->emissiveColor.y, currentMat->emissiveColor.z, 1.f};
        matData.metallicStrength = currentMat->metallicStrength;
        matData.roughnessStrength = currentMat->roughnessStrength;
        matData.normalStrength = currentMat->normalStrength;
        matData.heightStrength = currentMat->heightStrength;
        matData.aoStrength = currentMat->aoStrength;
        matDataList.push_back(matData);
      } else {
        currentMatID = it->second;
      }

      ObjectData obj{};
      obj.modelMatrix = transformComp.transform.matrix;
      obj.materialID = currentMatID;
      obj.indexCount = subMeshes[i].indexCounts[0];
      obj.vrsRate = subMeshes[i].vrsRate;
      obj.boneOffset = 0xFFFFFFFFu;
      obj.vertexCount = meshComp.model->getVertexCount();
      const uint32_t firstIdx = subMeshes[i].firstIndices[0];
      const uint32_t indexBytes = subMeshes[i].indexCounts[0] * sizeof(uint32_t);
      obj.posHeap = bindless_.registerStorageBuffer(
          meshComp.model->getPosVkBuffer(), 0,
          static_cast<VkDeviceSize>(obj.vertexCount) * sizeof(PackedVertexPos));
      obj.attrHeap = bindless_.registerStorageBuffer(
          meshComp.model->getAttrVkBuffer(), 0,
          static_cast<VkDeviceSize>(obj.vertexCount) * sizeof(PackedVertexAttr));
      obj.indexHeap = bindless_.registerStorageBuffer(
          meshComp.model->getIndexVkBuffer(),
          static_cast<VkDeviceSize>(firstIdx) * sizeof(uint32_t),
          indexBytes);
      obj.colorBufferAddress = 0;
      obj.uv2BufferAddress = 0;
      obj.animBufferAddress = 0;
      if constexpr (kMinimalRenderPath) {
        obj.pad0 = firstIdx;
      }
      obj.aabbMin = float4{meshComp.model->globalAabbMin.x, meshComp.model->globalAabbMin.y,
                           meshComp.model->globalAabbMin.z, 0.f};
      obj.aabbMax = float4{meshComp.model->globalAabbMax.x, meshComp.model->globalAabbMax.y,
                           meshComp.model->globalAabbMax.z, 0.f};
      objDataList.push_back(obj);
    }
  });

  totalSubMeshCount = static_cast<uint32_t>(objDataList.size());
  if constexpr (kMinimalRenderPath) {
    static uint32_t lastLogged = UINT32_MAX;
    if (totalSubMeshCount != lastLogged) {
      std::cerr << "[Minimal] rebuild subMeshes=" << totalSubMeshCount;
      if (!objDataList.empty()) {
        const ObjectData& o = objDataList[0];
        std::cerr << " matID=" << o.materialID
                  << " indexCount=" << o.indexCount
                  << " posHeap=" << o.posHeap
                  << " attrHeap=" << o.attrHeap
                  << " idxHeap=" << o.indexHeap
                  << " verts=" << o.vertexCount
                  << " aabb=[" << o.aabbMin.x << ',' << o.aabbMin.y << ',' << o.aabbMin.z << "]..["
                  << o.aabbMax.x << ',' << o.aabbMax.y << ',' << o.aabbMax.z << ']';
      }
      std::cerr << '\n';
      lastLogged = totalSubMeshCount;
    }
  }

  std::vector<ObjectData> uploadObjData = objDataList;
  std::vector<MaterialData> uploadMatData = matDataList;
  if (uploadObjData.empty()) {
    uploadObjData.push_back(ObjectData{});
  }
  if (uploadMatData.empty()) {
    uploadMatData.push_back(MaterialData{});
  }

  objectBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(ObjectData), uploadObjData.size(),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  objectBuffer->map();
  objectBuffer->writeToBuffer(uploadObjData.data());
  objectBuffer->flush();

  materialBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(MaterialData), uploadMatData.size(),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  materialBuffer->map();
  materialBuffer->writeToBuffer(uploadMatData.data());
  materialBuffer->flush();
  maxMaterialIndex = uploadMatData.empty() ? 0u : static_cast<uint32_t>(uploadMatData.size() - 1);

  if (cullingSystem) {
    cullingSystem->bindObjectBuffer(objectBuffer->handle(), objectBuffer->bufferSize());
  }

  // refreshBindlessSlots() at the start registered the previous object/material buffers;
  // those VkBuffers are destroyed above — re-bind heap slots to the new allocations.
  bindless_.slots().objectStorage = bindless_.registerStorageBuffer(
      objectBuffer->handle(), 0, objectBuffer->bufferSize());
  bindless_.slots().materialStorage = bindless_.registerStorageBuffer(
      materialBuffer->handle(), 0, materialBuffer->bufferSize());

  if constexpr (kMinimalRenderPath) {
    static uint32_t rebuildSeq = 0;
    std::cerr << "[Minimal] rebuildBatches #" << ++rebuildSeq
              << " objectStorageSlot=" << bindless_.slots().objectStorage;
    if (!objDataList.empty()) {
      const ObjectData& o = objDataList[0];
      std::cerr << " matID=" << o.materialID << " maxMat=" << maxMaterialIndex
                << " mats=" << uploadMatData.size()
                << " posHeap=" << o.posHeap << " attrHeap=" << o.attrHeap
                << " idxHeap=" << o.indexHeap;
    }
    if (!uploadMatData.empty()) {
      const MaterialData& m = uploadMatData[0];
      std::cerr << " mat0 heapSlot=" << m.albedoAlphaIdx
                << " texBase=" << bindless_.slots().textureTableBase
                << " albedo=(" << m.albedoColor.x << ',' << m.albedoColor.y << ',' << m.albedoColor.z
                << ')';
    }
    std::cerr << '\n';
    world.each([&](const MeshComponent& meshComp) {
      if (!meshComp.model || !meshComp.model->gpuDataReady || objDataList.empty()) {
        return;
      }
      const ObjectData& o = objDataList[0];
      const uint32_t firstIdx = o.pad0;
      const VkDeviceSize posBytes =
          static_cast<VkDeviceSize>(o.vertexCount) * sizeof(PackedVertexPos);
      const VkDeviceSize attrBytes =
          static_cast<VkDeviceSize>(o.vertexCount) * sizeof(PackedVertexAttr);
      const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(o.indexCount) * sizeof(uint32_t);
      bindless_.verifyStorageBufferSlot(
          o.posHeap, meshComp.model->getPosVkBuffer(), 0, "posHeap");
      bindless_.verifyStorageBufferSlot(
          o.attrHeap, meshComp.model->getAttrVkBuffer(), 0, "attrHeap");
      bindless_.verifyStorageBufferSlot(
          o.indexHeap, meshComp.model->getIndexVkBuffer(),
          static_cast<VkDeviceSize>(firstIdx) * sizeof(uint32_t), "indexHeap");
      if (objectBuffer) {
        bindless_.verifyStorageBufferSlot(
            bindless_.slots().objectStorage, objectBuffer->handle(), 0, "objectStorage");
        std::cerr << "[HeapVerify] objectBda=0x" << std::hex
                  << deviceAddressBits(objectBuffer->deviceAddress()) << std::dec << '\n';
      }
      std::cerr << "[HeapVerify] BLAS posBda=0x" << std::hex
                << deviceAddressBits(meshComp.model->getPosBufferAddress())
                << " idxBda=0x" << deviceAddressBits(meshComp.model->getIndexBufferAddress())
                << std::dec << " firstIdx=" << firstIdx << '\n';
    });
  }

  bindless_.slots().textureTableBase = defaultWhiteHeap;
  bindless_.slots().textureTableCount = bindless_.textures().count();
}

void Engine::buildTLAS(flecs::world& world) {
  auto pfnCreate = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
      vkGetDeviceProcAddr(device_.device(), "vkCreateAccelerationStructureKHR"));
  auto pfnDestroy = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
      vkGetDeviceProcAddr(device_.device(), "vkDestroyAccelerationStructureKHR"));
  auto pfnSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
      vkGetDeviceProcAddr(device_.device(), "vkGetAccelerationStructureBuildSizesKHR"));
  auto pfnBuild = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
      vkGetDeviceProcAddr(device_.device(), "vkCmdBuildAccelerationStructuresKHR"));
  auto pfnAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
      vkGetDeviceProcAddr(device_.device(), "vkGetAccelerationStructureDeviceAddressKHR"));
  if (!pfnCreate || !pfnSizes || !pfnBuild || !pfnAddress) {
    return;
  }

  if (tlasHandle != VK_NULL_HANDLE && pfnDestroy) {
    pfnDestroy(device_.device(), tlasHandle, nullptr);
    tlasHandle = VK_NULL_HANDLE;
  }

  std::vector<VkAccelerationStructureInstanceKHR> instances;
  uint32_t customIndex = 0;
  world.each([&](TransformComponent& transformComp, MeshComponent& meshComp) {
    if (!meshComp.model || !meshComp.isVisible || !meshComp.model->gpuDataReady) {
      return;
    }
    if (meshComp.model->getBLASAddress() == 0) {
      return;
    }
    VkAccelerationStructureInstanceKHR inst{};
    inst.transform = toVkTransformMatrix(transformComp.transform.matrix);
    inst.instanceCustomIndex = customIndex;
    inst.mask = 0xFF;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = meshComp.model->getBLASAddress();
    instances.push_back(inst);
    customIndex += static_cast<uint32_t>(meshComp.model->getSubMeshes().size());
  });

  if (instances.empty()) {
    return;
  }

  instancesBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(VkAccelerationStructureInstanceKHR), instances.size(),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  instancesBuffer->map();
  instancesBuffer->writeToBuffer(instances.data());

  VkDeviceAddress instanceData = instancesBuffer->deviceAddress();

  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  VkAccelerationStructureGeometryInstancesDataKHR instData{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
  instData.arrayOfPointers = VK_FALSE;
  instData.data.deviceAddress = instanceData;
  geom.geometry.instances = instData;

  VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries = &geom;

  uint32_t primCount = static_cast<uint32_t>(instances.size());
  VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  pfnSizes(device_.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount,
           &sizeInfo);

  auto scratch = std::make_unique<BurnhopeBuffer>(
      device_, static_cast<VkDeviceSize>(sizeInfo.buildScratchSize), 1,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  tlasBuffer = std::make_unique<BurnhopeBuffer>(
      device_, static_cast<VkDeviceSize>(sizeInfo.accelerationStructureSize), 1,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkAccelerationStructureCreateInfoKHR createInfo{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  createInfo.buffer = tlasBuffer->handle();
  createInfo.size = sizeInfo.accelerationStructureSize;
  createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  if (pfnCreate(device_.device(), &createInfo, nullptr, &tlasHandle) != VK_SUCCESS) {
    tlasHandle = VK_NULL_HANDLE;
    return;
  }

  VkAccelerationStructureBuildRangeInfoKHR range{};
  range.primitiveCount = primCount;
  const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

  buildInfo.dstAccelerationStructure = tlasHandle;
  buildInfo.scratchData.deviceAddress = scratch->deviceAddress();

  VkCommandBuffer cmd = device_.beginSingleTimeCommands();
  pfnBuild(cmd, 1, &buildInfo, &ranges);
  device_.endSingleTimeCommands(cmd);

  rtTlasHeap_ = bindless_.registerAccelerationStructure(tlasHandle);
  bindless_.slots().rtTlas = rtTlasHeap_;
}

} // namespace burnhope
