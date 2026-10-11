#pragma once

#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

template <size_t N>
void destroyShaderArray(Device& device, ShaderExt (&shaders)[N]) {
    for (size_t i = 0; i < N; ++i) {
        shaderDestroy(device, shaders[i]);
    }
}

template <size_t N>
bool createShaderArray(
    Device& device,
    ShaderExt (&outShaders)[N],
    const char* const (&paths)[N],
    VkShaderStageFlagBits stage,
    const VkDescriptorSetAndBindingMappingEXT* mappings,
    uint32_t mappingCount,
    VkShaderStageFlags nextStage = 0) {
    for (size_t i = 0; i < N; ++i) {
        const ShaderCreateDesc desc{
            .path = paths[i],
            .stage = stage,
            .nextStage = nextStage,
            .mappingCount = mappingCount,
            .mappings = mappings,
        };
        if (!shaderCreate(device, desc, outShaders[i])) {
            spdlog::error("shader array failed: {}", paths[i] != nullptr ? paths[i] : "");
            for (size_t done = 0; done <= i; ++done) {
                shaderDestroy(device, outShaders[done]);
            }
            return false;
        }
    }
    return true;
}

template <size_t N>
bool createShaderArray(
    Device& device,
    ShaderExt (&outShaders)[N],
    const char* const (&paths)[N],
    VkShaderStageFlagBits stage,
    const VkDescriptorSetAndBindingMappingEXT* mappings,
    const uint32_t (&mappingCounts)[N],
    VkShaderStageFlags nextStage = 0) {
    for (size_t i = 0; i < N; ++i) {
        const ShaderCreateDesc desc{
            .path = paths[i],
            .stage = stage,
            .nextStage = nextStage,
            .mappingCount = mappingCounts[i],
            .mappings = mappings,
        };
        if (!shaderCreate(device, desc, outShaders[i])) {
            spdlog::error("shader array failed: {}", paths[i] != nullptr ? paths[i] : "");
            for (size_t done = 0; done <= i; ++done) {
                shaderDestroy(device, outShaders[done]);
            }
            return false;
        }
    }
    return true;
}

template <size_t N>
bool createShaderArray(
    Device& device,
    ShaderExt (&outShaders)[N],
    const char* const (&paths)[N],
    VkShaderStageFlagBits stage,
    const VkDescriptorSetAndBindingMappingEXT* const (&mappings)[N],
    uint32_t mappingCount,
    VkShaderStageFlags nextStage = 0) {
    for (size_t i = 0; i < N; ++i) {
        const ShaderCreateDesc desc{
            .path = paths[i],
            .stage = stage,
            .nextStage = nextStage,
            .mappingCount = mappingCount,
            .mappings = mappings[i],
        };
        if (!shaderCreate(device, desc, outShaders[i])) {
            spdlog::error("shader array failed: {}", paths[i] != nullptr ? paths[i] : "");
            for (size_t done = 0; done <= i; ++done) {
                shaderDestroy(device, outShaders[done]);
            }
            return false;
        }
    }
    return true;
}

// fill(index, desc) дописывает карты, когда у каждого шейдера свой набор.
template <size_t N, typename Fill>
bool createShaderGroup(
    Device& device,
    ShaderExt (&outShaders)[N],
    const char* const (&paths)[N],
    VkShaderStageFlagBits stage,
    Fill fill,
    VkShaderStageFlags nextStage = 0) {
    for (size_t i = 0; i < N; ++i) {
        ShaderCreateDesc desc{
            .path = paths[i],
            .stage = stage,
            .nextStage = nextStage,
        };
        fill(static_cast<uint32_t>(i), desc);
        if (!shaderCreate(device, desc, outShaders[i])) {
            spdlog::error("shader group failed: {}", paths[i] != nullptr ? paths[i] : "");
            for (size_t done = 0; done <= i; ++done) {
                shaderDestroy(device, outShaders[done]);
            }
            return false;
        }
    }
    return true;
}

} // namespace burnhope
