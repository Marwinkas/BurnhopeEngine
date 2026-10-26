#include "rhi/Device.hpp"
#include "Str.hpp"
#include "platform/Jobs.hpp"

#include <spdlog/spdlog.h>

#include <renderdoc_app.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstring>
#include <vector>

namespace burnhope {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool instanceHasExt(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> props(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
    for (const auto& e : props) {
        if (std::strcmp(e.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

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
    caps.transferFamily = UINT32_MAX;

    for (uint32_t i = 0; i < n; ++i) {
        const bool gfx = (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool transfer = (fams[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
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
        if (transfer && !gfx && caps.transferFamily == UINT32_MAX) {
            caps.transferFamily = i;
        }
    }
    if (caps.transferFamily == UINT32_MAX) {
        caps.transferFamily = caps.graphicsFamily;
    }
    return caps.graphicsFamily != UINT32_MAX && caps.presentFamily != UINT32_MAX;
}

void fillCaps(VkPhysicalDevice pd, DeviceCaps& caps) {
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(pd, &props2);
    strCopyCap(caps.deviceName, sizeof(caps.deviceName), props2.properties.deviceName);
    caps.apiMajor = VK_API_VERSION_MAJOR(props2.properties.apiVersion);
    caps.apiMinor = VK_API_VERSION_MINOR(props2.properties.apiVersion);
    caps.apiPatch = VK_API_VERSION_PATCH(props2.properties.apiVersion);

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &f13,
    };
    VkPhysicalDeviceVulkan12Features f12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &f11,
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
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapMaint{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .pNext = &heapFeat,
    };
    VkPhysicalDeviceNestedCommandBufferFeaturesEXT nestedCmd{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT,
        .pNext = &swapMaint,
    };
    VkPhysicalDeviceDepthClampControlFeaturesEXT depthClamp{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT,
        .pNext = &nestedCmd,
    };
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR,
        .pNext = &depthClamp,
    };
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopMatrix{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR,
        .pNext = &barycentric,
    };
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rtMaint{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR,
        .pNext = &coopMatrix,
    };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeat{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .pNext = &rtMaint,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeat{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &rayQueryFeat,
    };
    VkPhysicalDeviceRobustness2FeaturesEXT robust2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
        .pNext = &accelFeat,
    };

    VkPhysicalDeviceFeatures2 feats{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &robust2,
    };
    vkGetPhysicalDeviceFeatures2(pd, &feats);

    caps.dynamicRendering = f13.dynamicRendering == VK_TRUE;
    caps.sync2 = f13.synchronization2 == VK_TRUE;
    caps.shaderDemote = f13.shaderDemoteToHelperInvocation == VK_TRUE;
    caps.timelineSemaphore = f12.timelineSemaphore == VK_TRUE;
    caps.bufferDeviceAddress = f12.bufferDeviceAddress == VK_TRUE;
    caps.maintenance6 = f14.maintenance6 == VK_TRUE;
    caps.hostImageCopy = f14.hostImageCopy == VK_TRUE;
    caps.shaderObject = hasDeviceExt(pd, VK_EXT_SHADER_OBJECT_EXTENSION_NAME) && shaderObj.shaderObject == VK_TRUE;
    caps.presentWait = hasDeviceExt(pd, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && presentWait.presentWait == VK_TRUE;
    caps.presentId = hasDeviceExt(pd, VK_KHR_PRESENT_ID_EXTENSION_NAME) && presentId.presentId == VK_TRUE;
    caps.meshShader = hasDeviceExt(pd, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    caps.accelStruct = hasDeviceExt(pd, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        && hasDeviceExt(pd, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
        && accelFeat.accelerationStructure == VK_TRUE;
    caps.rayQuery = caps.accelStruct
        && hasDeviceExt(pd, VK_KHR_RAY_QUERY_EXTENSION_NAME)
        && hasDeviceExt(pd, VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME)
        && rayQueryFeat.rayQuery == VK_TRUE
        && rtMaint.rayTracingMaintenance1 == VK_TRUE;
    caps.descriptorHeap = hasDeviceExt(pd, VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME)
        && heapFeat.descriptorHeap == VK_TRUE;
    caps.dgc = hasDeviceExt(pd, "VK_EXT_device_generated_commands")
        || hasDeviceExt(pd, "VK_NVX_device_generated_commands");
    caps.swapchainMaintenance1 = hasDeviceExt(pd, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)
        && swapMaint.swapchainMaintenance1 == VK_TRUE;
    caps.nestedCommandBuffer = hasDeviceExt(pd, VK_EXT_NESTED_COMMAND_BUFFER_EXTENSION_NAME)
        && nestedCmd.nestedCommandBuffer == VK_TRUE;
    caps.depthClampControl = hasDeviceExt(pd, VK_EXT_DEPTH_CLAMP_CONTROL_EXTENSION_NAME)
        && depthClamp.depthClampControl == VK_TRUE;
    caps.fragmentShaderBarycentric = hasDeviceExt(pd, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME)
        && barycentric.fragmentShaderBarycentric == VK_TRUE;
    // Только обнаружение: нет потребителя в текущих фазах, фичу устройству не запрашиваем.
    caps.cooperativeMatrix = hasDeviceExt(pd, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)
        && coopMatrix.cooperativeMatrix == VK_TRUE;
    caps.robustness2 = hasDeviceExt(pd, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)
        && robust2.robustBufferAccess2 == VK_TRUE
        && robust2.robustImageAccess2 == VK_TRUE
        && robust2.nullDescriptor == VK_TRUE;
    caps.samplerAnisotropy = feats.features.samplerAnisotropy == VK_TRUE;
    caps.shaderInt64 = feats.features.shaderInt64 == VK_TRUE;
    caps.shaderBufferInt64Atomics = f12.shaderBufferInt64Atomics == VK_TRUE;
    caps.shaderFloat16 = f12.shaderFloat16 == VK_TRUE;
    caps.storageBuffer16 = f11.storageBuffer16BitAccess == VK_TRUE;
}

} // namespace

void deviceDumpCaps(const Device& d) {
    const DeviceCaps& c = d.caps;
    spdlog::info("=== Burnhope caps (once) ===");
    spdlog::info("GPU: {}", c.deviceName);
    spdlog::info("device api: {}.{}.{}", c.apiMajor, c.apiMinor, c.apiPatch);
    spdlog::info("queues: graphics={} present={} compute={} transfer={}", c.graphicsFamily, c.presentFamily, c.computeFamily, c.transferFamily);
    spdlog::info("shaderObject: {}", c.shaderObject);
    spdlog::info("presentWait: {}  presentId: {}", c.presentWait, c.presentId);
    spdlog::info("dynamicRendering: {}  sync2: {}  timeline: {}  BDA: {}",
        c.dynamicRendering, c.sync2, c.timelineSemaphore, c.bufferDeviceAddress);
    spdlog::info("meshShader: {}  rayQuery: {}  descriptorHeap: {}  dgc: {}",
        c.meshShader, c.rayQuery, c.descriptorHeap, c.dgc);
    spdlog::info("maintenance6: {}  hostImageCopy: {}", c.maintenance6, c.hostImageCopy);
    spdlog::info("swapchainMaintenance1: {}  nestedCommandBuffer: {}  depthClampControl: {}  barycentric: {}  cooperativeMatrix(discover-only): {}",
        c.swapchainMaintenance1, c.nestedCommandBuffer, c.depthClampControl, c.fragmentShaderBarycentric, c.cooperativeMatrix);
    spdlog::info("robustness2: {}  int64: {}  int64atom: {}  float16: {}  storage16: {}", c.robustness2, c.shaderInt64, c.shaderBufferInt64Atomics, c.shaderFloat16, c.storageBuffer16);
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

bool deviceCreateSurface(Device& d, VkSurfaceKHR surface, VkSurfaceKHR& out) {
    if (d.instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
        return false;
    }
    out = surface;
    return true;
}

bool deviceCreateInstance(Device& d, std::span<const char* const> surfaceExts) {
    if (d.instance != VK_NULL_HANDLE) {
        return true;
    }
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

    if (surfaceExts.empty()) {
        spdlog::error("deviceCreateInstance: no surface extensions supplied");
        return false;
    }

    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExtProps(instExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExtProps.data());
    const auto hasInstExt = [&instExtProps](const char* name) {
        for (const auto& e : instExtProps) {
            if (std::strcmp(e.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };
    // VK_EXT_swapchain_maintenance1 нужен на устройстве под Фазу 1.5 (пересборка свопчейна без
    // vkDeviceWaitIdle), но он требует этот инстанс-расширение. Запрашиваем его тут и позже
    // сверяем оба флага перед включением фичи на устройстве — иначе caps.swapchainMaintenance1
    // будет врать: обнаружен, но не может быть включён без этого пререквизита.
    const bool instSurfaceMaint1 = hasInstExt(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    const bool instSurfaceCaps2 = hasInstExt(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
        && instSurfaceMaint1;

    std::vector<const char*> instExts(surfaceExts.begin(), surfaceExts.end());
    if (d.validation) {
        instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    if (instSurfaceCaps2) {
        instExts.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        instExts.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
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
    const VkValidationFeatureEnableEXT syncFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &dbgInfo,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures = &syncFeature,
    };
    VkInstanceCreateInfo ici{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = d.validation ? &validationFeatures : nullptr,
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
    return true;
}

bool deviceCreate(Device& d, VkSurfaceKHR surface) {
    if (d.instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
        return false;
    }
    d.surface = surface;

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
    // Делаем caps правдивым до любого решения об enable: если пререквизит-расширение инстанса
    // не попал в список (например старый лоадер), фичу устройства включать нельзя — тогда это
    // не «обнаружено, но забыли включить» (баг maintenance6), а честное «недоступно».
    const bool instSurfaceCaps2 = instanceHasExt(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
        && instanceHasExt(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    d.caps.swapchainMaintenance1 = d.caps.swapchainMaintenance1 && instSurfaceCaps2;

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
    if (d.caps.swapchainMaintenance1) {
        devExts.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    }
    if (d.caps.nestedCommandBuffer) {
        devExts.push_back(VK_EXT_NESTED_COMMAND_BUFFER_EXTENSION_NAME);
    }
    if (d.caps.depthClampControl) {
        devExts.push_back(VK_EXT_DEPTH_CLAMP_CONTROL_EXTENSION_NAME);
    }
    if (d.caps.fragmentShaderBarycentric) {
        devExts.push_back(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
    }
    if (d.caps.robustness2) {
        devExts.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    }
    // cooperativeMatrix: обнаружен и в логе, но не запрошен — нет потребителя в текущих фазах
    // (план, Северная звезда — будущий ReSTIR/нейро-апскейл). Инстанс-расширение
    // VK_KHR_SURFACE уже требует VK_KHR_get_surface_capabilities2 для maintenance1,
    // но у платформы он уже в списке инстанс-расширений по умолчанию на большинстве лоадеров.

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qcis[3]{};
    uint32_t qCount = 1;
    qcis[0] = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = d.caps.graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    if (d.caps.presentFamily != d.caps.graphicsFamily) {
        qcis[qCount] = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = d.caps.presentFamily,
            .queueCount = 1,
            .pQueuePriorities = &prio,
        };
        ++qCount;
    }
    if (d.caps.transferFamily != d.caps.graphicsFamily && d.caps.transferFamily != d.caps.presentFamily) {
        qcis[qCount] = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = d.caps.transferFamily,
            .queueCount = 1,
            .pQueuePriorities = &prio,
        };
        ++qCount;
    }

    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.synchronization2 = VK_TRUE;
    f13.dynamicRendering = VK_TRUE;
    f13.shaderDemoteToHelperInvocation = d.caps.shaderDemote ? VK_TRUE : VK_FALSE;

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
    f12.shaderFloat16 = d.caps.shaderFloat16 ? VK_TRUE : VK_FALSE;
    f12.shaderBufferInt64Atomics = d.caps.shaderBufferInt64Atomics ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceVulkan11Features f11{};
    f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    f11.pNext = &f12;
    f11.shaderDrawParameters = VK_TRUE;
    f11.storageBuffer16BitAccess = d.caps.storageBuffer16 ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceVulkan14Features f14{};
    f14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    f14.pNext = &f11;
    // caps.* was just queried from this same GPU above (fillCaps), so gating by it here
    // means we never ask the driver for a feature it did not report — no silent "caps says
    // yes, device never actually got it" gap (maintenance6 used to have exactly that bug).
    f14.maintenance6 = d.caps.maintenance6 ? VK_TRUE : VK_FALSE;
    f14.hostImageCopy = d.caps.hostImageCopy ? VK_TRUE : VK_FALSE;

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
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelOn{};
    accelOn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelOn.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR rayOn{};
    rayOn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayOn.rayQuery = VK_TRUE;
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR maintOn{};
    maintOn.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR;
    maintOn.rayTracingMaintenance1 = VK_TRUE;
    if (d.caps.rayQuery) {
        devExts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        devExts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        devExts.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        devExts.push_back(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
        maintOn.pNext = pNext;
        rayOn.pNext = &maintOn;
        accelOn.pNext = &rayOn;
        pNext = &accelOn;
    }

    // Фаза 1.5: позволяет менять present-режим и переиспользовать swapchain без полной
    // пересборки — follow-up к точечной правке `pace` в Swapchain.cpp, не включён в draw ещё.
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapMaintFeat{};
    swapMaintFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    swapMaintFeat.swapchainMaintenance1 = VK_TRUE;
    if (d.caps.swapchainMaintenance1) {
        swapMaintFeat.pNext = pNext;
        pNext = &swapMaintFeat;
    }

    // Фаза 5+: много потоков смогут писать командные буферы вложенно, когда появится
    // 3D-сцена с несколькими слоями; сейчас только фича включена, вызовов ещё нет.
    VkPhysicalDeviceNestedCommandBufferFeaturesEXT nestedCmdFeat{};
    nestedCmdFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT;
    nestedCmdFeat.nestedCommandBuffer = VK_TRUE;
    if (d.caps.nestedCommandBuffer) {
        nestedCmdFeat.pNext = pNext;
        pNext = &nestedCmdFeat;
    }

    VkPhysicalDeviceDepthClampControlFeaturesEXT depthClampFeat{};
    depthClampFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT;
    depthClampFeat.depthClampControl = VK_TRUE;
    if (d.caps.depthClampControl) {
        depthClampFeat.pNext = pNext;
        pNext = &depthClampFeat;
    }

    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentricFeat{};
    barycentricFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR;
    barycentricFeat.fragmentShaderBarycentric = VK_TRUE;
    if (d.caps.fragmentShaderBarycentric) {
        barycentricFeat.pNext = pNext;
        pNext = &barycentricFeat;
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

    VkPhysicalDeviceRobustness2FeaturesEXT robustFeat{};
    robustFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    robustFeat.robustBufferAccess2 = VK_TRUE;
    robustFeat.robustImageAccess2 = VK_TRUE;
    robustFeat.nullDescriptor = VK_TRUE;
    if (d.caps.robustness2) {
        robustFeat.pNext = pNext;
        pNext = &robustFeat;
    }

    VkPhysicalDeviceFeatures2 feat2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = pNext,
    };
    feat2.features.robustBufferAccess = VK_TRUE;
    feat2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    feat2.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    feat2.features.samplerAnisotropy = d.caps.samplerAnisotropy ? VK_TRUE : VK_FALSE;
    feat2.features.shaderInt64 = d.caps.shaderInt64 ? VK_TRUE : VK_FALSE;

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
    vkGetDeviceQueue(d.device, d.caps.transferFamily, 0, &d.transferQueue);

    VkCommandPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = d.caps.graphicsFamily,
    };
    if (vkCreateCommandPool(d.device, &pool, nullptr, &d.commandPool) != VK_SUCCESS) {
        spdlog::error("vkCreateCommandPool failed");
        return false;
    }
    if (d.caps.transferFamily != d.caps.graphicsFamily) {
        pool.queueFamilyIndex = d.caps.transferFamily;
        if (vkCreateCommandPool(d.device, &pool, nullptr, &d.transferPool) != VK_SUCCESS) {
            spdlog::error("transfer command pool failed");
            return false;
        }
    } else {
        d.transferPool = d.commandPool;
    }
    deviceName(d, reinterpret_cast<uint64_t>(d.device), VK_OBJECT_TYPE_DEVICE, "BurnhopeDevice");

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

uint64_t deviceMemoryUsed(const Device& d) {
    if (d.allocator == VK_NULL_HANDLE) {
        return 0;
    }
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(d.allocator, &stats);
    return stats.total.statistics.allocationBytes;
}

void deviceMarkLost(Device& d, const char* where, int32_t result) {
    const uint32_t was = d.lost.exchange(1, std::memory_order_acq_rel);
    if (was == 0) {
        spdlog::error("RHI device lost at {} ({}) — draws stop, host recovers next tick", where != nullptr ? where : "vulkan", result);
    }
}

void deviceDestroy(Device& d) {
    if (d.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(d.device);
    }
    if (d.compress.release != nullptr) {
        d.compress.release(d, d.compress.state);
    }
    d.compress = {};
    if (d.jobs.release != nullptr) {
        d.jobs.release(d, d.jobs.state);
    }
    d.jobs = {};
    if (d.allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(d.allocator);
        d.allocator = VK_NULL_HANDLE;
    }
    if (d.transferPool != VK_NULL_HANDLE && d.transferPool != d.commandPool) {
        vkDestroyCommandPool(d.device, d.transferPool, nullptr);
    }
    d.transferPool = VK_NULL_HANDLE;
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

void deviceName(Device& d, uint64_t handle, VkObjectType type, const char* name) {
    if (!d.validation || d.device == VK_NULL_HANDLE || handle == 0 || name == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };
    vkSetDebugUtilsObjectNameEXT(d.device, &info);
}

JobPool& deviceJobs(Device& d) {
    std::lock_guard<std::mutex> lock(d.queueMutex);
    if (d.jobs.state == nullptr) {
        d.jobs = {.state = jobsCreate(), .release = [](Device&, void* pool) { jobsDestroy(static_cast<JobPool*>(pool)); }};
    }
    return *static_cast<JobPool*>(d.jobs.state);
}

void rhiCaptureToggle(Device& d) {
    using GetApi = int (*)(int, void**);
    if (d.renderDocApi == nullptr) {
#if defined(_WIN32)
        if (d.renderDocLib == nullptr) {
            d.renderDocLib = reinterpret_cast<void*>(GetModuleHandleA("renderdoc.dll"));
            if (d.renderDocLib == nullptr) {
                d.renderDocLib = reinterpret_cast<void*>(LoadLibraryA("renderdoc.dll"));
            }
        }
        if (d.renderDocLib == nullptr) {
            return;
        }
        auto get = reinterpret_cast<GetApi>(GetProcAddress(reinterpret_cast<HMODULE>(d.renderDocLib), "RENDERDOC_GetAPI"));
#else
        if (d.renderDocLib == nullptr) {
            d.renderDocLib = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
            if (d.renderDocLib == nullptr) {
                d.renderDocLib = dlopen("librenderdoc.so", RTLD_NOW);
            }
        }
        if (d.renderDocLib == nullptr) {
            return;
        }
        auto get = reinterpret_cast<GetApi>(dlsym(d.renderDocLib, "RENDERDOC_GetAPI"));
#endif
        if (get == nullptr || get(eRENDERDOC_API_Version_1_6_0, &d.renderDocApi) != 1 || d.renderDocApi == nullptr) {
            d.renderDocApi = nullptr;
            return;
        }
    }
    auto* api = static_cast<RENDERDOC_API_1_6_0*>(d.renderDocApi);
    if (!d.renderDocOpen) {
        api->StartFrameCapture(nullptr, nullptr);
        d.renderDocOpen = true;
    } else {
        api->EndFrameCapture(nullptr, nullptr);
        d.renderDocOpen = false;
    }
}

} // namespace burnhope
