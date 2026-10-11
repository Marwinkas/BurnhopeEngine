uint atlasMipY(uint mip, uint baseHeight) {
    uint y = 0;
    uint h = baseHeight;
    for (uint i = 0; i < mip; ++i) {
        y += h;
        h = max(h >> 1, 1u);
    }
    return y;
}

float sampleHiZ(Texture2D<float> hizTex, float2 uv, uint mip, uint2 baseExtent) {
    const uint2 ext = max(baseExtent >> mip, uint2(1, 1));
    const uint2 p = uint2(clamp(uv, 0.0, 0.999) * float2(ext));
    const int3 at = int3(int(p.x), int(atlasMipY(mip, baseExtent.y) + p.y), 0);
    return hizTex.Load(at);
}
