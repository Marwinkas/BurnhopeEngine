// GLSL-compat helpers for ported shaders.

export float2 unpackHalf2x16(uint data) {
    return float2(f16tof32(uint16_t(data & 0xFFFFu)), f16tof32(uint16_t(data >> 16)));
}

export float4 unpackUnorm4x8(uint data) {
    return float4(
        float(data & 0xFFu) / 255.0,
        float((data >> 8) & 0xFFu) / 255.0,
        float((data >> 16) & 0xFFu) / 255.0,
        float((data >> 24) & 0xFFu) / 255.0
    );
}
