#include "Camera.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <limits>

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
    void Camera::Inputs(SDL_Window *window, float deltaTime)
    {
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mouseX, &mouseY);
        const bool allowMouse = (mouseState & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
        if (allowMouse)
        {
            SDL_SetWindowRelativeMouseMode(window, true);
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
            SDL_SetWindowRelativeMouseMode(window, false);
            firstClick = true;
        }
        if (allowMouse)
        {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            float currentSpeed = speed;
            if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
                currentSpeed *= 20.0f;
            if (keys[SDL_SCANCODE_W])
                Position += currentSpeed * Orientation;
            if (keys[SDL_SCANCODE_S])
                Position += currentSpeed * -Orientation;
            if (keys[SDL_SCANCODE_A])
                Position += currentSpeed * -glm::normalize(glm::cross(Orientation, Up));
            if (keys[SDL_SCANCODE_D])
                Position += currentSpeed * glm::normalize(glm::cross(Orientation, Up));
            if (keys[SDL_SCANCODE_E])
                Position += currentSpeed * Up;
            if (keys[SDL_SCANCODE_Q])
                Position += currentSpeed * -Up;
        }
    }
}
