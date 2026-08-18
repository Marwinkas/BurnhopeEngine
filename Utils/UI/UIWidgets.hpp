#pragma once
#include "UICore.hpp"
#include "UIInput.hpp"
#include "UIRenderer.hpp"
#include "UIText.hpp"
#include "../MurmurHash3.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <any>
#include <unordered_set>
#include <unordered_map>

// Immediate-mode widget layer, deliberately shaped like the old
// Utils/UIUtils.h + ImGui call sites so panel logic in Outliner/Inspector/
// ContentBrowser/Properties/MaterialEditor ports mechanically.
//
// Scoping note: widget placement inside a panel body uses a simple
// top-to-bottom/left-to-right immediate-mode cursor (ImGui/Nuklear style)
// rather than re-running Yoga for every leaf control every frame — Yoga
// (UILayout) is used for the coarse, structural regions (dock areas, panel
// rects) where retained flexbox layout actually pays for itself. This keeps
// per-widget cost trivial (a handful of float additions) which matters more
// here since widgets run every frame for potentially thousands of outliner
// rows / property fields.
namespace burnhope::ui {

    struct DragDropPayload {
        std::string type;
        std::any data;
        bool active = false;
    };

    class UIWidgets {
    public:
        UIWidgets(UIInput& input, UIRenderer& renderer, UIText& text, burnhope::BurnhopeDevice& device);

        void SetFont(UIFont* font) { m_Font = font; }
        void SetFontSize(float px) { m_FontSizePx = px; }

        void BeginFrame(glm::vec2 screenSize);
        void EndFrame();

        // --- ID stack (mirrors ImGui::PushID/PopID) ---
        void PushID(std::string_view label);
        void PushIDInt(uint64_t id);
        void PopID();
        uint64_t CurrentID(std::string_view label) const;

        // --- Regions ---
        // `id` is a stable scroll key (hash of ID-stack + label). Never hash
        // the pixel rect — dock splits move every frame and that used to
        // reset/snap scroll.
        void BeginRegion(Rect rect, float padding = kRegionPad, bool scroll = true);
        void BeginRegion(std::string_view id, Rect rect, float padding = kRegionPad, bool scroll = true);
        void EndRegion();
        Rect ContentRect() const { return m_Regions.empty() ? Rect{} : m_Regions.back().rect; }
        Rect ClipRect() const { return CurrentClip(); }
        void BeginChild(std::string_view id, Rect rect, bool scroll = true);
        void EndChild();
        void Background(Rect rect, Color color, float radius = 0.0f);
        void Image(VkImageView view, VkSampler sampler, glm::vec2 size,
                   Color tint = {1.0f, 1.0f, 1.0f, 1.0f});
        void ImageAt(Rect rect, VkImageView view, VkSampler sampler, float radius = 0.0f,
                     Color tint = {1.0f, 1.0f, 1.0f, 1.0f},
                     VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        void SetTooltip(std::string_view text);

        void SameLine(float spacing = 8.0f);
        void Separator();
        void Dummy(glm::vec2 size);
        void Indent(float width = 16.0f);
        void Unindent(float width = 16.0f);

        glm::vec2 GetCursor() const { return m_Cursor; }
        void SetCursor(glm::vec2 pos);
        glm::vec2 ContentAvail() const;
        Rect LastItemRect() const { return {m_LastItemPos.x, m_LastItemPos.y, m_LastItemSize.x, m_LastItemSize.y}; }
        float CalcTextWidth(std::string_view text) { return TextWidth(text); }
        bool IsPopupOpen() const { return m_OpenPopupId != 0 || m_OpenMenuId != 0; }
        bool HasTextFocus() const { return m_ActiveTextId != 0; }
        bool HoveringOverlay() const {
            const glm::vec2 m = m_Input.MousePos();
            return m_OverlayHitBlock.w > 0.0f && m_OverlayHitBlock.Contains(m.x, m.y);
        }

        bool TabItem(std::string_view label, bool selected, float height);

        // --- Basic controls ---
        void Text(std::string_view text, Color color = kTheme.text);
        void TextClipped(std::string_view text, float maxWidth, Color color = kTheme.text);
        void TextClippedCentered(std::string_view text, float maxWidth, Color color = kTheme.text);
        void TextColored(Color color, std::string_view text) { Text(text, color); }
        bool Button(std::string_view label, glm::vec2 size = {0, 0});
        bool Checkbox(std::string_view label, bool* value);
        bool InputText(std::string_view label, std::string& value, size_t maxLength = 256, glm::vec2 size = {0, 0});
        bool Selectable(std::string_view label, bool selected, glm::vec2 size = {0, 0});
        bool InvisibleHit(std::string_view id, Rect rect);
        bool SliderFloat(std::string_view id, float* value, float vMin, float vMax, glm::vec2 size = {0, 0});
        void ItemTooltip(std::string_view text);
        bool TreeNode(std::string_view label, bool selected = false, bool leaf = false, bool defaultOpen = false);
        void TreePop();
        bool BeginCombo(std::string_view label, std::string_view previewValue);
        bool ComboItem(std::string_view label, bool selected);
        void EndCombo();

        // --- Property-grid style controls (mirrors UIUtils.h) ---
        bool DrawFloatControl(std::string_view label, float* value, float resetValue = 0.0f, float speed = 0.1f);
        bool DrawVec3Control(std::string_view label, glm::vec3& values, float resetValue = 0.0f);
        bool DrawVec2Control(std::string_view label, glm::vec2& values, float resetValue = 0.0f);
        bool DrawVec4Control(std::string_view label, glm::vec4& values, float resetValue = 0.0f);
        bool DrawColorControl(std::string_view label, glm::vec3& color);
        bool DrawIntControl(std::string_view label, int* value);
        bool DrawCheckboxControl(std::string_view label, bool* value);

        void BeginPropertyGrid();
        void EndPropertyGrid();

        // --- Menus (drawn by UIDockspace's menu bar region) ---
        bool BeginMenuBar();
        void EndMenuBar();
        bool BeginMenu(std::string_view label);
        void EndMenu();
        bool MenuItem(std::string_view label, std::string_view shortcut = "", bool enabled = true, bool checked = false);

        // --- Popups (simple click-to-open / click-away-to-close) ---
        void OpenPopup(std::string_view id);
        bool BeginPopup(std::string_view id, float minWidth = 180.0f);
        void EndPopup();
        void CloseCurrentPopup();

        // --- Drag & drop ---
        bool BeginDragDropSource();
        void SetDragDropPayload(std::string_view type, std::any data);
        void EndDragDropSource();
        bool BeginDragDropTarget();
        const std::any* AcceptDragDropPayload(std::string_view type);
        const std::any* AcceptDragDropOnRect(std::string_view type, Rect rect);
        void EndDragDropTarget();
        bool IsDragDropActive() const { return m_DragPayload.active; }
        const std::any* PeekDragDropPayload(std::string_view type) const {
            if (!m_DragPayload.active || m_DragPayload.type != type) return nullptr;
            return &m_DragPayload.data;
        }

        void AbsorbMouseWheel() { m_WheelAbsorbed = true; }
        enum class MouseCursor : uint8_t { Arrow, EwResize, NsResize };
        void RequestCursor(MouseCursor cursor) { m_CursorRequest = cursor; }

        bool WasItemClicked() const { return m_LastItemClicked; }
        bool WasItemRightClicked() const { return m_LastItemRightClicked; }
        bool IsMouseOverItem() const { return m_LastItemHovered; }
        bool IsItemDoubleClicked() const { return m_LastItemDoubleClicked; }
        float ScrollOffset() const { return m_ScrollOffset; }
        bool KeyPressed(SDL_Scancode key) const { return m_Input.KeyPressed(key); }
        bool Ctrl() const { return m_Input.Ctrl(); }
        bool Shift() const { return m_Input.Shift(); }
        glm::vec2 MousePos() const { return m_Input.MousePos(); }
        bool MouseDown(int button = 0) const { return m_Input.MouseDown(button); }
        bool MouseClicked(int button = 0) const { return m_Input.MouseClicked(button); }
        bool MouseReleased(int button = 0) const { return m_Input.MouseReleased(button); }
        bool MouseRightReleased() const { return m_Input.MouseRightReleased(); }
        float MouseWheel() const { return m_Input.MouseWheel(); }

    private:
        struct RegionState {
            Rect rect;
            Rect clip;
            float padding = kRegionPad;
            uint64_t id = 0;
            bool scroll = true;
            float scrollY = 0.0f;
            float contentH = 0.0f;
        };

        glm::vec2 Cursor() const { return m_Cursor; }
        float LineStartX() const;
        float ScrollbarReserve() const;
        void Advance(glm::vec2 size);
        Rect NextItemRect(glm::vec2 size);
        bool InvisibleButton(uint64_t id, Rect rect);
        void DrawQuad(Rect rect, Color color, float cornerRadius = 0.0f, Rect clipOverride = {});
        void DrawLabel(Rect rect, std::string_view label, Color color);
        float TextWidth(std::string_view text);
        Rect CurrentClip() const;
        bool MouseOver(Rect rect) const;
        std::string_view VisibleLabel(std::string_view label) const;
        bool DragFloatAt(uint64_t id, float* value, float speed, Rect rect, Color badge, std::string_view badgeText);
        Rect PlacePopupRect(float width, float height);
        bool BeginOverlayPanel(Rect rect, uint64_t id);
        void EndOverlayPanel();
        void NoteContentY(float y);
        void DrawScrollbar(const RegionState& st, float contentH, float maxScroll);
        void DrawQueuedOverlays();
        void ApplyCursor();

        UIInput& m_Input;
        UIRenderer& m_Renderer;
        UIText& m_Text;
        burnhope::BurnhopeDevice& m_Device;
        UIFont* m_Font = nullptr;
        float m_FontSizePx = 13.0f;
        glm::vec2 m_ScreenSize{1920.0f, 1080.0f};

        std::vector<uint64_t> m_IDStack;
        std::vector<RegionState> m_Regions;
        std::vector<glm::vec2> m_CursorStack;
        std::vector<float> m_IndentStack;
        std::unordered_map<uint64_t, float> m_ScrollByRegion;
        std::unordered_map<uint64_t, float> m_MaxScrollByRegion;
        uint64_t m_WheelCandidateId = 0;
        glm::vec2 m_Cursor{0, 0};
        float m_RowHeight = kRowHeight;
        float m_IndentLevel = 0.0f;
        glm::vec2 m_LastItemPos{0, 0};
        glm::vec2 m_LastItemSize{0, 0};
        float m_LineMaxH = 0.0f;
        float m_PrevLineMaxH = 0.0f;

        bool m_LastItemClicked = false;
        bool m_LastItemHovered = false;
        bool m_LastItemDoubleClicked = false;
        bool m_LastItemRightClicked = false;
        uint64_t m_LastItemId = 0;

        uint64_t m_OpenPopupId = 0;
        uint64_t m_OpenMenuId = 0;
        bool m_InMenuBar = false;
        bool m_PopupRegionActive = false;
        bool m_MenuRegionActive = false;
        bool m_PopupPlaced = false;
        bool m_PopupJustOpened = false;
        bool m_PopupBegunThisFrame = false;
        glm::vec2 m_PopupAnchor{0, 0};
        glm::vec2 m_PopupAnchorSize{0, 0};
        Rect m_PopupRect{};
        Rect m_OverlayHitBlock{};
        bool m_InOverlay = false;
        std::unordered_map<uint64_t, float> m_PopupHeightById;

        uint64_t m_DragActiveFieldId = 0;
        glm::vec2 m_DragLastMouse{0, 0};
        uint64_t m_DragSourceId = 0;
        glm::vec2 m_DragStartMouse{0, 0};
        std::string m_DragPreview;
        std::string m_DragIcon;
        Color m_DragIconColor = kTheme.button;
        bool m_WheelAbsorbed = false;
        MouseCursor m_CursorRequest = MouseCursor::Arrow;
        std::string m_Tooltip;
        glm::vec2 m_TooltipPos{0, 0};

        DragDropPayload m_DragPayload;
        std::unordered_set<uint64_t> m_OpenTreeNodes;
        std::unordered_set<uint64_t> m_TreeDefaultsApplied;
        uint64_t m_ActiveTextId = 0;
        std::string m_TextEditBuffer;
        float m_ScrollOffset = 0.0f;
        float m_ContentMaxY = 0.0f;
    };
}
