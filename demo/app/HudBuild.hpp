#pragma once

#include "app/Engine.hpp"
#include "ui/Style.hpp"

namespace burnhope {

void setSlide(Canvas& canvas, uint16_t id, float value);
float slideAt(const Canvas& canvas, uint16_t id, float fallback);
bool checkOn(const Canvas& canvas, uint16_t id, bool fallback);
void pullProp(HudContext& hud, Canvas& canvas, uint8_t index);
void syncProp(Engine& e, Canvas& canvas);
void placeStats(UiState& state, const HudContext& hud);
void syncViewport(Engine& e, Canvas& canvas);
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
uint16_t hudDrop(HudContext& hud, UiState& s, uint16_t panel, const char* label, uint8_t kind, const HudMenuItem* items, uint8_t count, uint8_t selected);
uint16_t bindSwatch(HudContext& hud, UiState& s, uint16_t panel, const char* label, float* r, float* g, float* b, float r0, float g0, float b0);
bool addActorButton(HudContext& hud, uint16_t objects, uint8_t index, const char* name, uint8_t kind);
void spawnAndSelectActor(HudContext& hud, uint8_t index);
void onMaterialAct(void* user, uint16_t id);
void openMaterialFile(HudContext& hud, const char* path);
void saveOpenMaterial(HudContext& hud);
void paintMatTex(HudContext& hud);
void hudBuildActorMaterial(Canvas& canvas, HudContext* hud, uint16_t panel);
char* hudDocPath(BhmatDoc& doc, uint8_t channel);
UiState* hudUi(HudContext& hud);
void hudPaintSlots(HudContext& hud);
void hudPushLive(HudContext& hud, UiState& state);
void hudPaintPick(HudContext& hud, UiState& state);
void hudMaterialSync(Engine& e, Canvas& canvas);
void hudMaterialDrag(HudContext& hud, UiState& s);
void hudDragBadge(void* user, char* dst, uint8_t cap);
void hudDropImport(void* user, const char* path, float x, float y);
void hudTakeTexture(HudContext& hud, uint8_t channel, const char* path);
void onFileOpen(void* user, const char* path, int isDir);
void paintTools(HudContext& hud);
void onGizmoMode(void* user, uint16_t);
void onSpace(void* user, uint8_t index);
void onSnapToggle(void* user, uint16_t);
void onSnapPick(void* user);
void onPivotSpace(void* user, uint8_t index);
void onSpawnKind(void* user);
void onPlay(void* user, uint16_t);
void onPause(void* user, uint16_t);
void onGear(void* user, uint16_t);
void onCameraButton(void* user, uint16_t);
void onViewButton(void* user, uint16_t);
void onViewRadio(void* user, uint16_t id);
void onSpawnCube(void* user, uint16_t);
void onDuplicate(void* user, uint16_t);
void onDuplicateCmd(void* user);
void onStatsCmd(void* user);
bool loadToolIcon(Canvas& canvas, uint8_t slot, const char* file);
bool fontBesideExe(Canvas& canvas);
void hudBuildInspector(Canvas& canvas, HudContext* hud, uint16_t details, uint16_t stats, UiPaint hidden);
void hudBuildPost(Canvas& canvas, HudContext* hud, uint16_t details, UiPaint hidden);
void hudPushFrame(Engine& e, Canvas& canvas, uint8_t weatherIndex, uint8_t selected);
void hudSeed(HudContext& hud);
void wxClear(void* user);
void wxOvercast(void* user);
void wxRain(void* user);
void wxStorm(void* user);

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

void hudBuildView(Canvas& canvas, HudContext* hud, uint16_t root, uint16_t row, uint16_t viewCol, const UiLook& look);
void hudBuildStrip(Canvas& canvas, HudContext* hud, uint16_t toolbar, uint16_t root, const UiLook& look);
void projectOnFile(FileAct* hit);
[[nodiscard]] bool projectGatePoll(Engine& e);
void projectImportModel(HudContext& hud, const char* path);

} // namespace burnhope
