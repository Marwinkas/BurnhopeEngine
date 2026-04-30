#version 460 core
#extension GL_ARB_shader_draw_parameters : require

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec3 aTangent; // Возвращаем тангенс!

out vec3 crntPos;
out vec2 texCoord;
out mat3 TBN; 

uniform mat4 camMatrix;

struct ObjectData {
    mat4 modelMatrix;
    uint materialID;
    uint meshID;     // ← это поле было в C++ но не было в GLSL!
    uint padding;
    uint padding2;
};

layout(std430, binding = 10) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

flat out uint matID; 

void main()
{
    uint globalIndex    = gl_BaseInstanceARB + gl_InstanceID;
    ObjectData obj      = objects[globalIndex];
    mat4 instanceMatrix = obj.modelMatrix;
    matID               = obj.materialID;

    vec4 worldPos = instanceMatrix * vec4(aPos, 1.0);
    crntPos  = worldPos.xyz;
    texCoord = aTex;

    mat3 normalMatrix = transpose(inverse(mat3(instanceMatrix)));

    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt
    vec3 B = cross(N, T);

    TBN = mat3(T, B, N);

    gl_Position = camMatrix * worldPos;
}