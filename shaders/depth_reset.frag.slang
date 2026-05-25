struct DepthOut {
    float depth : SV_Depth;
};

[shader("fragment")]
DepthOut main() {
    DepthOut o;
    o.depth = 1.0;
    return o;
}
