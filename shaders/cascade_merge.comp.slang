import common.Bindless;

struct CascadeMergePC {
    uint currentCascade;
    uint nextCascade;
    float4 probeGridMin;
    float4 probeGridMax;
    int4 probeCount;
    float4 params;
};

[[vk::push_constant]] CascadeMergePC rc;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 pixel = int2(id.xy);
    RWTexture2D<float4> curTex = heapRWTex2D(rc.currentCascade);
    Texture2D<float4> nextTex = heapTex2D(rc.nextCascade);
    uint2 sz;
    curTex.GetDimensions(sz.x, sz.y);
    if (pixel.x >= int(sz.x) || pixel.y >= int(sz.y)) return;

    float4 near = curTex[pixel];
    if (near.a >= 0.99) return;

    int octaSize = int(rc.params.w);
    int3 pCount = rc.probeCount.xyz;

    int probeIdxX = pixel.x / octaSize;
    int probeIdxYZ = pixel.y / octaSize;
    int probeIdxY = probeIdxYZ % pCount.y;
    int probeIdxZ = probeIdxYZ / pCount.y;
    int2 localPixel = int2(pixel.x % octaSize, pixel.y % octaSize);

    float3 probePosF = float3(
        (float(probeIdxX) + 0.5) / float(pCount.x),
        (float(probeIdxY) + 0.5) / float(pCount.y),
        (float(probeIdxZ) + 0.5) / float(pCount.z)
    );

    int3 pCountNext = max(pCount >> 1, int3(1));
    float3 nextProbeF = clamp(probePosF * float3(pCountNext) - 0.5, float3(0.0), float3(pCountNext - int3(1)));
    int3 n000 = int3(floor(nextProbeF));
    int3 n111 = min(n000 + 1, pCountNext - int3(1));
    float3 t = frac(nextProbeF);

    float4 far = float4(0.0);
    for (int ix = 0; ix < 2; ix++)
    for (int iy = 0; iy < 2; iy++)
    for (int iz = 0; iz < 2; iz++) {
        int3 idx = int3(
            ix == 0 ? n000.x : n111.x,
            iy == 0 ? n000.y : n111.y,
            iz == 0 ? n000.z : n111.z
        );
        float w = (ix == 0 ? (1.0 - t.x) : t.x) * (iy == 0 ? (1.0 - t.y) : t.y) * (iz == 0 ? (1.0 - t.z) : t.z);
        int nextBaseX = idx.x * octaSize;
        int nextBaseY = (idx.z * pCountNext.y + idx.y) * octaSize;
        int2 nextPixel = int2(nextBaseX, nextBaseY) + localPixel;
        far += nextTex.Load(int3(nextPixel, 0)) * w;
    }

    float missFactor = 1.0 - near.a;
    float3 merged = near.rgb + far.rgb * missFactor;
    float outAlpha = max(near.a, far.a);
    curTex[pixel] = float4(merged, outAlpha);
}
