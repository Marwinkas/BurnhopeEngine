#include "Camera.hpp"

// std
#include <cassert>
#include <limits>
#include <GLFW/glfw3.h>


namespace burnhope {

    Camera::Camera(int width, int height, glm::vec3 position)
    {
        Camera::width = width;
        Camera::height = height;
        Position = position;
    }

    void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane)
    {
        // Теперь мы вызываем правильную функцию GetViewMatrix()
        glm::mat4 view = GetViewMatrix();

        // Считаем матрицу с TAA тряской
        glm::mat4 projJitter = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        viewProjectionMatrix = projJitter * view;

        // Считаем чистую матрицу
        glm::mat4 projClean = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        cleanViewProjectionMatrix = projClean * view;
    }
    bool Camera::IsSphereInFrustum(const glm::vec4* planes, const glm::vec3& center, float radius) {
        // Просто пробегаемся по готовым плоскостям
        for (int i = 0; i < 6; i++) {
            // Если центр сферы находится ЗА плоскостью дальше, чем её радиус - она невидима
            if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius) {
                return false;
            }
        }
        return true;
    }

    // --- УПРАВЛЕНИЕ (Вызывать каждый кадр) ---
    void Camera::Inputs(GLFWwindow* window, float deltaTime)
    {
        // Управление мышью (Вращение головы) работает всегда, НО в редакторе только с зажатой ПКМ
        bool allowMouse =(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

        if (allowMouse)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            if (firstClick) { lastMouseX = mouseX; lastMouseY = mouseY; firstClick = false; }

            float rotX = sensitivity * float(mouseY - lastMouseY);
            float rotY = sensitivity * float(mouseX - lastMouseX);
            lastMouseX = mouseX; lastMouseY = mouseY;

            // 1. Безопасно вычисляем вектор "Вправо" (Right)
            glm::vec3 right = glm::cross(Orientation, Up);
            if (glm::length(right) < 0.001f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f); // Запасной вектор, если смотрим в зенит
            }
            else {
                right = glm::normalize(right);
            }

            // 2. Вращение вверх/вниз (Pitch)
            glm::vec3 newOrientation = glm::rotate(Orientation, glm::radians(-rotX), right);

            // 3. ПУЛЕНЕПРОБИВАЕМАЯ проверка угла (без glm::angle и acos)
            // glm::dot равен 1, если смотрим ровно вверх, и -1, если ровно вниз.
            // abs(dot) < 0.99f ограничивает угол примерно на 82-85 градусах.
            float dot = glm::dot(newOrientation, Up);
            if (abs(dot) < 0.99f) {
                Orientation = newOrientation;
            }

            // 4. Вращение влево/вправо (Yaw)
            Orientation = glm::rotate(Orientation, glm::radians(-rotY), Up);

            // 5. ВАЖНО: Очищаем мусор float!
            // Если этого не делать, за пару минут полетов вектор "растянется" и сломает матрицы.
            Orientation = glm::normalize(Orientation);
        }
        else
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            firstClick = true;
        }

            // РЕЖИМ РЕДАКТОРА (Unreal Engine Style)
            // Движение работает ТОЛЬКО если зажата правая кнопка мыши!
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {

                float currentSpeed = speed;
                // Ускорение на Shift
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) currentSpeed *= 20.0f;

                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) Position += currentSpeed * Orientation;
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) Position += currentSpeed * -Orientation;
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) Position += currentSpeed * -glm::normalize(glm::cross(Orientation, Up));
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) Position += currentSpeed * glm::normalize(glm::cross(Orientation, Up));

                // Взлет и спуск (E - вверх, Q - вниз)
                if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) Position += currentSpeed * Up;
                if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) Position += currentSpeed * -Up;
        }
    }

}  // namespace burnhope
