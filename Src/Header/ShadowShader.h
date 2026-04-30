#ifndef SHADOWSHADER_CLASS_H
#define SHADOWSHADER_CLASS_H
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glad/glad.h>
#include "shaderClass.h"
class ShadowShader: public Shader {
public:
    struct {
        GLint lightProjection;
    } loc;
    ShadowShader() : Shader("Shaders/shadowMap.vert", "Shaders/shadowMap.frag") {
        Init();
    }
    unsigned int atlasFBO;
    unsigned int shadowAtlas;
    int atlasResolution = 4096;
    int tileSize = 512;
    unsigned int sunShadowArray;
    unsigned int staticSunShadowArray; // Кэш для статики
    bool staticShadowsDirty = true;    // Флаг: нужно ли перерисовать статику?
    bool sunIsStatic = false;
    unsigned int sunFBO;
    int sunShadowSize = 2048;
    std::vector<float> cascadeSplits = { 25.0f, 80.0f, 200.0f, 400.0f };

    void Init() override {
        loc.lightProjection = GetLoc("lightProjection");
        glGenFramebuffers(1, &atlasFBO);
        glGenTextures(1, &shadowAtlas);
        glBindTexture(GL_TEXTURE_2D, shadowAtlas);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, atlasResolution, atlasResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float clampColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, clampColor);
        glBindFramebuffer(GL_FRAMEBUFFER, atlasFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowAtlas, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glGenTextures(1, &sunShadowArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, sunShadowArray);
                glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT32F, sunShadowSize, sunShadowSize, 4);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);



        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGenTextures(1, &staticSunShadowArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, staticSunShadowArray);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT32F, sunShadowSize, sunShadowSize, 4);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);


        glGenFramebuffers(1, &sunFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, sunFBO);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, sunShadowArray, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};
#endif