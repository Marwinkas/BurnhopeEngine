cmake_minimum_required(VERSION 3.28)
project(BurnhopeEngine LANGUAGES CXX)

# Core rewrite (path-2026). Old deferred editor: cmake/LegacyEngine.cmake
option(BURNHOPE_LEGACY "Build pre-2026 G-Buffer editor" OFF)
if(BURNHOPE_LEGACY)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LegacyEngine.cmake)
    return()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.309.0
)
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(volk)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(vma)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

set(BH_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/engine ${vma_SOURCE_DIR}/include)
set(BH_DEFS
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    SPDLOG_NO_EXCEPTIONS
    FMT_EXCEPTIONS=0
)
set(BH_OPTS -Wall -Wextra -Wpedantic -std=c++2c -fno-rtti -fno-exceptions -Wno-missing-field-initializers)

function(bh_module name)
    add_library(bh_${name} OBJECT ${ARGN})
    target_include_directories(bh_${name} PUBLIC ${BH_INCLUDES})
    target_compile_definitions(bh_${name} PUBLIC ${BH_DEFS})
    target_compile_options(bh_${name} PRIVATE ${BH_OPTS})
    target_link_libraries(bh_${name} PUBLIC volk spdlog::spdlog)
endfunction()

bh_module(platform engine/platform/Window.cpp)
bh_module(memory   engine/memory/FrameArena.cpp)
bh_module(rhi
    engine/rhi/Device.cpp
    engine/rhi/Swapchain.cpp
    engine/rhi/GpuImage.cpp
    engine/rhi/GpuBuffer.cpp
    engine/rhi/DescriptorHeap.cpp
    engine/rhi/ShaderObject.cpp
    engine/rhi/VolkVma.cpp
)
bh_module(gpu_scene engine/gpu_scene/MeshletGpu.cpp)
bh_module(render
    engine/render/Visbuffer.cpp
    engine/render/Shade.cpp
    engine/render/SSR.cpp
    engine/render/Tonemap.cpp
)
bh_module(app    engine/app/Engine.cpp)

set_source_files_properties(engine/rhi/VolkVma.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(bh_rhi PUBLIC Vulkan::Vulkan)
target_link_libraries(bh_platform PUBLIC SDL3::SDL3)
target_link_libraries(bh_app PUBLIC SDL3::SDL3)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/render)

add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/visbuffer.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.frag.spv
        ${SHADER_OUT_DIR}/shade.comp.spv
        ${SHADER_OUT_DIR}/ssr.comp.spv
        ${SHADER_OUT_DIR}/tonemap.mesh.spv
        ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -o ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -target spirv -profile spirv_1_6 -stage fragment -entry visFs -o ${SHADER_OUT_DIR}/visbuffer.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade.slang -target spirv -profile spirv_1_6 -stage compute -entry shadeCs -o ${SHADER_OUT_DIR}/shade.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssr.slang -target spirv -profile spirv_1_6 -stage compute -entry ssrCs -o ${SHADER_OUT_DIR}/ssr.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -target spirv -profile spirv_1_6 -stage mesh -entry tonemapMesh -o ${SHADER_OUT_DIR}/tonemap.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -target spirv -profile spirv_1_6 -stage fragment -entry tonemapFs -o ${SHADER_OUT_DIR}/tonemap.frag.spv
    DEPENDS
        ${SHADER_DIR}/visbuffer.slang
        ${SHADER_DIR}/shade.slang
        ${SHADER_DIR}/ssr.slang
        ${SHADER_DIR}/tonemap.slang
    COMMENT "slangc vis/shade/ssr/tonemap → SPIR-V 1.6"
)
add_custom_target(BurnhopeSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.frag.spv
    ${SHADER_OUT_DIR}/shade.comp.spv
    ${SHADER_OUT_DIR}/ssr.comp.spv
    ${SHADER_OUT_DIR}/tonemap.mesh.spv
    ${SHADER_OUT_DIR}/tonemap.frag.spv
)

add_executable(BurnhopeEngine engine/app/Main.cpp)
target_link_libraries(BurnhopeEngine PRIVATE
    bh_app bh_render bh_gpu_scene bh_rhi bh_memory bh_platform
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeEngine PRIVATE ${BH_INCLUDES})
target_compile_definitions(BurnhopeEngine PRIVATE ${BH_DEFS})
target_compile_options(BurnhopeEngine PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeEngine BurnhopeSpv)
