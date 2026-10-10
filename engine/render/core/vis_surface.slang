// Экранные барицентрики треугольника из visbuffer. Дескрипторов нет:
// вызывающий уже объявил gVerts, gIdx, gMeshlets, gInstances и включил camera.slang.

struct VisTri {
    uint instanceId;
    uint triId;
    uint i0;
    uint i1;
    uint i2;
    GpuInstance inst;
    float3 w0;
    float3 w1;
    float3 w2;
    float3 faceN;
    float faceA;
    float3 n0;
    float3 n1;
    float3 n2;
    float bu;
    float bv;
    float bw;
    bool denomOk;
    bool inRange;
    float2 uv;
    float3 smoothN;
};

VisTri loadVisTri(uint instanceId, uint tri, float3 worldP) {
    VisTri t;
    t.instanceId = instanceId;
    t.triId = tri;
    t.inst = gInstances[instanceId];
    const MeshletRef meshlet = gMeshlets[t.inst.meshId];
    const uint at = meshlet.indexOffset + tri * 3u;
    t.i0 = meshlet.vertexOffset + gIdx[at];
    t.i1 = meshlet.vertexOffset + gIdx[at + 1u];
    t.i2 = meshlet.vertexOffset + gIdx[at + 2u];
    const float4 h0 = float4(gVerts[t.i0].pos, 1.0);
    const float4 h1 = float4(gVerts[t.i1].pos, 1.0);
    const float4 h2 = float4(gVerts[t.i2].pos, 1.0);
    t.w0 = float3(dot(t.inst.world0, h0), dot(t.inst.world1, h0), dot(t.inst.world2, h0));
    t.w1 = float3(dot(t.inst.world0, h1), dot(t.inst.world1, h1), dot(t.inst.world2, h1));
    t.w2 = float3(dot(t.inst.world0, h2), dot(t.inst.world1, h2), dot(t.inst.world2, h2));
    const float3 raw = cross(t.w1 - t.w0, t.w2 - t.w0);
    t.faceA = dot(raw, raw);
    t.faceN = t.faceA > 1e-12 ? normalize(raw) : float3(0.0, 1.0, 0.0);
    t.n0 = float3(dot(t.inst.world0.xyz, gVerts[t.i0].nrm), dot(t.inst.world1.xyz, gVerts[t.i0].nrm), dot(t.inst.world2.xyz, gVerts[t.i0].nrm));
    t.n1 = float3(dot(t.inst.world0.xyz, gVerts[t.i1].nrm), dot(t.inst.world1.xyz, gVerts[t.i1].nrm), dot(t.inst.world2.xyz, gVerts[t.i1].nrm));
    t.n2 = float3(dot(t.inst.world0.xyz, gVerts[t.i2].nrm), dot(t.inst.world1.xyz, gVerts[t.i2].nrm), dot(t.inst.world2.xyz, gVerts[t.i2].nrm));
    const float3 e1 = t.w1 - t.w0;
    const float3 e2 = t.w2 - t.w0;
    const float3 ep = worldP - t.w0;
    const float d00 = dot(e1, e1);
    const float d01 = dot(e1, e2);
    const float d11 = dot(e2, e2);
    const float denom = d00 * d11 - d01 * d01;
    t.denomOk = abs(denom) > 1e-8;
    t.bv = t.denomOk ? (d11 * dot(ep, e1) - d01 * dot(ep, e2)) / denom : 0.0;
    t.bw = t.denomOk ? (d00 * dot(ep, e2) - d01 * dot(ep, e1)) / denom : 0.0;
    t.bu = 1.0 - t.bv - t.bw;
    t.inRange = t.bu >= -0.2 && t.bv >= -0.2 && t.bw >= -0.2;
    t.uv = float2(
        gVerts[t.i0].u * t.bu + gVerts[t.i1].u * t.bv + gVerts[t.i2].u * t.bw,
        gVerts[t.i0].v * t.bu + gVerts[t.i1].v * t.bv + gVerts[t.i2].v * t.bw);
    t.smoothN = t.faceN;
    if (t.denomOk) {
        float3 Ns = t.n0 * t.bu + t.n1 * t.bv + t.n2 * t.bw;
        if (dot(Ns, Ns) > 1e-6) {
            Ns = normalize(Ns);
            if (dot(Ns, t.faceN) < 0.0) {
                Ns = -Ns;
            }
            t.smoothN = Ns;
        }
    }
    return t;
}
