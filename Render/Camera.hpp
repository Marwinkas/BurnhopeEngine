#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <array>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
namespace burnhope
{
    class Camera
    {
    public:
        glm::vec3 Position;
        glm::vec3 Orientation = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
        glm::mat4 cleanViewProjectionMatrix = glm::mat4(1.0f);
        double lastMouseX = 0.0, lastMouseY = 0.0;
        bool firstClick = true;
        int width;
        int height;
        float speed = 0.01f;
        float sensitivity = 0.2f;

        Camera(int width, int height, glm::vec3 position);

        void setViewMatrix(const glm::mat4& viewMatrix);

        bool IsSphereInFrustum(const glm::vec4 *planes, const glm::vec3 &center, float radius);
        
        glm::mat4 GetProjectionMatrix(float FOVdeg, float nearPlane, float farPlane) const
        {
            glm::mat4 proj = glm::perspective(glm::radians(FOVdeg), (float)width / (float)height, nearPlane, farPlane);
            proj[1][1] *= -1;
            return proj;
        }

        glm::mat4 GetProjectionMatrix(float FOVdeg, float nearPlane, float aspect, float farPlane) const
        {
            glm::mat4 proj = glm::perspective(glm::radians(FOVdeg), aspect, nearPlane, farPlane);
            proj[1][1] *= -1;
            return proj;
        }

        glm::mat4 GetViewMatrix() const
        {
            return glm::lookAt(Position, Position + Orientation, Up);
        }

        glm::mat4 GetViewProjectionMatrix()
        {
            return viewProjectionMatrix;
        }
        
        void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
        void Inputs(GLFWwindow *window, float deltaTime);
    };
}
#endif