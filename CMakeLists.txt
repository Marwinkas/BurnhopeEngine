cmake_minimum_required(VERSION 3.10)
project(BurnhopeEngine)
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(Threads REQUIRED)

# Указываем пути к заголовкам
include_directories(
    Libraries/include
    Libraries/include/imgui
    Libraries/include/imguizmo
    Libraries/include/imgui/backends
    Libraries/include/stb
    Libraries/include/nlohmann
)
file(GLOB_RECURSE IMGUI_SOURCES 
    "Libraries/src/imgui/*.cpp"
    "Libraries/src/imguizmo/*.cpp"
    "Libraries/src/imgui/backends/imgui_impl_vulkan.cpp"
    "Libraries/src/imgui/backends/imgui_impl_glfw.cpp"
)
# Собираем все твои файлы
file(GLOB_RECURSE ENGINE_SOURCES
    "Main.cpp"
    "MainApp.cpp"
    "Render/*.cpp"
    "Utils/*.cpp"
    "Libraries/include/glm/detail/glm.cpp"
)

set(ALL_SOURCES ${ENGINE_SOURCES} ${IMGUI_SOURCES})

add_executable(BurnhopeEngine ${ALL_SOURCES})
# Ищем компилятор шейдеров
find_program(GLSLC_EXECUTABLE glslc)

# Находим все файлы шейдеров в папке Shaders
file(GLOB SHADER_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
)

# Правило для компиляции каждого шейдера
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} to SPIR-V"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUTPUT})
endforeach()

# Создаем цель в проекте, чтобы шейдеры считались частью сборки
add_custom_target(ShadersTarget ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(BurnhopeEngine ShadersTarget)
target_link_libraries(BurnhopeEngine 
    glfw 
    Vulkan::Vulkan 
    assimp::assimp
    meshoptimizer
    dl 
    m 
    Threads::Threads
)

if(UNIX)
    add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMENT "Creating symlink to shaders directory in build folder"
    )
    # То же самое для моделей, так как движок ищет их по пути ../models
    add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_CURRENT_BINARY_DIR}/models
        COMMENT "Creating symlink to models directory"
    )
        add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/textures
        ${CMAKE_CURRENT_BINARY_DIR}/textures
        COMMENT "Creating symlink to textures directory"
    )
# На Windows создаем копию папки, так как с ссылками там могут быть проблемы с правами
else()
    add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
    )
    add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_CURRENT_BINARY_DIR}/models
    )
      add_custom_command(TARGET BurnhopeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/textures
        ${CMAKE_CURRENT_BINARY_DIR}/textures
    )
endif()