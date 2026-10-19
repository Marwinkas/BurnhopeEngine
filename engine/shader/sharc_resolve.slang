#include "sharc_params.slang"

[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] RWStructuredBuffer<uint4> gAcc;
[[vk::binding(72, 0)]] RWStructuredBuffer<uint4> gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;

#include "sharc_cache.slang"

[shader("compute")]
[numthreads(256, 1, 1)]
void sharcResolve(uint3 dtid : SV_DispatchThreadID) {
    sharcResolveEntry(dtid.x);
}
