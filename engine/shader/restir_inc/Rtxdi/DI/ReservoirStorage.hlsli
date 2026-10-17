#ifndef RTXDI_DI_RESERVOIR_STORAGE_HLSLI
#define RTXDI_DI_RESERVOIR_STORAGE_HLSLI

#include "Rtxdi/DI/Reservoir.hlsli"
#include "Rtxdi/Utils/ReservoirAddressing.hlsli"

#ifndef RTXDI_ENABLE_STORE_RESERVOIR
#define RTXDI_ENABLE_STORE_RESERVOIR 1
#endif

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_LIGHT_RESERVOIR_BUFFER must be defined"
#endif

static const uint RTXDI_PackedDIReservoir_DistanceChannelBits = 8;
static const uint RTXDI_PackedDIReservoir_DistanceXShift = 0;
static const uint RTXDI_PackedDIReservoir_DistanceYShift = 8;
static const uint RTXDI_PackedDIReservoir_AgeShift = 16;
static const uint RTXDI_PackedDIReservoir_MaxAge = 0xff;
static const uint RTXDI_PackedDIReservoir_DistanceMask = (1u << RTXDI_PackedDIReservoir_DistanceChannelBits) - 1;
static const int RTXDI_PackedDIReservoir_MaxDistance = int((1u << (RTXDI_PackedDIReservoir_DistanceChannelBits - 1)) - 1);

RTXDI_PackedDIReservoir RTXDI_PackDIReservoir(const RTXDI_DIReservoir reservoir)
{
    int2 clampedSpatialDistance = clamp(reservoir.spatialDistance, -RTXDI_PackedDIReservoir_MaxDistance, RTXDI_PackedDIReservoir_MaxDistance);
    uint clampedAge = clamp(reservoir.age, 0, RTXDI_PackedDIReservoir_MaxAge);

    RTXDI_PackedDIReservoir data;
    data.lightData = reservoir.lightData;
    data.uvData = reservoir.uvData;
    data.mVisibility = reservoir.packedVisibility
        | (min(uint(reservoir.M), RTXDI_PackedDIReservoir_MaxM) << RTXDI_PackedDIReservoir_MShift);
    data.distanceAge =
          ((clampedSpatialDistance.x & RTXDI_PackedDIReservoir_DistanceMask) << RTXDI_PackedDIReservoir_DistanceXShift)
        | ((clampedSpatialDistance.y & RTXDI_PackedDIReservoir_DistanceMask) << RTXDI_PackedDIReservoir_DistanceYShift)
        | (clampedAge << RTXDI_PackedDIReservoir_AgeShift);
    data.targetPdf = reservoir.targetPdf;
    data.weight = reservoir.weightSum;
    return data;
}

RTXDI_DIReservoir RTXDI_UnpackDIReservoir(RTXDI_PackedDIReservoir data)
{
    RTXDI_DIReservoir res;
    res.lightData = data.lightData;
    res.uvData = data.uvData;
    res.targetPdf = data.targetPdf;
    res.weightSum = data.weight;
    res.M = (data.mVisibility >> RTXDI_PackedDIReservoir_MShift) & RTXDI_PackedDIReservoir_MaxM;
    res.packedVisibility = data.mVisibility & RTXDI_PackedDIReservoir_VisibilityMask;
    res.spatialDistance.x = int(data.distanceAge << (32 - RTXDI_PackedDIReservoir_DistanceXShift - RTXDI_PackedDIReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedDIReservoir_DistanceChannelBits);
    res.spatialDistance.y = int(data.distanceAge << (32 - RTXDI_PackedDIReservoir_DistanceYShift - RTXDI_PackedDIReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedDIReservoir_DistanceChannelBits);
    res.age = (data.distanceAge >> RTXDI_PackedDIReservoir_AgeShift) & RTXDI_PackedDIReservoir_MaxAge;
    res.canonicalWeight = 0.0f;
    if (isinf(res.weightSum) || isnan(res.weightSum)) {
        res = RTXDI_EmptyDIReservoir();
    }
    return res;
}

RestirSlot RTXDI_PackRestirSlot(const RTXDI_DIReservoir reservoir)
{
    RTXDI_PackedDIReservoir packed = RTXDI_PackDIReservoir(reservoir);
    RestirSlot slot;
    slot.lightData = packed.lightData;
    slot.uvData = packed.uvData;
    slot.mVisibility = packed.mVisibility;
    slot.distanceAge = packed.distanceAge;
    slot.targetPdf = packed.targetPdf;
    slot.weight = packed.weight;
    slot.pad0 = 0u;
    slot.pad1 = 0u;
    return slot;
}

RTXDI_DIReservoir RTXDI_UnpackRestirSlot(RestirSlot slot)
{
    RTXDI_PackedDIReservoir packed;
    packed.lightData = slot.lightData;
    packed.uvData = slot.uvData;
    packed.mVisibility = slot.mVisibility;
    packed.distanceAge = slot.distanceAge;
    packed.targetPdf = slot.targetPdf;
    packed.weight = slot.weight;
    return RTXDI_UnpackDIReservoir(packed);
}

#if RTXDI_ENABLE_STORE_RESERVOIR
void RTXDI_StoreDIReservoir(
    const RTXDI_DIReservoir reservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_ReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    RTXDI_LIGHT_RESERVOIR_BUFFER[pointer] = RTXDI_PackRestirSlot(reservoir);
}
#endif

RTXDI_DIReservoir RTXDI_LoadDIReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_ReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    return RTXDI_UnpackRestirSlot(RTXDI_LIGHT_RESERVOIR_BUFFER[pointer]);
}

#endif
