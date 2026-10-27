#pragma once

#include <cstdint>

namespace burnhope {

// Click action ids of assets/gui/shell.json ("action" / "itemAction"). The JSON is the layout, these are the verbs.
enum GuiAction : uint32_t {
    kActPlay = 1,
    kActPause = 2,
    kActStop = 3,
    kActDebugView = 4,
    kActCameraSpeed = 5,
    kActProjection = 6,
    kActToolSelect = 7,
    kActToolMove = 8,
    kActToolRotate = 9,
    kActToolScale = 10,

    kActFileNew = 20,
    kActFileBistro = 21,
    kActFileSave = 22,
    kActFileQuit = 23,
    kActAddCube = 30,
    kActDuplicate = 31, // Edit menu and the inspector header
    kActViewLit = 40, // 40..43 pick kDebugViews[action - kActViewLit]
    kActViewNormals = 41,
    kActViewAlbedo = 42,
    kActViewAo = 43,

    kActStatic = 50,
    kActResetPos = 52,
    kActResetRot = 53,
    kActResetScale = 54,

    kActSunOn = 60,
    kActSunCast = 61,
    kActSunCsm = 62,
    kActSky = 63,
    kActClouds = 64,
    kActClockSun = 65,
    kActBloom = 70,
    kActGtao = 71,
    kActFog = 72,

    kActTreeItem = 80,
    kActFileItem = 81,
    kActFolderUp = 82,
};

} // namespace burnhope
