#pragma once

#include "host/PropImport.hpp"
#include "ui/Style.hpp"

namespace burnhope {

struct BhmatDoc;
struct Engine;
struct Canvas;
struct HudContext;

void setSlide(Canvas& canvas, uint16_t id, float value);
float slideAt(const Canvas& canvas, uint16_t id, float fallback);
bool checkOn(const Canvas& canvas, uint16_t id, bool fallback);
void pullProp(HudContext& hud, Canvas& canvas, uint8_t index);
void syncProp(Engine& e, Canvas& canvas);
void placeStats(UiState& state, const HudContext& hud);
void syncViewport(Engine& e, Canvas& canvas);
enum class UiPropCat : uint8_t {
    General = 0,
    Actor,
    Lod,
    Misc,
    Physics,
    Rendering,
    Streaming,
    All,
};

enum HudMenu : uint8_t {
    kMenuTone = 0,
    kMenuAa = 1,
    kMenuFsr = 2,
    kMenuGi = 3,
    kMenuDirect = 4,
    kMenuReflect = 5,
    kMenuAo = 6,
    kMenuSun = 7,
};

struct HudMenuItem {
    const char* label = nullptr;
    uint8_t value = 0;
};

void hudShowBodies(HudContext& hud, UiState& s);
void hudRefreshActors(HudContext& hud);
void spawnAndSelectActor(HudContext& hud, uint8_t index);
void applySnap(HudContext& hud, uint8_t channel, uint16_t index);
void onMaterialAct(void* user, uint16_t id);
void openMaterialFile(HudContext& hud, const char* path);
void saveOpenMaterial(HudContext& hud);
[[nodiscard]] bool matSame(const MaterialInfo& a, const MaterialInfo& b);
void hudBuildMaterialPage(Canvas& canvas, HudContext* hud, uint16_t panel);
void hudPaintMaterial(HudContext& hud);
void hudBakeMaterialView(HudContext& hud);
// Right-drag over the material preview orbits it.
void hudPreviewOrbit(HudContext& hud, const InputFrame& input);
void hudCommitMaps(HudContext& hud, uint8_t channel);
void hudBuildActorMaterial(Canvas& canvas, HudContext* hud, uint16_t panel);
char* hudDocPath(BhmatDoc& doc, uint8_t channel);
UiState* hudUi(HudContext& hud);
void hudPaintPick(HudContext& hud, UiState& state);
void hudMaterialSync(Engine& e, Canvas& canvas);
void hudMaterialDrag(HudContext& hud, UiState& s);
void hudDragBadge(void* user, char* dst, uint8_t cap);
void hudDropImport(void* user, const char* path, float x, float y);
void hudTakeTexture(HudContext& hud, uint8_t channel, const char* path);
void onFileOpen(void* user, const char* path, int isDir);
void paintTools(HudContext& hud);
void cmdGizmo(void* user, uint16_t mode);
void cmdSpace(void* user, uint16_t index);
void cmdPivot(void* user, uint16_t index);
void cmdSnapToggle(void* user, uint16_t channel);
void cmdSnapMove(void* user, uint16_t index);
void cmdSnapTurn(void* user, uint16_t index);
void cmdSnapScale(void* user, uint16_t index);
void cmdSpawn(void* user, uint16_t kind);
void cmdView(void* user, uint16_t mode);
void onPlay(void* user, uint16_t);
void onPause(void* user, uint16_t);
void onGear(void* user, uint16_t);
void onCameraButton(void* user, uint16_t);
void onViewButton(void* user, uint16_t);
void onSpawnCube(void* user, uint16_t);
void onDuplicate(void* user, uint16_t);
void onDuplicateCmd(void* user, uint16_t item);
void onStatsCmd(void* user, uint16_t item);
void hudBuildInspector(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t stats);
void hudBuildPost(Canvas& canvas, HudContext* hud, uint16_t details, UiPaint hidden);
void onImportFlag(void* user, uint16_t index, uint8_t value, uint16_t button, const char* label);
void hudPushFrame(Engine& e, Canvas& canvas, uint8_t weatherIndex, uint8_t selected);
void hudFrame(Engine& e);
void hudSeed(HudContext& hud);
void wxClear(void* user, uint16_t item);
void wxOvercast(void* user, uint16_t item);
void wxRain(void* user, uint16_t item);
void wxStorm(void* user, uint16_t item);

struct ViewMode {
    int index = 0;
    const char* name = nullptr;
};

inline const ViewMode kViews[] = {
    {0, "Lit"}, {1, "Primitive ID"}, {2, "Normals"}, {4, "GI"}, {5, "каскады"}, {6, "контакт"},
    {7, "луч"}, {8, "прямой"}, {9, "альбедо"}, {10, "солнце"}, {11, "шероховатость"},
    {12, "металл"}, {13, "AO"}, {14, "свечение"},
    {15, "ReSTIR свет"}, {16, "ReSTIR вес"}, {17, "ReSTIR M"}, {18, "ReSTIR тень"}, {19, "ReSTIR индекс"},
};

void hudWire(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t statsBody, uint16_t files, uint16_t detailsInner);
void cmdFileNew(void* user, uint16_t item);
void cmdFileOpen(void* user, uint16_t item);
void cmdFileSave(void* user, uint16_t item);
void cmdFileLoad(void* user, uint16_t item);
void cmdFileExit(void* user, uint16_t item);
[[nodiscard]] bool projectGatePoll(Engine& e);
// Opens (ask 2) or creates (ask 1) the project in `folder`, then remembers it in the recent list.
void projectTake(HudContext& hud, const char* folder, uint8_t ask);
// Builds the project launcher overlay on the canvas, and keeps it shown exactly while no project is open.
void hudBuildLauncher(Canvas& canvas, HudContext* hud);
void hudLauncherSync(Engine& e);
void projectImportModel(HudContext& hud, const char* path);

} // namespace burnhope
