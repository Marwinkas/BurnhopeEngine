struct PortalFragPC {
    float4x4 modelMatrix;
    uint portalID;
};

[[vk::push_constant]] PortalFragPC pc;

struct FragOut {
    float4 outNormalRoughness : SV_Target0;
    float4 outAlbedoMetallic : SV_Target1;
    float4 outExtra : SV_Target2;
    float4 outEmissive : SV_Target3;
    uint o_PortalID : SV_Target4;
};

[shader("fragment")]
FragOut main()
{
    FragOut o;
    o.outNormalRoughness = float4(0.0);
    o.outAlbedoMetallic = float4(0.0);
    o.outExtra = float4(0.0);
    o.outEmissive = float4(0.0);
    o.o_PortalID = pc.portalID;
    return o;
}
