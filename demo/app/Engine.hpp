#pragma once

#include "app/Shell.hpp"
#include "host/Host.hpp"
#include "render/SceneFrame.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <deque>

namespace burnhope {

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
    int index = 0;
    uint16_t label = kUiNone;
};

struct ToolHit {
    Canvas* canvas = nullptr;
    SceneFrame* scene = nullptr;
    uint16_t objects = kUiNone;
    uint16_t postPage = kUiNone;
    uint16_t sunPage = kUiNone;
};

struct GizmoHit {
    ToolHit* tool = nullptr;
    uint8_t mode = 0;
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
    uint16_t tone[4]{};
    uint16_t prof[4]{};
    uint16_t stat[12]{};
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
    uint16_t bloomth = kUiNone;
    uint16_t fog = kUiNone;
    uint16_t fogH = kUiNone;
    uint16_t fogS = kUiNone;
    uint16_t zenith = kUiNone;
    uint16_t azimuth = kUiNone;
    uint16_t clouds = kUiNone;
    uint16_t cdens = kUiNone;
    uint16_t hiz = kUiNone;
    uint16_t cone = kUiNone;
    uint16_t lod = kUiNone;
    uint16_t frust = kUiNone;
    uint16_t small = kUiNone;
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
    uint16_t sun = kUiNone;
    uint16_t points = kUiNone;
    uint16_t spots = kUiNone;
    uint16_t area = kUiNone;
    uint16_t sky = kUiNone;
    uint16_t clouden = kUiNone;
    uint16_t fogen = kUiNone;
    uint16_t reflecten = kUiNone;
    uint16_t contacten = kUiNone;
    uint16_t vt = kUiNone;
    uint16_t freeze = kUiNone;
    uint16_t rcen = kUiNone;
};

struct HudContext {
    SceneFrame* scene = nullptr;
    Host* host = nullptr;
    int slot = -1;
    uint8_t lookGrab = 0;
    uint8_t toolGrab = 0;
    float lookX = 0.0f;
    float lookY = 0.0f;
    uint8_t shown = 0xff;
    ToolHit tool{};
    StatsHit stats{};
    std::deque<PageHit> pages;
    std::deque<ModeHit> modes;
    std::deque<GizmoHit> gizmos;
    std::deque<ObjectHit> objects;
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

[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
