#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H

#define GLM_ENABLE_EXPERIMENTAL
#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glm/gtx/rotate_vector.hpp>
#include<glm/gtx/vector_angle.hpp>
#include <array>
#include"shaderClass.h"

class PhysicsEngine;

namespace physx {
    class PxRigidDynamic;
}

class Camera
{
public:
    glm::vec3 Position;
    glm::vec3 Orientation = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);

    // --- ПРАВИЛЬНЫЕ ИМЕНА ПЕРЕМЕННЫХ ---
    // Это готовые матрицы (Projection * View), которые ты обновляешь каждый кадр
    glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
    glm::mat4 cleanViewProjectionMatrix = glm::mat4(1.0f); // Для TAA без тряски

    double lastMouseX = 0.0, lastMouseY = 0.0;
    bool firstClick = true;
    int width;
    int height;
    float speed = 0.05f;
    float sensitivity = 1.0f;
    int taaFrameIndex = 0;

    // --- НОВЫЕ ПЕРЕМЕННЫЕ ДЛЯ РЕЖИМА ИГРЫ ---
    bool isPlayMode = false;           // Режим полета (false) или ходьбы (true)
    physx::PxRigidDynamic* playerBody = nullptr; // Наша физическая оболочка
    bool isGrounded = false;           // Стоим ли мы на полу?
    float jumpForce = 5.0f;            // Сила прыжка
    float walkSpeed = 5.0f;            // Скорость ходьбы по пол

    Camera(int width, int height, glm::vec3 position);
    void Matrix(Shader& shader, const char* uniform);
    // 1. МАТРИЦА ПРОЕКЦИИ (Перспектива, FOV, Near/Far)
    bool IsSphereInFrustum(const glm::vec4* planes, const glm::vec3& center, float radius);
    glm::mat4 GetProjectionMatrix(float FOVdeg, float nearPlane, float farPlane)
    {
        glm::mat4 proj = glm::perspective(glm::radians(FOVdeg), (float)width / (float)height, nearPlane, farPlane);
        return proj;
    }

    // 2. МАТРИЦА ВИДА (Где стоит камера и куда смотрит)
    // Раньше была: GetLookAtMatrix()
    glm::mat4 GetViewMatrix()
    {
        return glm::lookAt(Position, Position + Orientation, Up);
    }

    // 3. КОМБИНИРОВАННАЯ МАТРИЦА (Projection * View)
    // Раньше была: GetViewMatrix() - что вызывало жуткую путаницу!
    glm::mat4 GetViewProjectionMatrix()
    {
        return viewProjectionMatrix;
    }

    void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
    void Inputs(GLFWwindow* window, float deltaTime); // Добавили deltaTime для плавной физики!
    void TogglePlayMode(PhysicsEngine& physics);      // Переключатель режимов
    void UpdatePhysics(float deltaTime);              // Обновление координат от капсулы
};
#endif