cmake_minimum_required(VERSION 3.28)
project(BurnhopeEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(Freetype REQUIRED)
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

FetchContent_Declare(flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG v4.1.1
)
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(flecs)

set(YG_ENABLE_EVENTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yoga
    GIT_REPOSITORY https://github.com/facebook/yoga.git
    GIT_TAG v3.2.1
)
FetchContent_GetProperties(yoga)
if(NOT yoga_POPULATED)
    FetchContent_Populate(yoga)
    add_subdirectory(${yoga_SOURCE_DIR}/yoga ${yoga_BINARY_DIR}/yoga EXCLUDE_FROM_ALL)
endif()

set(BH_CORE ${CMAKE_CURRENT_SOURCE_DIR}/core)
set(BH_DEMO ${CMAKE_CURRENT_SOURCE_DIR}/demo)
set(BH_ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(BH_INCLUDES ${BH_CORE} ${vma_SOURCE_DIR}/include)

# Ядро видит только core/. Демо и движок подключают ядро снаружи.
# Если в core появится include демо или движка, конфигурация падает.
file(GLOB_RECURSE BH_CORE_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/*.cpp ${BH_CORE}/*.hpp ${BH_CORE}/*.h ${BH_CORE}/*.slang)
foreach(bh_core_file ${BH_CORE_SOURCES})
    file(READ ${bh_core_file} bh_core_text)
    if(bh_core_text MATCHES "#[ \t]*include[ \t]*[\"<][^\">]*(demo/|engine/|old/|\\.\\./)")
        message(FATAL_ERROR "core must not import demo, engine, or old: ${bh_core_file}")
    endif()
endforeach()
file(GLOB_RECURSE BH_LIVE_SOURCES CONFIGURE_DEPENDS
    ${BH_DEMO}/*.cpp ${BH_DEMO}/*.hpp ${BH_ENGINE}/*.cpp ${BH_ENGINE}/*.hpp)
foreach(bh_live_file ${BH_LIVE_SOURCES})
    file(READ ${bh_live_file} bh_live_text)
    if(bh_live_text MATCHES "#[ \t]*include[ \t]*[\"<][^\">]*old/")
        message(FATAL_ERROR "demo and engine must not import old: ${bh_live_file}")
    endif()
endforeach()
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

bh_module(platform core/platform/Window.cpp)
bh_module(memory   core/memory/FrameArena.cpp)
bh_module(rhi
    core/rhi/Device.cpp
    core/rhi/Swapchain.cpp
    core/rhi/GpuImage.cpp
    core/rhi/GpuBuffer.cpp
    core/rhi/DescriptorHeap.cpp
    core/rhi/ShaderObject.cpp
    core/rhi/VolkVma.cpp
)
find_program(ISPC_EXECUTABLE ispc REQUIRED)
set(ISPC_GEN ${CMAKE_CURRENT_BINARY_DIR}/ispc)
file(MAKE_DIRECTORY ${ISPC_GEN})
add_custom_command(
    OUTPUT ${ISPC_GEN}/kernel_ispc.o ${ISPC_GEN}/kernel_ispc.h
    COMMAND ${ISPC_EXECUTABLE} -O3
        ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp/kernel.ispc
        -o ${ISPC_GEN}/kernel_ispc.o
        -h ${ISPC_GEN}/kernel_ispc.h
        --target=avx2 --arch=x86-64 --addressing=64 --opt=fast-math --pic --woff
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp/kernel.ispc
    COMMENT "ispc texcomp kernel"
)
set_source_files_properties(${ISPC_GEN}/kernel_ispc.o PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_library(bh_texcomp STATIC
    Libraries/ispc_texcomp/ispc_texcomp.cpp
    ${ISPC_GEN}/kernel_ispc.o
)
target_include_directories(bh_texcomp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp
    PRIVATE ${ISPC_GEN}
)
target_compile_options(bh_texcomp PRIVATE -w)

bh_module(image core/image/ImageFile.cpp)
target_include_directories(bh_image PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp
)
target_link_libraries(bh_image PUBLIC bh_rhi bh_texcomp)
set_source_files_properties(core/image/ImageFile.cpp PROPERTIES COMPILE_OPTIONS "-w")
bh_module(gpu_scene engine/gpu_scene/MeshletGpu.cpp)
target_include_directories(bh_gpu_scene PUBLIC ${BH_ENGINE})
bh_module(render
    engine/render/Visbuffer.cpp
    engine/render/Shade.cpp
    engine/render/SSR.cpp
    engine/render/Tonemap.cpp
)
target_include_directories(bh_render PUBLIC ${BH_ENGINE})
bh_module(ui
    core/ui/Canvas.cpp
    core/ui/Font.cpp
    core/ui/Panel.cpp
    core/ui/Json.cpp
    core/ui/widget/Tree.cpp
    core/ui/widget/Button.cpp
    core/ui/widget/Field.cpp
    core/ui/widget/TextEdit.cpp
    core/ui/widget/Text.cpp
    core/ui/widget/Slider.cpp
    core/ui/widget/Scroll.cpp
    core/ui/widget/Splitter.cpp
    core/ui/widget/Image.cpp
    core/ui/widget/List.cpp
    core/ui/widget/Menu.cpp
    core/ui/widget/Input.cpp
    core/ui/widget/Tip.cpp
    core/ui/kit/Calendar.cpp
    core/ui/kit/Color.cpp
)
bh_module(files core/files/Files.cpp)
target_link_libraries(bh_files PUBLIC bh_ui)
bh_module(app
    demo/app/Engine.cpp
    demo/app/Shell.cpp
)
target_include_directories(bh_app PUBLIC ${BH_DEMO})
target_link_libraries(bh_ui PUBLIC bh_image yogacore flecs_static Freetype::Freetype)
target_compile_options(bh_ui PRIVATE -Wno-deprecated-declarations)
target_compile_definitions(bh_ui PRIVATE
    "BH_UI_VERT=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.vert.spv\""
    "BH_UI_FRAG=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.frag.spv\""
)

set_source_files_properties(core/rhi/VolkVma.cpp PROPERTIES COMPILE_OPTIONS "-w")
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

set(UI_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/ui.slang)
add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/ui.vert.spv
        ${SHADER_OUT_DIR}/ui.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile spirv_1_6 -stage vertex -entry uiVs -o ${SHADER_OUT_DIR}/ui.vert.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile spirv_1_6 -stage fragment -entry uiFs -o ${SHADER_OUT_DIR}/ui.frag.spv
    DEPENDS ${UI_SHADER}
    COMMENT "slangc ui → SPIR-V 1.6"
)
add_custom_target(UiSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/ui.vert.spv
    ${SHADER_OUT_DIR}/ui.frag.spv
)

add_executable(BurnhopeEngine demo/app/Main.cpp demo/Sample.cpp)
target_link_libraries(BurnhopeEngine PRIVATE
    bh_app bh_files bh_ui bh_image bh_rhi bh_memory bh_platform
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeEngine PRIVATE ${BH_CORE} ${BH_DEMO} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeEngine PRIVATE ${BH_DEFS})
target_compile_options(BurnhopeEngine PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeEngine BurnhopeSpv UiSpv)
