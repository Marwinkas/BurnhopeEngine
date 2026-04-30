#version 460 core
#extension GL_ARB_shader_draw_parameters : require // Важно для gl_BaseInstanceARB

layout (location = 0) in vec3 aPos;

// ТОТ ЖЕ САМЫЙ БУФЕР, ЧТО И В ГЕОМЕТРИИ!
struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint pad1, pad2, pad3;
};

layout(std430, binding = 10) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

uniform mat4 lightProjection;

void main() {
    // Берем индекс и матрицу прямо из глобального SSBO!
    uint globalIndex = gl_BaseInstanceARB + gl_InstanceID; 
    mat4 instanceMatrix = objects[globalIndex].modelMatrix;
    
    gl_Position = lightProjection * instanceMatrix * vec4(aPos, 1.0);
}