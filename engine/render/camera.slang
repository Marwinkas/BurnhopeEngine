// Screen to world. Included after frame.slang. Passes do not each keep a copy.

float3 reconstructWorld(float2 uv, float depth) {
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 world = mul(gFrame.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

float3 reconstructWorld(uint2 px, float depth) {
    return reconstructWorld((float2(px) + 0.5) * gFrame.invExtent, depth);
}

float4 worldToClip(float3 world) {
    return mul(gFrame.viewProj, float4(world, 1.0));
}

float4 worldToSun(uint cascade, float3 world) {
    return mul(gFrame.sunCascade[cascade], float4(world, 1.0));
}
