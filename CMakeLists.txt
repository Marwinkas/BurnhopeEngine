cmake_minimum_required(VERSION 3.28)
project(BurnhopeEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(Freetype REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(assimp REQUIRED)

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
set(BH_OPTS -Wall -Wextra -Wpedantic -std=c++26 -fno-rtti -fno-exceptions -Wno-missing-field-initializers)

function(bh_module name)
    add_library(bh_${name} OBJECT ${ARGN})
    target_include_directories(bh_${name} PUBLIC ${BH_INCLUDES})
    target_compile_definitions(bh_${name} PUBLIC ${BH_DEFS})
    target_compile_options(bh_${name} PRIVATE ${BH_OPTS})
    target_link_libraries(bh_${name} PUBLIC volk spdlog::spdlog)
endfunction()

option(BH_TRACY "Pull Tracy from https://github.com/wolfpld/tracy and mark frame zones" OFF)
option(BH_ASAN "Debug address and undefined sanitizers" OFF)
option(BH_TSAN "Debug thread sanitizer" OFF)

bh_module(platform
    core/platform/Window.cpp
    core/platform/Dialog.cpp
    core/platform/Check.cpp
    core/platform/Jobs.cpp
)
set_source_files_properties(core/platform/Check.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-frtti")
find_package(cpptrace CONFIG REQUIRED)
target_link_libraries(bh_platform PUBLIC cpptrace::cpptrace)

if(BH_TRACY)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    target_compile_definitions(bh_platform PUBLIC BH_TRACY)
    target_link_libraries(bh_platform PUBLIC TracyClient)
endif()

if(BH_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(BH_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
bh_module(memory   core/memory/FrameArena.cpp)
bh_module(wasm core/wasm/WasmHost.cpp)
bh_module(io core/io/File.cpp)
bh_module(asset core/asset/AssimpImporter.cpp)
target_link_libraries(bh_asset PUBLIC assimp::assimp meshoptimizer bh_platform)
set_source_files_properties(core/asset/AssimpImporter.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-frtti")
target_link_libraries(bh_wasm PUBLIC bh_memory)
bh_module(rhi
    core/rhi/Device.cpp
    core/rhi/Swapchain.cpp
    core/rhi/GpuImage.cpp
    core/rhi/GpuBuffer.cpp
    core/rhi/DescriptorHeap.cpp
    core/rhi/ShaderObject.cpp
    core/rhi/MeshDraw.cpp
    core/rhi/GpuProfiler.cpp
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
target_link_libraries(bh_image PUBLIC bh_rhi bh_texcomp bh_platform)
set_source_files_properties(core/image/ImageFile.cpp PROPERTIES COMPILE_OPTIONS "-w")
bh_module(gpu_scene engine/gpu_scene/MeshletGpu.cpp)
target_include_directories(bh_gpu_scene PUBLIC ${BH_ENGINE})
bh_module(render
    engine/render/Visbuffer.cpp
    engine/render/Shade.cpp
    engine/render/Ssrc.cpp
    engine/render/Radiance.cpp
    engine/render/SSR.cpp
    engine/render/Cube.cpp
    engine/render/Cull.cpp
    engine/render/Tonemap.cpp
    engine/render/PassClusters.cpp
    engine/render/PassCull.cpp
    engine/render/PassSun.cpp
    engine/render/PassPoints.cpp
    engine/render/PassVis.cpp
    engine/render/PassShade.cpp
    engine/render/PassRadiance.cpp
    engine/render/PassSsr.cpp
    engine/render/PassTonemap.cpp
    engine/render/SceneFrame.cpp
)
target_include_directories(bh_render PUBLIC ${BH_ENGINE})
target_compile_definitions(bh_render PUBLIC "BH_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/shaders\"")
bh_module(ui
    core/ui/Canvas.cpp
    core/ui/Font.cpp
    core/ui/Panel.cpp
    core/ui/Json.cpp
    core/ui/Layout.cpp
    core/ui/widget/Chrome.cpp
    core/ui/widget/Tree.cpp
    core/ui/widget/Button.cpp
    core/ui/widget/Field.cpp
    core/ui/widget/TextEdit.cpp
    core/ui/widget/Text.cpp
    core/ui/widget/Slider.cpp
    core/ui/widget/Curve.cpp
    core/ui/widget/Scroll.cpp
    core/ui/widget/Splitter.cpp
    core/ui/widget/Image.cpp
    core/ui/widget/List.cpp
    core/ui/widget/Virtual.cpp
    core/ui/widget/Menu.cpp
    core/ui/widget/Input.cpp
    core/ui/widget/Tip.cpp
    core/ui/kit/Calendar.cpp
    core/ui/kit/Color.cpp
)
bh_module(files core/files/Files.cpp)
target_link_libraries(bh_files PUBLIC bh_ui)
bh_module(host core/host/Host.cpp)
target_link_libraries(bh_host PUBLIC bh_ui bh_rhi bh_platform bh_memory)
bh_module(app
    demo/app/Engine.cpp
    demo/app/Shell.cpp
)
target_include_directories(bh_app PUBLIC ${BH_DEMO} ${BH_ENGINE})
target_link_libraries(bh_ui PUBLIC bh_image yogacore flecs_static Freetype::Freetype)
target_compile_options(bh_ui PRIVATE -Wno-deprecated-declarations)
target_compile_definitions(bh_ui PRIVATE
    "BH_UI_VERT=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.vert.spv\""
    "BH_UI_FRAG=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.frag.spv\""
    "BH_UI_MESH=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.mesh.spv\""
)

set_source_files_properties(core/rhi/VolkVma.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(bh_rhi PUBLIC Vulkan::Vulkan)
target_link_libraries(bh_platform PUBLIC SDL3::SDL3)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/render)

add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/visbuffer.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.mesh1.spv
        ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.late1.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.shadow0.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.shadow1.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.shadow2.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.frag.spv
        ${SHADER_OUT_DIR}/shade.comp.spv
        ${SHADER_OUT_DIR}/resolve.comp.spv
        ${SHADER_OUT_DIR}/cull.comp.spv
        ${SHADER_OUT_DIR}/cull.comp1.spv
        ${SHADER_OUT_DIR}/cull.late.spv
        ${SHADER_OUT_DIR}/cull.late1.spv
        ${SHADER_OUT_DIR}/cull.clear.spv
        ${SHADER_OUT_DIR}/cull.clear1.spv
        ${SHADER_OUT_DIR}/cluster.comp.spv
        ${SHADER_OUT_DIR}/shadowcull.comp.spv
        ${SHADER_OUT_DIR}/shadowcull.clear.spv
        ${SHADER_OUT_DIR}/ssr.comp.spv
        ${SHADER_OUT_DIR}/ssr.up.comp.spv
        ${SHADER_OUT_DIR}/gtao.comp.spv
        ${SHADER_OUT_DIR}/cmaa2.comp.spv
        ${SHADER_OUT_DIR}/bloom0.comp.spv
        ${SHADER_OUT_DIR}/bloom1.comp.spv
        ${SHADER_OUT_DIR}/bloom2.comp.spv
        ${SHADER_OUT_DIR}/bloom3.comp.spv
        ${SHADER_OUT_DIR}/bloomup0.comp.spv
        ${SHADER_OUT_DIR}/bloomup1.comp.spv
        ${SHADER_OUT_DIR}/bloomup2.comp.spv
        ${SHADER_OUT_DIR}/hiz0.comp.spv
        ${SHADER_OUT_DIR}/hiz1.comp.spv
        ${SHADER_OUT_DIR}/hiz2.comp.spv
        ${SHADER_OUT_DIR}/hiz3.comp.spv
        ${SHADER_OUT_DIR}/hiz4.comp.spv
        ${SHADER_OUT_DIR}/hiz5.comp.spv
        ${SHADER_OUT_DIR}/hizmax0.comp.spv
        ${SHADER_OUT_DIR}/hizmax1.comp.spv
        ${SHADER_OUT_DIR}/hizmax2.comp.spv
        ${SHADER_OUT_DIR}/hizmax3.comp.spv
        ${SHADER_OUT_DIR}/hizmax4.comp.spv
        ${SHADER_OUT_DIR}/hizmax5.comp.spv
        ${SHADER_OUT_DIR}/visbuffer.point0.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.point1.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.point2.mesh.spv
        ${SHADER_OUT_DIR}/cube0.mesh.spv
        ${SHADER_OUT_DIR}/cube1.mesh.spv
        ${SHADER_OUT_DIR}/cube2.mesh.spv
        ${SHADER_OUT_DIR}/cube3.mesh.spv
        ${SHADER_OUT_DIR}/cube4.mesh.spv
        ${SHADER_OUT_DIR}/cube5.mesh.spv
        ${SHADER_OUT_DIR}/cube.frag.spv
        ${SHADER_OUT_DIR}/ssrc0.comp.spv
        ${SHADER_OUT_DIR}/ssrc1.comp.spv
        ${SHADER_OUT_DIR}/ssrc2.comp.spv
        ${SHADER_OUT_DIR}/ssrc3.comp.spv
        ${SHADER_OUT_DIR}/ssrc.apply.comp.spv
        ${SHADER_OUT_DIR}/radiance.trace.comp.spv
        ${SHADER_OUT_DIR}/radiance.apply.comp.spv
        ${SHADER_OUT_DIR}/tonemap.mesh.spv
        ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -DCULL_FLIGHT=0 -o ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -DCULL_FLIGHT=1 -o ${SHADER_OUT_DIR}/visbuffer.mesh1.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -DLATE=1 -DCULL_FLIGHT=0 -o ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -DLATE=1 -DCULL_FLIGHT=1 -o ${SHADER_OUT_DIR}/visbuffer.late1.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry shadowMesh -DSHADOW_CASCADE=0 -o ${SHADER_OUT_DIR}/visbuffer.shadow0.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry shadowMesh -DSHADOW_CASCADE=1 -o ${SHADER_OUT_DIR}/visbuffer.shadow1.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry shadowMesh -DSHADOW_CASCADE=2 -o ${SHADER_OUT_DIR}/visbuffer.shadow2.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry visFs -o ${SHADER_OUT_DIR}/visbuffer.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadeCs -o ${SHADER_OUT_DIR}/shade.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/resolve.slang -target spirv -profile spirv_1_6 -stage compute -entry resolveCs -o ${SHADER_OUT_DIR}/resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCull -DCULL_FLIGHT=0 -o ${SHADER_OUT_DIR}/cull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCull -DCULL_FLIGHT=1 -o ${SHADER_OUT_DIR}/cull.comp1.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCullLate -DCULL_FLIGHT=0 -o ${SHADER_OUT_DIR}/cull.late.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCullLate -DCULL_FLIGHT=1 -o ${SHADER_OUT_DIR}/cull.late1.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cullClear -DCULL_FLIGHT=0 -o ${SHADER_OUT_DIR}/cull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cullClear -DCULL_FLIGHT=1 -o ${SHADER_OUT_DIR}/cull.clear1.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cluster.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry clusterCs -o ${SHADER_OUT_DIR}/cluster.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadowCull -o ${SHADER_OUT_DIR}/shadowcull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadowClear -o ${SHADER_OUT_DIR}/shadowcull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssr.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrCs -o ${SHADER_OUT_DIR}/ssr.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssr.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrUp -o ${SHADER_OUT_DIR}/ssr.up.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gtao.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry gtaoCs -o ${SHADER_OUT_DIR}/gtao.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cmaa2.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cmaa2Cs -o ${SHADER_OUT_DIR}/cmaa2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=0 -o ${SHADER_OUT_DIR}/hiz0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=1 -o ${SHADER_OUT_DIR}/hiz1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=2 -o ${SHADER_OUT_DIR}/hiz2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=3 -o ${SHADER_OUT_DIR}/hiz3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=4 -o ${SHADER_OUT_DIR}/hiz4.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=5 -o ${SHADER_OUT_DIR}/hiz5.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=0 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=1 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=2 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=3 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=4 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax4.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -DHIZ_MIP=5 -DHIZ_MAX=1 -o ${SHADER_OUT_DIR}/hizmax5.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry pointMesh -DPOINT_LIGHT=0 -o ${SHADER_OUT_DIR}/visbuffer.point0.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry pointMesh -DPOINT_LIGHT=1 -o ${SHADER_OUT_DIR}/visbuffer.point1.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry pointMesh -DPOINT_LIGHT=2 -o ${SHADER_OUT_DIR}/visbuffer.point2.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=0 -o ${SHADER_OUT_DIR}/cube0.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=1 -o ${SHADER_OUT_DIR}/cube1.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=2 -o ${SHADER_OUT_DIR}/cube2.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=3 -o ${SHADER_OUT_DIR}/cube3.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=4 -o ${SHADER_OUT_DIR}/cube4.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -DCUBE_FACE=5 -o ${SHADER_OUT_DIR}/cube5.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry cubeFs -o ${SHADER_OUT_DIR}/cube.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssrc.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrcCs -DCASCADE=0 -o ${SHADER_OUT_DIR}/ssrc0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssrc.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrcCs -DCASCADE=1 -o ${SHADER_OUT_DIR}/ssrc1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssrc.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrcCs -DCASCADE=2 -o ${SHADER_OUT_DIR}/ssrc2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssrc.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrcCs -DCASCADE=3 -o ${SHADER_OUT_DIR}/ssrc3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/ssrc.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry ssrcApply -o ${SHADER_OUT_DIR}/ssrc.apply.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/radiance.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcTrace -o ${SHADER_OUT_DIR}/radiance.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/radiance.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcApply -o ${SHADER_OUT_DIR}/radiance.apply.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -target spirv -profile spirv_1_6 -stage mesh -entry tonemapMesh -o ${SHADER_OUT_DIR}/tonemap.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -target spirv -profile spirv_1_6 -stage fragment -entry tonemapFs -o ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomDown -DBLOOM_LEVEL=0 -o ${SHADER_OUT_DIR}/bloom0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomDown -DBLOOM_LEVEL=1 -o ${SHADER_OUT_DIR}/bloom1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomDown -DBLOOM_LEVEL=2 -o ${SHADER_OUT_DIR}/bloom2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomDown -DBLOOM_LEVEL=3 -o ${SHADER_OUT_DIR}/bloom3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomUp -DBLOOM_LEVEL=0 -o ${SHADER_OUT_DIR}/bloomup0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomUp -DBLOOM_LEVEL=1 -o ${SHADER_OUT_DIR}/bloomup1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry bloomUp -DBLOOM_LEVEL=2 -o ${SHADER_OUT_DIR}/bloomup2.comp.spv
    DEPENDS
        ${SHADER_DIR}/visbuffer.slang
        ${SHADER_DIR}/frame.slang
        ${SHADER_DIR}/shade.slang
        ${SHADER_DIR}/resolve.slang
        ${SHADER_DIR}/cull.slang
        ${SHADER_DIR}/cluster.slang
        ${SHADER_DIR}/shadowcull.slang
        ${SHADER_DIR}/ssr.slang
        ${SHADER_DIR}/gtao.slang
        ${SHADER_DIR}/cmaa2.slang
        ${SHADER_DIR}/bloom.slang
        ${SHADER_DIR}/hiz.slang
        ${SHADER_DIR}/cube.slang
        ${SHADER_DIR}/ssrc.slang
        ${SHADER_DIR}/radiance.slang
        ${SHADER_DIR}/tonemap.slang
    COMMENT "slangc vis/shade/ssr/tonemap → SPIR-V 1.6"
)
add_custom_target(BurnhopeSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.mesh1.spv
    ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.late1.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.frag.spv
    ${SHADER_OUT_DIR}/shade.comp.spv
    ${SHADER_OUT_DIR}/resolve.comp.spv
    ${SHADER_OUT_DIR}/cull.comp.spv
    ${SHADER_OUT_DIR}/cull.comp1.spv
    ${SHADER_OUT_DIR}/cull.late.spv
    ${SHADER_OUT_DIR}/cull.late1.spv
    ${SHADER_OUT_DIR}/cull.clear.spv
    ${SHADER_OUT_DIR}/cull.clear1.spv
    ${SHADER_OUT_DIR}/ssr.comp.spv
    ${SHADER_OUT_DIR}/ssr.up.comp.spv
    ${SHADER_OUT_DIR}/gtao.comp.spv
    ${SHADER_OUT_DIR}/cmaa2.comp.spv
    ${SHADER_OUT_DIR}/ssrc0.comp.spv
    ${SHADER_OUT_DIR}/ssrc1.comp.spv
    ${SHADER_OUT_DIR}/ssrc2.comp.spv
    ${SHADER_OUT_DIR}/ssrc3.comp.spv
    ${SHADER_OUT_DIR}/ssrc.apply.comp.spv
    ${SHADER_OUT_DIR}/radiance.trace.comp.spv
    ${SHADER_OUT_DIR}/radiance.apply.comp.spv
    ${SHADER_OUT_DIR}/tonemap.mesh.spv
    ${SHADER_OUT_DIR}/tonemap.frag.spv
)

set(UI_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/core/ui/ui.slang)
add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/ui.vert.spv
        ${SHADER_OUT_DIR}/ui.frag.spv
        ${SHADER_OUT_DIR}/ui.mesh.spv
        ${SHADER_OUT_DIR}/msdf.mesh.spv
        ${SHADER_OUT_DIR}/msdf.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile spirv_1_6 -stage vertex -entry uiVs -o ${SHADER_OUT_DIR}/ui.vert.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile spirv_1_6 -stage fragment -entry uiFs -o ${SHADER_OUT_DIR}/ui.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile spirv_1_6 -stage mesh -entry uiMs -o ${SHADER_OUT_DIR}/ui.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/core/text/msdf.slang -target spirv -profile spirv_1_6 -stage mesh -entry glyphMs -o ${SHADER_OUT_DIR}/msdf.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/core/text/msdf.slang -target spirv -profile spirv_1_6 -stage fragment -entry glyphFs -o ${SHADER_OUT_DIR}/msdf.frag.spv
    DEPENDS ${UI_SHADER} ${CMAKE_CURRENT_SOURCE_DIR}/core/text/msdf.slang
    COMMENT "slangc ui + msdf → SPIR-V 1.6"
)
add_custom_target(UiSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/ui.vert.spv
    ${SHADER_OUT_DIR}/ui.frag.spv
    ${SHADER_OUT_DIR}/ui.mesh.spv
    ${SHADER_OUT_DIR}/msdf.mesh.spv
    ${SHADER_OUT_DIR}/msdf.frag.spv
)

set(COMPRESS_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/core/image/compress.slang)
add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/compress.comp.spv
        ${SHADER_OUT_DIR}/compress_bc3.comp.spv
        ${SHADER_OUT_DIR}/compress_bc4.comp.spv
        ${SHADER_OUT_DIR}/compress_bc5.comp.spv
        ${SHADER_OUT_DIR}/compress_bc7.comp.spv
        ${SHADER_OUT_DIR}/compress_bc6h.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc1 -o ${SHADER_OUT_DIR}/compress.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc3 -o ${SHADER_OUT_DIR}/compress_bc3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc4 -o ${SHADER_OUT_DIR}/compress_bc4.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc5 -o ${SHADER_OUT_DIR}/compress_bc5.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc7 -o ${SHADER_OUT_DIR}/compress_bc7.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile spirv_1_6 -stage compute -entry csBc6h -o ${SHADER_OUT_DIR}/compress_bc6h.comp.spv
    DEPENDS ${COMPRESS_SHADER}
    COMMENT "slangc compress BC1/3/4/5/7 + BC6H mode 11"
)
add_custom_target(CompressSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/compress.comp.spv
    ${SHADER_OUT_DIR}/compress_bc3.comp.spv
    ${SHADER_OUT_DIR}/compress_bc4.comp.spv
    ${SHADER_OUT_DIR}/compress_bc5.comp.spv
    ${SHADER_OUT_DIR}/compress_bc7.comp.spv
    ${SHADER_OUT_DIR}/compress_bc6h.comp.spv
)
target_compile_definitions(bh_image PRIVATE
    "BH_COMPRESS_BC1=\"${SHADER_OUT_DIR}/compress.comp.spv\""
    "BH_COMPRESS_BC3=\"${SHADER_OUT_DIR}/compress_bc3.comp.spv\""
    "BH_COMPRESS_BC4=\"${SHADER_OUT_DIR}/compress_bc4.comp.spv\""
    "BH_COMPRESS_BC5=\"${SHADER_OUT_DIR}/compress_bc5.comp.spv\""
    "BH_COMPRESS_BC7=\"${SHADER_OUT_DIR}/compress_bc7.comp.spv\""
    "BH_COMPRESS_BC6H=\"${SHADER_OUT_DIR}/compress_bc6h.comp.spv\""
)
add_dependencies(bh_image CompressSpv)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(UI_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/ui)
file(MAKE_DIRECTORY ${UI_BIN_DIR})
add_custom_command(
    OUTPUT ${UI_BIN_DIR}/shell.uib
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/shell.json ${UI_BIN_DIR}/shell.uib
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/shell.json
    COMMENT "bake shell.json → shell.uib"
)
add_custom_command(
    OUTPUT ${UI_BIN_DIR}/sample.uib
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/sample.json ${UI_BIN_DIR}/sample.uib
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/sample.json
    COMMENT "bake sample.json → sample.uib"
)
add_custom_target(UiBin DEPENDS ${UI_BIN_DIR}/shell.uib ${UI_BIN_DIR}/sample.uib)

add_executable(BurnhopeEngine demo/app/Main.cpp demo/Sample.cpp)
target_link_libraries(BurnhopeEngine PRIVATE
    bh_app bh_host bh_files bh_ui bh_image bh_rhi bh_memory bh_platform bh_render bh_gpu_scene bh_asset bh_io
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeEngine PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_DEMO} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeEngine PRIVATE ${BH_DEFS}
    "BH_UI_SHELL=\"${UI_BIN_DIR}/shell.uib\""
    "BH_UI_SAMPLE=\"${UI_BIN_DIR}/sample.uib\""
)
target_compile_options(BurnhopeEngine PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeEngine BurnhopeSpv UiSpv UiBin)

add_executable(BurnhopeHostTick tests/HostTick.cpp)
target_link_libraries(BurnhopeHostTick PRIVATE
    bh_host bh_ui bh_image bh_rhi bh_memory bh_platform
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads Freetype::Freetype
)
target_include_directories(BurnhopeHostTick PRIVATE ${BH_CORE} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeHostTick PRIVATE ${BH_DEFS})
target_compile_options(BurnhopeHostTick PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeHostTick UiSpv)

add_executable(BurnhopeTest tests/Core.cpp)
target_link_libraries(BurnhopeTest PRIVATE
    bh_files bh_ui bh_image bh_rhi bh_memory bh_platform bh_wasm bh_io
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeTest PRIVATE ${BH_CORE} ${BH_ENGINE} ${vma_SOURCE_DIR}/include)
target_compile_options(BurnhopeTest PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeTest UiSpv)
