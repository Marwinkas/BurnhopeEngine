import common.Bindless;

struct HiZHeapPC {
    uint depthIn;
    uint hiZOut;
    float2 invSize;
    uint defaultSampler;
};

[[vk::push_constant]] HiZHeapPC pc;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 pos = int2(id.xy);
    Texture2D<float> inDepth = heapTex2D_r(pc.depthIn);
    RWTexture2D<float4> outDepth = heapRWTex2D(pc.hiZOut);
    SamplerState sam = heapSampler(pc.defaultSampler);
    uint2 outSize;
    outDepth.GetDimensions(outSize.x, outSize.y);
    if (pos.x >= int(outSize.x) || pos.y >= int(outSize.y)) return;
    float2 uv = (float2(pos) + 0.5) / float2(outSize);
    float d1 = inDepth.SampleLevel(sam, uv + float2(-0.25, -0.25) * pc.invSize, 0.0).x;
    float d2 = inDepth.SampleLevel(sam, uv + float2(0.25, -0.25) * pc.invSize, 0.0).x;
    float d3 = inDepth.SampleLevel(sam, uv + float2(-0.25, 0.25) * pc.invSize, 0.0).x;
    float d4 = inDepth.SampleLevel(sam, uv + float2(0.25, 0.25) * pc.invSize, 0.0).x;
    float maxD = max(max(d1, d2), max(d3, d4));
    outDepth[pos] = float4(maxD, 0, 0, 0);
}
