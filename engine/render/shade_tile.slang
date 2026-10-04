[[vk::binding(1, 0)]] Texture2D<uint> gVis;

groupshared uint gTileLit;

bool shadeTile(uint3 dtid, uint gtid, out uint vis, out uint2 px) {
    const bool inside = dtid.x < gFrame.extent.x && dtid.y < gFrame.extent.y;
    px = dtid.xy;
    vis = inside ? gVis.Load(int3(px, 0)) : 0u;
    if (gtid == 0u) {
        gTileLit = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (vis != 0u) {
        InterlockedOr(gTileLit, 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (!inside) {
        return false;
    }
    return gTileLit != 0u && vis != 0u;
}
