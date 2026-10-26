#pragma once

#include "asset/ProjectDoc.hpp"
#include "host/Host.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "host/PropBind.hpp"

#include <vector>

namespace burnhope {

struct HudContext;

struct StatsHit {
    Canvas* canvas = nullptr;
    uint16_t page = kUiNone;
    uint16_t dock = kUiNone;
    uint8_t open = 0;
};

struct ToolHit {
    Canvas* canvas = nullptr;
    SceneFrame* scene = nullptr;
    uint16_t objects = kUiNone;
    uint16_t propPage = kUiNone;
};

struct MatLeaf {
    char name[96]{};
    char path[512]{};
};

struct HudIds {
    uint16_t statList = kUiNone;
    uint16_t statsLbl = kUiNone;
    uint16_t memLbl = kUiNone;
    uint16_t loadLbl = kUiNone;
};

// Live inspector and post values. Sliders point here when they are created.
struct HudActor {
    float color[3]{1.0f, 1.0f, 1.0f};
    float rough = 0.5f;
    float metal = 0.0f;
    float emissive = 0.0f;
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    float pivot[3]{};
    uint8_t isStatic = 0;
};

// Modes and pass bits that are not fields of HudPost or SceneTweaks.
struct HudFlags {
    uint8_t hiz = 1;
    uint8_t cone = 1;
    uint8_t lod = 1;
    uint8_t frust = 1;
    uint8_t small = 1;
    uint8_t backf = 1;
    uint8_t scrn = 1;
    uint8_t behind = 1;
    uint8_t shadowCull = 1;
    uint8_t probes = 1;
    uint8_t cluster = 1;
    uint8_t points = 1;
    uint8_t spots = 1;
    uint8_t area = 1;
    uint8_t sky = 1;
    uint8_t clouden = 1;
    uint8_t fogen = 0;
    uint8_t bloomen = 1;
    uint8_t contacten = 0;
    uint8_t vt = 0;
    uint8_t lumScale = 0;
    uint8_t gamut = 0;
    uint8_t sun = 1;
    uint8_t sunCast = 1;
    uint8_t wxClock = 0;
    uint8_t tone = 0;
    uint8_t aa = 4;
    uint8_t fsrQ = 0;
    uint8_t giMode = 3;
    uint8_t directMode = 1;
    uint8_t reflectMode = 2;
    uint8_t aoMode = 1;
    uint8_t sunShadow = 0;
    uint8_t viewMode = 0;
    float forceLod = -1.0f;
};

struct MatHit {
    HudContext* hud = nullptr;
    uint8_t kind = 0;
};

inline constexpr uint8_t kMatSlotN = 9;

struct HudContext {
    HostSampler sampler{};
    SceneFrame* scene = nullptr;
    Host* host = nullptr;
    ColorBook* book = nullptr;
    int slot = -1;
    uint8_t lookGrab = 0;
    uint8_t toolGrab = 0;
    uint32_t presentSeen = 0;
    float lookX = 0.0f;
    float lookY = 0.0f;
    uint8_t shown = 0xff;
    uint8_t propSel = 0xff;
    uint8_t propStatic = 0;
    float propLast[15]{};
    float propPivot[3]{};
    ToolHit tool{};
    StatsHit stats{};
    uint16_t viewRadio[20]{};
    uint16_t snapBtn[3]{};
    uint16_t snapIcon[3]{};
    uint16_t snapNum[3]{};
    uint16_t playBtn = kUiNone;
    uint16_t playGlyph = kUiNone;
    uint16_t pauseBtn = kUiNone;
    uint16_t spawnBtn = kUiNone;
    uint16_t gearBtn = kUiNone;
    uint16_t toolBar = kUiNone;
    uint16_t viewPane = kUiNone;
    uint16_t statsPanel = kUiNone;
    uint16_t statCheck[6]{};
    uint16_t statLine[6]{};
    uint16_t overlay = kUiNone;
    uint16_t viewBtn = kUiNone;
    uint16_t viewCap = kUiNone;
    uint16_t viewPanel = kUiNone;
    float viewPopH = 0.0f;
    uint16_t camBtn = kUiNone;
    uint16_t camPanel = kUiNone;
    uint16_t camFov = kUiNone;
    uint16_t camFovNum = kUiNone;
    uint16_t camNear = kUiNone;
    uint16_t camFar = kUiNone;
    uint16_t camSpeed = kUiNone;
    uint16_t camProj[2]{};
    uint16_t camAxis[6]{};
    uint8_t toolMode = 0;
    uint8_t space = 0;
    uint8_t pivotSpace = 0;
    uint8_t snapOn[3]{};
    uint8_t snapPick[3]{3, 2, 0};
    float snapSent[3]{1.0f, 15.0f, 0.1f};
    uint8_t statOn[6]{};
    uint8_t statsWant = 0;
    uint8_t camWant = 0;
    uint8_t viewWant = 0;
    uint8_t camBreak = 0;
    uint8_t camInit = 0;
    uint8_t viewSent = 0;
    float fovSent = 60.0f;
    float nearSent = 0.1f;
    float farSent = 8000.0f;
    float speedSent = 4.0f;
    HudIds ids{};
    UiPropBinder bind{};
    SceneBlock block{};
    HudPost post{};
    SceneTweaks tweaks{};
    HudFlags flags{};
    HudActor actor{};
    uint16_t menuBtn[8]{};
    uint16_t diBody = kUiNone;
    uint16_t indBody = kUiNone;
    uint16_t reflBody = kUiNone;
    uint16_t rtBody = kUiNone;
    uint16_t aoBody = kUiNone;
    uint16_t sscsBody = kUiNone;
    uint16_t bloomBody = kUiNone;
    uint16_t fsrBody = kUiNone;
    uint16_t fogBody = kUiNone;
    uint16_t fogSwatch = kUiNone;
    uint16_t sunSwatch = kUiNone;
    uint16_t wxBtn = kUiNone;
    uint8_t projectAsk = 0;
    uint8_t texAsk = 0;
    uint8_t rootReady = 0;
    uint8_t matOpen = 0;
    uint8_t texLive = 0;
    MatHit matHit[kMatSlotN]{};
    uint16_t matTex[kMatSlotN]{};
    uint16_t matThumb[kMatSlotN]{};
    uint16_t matClear[kMatSlotN]{};
    uint16_t matEmpty[kMatSlotN]{};
    uint16_t matVal[kMatSlotN]{};
    uint16_t matType = kUiNone;
    uint16_t matPack = kUiNone;
    uint16_t matPreview = kUiNone;
    float previewYaw = 0.55f;
    float previewPitch = 0.35f;
    float previewTint[3]{1.0f, 1.0f, 1.0f};
    float previewPx = 0.0f;
    float previewPy = 0.0f;
    uint8_t previewDown = 0;
    uint8_t previewDirty = 1;
    uint8_t atlasDirty = 1;
    bool matEdited = false;
    uint8_t matListed = 0;
    MaterialInfo matDoc{};
    MaterialInfo matSent{};
    char matPath[512]{};
    char projectRoot[512]{};
    BhmatDoc live{};
    BhmatDoc liveSent{};
    char livePath[512]{};
    uint8_t liveActor = 0xff;
    float uvDeg = 0.0f;
    uint16_t livePick = kUiNone;
    uint16_t liveDrop = kUiNone;
    std::vector<MatLeaf> mats;
    std::vector<uint8_t> previewBuf;
    std::vector<uint8_t> atlasBuf;
};

struct Engine {
    Host host;
    ColorBook colors{};
    SceneFrame* scene = nullptr;
    HudContext hud{};
};

[[nodiscard]] bool engineImportScene();
[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
