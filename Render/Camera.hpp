#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H
#include "../Utils/DirectXMathCompat.hpp"
#include <array>
#include <SDL3/SDL.h>
namespace burnhope
{
    class Camera
    {
    public:
        float3 Position;
        float3 Orientation = float3{0.0f, 0.0f, -1.0f};
        float3 Up = float3{0.0f, 1.0f, 0.0f};
        float4x4 viewProjectionMatrix = MatrixIdentity();
        float4x4 cleanViewProjectionMatrix = MatrixIdentity();
        double lastMouseX = 0.0, lastMouseY = 0.0;
        bool firstClick = true;
        int width = 800;
        int height = 600;
        float speed = 0.01f;
        float sensitivity = 0.2f;

        Camera(int width, int height, float3 position);

        void setViewMatrix(const float4x4& viewMatrix);

        bool IsSphereInFrustum(const float4 *planes, const float3 &center, float radius);
        
        float4x4 GetProjectionMatrix(float FOVdeg, float nearPlane, float farPlane) const
        {
            float4x4 proj = MatrixPerspectiveFovLH(Radians(FOVdeg), (float)width / (float)height, nearPlane, farPlane);
            // Vulkan Y-flip
            proj._22 *= -1.0f;
            return proj;
        }

        float4x4 GetProjectionMatrix(float FOVdeg, float nearPlane, float aspect, float farPlane) const
        {
            float4x4 proj = MatrixPerspectiveFovLH(Radians(FOVdeg), aspect, nearPlane, farPlane);
            proj._22 *= -1.0f;
            return proj;
        }

        float4x4 GetViewMatrix() const
        {
            float3 at = float3{Position.x + Orientation.x, Position.y + Orientation.y, Position.z + Orientation.z};
            return MatrixLookAtLH(Position, at, Up);
        }

        float4x4 GetViewProjectionMatrix()
        {
            return viewProjectionMatrix;
        }
        
        void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
        void Inputs(SDL_Window *window, float deltaTime);
    };
}
#endif