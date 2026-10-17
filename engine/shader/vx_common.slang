// Клипмап VXGI. Три уровня, 128³, два flight. Блок каскада — 64 байта, как VxCascade.

static const uint kVxRes = 128u;
static const uint kVxN = 128u * 128u * 128u;

struct VxCascade {
    int anchorX;
    int anchorY;
    int anchorZ;
    int scrollX;
    int scrollY;
    int scrollZ;
    float voxel;
    float extent;
    int slabMinX;
    int slabMinY;
    int slabMinZ;
    uint full;
    int slabMaxX;
    int slabMaxY;
    int slabMaxZ;
    int pad;
};

struct VxClip {
    VxCascade level0;
    VxCascade level1;
    VxCascade level2;
};

struct VxPush {
    uint cascade;
    uint flight;
    uint instances;
    uint flags;
};

[[vk::push_constant]]
VxPush gPush;

[[vk::binding(59, 0)]]
ConstantBuffer<VxClip> gVx;

VxCascade vxLevel(uint cascade) {
    if (cascade == 0u) {
        return gVx.level0;
    }
    if (cascade == 1u) {
        return gVx.level1;
    }
    return gVx.level2;
}

uint vxAtomBase() {
    return (gPush.flight * 3u + gPush.cascade) * kVxN * 4u;
}

uint vxImage() {
    return gPush.flight * 3u + gPush.cascade;
}

bool vxInside(VxCascade cas, int x, int y, int z) {
    return x >= cas.anchorX && y >= cas.anchorY && z >= cas.anchorZ
        && x < cas.anchorX + int(kVxRes) && y < cas.anchorY + int(kVxRes) && z < cas.anchorZ + int(kVxRes);
}

bool vxAxisSlab(int v, int smin, int smax) {
    return smax > smin && v >= smin && v < smax;
}

bool vxDirty(VxCascade cas, int x, int y, int z) {
    if (!vxInside(cas, x, y, z)) {
        return false;
    }
    if (cas.full != 0u) {
        return true;
    }
    return vxAxisSlab(x, cas.slabMinX, cas.slabMaxX)
        || vxAxisSlab(y, cas.slabMinY, cas.slabMaxY)
        || vxAxisSlab(z, cas.slabMinZ, cas.slabMaxZ);
}

int3 vxScroll(VxCascade cas) {
    return int3(cas.scrollX, cas.scrollY, cas.scrollZ);
}

int3 vxAnchor(VxCascade cas) {
    return int3(cas.anchorX, cas.anchorY, cas.anchorZ);
}
