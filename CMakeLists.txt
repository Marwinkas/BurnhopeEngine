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
    core/platform/Desktop.cpp
    core/platform/PlatformUtil.cpp
    core/platform/Action.cpp
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

bh_module(image
    core/image/ImageDecode.cpp
    core/image/ImageCompress.cpp
    core/image/ImageUpload.cpp)
target_include_directories(bh_image PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp
)
target_link_libraries(bh_image PUBLIC bh_rhi bh_texcomp bh_platform)
set_source_files_properties(core/image/ImageDecode.cpp PROPERTIES COMPILE_OPTIONS "-w")
add_library(bh_gpu_scene INTERFACE)
target_include_directories(bh_gpu_scene INTERFACE ${BH_INCLUDES} ${BH_ENGINE} ${BH_ENGINE}/include)
target_compile_definitions(bh_gpu_scene INTERFACE ${BH_DEFS})
bh_module(render
    engine/src/Visbuffer.cpp
    engine/src/Shade.cpp
    engine/src/Sky.cpp
    engine/src/Radiance.cpp
    engine/src/Hiz.cpp
    engine/src/Cube.cpp
    engine/src/Cull.cpp
    engine/src/Tonemap.cpp
    engine/src/PassClusters.cpp
    engine/src/PassCull.cpp
    engine/src/PassSun.cpp
    engine/src/PassPoints.cpp
    engine/src/PassVis.cpp
    engine/src/PassShade.cpp
    engine/src/PassRadiance.cpp
    engine/src/Sharc.cpp
    engine/src/PassSharc.cpp
    engine/src/Restir.cpp
    engine/src/PassRestir.cpp
    engine/src/Fsr.cpp
    engine/src/Gtao.cpp
    RTXDI/Libraries/Rtxdi/Source/ReSTIRDI.cpp
    RTXDI/Libraries/Rtxdi/Source/RtxdiUtils.cpp
    engine/src/Voxel.cpp
    engine/src/PassVoxel.cpp
    engine/src/PassTonemap.cpp
    engine/src/PassGizmo.cpp
    engine/src/SceneFrame.cpp
)
target_include_directories(bh_render PUBLIC ${BH_ENGINE} ${BH_ENGINE}/include ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include)
set_source_files_properties(
    RTXDI/Libraries/Rtxdi/Source/ReSTIRDI.cpp
    RTXDI/Libraries/Rtxdi/Source/RtxdiUtils.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(bh_render PUBLIC "BH_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/shaders\"")
target_link_libraries(bh_render PUBLIC flecs_static)
bh_module(ui
    core/ui/Canvas.cpp
    core/ui/Motion.cpp
    core/ui/rhi/UiRenderer.cpp
    core/ui/LayoutBridge.cpp
    core/ui/PrimitiveBatch.cpp
    core/ui/Font.cpp
    core/ui/Text.cpp
    core/ui/Hover.cpp
    core/ui/Panel.cpp
    core/ui/Json.cpp
    core/io/Json.cpp
    core/ui/UiBin.cpp
    core/ui/UiPhoto.cpp
    core/ui/UiWindowBridge.cpp
    core/ui/FlexAttr.cpp
    core/ui/widget/Chrome.cpp
    core/ui/widget/Docking.cpp
    core/ui/widget/Node.cpp
    core/ui/widget/Widgets.cpp
    core/ui/widget/Hit.cpp
    core/ui/widget/Focus.cpp
    core/ui/widget/TreeView.cpp
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
    core/ui/widget/VirtualGrid.cpp
    core/ui/widget/Thumb.cpp
    core/ui/widget/DragDrop.cpp
    core/ui/widget/Breadcrumbs.cpp
    core/ui/widget/Rubberband.cpp
    core/ui/widget/Menu.cpp
    core/ui/widget/Input.cpp
    core/ui/widget/Tip.cpp
    core/ui/kit/Calendar.cpp
    core/ui/kit/Color.cpp
)
bh_module(files
    core/files/Files.cpp
    core/files/FileSession.cpp
    core/files/FileBind.cpp
    core/files/FileMenu.cpp
    core/files/FileActions.cpp
    core/files/FileTheme.cpp
    core/files/Project.cpp
    core/files/Format.cpp
    core/files/Model.cpp
)
target_link_libraries(bh_files PUBLIC bh_ui)
bh_module(host core/host/Host.cpp)
target_link_libraries(bh_host PUBLIC bh_ui bh_rhi bh_platform bh_memory)
bh_module(app
    demo/app/Engine.cpp
    demo/app/Shell.cpp
)
target_include_directories(bh_app PUBLIC ${BH_DEMO} ${BH_ENGINE} ${BH_ENGINE}/include)
target_link_libraries(bh_app PUBLIC bh_ui bh_files)
target_link_libraries(bh_ui PUBLIC bh_io bh_image yogacore flecs_static Freetype::Freetype)
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
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/shader)

add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/visbuffer.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.shadow.mesh.spv
        ${SHADER_OUT_DIR}/visbuffer.shadow.frag.spv
        ${SHADER_OUT_DIR}/visbuffer.frag.spv
        ${SHADER_OUT_DIR}/shade.sky.comp.spv
        ${SHADER_OUT_DIR}/shade.sun.comp.spv
        ${SHADER_OUT_DIR}/shade.punctual.comp.spv
        ${SHADER_OUT_DIR}/restir.presample.comp.spv
        ${SHADER_OUT_DIR}/restir.di.comp.spv
        ${SHADER_OUT_DIR}/restir.fast.comp.spv
        ${SHADER_OUT_DIR}/shade.fog.comp.spv
        ${SHADER_OUT_DIR}/resolve.comp.spv
        ${SHADER_OUT_DIR}/cull.comp.spv
        ${SHADER_OUT_DIR}/cull.late.spv
        ${SHADER_OUT_DIR}/cull.clear.spv
        ${SHADER_OUT_DIR}/cluster.comp.spv
        ${SHADER_OUT_DIR}/shadowcull.comp.spv
        ${SHADER_OUT_DIR}/shadowcull.clear.spv
        ${SHADER_OUT_DIR}/gtao.comp.spv
        ${SHADER_OUT_DIR}/cmaa2.comp.spv
        ${SHADER_OUT_DIR}/smaa.comp.spv
        ${SHADER_OUT_DIR}/bloom0.comp.spv
        ${SHADER_OUT_DIR}/bloom1.comp.spv
        ${SHADER_OUT_DIR}/bloom2.comp.spv
        ${SHADER_OUT_DIR}/bloom3.comp.spv
        ${SHADER_OUT_DIR}/bloomup0.comp.spv
        ${SHADER_OUT_DIR}/bloomup1.comp.spv
        ${SHADER_OUT_DIR}/bloomup2.comp.spv
        ${SHADER_OUT_DIR}/hizmax.comp.spv
        ${SHADER_OUT_DIR}/visbuffer.point.mesh.spv
        ${SHADER_OUT_DIR}/cube.mesh.spv
        ${SHADER_OUT_DIR}/cube.frag.spv
        ${SHADER_OUT_DIR}/cube.fill.comp.spv
        ${SHADER_OUT_DIR}/probe.comp.spv
        ${SHADER_OUT_DIR}/sky.trans.comp.spv
        ${SHADER_OUT_DIR}/sky.ms.comp.spv
        ${SHADER_OUT_DIR}/sky.view.comp.spv
        ${SHADER_OUT_DIR}/cloud.march.comp.spv
        ${SHADER_OUT_DIR}/cloud.up.comp.spv
        ${SHADER_OUT_DIR}/cloud.noise.comp.spv
        ${SHADER_OUT_DIR}/contact.comp.spv
        ${SHADER_OUT_DIR}/sky.probe.comp.spv
        ${SHADER_OUT_DIR}/reflect.trace.comp.spv
        ${SHADER_OUT_DIR}/reflect.up.comp.spv
        ${SHADER_OUT_DIR}/rc.alloc.comp.spv
        ${SHADER_OUT_DIR}/rc.prefix.comp.spv
        ${SHADER_OUT_DIR}/rc.trace.comp.spv
        ${SHADER_OUT_DIR}/rc.merge.comp.spv
        ${SHADER_OUT_DIR}/rc.oct.comp.spv
        ${SHADER_OUT_DIR}/rc.shade.comp.spv
        ${SHADER_OUT_DIR}/sharc.update.comp.spv
        ${SHADER_OUT_DIR}/sharc.resolve.comp.spv
        ${SHADER_OUT_DIR}/sharc.query.comp.spv
        ${SHADER_OUT_DIR}/sharc.relax.temporal.comp.spv
        ${SHADER_OUT_DIR}/sharc.relax.spatial.comp.spv
        ${SHADER_OUT_DIR}/reblur.comp.spv
        ${SHADER_OUT_DIR}/sigma.comp.spv
        ${SHADER_OUT_DIR}/relax.comp.spv
        ${SHADER_OUT_DIR}/vx.clear.comp.spv
        ${SHADER_OUT_DIR}/vx.voxel.comp.spv
        ${SHADER_OUT_DIR}/vx.resolve.comp.spv
        ${SHADER_OUT_DIR}/vx.trace.comp.spv
        ${SHADER_OUT_DIR}/tonemap.mesh.spv
        ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -o ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry visMesh -DLATE=1 -o ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry shadowMesh -DSHADOW_CASCADE=0 -o ${SHADER_OUT_DIR}/visbuffer.shadow.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry shadowFs -o ${SHADER_OUT_DIR}/visbuffer.shadow.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry visFs -o ${SHADER_OUT_DIR}/visbuffer.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_sky.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadeSky -o ${SHADER_OUT_DIR}/shade.sky.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_sun.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadeSun -o ${SHADER_OUT_DIR}/shade.sun.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_punctual.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadePunctual -o ${SHADER_OUT_DIR}/shade.punctual.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_presample.slang -I ${SHADER_DIR}/restir_inc -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR+spvGroupNonUniform+spvGroupNonUniformShuffle+spvGroupNonUniformBallot+spvGroupNonUniformArithmetic -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.presample.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_di.slang -I ${SHADER_DIR}/restir_inc -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR+spvGroupNonUniform+spvGroupNonUniformShuffle+spvGroupNonUniformBallot+spvGroupNonUniformArithmetic -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.di.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_fast.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.fast.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_fog.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadeFog -o ${SHADER_OUT_DIR}/shade.fog.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/contact.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry contactCs -o ${SHADER_OUT_DIR}/contact.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/resolve.slang -target spirv -profile spirv_1_6 -stage compute -entry resolveCs -o ${SHADER_OUT_DIR}/resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCull -o ${SHADER_OUT_DIR}/cull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry meshletCullLate -o ${SHADER_OUT_DIR}/cull.late.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cullClear -o ${SHADER_OUT_DIR}/cull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cluster.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry clusterCs -o ${SHADER_OUT_DIR}/cluster.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadowCull -o ${SHADER_OUT_DIR}/shadowcull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry shadowClear -o ${SHADER_OUT_DIR}/shadowcull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gtao.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry gtaoCs -o ${SHADER_OUT_DIR}/gtao.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cmaa2.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cmaa2Cs -o ${SHADER_OUT_DIR}/cmaa2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/smaa.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry smaaCs -o ${SHADER_OUT_DIR}/smaa.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry hizCs -o ${SHADER_OUT_DIR}/hizmax.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry pointMesh -DPOINT_LIGHT=0 -o ${SHADER_OUT_DIR}/visbuffer.point.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry cubeMesh -o ${SHADER_OUT_DIR}/cube.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry cubeFs -o ${SHADER_OUT_DIR}/cube.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube_fill.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cubeFill -o ${SHADER_OUT_DIR}/cube.fill.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/probe.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry probeBake -o ${SHADER_OUT_DIR}/probe.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry skyTransmittance -o ${SHADER_OUT_DIR}/sky.trans.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry skyMulti -o ${SHADER_OUT_DIR}/sky.ms.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry skyView -o ${SHADER_OUT_DIR}/sky.view.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cloudMarch -o ${SHADER_OUT_DIR}/cloud.march.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cloudUp -o ${SHADER_OUT_DIR}/cloud.up.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud_noise.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry cloudNoise -o ${SHADER_OUT_DIR}/cloud.noise.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_probe.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry skyProbe -o ${SHADER_OUT_DIR}/sky.probe.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reflect.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry reflectTrace -o ${SHADER_OUT_DIR}/reflect.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reflect.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry reflectUp -o ${SHADER_OUT_DIR}/reflect.up.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_alloc_probes.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcAlloc -o ${SHADER_OUT_DIR}/rc.alloc.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_prefix.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcPrefix -o ${SHADER_OUT_DIR}/rc.prefix.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_trace_split.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry rcTrace -o ${SHADER_OUT_DIR}/rc.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_merge.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvGroupNonUniform+spvGroupNonUniformShuffle -stage compute -entry rcMerge -o ${SHADER_OUT_DIR}/rc.merge.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_octahedral.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcOctahedral -o ${SHADER_OUT_DIR}/rc.oct.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_shade.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry rcShade -o ${SHADER_OUT_DIR}/rc.shade.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_update.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry sharcUpdate -o ${SHADER_OUT_DIR}/sharc.update.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_resolve.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry sharcResolve -o ${SHADER_OUT_DIR}/sharc.resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_query.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6+spvRayQueryKHR -stage compute -entry sharcQuery -o ${SHADER_OUT_DIR}/sharc.query.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_relax_temporal.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry sharcRelaxTemporal -o ${SHADER_OUT_DIR}/sharc.relax.temporal.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_relax_spatial.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry sharcRelaxSpatial -o ${SHADER_OUT_DIR}/sharc.relax.spatial.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reblur.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry main -o ${SHADER_OUT_DIR}/reblur.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sigma.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry main -o ${SHADER_OUT_DIR}/sigma.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/relax.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry main -o ${SHADER_OUT_DIR}/relax.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_clear.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry vxClear -o ${SHADER_OUT_DIR}/vx.clear.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_voxel.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry vxVoxel -o ${SHADER_OUT_DIR}/vx.voxel.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_resolve.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry vxResolve -o ${SHADER_OUT_DIR}/vx.resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_trace.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry vxTrace -o ${SHADER_OUT_DIR}/vx.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry tonemapMesh -o ${SHADER_OUT_DIR}/tonemap.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry tonemapFs -o ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/taa.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry taaCs -o ${SHADER_OUT_DIR}/taa.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/taa.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage compute -entry taaCopy -o ${SHADER_OUT_DIR}/taacopy.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gizmo.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage mesh -entry gizmoMesh -o ${SHADER_OUT_DIR}/gizmo.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gizmo.slang -I ${SHADER_DIR} -target spirv -profile spirv_1_6 -stage fragment -entry gizmoFs -o ${SHADER_OUT_DIR}/gizmo.frag.spv
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
        ${SHADER_DIR}/shade_sky.slang
        ${SHADER_DIR}/shade_sun.slang
        ${SHADER_DIR}/shade_punctual.slang
        ${SHADER_DIR}/restir_rab.slang
        ${SHADER_DIR}/restir_presample.slang
        ${SHADER_DIR}/restir_di.slang
        ${SHADER_DIR}/restir_fast.slang
        ${SHADER_DIR}/restir_inc/Rtxdi/DI/ReservoirStorage.hlsli
        ${SHADER_DIR}/shade_fog.slang
        ${SHADER_DIR}/contact.slang
        ${SHADER_DIR}/sky_probe.slang
        ${SHADER_DIR}/reflect.slang
        ${SHADER_DIR}/shade_hit.slang
        ${SHADER_DIR}/shade_shadow.slang
        ${SHADER_DIR}/resolve.slang
        ${SHADER_DIR}/cull.slang
        ${SHADER_DIR}/cluster.slang
        ${SHADER_DIR}/shadowcull.slang
        ${SHADER_DIR}/gtao.slang
        ${SHADER_DIR}/cmaa2.slang
        ${SHADER_DIR}/smaa.slang
        ${SHADER_DIR}/bloom.slang
        ${SHADER_DIR}/hiz.slang
        ${SHADER_DIR}/cube.slang
        ${SHADER_DIR}/cube_fill.slang
        ${SHADER_DIR}/probe.slang
        ${SHADER_DIR}/sky_lut.slang
        ${SHADER_DIR}/cloud.slang
        ${SHADER_DIR}/cloud_noise.slang
        ${SHADER_DIR}/core/atmosphere.slang
        ${SHADER_DIR}/rc_common.slang
        ${SHADER_DIR}/rc_geom.slang
        ${SHADER_DIR}/rc_surface.slang
        ${SHADER_DIR}/rc_alloc_probes.slang
        ${SHADER_DIR}/rc_prefix.slang
        ${SHADER_DIR}/rc_trace_split.slang
        ${SHADER_DIR}/rc_merge.slang
        ${SHADER_DIR}/rc_octahedral.slang
        ${SHADER_DIR}/rc_shade.slang
        ${SHADER_DIR}/sharc_params.slang
        ${SHADER_DIR}/sharc_cache.slang
        ${SHADER_DIR}/sharc_hit.slang
        ${SHADER_DIR}/sharc_update.slang
        ${SHADER_DIR}/sharc_resolve.slang
        ${SHADER_DIR}/sharc_query.slang
        ${SHADER_DIR}/sharc_relax_temporal.slang
        ${SHADER_DIR}/sharc_relax_spatial.slang
        ${SHADER_DIR}/reblur.slang
        ${SHADER_DIR}/nrd_reproj.slang
        ${SHADER_DIR}/sigma.slang
        ${SHADER_DIR}/relax.slang
        ${SHADER_DIR}/vx_common.slang
        ${SHADER_DIR}/vx_clear.slang
        ${SHADER_DIR}/vx_voxel.slang
        ${SHADER_DIR}/vx_resolve.slang
        ${SHADER_DIR}/vx_trace.slang
        ${SHADER_DIR}/tonemap.slang
        ${SHADER_DIR}/taa.slang
        ${SHADER_DIR}/core/grade.slang
        ${SHADER_DIR}/gizmo.slang
        ${SHADER_DIR}/camera.slang
        ${SHADER_DIR}/core/math.slang
        ${SHADER_DIR}/core/brdf.slang
        ${SHADER_DIR}/core/hiz_atlas.slang
        ${SHADER_DIR}/core/vis_surface.slang
        ${SHADER_DIR}/shade_tile.slang
    COMMENT "slangc vis/shade/ssr/tonemap → SPIR-V 1.6"
)
set(FSR_GPU ${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX-SDK/Kits/FidelityFX/upscalers/fsr3/include/gpu)
set(FSR_API ${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX-SDK/Kits/FidelityFX/api/internal/gpu)
set(FSR_PASS ${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX-SDK/Kits/FidelityFX/upscalers/fsr3/internal/shaders)
set(FSR_FLAGS
    -I ${FSR_GPU} -I ${FSR_API}
    -DFFX_GPU=1 -DFFX_HLSL=1 -DFFX_HALF=0 -DFFX_HLSL_SM=62
    -DFFX_IMPLICIT_SHADER_REGISTER_BINDING_HLSL=0
    -DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=1
    -DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=1
    -DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1
    -DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0
    -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=0
    -fvk-b-shift 0 0 -fvk-s-shift 4 0 -fvk-t-shift 8 0 -fvk-u-shift 24 0
    -target spirv -profile spirv_1_6+spvGroupNonUniform+spvGroupNonUniformBallot+spvGroupNonUniformShuffle+spvGroupNonUniformArithmetic
    -stage compute -entry CS)
add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/fsr.prepare.comp.spv
        ${SHADER_OUT_DIR}/fsr.luma.comp.spv
        ${SHADER_OUT_DIR}/fsr.shading_pyramid.comp.spv
        ${SHADER_OUT_DIR}/fsr.shading.comp.spv
        ${SHADER_OUT_DIR}/fsr.reactivity.comp.spv
        ${SHADER_OUT_DIR}/fsr.instability.comp.spv
        ${SHADER_OUT_DIR}/fsr.accumulate.comp.spv
        ${SHADER_OUT_DIR}/fsr.rcas.comp.spv
        ${SHADER_OUT_DIR}/fsr.motion.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_prepare_inputs_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.prepare.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_luma_pyramid_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.luma.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_shading_change_pyramid_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.shading_pyramid.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_shading_change_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.shading.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_prepare_reactivity_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.reactivity.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_luma_instability_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.instability.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_accumulate_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.accumulate.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${FSR_PASS}/ffx_fsr3upscaler_rcas_pass.hlsl ${FSR_FLAGS} -o ${SHADER_OUT_DIR}/fsr.rcas.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/fsr_motion.slang -target spirv -profile spirv_1_6 -stage compute -entry main -o ${SHADER_OUT_DIR}/fsr.motion.comp.spv
    DEPENDS
        ${FSR_PASS}/ffx_fsr3upscaler_prepare_inputs_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_luma_pyramid_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_shading_change_pyramid_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_shading_change_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_prepare_reactivity_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_luma_instability_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_accumulate_pass.hlsl
        ${FSR_PASS}/ffx_fsr3upscaler_rcas_pass.hlsl
        ${FSR_GPU}/fsr3upscaler/ffx_fsr3upscaler_prepare_reactivity.h
        ${FSR_GPU}/fsr3upscaler/ffx_fsr3upscaler_upsample.h
        ${SHADER_DIR}/fsr_motion.slang
    COMMENT "slangc FSR 3.1.5 upscaler → SPIR-V 1.6"
)
set(XE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/XeGTAO/Source/Rendering/Shaders)
set(XE_FLAGS
    -I ${XE_DIR}
    -DXE_GTAO_USE_HALF_FLOAT_PRECISION=0
    -DXE_GTAO_DEFAULT_THIN_OBJECT_HEURISTIC=0
    -fvk-b-shift 0 0 -fvk-s-shift 4 0 -fvk-t-shift 8 0 -fvk-u-shift 24 0
    -target spirv -profile spirv_1_6+spvImageGatherExtended -stage compute)
add_custom_command(
    OUTPUT
        ${SHADER_OUT_DIR}/xegtao.prefilter.comp.spv
        ${SHADER_OUT_DIR}/xegtao.normals.comp.spv
        ${SHADER_OUT_DIR}/xegtao.low.comp.spv
        ${SHADER_OUT_DIR}/xegtao.medium.comp.spv
        ${SHADER_OUT_DIR}/xegtao.high.comp.spv
        ${SHADER_OUT_DIR}/xegtao.ultra.comp.spv
        ${SHADER_OUT_DIR}/xegtao.denoise.comp.spv
        ${SHADER_OUT_DIR}/xegtao.denoise_last.comp.spv
        ${SHADER_OUT_DIR}/xegtao.resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=0 -entry CSPrefilter -o ${SHADER_OUT_DIR}/xegtao.prefilter.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=1 -entry CSNormals -o ${SHADER_OUT_DIR}/xegtao.normals.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=2 -entry CSGTAOLow -o ${SHADER_OUT_DIR}/xegtao.low.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=2 -entry CSGTAOMedium -o ${SHADER_OUT_DIR}/xegtao.medium.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=2 -entry CSGTAOHigh -o ${SHADER_OUT_DIR}/xegtao.high.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=2 -entry CSGTAOUltra -o ${SHADER_OUT_DIR}/xegtao.ultra.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=3 -entry CSDenoise -o ${SHADER_OUT_DIR}/xegtao.denoise.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=3 -entry CSDenoiseLast -o ${SHADER_OUT_DIR}/xegtao.denoise_last.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/xegtao.hlsl ${XE_FLAGS} -DXE_ENTRY=4 -entry CSResolve -o ${SHADER_OUT_DIR}/xegtao.resolve.comp.spv
    DEPENDS
        ${SHADER_DIR}/xegtao.hlsl
        ${XE_DIR}/XeGTAO.h
        ${XE_DIR}/XeGTAO.hlsli
    COMMENT "slangc XeGTAO → SPIR-V 1.6"
)
add_custom_target(BurnhopeSpv ALL DEPENDS
    ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
    ${SHADER_OUT_DIR}/visbuffer.shadow.frag.spv
    ${SHADER_OUT_DIR}/visbuffer.frag.spv
    ${SHADER_OUT_DIR}/shade.sky.comp.spv
    ${SHADER_OUT_DIR}/shade.sun.comp.spv
    ${SHADER_OUT_DIR}/shade.punctual.comp.spv
    ${SHADER_OUT_DIR}/restir.presample.comp.spv
    ${SHADER_OUT_DIR}/restir.di.comp.spv
    ${SHADER_OUT_DIR}/restir.fast.comp.spv
    ${SHADER_OUT_DIR}/fsr.prepare.comp.spv
    ${SHADER_OUT_DIR}/fsr.luma.comp.spv
    ${SHADER_OUT_DIR}/fsr.shading_pyramid.comp.spv
    ${SHADER_OUT_DIR}/fsr.shading.comp.spv
    ${SHADER_OUT_DIR}/fsr.reactivity.comp.spv
    ${SHADER_OUT_DIR}/fsr.instability.comp.spv
    ${SHADER_OUT_DIR}/fsr.accumulate.comp.spv
    ${SHADER_OUT_DIR}/fsr.rcas.comp.spv
    ${SHADER_OUT_DIR}/fsr.motion.comp.spv
    ${SHADER_OUT_DIR}/xegtao.prefilter.comp.spv
    ${SHADER_OUT_DIR}/xegtao.normals.comp.spv
    ${SHADER_OUT_DIR}/xegtao.low.comp.spv
    ${SHADER_OUT_DIR}/xegtao.medium.comp.spv
    ${SHADER_OUT_DIR}/xegtao.high.comp.spv
    ${SHADER_OUT_DIR}/xegtao.ultra.comp.spv
    ${SHADER_OUT_DIR}/xegtao.denoise.comp.spv
    ${SHADER_OUT_DIR}/xegtao.denoise_last.comp.spv
    ${SHADER_OUT_DIR}/xegtao.resolve.comp.spv
    ${SHADER_OUT_DIR}/shade.fog.comp.spv
    ${SHADER_OUT_DIR}/resolve.comp.spv
    ${SHADER_OUT_DIR}/cull.comp.spv
    ${SHADER_OUT_DIR}/cull.late.spv
    ${SHADER_OUT_DIR}/cull.clear.spv
    ${SHADER_OUT_DIR}/gtao.comp.spv
    ${SHADER_OUT_DIR}/cmaa2.comp.spv
    ${SHADER_OUT_DIR}/rc.alloc.comp.spv
    ${SHADER_OUT_DIR}/rc.prefix.comp.spv
        ${SHADER_OUT_DIR}/rc.trace.comp.spv
        ${SHADER_OUT_DIR}/rc.merge.comp.spv
    ${SHADER_OUT_DIR}/rc.oct.comp.spv
    ${SHADER_OUT_DIR}/rc.shade.comp.spv
    ${SHADER_OUT_DIR}/sharc.update.comp.spv
    ${SHADER_OUT_DIR}/sharc.resolve.comp.spv
    ${SHADER_OUT_DIR}/sharc.query.comp.spv
    ${SHADER_OUT_DIR}/sharc.relax.temporal.comp.spv
    ${SHADER_OUT_DIR}/sharc.relax.spatial.comp.spv
    ${SHADER_OUT_DIR}/reblur.comp.spv
    ${SHADER_OUT_DIR}/sigma.comp.spv
    ${SHADER_OUT_DIR}/relax.comp.spv
    ${SHADER_OUT_DIR}/vx.clear.comp.spv
    ${SHADER_OUT_DIR}/vx.voxel.comp.spv
    ${SHADER_OUT_DIR}/vx.resolve.comp.spv
    ${SHADER_OUT_DIR}/vx.trace.comp.spv
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
add_custom_command(
    OUTPUT ${UI_BIN_DIR}/files.uib
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/files.json ${UI_BIN_DIR}/files.uib
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/uibake.py ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/files.json
    COMMENT "bake files.json → files.uib"
)
add_custom_target(UiBin DEPENDS ${UI_BIN_DIR}/shell.uib ${UI_BIN_DIR}/sample.uib ${UI_BIN_DIR}/files.uib)
target_compile_definitions(bh_files PRIVATE "BH_UI_FILES=\"${UI_BIN_DIR}/files.uib\"")

add_executable(BurnhopeEngine demo/app/Main.cpp demo/Sample.cpp)
target_link_libraries(BurnhopeEngine PRIVATE
    bh_app bh_host bh_files bh_ui bh_image bh_rhi bh_memory bh_platform bh_render bh_gpu_scene bh_asset bh_io
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeEngine PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_ENGINE}/include ${BH_DEMO} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeEngine PRIVATE ${BH_DEFS}
    "BH_UI_SHELL=\"${UI_BIN_DIR}/shell.uib\""
    "BH_UI_SAMPLE=\"${UI_BIN_DIR}/sample.uib\""
)
target_compile_options(BurnhopeEngine PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeEngine BurnhopeSpv UiSpv UiBin)

set(BH_TEST_SOURCES
    tests/Main.cpp
    tests/platform/TestPlatform.cpp
    tests/rhi/TestRhiDraw.cpp
    tests/rhi/TestRhiGolden.cpp
    tests/rhi/TestRhiCompute.cpp
    tests/core/TestCoreTypes.cpp
    tests/gfx/TestGfxScene.cpp
    tests/gfx/TestStreaming.cpp
    tests/ui/TestUiLayout.cpp
    tests/ui/TestUiWidgets.cpp
    tests/ui/TestUiPrimitives.cpp
    tests/ui/TestUiBinary.cpp
    tests/tools/TestFileBrowser.cpp
    tests/tools/TestEditorTools.cpp
)
add_executable(BurnhopeTest ${BH_TEST_SOURCES})
target_link_libraries(BurnhopeTest PRIVATE
    bh_host bh_files bh_ui bh_image bh_rhi bh_memory bh_platform bh_wasm bh_io
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads Freetype::Freetype
)
target_include_directories(BurnhopeTest PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_ENGINE}/include ${vma_SOURCE_DIR}/include)
target_compile_options(BurnhopeTest PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeTest UiSpv UiBin)
