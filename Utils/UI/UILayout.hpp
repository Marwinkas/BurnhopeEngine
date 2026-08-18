#pragma once
#include "UICore.hpp"
#include <yoga/Yoga.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

// Thin Yoga (flexbox) wrapper for the immediate-mode widget layer. Nodes are
// retained across frames keyed by a stable id (hash of the widget's ID-stack
// path, same idea as ImGui's PushID/PopID), so style calls are cheap and the
// tree does not get rebuilt from scratch — only YGNodeCalculateLayout runs
// when something is marked dirty.
namespace burnhope::ui {

    struct FlexStyle {
        YGFlexDirection direction = YGFlexDirectionColumn;
        YGJustify justify = YGJustifyFlexStart;
        YGAlign alignItems = YGAlignStretch;
        YGAlign alignSelf = YGAlignAuto;
        YGWrap wrap = YGWrapNoWrap;

        float width = YGUndefined;
        float height = YGUndefined;
        float minWidth = YGUndefined;
        float minHeight = YGUndefined;
        float grow = 0.0f;
        float shrink = 1.0f;

        float paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
        float marginLeft = 0, marginRight = 0, marginTop = 0, marginBottom = 0;
        float gap = 0.0f;
        YGPositionType positionType = YGPositionTypeStatic;
        float left = YGUndefined, top = YGUndefined;
    };

    class UILayout {
    public:
        UILayout() { m_Root = YGNodeNew(); }
        ~UILayout() {
            for (auto& [id, node] : m_Nodes) YGNodeFree(node);
            YGNodeFree(m_Root);
        }

        void BeginFrame(float rootWidth, float rootHeight) {
            m_VisitedThisFrame.clear();
            YGNodeStyleSetWidth(m_Root, rootWidth);
            YGNodeStyleSetHeight(m_Root, rootHeight);
            m_Stack.clear();
            m_Stack.push_back({m_Root, 0});
            m_RootSize = {rootWidth, rootHeight};
        }

        // Creates (or reuses) a flex container with the given persistent id,
        // applies `style`, and pushes it as the new layout parent.
        void Push(uint64_t id, const FlexStyle& style) {
            YGNodeRef node = GetOrCreate(id);
            ApplyStyle(node, style);
            AttachToCurrentParent(node);
            m_Stack.push_back({node, 0});
        }

        void Pop() {
            if (m_Stack.size() > 1) m_Stack.pop_back();
        }

        // Leaf widget: same as Push/Pop but with no children expected.
        void Leaf(uint64_t id, const FlexStyle& style) {
            YGNodeRef node = GetOrCreate(id);
            ApplyStyle(node, style);
            AttachToCurrentParent(node);
        }

        // Call once per frame after all Push/Pop/Leaf calls for the frame.
        void Calculate() {
            PruneUnvisited();
            YGNodeCalculateLayout(m_Root, m_RootSize.x, m_RootSize.y, YGDirectionLTR);
        }

        Rect GetRect(uint64_t id) const {
            auto it = m_Nodes.find(id);
            if (it == m_Nodes.end()) return {};
            return AbsoluteRect(it->second);
        }

    private:
        struct StackEntry { YGNodeRef node; size_t childIndex; };

        YGNodeRef GetOrCreate(uint64_t id) {
            m_VisitedThisFrame.insert(id);
            auto it = m_Nodes.find(id);
            if (it != m_Nodes.end()) return it->second;
            YGNodeRef node = YGNodeNew();
            m_Nodes.emplace(id, node);
            return node;
        }

        void AttachToCurrentParent(YGNodeRef node) {
            StackEntry& top = m_Stack.back();
            // Reconcile: ensure `node` sits at `childIndex` under the current
            // parent (cheap no-op if it is already there).
            if (YGNodeGetChild(top.node, static_cast<uint32_t>(top.childIndex)) != node) {
                if (YGNodeGetParent(node) != nullptr) YGNodeRemoveChild(YGNodeGetParent(node), node);
                YGNodeInsertChild(top.node, node, static_cast<uint32_t>(top.childIndex));
            }
            top.childIndex++;
        }

        static void ApplyStyle(YGNodeRef node, const FlexStyle& s) {
            YGNodeStyleSetFlexDirection(node, s.direction);
            YGNodeStyleSetJustifyContent(node, s.justify);
            YGNodeStyleSetAlignItems(node, s.alignItems);
            YGNodeStyleSetAlignSelf(node, s.alignSelf);
            YGNodeStyleSetFlexWrap(node, s.wrap);
            YGNodeStyleSetFlexGrow(node, s.grow);
            YGNodeStyleSetFlexShrink(node, s.shrink);
            YGNodeStyleSetPositionType(node, s.positionType);

            if (!YGFloatIsUndefined(s.width)) YGNodeStyleSetWidth(node, s.width); else YGNodeStyleSetWidthAuto(node);
            if (!YGFloatIsUndefined(s.height)) YGNodeStyleSetHeight(node, s.height); else YGNodeStyleSetHeightAuto(node);
            if (!YGFloatIsUndefined(s.minWidth)) YGNodeStyleSetMinWidth(node, s.minWidth);
            if (!YGFloatIsUndefined(s.minHeight)) YGNodeStyleSetMinHeight(node, s.minHeight);
            if (!YGFloatIsUndefined(s.left)) YGNodeStyleSetPosition(node, YGEdgeLeft, s.left);
            if (!YGFloatIsUndefined(s.top)) YGNodeStyleSetPosition(node, YGEdgeTop, s.top);

            YGNodeStyleSetPadding(node, YGEdgeLeft, s.paddingLeft);
            YGNodeStyleSetPadding(node, YGEdgeRight, s.paddingRight);
            YGNodeStyleSetPadding(node, YGEdgeTop, s.paddingTop);
            YGNodeStyleSetPadding(node, YGEdgeBottom, s.paddingBottom);
            YGNodeStyleSetMargin(node, YGEdgeLeft, s.marginLeft);
            YGNodeStyleSetMargin(node, YGEdgeRight, s.marginRight);
            YGNodeStyleSetMargin(node, YGEdgeTop, s.marginTop);
            YGNodeStyleSetMargin(node, YGEdgeBottom, s.marginBottom);
            YGNodeStyleSetGap(node, YGGutterAll, s.gap);
        }

        Rect AbsoluteRect(YGNodeRef node) const {
            float x = YGNodeLayoutGetLeft(node);
            float y = YGNodeLayoutGetTop(node);
            YGNodeRef parent = YGNodeGetParent(node);
            while (parent && parent != m_Root) {
                x += YGNodeLayoutGetLeft(parent);
                y += YGNodeLayoutGetTop(parent);
                parent = YGNodeGetParent(parent);
            }
            if (parent == m_Root) {
                x += YGNodeLayoutGetLeft(m_Root);
                y += YGNodeLayoutGetTop(m_Root);
            }
            return {x, y, YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)};
        }

        void PruneUnvisited() {
            for (auto it = m_Nodes.begin(); it != m_Nodes.end();) {
                if (!m_VisitedThisFrame.count(it->first)) {
                    if (YGNodeRef parent = YGNodeGetParent(it->second)) YGNodeRemoveChild(parent, it->second);
                    YGNodeFree(it->second);
                    it = m_Nodes.erase(it);
                } else {
                    ++it;
                }
            }
        }

        YGNodeRef m_Root;
        glm::vec2 m_RootSize{0, 0};
        std::unordered_map<uint64_t, YGNodeRef> m_Nodes;
        std::unordered_set<uint64_t> m_VisitedThisFrame;
        std::vector<StackEntry> m_Stack;
    };
}
