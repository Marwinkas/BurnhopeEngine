#include "../Header/Camera.h"
#include "../Header/PhysicsEngine.h" // Подключаем физику именно здесь!
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

void Camera::Matrix(Shader& shader, const char* uniform)
{
    // Передаем новую правильную комбинированную матрицу
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, uniform), 1, GL_FALSE, glm::value_ptr(viewProjectionMatrix));
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
// --- СОЗДАНИЕ/УДАЛЕНИЕ ФИЗИКИ КАМЕРЫ ---
void Camera::TogglePlayMode(PhysicsEngine& physics) {
  
}

// --- ОБНОВЛЕНИЕ ПОЗИЦИИ ИЗ PHYSX (Вызывать каждый кадр) ---
void Camera::UpdatePhysics(float deltaTime) {
    
}

// --- УПРАВЛЕНИЕ (Вызывать каждый кадр) ---
void Camera::Inputs(GLFWwindow* window, float deltaTime)
{
    // Управление мышью (Вращение головы) работает всегда, НО в редакторе только с зажатой ПКМ
    bool allowMouse = isPlayMode || (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

    if (allowMouse)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (firstClick) { lastMouseX = mouseX; lastMouseY = mouseY; firstClick = false; }

        float rotX = sensitivity * float(mouseY - lastMouseY);
        float rotY = sensitivity * float(mouseX - lastMouseX);
        lastMouseX = mouseX; lastMouseY = mouseY;

        // Вращение вверх/вниз
        glm::vec3 newOrientation = glm::rotate(Orientation, glm::radians(-rotX), glm::normalize(glm::cross(Orientation, Up)));
        if (abs(glm::angle(newOrientation, Up) - glm::radians(90.0f)) <= glm::radians(85.0f)) {
            Orientation = newOrientation;
        }
        // Вращение влево/вправо
        Orientation = glm::rotate(Orientation, glm::radians(-rotY), Up);
    }
    else
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        firstClick = true;
    }


    // --- ДВИЖЕНИЕ ---
    if (isPlayMode) {
   

    }
    else
    {
        // РЕЖИМ РЕДАКТОРА (Unreal Engine Style)
        // Движение работает ТОЛЬКО если зажата правая кнопка мыши!
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {

            float currentSpeed = speed;
            // Ускорение на Shift
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) currentSpeed *= 4.0f;

            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) Position += currentSpeed * Orientation;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) Position += currentSpeed * -Orientation;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) Position += currentSpeed * -glm::normalize(glm::cross(Orientation, Up));
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) Position += currentSpeed * glm::normalize(glm::cross(Orientation, Up));

            // Взлет и спуск (E - вверх, Q - вниз)
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) Position += currentSpeed * Up;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) Position += currentSpeed * -Up;
        }
    }
}