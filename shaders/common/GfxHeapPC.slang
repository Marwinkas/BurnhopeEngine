// Push block 256 bytes (std430: materialStorageBda @200, _pad208[12] @208).
export struct GfxHeapPC {
    /** DirectX row-major from CPU (mul(v,M,V,P)). Not toGpuMatrix — frag uses GlobalUbo separately. */
    row_major float4x4 meshProjection;   // 0
    row_major float4x4 meshView;         // 64
    uint64_t sceneAddressesBda; // 128 — SceneAddresses (UBO/object/mat/bone BDA + sampler index)
    uint64_t meshPosBda;
    uint64_t meshAttrBda;
    uint64_t meshIdxBda;       // 152
    uint gfxDebug;             // 160
    uint cpuLodTriCount;
    uint meshVertexCount;
    uint meshIndexBase;        // 172
    uint meshMaterialId;       // 176
    uint heapResourceBase;     // 180 — shader-object resource heap push index (must be 0)
    uint globalUboHeap;        // 184
    uint defaultSampler;       // 188
    uint textureTableCount;    // 192
    uint _pad196;              // 196 (std430 alignment before uint64)
    uint64_t materialStorageBda; // 200
    uint _pad208[12];          // 208..255 (256-byte push limit)
};
