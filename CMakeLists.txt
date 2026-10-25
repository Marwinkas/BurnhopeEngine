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

FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.3.0
    SOURCE_SUBDIR Build
)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR bh-header-only
)
FetchContent_MakeAvailable(miniaudio)
if (TARGET Jolt)
    set_target_properties(Jolt PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON SYSTEM TRUE)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(Jolt PRIVATE -w)
    endif()
endif()

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
    SOURCE_SUBDIR yoga
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(yoga)

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
# Слоевые брандмауэры include. Нижний слой не видит верхний, UI-ядро не видит RHI и виджеты.
function(bh_firewall rule pattern)
    foreach(bh_fw_file ${ARGN})
        file(READ ${bh_fw_file} bh_fw_text)
        if(bh_fw_text MATCHES "#[ \t]*include[ \t]*[\"<](${pattern})")
            message(FATAL_ERROR "${rule}: ${bh_fw_file}")
        endif()
    endforeach()
endfunction()
file(GLOB_RECURSE BH_RHI_SOURCES CONFIGURE_DEPENDS ${BH_CORE}/rhi/*.cpp ${BH_CORE}/rhi/*.hpp)
file(GLOB BH_UI_CORE_SOURCES CONFIGURE_DEPENDS ${BH_CORE}/ui/*.cpp ${BH_CORE}/ui/*.hpp)
file(GLOB_RECURSE BH_UI_WIDGET_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/ui/widget/*.cpp ${BH_CORE}/ui/widget/*.hpp
    ${BH_CORE}/ui/kit/*.cpp ${BH_CORE}/ui/kit/*.hpp)
bh_firewall("core/rhi must not import Window, ui, files, host or SDL3"
    "platform/Window\\.hpp|ui/|files/|host/|SDL3/" ${BH_RHI_SOURCES})
bh_firewall("core/ui must not import rhi, image, ui/rhi, widgets, kits, files or host"
    "rhi/|image/|ui/rhi/|ui/widget/|ui/kit/|files/|host/" ${BH_UI_CORE_SOURCES})
bh_firewall("core/ui/widget and core/ui/kit must not import rhi, image, ui/rhi, files, host or yoga"
    "rhi/|image/|ui/rhi/|files/|host/|yoga/" ${BH_UI_WIDGET_SOURCES})
file(GLOB BH_UI_WIDGET_HEADERS CONFIGURE_DEPENDS ${BH_CORE}/ui/widget/*.hpp ${BH_CORE}/ui/Command.hpp)
list(REMOVE_ITEM BH_UI_WIDGET_HEADERS ${BH_CORE}/ui/widget/Widgets.hpp)
bh_firewall("widget headers must not import ui/State.hpp or flecs; include it from the .cpp"
    "ui/State\\.hpp|flecs\\.h" ${BH_UI_WIDGET_HEADERS})
file(GLOB BH_UI_BRICK_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/ui/widget/Label.* ${BH_CORE}/ui/widget/Button.* ${BH_CORE}/ui/widget/Check.*
    ${BH_CORE}/ui/widget/Field.* ${BH_CORE}/ui/widget/FieldFilter.* ${BH_CORE}/ui/widget/TextEdit.*
    ${BH_CORE}/ui/widget/Search.* ${BH_CORE}/ui/widget/Number.* ${BH_CORE}/ui/widget/NumberEdit.*
    ${BH_CORE}/ui/widget/Slider.* ${BH_CORE}/ui/widget/Scroll.* ${BH_CORE}/ui/widget/Splitter.*
    ${BH_CORE}/ui/widget/Fold.* ${BH_CORE}/ui/widget/Tip.* ${BH_CORE}/ui/widget/Curve.*
    ${BH_CORE}/ui/widget/Gradient.* ${BH_CORE}/ui/widget/Image.* ${BH_CORE}/ui/widget/Tabs.*)
bh_firewall("atomic bricks must not import composite kits"
    "([a-z]+/)*(Virtual|VirtualGrid|TreeView|Thumb|Menu|Choice|Popover|Breadcrumbs|AssetSlot|DragDrop|Rubberband|Chrome|Docking|Calendar|ColorPicker|Widgets)\\.hpp" ${BH_UI_BRICK_SOURCES})
file(GLOB_RECURSE BH_T0_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/memory/*.cpp ${BH_CORE}/memory/*.hpp ${BH_CORE}/io/*.cpp ${BH_CORE}/io/*.hpp
    ${BH_CORE}/wasm/*.cpp ${BH_CORE}/wasm/*.hpp ${BH_CORE}/color/*.hpp ${BH_CORE}/time/*.hpp
    ${BH_CORE}/doc/*.hpp ${BH_CORE}/text/*.hpp
    ${BH_CORE}/platform/Check.* ${BH_CORE}/platform/PlatformUtil.* ${BH_CORE}/platform/Jobs.*
    ${BH_CORE}/platform/Desktop.* ${BH_CORE}/platform/Trace.hpp)
file(GLOB BH_T1_SOURCES CONFIGURE_DEPENDS ${BH_CORE}/platform/*.cpp ${BH_CORE}/platform/*.hpp)
list(REMOVE_ITEM BH_T1_SOURCES ${BH_CORE}/platform/Window.cpp ${BH_CORE}/platform/WindowEvents.cpp)
file(GLOB_RECURSE BH_T2_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/gfx/*.cpp ${BH_CORE}/gfx/*.hpp ${BH_CORE}/asset/*.cpp ${BH_CORE}/asset/*.hpp
    ${BH_CORE}/audio/*.cpp ${BH_CORE}/audio/*.hpp ${BH_CORE}/scene/*.cpp ${BH_CORE}/scene/*.hpp
    ${BH_CORE}/stream/*.cpp ${BH_CORE}/stream/*.hpp)
file(GLOB_RECURSE BH_T5_SOURCES CONFIGURE_DEPENDS
    ${BH_ENGINE}/gpu_scene/*.cpp ${BH_ENGINE}/gpu_scene/*.hpp
    ${BH_ENGINE}/include/render/*.cpp ${BH_ENGINE}/include/render/*.hpp
    ${BH_ENGINE}/src/*.cpp ${BH_ENGINE}/src/*.hpp)
bh_firewall("tier 0 foundation must not import rhi, image, Window, ui, files, host, gfx, asset, audio, volk or SDL3"
    "rhi/|image/|platform/Window\\.hpp|ui/|files/|host/|gfx/|asset/|audio/|volk\\.h|SDL3/" ${BH_T0_SOURCES})
bh_firewall("tier 1 platform must not import rhi, image, ui, files, host, gfx, asset, audio or volk"
    "rhi/|image/|ui/|files/|host/|gfx/|asset/|audio/|volk\\.h" ${BH_T1_SOURCES})
bh_firewall("tier 2 world and asset data must not import ui, files, host or Window"
    "ui/|files/|host/|platform/Window\\.hpp" ${BH_T2_SOURCES})
bh_firewall("tier 5 engine must not import ui, files, host or Window"
    "ui/|files/|host/|platform/Window\\.hpp" ${BH_T5_SOURCES})
file(GLOB_RECURSE BH_NOWINDOW_SOURCES CONFIGURE_DEPENDS
    ${BH_CORE}/image/*.cpp ${BH_CORE}/image/*.hpp ${BH_CORE}/files/*.cpp ${BH_CORE}/files/*.hpp
    ${BH_CORE}/audio/*.cpp ${BH_CORE}/audio/*.hpp)
bh_firewall("image, files and audio must not import Window, Action, Dialog or SDL3"
    "platform/(Window|Action|Dialog)\\.hpp|SDL3/" ${BH_NOWINDOW_SOURCES})
file(GLOB_RECURSE BH_FILES_SOURCES CONFIGURE_DEPENDS ${BH_CORE}/files/*.cpp ${BH_CORE}/files/*.hpp)
bh_firewall("core/files must not import asset" "asset/" ${BH_FILES_SOURCES})
file(GLOB BH_INSPECTOR_SOURCES CONFIGURE_DEPENDS ${BH_CORE}/host/PropGrid.* ${BH_CORE}/host/PropFilter.*
    ${BH_CORE}/host/PropBind.* ${BH_CORE}/host/PropImport*)
bh_firewall("ui inspector must not import rhi, Window, ui/rhi or Host"
    "rhi/|platform/Window\\.hpp|ui/rhi/|host/Host\\.hpp" ${BH_INSPECTOR_SOURCES})
file(GLOB BH_RENDER_HEADERS CONFIGURE_DEPENDS ${BH_ENGINE}/include/render/*.hpp)
bh_firewall("engine/include/render headers must not import flecs or the RTXDI context" "flecs\\.h|Rtxdi/DI/ReSTIRDI\\.h" ${BH_RENDER_HEADERS})
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
set(BH_OPTS -Wall -Wextra -Wpedantic -std=c++26 -fno-rtti -fno-exceptions -Wno-missing-field-initializers -Wno-deprecated-declarations)

function(bh_module name)
    add_library(bh_${name} OBJECT ${ARGN})
    target_include_directories(bh_${name} PUBLIC ${BH_INCLUDES})
    target_compile_definitions(bh_${name} PUBLIC ${BH_DEFS})
    target_compile_options(bh_${name} PRIVATE ${BH_OPTS})
    target_link_libraries(bh_${name} PUBLIC spdlog::spdlog)
endfunction()

option(BH_TRACY "Pull Tracy from https://github.com/wolfpld/tracy and mark frame zones" OFF)
option(BH_ASAN "Debug address and undefined sanitizers" OFF)
option(BH_TSAN "Debug thread sanitizer" OFF)

bh_module(foundation
    core/platform/Check.cpp
    core/platform/Jobs.cpp
    core/platform/Desktop.cpp
    core/platform/PlatformUtil.cpp
    core/memory/FrameArena.cpp
    core/io/File.cpp
    core/io/Json.cpp
    core/wasm/WasmHost.cpp
)
set_source_files_properties(core/platform/Check.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-frtti")
find_package(cpptrace CONFIG REQUIRED)
target_link_libraries(bh_foundation PUBLIC cpptrace::cpptrace)

bh_module(platform
    core/platform/Window.cpp
    core/platform/WindowEvents.cpp
    core/platform/Dialog.cpp
    core/platform/Action.cpp
)
target_link_libraries(bh_platform PUBLIC bh_foundation)

if(BH_TRACY)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    target_compile_definitions(bh_foundation PUBLIC BH_TRACY)
    target_link_libraries(bh_foundation PUBLIC TracyClient)
endif()

if(BH_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(BH_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
bh_module(material core/gfx/Material.cpp)
target_link_libraries(bh_material PUBLIC bh_foundation)
bh_module(asset core/asset/AssimpImporter.cpp core/asset/ProjectDoc.cpp)
target_link_libraries(bh_asset PUBLIC assimp::assimp meshoptimizer bh_foundation bh_material)
set_source_files_properties(core/asset/AssimpImporter.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-frtti")
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
target_link_libraries(bh_image PUBLIC bh_rhi bh_texcomp bh_foundation volk)
set_source_files_properties(core/image/ImageDecode.cpp PROPERTIES COMPILE_OPTIONS "-w")
bh_module(gpu_scene engine/gpu_scene/SceneObject.cpp engine/gpu_scene/SceneGizmo.cpp engine/gpu_scene/Physics.cpp)
target_include_directories(bh_gpu_scene PUBLIC ${BH_ENGINE} ${BH_ENGINE}/include)
target_link_libraries(bh_gpu_scene PUBLIC flecs_static Jolt)
bh_module(audio core/audio/Audio.cpp)
target_include_directories(bh_audio PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/steamaudio/include
)
target_link_libraries(bh_audio PUBLIC
    bh_foundation
    "${CMAKE_SOURCE_DIR}/steamaudio/lib/linux-x64/libphonon.so"
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
set_source_files_properties(core/audio/Audio.cpp PROPERTIES COMPILE_OPTIONS "-w")

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
    engine/src/Particles.cpp
    engine/src/Fog.cpp
    engine/src/Rain.cpp
    engine/src/Weather.cpp
    engine/src/SceneFrame.cpp
    engine/src/SceneActors.cpp
    engine/src/SceneGeometry.cpp
    engine/src/SceneCullUpload.cpp
    engine/src/SceneProcedural.cpp
    engine/src/SceneTargets.cpp
    engine/src/SceneTextures.cpp
    engine/src/SceneUniforms.cpp
    engine/src/SceneLights.cpp
)
target_include_directories(bh_render PUBLIC ${BH_ENGINE} ${BH_ENGINE}/include ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include)
set_source_files_properties(
    RTXDI/Libraries/Rtxdi/Source/ReSTIRDI.cpp
    RTXDI/Libraries/Rtxdi/Source/RtxdiUtils.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(bh_render PUBLIC "BH_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/shaders\"")
target_link_libraries(bh_render PUBLIC volk flecs_static Jolt bh_audio bh_gpu_scene)
bh_module(ui_core
    core/ui/Command.cpp
    core/ui/FlexAttr.cpp
    core/ui/Focus.cpp
    core/ui/Hit.cpp
    core/ui/Hover.cpp
    core/ui/Input.cpp
    core/ui/Json.cpp
    core/ui/LayoutBridge.cpp
    core/ui/Motion.cpp
    core/ui/Node.cpp
    core/ui/NodeDump.cpp
    core/ui/Panel.cpp
    core/ui/PrimitiveBatch.cpp
    core/ui/Text.cpp
    core/ui/UiBin.cpp
    core/ui/UiWindowBridge.cpp
)
bh_module(ui_bricks
    core/ui/widget/Button.cpp
    core/ui/widget/Check.cpp
    core/ui/widget/Curve.cpp
    core/ui/widget/Field.cpp
    core/ui/widget/FieldDraw.cpp
    core/ui/widget/FieldFilter.cpp
    core/ui/widget/Fold.cpp
    core/ui/widget/Gradient.cpp
    core/ui/widget/Image.cpp
    core/ui/widget/Label.cpp
    core/ui/widget/Number.cpp
    core/ui/widget/NumberEdit.cpp
    core/ui/widget/Scroll.cpp
    core/ui/widget/Search.cpp
    core/ui/widget/Slider.cpp
    core/ui/widget/Splitter.cpp
    core/ui/widget/Tabs.cpp
    core/ui/widget/TextEdit.cpp
    core/ui/widget/Tip.cpp
)
bh_module(ui_kits
    core/ui/kit/Calendar.cpp
    core/ui/kit/Color.cpp
    core/ui/kit/ColorPopup.cpp
    core/ui/kit/ColorDraw.cpp
    core/ui/widget/AssetSlot.cpp
    core/ui/widget/Breadcrumbs.cpp
    core/ui/widget/Choice.cpp
    core/ui/widget/Chrome.cpp
    core/ui/widget/Docking.cpp
    core/ui/widget/DragDrop.cpp
    core/ui/widget/Menu.cpp
    core/ui/widget/Popover.cpp
    core/ui/widget/Rubberband.cpp
    core/ui/widget/Thumb.cpp
    core/ui/widget/TreeView.cpp
    core/ui/widget/Virtual.cpp
    core/ui/widget/VirtualGrid.cpp
    core/ui/widget/Widgets.cpp
)
bh_module(ui_rhi
    core/ui/rhi/Canvas.cpp
    core/ui/rhi/Font.cpp
    core/ui/rhi/UiPhoto.cpp
    core/ui/rhi/UiRenderer.cpp
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
target_link_libraries(bh_files PUBLIC bh_foundation bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_material)
bh_module(ui_inspector
    core/host/PropBind.cpp
    core/host/PropImport.cpp
    core/host/PropImportParse.cpp
    core/host/PropGrid.cpp
    core/host/PropFilter.cpp
)
target_link_libraries(bh_ui_inspector PUBLIC bh_ui_kits bh_ui_bricks bh_ui_core bh_foundation)
bh_module(host
    core/host/Host.cpp
    core/host/SceneMask.cpp
)
target_link_libraries(bh_host PUBLIC volk bh_ui_inspector bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_rhi bh_platform bh_foundation)
bh_module(app
    demo/app/Engine.cpp
    demo/app/HudAct.cpp
    demo/app/HudMat.cpp
    demo/app/HudMaterial.cpp
    demo/app/MatPage.cpp
    demo/app/MatEdit.cpp
    demo/app/MatSlot.cpp
    demo/app/MatPreview.cpp
    demo/app/HudPick.cpp
    demo/app/HudToolbar.cpp
    demo/app/HudView.cpp
    demo/app/HudInspector.cpp
    demo/app/HudPost.cpp
    demo/app/HudSync.cpp
    demo/app/HudPush.cpp
    demo/app/ProjectGate.cpp
    demo/app/SceneBlockFill.cpp
)
target_include_directories(bh_app PUBLIC ${BH_DEMO} ${BH_ENGINE} ${BH_ENGINE}/include)
target_link_libraries(bh_app PUBLIC bh_ui_inspector bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_files bh_gpu_scene)
target_link_libraries(bh_ui_core PUBLIC bh_foundation yogacore flecs_static)
target_link_libraries(bh_ui_bricks PUBLIC bh_ui_core bh_foundation)
target_link_libraries(bh_ui_kits PUBLIC bh_ui_bricks)
target_link_libraries(bh_ui_rhi PUBLIC bh_ui_core bh_rhi bh_image volk Freetype::Freetype)
target_compile_definitions(bh_ui_rhi PRIVATE
    "BH_UI_VERT=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.vert.spv\""
    "BH_UI_FRAG=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.frag.spv\""
    "BH_UI_MESH=\"${CMAKE_CURRENT_BINARY_DIR}/shaders/ui.mesh.spv\""
)

set_source_files_properties(core/rhi/VolkVma.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(bh_rhi PUBLIC volk Vulkan::Vulkan)
target_link_libraries(bh_platform PUBLIC SDL3::SDL3)

find_program(SLANGC_EXECUTABLE slangc REQUIRED)
set(BH_SPV spirv_1_6+spvRayQueryKHR+spvGroupNonUniform+spvGroupNonUniformBallot+spvGroupNonUniformShuffle+spvGroupNonUniformArithmetic+spvDerivativeControl+spvImageQuery+spvImageGatherExtended+spvSparseResidency+spvMinLod+spvFragmentFullyCoveredEXT+SPV_GOOGLE_user_type)
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
        ${SHADER_OUT_DIR}/cloud.shadow.comp.spv
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
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry visMesh -o ${SHADER_OUT_DIR}/visbuffer.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry visMesh -DLATE=1 -o ${SHADER_OUT_DIR}/visbuffer.late.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry shadowMesh -DSHADOW_CASCADE=0 -o ${SHADER_OUT_DIR}/visbuffer.shadow.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry shadowFs -o ${SHADER_OUT_DIR}/visbuffer.shadow.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry visFs -o ${SHADER_OUT_DIR}/visbuffer.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_sky.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadeSky -o ${SHADER_OUT_DIR}/shade.sky.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_sun.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadeSun -o ${SHADER_OUT_DIR}/shade.sun.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_punctual.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadePunctual -o ${SHADER_OUT_DIR}/shade.punctual.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_presample.slang -I ${SHADER_DIR}/restir_inc -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.presample.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_di.slang -I ${SHADER_DIR}/restir_inc -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXDI/Libraries/Rtxdi/Include -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.di.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/restir_fast.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/restir.fast.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shade_fog.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadeFog -o ${SHADER_OUT_DIR}/shade.fog.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/contact.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry contactCs -o ${SHADER_OUT_DIR}/contact.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/resolve.slang -target spirv -profile ${BH_SPV} -stage compute -entry resolveCs -o ${SHADER_OUT_DIR}/resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry meshletCull -o ${SHADER_OUT_DIR}/cull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry meshletCullLate -o ${SHADER_OUT_DIR}/cull.late.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cull.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cullClear -o ${SHADER_OUT_DIR}/cull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cluster.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry clusterCs -o ${SHADER_OUT_DIR}/cluster.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadowCull -o ${SHADER_OUT_DIR}/shadowcull.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/shadowcull.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry shadowClear -o ${SHADER_OUT_DIR}/shadowcull.clear.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gtao.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry gtaoCs -o ${SHADER_OUT_DIR}/gtao.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cmaa2.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cmaa2Cs -o ${SHADER_OUT_DIR}/cmaa2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/smaa.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry smaaCs -o ${SHADER_OUT_DIR}/smaa.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/hiz.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry hizCs -o ${SHADER_OUT_DIR}/hizmax.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/visbuffer.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry pointMesh -DPOINT_LIGHT=0 -o ${SHADER_OUT_DIR}/visbuffer.point.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry cubeMesh -o ${SHADER_OUT_DIR}/cube.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry cubeFs -o ${SHADER_OUT_DIR}/cube.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cube_fill.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cubeFill -o ${SHADER_OUT_DIR}/cube.fill.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/probe.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry probeBake -o ${SHADER_OUT_DIR}/probe.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry skyTransmittance -o ${SHADER_OUT_DIR}/sky.trans.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry skyMulti -o ${SHADER_OUT_DIR}/sky.ms.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_lut.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry skyView -o ${SHADER_OUT_DIR}/sky.view.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud_march.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cloudMarchCs -o ${SHADER_OUT_DIR}/cloud.march.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud_shadow.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cloudShadowCs -o ${SHADER_OUT_DIR}/cloud.shadow.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cloudUp -o ${SHADER_OUT_DIR}/cloud.up.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/cloud_noise.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry cloudNoise -o ${SHADER_OUT_DIR}/cloud.noise.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sky_probe.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry skyProbe -o ${SHADER_OUT_DIR}/sky.probe.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reflect.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry reflectTrace -o ${SHADER_OUT_DIR}/reflect.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reflect.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry reflectUp -o ${SHADER_OUT_DIR}/reflect.up.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_alloc_probes.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcAlloc -o ${SHADER_OUT_DIR}/rc.alloc.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_prefix.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcPrefix -o ${SHADER_OUT_DIR}/rc.prefix.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_trace_split.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcTrace -o ${SHADER_OUT_DIR}/rc.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_merge.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcMerge -o ${SHADER_OUT_DIR}/rc.merge.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_octahedral.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcOctahedral -o ${SHADER_OUT_DIR}/rc.oct.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rc_shade.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rcShade -o ${SHADER_OUT_DIR}/rc.shade.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_update.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry sharcUpdate -o ${SHADER_OUT_DIR}/sharc.update.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_resolve.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry sharcResolve -o ${SHADER_OUT_DIR}/sharc.resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_query.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry sharcQuery -o ${SHADER_OUT_DIR}/sharc.query.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_relax_temporal.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry sharcRelaxTemporal -o ${SHADER_OUT_DIR}/sharc.relax.temporal.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sharc_relax_spatial.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry sharcRelaxSpatial -o ${SHADER_OUT_DIR}/sharc.relax.spatial.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/reblur.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/reblur.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/sigma.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/sigma.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/relax.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/relax.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_clear.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry vxClear -o ${SHADER_OUT_DIR}/vx.clear.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_voxel.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry vxVoxel -o ${SHADER_OUT_DIR}/vx.voxel.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_resolve.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry vxResolve -o ${SHADER_OUT_DIR}/vx.resolve.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/vx_trace.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry vxTrace -o ${SHADER_OUT_DIR}/vx.trace.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry tonemapMesh -o ${SHADER_OUT_DIR}/tonemap.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/tonemap.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry tonemapFs -o ${SHADER_OUT_DIR}/tonemap.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/taa.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry taaCs -o ${SHADER_OUT_DIR}/taa.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/taa.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry taaCopy -o ${SHADER_OUT_DIR}/taacopy.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/fog_volume.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry fogScatter -o ${SHADER_OUT_DIR}/fog.scatter.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/fog_volume.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry fogIntegrate -o ${SHADER_OUT_DIR}/fog.integrate.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/fog_volume.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry fogApply -o ${SHADER_OUT_DIR}/fog.apply.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rain.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry rainSim -o ${SHADER_OUT_DIR}/rain.sim.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rain.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry rainMesh -o ${SHADER_OUT_DIR}/rain.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/rain.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry rainFs -o ${SHADER_OUT_DIR}/rain.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/particle.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry particleSim -o ${SHADER_OUT_DIR}/particle.sim.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/particle.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry particleMesh -o ${SHADER_OUT_DIR}/particle.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/particle.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry particleFs -o ${SHADER_OUT_DIR}/particle.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gizmo.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage mesh -entry gizmoMesh -o ${SHADER_OUT_DIR}/gizmo.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/gizmo.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage fragment -entry gizmoFs -o ${SHADER_OUT_DIR}/gizmo.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomDown -DBLOOM_LEVEL=0 -o ${SHADER_OUT_DIR}/bloom0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomDown -DBLOOM_LEVEL=1 -o ${SHADER_OUT_DIR}/bloom1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomDown -DBLOOM_LEVEL=2 -o ${SHADER_OUT_DIR}/bloom2.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomDown -DBLOOM_LEVEL=3 -o ${SHADER_OUT_DIR}/bloom3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomUp -DBLOOM_LEVEL=0 -o ${SHADER_OUT_DIR}/bloomup0.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomUp -DBLOOM_LEVEL=1 -o ${SHADER_OUT_DIR}/bloomup1.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/bloom.slang -I ${SHADER_DIR} -target spirv -profile ${BH_SPV} -stage compute -entry bloomUp -DBLOOM_LEVEL=2 -o ${SHADER_OUT_DIR}/bloomup2.comp.spv
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
        ${SHADER_DIR}/cloud_march.slang
        ${SHADER_DIR}/cloud_shadow.slang
        ${SHADER_DIR}/cloud_density.slang
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
        ${SHADER_DIR}/fog_volume.slang
        ${SHADER_DIR}/rain.slang
        ${SHADER_DIR}/particle.slang
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
    -D__XBATG_EXTRA_16_BIT_OPTIMISATION=0
    -fvk-b-shift 0 0 -fvk-s-shift 4 0 -fvk-t-shift 8 0 -fvk-u-shift 24 0
    -target spirv -profile spirv_1_6+spvGroupNonUniform+spvGroupNonUniformBallot+spvGroupNonUniformShuffle+spvGroupNonUniformArithmetic+spvGroupNonUniformQuad
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
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_DIR}/fsr_motion.slang -target spirv -profile ${BH_SPV} -stage compute -entry main -o ${SHADER_OUT_DIR}/fsr.motion.comp.spv
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
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile ${BH_SPV} -stage vertex -entry uiVs -o ${SHADER_OUT_DIR}/ui.vert.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile ${BH_SPV} -stage fragment -entry uiFs -o ${SHADER_OUT_DIR}/ui.frag.spv
    COMMAND ${SLANGC_EXECUTABLE} ${UI_SHADER} -target spirv -profile ${BH_SPV} -stage mesh -entry uiMs -o ${SHADER_OUT_DIR}/ui.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/core/text/msdf.slang -target spirv -profile ${BH_SPV} -stage mesh -entry glyphMs -o ${SHADER_OUT_DIR}/msdf.mesh.spv
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/core/text/msdf.slang -target spirv -profile ${BH_SPV} -stage fragment -entry glyphFs -o ${SHADER_OUT_DIR}/msdf.frag.spv
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
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc1 -o ${SHADER_OUT_DIR}/compress.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc3 -o ${SHADER_OUT_DIR}/compress_bc3.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc4 -o ${SHADER_OUT_DIR}/compress_bc4.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc5 -o ${SHADER_OUT_DIR}/compress_bc5.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc7 -o ${SHADER_OUT_DIR}/compress_bc7.comp.spv
    COMMAND ${SLANGC_EXECUTABLE} ${COMPRESS_SHADER} -target spirv -profile ${BH_SPV} -stage compute -entry csBc6h -o ${SHADER_OUT_DIR}/compress_bc6h.comp.spv
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

add_executable(BurnhopeEngine demo/app/Main.cpp)
target_link_libraries(BurnhopeEngine PRIVATE
    bh_app bh_host bh_ui_inspector bh_files bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_image bh_rhi bh_foundation bh_platform bh_render bh_audio bh_gpu_scene bh_asset bh_material
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeEngine PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_ENGINE}/include ${BH_DEMO} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeEngine PRIVATE ${BH_DEFS})
target_compile_options(BurnhopeEngine PRIVATE ${BH_OPTS})
set_target_properties(BurnhopeEngine PROPERTIES
    BUILD_RPATH "${CMAKE_SOURCE_DIR}/steamaudio/lib/linux-x64"
)
add_dependencies(BurnhopeEngine BurnhopeSpv UiSpv UiBin)

add_executable(BurnhopeSample demo/sample/Main.cpp demo/Sample.cpp demo/app/Shell.cpp)
target_link_libraries(BurnhopeSample PRIVATE
    bh_host bh_ui_inspector bh_files bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_image bh_rhi bh_foundation bh_platform bh_gpu_scene bh_asset bh_material
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads
)
target_include_directories(BurnhopeSample PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_ENGINE}/include ${BH_DEMO} ${vma_SOURCE_DIR}/include)
target_compile_definitions(BurnhopeSample PRIVATE ${BH_DEFS}
    "BH_UI_SHELL=\"${UI_BIN_DIR}/shell.uib\""
    "BH_UI_SAMPLE=\"${UI_BIN_DIR}/sample.uib\""
)
target_compile_options(BurnhopeSample PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeSample UiSpv UiBin)

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
    bh_host bh_ui_inspector bh_files bh_ui_core bh_ui_bricks bh_ui_kits bh_ui_rhi bh_image bh_rhi bh_foundation bh_platform bh_gpu_scene bh_asset bh_material
    yogacore flecs_static
    SDL3::SDL3 Vulkan::Vulkan volk spdlog::spdlog Threads::Threads Freetype::Freetype
)
target_include_directories(BurnhopeTest PRIVATE ${BH_CORE} ${BH_ENGINE} ${BH_ENGINE}/include ${vma_SOURCE_DIR}/include)
target_compile_options(BurnhopeTest PRIVATE ${BH_OPTS})
add_dependencies(BurnhopeTest UiSpv UiBin)
