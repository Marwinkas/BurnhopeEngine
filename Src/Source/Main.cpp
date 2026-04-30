#include<filesystem>
namespace fs = std::filesystem;
#include <windows.h>
#include <string>
#include"../Header/Model.h"
#include "../Header/LitShader.h"
#include "../Header/ShadowShader.h"
#include "../Header/PostProcessingShader.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include "../Header/Window.h"
#include <map>
#include <nlohmann/json.hpp>
#include "../Header/UI.h"
#include "../Header/Render.h"
#include "../Header/Serializer.h"
#include "../Header/CullingShader.h"
#include "../Header/PhysicsEngine.h" 
#include "../Header/DefferedShader.h"
#include "../Header/TextureStreamer.h"
using namespace entt;
std::vector <Material> material;
std::vector <Mesh> mesh;
std::string getExecutablePaths()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    std::string path = exePath.parent_path().string();

    // Лёша, это поможет нам понять, не "врет" ли путь
    std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;

    return path;
}
double crntTime = 0.0;
glm::vec3 GetMouseRay(Window& window, Camera& camera) {
	double mouseX, mouseY;
	glfwGetCursorPos(window.window, &mouseX, &mouseY);
	glm::vec4 viewport = glm::vec4(0, 0, window.settings.width, window.settings.height);
	glm::mat4 view = glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up);
	glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f);
		glm::vec3 nearPos = glm::unProject(glm::vec3(mouseX, window.settings.height - mouseY, 0.0f), view, proj, viewport);
		glm::vec3 farPos = glm::unProject(glm::vec3(mouseX, window.settings.height - mouseY, 1.0f), view, proj, viewport);
	return glm::normalize(farPos - nearPos);
}
TextureStreamer* globalTextureStreamer = nullptr;
int main()
{
    setlocale(LC_ALL, "ru_RU.UTF-8"); // Говорим С-библиотекам про UTF-8
    SetConsoleOutputCP(65001);       // Говорим Windows про UTF-8
    try {
        // Устанавливаем кодировку вывода в UTF-8 (код 65001)

        WindowSettings winProps;
        winProps.title = "BurnHope Engine - Level Editor";
        winProps.width = 1280;
        winProps.height = 720;
        winProps.vSync = true;   // Включаем лок кадров под монитор
        winProps.resizable = true;

        // Передаем их в окно
        Window window(winProps);

        // Проверяем пути
        std::string exePath = getExecutablePaths();
        std::string projectFolder = exePath + "/project";
        std::string projectFolder2 = exePath + "\\project";
        std::string sceneFile = projectFolder + "/level1.bhscene";
        window.SetIcon(exePath + "\\Resources\\icon.png");
        UI ui(window, projectFolder, exePath);
        CullingShader cullingshader;

        PostProcessingShader postprocessingshader(window);
        ShadowShader shadowshader;
        DefferedShader deferredshader;
        Camera camera(window.settings.width, window.settings.height, glm::vec3(0.0f, 0.0f, 2.0f));
        Render render;
        render.InitGBuffer(window.settings.width, window.settings.height);
        render.UpdateClusterGrid(camera, window, cullingshader);
        TextureStreamer texStreamer;
        globalTextureStreamer = &texStreamer;
        std::cout << "Initializing Shaders..." << std::endl;
        LitShader litshader; // Вот тут скорее всего падает из-за blue_noise.png

        PhysicsEngine physicsEngine;
        physicsEngine.Init();
        
        float lastFrame = glfwGetTime();

        entt::registry registry;

        Serializer::LoadScene(projectFolder2 + "\\MyLevel.bhscene", projectFolder2, registry);
        physicsEngine.RegisterEntities(registry);
        render.isSceneDirty = true; // Говорим рендеру, что сцена обновилась
        window.Show();

        while (true)
        {
            if (glfwWindowShouldClose(window.window)) {
                // Если сцена не сохранена и мы еще не дали ответ
                if (ui.isSceneUnsaved && !ui.readyToExit) {
                    // Отменяем закрытие окна!
                    glfwSetWindowShouldClose(window.window, GLFW_FALSE);
                    // Командуем UI показать всплывающее окно
                    ui.showExitPrompt = true;
                }
                else {
                    // Если всё сохранено или мы ответили "Нет" — прерываем цикл (выходим из игры)
                    break;
                }
            }
            if (texStreamer.Update()) {
                render.isSceneDirty = true;
            }
            float currentFrame = glfwGetTime();
            float deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            camera.Inputs(window.window, deltaTime);
            camera.taaFrameIndex++;
            camera.updateMatrix(45.0f, 0.1f, 1000);
            static bool f5Pressed = false;
            if (glfwGetKey(window.window, GLFW_KEY_F5) == GLFW_PRESS) {
                if (!f5Pressed) {
                    f5Pressed = true;
                }
            }
            render.Draw(registry, litshader, shadowshader, postprocessingshader, window, camera, deltaTime, ui, cullingshader, deferredshader);
            
            if (!f5Pressed) {
                ui.Draw(window, camera, registry, render, postprocessingshader);

            }
            physicsEngine.RebuildPhysicsEntities(registry);

            // 2. Движение Гизмо (ручной перенос)
            physicsEngine.UpdatePhysicsFromTransforms(registry);
            if (ui.isPlaying) {


                // 3. Шаг физики Jolt
                physicsEngine.Update(deltaTime);

                // 4. Синхронизация обратно в графику
                if (physicsEngine.SyncTransforms(registry)) {
                    render.isSceneDirty = true;
                }
            }


            glfwSwapBuffers(window.window);
            glfwPollEvents();
        }

        glDeleteFramebuffers(1, &postprocessingshader.FBO);
        glDeleteFramebuffers(1, &postprocessingshader.compositeFBO);
        glfwDestroyWindow(window.window);
        glfwTerminate();
        physicsEngine.Cleanup();
    }
    catch (int e) {
        std::cerr << "\n--- КРИТИЧЕСКАЯ ОШИБКА ---" << std::endl;
        std::cerr << "Код ошибки (errno): " << e << std::endl;
        std::cerr << "Скорее всего, не найден файл! Проверь папки project и resources." << std::endl;
        system("pause"); // Чтобы консоль не закрылась
    }
    catch (const std::exception& e) {
        std::cerr << "\n--- СТАНДАРТНОЕ ИСКЛЮЧЕНИЕ ---" << std::endl;
        std::cerr << e.what() << std::endl;
        system("pause");
    }

    return 0;
}