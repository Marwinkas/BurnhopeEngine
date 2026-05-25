/** std430 — matches burnhope::MaterialData (112 bytes stride). */
export struct MaterialData {
    int albedoAlphaIdx;
    int normalIdx;
    int ormxIdx;
    int emissiveIdx;
    int useTriplanar;
    int isTransparent;
    int repeatTexture;
    int pad1;
    float2 uvScale;
    float triplanarScale;
    float emissiveIntensity;
    float4 albedoColor;
    float4 emissiveColor;
    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;
    float aoStrength;
    float pad2, pad3, pad4;
};

static const uint kMaterialDataStride = 112u;

export MaterialData loadMaterialBda(uint64_t matBda, uint matIdx) {
    return *((MaterialData*)(matBda + uint64_t(matIdx) * kMaterialDataStride));
}
