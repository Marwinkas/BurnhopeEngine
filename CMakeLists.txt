cmake_minimum_required(VERSION 3.10)
project(BurnhopeEngine)



set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)


if(MSVC)
    add_compile_options(/utf-8)
endif()



find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)


find_package(glfw3 QUIET) 




set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)


set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)


set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)


set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_FORCE_ZLIB ON CACHE BOOL "" FORCE)


include_directories(Libraries/assimp/contrib/unzip)


add_subdirectory(Libraries/assimp)



include_directories(
    Libraries/include
    Libraries/include/imgui
    Libraries/include/imguizmo
    Libraries/include/imgui/backends
    Libraries/include/stb
    Libraries/include/nlohmann
    
    Libraries/assimp/include
)




file(GLOB ENGINE_BASE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MainApp.cpp"
)


file(GLOB_RECURSE RENDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Render/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp")


file(GLOB_RECURSE IMGUI_SOURCES 
    "Libraries/src/imgui/*.cpp"
    "Libraries/src/imguizmo/*.cpp"
    "Libraries/src/imgui/backends/imgui_impl_vulkan.cpp"
    "Libraries/src/imgui/backends/imgui_impl_glfw.cpp"
)


set(ALL_SOURCES 
    ${ENGINE_BASE_SOURCES} 
    ${RENDER_SOURCES} 
    ${UTILS_SOURCES} 
    ${IMGUI_SOURCES}
    "Libraries/include/glm/detail/glm.cpp"
)



add_executable(${PROJECT_NAME} ${ALL_SOURCES})



find_program(GLSLC_EXECUTABLE glslc)

file(GLOB SHADER_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
)

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

add_custom_target(ShadersTarget ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(${PROJECT_NAME} ShadersTarget)



target_link_libraries(${PROJECT_NAME} 
    glfw 
    Vulkan::Vulkan 
    assimp           
    meshoptimizer
    Threads::Threads
    ${CMAKE_DL_LIBS} 
    m                
)



if(UNIX)
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_CURRENT_BINARY_DIR}/models
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_CURRENT_BINARY_DIR}/textures
        COMMENT "Creating symlinks for assets"
    )
else()
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_CURRENT_BINARY_DIR}/models
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_CURRENT_BINARY_DIR}/textures
        COMMENT "Copying assets to build directory"
    )
endif()