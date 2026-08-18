#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_demote_to_helper_invocation : require

layout(location = 0) in vec2 inLocalUV;
layout(location = 1) in vec2 inTexUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inClipRect; // x,y,w,h in framebuffer pixels
layout(location = 4) in flat float inCornerRadius;
layout(location = 5) in flat uint inMode;
layout(location = 6) in flat uint inTextureIndex;
layout(location = 7) in vec2 inQuadSizePx;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D uTextures[256];

const uint MODE_SOLID = 0u;
const uint MODE_TEXTURE = 1u;
const uint MODE_SDF_GLYPH = 2u;

// Signed distance to a rounded box, centered at origin, half-extent `half`.
float roundedBoxSDF(vec2 p, vec2 half_, float radius) {
    vec2 q = abs(p) - half_ + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    // Rectangular clip (scissor-equivalent, since we do a single instanced
    // draw instead of per-window scissor rects like ImGui).
    if (gl_FragCoord.x < inClipRect.x || gl_FragCoord.y < inClipRect.y ||
        gl_FragCoord.x > inClipRect.x + inClipRect.z || gl_FragCoord.y > inClipRect.y + inClipRect.w) {
        demote;
        outColor = vec4(0.0);
        return;
    }

    if (inCornerRadius > 0.0) {
        vec2 p = (inLocalUV - 0.5) * inQuadSizePx;
        float d = roundedBoxSDF(p, inQuadSizePx * 0.5, inCornerRadius);
        float aa = fwidth(d);
        float alphaMask = 1.0 - smoothstep(-aa, aa, d);
        if (alphaMask <= 0.001) { demote; outColor = vec4(0.0); return; }
    }

    vec4 sampled = vec4(1.0);
    if (inMode == MODE_TEXTURE) {
        sampled = texture(uTextures[nonuniformEXT(inTextureIndex)], inTexUV);
    } else if (inMode == MODE_SDF_GLYPH) {
        // Single-channel SDF baked at font-load time (stb_truetype), not
        // full MSDF yet — see UIText.hpp for the scoping note. Distance is
        // stored in the red channel, 0.5 == the glyph edge.
        float dist = texture(uTextures[nonuniformEXT(inTextureIndex)], inTexUV).r;
        float aa = fwidth(dist) * 1.4;
        float alpha = smoothstep(0.5 - aa, 0.5 + aa, dist);
        sampled = vec4(1.0, 1.0, 1.0, alpha);
    }

    vec4 finalColor = sampled * inColor;
    if (inCornerRadius > 0.0) {
        vec2 p = (inLocalUV - 0.5) * inQuadSizePx;
        float d = roundedBoxSDF(p, inQuadSizePx * 0.5, inCornerRadius);
        float aa = fwidth(d);
        finalColor.a *= (1.0 - smoothstep(-aa, aa, d));
    }

    if (finalColor.a <= 0.001) { demote; outColor = vec4(0.0); return; }
    outColor = finalColor;
}
