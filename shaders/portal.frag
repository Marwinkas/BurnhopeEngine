#version 450

layout(location = 0) out vec4 outNormalRoughness;
layout(location = 1) out vec4 outAlbedoMetallic;
layout(location = 2) out vec4 outExtra;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out uint o_PortalID;

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    uint portalID;
} pc;

void main() {
    outNormalRoughness = vec4(0.0);
    outAlbedoMetallic = vec4(0.0);
    outExtra = vec4(0.0);
    outEmissive = vec4(0.0);

    o_PortalID = pc.portalID;
}