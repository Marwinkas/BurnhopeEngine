cmake_minimum_required(VERSION 3.10)
project(BurnHopeEngine)

# Настройки C++ (используем стандарт C++20, как в оригинале)[cite: 2]
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск системных библиотек
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Пути к заголовочным файлам[cite: 2, 5]
include_directories(Src/Header)
include_directories(Libraries/include)

# Собираем все исходники
set(SOURCES
    Src/Source/Camera.cpp
    Src/Source/EBO.cpp
    Src/Source/glad.c
    Src/Source/Main.cpp
    Src/Source/Material.cpp
    Src/Source/Mesh.cpp
    Src/Source/Model.cpp
    Src/Source/ModelImporter.cpp
    Src/Source/PhysicsEngine.cpp
    Src/Source/PostProcessingShader.cpp
    Src/Source/Render.cpp
    Src/Source/shaderClass.cpp
    Src/Source/stb.cpp
    Src/Source/Texture.cpp
    Src/Source/TextureImporter.cpp
    Src/Source/TextureStreamer.cpp
    Src/Source/VAO.cpp
    Src/Source/VBO.cpp
)

# Создаем исполняемый файл
add_executable(BurnHopeEngine ${SOURCES})

# Привязываем библиотеки (вместо .lib файлов из Windows)[cite: 2]
target_link_libraries(BurnHopeEngine OpenGL::GL glfw glm::glm)
