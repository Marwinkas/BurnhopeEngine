// Предыдущая матрица кадра. GradeBlock заполняется до shade и не переписывается до конца кадра.
// Конструктор совпадает с sssrPrevVp: столбцы лежат в prev0..prev3.

[[vk::binding(60, 0)]] ConstantBuffer<GradeBlock> gGrade;

float4x4 nrdPrevVp() {
    return float4x4(
        float4(gGrade.prev0.x, gGrade.prev1.x, gGrade.prev2.x, gGrade.prev3.x),
        float4(gGrade.prev0.y, gGrade.prev1.y, gGrade.prev2.y, gGrade.prev3.y),
        float4(gGrade.prev0.z, gGrade.prev1.z, gGrade.prev2.z, gGrade.prev3.z),
        float4(gGrade.prev0.w, gGrade.prev1.w, gGrade.prev2.w, gGrade.prev3.w));
}

float2 nrdPrevUv(float3 world) {
    const float4 prevClip = mul(nrdPrevVp(), float4(world, 1.0));
    return prevClip.xy / max(prevClip.w, 1e-5) * 0.5 + 0.5;
}

bool nrdInside(float2 uv) {
    return uv.x > 0.0 && uv.y > 0.0 && uv.x < 1.0 && uv.y < 1.0;
}

int2 nrdUvPx(float2 uv) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    return clamp(int2(uv * float2(gFrame.extent.xy)), int2(0, 0), ext);
}

float3 nrdNormal(RWTexture2D<float4> surf, Texture2D<float> depthTex, int2 p) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), ext);
    float3 n = octDecode(surf[c].xy);
    const float depth = depthTex.Load(int3(c, 0));
    if (dot(n, n) < 0.5 || depth <= 0.0) {
        n = reconstructNormalFromDepth(depthTex, uint2(c), reconstructWorld(uint2(c), max(depth, 1e-5)));
    }
    return normalize(n);
}

float nrdPlane(float3 n0, float z0, float3 n1, float z1) {
    const float nd = saturate(dot(n0, n1));
    const float dz = abs(z0 - z1) / max(z0, 1e-3);
    if (nd < 0.9 || dz > 0.03) {
        return 0.0;
    }
    return nd * exp(-dz * 40.0);
}
