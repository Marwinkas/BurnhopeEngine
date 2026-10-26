#pragma once

#include "platform/Input.hpp"
#include "ui/Flex.hpp"
#include "ui/Geometry.hpp"
#include "ui/Interaction.hpp"
#include "ui/Primitive.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/Typography.hpp"
#include "ui/Types.hpp"
#include "ui/UiBin.hpp"

struct YGNode;

namespace burnhope {

// Id-based node API of the UI runtime. It needs no UiState layout, so Layer 3 reaches it without State.hpp.

void uiFocus(UiState& s, uint16_t id);
void uiFocusStep(UiState& s, int dir);
void uiTab(UiState& s, uint16_t id, uint16_t index);
void uiCursorPush(UiState& s, uint8_t kind);
void uiCursorPop(UiState& s);
void uiSyncInput(UiState& s);
[[nodiscard]] uint16_t uiYogaId(const UiState& s, const YGNode* node);
void uiCacheVisibility(UiState& s);
void uiCapture(UiState& s, uint16_t id);
void uiReleaseCapture(UiState& s);
void uiMarkLayout(UiState& s, uint16_t id);
void uiShowOnly(UiState& s, uint16_t id);
void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user);
void uiBindName(UiState& s, const char* name, UiClickFn fn, void* user);
void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user);
// Right-button drag on a node; fn gets the pointer delta each frame (orbiting a preview, for one).
void uiDragRight(UiState& s, uint16_t id, UiDragRelFn fn, void* user);
void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user);
void uiShow(UiState& s, uint16_t id, bool visible);
void uiPaintRgb(UiState& s, uint16_t id, float r, float g, float b);
void uiReparent(UiState& s, uint16_t id, uint16_t parent);
// Moves a node to the last slot among its siblings.
void uiSendToEnd(UiState& s, uint16_t id);
void uiDrop(UiState& s, uint16_t id);
void uiTrace(const UiState& s, const char* fn, uint16_t id);
void uiDumpLayout(const UiState& s);
void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h);
void uiGrow(UiState& s, uint16_t id, float extraW, float h);
void uiSetHeight(UiState& s, uint16_t id, float h);
void uiSetWidth(UiState& s, uint16_t id, float w);
void uiFlexLimit(UiState& s, uint16_t id, float width, float height, float shrink);
void uiFlexShrink(UiState& s, uint16_t id, float shrink);
void uiRowIndent(UiState& s, uint16_t id, float pad);
void uiPlaceAbsolute(UiState& s, uint16_t id, float x, float y);
uint16_t uiFindChildText(const UiState& s, uint16_t parent);
void uiCenterChildren(UiState& s, uint16_t parent);
void uiSetDrop(UiState& s, uint16_t id, uint8_t drop);
void uiSetFocusable(UiState& s, uint16_t id, bool on = true);
void uiSetPass(UiState& s, uint16_t id, bool on = true);
[[nodiscard]] uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text);
[[nodiscard]] uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size);
[[nodiscard]] uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiFindName(const UiState& s, const char* name);
void uiRequestTop(UiState& s, bool on);
void uiRequestOpacity(UiState& s, float opacity);
void uiRequestMinimize(UiState& s);
void uiRequestMaximize(UiState& s);
void uiRequestClose(UiState& s);
void uiName(UiState& s, uint16_t id, const char* name);
void uiPlaceAdd(UiState& s, void (*fn)(UiState& s));
void uiBeforeLayoutAdd(UiState& s, void (*fn)(UiState& s));
bool nodeShown(const UiState& s, uint16_t id);
uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void addText(UiState& s, uint16_t id, const char* text);
uint16_t hitTest(const UiState& s, float x, float y);
void activate(UiState& s, uint16_t id);
uint32_t uiNodeChildCount(const UiState& s, uint16_t parent);
uint16_t uiNodeChild(const UiState& s, uint16_t parent, uint32_t index);
void uiNodeMoveChild(UiState& s, uint16_t parent, uint16_t child, uint32_t newIndex);
void uiClear(UiState& s);
uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void stamp(UiState& s, uint16_t id, UiToken token);
void uiStamp(UiState& s, uint16_t id, UiToken token);
void uiBlur(UiState& s, uint16_t id, float blur);
void uiTransform(UiState& s, uint16_t id, float angle, float scaleX, float scaleY);
void uiFilter(UiState& s, uint16_t id, float bright, float contrast);
void uiSetPaint(UiState& s, uint16_t id, float r, float g, float b, float a);
[[nodiscard]] UiEvent uiApplyInput(UiState& s, const InputFrame& in);
void uiLayout(UiState& s, float w, float h);
void uiRestyle(UiState& s);
void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a);
void uiReveal(UiState& s, uint16_t id, float full, bool open);
[[nodiscard]] float uiRevealUnit(const UiState& s, uint16_t id);
void uiRevealTick(UiState& s);
[[nodiscard]] bool uiTickMotion(UiState& s);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);
bool uiListen(UiState& s, UiValueFn fn, void* user);
void uiTextStyle(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis, UiTextValign valign, bool wrap, UiTextDeco deco, float leading);
void uiTextReadTail(UiState& s, uint16_t id, const BinTail& tail);
void uiSetDisabled(UiState& s, uint16_t id, bool disabled);
[[nodiscard]] uint16_t uiTextId(const UiState& s, uint16_t id);

// Out-of-line accessors that read UiState members; State.hpp holds the struct they look into.
void uiNotifyLayoutDirty(UiState& s);
void uiNotifyVisualDirty(UiState& s);
void uiSetContextMenu(UiState& s, uint16_t node, uint16_t menu);
[[nodiscard]] uint16_t uiParent(const UiState& s, uint16_t id);
[[nodiscard]] UiBox uiNodeBox(const UiState& s, uint16_t id);
void uiApplyLook(UiState& s, bool warm);

} // namespace burnhope
