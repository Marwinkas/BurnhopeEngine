#pragma once
#include "Utils/Device.hpp"
#include "Utils/Renderer.hpp"
#include "Utils/Window.hpp"
#include "Utils/Descriptors.hpp"
#include "Render/Material.hpp"
#include "Utils/Components.hpp"
#include "Utils/UIManager.h"
#include "Render/Gbuffer.hpp"
#include "Render/MainRender.hpp"
#include "Render/PortalRenderSystem.hpp"
#include "Render/ShadowRender.hpp"
#include "Render/GTAOSystem.hpp"
#include "Render/HiZSystem.hpp"
#include "Render/ComputeShader.hpp"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif
#define NOMINMAX
#include "Render/Deferred.hpp"
#include "Render/shadow.hpp"
#include "Render/RadianceCascades.hpp"
#include "Render/SSGISystem.hpp"
namespace burnhope
{
    namespace fs = std::filesystem;
    inline std::string getExecutablePaths()
    {
#ifdef _WIN32
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return fs::path(buffer).parent_path().string();
#else
        char buffer[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
        if (count == -1)
            return "";
        return fs::path(std::string(buffer, count)).parent_path().string();
#endif
    }

inline VkTransformMatrixKHR toVkMatrix(const glm::mat4& m) {
    VkTransformMatrixKHR out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            out.matrix[i][j] = m[j][i]; 
        }
    }
    return out;
}
    class FirstApp
    {
    public:
        static constexpr int WIDTH = 800;
        static constexpr int HEIGHT = 600;
        static constexpr int MAX_PORTALS = 10;
        FirstApp();
        ~FirstApp();
        FirstApp(const FirstApp &) = delete;
        FirstApp &operator=(const FirstApp &) = delete;
        void run();
        std::unique_ptr<BurnhopeBuffer> tlasBuffer;
        std::unique_ptr<BurnhopeBuffer> instancesBuffer;
        VkAccelerationStructureKHR tlasHandle = VK_NULL_HANDLE;

        void buildTLAS(entt::registry& registry);
    private:
        BurnhopeWindow lveWindow{WIDTH, HEIGHT, "BurnHope Engine"};
        BurnhopeDevice lveDevice{lveWindow};
        BurnhopeRenderer lveRenderer{lveWindow, lveDevice};
        std::unique_ptr<BurnhopeDescriptorPool> globalPool{};

        void loadGameObjects(entt::registry &registry);
        void initCompute(VkDescriptorSetLayout globalSetLayout);
        void rebuildGBufferDescriptorSets();
        std::unique_ptr<DeferredLightingSystem> lightingSystem;
        std::unique_ptr<BurnhopeTexture> hdrOutputTexture;
        std::unique_ptr<BurnhopeTexture> gtaoOutputTexture;
        std::unique_ptr<ShadowRenderSystem> shadowRenderSystem;
        std::unique_ptr<BurnhopeDescriptorSetLayout> shadowObjectLayoutPtr;
        std::unique_ptr<CullingSystem> cullingSystem;
        uint32_t totalSubMeshCount = 0;
        VkDescriptorSet shadowObjectSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout gBufferSetLayout;
        VkDescriptorSetLayout outputSetLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> gBufferLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> outputLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> giLayoutPtr;
        std::unique_ptr<BurnhopeBuffer> dummyGridBuffer;
        std::unique_ptr<BurnhopeBuffer> dummyIndexBuffer;
        VkDescriptorSet gBufferSet;
        VkDescriptorSet shadowDummySet;
        VkDescriptorSet lightDummySet;
        VkDescriptorSet computeOutputSet;
        std::unique_ptr<BurnhopeTexture> ssgiRawTexture;
        
        std::unique_ptr<BurnhopeTexture> postProcessTexture;
        std::unique_ptr<BurnhopeDescriptorSetLayout> postProcessLayoutPtr;
        VkDescriptorSet postProcessSet = VK_NULL_HANDLE;
        std::unique_ptr<ComputeShader> postProcessShader;
        
        std::unique_ptr<ComputeShader> lightCullingShader;
        std::unique_ptr<ComputeShader> vsmMarkPagesShader;

        std::unique_ptr<BurnhopeDescriptorSetLayout> ssgiLayoutPtr;
        VkDescriptorSet ssgiSet = VK_NULL_HANDLE;
        std::unique_ptr<SSGISystem> ssgiSystem;
        std::unique_ptr<GeometryRenderSystem> simpleRenderSystem;
        std::unique_ptr<PortalRenderSystem> portalRenderSystem;
        void RebuildBatches(entt::registry &registry, GeometryRenderSystem &renderSystem);
        std::unique_ptr<burnhope::RadianceCascadesSystem> rcSystem;
        std::shared_ptr<BurnhopeTexture> defaultWhiteTex;
        std::shared_ptr<BurnhopeTexture> defaultNormalTex;
        std::shared_ptr<Material> defaultWhiteMaterial;
        std::shared_ptr<BurnhopeTexture> blueNoiseTex;
        std::unique_ptr<BurnhopeShadowSystem> shadowSystem;
        std::unique_ptr<BurnhopeBuffer> lightUboBuffer;
        std::unique_ptr<BurnhopeDescriptorSetLayout> shadowLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> lightLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> gtaoLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> rtLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> vsmLayoutPtr;
        VkDescriptorSet shadowSet = VK_NULL_HANDLE;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        VkDescriptorSet vsmSet = VK_NULL_HANDLE;
        std::unique_ptr<HiZSystem> hizSystem;
        VkDescriptorSet gtaoSet = VK_NULL_HANDLE;
        VkDescriptorSet rtSet = VK_NULL_HANDLE;
        std::unique_ptr<GTAOSystem> gtaoSystem;
        std::vector<std::vector<std::unique_ptr<BurnhopeBuffer>>> portalUboBuffers;
        std::vector<std::vector<VkDescriptorSet>> portalDescriptorSets;
        
#ifdef _WIN32
        std::string rootPath = "C:/";
#else
        std::string rootPath = fs::current_path().string();
#endif
        
        entt::registry registry;
        std::unique_ptr<UIManager> uiManager;
        
        std::unique_ptr<BurnhopeBuffer> objectBuffer;
        std::unique_ptr<BurnhopeBuffer> materialBuffer;
        std::unique_ptr<BurnhopeBuffer> faceMatricesBuffer;
        std::unique_ptr<BurnhopeDescriptorSetLayout> globalSetLayout;
        VkDescriptorSet storageSet;
        VkDescriptorSet textureSet;
        VkImageView csmArrayView = VK_NULL_HANDLE;
        std::unique_ptr<BurnhopeGBuffer> gBuffer;
    };
}