import common.Bindless;

struct ObjectData {
    float4x4 modelMatrix;
    uint materialID;
    uint indexCount;
    uint vrsRate;
    uint boneOffset;
    uint64_t posBufferAddress;
    uint64_t attrBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t colorBufferAddress;
    uint64_t uv2BufferAddress;
    uint64_t animBufferAddress;
    float4 aabbMin;
    float4 aabbMax;
};

struct SubMeshInfo {
    float3 aabbMin;
    float boundingRadius;
    float3 aabbMax;
    uint lodCount;
    uint indexCounts[4];
    uint firstIndices[4];
    uint materialIndex;
    uint pad1, pad2, pad3;
};

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct CullPushConstants {
    uint objectHeap;
    uint subMeshHeap;
    uint drawCmdHeap;
    uint hiZHeap;
    uint defaultSampler;
    float4x4 viewProj;
    float4 frustumPlanes[6];
    float3 camPos;
    uint objectCount;
    float zNear;
};

[[vk::push_constant]] CullPushConstants pc;

bool isVisible(float3 worldMin, float3 worldMax) {
    for (int i = 0; i < 6; i++) {
        float3 n = pc.frustumPlanes[i].xyz;
        float d = pc.frustumPlanes[i].w;
        float3 positiveVertex = float3(
            n.x >= 0.0 ? worldMax.x : worldMin.x,
            n.y >= 0.0 ? worldMax.y : worldMin.y,
            n.z >= 0.0 ? worldMax.z : worldMin.z
        );
        if (dot(n, positiveVertex) + d < 0.0) return false;
    }
    return true;
}

bool isOccluded(float3 corners[8], Texture2D<float> hiZ, SamplerState sam) {
    float2 minXY = float2(1.0);
    float2 maxXY = float2(0.0);
    float minZ = 1.0;
    for (int i = 0; i < 8; i++) {
        float4 clip = mul(pc.viewProj, float4(corners[i], 1.0));
        if (clip.w <= 0.0) return false;
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        minXY = min(minXY, uv);
        maxXY = max(maxXY, uv);
        minZ = min(minZ, ndc.z);
    }
    if (minXY.x > 1.0 || maxXY.x < 0.0 || minXY.y > 1.0 || maxXY.y < 0.0) return true;
    minXY = clamp(minXY, 0.0, 1.0);
    maxXY = clamp(maxXY, 0.0, 1.0);
    uint2 hiZSize;
    hiZ.GetDimensions(hiZSize.x, hiZSize.y);
    float2 size = (maxXY - minXY) * float2(hiZSize);
    float lod = max(0.0, ceil(log2(max(size.x, size.y))) + 1.0);
    float d1 = hiZ.SampleLevel(sam, float2(minXY.x, minXY.y), lod).x;
    float d2 = hiZ.SampleLevel(sam, float2(maxXY.x, minXY.y), lod).x;
    float d3 = hiZ.SampleLevel(sam, float2(minXY.x, maxXY.y), lod).x;
    float d4 = hiZ.SampleLevel(sam, float2(maxXY.x, maxXY.y), lod).x;
    return minZ > max(max(d1, d2), max(d3, d4));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= pc.objectCount) return;

    StructuredBuffer<ObjectData> objects = DescriptorHandle<StructuredBuffer<ObjectData>>(pc.objectHeap);
    StructuredBuffer<SubMeshInfo> subMeshes = DescriptorHandle<StructuredBuffer<SubMeshInfo>>(pc.subMeshHeap);
    RWStructuredBuffer<DrawCommand> drawCommands = DescriptorHandle<RWStructuredBuffer<DrawCommand>>(pc.drawCmdHeap);
    Texture2D<float> hiZ = heapTex2D_r(pc.hiZHeap);
    SamplerState sam = heapSampler(pc.defaultSampler);

    SubMeshInfo sub = subMeshes[idx];
    ObjectData obj = objects[idx];

    float3 corners[8];
    corners[0] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMin.y, sub.aabbMin.z, 1.0)).xyz;
    corners[1] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMin.y, sub.aabbMin.z, 1.0)).xyz;
    corners[2] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMax.y, sub.aabbMin.z, 1.0)).xyz;
    corners[3] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMax.y, sub.aabbMin.z, 1.0)).xyz;
    corners[4] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMin.y, sub.aabbMax.z, 1.0)).xyz;
    corners[5] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMin.y, sub.aabbMax.z, 1.0)).xyz;
    corners[6] = mul(obj.modelMatrix, float4(sub.aabbMin.x, sub.aabbMax.y, sub.aabbMax.z, 1.0)).xyz;
    corners[7] = mul(obj.modelMatrix, float4(sub.aabbMax.x, sub.aabbMax.y, sub.aabbMax.z, 1.0)).xyz;

    float3 worldMin = corners[0];
    float3 worldMax = corners[0];
    for (int i = 1; i < 8; i++) {
        worldMin = min(worldMin, corners[i]);
        worldMax = max(worldMax, corners[i]);
    }

    bool visible = isVisible(worldMin, worldMax);
    if (visible) visible = !isOccluded(corners, hiZ, sam);

    uint selectedLod = 0;
    if (visible) {
        float3 center = (worldMin + worldMax) * 0.5;
        float dist = distance(pc.camPos, center);
        if (dist > 20.0) selectedLod = 1;
        if (dist > 50.0) selectedLod = 2;
        if (dist > 100.0) selectedLod = 3;
        selectedLod = min(selectedLod, sub.lodCount - 1);
    }

    drawCommands[idx].indexCount = visible ? sub.indexCounts[selectedLod] : 0;
    drawCommands[idx].instanceCount = visible ? 1 : 0;
    drawCommands[idx].firstIndex = sub.firstIndices[selectedLod];
    drawCommands[idx].vertexOffset = 0;
    drawCommands[idx].firstInstance = idx;
}
