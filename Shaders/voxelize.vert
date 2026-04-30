#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
uniform mat4 model;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;
void main() {
    vWorldPos = vec3(model * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vTexCoords = aTexCoords;
    gl_Position = vec4(vWorldPos, 1.0); 
}