// Общая геометрия пикселя для выделения зонда и для луча. Оба прохода обязаны
// выбрать одну и ту же клетку, иначе префиксная сумма и луч разъедутся.

float rcBaseSpacing() {
    return max(gPush.baseSpacing, 0.05);
}

uint rcLod(float3 P) {
    const float3 rel = abs(P - gFrame.cameraPos.xyz);
    const float cheb = max(rel.x, max(rel.y, rel.z));
    const float lodf = log2(max(cheb / rcBaseSpacing(), 1.0));
    return min(uint(lodf), 5u);
}

float rcSpacing(uint cascade, uint lod) {
    return rcBaseSpacing() * exp2(float(cascade + lod));
}

int3 rcCell(float3 world, uint cascade, uint lod) {
    return int3(floor(world / max(rcSpacing(cascade, lod), 1e-3)));
}

float3 rcCenter(int3 cell, uint cascade, uint lod) {
    return (float3(cell) + 0.5) * rcSpacing(cascade, lod);
}

bool rcPixel(uint2 px, out float depth) {
    depth = 0.0;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return false;
    }
    if (gVis.Load(int3(px, 0)) == 0u) {
        return false;
    }
    depth = gDepth.Load(int3(px, 0));
    return depth > 0.0;
}
