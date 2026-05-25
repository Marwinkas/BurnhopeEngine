#pragma once

#include "../Utils/DirectXMathCompat.hpp"
#include <SDL3/SDL.h>

namespace burnhope {

class Camera final {
public:
  float3 Position{};
  float3 Orientation{0.0f, 0.0f, -1.0f};
  float3 Up{0.0f, 1.0f, 0.0f};
  float4x4 viewProjectionMatrix = MatrixIdentity();
  float4x4 cleanViewProjectionMatrix = MatrixIdentity();
  double lastMouseX{0.0};
  double lastMouseY{0.0};
  bool firstClick{true};
  int width{800};
  int height{600};
  float speed{0.01f};
  float sensitivity{0.2f};

  Camera(int w, int h, float3 position);

  void setViewMatrix(const float4x4& viewMatrix);
  [[nodiscard]] bool IsSphereInFrustum(const float4* planes, const float3& center, float radius) const;

  [[nodiscard]] float4x4 GetProjectionMatrix(float fovDeg, float nearPlane, float farPlane) const;
  [[nodiscard]] float4x4 GetProjectionMatrix(float fovDeg, float nearPlane, float aspect, float farPlane) const;
  [[nodiscard]] float4x4 GetViewMatrix() const;
  [[nodiscard]] float4x4 GetViewProjectionMatrix() const noexcept { return viewProjectionMatrix; }

  void updateMatrix(float fovDeg, float nearPlane, float farPlane);
  void Inputs(SDL_Window* window, float deltaTime);
};

} // namespace burnhope
