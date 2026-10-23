// FrameView — binding 0, 384. FrameSun — binding 49, 1920. Ползунки — binding 50–57.
// Размеры совпадают с FrameUniforms.hpp. Массив скаляров здесь запрещён.

struct FrameView {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 cameraPos;
    float2 invExtent;
    float2 padInv;
    uint2 extent;
    uint2 padExt;
    uint debugMode;
    uint disableSSR;
    uint disableSSRC;
    uint disableHiZ;
    uint disableConeCull;
    uint disableLod;
    uint disableFrustum;
    uint disableSubpixel;
    uint disableShadow;
    uint debugPad0;
    uint debugPad1;
    uint debugPad2;
    uint frameParity;
    uint disableBackface;
    uint disableScreen;
    uint disableBehind;
    float4x4 freezeVp;
    float4 viewTail0;
    float4 viewTail1;
    float4 viewTail2;
    float4 viewTail3;
    float4 viewPad;
};

struct FrameSun {
    float4 sunDirIntensity;
    float4 sunColor;
    float4 pointPos[3];
    float4 pointColor[3];
    float4x4 sunCascade[3];
    float4 sunSplit;
    float4x4 pointFace[3][6];
    float4x4 cubeFace[6];
    float4 cubeMin;
    float4 cubeMax;
    float4 pad;
};

struct PassFlags {
    int forceLod;
    uint passMask;
    uint lightProfile;
    uint p1; uint p2; uint p3; uint p4; uint p5; uint p6;
    uint p7; uint p8; uint p9; uint p10; uint p11; uint p12; uint p13;
};
struct GtaoParams {
    float radius;
    float steps;
    float slices;
    float power;
    float falloff;
    float intensity;
    float a0; float a1; float a2; float a3; float a4;
    float a5; float a6; float a7; float a8; float a9;
};
struct SsrParams {
    float cutoff;
    float steps;
    float stride;
    float thick;
    float maxDist;
    float bias;
    float b0; float b1; float b2; float b3; float b4;
    float b5; float b6; float b7; float b8; float b9;
};
struct SsrcParams {
    float base;
    float thick;
    float cap;
    float steps;
    float strength;
    float c1; float c2; float c3; float c4; float c5;
    float c6; float c7; float c8; float c9; float c10; float c11;
};
struct RcParams {
    float rays;
    float spacing;
    float intensity;
    float maxDist;
    float cap;
    float sunScale;
    float bounce;
    float fogFalloff;
    float fogR;
    float fogG;
    float fogB;
    float fogLum;
    float fogStart;
    float fogOpacity;
    float fogInStart;
    float fogInSize;
};
struct ShadeParams {
    float sscsReach;
    float sscsThick;
    float sscsSteps;
    float sscsPx;
    float fogDensity;
    float fogHeight;
    float fogScatter;
    float sscsAmt;
    float specOcc;
    float toksvig;
    float pom;
    float ggx;
    float gtaoAmt;
    float lamp;
    float pad0;
    float pad1;
};
struct GradeBlock {
    float4 prev0;
    float4 prev1;
    float4 prev2;
    float4 prev3;
    float4 mixR;
    float4 mixG;
    float4 mixB;
    float4 shadows;
    float4 midtones;
    float4 highlights;
    float4 slope;
    float4 offset;
    float4 power;
    float4 ranges;
    float4 generic;
    float4 curves;
};

struct BloomParams {
    float threshold;
    uint tonemapper;
    uint padU;
    float grain;
    float bloomMix;
    float vignette;
    float ca;
    float cas;
    float f4; float f5;
    float f6; float f7; float f8; float f9; float f10; float f11;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameView> gFrame;
[[vk::binding(49, 0)]] ConstantBuffer<FrameSun> gSun;
[[vk::binding(50, 0)]] ConstantBuffer<PassFlags> gPass;
[[vk::binding(51, 0)]] ConstantBuffer<GtaoParams> gGtao;
[[vk::binding(52, 0)]] ConstantBuffer<SsrParams> gSsr;
[[vk::binding(53, 0)]] ConstantBuffer<SsrcParams> gSsrc;
[[vk::binding(54, 0)]] ConstantBuffer<RcParams> gRcParams;
[[vk::binding(55, 0)]] ConstantBuffer<ShadeParams> gShade;
[[vk::binding(57, 0)]] ConstantBuffer<BloomParams> gTone;

struct Vertex {
    float3 pos;
    float u;
    float3 nrm;
    float v;
};

struct MaterialGpu {
    float4 baseColor;
    float roughness;
    float metallic;
    float emissive;
    uint flags;
    uint albedo;
    uint normalTex;
    uint orm;
    uint emissiveTex;
    uint pad0;
    uint pad1;
    uint pad2;
    uint pad3;
    float reflectance;
    float aoStrength;
    float normalScale;
    float clearcoat;
    float clearcoatRough;
    float alphaCutoff;
    float uvScaleX;
    float uvScaleY;
    float uvOffX;
    float uvOffY;
    float uvRotation;
    float worldGrid;
    float triSharp;
    float worldOffX;
    float worldOffY;
    float worldOffZ;
    float emitR;
    float emitG;
    float emitB;
    float emissiveGain;
    uint aoTex;
    uint tail0;
    uint tail1;
    uint tail2;
    uint tail3;
    uint tail4;
    uint tail5;
    uint tail6;
    uint tail7;
    uint tail8;
    uint tail9;
    uint tail10;
};

struct MeshletRef {
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint triangleCount;
    float4 cone;
    float4 groupSphere;
};

struct GpuInstance {
    float4 world0;
    float4 world1;
    float4 world2;
    float4 sphere;
    uint materialId;
    uint meshId;
    uint2 pad;
};
