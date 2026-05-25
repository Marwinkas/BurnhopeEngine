import common.GlobalUbo;
import common.SceneGpu;
import common.SlangBuiltins;
import common.GfxHeapPC;
import common.SceneAddresses;
import common.SceneUboHeap;

[[vk::push_constant]] GfxHeapPC pc;

struct VSOut {
    float3 outCrntPos : TEXCOORD0;
    float2 outTexCoord : TEXCOORD1;
    float3 outTBN0 : TEXCOORD2;
    float3 outTBN1 : TEXCOORD3;
    float3 outTBN2 : TEXCOORD4;
    nointerpolation uint outMatID : TEXCOORD5;
    float4 outColor : TEXCOORD6;
    float2 outUV2 : TEXCOORD7;
    float outThickness : TEXCOORD8;
    float4 pos : SV_Position;
};

[shader("vertex")]
VSOut main(
    uint SV_InstanceID : SV_InstanceID,
    uint SV_VertexID : SV_VertexID,
    float4 aPosAABB : POSITION0,
    float2 aTex : TEXCOORD0,
    float4 aQTangent : TEXCOORD1)
{
    VSOut o;
    SceneAddresses scene = loadSceneAddresses(pc.sceneAddressesBda);
    GlobalUbo ubo = loadGlobalUboScene(scene);
    ObjectData obj = loadObjectBda(scene.objectStorageBda, SV_InstanceID);
    float4x4 modelMatrix = obj.modelMatrix;
    o.outMatID = obj.materialID;

    float3 extent = obj.aabbMax.xyz - obj.aabbMin.xyz;
    float3 localPos = obj.aabbMin.xyz + aPosAABB.xyz * extent;

    uint pad = uint(aPosAABB.w * 65535.0 + 0.5);
    float thickness = float((pad >> 8) & 0xFF) / 255.0;
    float windW = float(pad & 0xFF) / 255.0;
    o.outThickness = thickness;

    if (windW > 0.0) {
        float time = ubo.ppMotionBlur.z * 1.5;
        float3 sway = float3(sin(time + localPos.x), 0.0, cos(time + localPos.z)) * windW * 0.15;
        localPos += sway;
    }

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
        if (w0 > 0.0) skinMat += loadBoneMatrixBda(scene.boneStorageBda, obj.boneOffset + ((bInd >> 0) & 0xFF)) * w0;
        if (w1 > 0.0) skinMat += loadBoneMatrixBda(scene.boneStorageBda, obj.boneOffset + ((bInd >> 8) & 0xFF)) * w1;
        if (w2 > 0.0) skinMat += loadBoneMatrixBda(scene.boneStorageBda, obj.boneOffset + ((bInd >> 16) & 0xFF)) * w2;
        if (w3 > 0.0) skinMat += loadBoneMatrixBda(scene.boneStorageBda, obj.boneOffset + ((bInd >> 24) & 0xFF)) * w3;
    }

    float4 worldPos = obj.boneOffset != 0xFFFFFFFF
        ? mul(modelMatrix, mul(skinMat, float4(localPos, 1.0)))
        : mul(modelMatrix, float4(localPos, 1.0));
    o.outCrntPos = worldPos.xyz;
    o.outTexCoord = aTex;

    float4 vColor = float4(1.0);
    if (obj.colorBufferAddress != 0) {
        ColorBuffer* cb = (ColorBuffer*)obj.colorBufferAddress;
        vColor = unpackUnorm4x8(cb->c[SV_VertexID]);
    }
    o.outColor = vColor;

    float2 vUV2 = float2(0.0);
    if (obj.uv2BufferAddress != 0) {
        UV2Buffer* u2b = (UV2Buffer*)obj.uv2BufferAddress;
        vUV2 = unpackHalf2x16(u2b->u[SV_VertexID]);
    }
    o.outUV2 = vUV2;

    float3x3 normalMatrix = transpose(inverse3x3(float3x3(modelMatrix)));
    float qx = aQTangent.x;
    float qy = aQTangent.y;
    float qz = aQTangent.z;
    float qw = sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz));
    float3 T = float3(
        1.0 - 2.0 * qy * qy - 2.0 * qz * qz,
        2.0 * qx * qy + 2.0 * qz * qw,
        2.0 * qx * qz - 2.0 * qy * qw
    );
    float3 N = float3(
        2.0 * qx * qz + 2.0 * qy * qw,
        2.0 * qy * qz - 2.0 * qx * qw,
        1.0 - 2.0 * qx * qx - 2.0 * qy * qy
    );
    float signT = aQTangent.w < 0.0 ? -1.0 : 1.0;
    float3 B = cross(N, T) * signT;

    if (obj.boneOffset != 0xFFFFFFFF) {
        float3x3 skinMat3 = float3x3(skinMat);
        normalMatrix = transpose(inverse3x3(mul(float3x3(modelMatrix), skinMat3)));
    }

    T = normalize(mul(normalMatrix, T));
    B = normalize(mul(normalMatrix, B));
    N = normalize(mul(normalMatrix, N));
    o.outTBN0 = T;
    o.outTBN1 = B;
    o.outTBN2 = N;

    if (ubo.ppCausticsBreath.y > 0.5) {
        float time = ubo.ppMotionBlur.z * ubo.ppCausticsBreath.w;
        float amp = ubo.ppCausticsBreath.z;
        float wave = sin(worldPos.x * 2.0 + time) * cos(worldPos.z * 2.0 + time);
        worldPos.xyz += N * wave * amp;
    }

    o.pos = mul(ubo.projection, mul(ubo.view, worldPos));

    if (ubo.ppCanvasInk.y > 0.5) {
        float curve = ubo.ppCanvasInk.z;
        float distSq = dot(o.pos.xz, o.pos.xz);
        o.pos.y -= distSq * curve;
    }

    if (ubo.ppWarp.w > 0.5) {
        float time = ubo.ppMotionBlur.z * ubo.ppWarp2.y;
        float scale = ubo.ppWarp2.z;
        float noise = frac(sin(dot(worldPos.xyz * scale, float3(12.9898, 78.233, 45.164)) + time) * 43758.5453);
        worldPos.xyz += N * (noise - 0.5) * ubo.ppWarp2.x;
        o.pos = mul(ubo.projection, mul(ubo.view, worldPos));
    }

    if (ubo.ppRetroParams2.y > 0.0) {
        float res = ubo.ppRetroParams2.y;
        o.pos.xyz = o.pos.xyz / o.pos.w;
        o.pos.xy = floor(o.pos.xy * res) / res;
        o.pos.xyz *= o.pos.w;
    }

    return o;
}
