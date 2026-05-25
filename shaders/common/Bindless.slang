// Slang bindless helpers — VK_EXT_descriptor_heap via DescriptorHandle.
import common.GlobalUbo;

/** Keeps heap indices 32-bit (Slang otherwise emits invalid OpCompositeExtract from ulong). */
export uint heapIndex(uint slot) { return slot; }

export struct HeapIndices {
    uint globalUbo;
    uint objectBuffer;
    uint subMeshBuffer;
    uint drawCommands;
    uint hiZTex;
    uint textureBase;
};

export Texture2D<float4> heapTex2D(uint index) {
    return DescriptorHandle<Texture2D<float4>>(heapIndex(index));
}

export Texture2D<float> heapTex2D_r(uint index) {
    return DescriptorHandle<Texture2D<float>>(heapIndex(index));
}

export SamplerState heapSampler(uint index) {
    return DescriptorHandle<SamplerState>(heapIndex(index));
}

export RWTexture2D<float4> heapRWTex2D(uint index) {
    return DescriptorHandle<RWTexture2D<float4>>(heapIndex(index));
}

export RWTexture2D<float> heapRWTex2D_r(uint index) {
    return DescriptorHandle<RWTexture2D<float>>(heapIndex(index));
}

export RWTexture2D<unorm float4> heapRWTex2D_unorm(uint index) {
    return DescriptorHandle<RWTexture2D<unorm float4>>(heapIndex(index));
}

export Texture2DArray<float> heapTex2DArray(uint index) {
    return DescriptorHandle<Texture2DArray<float>>(heapIndex(index));
}

export Texture2D<uint> heapTex2D_u(uint index) {
    return DescriptorHandle<Texture2D<uint>>(heapIndex(index));
}

export Texture2D<float4> heapBindlessTex2D(uint index) {
    return DescriptorHandle<Texture2D<float4>>(NonUniformResourceIndex(heapIndex(index)));
}

/** Per-pixel varying heap index — nonuniformEXT. Caller must guard index < 0. */
export Texture2D<float4> heapBindlessTex2DNonUniform(int index) {
    return DescriptorHandle<Texture2D<float4>>(NonUniformResourceIndex(heapIndex(uint(index))));
}

export StructuredBuffer<T> heapStructured<T>(uint slot) {
    return DescriptorHandle<StructuredBuffer<T>>(heapIndex(slot));
}

/** Dynamic heap index (e.g. per-object posHeap) — requires non-uniform indexing. */
export StructuredBuffer<T> heapStructuredNonUniform<T>(uint slot) {
    return DescriptorHandle<StructuredBuffer<T>>(NonUniformResourceIndex(heapIndex(slot)));
}

export RWStructuredBuffer<T> heapRWStructured<T>(uint slot) {
    return DescriptorHandle<RWStructuredBuffer<T>>(heapIndex(slot));
}

export ConstantBuffer<T> heapConstant<T>(uint slot) {
    return *DescriptorHandle<ConstantBuffer<T>>(heapIndex(slot));
}

/** Dereference heap UBO slot (use: `(*heapGlobalUbo(slot)).member`). */
export ConstantBuffer<GlobalUbo> heapGlobalUbo(uint index) {
    return *DescriptorHandle<ConstantBuffer<GlobalUbo>>(heapIndex(index));
}

/** Full struct copy — avoid in gbuffer.frag (use heapGlobalUbo). */
export GlobalUbo loadGlobalUbo(uint index) {
    return *DescriptorHandle<ConstantBuffer<GlobalUbo>>(heapIndex(index));
}
