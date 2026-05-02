#version 450

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent; // Возвращаем тангенс!
layout (location = 4) in vec3 aBitangent;

// Push constant с матрицей света
layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
} push;

// ObjectBuffer — для получения modelMatrix по instanceIndex
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint meshID;
    uint padding1;
    uint padding2;
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
