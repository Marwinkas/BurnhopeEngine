#include "Camera.hpp"
#include <cassert>
#include <limits>
#include <SDL3/SDL.h>
namespace burnhope
{
    Camera::Camera(int width, int height, float3 position)
    {
        Camera::width = width;
        Camera::height = height;
        Position = position;
    }

    float4x4 Camera::GetViewMatrix() const
    {
        return MatrixLookAtLH(Position, float3{Position.x + Orientation.x, Position.y + Orientation.y, Position.z + Orientation.z}, Up);
    }

    float4x4 Camera::GetProjectionMatrix(float fovDeg, float nearPlane, float farPlane) const
    {
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        return GetProjectionMatrix(fovDeg, nearPlane, aspect, farPlane);
    }

    float4x4 Camera::GetProjectionMatrix(float fovDeg, float nearPlane, float aspect, float farPlane) const
    {
        float4x4 proj = MatrixPerspectiveFovLH(Radians(fovDeg), aspect, nearPlane, farPlane);
        proj._22 *= -1.0f;
        return proj;
    }

    void Camera::setViewMatrix(const float4x4& viewMatrix)
    {
        float4x4 invView = MatrixInverse(viewMatrix);

        Position = float3{invView._41, invView._42, invView._43};

        // Extract forward vector from view matrix (negated 3rd row)
        float3 forward = float3{-invView._31, -invView._32, -invView._33};
        Orientation = Normalize(forward);

        // Extract up vector from view matrix (2nd row)
        float3 up = float3{invView._21, invView._22, invView._23};
        Up = Normalize(up);
    }

    void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane)
    {
        float4x4 view = GetViewMatrix();
        float4x4 projJitter = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        viewProjectionMatrix = MatrixMultiply(projJitter, view);
        float4x4 projClean = GetProjectionMatrix(FOVdeg, nearPlane, farPlane);
        cleanViewProjectionMatrix = MatrixMultiply(projClean, view);
    }

    bool Camera::IsSphereInFrustum(const float4 *planes, const float3 &center, float radius) const
    {
        for (int i = 0; i < 6; i++)
        {
            float3 planeNormal{planes[i].x, planes[i].y, planes[i].z};
            float dist = Dot(planeNormal, center) + planes[i].w;
            if (dist < -radius)
            {
                return false;
            }
        }
        return true;
    }

    void Camera::Inputs(SDL_Window *window, float deltaTime)
    {
        float relX, relY;
        // В SDL3 GetRelativeMouseState возвращает дельту с момента последнего вызова и маску кнопок
        SDL_MouseButtonFlags mouseState = SDL_GetRelativeMouseState(&relX, &relY);

        bool allowMouse = (mouseState & SDL_BUTTON_RMASK) != 0;
        if (allowMouse)
        {
            SDL_SetWindowRelativeMouseMode(window, true);
            
            // Используем дельты напрямую. firstClick больше не нужен для предотвращения скачков,
            // так как GetRelativeMouseState при первом вызове после паузы сбросит аккумулятор.
            float rotX = sensitivity * relY;
            float rotY = -sensitivity * relX;

            float3 right = Cross(Orientation, Up);
            if (Length(right) < 0.001f)
            {
                right = float3{1.0f, 0.0f, 0.0f};
            }
            else
            {
                right = Normalize(right);
            }

            // Rotation around X axis (pitch)
            float pitchAngle = Radians(-rotX);
            float4x4 pitchRot = MatrixRotationAxis(right, pitchAngle);
            float4 newOrient4 = TransformFloat4(float4{Orientation.x, Orientation.y, Orientation.z, 0.0f}, pitchRot);
            float3 newOrientation{newOrient4.x, newOrient4.y, newOrient4.z};

            float dot = Dot(newOrientation, Up);
            if (abs(dot) < 0.99f)
            {
                Orientation = newOrientation;
            }

            // Rotation around Y axis (yaw)
            float yawAngle = Radians(-rotY);
            float4x4 yawRot = MatrixRotationAxis(Up, yawAngle);
            float4 orient4 = TransformFloat4(float4{Orientation.x, Orientation.y, Orientation.z, 0.0f}, yawRot);
            float3 orient3{orient4.x, orient4.y, orient4.z};
            Orientation = Normalize(orient3);
        }
        else
        {
            SDL_SetWindowRelativeMouseMode(window, false);
            firstClick = true;
        }
        // ОШИБКА: SDL_BUTTON_RIGHT — это индекс (3), а нам нужна маска (SDL_BUTTON_RMASK = 4)
        if ((mouseState & SDL_BUTTON_RMASK) != 0)
        {
            float currentSpeed = speed;
            const bool* keyboardState = SDL_GetKeyboardState(NULL);
            if (keyboardState[SDL_SCANCODE_LSHIFT] || keyboardState[SDL_SCANCODE_RSHIFT])
                currentSpeed *= 20.0f;
            if (keyboardState[SDL_SCANCODE_W])
                Position = float3{Position.x + currentSpeed * Orientation.x, Position.y + currentSpeed * Orientation.y, Position.z + currentSpeed * Orientation.z};
            if (keyboardState[SDL_SCANCODE_S])
                Position = float3{Position.x - currentSpeed * Orientation.x, Position.y - currentSpeed * Orientation.y, Position.z - currentSpeed * Orientation.z};
            if (keyboardState[SDL_SCANCODE_A])
            {
                float3 right = Normalize(Cross(Orientation, Up));
                Position = float3{Position.x + currentSpeed * right.x, Position.y - currentSpeed * right.y, Position.z - currentSpeed * right.z};
            }
            if (keyboardState[SDL_SCANCODE_D])
            {
                float3 right = Normalize(Cross(Orientation, Up));
                Position = float3{Position.x - currentSpeed * right.x, Position.y + currentSpeed * right.y, Position.z + currentSpeed * right.z};
            }
            if (keyboardState[SDL_SCANCODE_E])
                Position = float3{Position.x + currentSpeed * Up.x, Position.y + currentSpeed * Up.y, Position.z + currentSpeed * Up.z};
            if (keyboardState[SDL_SCANCODE_Q])
                Position = float3{Position.x - currentSpeed * Up.x, Position.y - currentSpeed * Up.y, Position.z - currentSpeed * Up.z};
        }
    }
}
