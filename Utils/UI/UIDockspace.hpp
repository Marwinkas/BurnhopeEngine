#pragma once
#include "UICore.hpp"
#include "UIWidgets.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// Fixed-region tabbed docking: reproduces the previous ImGui dock preset
// (left Outliner / bottom Content Browser / right Inspector+Properties+
// MaterialEditor, tabbed within each region) with draggable splitters and
// per-panel show/hide + "Reset Layout". Free-form drag-to-any-edge
// re-docking (ImGui's DockBuilder) is explicitly out of scope per the plan.
namespace burnhope::ui {

    enum class DockSlot { Left, Bottom, Right, Center };

    struct DockedPanelInfo {
        std::string name;
        DockSlot slot;
        bool open = true;
    };

    class UIDockspace {
    public:
        void RegisterPanel(const std::string& name, DockSlot slot, bool defaultOpen = true) {
            m_Panels.push_back({name, slot, defaultOpen});
            if (m_ActiveTab.find(slot) == m_ActiveTab.end()) m_ActiveTab[slot] = name;
        }

        void SetPanelOpen(const std::string& name, bool open) {
            for (auto& p : m_Panels) if (p.name == name) {
                p.open = open;
                if (open && m_ActiveTab[p.slot] != name) {
                    bool activeStillOpen = false;
                    for (auto& q : m_Panels) {
                        if (q.slot == p.slot && q.open && q.name == m_ActiveTab[p.slot]) {
                            activeStillOpen = true;
                            break;
                        }
                    }
                    if (!activeStillOpen) m_ActiveTab[p.slot] = name;
                }
            }
        }

        bool IsPanelOpen(const std::string& name) const {
            for (auto& p : m_Panels) if (p.name == name) return p.open;
            return false;
        }

        void ActivateTab(const std::string& name) {
            for (auto& p : m_Panels) if (p.name == name) {
                p.open = true;
                m_ActiveTab[p.slot] = name;
                return;
            }
        }

        void ResetLayout() {
            m_LeftWidthFrac = 0.20f;
            m_RightWidthFrac = 0.24f;
            m_BottomHeightFrac = 0.28f;
            for (auto& p : m_Panels) p.open = true;
            for (auto& [slot, active] : m_ActiveTab) {
                for (auto& p : m_Panels) if (p.slot == slot) { active = p.name; break; }
            }
        }

        struct Result {
            Rect viewport;
            std::unordered_map<std::string, Rect> panelContentRect;
        };

        Result Compute(UIInput& input, UIWidgets& widgets, Rect full) {
            Result result;

            bool leftOn = SlotVisible(DockSlot::Left);
            bool rightOn = SlotVisible(DockSlot::Right);
            bool bottomOn = SlotVisible(DockSlot::Bottom);

            float leftW = leftOn ? full.w * m_LeftWidthFrac : 0.0f;
            float rightW = rightOn ? full.w * m_RightWidthFrac : 0.0f;
            float bottomH = bottomOn ? full.h * m_BottomHeightFrac : 0.0f;

            // Sides run the full dock height so the bottom-left / bottom-right
            // corners belong to Outliner / Inspector instead of empty viewport.
            Rect leftRect{full.x, full.y, leftW, full.h};
            Rect rightRect{full.x + full.w - rightW, full.y, rightW, full.h};
            Rect bottomRect{full.x + leftW, full.y + full.h - bottomH, full.w - leftW - rightW, bottomH};
            Rect centerRect{full.x + leftW, full.y, full.w - leftW - rightW, full.h - bottomH};

            constexpr float kSplitterThickness = 5.0f;
            if (leftOn) {
                DragSplitterVertical(input, widgets,
                    {leftRect.x + leftRect.w - kSplitterThickness * 0.5f, leftRect.y, kSplitterThickness, leftRect.h},
                    m_LeftWidthFrac, full.x, full.w, true);
            }
            if (rightOn) {
                DragSplitterVertical(input, widgets,
                    {rightRect.x - kSplitterThickness * 0.5f, rightRect.y, kSplitterThickness, rightRect.h},
                    m_RightWidthFrac, full.x, full.w, false);
            }
            if (bottomOn) {
                DragSplitterHorizontal(input, widgets,
                    {bottomRect.x, bottomRect.y - kSplitterThickness * 0.5f, bottomRect.w, kSplitterThickness},
                    m_BottomHeightFrac, full.y, full.h);
            }

            if (bottomOn) DrawDockRegion(widgets, DockSlot::Bottom, bottomRect, result);
            if (leftOn) DrawDockRegion(widgets, DockSlot::Left, leftRect, result);
            if (rightOn) DrawDockRegion(widgets, DockSlot::Right, rightRect, result);

            result.viewport = centerRect;
            return result;
        }

    private:
        bool SlotVisible(DockSlot slot) const {
            for (const auto& p : m_Panels) if (p.slot == slot && p.open) return true;
            return false;
        }

        void DrawSplitterLine(UIWidgets& widgets, Rect handle, bool hovered, bool vertical) {
            Color c = hovered ? kTheme.splitterHover : kTheme.splitter;
            if (vertical) {
                widgets.Background({handle.x + handle.w * 0.5f - 1.0f, handle.y, 2.0f, handle.h}, c);
            } else {
                widgets.Background({handle.x, handle.y + handle.h * 0.5f - 1.0f, handle.w, 2.0f}, c);
            }
        }

        void DragSplitterVertical(UIInput& input, UIWidgets& widgets, Rect handle, float& frac,
                                  float originX, float totalWidth, bool growsLeftEdge) {
            uint64_t id = burnhope::hash::HashString(growsLeftEdge ? "split_l" : "split_r");
            bool hovered = handle.Contains(input.MousePos().x, input.MousePos().y) || input.IsActive(id);
            if (handle.Contains(input.MousePos().x, input.MousePos().y) && input.MouseClicked(0)) input.SetActive(id);
            if (hovered) widgets.RequestCursor(UIWidgets::MouseCursor::EwResize);
            if (input.IsActive(id) && input.MouseDown(0)) {
                float mouseX = input.MousePos().x;
                frac = growsLeftEdge ? (mouseX - originX) / totalWidth
                                     : (originX + totalWidth - mouseX) / totalWidth;
                frac = Clamp(frac, 0.12f, 0.45f);
            }
            if (input.MouseReleased(0) && input.IsActive(id)) input.ClearActive();
            DrawSplitterLine(widgets, handle, hovered, true);
        }

        void DragSplitterHorizontal(UIInput& input, UIWidgets& widgets, Rect handle, float& frac,
                                    float originY, float totalHeight) {
            uint64_t id = burnhope::hash::HashString("split_b");
            bool hovered = handle.Contains(input.MousePos().x, input.MousePos().y) || input.IsActive(id);
            if (handle.Contains(input.MousePos().x, input.MousePos().y) && input.MouseClicked(0)) input.SetActive(id);
            if (hovered) widgets.RequestCursor(UIWidgets::MouseCursor::NsResize);
            if (input.IsActive(id) && input.MouseDown(0)) {
                frac = (originY + totalHeight - input.MousePos().y) / totalHeight;
                frac = Clamp(frac, 0.12f, 0.50f);
            }
            if (input.MouseReleased(0) && input.IsActive(id)) input.ClearActive();
            DrawSplitterLine(widgets, handle, hovered, false);
        }

        void DrawDockRegion(UIWidgets& widgets, DockSlot slot, Rect rect, Result& result) {
            if (rect.w < 8.0f || rect.h < 8.0f) return;

            Rect tabBar{rect.x, rect.y, rect.w, kTabBarHeight};
            Rect content{rect.x + 1.0f, rect.y + kTabBarHeight, rect.w - 2.0f, rect.h - kTabBarHeight - 1.0f};

            widgets.Background(rect, kTheme.panel);
            widgets.Background({rect.x, rect.y, 1.0f, rect.h}, kTheme.border);
            widgets.Background({rect.x + rect.w - 1.0f, rect.y, 1.0f, rect.h}, kTheme.border);
            widgets.Background({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, kTheme.border);
            widgets.Background(tabBar, kTheme.title);

            const char* tabId = (slot == DockSlot::Left) ? "##tabs_l"
                               : (slot == DockSlot::Right) ? "##tabs_r" : "##tabs_b";
            widgets.BeginRegion(tabId, tabBar, 2.0f, false);
            std::string& active = m_ActiveTab[slot];
            bool anyOpen = false;
            bool first = true;
            for (auto& panel : m_Panels) {
                if (panel.slot != slot || !panel.open) continue;
                anyOpen = true;
                if (active.empty()) active = panel.name;
                if (!first) widgets.SameLine(2.0f);
                first = false;
                if (widgets.TabItem(panel.name, active == panel.name, kTabBarHeight - 4.0f)) {
                    active = panel.name;
                }
            }
            widgets.EndRegion();

            widgets.Background(content, kTheme.panelInner);

            for (auto& panel : m_Panels) {
                if (panel.slot == slot && panel.open && (panel.name == active || !anyOpen)) {
                    result.panelContentRect[panel.name] = content;
                }
            }
        }

        std::vector<DockedPanelInfo> m_Panels;
        std::unordered_map<DockSlot, std::string> m_ActiveTab;

        float m_LeftWidthFrac = 0.20f;
        float m_RightWidthFrac = 0.24f;
        float m_BottomHeightFrac = 0.26f;
    };
}
