#version 450

layout(location = 0) out vec4 outNormalRoughness;
layout(location = 1) out vec4 outAlbedoMetallic;
layout(location = 2) out vec4 outExtra;

void main() {
    outNormalRoughness = vec4(0.0);
    outAlbedoMetallic = vec4(0.0);
    outExtra = vec4(0.0);
}