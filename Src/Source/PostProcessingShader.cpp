#include "../Header/PostProcessingShader.h"
#include "../Header/UI.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <random>
#include <string>

const float rectangleVerticesSSAO[] =
{
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f
};

PostProcessingShader::PostProcessingShader(Window& window) {
    // 1. ЗАГРУЗКА ШЕЙДЕРОВ
    ssaoShader = Shader("Shaders/ssao.comp");
    bloomThresholdShader = Shader("Shaders/bloom_threshold.comp");
    bloomBlurShader = Shader("Shaders/bloom_blur.comp");
    ssaoBlurShader = Shader("Shaders/ssao_blur.comp");
    hiZShader = Shader("Shaders/hiz.comp");
    volInjectShader = Shader("Shaders/volumetric_inject.comp");
    volAccumulateShader = Shader("Shaders/volumetric_accumulate.comp");
    SCSSShader = Shader("Shaders/sscs.comp");
    SceneColorShader = Shader("Shaders/scene_color.comp");
    PostProcessShader = Shader("Shaders/postprocess.comp");
    sharpenShader = Shader("Shaders/framebuffer.vert", "Shaders/sharpen.frag");
    rtgiShader = Shader("Shaders/rtgi.comp");
    // =================================================================
    // 2. ГЛОБАЛЬНЫЕ/ПОСТОЯННЫЕ ТЕКСТУРЫ (Туман)
    // =================================================================
    glGenTextures(1, &volumeTexture);
    glBindTexture(GL_TEXTURE_3D, volumeTexture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    std::vector<float> clearFog(80 * 45 * 64 * 4, 0.0f);
    for (int i = 3; i < clearFog.size(); i += 4)
        clearFog[i] = 1.0f; // alpha = 1.0 = нет тумана
    glBindTexture(GL_TEXTURE_3D, volumeTexture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 80, 45, 64, 0, GL_RGBA, GL_FLOAT, clearFog.data());

    // Замени инициализацию accumVolumeTexture:
    glGenTextures(1, &accumVolumeTexture);
    glBindTexture(GL_TEXTURE_3D, accumVolumeTexture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    // PostProcessingShader.cpp — после glTexImage3D для accumVolumeTexture
    // Заполни нулями RGB и 1.0 в alpha (transmittance=1 = нет тумана)
    std::vector<float> clearAccum(80 * 45 * 64 * 4, 0.0f);
    for (int i = 3; i < (int)clearAccum.size(); i += 4)
        clearAccum[i] = 1.0f; // transmittance = 1.0 = прозрачно

    glBindTexture(GL_TEXTURE_3D, accumVolumeTexture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 80, 45, 64, 0,
        GL_RGBA, GL_FLOAT, clearAccum.data());

    // =================================================================
    // 3. SSAO ДАННЫЕ (Ядро и Шум)
    // =================================================================
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    for (unsigned int i = 0; i < 64; ++i) {
        glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        ssaoKernel.push_back(sample * scale);
    }

    std::vector<glm::vec3> ssaoNoise;
    for (unsigned int i = 0; i < 16; i++) {
        ssaoNoise.push_back(glm::vec3(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f));
    }

    glGenTextures(1, &ssaoNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // =================================================================
    // 4. ЭКРАННЫЙ КВАДРАТ (VAO)
    // =================================================================
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVerticesSSAO), &rectangleVerticesSSAO, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // =================================================================
    // 5. ГЛАВНЫЙ FBO (Сюда рисуется Skybox и копируется свет)
    // =================================================================
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);

    glGenTextures(1, &postProcessingTexture);
    glBindTexture(GL_TEXTURE_2D, postProcessingTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, window.settings.width, window.settings.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postProcessingTexture, 0);

    glGenTextures(1, &postDepthTexture);
    glBindTexture(GL_TEXTURE_2D, postDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, window.settings.width, window.settings.height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, postDepthTexture, 0);

    // =================================================================
    // 6. HI-Z ТЕКСТУРА
    // =================================================================
    hiZMipMapCount = (int)std::floor(std::log2(std::max(window.settings.width, window.settings.height))) + 1;
    glGenTextures(1, &hiZTexture);
    glBindTexture(GL_TEXTURE_2D, hiZTexture);
    glTexStorage2D(GL_TEXTURE_2D, hiZMipMapCount, GL_R32F, window.settings.width, window.settings.height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // =================================================================
    // 7. СГЛАЖИВАНИЕ TAA (Хранит кадры, поэтому тоже постоянна)
    // =================================================================
    ssrShader = Shader("Shaders/ssr.comp");

    glGenTextures(1, &ssrTexture);
    glBindTexture(GL_TEXTURE_2D, ssrTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, window.settings.width, window.settings.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Генерируем FBO и Текстуру
    glGenFramebuffers(1, &finalSceneFBO);
    glGenTextures(1, &finalSceneTexture);

    // Настраиваем текстуру размером с экран игры (fullW, fullH)
    glBindTexture(GL_TEXTURE_2D, finalSceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, window.settings.width, window.settings.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Привязываем текстуру к FBO
    glBindFramebuffer(GL_FRAMEBUFFER, finalSceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, finalSceneTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "[ERROR] Финальный FBO не создался!" << std::endl;

    // Отвязываем, чтобы ничего не сломать
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcessingShader::Bind(Window& window) {
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glViewport(0, 0, window.settings.width, window.settings.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
}

static glm::mat4 lastViewProj = glm::mat4(1.0f);

void PostProcessingShader::Update(Window& window, float crntTime, Camera& camera, UI& ui, glm::vec3 sunDir, unsigned int uboLights, ShadowShader& shadowshader, GLuint hdrOutputTexture, GLuint gDepth, GLuint gNormalRoughness, GLuint gAlbedo) {

    // Гарантируем, что Compute Shader закончил работу со светом
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    int fullW = window.settings.width;
    int fullH = window.settings.height;
    int halfW = fullW / 2;
    int halfH = fullH / 2;
    int quarterW = fullW / 4;
    int quarterH = fullH / 4;

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
    glm::mat4 invViewProj = glm::inverse(projection * view);

    // ==========================================
    // 1. ГЕНЕРАЦИЯ HI-Z
    // ==========================================
    hiZShader.Activate();
    for (int i = 0; i < hiZMipMapCount; ++i) {
        int mipWidth = std::max(1, fullW >> i);
        int mipHeight = std::max(1, fullH >> i);
        glActiveTexture(GL_TEXTURE0);
        if (i == 0) {
            glBindTexture(GL_TEXTURE_2D, gDepth);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), 0);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 1);
        }
        else {
            glBindTexture(GL_TEXTURE_2D, hiZTexture);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "inMipLevel"), i - 1);
            glUniform1i(glGetUniformLocation(hiZShader.ID, "copyOnly"), 0);
        }
        glUniform1i(glGetUniformLocation(hiZShader.ID, "inDepth"), 0);
        glBindImageTexture(0, hiZTexture, i, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
        glDispatchCompute((mipWidth + 15) / 16, (mipHeight + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    // ==========================================
    // 2. RENDER GRAPH: SSAO (COMPUTE)
    // ==========================================
    GLuint ssaoFinalTexture = 0;
    TransientRT ssaoRawRT, ssaoBlurRT;

    if (ui.ppSettings.enableSSAO) {
        ssaoRawRT = rtPool.Acquire(halfW, halfH, GL_R16F);

        ssaoShader.Activate();
        glUniform1i(glGetUniformLocation(ssaoShader.ID, "enableSSAO"), ui.ppSettings.enableSSAO);
        glUniform1f(glGetUniformLocation(ssaoShader.ID, "radius"), ui.ppSettings.ssaoRadius);
        glUniform1f(glGetUniformLocation(ssaoShader.ID, "bias"), ui.ppSettings.ssaoBias);
        glUniform1f(glGetUniformLocation(ssaoShader.ID, "intensity"), ui.ppSettings.ssaoIntensity);
        glUniform1f(glGetUniformLocation(ssaoShader.ID, "power"), ui.ppSettings.ssaoPower);

        for (unsigned int i = 0; i < 64; ++i)
            glUniform3fv(glGetUniformLocation(ssaoShader.ID, ("samples[" + std::to_string(i) + "]").c_str()), 1, glm::value_ptr(ssaoKernel[i]));

        glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(ssaoShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
        glUniform2f(glGetUniformLocation(ssaoShader.ID, "noiseScale"), (float)halfW / 4.0f, (float)halfH / 4.0f);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
        glUniform1i(glGetUniformLocation(ssaoShader.ID, "normalTexture"), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gDepth);
        glUniform1i(glGetUniformLocation(ssaoShader.ID, "depthTexture"), 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
        glUniform1i(glGetUniformLocation(ssaoShader.ID, "texNoise"), 2);

        glBindImageTexture(0, ssaoRawRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
        glDispatchCompute((halfW + 15) / 16, (halfH + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // --- SSAO BLUR ---
        ssaoBlurRT = rtPool.Acquire(halfW, halfH, GL_R16F);
        ssaoBlurShader.Activate();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ssaoRawRT.Texture);
        glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "ssaoInput"), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gDepth);
        glUniform1i(glGetUniformLocation(ssaoBlurShader.ID, "depthTexture"), 1);
        glUniformMatrix4fv(glGetUniformLocation(ssaoBlurShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));

        glBindImageTexture(0, ssaoBlurRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
        glDispatchCompute((halfW + 15) / 16, (halfH + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        rtPool.Release(ssaoRawRT);
        ssaoFinalTexture = ssaoBlurRT.Texture;
    }

    // ==========================================
    // 3. RENDER GRAPH: SSCS (Contact Shadows)
    // ==========================================
    TransientRT sscsRT = rtPool.Acquire(fullW, fullH, GL_R16F);
    SCSSShader.Activate();
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "enableContactShadows"), ui.ppSettings.enableContactShadows ? 1 : 0);
    glUniform1f(glGetUniformLocation(SCSSShader.ID, "contactShadowLength"), ui.ppSettings.contactShadowLength);
    glUniform1f(glGetUniformLocation(SCSSShader.ID, "contactShadowThickness"), ui.ppSettings.contactShadowThickness);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "contactShadowSteps"), ui.ppSettings.contactShadowSteps);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "hiZMipCount"), hiZMipMapCount);
    glUniform3f(glGetUniformLocation(SCSSShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
    glUniform3f(glGetUniformLocation(SCSSShader.ID, "sunDirFog"), sunDir.x, sunDir.y, sunDir.z);
    glUniformMatrix4fv(glGetUniformLocation(SCSSShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniformMatrix4fv(glGetUniformLocation(SCSSShader.ID, "projectionMatrix"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(SCSSShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(view));
    // В PostProcessingShader::Update перед dispatch SSCS:
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gDepth);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "depthTexture"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "normalTexture"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, hiZTexture);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "hiZTexture"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glUniform1i(glGetUniformLocation(SCSSShader.ID, "noiseTexture"), 3);

    glBindImageTexture(0, sscsRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
    glDispatchCompute((fullW + 15) / 16, (fullH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // ==========================================
    // 4. ТУМАН (VOLUMETRIC FOG)
    // ==========================================
    if (ui.ppSettings.enableFog) {
        volInjectShader.Activate();
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboLights);
        glUniformMatrix4fv(glGetUniformLocation(volInjectShader.ID, "invViewProj"),
            1, GL_FALSE, glm::value_ptr(invViewProj));
        glUniformMatrix4fv(glGetUniformLocation(volInjectShader.ID, "invProj"),
            1, GL_FALSE, glm::value_ptr(glm::inverse(projection)));
        glUniformMatrix4fv(glGetUniformLocation(volInjectShader.ID, "invView"),
            1, GL_FALSE, glm::value_ptr(glm::inverse(view)));
        glUniform3fv(glGetUniformLocation(volInjectShader.ID, "camPos"),
            1, glm::value_ptr(camera.Position));
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "zNear"), 0.1f);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "zFar"), 100.0f);

        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogDensity"),
            ui.ppSettings.fogDensity);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogBaseHeight"),
            ui.ppSettings.fogBaseHeight);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "fogHeightFalloff"),
            ui.ppSettings.fogHeightFalloff);
        glUniform3fv(glGetUniformLocation(volInjectShader.ID, "fogColor"),
            1, ui.ppSettings.fogColor);
        glUniform3fv(glGetUniformLocation(volInjectShader.ID, "inscatterColor"),
            1, ui.ppSettings.inscatterColor);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "inscatterPower"),
            ui.ppSettings.inscatterPower);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "inscatterIntensity"),
            ui.ppSettings.inscatterIntensity);
        glUniform3f(glGetUniformLocation(volInjectShader.ID, "sunDir"),
            sunDir.x, sunDir.y, sunDir.z);
        glUniform1f(glGetUniformLocation(volInjectShader.ID, "time"), crntTime);

        glBindImageTexture(0, volumeTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(10, 6, 8);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
            GL_TEXTURE_FETCH_BARRIER_BIT);

        volAccumulateShader.Activate();
        glUniform1f(glGetUniformLocation(volAccumulateShader.ID, "zNear"), 0.1f);
        glUniform1f(glGetUniformLocation(volAccumulateShader.ID, "zFar"), 100.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, volumeTexture);
        glUniform1i(glGetUniformLocation(volAccumulateShader.ID, "volumeTex"), 0);
        glBindImageTexture(0, accumVolumeTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(10, 6, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
            GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    else {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_3D, 0); // отвязываем
    }

    TransientRT rtgiRT = rtPool.Acquire(fullW, fullH, GL_RGBA16F);
    rtgiShader.Activate();

    // Пока хардкодим качество, потом выведешь в UI, если захочешь
    glUniform1i(glGetUniformLocation(rtgiShader.ID, "rtgiRays"), 4);
    glUniform1i(glGetUniformLocation(rtgiShader.ID, "rtgiSteps"), 12);
    glUniform1f(glGetUniformLocation(rtgiShader.ID, "rtgiRadius"), 5.0f);
    glUniform1f(glGetUniformLocation(rtgiShader.ID, "rtgiThickness"), 0.5f);
    glUniform1f(glGetUniformLocation(rtgiShader.ID, "time"), crntTime);

    glUniformMatrix4fv(glGetUniformLocation(rtgiShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniformMatrix4fv(glGetUniformLocation(rtgiShader.ID, "projectionMatrix"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(rtgiShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gDepth);
    glUniform1i(glGetUniformLocation(rtgiShader.ID, "gDepth"), 0);

    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
    glUniform1i(glGetUniformLocation(rtgiShader.ID, "gNormalRoughness"), 1);

    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, hdrOutputTexture); // Наш прямой свет
    glUniform1i(glGetUniformLocation(rtgiShader.ID, "directLightTex"), 2);

    glBindImageTexture(0, rtgiRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((fullW + 15) / 16, (fullH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // ==========================================
    // 5. SCENE COLOR COMPOSITE (Склейка Света, Теней, Тумана)
    // ==========================================


    // После SSCS, перед SceneColor:
    TransientRT ssrRT = rtPool.Acquire(fullW, fullH, GL_RGBA16F);
    ssrShader.Activate();
    glUniformMatrix4fv(glGetUniformLocation(ssrShader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(ssrShader.ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(ssrShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniform3f(glGetUniformLocation(ssrShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
    glUniform2f(glGetUniformLocation(ssrShader.ID, "screenSize"), (float)fullW, (float)fullH);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gDepth);
    glUniform1i(glGetUniformLocation(ssrShader.ID, "gDepth"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
    glUniform1i(glGetUniformLocation(ssrShader.ID, "gNormalRoughness"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, hdrOutputTexture);
    glUniform1i(glGetUniformLocation(ssrShader.ID, "sceneColorTex"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, gAlbedo);
    glUniform1i(glGetUniformLocation(ssrShader.ID, "gAlbedoMetallic"), 3);

    glBindImageTexture(0, ssrRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((fullW + 15) / 16, (fullH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    TransientRT sceneColorRT = rtPool.Acquire(fullW, fullH, GL_RGBA16F);
    SceneColorShader.Activate();
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "hasSSAO"), (ssaoFinalTexture != 0));
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "enableFog"), ui.ppSettings.enableFog);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "enableChromaticAberration"), ui.ppSettings.enableChromaticAberration);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "caIntensity"), ui.ppSettings.caIntensity);
    glUniformMatrix4fv(glGetUniformLocation(SceneColorShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniformMatrix4fv(glGetUniformLocation(SceneColorShader.ID, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(view));

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrOutputTexture);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "screenTexture"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gDepth);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "depthTexture"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gNormalRoughness);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "normalTexture"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssaoFinalTexture ? ssaoFinalTexture : 0);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "ssaoTexture"), 3);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, sscsRT.Texture);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "sscsTexture"), 4);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_3D, accumVolumeTexture);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "volumetricFogTex"), 6);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "fogDensity"),
        ui.ppSettings.fogDensity);
    glUniform3fv(glGetUniformLocation(SceneColorShader.ID, "fogColor"),
        1, ui.ppSettings.fogColor);
    // ПЕРЕДАЕМ НОВЫЕ ТЕКСТУРЫ ДЛЯ RTGI В SCENE COLOR
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, rtgiRT.Texture);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "rtgiTexture"), 7);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "fogBaseHeight"), ui.ppSettings.fogBaseHeight);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "fogHeightFalloff"), ui.ppSettings.fogHeightFalloff);

    // fogDensity и camPos уже передаются — проверь что они есть
    glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, gAlbedo); // Передаем цвет стены из G-Buffer!
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "albedoTexture"), 8);
    glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_2D, ssrRT.Texture);
    glUniform1i(glGetUniformLocation(SceneColorShader.ID, "ssrTexture"), 9);
    glUniform3f(glGetUniformLocation(SceneColorShader.ID, "camPos"),

        camera.Position.x, camera.Position.y, camera.Position.z);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "fogZNear"), 0.1f);
    glUniform1f(glGetUniformLocation(SceneColorShader.ID, "fogZFar"), 100.0f);
    glBindImageTexture(0, sceneColorRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((fullW + 15) / 16, (fullH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    // ==========================================
    // 6. BLOOM (Вычисляется из уже готовой SceneColor)
    // ==========================================
    GLuint bloomFinalTexture = 0;
    TransientRT bloomThresholdRT, pingRT, pongRT;

    if (ui.ppSettings.enableBloom) {
        bloomThresholdRT = rtPool.Acquire(quarterW, quarterH, GL_RGBA16F);
        bloomThresholdShader.Activate();
        glUniform1f(glGetUniformLocation(bloomThresholdShader.ID, "threshold"), ui.ppSettings.bloomThreshold);
        glUniform2f(glGetUniformLocation(bloomThresholdShader.ID, "targetSize"), (float)quarterW, (float)quarterH);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColorRT.Texture); // Блум берет готовую сцену!
        glUniform1i(glGetUniformLocation(bloomThresholdShader.ID, "screenTexture"), 0);
        glBindImageTexture(0, bloomThresholdRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute((quarterW + 15) / 16, (quarterH + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        pingRT = rtPool.Acquire(quarterW, quarterH, GL_RGBA16F);
        pongRT = rtPool.Acquire(quarterW, quarterH, GL_RGBA16F);
        int amount = ui.ppSettings.bloomBlurIterations * 2;
        if (amount == 0) {
            bloomFinalTexture = bloomThresholdRT.Texture;
            rtPool.Release(pingRT); rtPool.Release(pongRT);
        }
        else {
            bloomBlurShader.Activate();
            glUniform2f(glGetUniformLocation(bloomBlurShader.ID, "targetSize"), (float)quarterW, (float)quarterH);
            bool horizontal = true; bool first_iteration = true;
            for (unsigned int i = 0; i < amount; i++) {
                glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "horizontal"), horizontal ? 1 : 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, first_iteration ? bloomThresholdRT.Texture : (horizontal ? pongRT.Texture : pingRT.Texture));
                glUniform1i(glGetUniformLocation(bloomBlurShader.ID, "imageInput"), 0);
                glBindImageTexture(0, horizontal ? pingRT.Texture : pongRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                glDispatchCompute((quarterW + 15) / 16, (quarterH + 15) / 16, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
                horizontal = !horizontal; first_iteration = false;
            }
            rtPool.Release(bloomThresholdRT);
            bloomFinalTexture = horizontal ? pongRT.Texture : pingRT.Texture;
        }
    }

    // ==========================================
    // 7. POST PROCESS (DOF, Tonemap, Lens Flares)
    // ==========================================
    TransientRT finalPPRT = rtPool.Acquire(fullW, fullH, GL_RGBA16F);
    PostProcessShader.Activate();

    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableDoF"), ui.ppSettings.enableDoF);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "focusDistance"), ui.ppSettings.focusDistance);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "focusRange"), ui.ppSettings.focusRange);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "bokehSize"), ui.ppSettings.bokehSize);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableMotionBlur"), ui.ppSettings.enableMotionBlur);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "mbStrength"), ui.ppSettings.mbStrength);
    glUniformMatrix4fv(glGetUniformLocation(PostProcessShader.ID, "prevViewProj"), 1, GL_FALSE, glm::value_ptr(lastViewProj));
    glUniformMatrix4fv(glGetUniformLocation(PostProcessShader.ID, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniform3f(glGetUniformLocation(PostProcessShader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);

    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "hasBloom"), (bloomFinalTexture != 0));
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableBloom"), ui.ppSettings.enableBloom);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "bloomIntensity"), ui.ppSettings.bloomIntensity);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableLensFlares"), ui.ppSettings.enableLensFlares);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "flareIntensity"), ui.ppSettings.flareIntensity);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "ghostDispersal"), ui.ppSettings.ghostDispersal);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "ghosts"), ui.ppSettings.ghosts);

    glm::vec3 sunPos = -sunDir * 1000.0f;
    glm::vec4 clipSpacePos = camera.cleanViewProjectionMatrix * glm::vec4(sunPos, 1.0f);
    glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos) / clipSpacePos.w;
    glm::vec2 sunScreenPos = (glm::vec2(ndcSpacePos.x, ndcSpacePos.y) + 1.0f) / 2.0f;
    float sunVisible = (clipSpacePos.w > 0.0f && clipSpacePos.z < clipSpacePos.w) ? glm::clamp(-sunDir.y * 10.0f + 0.5f, 0.0f, 1.0f) : 0.0f;

    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableGodRays"), ui.ppSettings.enableGodRays);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "godRaysIntensity"), ui.ppSettings.godRaysIntensity * sunVisible);
    glUniform2f(glGetUniformLocation(PostProcessShader.ID, "lightScreenPos"), sunScreenPos.x, sunScreenPos.y);

    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "temperature"), ui.ppSettings.temperature);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "currentExposure"), ui.ppSettings.currentExposure);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "autoExposure"), ui.ppSettings.autoExposure);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "exposureCompensation"), ui.ppSettings.exposureCompensation);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "minBrightness"), ui.ppSettings.minBrightness);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "maxBrightness"), ui.ppSettings.maxBrightness);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "contrast"), ui.ppSettings.contrast);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "saturation"), ui.ppSettings.saturation);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableVignette"), ui.ppSettings.enableVignette);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "vignetteIntensity"), ui.ppSettings.vignetteIntensity);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "gamma"), ui.ppSettings.gamma);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "enableFilmGrain"), ui.ppSettings.enableFilmGrain);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "grainIntensity"), ui.ppSettings.grainIntensity);
    glUniform1f(glGetUniformLocation(PostProcessShader.ID, "time"), crntTime);
    // В блоке SceneColorShader.Activate():

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColorRT.Texture);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "sceneColorTex"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gDepth);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "depthTexture"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, bloomFinalTexture ? bloomFinalTexture : 0);
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "bloomTexture"), 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, hdrOutputTexture); // Для GodRays
    glUniform1i(glGetUniformLocation(PostProcessShader.ID, "screenTexture"), 3);

    glBindImageTexture(0, finalPPRT.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((fullW + 15) / 16, (fullH + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // ==========================================
    // 8. TAA (Сглаживание - всё ещё на Fragment Shader, т.к. рисует в историю)
    // ==========================================
   // ==========================================
    // 8. ВЫВОД НА ЭКРАН (FXAA + Sharpen)
    // ==========================================
    glBindFramebuffer(GL_FRAMEBUFFER, finalSceneFBO);
    glViewport(0, 0, window.settings.width, window.settings.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    sharpenShader.Activate();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, finalPPRT.Texture); // Твоя готовая картинка

    glUniform1i(glGetUniformLocation(sharpenShader.ID, "screenTexture"), 0);

    // ПЕРЕДАЕМ РАЗМЕР ПИКСЕЛЯ ДЛЯ СГЛАЖИВАНИЯ!
    glUniform2f(glGetUniformLocation(sharpenShader.ID, "texelSize"), 1.0f / fullW, 1.0f / fullH);

    float sharpness = ui.ppSettings.enableSharpen ? ui.ppSettings.sharpenIntensity : 0.0f;
    glUniform1f(glGetUniformLocation(sharpenShader.ID, "sharpenAmount"), sharpness);

    glBindVertexArray(rectVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);

    // ОЧИСТКА ПУЛА
    if (ui.ppSettings.enableSSAO) rtPool.Release(ssaoBlurRT);
    if (ui.ppSettings.enableBloom) { rtPool.Release(pingRT); rtPool.Release(pongRT); }
    rtPool.Release(sscsRT);
    rtPool.Release(sceneColorRT);
    rtPool.Release(finalPPRT);
    rtPool.ResetForNextFrame();

    // Оставляем для Motion Blur
    lastViewProj = camera.cleanViewProjectionMatrix;
}