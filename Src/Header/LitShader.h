#ifndef LITSHADER_CLASS_H
#define LITSHADER_CLASS_H

#include <glm/glm.hpp>
#include <glad/glad.h>
#define NOMINMAX
#include <windows.h>
#include <string>
#include <filesystem>
#include "shaderClass.h"
#include "stb_image.h" // Убедись, что путь правильный
#include <iostream>
class LitShader : public Shader {
public:
    struct {
        GLint camPos, shadowAtlas, farPlane, atlasResolution, tileSize, noiseTexture, viewMatrix, nearPlane, screenSize
            , sunLightSpaceMatrices, cascadeSplits, view, camMatrix, materialID, sunShadowMap;
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
    LitShader() : Shader("Shaders/gbuffer.vert", "Shaders/gbuffer.frag") {
        Init();
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
    }
};
#endif