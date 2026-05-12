cmake_minimum_required(VERSION 3.10)
project(BurnhopeEngine)

# --- 1. Глобальные настройки проекта ---

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Математика для ImGui
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)



# --- 2. Поиск системных пакетов ---

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
# либо подключается через add_subdirectory, если он в папке Libraries
find_package(glfw3 CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
# --- Подключение Vulkan Memory Allocator ---
include(FetchContent)
# --- Подключение Vulkan Memory Allocator ---
include(FetchContent)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        master  # Или конкретно v3.1.0, чтобы быть уверенным в фиксе
)
FetchContent_MakeAvailable(vma)
# --- 3. Настройка Assimp (максимальная очистка) ---

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

# --- 4. Заголовки (Include) ---
# Подключение ispc_texcomp (Предполагается, что она лежит в системе или папке Libraries)
# Включаем поддержку языка ISPC
enable_language(ISPC)

# Твой Ryzen 9950x поддерживает AVX2 и AVX512. 
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
set_target_properties(ispc_texcomp PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Делаем так, чтобы движок видел заголовок ispc_texcomp.h
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
)

# --- 5. Сбор исходных файлов ---

# Собираем файлы движка точечно, чтобы не захватить лишние Main.cpp из библиотек
file(GLOB ENGINE_BASE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MainApp.cpp"
)

# Рекурсивно собираем только твои папки с кодом
file(GLOB_RECURSE RENDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Render/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp")

# Собираем ImGui
file(GLOB_RECURSE IMGUI_SOURCES 
    "Libraries/src/imgui/*.cpp"
    "Libraries/src/imguizmo/*.cpp"
    "Libraries/src/imgui/backends/imgui_impl_vulkan.cpp"
    "Libraries/src/imgui/backends/imgui_impl_glfw.cpp"
)

# Объединяем всё в один список
set(ALL_SOURCES 
    ${ENGINE_BASE_SOURCES} 
    ${RENDER_SOURCES} 
    ${UTILS_SOURCES} 
    ${IMGUI_SOURCES}
    "Libraries/include/glm/detail/glm.cpp"
)

# --- 6. Создание исполняемого файла ---

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# --- 7. Компиляция шейдеров в SPIR-V ---

find_program(GLSLC_EXECUTABLE glslc)

file(GLOB SHADER_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.task"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.mesh"
)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE}  --target-env=vulkan1.2 ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} to SPIR-V"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(ShadersTarget ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(${PROJECT_NAME} ShadersTarget)


# --- 8. Линковка библиотек ---

target_link_libraries(${PROJECT_NAME} 
    glfw 
    Vulkan::Vulkan 
    assimp           # Ссылка на цель из add_subdirectory
    meshoptimizer::meshoptimizer
    Threads::Threads
    ispc_texcomp
    VulkanMemoryAllocator
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