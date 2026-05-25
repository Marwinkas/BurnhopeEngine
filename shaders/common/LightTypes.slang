import common.GlobalUbo;

export struct PointLightData {
    float4 posType;
    float4 colorInt;
    float4 dirRadius;
    float4 shadowParams;
    float4x4 lightSpaceMatrix;
};

// Must match burnhope::LightUBOData in Render/shadow.hpp (int + 12-byte pad before lights[100])
export struct LightUBOBlock {
    int activeLightsCount;
    int _pad0;
    int _pad1;
    int _pad2;
    PointLightData lights[100];
};

export struct LightGrid {
    uint offset;
    uint count;
};

export struct PointFaceMatrices {
    float4x4 faces[6];
};

export struct DecalDataGPU {
    float4x4 invModelMatrix;
    float4 params;
};

export struct DecalBlock {
    int decalCount;
    int pad[3];
    DecalDataGPU decals[256];
};

export struct PortalUBO {
    GlobalUbo data[10];
};

export struct Light {
    int type;
    float3 position;
    float3 direction;
    float3 color;
    float intensity;
    float radius;
    float innerCone;
    float outerCone;
    int castShadows;
    int shadowSlot;
    float4x4 lightSpaceMatrix;
    int lightIndex;
};
