#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec4 aTangent;
layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
} push;
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint pad0;
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t pad1; // ДОБАВИТЬ
};
layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;
void main() {
    uint globalIndex = gl_InstanceIndex; 
    ObjectData obj = objectBuffer.objects[globalIndex];
    mat4 modelMatrix = obj.modelMatrix;
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    gl_Position = push.lightSpaceMatrix * worldPos;
}
