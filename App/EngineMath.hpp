#pragma once

#include "../Utils/DirectXMathCompat.hpp"
#include <cmath>

namespace burnhope {

/** CPU: DirectX row-vectors — v' = v * View * Projection (apply View, then Projection). */
inline float4x4 rowViewProjection(const float4x4& view, const float4x4& projection) {
  return MatrixMultiply(view, projection);
}

/**
 * GPU / Slang (GlobalUbo, gbuffer.frag): mul(matrix, columnVector).
 * Upload transpose(rowMajor) so mul(toGpuMatrix(V), w) matches v_row * V.
 *
 * Mesh path (minimal): row_major push + BDA modelMatrix WITHOUT toGpuMatrix;
 * mul(mul(mul(p, M), V), P) — matches DirectXMath row-vector convention.
 */
inline float4x4 toGpuMatrix(const float4x4& rowMajor) { return MatrixTranspose(rowMajor); }

/** Inverse view-projection for mul(invViewProj, clipColumn) world reconstruction. */
inline float4x4 gpuInvViewProjection(const float4x4& view, const float4x4& projection) {
  return MatrixTranspose(MatrixInverse(rowViewProjection(view, projection)));
}

/** Motion / TAA: row-vector prev VP for mul(prevViewProj, worldColumn). */
inline float4x4 gpuPrevViewProjection(const float4x4& view, const float4x4& projection) {
  return rowViewProjection(view, projection);
}

/** Row-vector inv(VP) for mul(invViewProj, clipColumn). */
inline float4x4 gpuInvViewProjectionRow(const float4x4& view, const float4x4& projection) {
  return MatrixInverse(rowViewProjection(view, projection));
}

inline float4x4 shadowPerspective(float fovY, float aspect, float zNear, float zFar) {
  float4x4 proj = MatrixPerspectiveFovLH(Radians(fovY), aspect, zNear, zFar);
  proj._22 *= -1.0f;
  return proj;
}

inline float4x4 computeObliqueProjection(
    const float4x4& proj,
    const float4x4& view,
    const float3& planePos,
    const float3& planeNormal) {
  const float4x4 viewInv = MatrixInverse(view);
  const float4x4 viewInvT = MatrixTranspose(viewInv);
  const float3 normalView = Normalize(TransformVector(planeNormal, viewInvT));
  const float4 pointView = TransformFloat4(float4{planePos.x, planePos.y, planePos.z, 1.0f}, view);
  float d = -Dot(normalView, float3{pointView.x, pointView.y, pointView.z});
  float4 clipPlane{normalView.x, normalView.y, normalView.z, d};

  if (clipPlane.z > 0.0f) {
    clipPlane = -clipPlane;
  }

  float4x4 result = proj;
  float4 q;
  q.x = (Sign(clipPlane.x) + proj._31) / proj._11;
  q.y = (Sign(clipPlane.y) + proj._32) / proj._22;
  q.z = -1.0f;
  q.w = (1.0f + proj._33) / proj._43;

  const float dotProd = Dot(clipPlane, q);
  if (std::abs(dotProd) < 1e-6f) {
    return proj;
  }
  const float4 c = clipPlane / dotProd;
  result._13 = c.x;
  result._23 = c.y;
  result._33 = c.z;
  result._43 = c.w;
  return result;
}

inline VkTransformMatrixKHR toVkTransformMatrix(const float4x4& m) {
  VkTransformMatrixKHR out{};
  const float mData[16] = {
      m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24,
      m._31, m._32, m._33, m._34, m._41, m._42, m._43, m._44};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
      out.matrix[i][j] = mData[j * 4 + i];
    }
  }
  return out;
}

} // namespace burnhope
