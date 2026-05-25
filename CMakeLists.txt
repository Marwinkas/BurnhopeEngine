cmake_minimum_required(VERSION 3.10)
project(BurnhopeEngine)

# --- 1. Глобальные настройки проекта ---

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Математика для ImGui
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)



# --- 2. Поиск системных пакетов и внешних библиотек ---

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

include(FetchContent)

# --- Подключение Vulkan Memory Allocator ---
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        master  # Или конкретно v3.1.0, чтобы быть уверенным в фиксе
)
FetchContent_MakeAvailable(vma)

# --- Подключение DirectXMath ---
FetchContent_Declare(
    directxmath
    GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
    GIT_TAG        main
)

set(DIRECTXMATH_STANDALONE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(directxmath)

# --- Подключение Flecs ---
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(flecs)

# Фикс sal.h для Linux: просто создаем пустой файл-заглушку
set(SAL_DUMMY_DIR "${CMAKE_CURRENT_BINARY_DIR}/sal_compat")
if(NOT EXISTS "${SAL_DUMMY_DIR}/sal.h")
    file(MAKE_DIRECTORY "${SAL_DUMMY_DIR}")
    file(WRITE "${SAL_DUMMY_DIR}/sal.h" "// Dummy sal.h for Linux compatibility\n")
endif()
# Выключаем всё, что тянет лишние зависимости (DirectX, GUI, тесты)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)

# Собираем статически, чтобы не таскать .dll/.so
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Оставляем только нужные форматы (OBJ, FBX, GLTF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)

# Принудительно используем внутренний zlib и minizip (для работы с архивами внутри FBX/GLTF)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_FORCE_ZLIB ON CACHE BOOL "" FORCE)

# Фикс ошибки unzip.h: явно указываем путь к папке с заголовками архиватора
include_directories(Libraries/assimp/contrib/unzip)

# Подключаем Assimp
add_subdirectory(Libraries/assimp)


# --- 4. Заголовки (Include) и сборка компрессора ---

# Включаем поддержку языка ISPC
enable_language(ISPC)

# Указываем ISPC собирать оптимизированные инструкции для максимальной скорости сжатия.
set(CMAKE_ISPC_INSTRUCTION_SETS "avx2")

# Указываем путь к папке (измени, если она лежит не в Libraries)
set(ISPC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/ispc_texcomp")

# Собираем исходники компрессора
set(ISPC_TEXCOMP_SOURCES
    "${ISPC_DIR}/ispc_texcomp.cpp"
    "${ISPC_DIR}/ispc_texcomp_astc.cpp"
    "${ISPC_DIR}/kernel.ispc"
    "${ISPC_DIR}/kernel_astc.ispc"
)

# Создаем статичную библиотеку из этих файлов
add_library(ispc_texcomp STATIC ${ISPC_TEXCOMP_SOURCES})

# Устанавливаем свойства корректно для компилятора ISPC
set_target_properties(ispc_texcomp PROPERTIES 
    POSITION_INDEPENDENT_CODE ON
    # Используем только те флаги, которые понимает ISPC
    # --nowarn отключает все предупреждения
    ISPC_FLAGS "--arch=x86-64;--target=avx2;--nowarn"
)

# Если ты хочешь заглушить предупреждения в .cpp файлах (C++), 
# которые линкуются в этой библиотеке, используй это отдельно:
set_source_files_properties(${ISPC_DIR}/ispc_texcomp.cpp 
                            ${ISPC_DIR}/ispc_texcomp_astc.cpp 
                            PROPERTIES COMPILE_FLAGS "-w")

target_include_directories(ispc_texcomp PUBLIC "${ISPC_DIR}")

include_directories(
    Libraries/include
    Libraries/include/imgui
    Libraries/include/imguizmo
    Libraries/include/imgui/backends
    Libraries/include/stb
    Libraries/include/nlohmann
    # Путь к заголовкам Assimp (публичный интерфейс)
    Libraries/assimp/include

    ${directxmath_SOURCE_DIR}/Inc
    ${SAL_DUMMY_DIR}
)


# --- 5. Сбор исходных файлов ---

# Собираем файлы движка точечно, чтобы не захватить лишние Main.cpp из библиотек
file(GLOB ENGINE_BASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")
file(GLOB_RECURSE APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/App/*.cpp")

# Рекурсивно собираем только твои папки с кодом
file(GLOB_RECURSE RENDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Render/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp")

# Собираем ImGui
file(GLOB_RECURSE IMGUI_SOURCES 
    "Libraries/src/imgui/*.cpp"
    "Libraries/src/imguizmo/*.cpp"
    "Libraries/src/imgui/backends/imgui_impl_vulkan.cpp"
    "Libraries/src/imgui/backends/imgui_impl_sdl3.cpp"
)

# Объединяем всё в один список (убран GLM)
set(ALL_SOURCES 
    ${ENGINE_BASE_SOURCES}
    ${APP_SOURCES}
    ${RENDER_SOURCES} 
    ${UTILS_SOURCES} 
    ${IMGUI_SOURCES}
)


# --- 6. Создание исполняемого файла ---

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_link_options("-fuse-ld=lld")
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall 
        -Wextra 
        -Wno-unused-parameter
        # Раскомментируй строку ниже, если хочешь, чтобы компилятор предупреждал о паддингах в структурах:
         -Wpadded
    )
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_26)

option(BURNHOPE_VULKAN_VALIDATION "Enable Vulkan validation layers (stderr)" ON)
if(BURNHOPE_VULKAN_VALIDATION)
  target_compile_definitions(${PROJECT_NAME} PRIVATE BURNHOPE_VULKAN_VALIDATION=1)
endif()

# --- 7. Slang -> SPIR-V (VK_EXT_descriptor_heap / shader objects) ---

find_program(SLANGC_EXECUTABLE slangc REQUIRED)

file(GLOB SLANG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.task.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.mesh.slang"
)
file(GLOB SLANG_COMMON_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/*.slang")

foreach(SLANG ${SLANG_SOURCES})
    get_filename_component(SLANG_NAME ${SLANG} NAME)
    string(REPLACE ".slang" ".spv" SPIRV_NAME "${SLANG_NAME}")
    set(SPIRV_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SPIRV_NAME}")

    set(SLANG_STAGE "compute")
    if(SLANG_NAME MATCHES "\\.vert\\.slang$")
        set(SLANG_STAGE "vertex")
    elseif(SLANG_NAME MATCHES "\\.frag\\.slang$")
        set(SLANG_STAGE "fragment")
    elseif(SLANG_NAME MATCHES "\\.task\\.slang$")
        set(SLANG_STAGE "task")
    elseif(SLANG_NAME MATCHES "\\.mesh\\.slang$")
        set(SLANG_STAGE "mesh")
    endif()

    set(SLANG_PROFILE "spirv_1_6")
    set(SLANG_EXTRA_FLAGS -Wno-38052)
    if(SLANG_NAME MATCHES "^(probe_render|probe_update|rt_reflections)\\.comp\\.slang$")
        set(SLANG_PROFILE "spirv_1_6+spvRayQueryKHR")
    endif()
    if(SLANG_NAME MATCHES "^lighting\\.comp\\.slang$")
        # Slang codegen ICE on full optimization for this TU.
        set(SLANG_EXTRA_FLAGS "-O0")
        set(SLANG_PROFILE "spirv_1_6+spvImageQuery+spvImageGatherExtended+spvDerivativeControl+SPV_GOOGLE_user_type+spvSparseResidency+spvMinLod+spvFragmentFullyCoveredEXT")
    endif()
    if(SLANG_NAME MATCHES "^gbuffer\\.frag\\.slang$")
        set(SLANG_PROFILE "spirv_1_6+spvImageQuery+spvImageGatherExtended+spvDerivativeControl+SPV_GOOGLE_user_type+spvSparseResidency+spvMinLod+spvFragmentFullyCoveredEXT")
    endif()
    if(SLANG_NAME MATCHES "^gbuffer\\.mesh\\.slang$")
        # -O0: avoid NV driver SIGSEGV in vkCreateShadersEXT on some SPIR-V shapes.
        # Mesh geometry uses BDA (PhysicalStorageBuffer64); spirv_1_6 enables capability.
        set(SLANG_EXTRA_FLAGS -O0 -Wno-38052)
    endif()

    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${SLANGC_EXECUTABLE}
            -lang slang
            -target spirv
            -profile ${SLANG_PROFILE}
            ${SLANG_EXTRA_FLAGS}
            -entry main
            -stage ${SLANG_STAGE}
            -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            ${SLANG}
            -o ${SPIRV_OUTPUT}
        DEPENDS ${SLANG} ${SLANG_COMMON_DEPS}
        COMMENT "Slang ${SLANG_NAME} -> SPIR-V"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(ShadersTarget ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(${PROJECT_NAME} ShadersTarget)


# --- 8. Линковка библиотек ---

target_link_libraries(${PROJECT_NAME} 
    SDL3::SDL3 
    Vulkan::Vulkan 
    assimp           # Ссылка на цель из add_subdirectory
    meshoptimizer::meshoptimizer
    Threads::Threads
    ispc_texcomp
    VulkanMemoryAllocator
    DirectXMath # Добавлена цель DirectXMath
    flecs::flecs_static
)


# --- 9. Копирование ресурсов (модели, текстуры, шейдеры) ---

if(UNIX)
    # На Linux создаем символические ссылки (экономим место)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_CURRENT_BINARY_DIR}/models
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_CURRENT_BINARY_DIR}/textures
        COMMENT "Creating symlinks for assets"
    )
else()
    # На Windows копируем папки целиком в build/Release или build/Debug
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/Debug/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_CURRENT_BINARY_DIR}/models
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_CURRENT_BINARY_DIR}/textures
        COMMENT "Copying assets to build directory"
    )
endif()