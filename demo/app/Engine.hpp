#pragma once

#include "app/Shell.hpp"
#include "host/Host.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <deque>

namespace burnhope {

struct HudContext;

struct PageHit {
    Canvas* canvas = nullptr;
    uint16_t page = kUiNone;
};

struct StatsHit {
    Canvas* canvas = nullptr;
    uint16_t page = kUiNone;
    uint16_t dock = kUiNone;
    uint8_t open = 0;
};

struct ModeHit {
    Canvas* canvas = nullptr;
    HudContext* hud = nullptr;
    int index = 0;
    uint16_t label = kUiNone;
    uint16_t radio = kUiNone;
};

struct ToolHit {
    Canvas* canvas = nullptr;
    SceneFrame* scene = nullptr;
    uint16_t objects = kUiNone;
    uint16_t postPage = kUiNone;
    uint16_t sunPage = kUiNone;
    uint16_t propPage = kUiNone;
};

struct HudContext;

struct GizmoHit {
    ToolHit* tool = nullptr;
    HudContext* hud = nullptr;
    uint8_t mode = 0;
};

struct SnapHit {
    HudContext* hud = nullptr;
    uint8_t channel = 0;
    uint8_t index = 0;
    float step = 1.0f;
    uint16_t caption = kUiNone;
};

struct SpawnHit {
    HudContext* hud = nullptr;
    uint8_t kind = 0;
};

struct ObjectHit {
    Canvas* canvas = nullptr;
    SceneFrame* scene = nullptr;
    uint8_t index = 0;
    uint16_t page = kUiNone;
};

struct FileAct {
    Canvas* canvas = nullptr;
    int kind = 0;
};

// Состояние HUD одного окна сцены. Кнопки держат указатели на элементы deque:
// push_back их не сдвигает.
struct HudIds {
    uint16_t mode[20]{};
    uint16_t tone[8]{};
    uint16_t prof[4]{};
    uint16_t stat[40]{};
    uint16_t bright = kUiNone;
    uint16_t grain = kUiNone;
    uint16_t bloom = kUiNone;
    uint16_t vig = kUiNone;
    uint16_t ca = kUiNone;
    uint16_t contact = kUiNone;
    uint16_t spec = kUiNone;
    uint16_t tok = kUiNone;
    uint16_t pom = kUiNone;
    uint16_t ggx = kUiNone;
    uint16_t gtao = kUiNone;
    uint16_t lamp = kUiNone;
    uint16_t sunI = kUiNone;
    uint16_t sunR = kUiNone;
    uint16_t sunG = kUiNone;
    uint16_t sunB = kUiNone;
    uint16_t lodf = kUiNone;
    uint16_t gtaor = kUiNone;
    uint16_t gtaoSt = kUiNone;
    uint16_t gtaoSl = kUiNone;
    uint16_t gtaoPw = kUiNone;
    uint16_t gtaoFa = kUiNone;
    uint16_t sscsR = kUiNone;
    uint16_t sscsT = kUiNone;
    uint16_t sscsS = kUiNone;
    uint16_t sscsP = kUiNone;
    uint16_t rcRays = kUiNone;
    uint16_t rcCell = kUiNone;
    uint16_t rcGain = kUiNone;
    uint16_t giGain = kUiNone;
    uint16_t denoise = kUiNone;
    uint16_t rcMax = kUiNone;
    uint16_t rcSun = kUiNone;
    uint16_t rcBounce = kUiNone;
    uint16_t rsLocal = kUiNone;
    uint16_t rsSpatial = kUiNone;
    uint16_t rsRadius = kUiNone;
    uint16_t rsHistory = kUiNone;
    uint16_t rsBoil = kUiNone;
    uint16_t rsTDepth = kUiNone;
    uint16_t rsTNorm = kUiNone;
    uint16_t rsTBias = kUiNone;
    uint16_t rsSDepth = kUiNone;
    uint16_t rsSNorm = kUiNone;
    uint16_t rsSBias = kUiNone;
    uint16_t rsSBoost = kUiNone;
    uint16_t rsSun = kUiNone;
    uint16_t rsSunRays = kUiNone;
    uint16_t rsShadowRays = kUiNone;
    uint16_t rsShadowRange = kUiNone;
    uint16_t rsShadowBias = kUiNone;
    uint16_t rsPermTh = kUiNone;
    uint16_t rsTarget = kUiNone;
    uint16_t rsTOn = kUiNone;
    uint16_t rsSOn = kUiNone;
    uint16_t rsBoilOn = kUiNone;
    uint16_t rsVis = kUiNone;
    uint16_t rsPerm = kUiNone;
    uint16_t rsShort = kUiNone;
    uint16_t rsMat = kUiNone;
    uint16_t rsNaive = kUiNone;
    uint16_t rsSunOn = kUiNone;
    uint16_t restiren = kUiNone;
    uint16_t bloomth = kUiNone;
    uint16_t fog = kUiNone;
    uint16_t fogH = kUiNone;
    uint16_t fogS = kUiNone;
    uint16_t fogFall = kUiNone;
    uint16_t fogR = kUiNone;
    uint16_t fogG = kUiNone;
    uint16_t fogB = kUiNone;
    uint16_t fogLum = kUiNone;
    uint16_t fogStart = kUiNone;
    uint16_t fogOp = kUiNone;
    uint16_t fogIn = kUiNone;
    uint16_t rain = kUiNone;
    uint16_t rainWet = kUiNone;
    uint16_t rainRip = kUiNone;
    uint16_t wind = kUiNone;
    uint16_t windYaw = kUiNone;
    uint16_t wxTime = kUiNone;
    uint16_t wxDay = kUiNone;
    uint16_t wxTemp = kUiNone;
    uint16_t wxClock = kUiNone;
    uint16_t zenith = kUiNone;
    uint16_t azimuth = kUiNone;
    uint16_t clouds = kUiNone;
    uint16_t cdens = kUiNone;
    uint16_t hiz = kUiNone;
    uint16_t cone = kUiNone;
    uint16_t lod = kUiNone;
    uint16_t frust = kUiNone;
    uint16_t small = kUiNone;
    uint16_t backface = kUiNone;
    uint16_t screen = kUiNone;
    uint16_t behind = kUiNone;
    uint16_t shadow = kUiNone;
    uint16_t cluster = kUiNone;
    uint16_t probes = kUiNone;
    uint16_t gien = kUiNone;
    uint16_t cmaa = kUiNone;
    uint16_t fsr = kUiNone;
    uint16_t fsrq[5]{};
    uint16_t fsrSharp = kUiNone;
    uint16_t gtaoen = kUiNone;
    uint16_t gtaoDn = kUiNone;
    uint16_t bloomen = kUiNone;
    uint16_t smaa = kUiNone;
    uint16_t fxaa = kUiNone;
    uint16_t taa = kUiNone;
    uint16_t lumScale = kUiNone;
    uint16_t gamut = kUiNone;
    uint16_t exposure = kUiNone;
    uint16_t temperature = kUiNone;
    uint16_t tint = kUiNone;
    uint16_t contrast = kUiNone;
    uint16_t saturation = kUiNone;
    uint16_t night = kUiNone;
    uint16_t shadows = kUiNone;
    uint16_t midtones = kUiNone;
    uint16_t highlights = kUiNone;
    uint16_t slope = kUiNone;
    uint16_t cdlOffset = kUiNone;
    uint16_t power = kUiNone;
    uint16_t dofFocus = kUiNone;
    uint16_t dofAmount = kUiNone;
    uint16_t flare = kUiNone;
    uint16_t mix = kUiNone;
    uint16_t genContrast = kUiNone;
    uint16_t midIn = kUiNone;
    uint16_t midOut = kUiNone;
    uint16_t hdrMax = kUiNone;
    uint16_t vibrance = kUiNone;
    uint16_t sun = kUiNone;
    uint16_t points = kUiNone;
    uint16_t spots = kUiNone;
    uint16_t area = kUiNone;
    uint16_t sky = kUiNone;
    uint16_t clouden = kUiNone;
    uint16_t fogen = kUiNone;
    uint16_t reflecten = kUiNone;
    uint16_t ssrCut = kUiNone;
    uint16_t ssrSteps = kUiNone;
    uint16_t ssrThick = kUiNone;
    uint16_t ssrMax = kUiNone;
    uint16_t ssrBlur = kUiNone;
    uint16_t ssrMetal = kUiNone;
    uint16_t ssrRt = kUiNone;
    uint16_t ssrEdge = kUiNone;
    uint16_t pbrR = kUiNone;
    uint16_t pbrG = kUiNone;
    uint16_t pbrB = kUiNone;
    uint16_t pbrRough = kUiNone;
    uint16_t pbrMetal = kUiNone;
    uint16_t pbrEm = kUiNone;
    uint16_t pbrPx = kUiNone;
    uint16_t pbrPy = kUiNone;
    uint16_t pbrPz = kUiNone;
    uint16_t pbrEx = kUiNone;
    uint16_t pbrEy = kUiNone;
    uint16_t pbrEz = kUiNone;
    uint16_t pbrSx = kUiNone;
    uint16_t pbrSy = kUiNone;
    uint16_t pbrSz = kUiNone;
    uint16_t pbrOx = kUiNone;
    uint16_t pbrOy = kUiNone;
    uint16_t pbrOz = kUiNone;
    uint16_t xfStatic = kUiNone;
    uint16_t reflRt = kUiNone;
    uint16_t fogSwatch = kUiNone;
    uint16_t sunSwatch = kUiNone;
    uint16_t sunCast = kUiNone;
    uint16_t sunKelvin = kUiNone;
    uint16_t sunAngle = kUiNone;
    uint16_t sunSoft = kUiNone;
    uint16_t sunIndirect = kUiNone;
    uint16_t sunSh0 = kUiNone;
    uint16_t sunSh1 = kUiNone;
    uint16_t contacten = kUiNone;
    uint16_t vt = kUiNone;
    uint16_t freeze = kUiNone;
    uint16_t rcen = kUiNone;
};

struct HudContext {
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
    std::deque<PageHit> pages;
    std::deque<ModeHit> modes;
    std::deque<GizmoHit> gizmos;
    std::deque<SnapHit> snaps;
    std::deque<SpawnHit> spawns;
    std::deque<ObjectHit> objects;
    uint16_t toolBtn[5]{};
    uint16_t toolIcon[5]{};
    uint16_t snapBtn[3]{};
    uint16_t snapIcon[3]{};
    uint16_t snapNum[3]{};
    uint16_t xformLock[4]{};
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
    FileAct fileNew{};
    FileAct fileOpen{};
    FileAct fileSave{};
    FileAct fileExit{};
    HudIds ids{};
};

struct Engine {
    Host host;
    ColorBook colors{};
    SceneFrame* scene = nullptr;
    ShellDemo demo{};
    HudContext hud{};
};

[[nodiscard]] bool engineImportScene();
[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
