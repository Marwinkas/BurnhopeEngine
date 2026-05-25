import common.Bindless;
import common.SceneGpu;
import common.SlangBuiltins;

struct ShadowVertPC {
    float4x4 lightSpaceMatrix;
    uint objectStorage;
    uint boneStorage;
};

[[vk::push_constant]] ShadowVertPC pc;

struct VSOut {
    float4 pos : SV_Position;
};

[shader("vertex")]
VSOut main(
    uint SV_InstanceID : SV_InstanceID,
    uint SV_VertexID : SV_VertexID,
    float4 aPosAABB : POSITION0)
{
    VSOut o;
    StructuredBuffer<ObjectData> objectBuffer = heapStructured<ObjectData>(pc.objectStorage);
    StructuredBuffer<float4x4> boneMatrices = heapStructured<float4x4>(pc.boneStorage);

    ObjectData obj = objectBuffer[SV_InstanceID];
    float3 extent = obj.aabbMax.xyz - obj.aabbMin.xyz;
    float3 localPos = obj.aabbMin.xyz + aPosAABB.xyz * extent;

    float4x4 skinMat = float4x4(
        float4(1, 0, 0, 0),
        float4(0, 1, 0, 0),
        float4(0, 0, 1, 0),
        float4(0, 0, 0, 1)
    );
    if (obj.boneOffset != 0xFFFFFFFF && obj.animBufferAddress != 0) {
        AnimBuffer* anBuf = (AnimBuffer*)obj.animBufferAddress;
        PackedVertexAnim vtxAn = anBuf->a[SV_VertexID];
        uint bInd = vtxAn.boneIndices;
        uint bWgh = vtxAn.boneWeights;

        float w0 = float((bWgh >> 0) & 0xFF) / 255.0;
        float w1 = float((bWgh >> 8) & 0xFF) / 255.0;
        float w2 = float((bWgh >> 16) & 0xFF) / 255.0;
        float w3 = float((bWgh >> 24) & 0xFF) / 255.0;
        skinMat = float4x4(0.0);
        if (w0 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 0) & 0xFF)] * w0;
        if (w1 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 8) & 0xFF)] * w1;
        if (w2 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 16) & 0xFF)] * w2;
        if (w3 > 0.0) skinMat += boneMatrices[obj.boneOffset + ((bInd >> 24) & 0xFF)] * w3;
    }

    float4 worldPos = obj.boneOffset != 0xFFFFFFFF
        ? mul(obj.modelMatrix, mul(skinMat, float4(localPos, 1.0)))
        : mul(obj.modelMatrix, float4(localPos, 1.0));

    o.pos = mul(pc.lightSpaceMatrix, worldPos);
    return o;
}
