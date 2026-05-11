#include "Camera.hpp"
#include <cassert>
#include <limits>
#include <GLFW/glfw3.h>
namespace burnhope
{
    Camera::Camera(int width, int height, glm::vec3 position)
    {
        Camera::width = width;
        Camera::height = height;
        Position = position;
    }

    void Camera::setViewMatrix(const glm::mat4& viewMatrix)
    {
        glm::mat4 invView = glm::inverse(viewMatrix);

        Position = glm::vec3(invView[3]);

        Orientation = -glm::normalize(glm::vec3(invView[2]));

        Up = glm::normalize(glm::vec3(invView[1]));
    }

    void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane)
    {
        glm::mat4 view = GetViewMatrix();
        glm::mat4 projJitter = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        viewProjectionMatrix = projJitter * view;
        glm::mat4 projClean = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        cleanViewProjectionMatrix = projClean * view;
    }
    bool Camera::IsSphereInFrustum(const glm::vec4 *planes, const glm::vec3 &center, float radius)
    {
        for (int i = 0; i < 6; i++)
        {
            if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius)
            {
                return false;
            }
        }
        return true;
    }
    void Camera::Inputs(GLFWwindow *window, float deltaTime)
    {
        bool allowMouse = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        if (allowMouse)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            if (firstClick)
            {
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                firstClick = false;
            }
            float rotX = sensitivity * float(mouseY - lastMouseY);
            float rotY = sensitivity * float(mouseX - lastMouseX);
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            glm::vec3 right = glm::cross(Orientation, Up);
            if (glm::length(right) < 0.001f)
            {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                right = glm::normalize(right);
            }
            glm::vec3 newOrientation = glm::rotate(Orientation, glm::radians(-rotX), right);
            float dot = glm::dot(newOrientation, Up);
            if (abs(dot) < 0.99f)
            {
                Orientation = newOrientation;
            }
            Orientation = glm::rotate(Orientation, glm::radians(-rotY), Up);
            Orientation = glm::normalize(Orientation);
        }
        else
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            firstClick = true;
        }
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        {
            float currentSpeed = speed;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
                currentSpeed *= 20.0f;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
                Position += currentSpeed * Orientation;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
                Position += currentSpeed * -Orientation;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
                Position += currentSpeed * -glm::normalize(glm::cross(Orientation, Up));
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
                Position += currentSpeed * glm::normalize(glm::cross(Orientation, Up));
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
                Position += currentSpeed * Up;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
                Position += currentSpeed * -Up;
        }
    }
}
