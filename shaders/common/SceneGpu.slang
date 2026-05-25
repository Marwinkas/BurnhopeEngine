// Shared scene GPU layouts — matches Render/Core/SceneGpuTypes.hpp.
import common.SlangBuiltins;

export struct ObjectData {
    float4x4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint posHeap;
    uint attrHeap;
    uint indexHeap;
    uint vertexCount;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    uint _pad120[2];
    float4 aabbMin;
    float4 aabbMax;
    uint _pad0;
    uint _pad1;
    uint _pad168[2];
    uint4 _pad192;
};

export struct SubMeshInfo {
    float3 aabbMin;
    float boundingRadius;
    float3 aabbMax;
    uint lodCount;
    uint indexCounts[4];
    uint firstIndices[4];
    uint materialIndex;
    uint pad1, pad2, pad3;
};

export struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

export struct PackedVertexPos {
    uint16_t x, y, z, pad;
};

export struct PackedVertexAttr {
    uint texUV;
    uint qTangent;
};

export struct PackedVertexAnim {
    uint16_t pivotX, pivotY, pivotZ, cloth_ao;
    uint boneIndices;
    uint boneWeights;
};

export struct PosBuffer {
    PackedVertexPos p[];
};

export struct AttrBuffer {
    PackedVertexAttr a[];
};

export struct IndexBuffer {
    uint i[];
};

export struct ColorBuffer {
    uint c[];
};

export struct UV2Buffer {
    uint u[];
};

export struct AnimBuffer {
    PackedVertexAnim a[];
};

export float3x3 inverse3x3(float3x3 m) {
    float det = dot(m[0], cross(m[1], m[2]));
    float3x3 adj = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    return transpose(adj) / det;
}

export float2 unpackUV(uint uvPacked) {
    return unpackHalf2x16(uvPacked);
}

export float4 unpackQTangent(uint packedVal) {
    int ix = (int(packedVal) << 22) >> 22;
    int iy = (int(packedVal) << 12) >> 22;
    int iz = (int(packedVal) << 2) >> 22;
    int iw = (int(packedVal) >> 30);
    return float4(
        max(float(ix) / 511.0, -1.0),
        max(float(iy) / 511.0, -1.0),
        max(float(iz) / 511.0, -1.0),
        max(float(iw) / 1.0, -1.0)
    );
}

export float3 getNormalFromQTangent(uint packedVal) {
    float4 q = unpackQTangent(packedVal);
    float qx = q.x;
    float qy = q.y;
    float qz = q.z;
    float qw = sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz));
    return float3(
        2.0 * qx * qz + 2.0 * qy * qw,
        2.0 * qy * qz - 2.0 * qx * qw,
        1.0 - 2.0 * qx * qx - 2.0 * qy * qy);
}
