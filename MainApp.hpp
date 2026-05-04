#pragma once

#include "Utils/Device.hpp"
#include "Utils/Renderer.hpp"
#include "Utils/Window.hpp"
#include "Utils/Descriptors.hpp"
#include "Render/Material.hpp"
#include "Utils/Components.hpp"
#include "Utils/UI.hpp"
#include "Render/Gbuffer.hpp"

#include "Render/MainRender.hpp" 
#include "Render/ShadowRender.hpp"
#include "Render/GTAOSystem.hpp"
#include "Render/HiZSystem.hpp"

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
namespace burnhope {

    
namespace fs = std::filesystem;

    inline std::string getExecutablePaths() {
    #ifdef _WIN32
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return fs::path(buffer).parent_path().string();
    #else
        char buffer[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
        if (count == -1) return "";
        return fs::path(std::string(buffer, count)).parent_path().string();
    #endif
    }
    class FirstApp {
    public:
        static constexpr int WIDTH = 800;
        static constexpr int HEIGHT = 600;

        FirstApp();
        ~FirstApp();

        FirstApp(const FirstApp&) = delete;
        FirstApp& operator=(const FirstApp&) = delete;

        void run();

    private:
        void loadGameObjects(entt::registry& registry);

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
        std::unique_ptr<BurnhopeDescriptorSetLayout> irradianceLayoutPtr;
        std::unique_ptr<BurnhopeBuffer> dummyGridBuffer;
        std::unique_ptr<BurnhopeBuffer> dummyIndexBuffer;

        VkDescriptorSet gBufferSet;      
        VkDescriptorSet shadowDummySet;  
        VkDescriptorSet lightDummySet;   
        VkDescriptorSet computeOutputSet;

        
        void RebuildBatches(entt::registry& registry, GeometryRenderSystem& renderSystem);
        std::unique_ptr<burnhope::RadianceCascadesSystem> rcSystem;
        BurnhopeWindow lveWindow{ WIDTH, HEIGHT, "BurnHope Engine" };
        BurnhopeDevice lveDevice{ lveWindow };
        BurnhopeRenderer lveRenderer{ lveWindow, lveDevice };

        
        std::shared_ptr<BurnhopeTexture> defaultWhiteTex;
        std::shared_ptr<BurnhopeTexture> defaultNormalTex;
        std::shared_ptr<Material> defaultWhiteMaterial;

        std::unique_ptr<BurnhopeShadowSystem> shadowSystem;
        std::unique_ptr<BurnhopeBuffer>       lightUboBuffer;
        std::unique_ptr<BurnhopeDescriptorSetLayout> shadowLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> lightLayoutPtr;
         std::unique_ptr<BurnhopeDescriptorSetLayout> gtaoLayoutPtr;
        VkDescriptorSet shadowSet = VK_NULL_HANDLE;
        VkDescriptorSet lightSet = VK_NULL_HANDLE;
        std::unique_ptr<HiZSystem> hizSystem;

        
        VkDescriptorSet gtaoSet = VK_NULL_HANDLE;
        std::unique_ptr<GTAOSystem> gtaoSystem;
        #ifdef _WIN32
            std::string rootPath = "C:/";
        #else
            
            std::string rootPath = fs::current_path().string(); 
        #endif

        UI ui{ lveWindow, lveDevice, lveRenderer.getSwapChainRenderPass(), rootPath, rootPath };

        
        entt::registry registry;

        
        std::unique_ptr<BurnhopeBuffer> objectBuffer;
        std::unique_ptr<BurnhopeBuffer> materialBuffer;
        std::unique_ptr<BurnhopeBuffer> faceMatricesBuffer;
        
        VkDescriptorSet storageSet;
        VkDescriptorSet textureSet;
        std::unique_ptr<BurnhopeGBuffer> gBuffer;
        
        std::unique_ptr<BurnhopeDescriptorPool> globalPool{};
    };

} 