// Mesh-stage geometry via BDA. Object matrix via BDA byte loads; aabb/material in push (no heap).
import common.SceneGpu;

/** std430 ObjectData stride 192 — row-major modelMatrix (CPU Transform.matrix, no toGpuMatrix). */
export void meshLoadModelMatrix(uint64_t objectBda, uint objectId, out row_major float4x4 outMat) {
    uint64_t base = objectBda + uint64_t(objectId) * 192u;
    outMat = *((row_major float4x4*)base);
}

export void meshLoadObjectAabb(uint64_t objectBda, uint objectId, out float3 aabbMin, out float3 aabbMax) {
    uint64_t base = objectBda + uint64_t(objectId) * 192u;
    float4 mn = *((float4*)(base + 128u));
    float4 mx = *((float4*)(base + 144u));
    aabbMin = mn.xyz;
    aabbMax = mx.xyz;
}

export uint meshLoadMaterialId(uint64_t objectBda, uint objectId) {
    uint64_t base = objectBda + uint64_t(objectId) * 192u;
    return *((uint*)(base + 64u));
}

/** 8 bytes/vert: uint16 x,y,z + pad (Render/Model.hpp PackedVertexPos). */
export PackedVertexPos meshLoadPos(uint64_t posBda, uint index) {
    uint64_t packed = *((uint64_t*)(posBda + uint64_t(index) * 8u));
    PackedVertexPos v;
    v.x = uint16_t(packed & 0xFFFFu);
    v.y = uint16_t((packed >> 16) & 0xFFFFu);
    v.z = uint16_t((packed >> 32) & 0xFFFFu);
    v.pad = uint16_t((packed >> 48) & 0xFFFFu);
    return v;
}

export PackedVertexAttr meshLoadAttr(uint64_t attrBda, uint index) {
    uint64_t packed = *((uint64_t*)(attrBda + uint64_t(index) * 8u));
    PackedVertexAttr a;
    a.texUV = uint(packed & 0xFFFFFFFFu);
    a.qTangent = uint(packed >> 32);
    return a;
}

export uint meshLoadIndex(uint64_t idxBda, uint corner) {
    uint* ptr = (uint*)(idxBda + uint64_t(corner) * 4u);
    return *ptr;
}
