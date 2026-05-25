#pragma once

namespace burnhope {

inline constexpr bool kMinimalRenderPath = true;
/** false = G-Buffer → lighting.comp → HDR blit (deferred). true = debug blit albedo only. */
inline constexpr bool kMinimalBlitGBufferAlbedo = false;
inline constexpr bool kMinimalUseDeferredLighting = true;

/**
 * Minimal-path visual debug (change value, rebuild, run).
 * 0 = normal (albedo blit)
 * 1 = cyan clear on swapchain only (no G-Buffer) — must differ from magenta blit
 * 2 = (unused) use kGBufferDebugFragMode in RenderDebug.hpp
 * 3 = blit G-Buffer depth as grayscale
 * 4 = blit G-Buffer normals
 * 5 = G-Buffer frag: show materialID as color
 */
inline constexpr int kMinimalDebugStage = 0;

inline constexpr bool kMinimalBlitToSwapchainPresent = false;

/**
 * true = no vkCmdBlitImage; black clear + present (isolates mesh vs blit DEVICE_LOST).
 * Rebuild not required — C++ only.
 */
inline constexpr bool kMinimalSkipBlitPresent = false;

inline constexpr bool kMinimalUseImGuiSceneViewport = false;

/** false: transpose view/proj for mul(P, V, M, p) in shaders. */
inline constexpr bool kGpuRowVectorMul = false;

/**
 * G-Buffer frag debug (packed into GfxHeapPC.gfxDebug).
 * 0 = normal  1 = solid green  2 = solid magenta  3 = materialID heatmap
 */
/** 6 = SceneAddresses BDA isolate tests (see kGBufferDebugFragSubTest). */
inline constexpr uint32_t kGBufferDebugFragMode = 0u;
/**
 * Mode 6 sub-tests (gfxDebug bits 4..6). Rebuild shaders after change.
 * 0 = UV checkerboard (R/G stripes) — NOT texture
 * 1 = BDA GlobalUbo sunColor
 * 2 = material albedoColor only (no texture)
 * 3,5,6 = albedo heap sample at mesh UV (same path)
 * 4 = index diagnostic (white = indices OK, not texture)
 * 7 = textureTableBase slot + mesh UV
 */
inline constexpr uint32_t kGBufferDebugFragSubTest = 0u;

/**
 * Minimal + frag mode 0: no loadGlobalUbo / bindless textures (NV DEVICE_LOST with full frag).
 */
/** true = frag without heap (vertex color only). Mesh/task never use heap. */
inline constexpr bool kMinimalFragNoHeap = false;

/**
 * Material SSBO only (no UBO / textures). Requires kMinimalFragNoHeap = false;
 * bit 64 and 128 are mutually exclusive in packGfxDebugFlags().
 */
inline constexpr bool kMinimalFragMaterialOnly = false;
inline constexpr bool kMinimalFragUboOnly = false;

/**
 * Mesh NDC triangle (no heap vertex reads). Toggle here, rebuild shaders + restart.
 * Do NOT add runtime branches in gbuffer.mesh.slang — NV driver crashes vkCreateShadersEXT
 * when SPIR-V includes VariablePointersStorageBuffer from conditional heap bindings.
 */
inline constexpr bool kMeshForceNdcTriangle = false;

inline constexpr uint32_t packGfxDebugFlags() noexcept {
  uint32_t flags = (kGBufferDebugFragMode & 15u) | ((kGBufferDebugFragSubTest & 7u) << 4);
  if (kMeshForceNdcTriangle) {
    flags |= 16u;
  }
  if (kMinimalRenderPath && kMinimalFragMaterialOnly) {
    flags |= 128u;
  } else if (kMinimalRenderPath && kMinimalFragUboOnly) {
    flags |= 256u;
  } else if (kMinimalRenderPath && kMinimalFragNoHeap) {
    flags |= 64u;
  }
  return flags;
}

} // namespace burnhope
