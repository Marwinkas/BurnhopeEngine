#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout (location = 0) in vec4 aPosAABB;

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
} push;
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint64_t posBufferAddress;
    uint64_t attrBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    vec4 aabbMin;
    vec4 aabbMax;
};



layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

layout(std430, set = 0, binding = 1) readonly buffer BoneMatricesBuffer {
    mat4 boneMatrices[];
};

struct PackedVertexAnim {
    uint16_t pivotX, pivotY, pivotZ, cloth_ao;
    uint boneIndices;
    uint boneWeights;
};
layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer AnimBuffer { PackedVertexAnim a[]; };

void main() {
    uint globalIndex = gl_InstanceIndex; 
    ObjectData obj = objectBuffer.objects[globalIndex];
    
    vec3 extent = obj.aabbMax.xyz - obj.aabbMin.xyz;
    vec3 localPos = obj.aabbMin.xyz + aPosAABB.xyz * extent;
    
    // Initialize to identity to handle cases where boneOffset is valid but animBuffer is not
    mat4 skinMat = mat4(1.0);
    if (obj.boneOffset != 0xFFFFFFFF && obj.animBufferAddress != 0) {
        AnimBuffer anBuf = AnimBuffer(obj.animBufferAddress);
        PackedVertexAnim vtxAn = anBuf.a[gl_VertexIndex];
        uint bInd = vtxAn.boneIndices;
        uint bWgh = vtxAn.boneWeights;
        
        float w0 = float((bWgh >> 0) & 0xFF) / 255.0;
        float w1 = float((bWgh >> 8) & 0xFF) / 255.0;
        float w2 = float((bWgh >> 16) & 0xFF) / 255.0;
        float w3 = float((bWgh >> 24) & 0xFF) / 255.0;
        
        skinMat = mat4(0.0); // Reset for summation
        if (w0 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 0) & 0xFF)] * w0;
        if (w1 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 8) & 0xFF)] * w1;
        if (w2 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 16) & 0xFF)] * w2;
        if (w3 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 24) & 0xFF)] * w3;
    }

    vec4 worldPos = obj.boneOffset != 0xFFFFFFFF ? (obj.modelMatrix * skinMat * vec4(localPos, 1.0)) : (obj.modelMatrix * vec4(localPos, 1.0));
    gl_Position = push.lightSpaceMatrix * worldPos;
}
