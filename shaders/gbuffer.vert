#version 450

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent; 

layout (location = 0) out vec3 outCrntPos;
layout (location = 1) out vec2 outTexCoord;
layout (location = 2) out mat3 outTBN;
layout (location = 5) flat out uint outMatID; 

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
    float cascadeSplits[4];
    uint gridDimX;
    uint gridDimY;
    uint gridDimZ;
    float lightSize;
} ubo;

struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint meshID;
    uint padding1;
    uint padding2;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

void main() {
    
    uint globalIndex = gl_InstanceIndex; 
ObjectData obj = objectBuffer.objects[globalIndex];
    
    mat4 modelMatrix = obj.modelMatrix;
    outMatID = obj.materialID;

    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    outCrntPos = worldPos.xyz;
    outTexCoord = aTex;

    
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));

    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent);
    T = normalize(T - dot(T, N) * N); 
    vec3 B = cross(N, T);
    
    outTBN = mat3(T, B, N);

    gl_Position = ubo.projection * ubo.view * worldPos;
}