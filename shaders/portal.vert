#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 normal;
layout(location = 2) in vec2 texUV;
layout(location = 3) in vec4 tangent;

layout(set = 0, binding = 0) uniform GlobalSceneUbo {
    mat4 projection;
    mat4 invViewProj;
    mat4 view;
    vec3 camPos; 
    float zNear;
    vec3 sunDir;
    float zFar;
    vec4 screenSize;
    mat4 sunLightSpaceMatrices[4];
     vec4 cascadeSplits;
    uint gridDimX; 
    uint gridDimY;
    uint gridDimZ;
    float lightSize;
        vec3 sunColor;  
    float sunIntensity; 
} ubo;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    uint portalID;
} push;

void main() {
    // Вычисляем итоговую позицию вершины на экране
    gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(position, 1.0);
}