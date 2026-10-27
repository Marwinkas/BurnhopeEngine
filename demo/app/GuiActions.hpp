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
    kActSnap = 11,

    kActFileNew = 20,
    kActFileBistro = 21,
    kActFileSave = 22,
    kActFileQuit = 23,
    kActFileProjects = 24,
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

    kActFilterBase = 90, // 90..93 pick the file kind filter: All, Textures, Materials, Scenes
    kActDebugBase = 100, // 100..118 pick kDebugViews[action - kActDebugBase]

    kActMatClose = 140,
    kActMatFlipY = 141,
    kActMatResetTiling = 142,
    kActMatResetOffset = 143,
    kActMatSave = 144,
    kActMatShape = 145, // 145..147: sphere, cube, plane

    kActBrowserCrumb = 150, // 150..155 jump to the n-th shown breadcrumb
    kActBrowserPage = 160,  // new file
    kActBrowserBack = 161,
    kActBrowserForward = 162,
    kActBrowserCreate = 170, // 170..173: new folder, material, scene, file
    kActBrowserSort = 175,
    kActBrowserView = 177, // tile grid <-> compact list
    kActCtxOpen = 180,
    kActCtxReveal = 181,
    kActCtxDuplicate = 182,
    kActCtxDelete = 183,
    kActLauncherCreate = 190,
    kActLauncherOpen = 191,
    kActLauncherRecent = 192, // 192..199 open the n-th recent project
    kActLauncherForget = 200, // 200..207 drop the n-th recent project
    kActLauncherClose = 208,

    kActTreeItem = 80,
    kActFileItem = 81,
    kActFolderUp = 82,
    kActHierarchyItem = 83,
};

} // namespace burnhope
