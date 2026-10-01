#include "rhi/Device.hpp"

#include <SDL3/SDL_vulkan.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace burnhope {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (data == nullptr || data->pMessage == nullptr) {
        return VK_FALSE;
    }
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        spdlog::error("vk: {}", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("vk: {}", data->pMessage);
    } else {
        spdlog::debug("vk: {}", data->pMessage);
    }
    return VK_FALSE;
}

bool hasLayer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool hasDeviceExt(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool pickQueues(VkPhysicalDevice pd, VkSurfaceKHR surface, DeviceCaps& caps) {
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, fams.data());

    caps.graphicsFamily = UINT32_MAX;
    caps.presentFamily = UINT32_MAX;
    caps.computeFamily = UINT32_MAX;

    for (uint32_t i = 0; i < n; ++i) {
        const bool gfx = (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
        if (gfx && caps.graphicsFamily == UINT32_MAX) {
            caps.graphicsFamily = i;
        }
        if (present && caps.presentFamily == UINT32_MAX) {
            caps.presentFamily = i;
        }
        if (compute && caps.computeFamily == UINT32_MAX) {
            caps.computeFamily = i;
        }
    }
    return caps.graphicsFamily != UINT32_MAX && caps.presentFamily != UINT32_MAX;
}

void fillCaps(VkPhysicalDevice pd, DeviceCaps& caps) {
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(pd, &props2);
    std::strncpy(caps.deviceName, props2.properties.deviceName, sizeof(caps.deviceName) - 1);
    caps.apiMajor = VK_API_VERSION_MAJOR(props2.properties.apiVersion);
    caps.apiMinor = VK_API_VERSION_MINOR(props2.properties.apiVersion);
    caps.apiPatch = VK_API_VERSION_PATCH(props2.properties.apiVersion);

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &f13,
    };
    VkPhysicalDeviceVulkan14Features f14{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &f12,
    };
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObj{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
        .pNext = &f14,
    };
    VkPhysicalDevicePresentWaitFeaturesKHR presentWait{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .pNext = &shaderObj,
    };
    VkPhysicalDevicePresentIdFeaturesKHR presentId{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
        .pNext = &presentWait,
    };
    VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeat{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT,
        .pNext = &presentId,
    };

    VkPhysicalDeviceFeatures2 feats{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &heapFeat,
    };
    vkGetPhysicalDeviceFeatures2(pd, &feats);

    caps.dynamicRendering = f13.dynamicRendering == VK_TRUE;
    caps.sync2 = f13.synchronization2 == VK_TRUE;
    caps.timelineSemaphore = f12.timelineSemaphore == VK_TRUE;
    caps.bufferDeviceAddress = f12.bufferDeviceAddress == VK_TRUE;
    caps.maintenance6 = f14.maintenance6 == VK_TRUE;
    caps.shaderObject = hasDeviceExt(pd, VK_EXT_SHADER_OBJECT_EXTENSION_NAME) && shaderObj.shaderObject == VK_TRUE;
    caps.presentWait = hasDeviceExt(pd, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && presentWait.presentWait == VK_TRUE;
    caps.presentId = hasDeviceExt(pd, VK_KHR_PRESENT_ID_EXTENSION_NAME) && presentId.presentId == VK_TRUE;
    caps.meshShader = hasDeviceExt(pd, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    caps.rayQuery = hasDeviceExt(pd, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    caps.descriptorHeap = hasDeviceExt(pd, VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME)
        && heapFeat.descriptorHeap == VK_TRUE;
    caps.dgc = hasDeviceExt(pd, "VK_EXT_device_generated_commands")
        || hasDeviceExt(pd, "VK_NVX_device_generated_commands");
}

} // namespace

void deviceDumpCaps(const Device& d) {
    const DeviceCaps& c = d.caps;
    spdlog::info("=== Burnhope caps (once) ===");
    spdlog::info("GPU: {}", c.deviceName);
    spdlog::info("device api: {}.{}.{}", c.apiMajor, c.apiMinor, c.apiPatch);
    spdlog::info("queues: graphics={} present={} compute={}", c.graphicsFamily, c.presentFamily, c.computeFamily);
    spdlog::info("shaderObject: {}", c.shaderObject);
    spdlog::info("presentWait: {}  presentId: {}", c.presentWait, c.presentId);
    spdlog::info("dynamicRendering: {}  sync2: {}  timeline: {}  BDA: {}",
        c.dynamicRendering, c.sync2, c.timelineSemaphore, c.bufferDeviceAddress);
    spdlog::info("meshShader: {}  rayQuery: {}  descriptorHeap: {}  dgc: {}",
        c.meshShader, c.rayQuery, c.descriptorHeap, c.dgc);
    if (d.heapProps.imageDescriptorSize != 0) {
        spdlog::info("heap: imgDesc={} bufDesc={} sampDesc={} resAlign={} reservedRes={}",
            static_cast<unsigned long long>(d.heapProps.imageDescriptorSize),
            static_cast<unsigned long long>(d.heapProps.bufferDescriptorSize),
            static_cast<unsigned long long>(d.heapProps.samplerDescriptorSize),
            static_cast<unsigned long long>(d.heapProps.resourceHeapAlignment),
            static_cast<unsigned long long>(d.heapProps.minResourceHeapReservedRange));
    }
    spdlog::info("============================");
}

bool deviceCreate(Device& d, Window& window) {
    if (volkInitialize() != VK_SUCCESS) {
        spdlog::error("volkInitialize failed");
        return false;
    }

#ifndef NDEBUG
    d.validation = hasLayer(kValidationLayer);
#endif

    VkApplicationInfo app{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Burnhope",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "Burnhope",
        .engineVersion = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (sdlExts == nullptr) {
        spdlog::error("SDL_Vulkan_GetInstanceExtensions: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> instExts(sdlExts, sdlExts + sdlExtCount);
    if (d.validation) {
        instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkDebugUtilsMessengerCreateInfoEXT dbgInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    const char* layers[] = {kValidationLayer};
    VkInstanceCreateInfo ici{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = d.validation ? &dbgInfo : nullptr,
        .pApplicationInfo = &app,
        .enabledLayerCount = d.validation ? 1u : 0u,
        .ppEnabledLayerNames = d.validation ? layers : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(instExts.size()),
        .ppEnabledExtensionNames = instExts.data(),
    };

    if (vkCreateInstance(&ici, nullptr, &d.instance) != VK_SUCCESS) {
        spdlog::error("vkCreateInstance (1.4) failed");
        return false;
    }
    volkLoadInstance(d.instance);

    if (d.validation) {
        if (vkCreateDebugUtilsMessengerEXT(d.instance, &dbgInfo, nullptr, &d.debug) != VK_SUCCESS) {
            spdlog::warn("debug messenger failed");
        }
    }

    if (!SDL_Vulkan_CreateSurface(window.handle, d.instance, nullptr, &d.surface)) {
        spdlog::error("SDL_Vulkan_CreateSurface: {}", SDL_GetError());
        return false;
    }

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(d.instance, &physCount, nullptr);
    if (physCount == 0) {
        spdlog::error("no Vulkan devices");
        return false;
    }
    std::vector<VkPhysicalDevice> phys(physCount);
    vkEnumeratePhysicalDevices(d.instance, &physCount, phys.data());

    d.physical = VK_NULL_HANDLE;
    for (VkPhysicalDevice pd : phys) {
        DeviceCaps trial{};
        if (!pickQueues(pd, d.surface, trial)) {
            continue;
        }
        fillCaps(pd, trial);
        if (!hasDeviceExt(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        if (!trial.dynamicRendering || !trial.sync2) {
            continue;
        }
        d.physical = pd;
        d.caps = trial;
        if (trial.shaderObject && trial.presentWait) {
            break; // prefer a GPU that satisfies M0 musts
        }
    }
    if (d.physical == VK_NULL_HANDLE) {
        spdlog::error("no device with swapchain + dynamic rendering + sync2");
        return false;
    }

    fillCaps(d.physical, d.caps);
    pickQueues(d.physical, d.surface, d.caps);

    d.heapProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
    d.heapProps.pNext = nullptr;
    VkPhysicalDeviceProperties2 props2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = d.caps.descriptorHeap ? &d.heapProps : nullptr,
    };
    vkGetPhysicalDeviceProperties2(d.physical, &props2);
    deviceDumpCaps(d);

    if (!d.caps.shaderObject) {
        spdlog::error("M0 requires VK_EXT_shader_object");
        return false;
    }
    if (!d.caps.descriptorHeap) {
        spdlog::error("VK_EXT_descriptor_heap is required");
        return false;
    }

    std::vector<const char*> devExts{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
    };
    if (d.caps.presentId) {
        devExts.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
    }
    if (d.caps.presentWait) {
        devExts.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    }

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qcis[2]{};
    uint32_t qCount = 1;
    qcis[0] = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = d.caps.graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    if (d.caps.presentFamily != d.caps.graphicsFamily) {
        qcis[1] = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = d.caps.presentFamily,
            .queueCount = 1,
            .pQueuePriorities = &prio,
        };
        qCount = 2;
    }

    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.synchronization2 = VK_TRUE;
    f13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    f12.bufferDeviceAddress = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE;
    f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    f12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    f12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    f12.runtimeDescriptorArray = VK_TRUE;
    f12.scalarBlockLayout = VK_TRUE;

    VkPhysicalDeviceVulkan11Features f11{};
    f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    f11.pNext = &f12;
    f11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan14Features f14{};
    f14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    f14.pNext = &f11;

    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObj{};
    shaderObj.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    shaderObj.pNext = &f14;
    shaderObj.shaderObject = VK_TRUE;

    VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeat{};
    heapFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
    heapFeat.pNext = &shaderObj;
    heapFeat.descriptorHeap = VK_TRUE;
    void* pNext = &heapFeat;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeat{};
    meshFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeat.meshShader = VK_TRUE;
    if (d.caps.meshShader) {
        devExts.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        meshFeat.pNext = pNext;
        pNext = &meshFeat;
    }

    VkPhysicalDevicePresentIdFeaturesKHR presentIdFeat{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
        .presentId = VK_TRUE,
    };
    VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeat{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .presentWait = VK_TRUE,
    };
    if (d.caps.presentWait && d.caps.presentId) {
        presentWaitFeat.pNext = pNext;
        presentIdFeat.pNext = &presentWaitFeat;
        pNext = &presentIdFeat;
    }

    VkPhysicalDeviceFeatures2 feat2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = pNext,
    };
    feat2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    feat2.features.shaderStorageImageReadWithoutFormat = VK_TRUE;

    VkDeviceCreateInfo dci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &feat2,
        .queueCreateInfoCount = qCount,
        .pQueueCreateInfos = qcis,
        .enabledExtensionCount = static_cast<uint32_t>(devExts.size()),
        .ppEnabledExtensionNames = devExts.data(),
    };

    if (vkCreateDevice(d.physical, &dci, nullptr, &d.device) != VK_SUCCESS) {
        spdlog::error("vkCreateDevice failed");
        return false;
    }
    volkLoadDevice(d.device);

    vkGetDeviceQueue(d.device, d.caps.graphicsFamily, 0, &d.graphicsQueue);
    vkGetDeviceQueue(d.device, d.caps.presentFamily, 0, &d.presentQueue);

    VkCommandPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = d.caps.graphicsFamily,
    };
    if (vkCreateCommandPool(d.device, &pool, nullptr, &d.commandPool) != VK_SUCCESS) {
        spdlog::error("vkCreateCommandPool failed");
        return false;
    }

    VmaVulkanFunctions vmaFn{};
    vmaFn.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFn.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo aci{};
    aci.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    aci.physicalDevice = d.physical;
    aci.device = d.device;
    aci.instance = d.instance;
    aci.vulkanApiVersion = VK_API_VERSION_1_4;
    aci.pVulkanFunctions = &vmaFn;
    if (vmaCreateAllocator(&aci, &d.allocator) != VK_SUCCESS) {
        spdlog::error("vmaCreateAllocator failed");
        return false;
    }

    if (!d.caps.presentWait) {
        spdlog::warn("present_wait missing — present without wait (caps fallback)");
    }
    return true;
}

void deviceDestroy(Device& d) {
    if (d.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(d.device);
    }
    if (d.allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(d.allocator);
        d.allocator = VK_NULL_HANDLE;
    }
    if (d.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(d.device, d.commandPool, nullptr);
        d.commandPool = VK_NULL_HANDLE;
    }
    if (d.device != VK_NULL_HANDLE) {
        vkDestroyDevice(d.device, nullptr);
        d.device = VK_NULL_HANDLE;
    }
    if (d.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(d.instance, d.surface, nullptr);
        d.surface = VK_NULL_HANDLE;
    }
    if (d.debug != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(d.instance, d.debug, nullptr);
        d.debug = VK_NULL_HANDLE;
    }
    if (d.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(d.instance, nullptr);
        d.instance = VK_NULL_HANDLE;
    }
}

} // namespace burnhope
