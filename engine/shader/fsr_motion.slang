// Камера без джиттера в векторе. FSR ждёт смещение в UV, не в пикселях.

[[vk::binding(0, 0)]]
cbuffer MotionCb {
    float4x4 currInv;
    float4x4 currUnjittered;
    float4x4 prevUnjittered;
    uint2 size;
    float2 pad;
};

[[vk::binding(1, 0)]]
Texture2D<float> gDepth;

[[vk::binding(2, 0)]]
RWTexture2D<float2> gMotion;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= size.x || tid.y >= size.y) {
        return;
    }
    const float2 uv = (float2(tid.xy) + 0.5) / float2(size);
    const float depth = gDepth[tid.xy];
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    const float4 hw = mul(currInv, float4(ndc, depth, 1.0));
    const float3 world = hw.xyz / max(hw.w, 1e-6);
    const float4 curr = mul(currUnjittered, float4(world, 1.0));
    const float4 prev = mul(prevUnjittered, float4(world, 1.0));
    const float2 currUv = curr.xy / curr.w * 0.5 + 0.5;
    const float2 prevUv = prev.xy / prev.w * 0.5 + 0.5;
    gMotion[tid.xy] = prevUv - currUv;
}
