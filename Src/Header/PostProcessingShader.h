#ifndef POSTPROCESSINGSHADER_CLASS_H
#define POSTPROCESSINGSHADER_CLASS_H
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <string>
#include "shaderClass.h"
#include "Window.h"
#include "Camera.h"
#include <tuple>
#include <map>

// Временный холст (Render Target)
struct TransientRT {
    GLuint FBO;
    GLuint Texture;
    int width;
    int height;
    GLenum format;
};

class RenderTargetPool {
private:
    std::vector<TransientRT> freeRTs;
    std::vector<TransientRT> inUseRTs;

public:
    // ЗАПРАШИВАЕМ ТЕКСТУРУ ИЗ ПУЛА

    TransientRT Acquire(int width, int height, GLenum format) {
        // 1. Ищем свободную текстуру такого же размера и формата
        for (auto it = freeRTs.begin(); it != freeRTs.end(); ++it) {
            if (it->width == width && it->height == height && it->format == format) {
                TransientRT rt = *it;
                freeRTs.erase(it);
                inUseRTs.push_back(rt);
                return rt;
            }
        }

        // 2. Если свободной нет — создаем новую (только один раз!)
        TransientRT newRT;
        newRT.width = width; newRT.height = height; newRT.format = format;

        glGenFramebuffers(1, &newRT.FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, newRT.FBO);

        glGenTextures(1, &newRT.Texture);
        glBindTexture(GL_TEXTURE_2D, newRT.Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format == GL_RED ? GL_RED : GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, newRT.Texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        inUseRTs.push_back(newRT);
        return newRT;
    }

    // ВОЗВРАЩАЕМ ТЕКСТУРУ В ПУЛ (СЕКРЕТ АЛИАСИНГА)
    void Release(TransientRT rt) {
        for (auto it = inUseRTs.begin(); it != inUseRTs.end(); ++it) {
            if (it->FBO == rt.FBO) {
                freeRTs.push_back(*it);
                inUseRTs.erase(it);
                return;
            }
        }
    }

    // В конце кадра можно проверить, не забыли ли мы вернуть текстуры
    void ResetForNextFrame() {
        for (auto& rt : inUseRTs) freeRTs.push_back(rt);
        inUseRTs.clear();
    }
};
class UI;
class ShadowShader;
class PostProcessingShader {
public:
    Shader ssgiShader;
    Shader blurShader;
    Shader ssaoShader;
    Shader bloomThresholdShader;
    Shader bloomBlurShader;
    Shader ssgiSeparableBlurShader;
    Shader ssaoBlurShader;
    Shader rcRaycastShader;
    Shader rcCollapseShader;
    unsigned int rcCascadeArray;
    unsigned int rcMergedTexture;
    int numCascades = 4;
    GLuint finalSceneFBO = 0;
    GLuint finalSceneTexture = 0;
    Shader sharpenShader; 
    Shader volInjectShader;
    Shader volAccumulateShader;
    Shader rtgiShader;
    unsigned int volumeTexture;
    unsigned int accumVolumeTexture;
    unsigned int compositeFBO, compositeTexture;           
        unsigned int bloomThresholdFBO, bloomThresholdTexture;
    unsigned int ssgiBlurFBO[2], ssgiBlurTextures[2];
    unsigned int ssaoBlurFBO, ssaoBlurTexture;
    unsigned int pingpongFBO[2], pingpongColorbuffers[2];
        unsigned int samples = 8;
    unsigned int rectVAO, rectVBO;
    unsigned int FBO, RBO;     unsigned int framebufferTextureMS, normalTextureMS, positionTextureMS, gNormalTexMS, gAlbedoTexMS;
        unsigned int resolveFBO;
    unsigned int postProcessingTexture, postNormalTexture, postPositionTexture, gNormalTexResolved, gAlbedoTexResolved, postDepthTexture;
    unsigned int ssgiFBO, ssgiTexture;
    unsigned int ssaoFBO, ssaoTexture;
    unsigned int ssrTexture;
    unsigned int ssaoNoiseTexture;
    RenderTargetPool rtPool;
    Shader hiZShader;
    Shader ssrShader;
    Shader SCSSShader;
    Shader SceneColorShader;
    Shader PostProcessShader;
    unsigned int hiZTexture;
    int hiZMipMapCount;
    std::vector<glm::vec3> ssaoKernel;
    PostProcessingShader(Window& window);
    void Bind(Window& window);
    void Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir, unsigned int uboLights, ShadowShader& shadowshader, GLuint hdrOutputTexture, GLuint gDepth, GLuint gNormalRoughness, GLuint gAlbedo);
};
#endif