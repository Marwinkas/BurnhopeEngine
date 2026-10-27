#pragma once

#include "gui/data/GuiComponents.hpp"

namespace burnhope::gui {

// Yoga-backed layout for the flecs UI tree.
//
// Ownership: every entity with a UiFlex owns one YGNode created by guiLayoutAttach. The node's context points at a
// small heap record (entity id + per-world shared state), so Yoga callbacks resolve their entity in O(1). Nodes are
// freed by guiLayoutDetach or, as a safety net, by an on_remove hook on UiFlex installed by guiLayoutInit.
// Yoga styles are written by the style resolver (YGNodeStyleSet* on UiFlex::node); this module never styles nodes.
//
// Text metrics: text measurement uses the singleton component GuiTextMetrics (gui/text/GuiTextEngine.hpp) of the
// world. A host installs real font advances with `world.set<GuiTextMetrics>(sheet.text)` (or by editing the singleton
// through guiTextMetricsSet) before the first guiLayoutRun; when the singleton is missing guiLayoutRun creates it
// from guiTextMetricsDefault. After changing the singleton call guiLayoutInvalidate so text nodes re-measure.
//
// Text node measuring: a childless node that has UiText gets a Yoga measure function. Width is the widest wrapped
// line, height is lineCount (at least 1) * guiTextLineHeight(size); both are rounded up to whole pixels. Nodes that
// gain children lose the measure function automatically.

// Installs the per-world layout state and the UiFlex on_remove hook. Idempotent; guiLayoutAttach calls it lazily, but
// calling it before the first UiFlex entity exists is the safest order. Returns false on allocation failure.
bool guiLayoutInit(flecs::world& w);

// Creates the YGNode of `e`, stores the entity id in its context, sets UiFlex (and an empty UiBox) on `e` and appends
// the node under the node of `e.parent()` when that parent has a UiFlex. Attach parents before children; children
// appear in Yoga in attach order. Idempotent for an entity that already has a node. Returns false (and BH_ASSERT in
// debug) when `e` is dead or allocation fails.
bool guiLayoutAttach(flecs::entity e);

// Frees the node of `e` and of every descendant node (children first), removing their UiFlex components.
void guiLayoutDetach(flecs::entity e);

// Forces every text node to be re-measured on the next guiLayoutRun (call after replacing GuiTextMetrics).
void guiLayoutInvalidate(flecs::world& w);

// Lays out every UiRoot entity and writes the absolute screen-space UiBox of the whole subtree.
//  - Roots without UiOverlay get style width/height = screenW/screenH; roots with UiOverlay keep their own style and
//    shrink-wrap their content (unconstrained available size). Place overlays (Yoga position) BEFORE this call.
//  - Child boxes are parentContentOrigin + yogaLeft/Top, where contentOrigin = box origin minus the parent's
//    UiScroll.offX/offY (so scrolling shifts descendants, and nested scrolls accumulate).
//  - UiScroll.viewW/H = own size; contentW/H = extent of the children plus the node's right/bottom padding. Entities
//    that also carry UiVirtual keep their contentW/H (the virtual pool owns the logical extent).
//  - Run guiScissorRun afterwards to refresh UiClip.
void guiLayoutRun(flecs::world& w, float screenW, float screenH);

// Shows or hides `e` and its subtree: toggles UiHidden and the Yoga display so a hidden node also frees its space.
void guiSetShown(flecs::entity e, bool shown);

// Pins `e` at an absolute (x, y, w, h) inside its parent (screen space for a direct child of the screen root).
void guiLayoutSetRect(flecs::entity e, float x, float y, float w, float h);

// Entity owning a Yoga node, or an invalid entity when the node has no context.
flecs::entity guiLayoutEntity(flecs::world& w, YGNodeConstRef node);

} // namespace burnhope::gui
