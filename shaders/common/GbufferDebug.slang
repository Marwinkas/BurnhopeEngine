// Runtime G-Buffer debug via GfxHeapPC.gfxDebug (set from Render/RenderDebug.hpp).

export bool meshForceNdcTriangle(uint gfxDebug) {
    return (gfxDebug & 16u) != 0u;
}

export uint gbufferFragDebugMode(uint gfxDebug) {
    return gfxDebug & 15u;
}

/** Bit 64: minimal path frag mode 0 without heap (NV: mesh BDA + frag heap = DEVICE_LOST). */
export bool gbufferMinimalFragNoHeap(uint gfxDebug) {
    return (gfxDebug & 64u) != 0u;
}

/** Bit 128: material SSBO only — no GlobalUbo, no texture samples (bisect NV crash). */
export bool gbufferMinimalFragMaterialOnly(uint gfxDebug) {
    return (gfxDebug & 128u) != 0u;
}

/** Bit 256: material + GlobalUbo heap, no texture samples. */
export bool gbufferMinimalFragUboOnly(uint gfxDebug) {
    return (gfxDebug & 256u) != 0u;
}

/** Bit 512: visualize scene.globalUboHeap from BDA (no heap bindless read). */
export bool gbufferDebugSceneHeapIndex(uint gfxDebug) {
    return (gfxDebug & 512u) != 0u;
}

