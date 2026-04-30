#ifndef DEFFEREDSHADER_CLASS_H
#define DEFFEREDSHADER_CLASS_H

#include <glm/glm.hpp>
#include <glad/glad.h>
#define NOMINMAX
#include <windows.h>
#include <string>
#include <filesystem>
#include "shaderClass.h"
#include "stb_image.h" // Убедись, что путь правильный
#include <iostream>
class DefferedShader : public Shader {
public:
    struct {
        GLint camPos, shadowAtlas, farPlane, atlasResolution, tileSize, noiseTexture, viewMatrix, nearPlane, screenSize
            , sunLightSpaceMatrices, cascadeSplits, view, camMatrix, materialID, sunShadowMap, gAlbedoMetallic, gNormalRoughness, gHeightAO;
    } loc;

    GLuint blueNoiseTexture;
    std::string getExecutablePathss()
    {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::filesystem::path exePath(buffer);
        std::string path = exePath.parent_path().string();

        // Лёша, это поможет нам понять, не "врет" ли путь
        std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;

        return path;
    }
    DefferedShader() : Shader("Shaders/lighting.comp") {
        Init();
        // Грузим синий шум. Файл должен лежать в папке с ресурсами.
        std::string projectFolder = getExecutablePathss() + "/resources/blue_noise.png";
        blueNoiseTexture = loadBlueNoiseTexture(projectFolder.c_str());
    }

    // Тот самый метод для загрузки правильного шума
    GLuint loadBlueNoiseTexture(const char* path) {
        GLuint textureID;
        glGenTextures(1, &textureID);

        int width, height, nrChannels;
        // Синий шум обычно одноканальный или RGB, нам хватит 8-бит
        unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
        if (data) {
            glBindTexture(GL_TEXTURE_2D, textureID);
            // Используем GL_RED или GL_RGB в зависимости от файла, 
            // но GL_RGB универсальнее для большинства PNG
            GLenum format = (nrChannels == 1) ? GL_RED : GL_RGB;

            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

            // Обязательно REPEAT, чтобы шум бесконечно шел по экрану
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            // NEAREST, чтобы не размывать драгоценные точки шума
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            stbi_image_free(data);
        }
        else {
            std::cerr << "Failed to load Blue Noise texture at: " << path << std::endl;
            stbi_image_free(data);
        }
        return textureID;
    }

    void Init() override {
        loc.camPos = GetLoc("camPos");
        loc.shadowAtlas = GetLoc("shadowAtlas");
        loc.farPlane = GetLoc("farPlane");
        loc.atlasResolution = GetLoc("atlasResolution");
        loc.tileSize = GetLoc("tileSize");
        loc.noiseTexture = GetLoc("noiseTexture");
        loc.viewMatrix = GetLoc("viewMatrix");
        loc.nearPlane = GetLoc("nearPlane");
        loc.screenSize = GetLoc("screenSize");
        loc.sunLightSpaceMatrices = GetLoc("sunLightSpaceMatrices");
        loc.cascadeSplits = GetLoc("cascadeSplits");
        loc.view = GetLoc("view");
        loc.camMatrix = GetLoc("camMatrix");
        loc.materialID = GetLoc("materialID");
        loc.sunShadowMap = GetLoc("sunShadowMap");
        loc.gAlbedoMetallic = GetLoc("gAlbedoMetallic");
        loc.gNormalRoughness = GetLoc("gNormalRoughness");
        loc.gHeightAO = GetLoc("gHeightAO");
    }
};
#endif