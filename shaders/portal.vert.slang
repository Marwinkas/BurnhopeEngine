import common.Bindless;
import common.GlobalUbo;

struct PortalVertPC {
    float4x4 modelMatrix;
    uint portalID;
    uint globalUbo;
};

[[vk::push_constant]] PortalVertPC pc;

struct PortalVSOut {
    float4 position : SV_Position;
};

[shader("vertex")]
PortalVSOut main(float4 aPosAABB : POSITION0)
{
    PortalVSOut o;
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    float3 aabbMin = float3(-1.0, -1.0, 0.0);
    float3 aabbMax = float3(1.0, 1.0, 0.0);
    float3 extent = aabbMax - aabbMin;
    float3 localPos = aabbMin + aPosAABB.xyz * extent;
    o.position = mul(ubo.projection, mul(ubo.view, mul(pc.modelMatrix, float4(localPos, 1.0))));
    return o;
}
