#include "restir_rab.slang"
#include "Rtxdi/LightSampling/RISBuffer.hlsli"
#include "Rtxdi/LightSampling/PresamplingFunctions.hlsli"

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint2 id : SV_DispatchThreadID) {
    if (id.x >= gRestir.localRis.tileSize || id.y >= gRestir.localRis.tileCount) {
        return;
    }
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(id, gRestir.runtime.frameIndex, 0u);
    RTXDI_PresampleLocalLights(
        rng,
        gPdf,
        uint2(gRestir.pdfWidth, gRestir.pdfHeight),
        id.y,
        id.x,
        gRestir.lights.localLightBufferRegion,
        gRestir.localRis);
}
