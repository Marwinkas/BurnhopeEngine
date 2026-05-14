#version 450

layout(location = 0) in vec4 aPosAABB;

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
    vec3 aabbMin = vec3(-1.0, -1.0, 0.0);
    vec3 aabbMax = vec3(1.0, 1.0, 0.0);
    vec3 extent = aabbMax - aabbMin;
    
    vec3 localPos = aabbMin + aPosAABB.xyz * extent;
    gl_Position = ubo.projection * ubo.view * push.modelMatrix * vec4(localPos, 1.0);
}