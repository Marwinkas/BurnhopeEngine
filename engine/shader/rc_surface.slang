// Пиксель кадра нужен только чтобы прочитать точку затенения. Зонды живут в мире.

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
