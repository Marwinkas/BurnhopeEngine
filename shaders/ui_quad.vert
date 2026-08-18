#version 460
#extension GL_EXT_scalar_block_layout : require

// One instance per rectangle (panel/button/glyph). No vertex buffers: the
// unit quad is generated from gl_VertexIndex and instance data pulled from
// a bindless SSBO written by the CPU each frame (UIRenderer::UIInstance).

struct UIInstance {
    vec2 position;
    vec2 size;
    vec4 uvRect;
    vec4 color;
    vec4 clipRect;
    float cornerRadius;
    uint mode;
    uint textureIndex;
    float _pad;
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    UIInstance instances[];
};

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
} pc;

layout(location = 0) out vec2 outLocalUV;   // 0..1 across the quad
layout(location = 1) out vec2 outTexUV;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec4 outClipRect;
layout(location = 4) out flat float outCornerRadius;
layout(location = 5) out flat uint outMode;
layout(location = 6) out flat uint outTextureIndex;
layout(location = 7) out vec2 outQuadSizePx;

const vec2 kUnitQuad[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    UIInstance inst = instances[gl_InstanceIndex];
    vec2 unit = kUnitQuad[gl_VertexIndex];

    vec2 pixelPos = inst.position + unit * inst.size;
    vec2 ndc = (pixelPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outLocalUV = unit;
    outTexUV = inst.uvRect.xy + unit * inst.uvRect.zw;
    outColor = inst.color;
    outClipRect = inst.clipRect;
    outCornerRadius = inst.cornerRadius;
    outMode = inst.mode;
    outTextureIndex = inst.textureIndex;
    outQuadSizePx = inst.size;
}
