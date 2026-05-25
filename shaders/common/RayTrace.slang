// Vulkan ray query (Slang RayQuery API, not GLSL rayQueryEXT).
import common.Bindless;

export struct RtHitInfo {
    float t;
    uint instanceId;
    uint instanceCustomIndex;
    uint geometryIndex;
    uint primitiveIndex;
    float2 barycentrics;
};

export RaytracingAccelerationStructure heapTlas(uint index) {
    return DescriptorHandle<RaytracingAccelerationStructure>(heapIndex(index));
}

export bool rtTraceOpaque(
    RaytracingAccelerationStructure tlas,
    float3 origin,
    float3 dir,
    float tMax,
    out RtHitInfo hit)
{
    RayQuery<RAY_FLAG_FORCE_OPAQUE> rq;
    RayDesc rd;
    rd.Origin = origin;
    rd.TMin = 0.05;
    rd.Direction = dir;
    rd.TMax = tMax;
    rq.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, rd);
    uint steps = 0;
    while (rq.Proceed()) {
        steps++;
        if (steps > 128) break;
    }
    if (rq.CommittedStatus() == COMMITTED_NOTHING) {
        hit = RtHitInfo();
        return false;
    }
    hit.t = rq.CommittedRayT();
    hit.instanceId = rq.CommittedInstanceID();
    hit.instanceCustomIndex = rq.CommittedRayInstanceCustomIndex();
    hit.geometryIndex = rq.CommittedGeometryIndex();
    hit.primitiveIndex = rq.CommittedPrimitiveIndex();
    hit.barycentrics = rq.CommittedTriangleBarycentrics();
    return true;
}

export bool rtTraceShadow(
    RaytracingAccelerationStructure tlas,
    float3 origin,
    float3 dir,
    float tMax)
{
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> rq;
    RayDesc rd;
    rd.Origin = origin;
    rd.TMin = 0.01;
    rd.Direction = dir;
    rd.TMax = tMax;
    rq.TraceRayInline(tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE, 0xFF, rd);
    uint steps = 0;
    while (rq.Proceed()) {
        steps++;
        if (steps > 64) break;
    }
    return rq.CommittedStatus() != COMMITTED_NOTHING;
}
