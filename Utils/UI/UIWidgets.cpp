#include "UIWidgets.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <cctype>

namespace burnhope::ui {

UIWidgets::UIWidgets(UIInput& input, UIRenderer& renderer, UIText& text, burnhope::BurnhopeDevice& device)
    : m_Input(input), m_Renderer(renderer), m_Text(text), m_Device(device) {}

void UIWidgets::BeginFrame(glm::vec2 screenSize) {
    m_ScreenSize = screenSize;
    m_IDStack.clear();
    m_Regions.clear();
    m_CursorStack.clear();
    m_IndentStack.clear();
    m_LastItemClicked = false;
    m_LastItemHovered = false;
    m_LastItemDoubleClicked = false;
    m_LastItemRightClicked = false;
    m_LastItemId = 0;
    m_PopupBegunThisFrame = false;
    m_PopupRegionActive = false;
    m_MenuRegionActive = false;
    m_InOverlay = false;
    m_OverlayHitBlock = {};
    m_LineMaxH = 0.0f;
    m_WheelAbsorbed = false;
    m_CursorRequest = MouseCursor::Arrow;
    m_Tooltip.clear();
    m_Input.ClearHot();
    m_Renderer.SetOverlay(false);
}

void UIWidgets::EndFrame() {
    if (m_OpenPopupId != 0 && !m_PopupBegunThisFrame) {
        CloseCurrentPopup();
    }

    DrawQueuedOverlays();
    m_Renderer.SetOverlay(false);

    const glm::vec2 mouse = m_Input.MousePos();
    if (m_OverlayHitBlock.w > 0.0f && m_OverlayHitBlock.Contains(mouse.x, mouse.y)) {
        m_WheelCandidateId = 0;
    }
    if (!m_WheelAbsorbed && m_WheelCandidateId != 0 && m_Input.MouseWheel() != 0.0f) {
        float& y = m_ScrollByRegion[m_WheelCandidateId];
        y -= m_Input.MouseWheel() * 48.0f;
        float maxScroll = 0.0f;
        if (auto it = m_MaxScrollByRegion.find(m_WheelCandidateId); it != m_MaxScrollByRegion.end()) {
            maxScroll = it->second;
        }
        y = Clamp(y, 0.0f, std::max(0.0f, maxScroll));
    }

    if (!m_Input.MouseDown(0)) {
        m_DragPayload.active = false;
        m_DragPayload.data.reset();
        m_DragPayload.type.clear();
        m_DragSourceId = 0;
        m_DragPreview.clear();
    }

    m_WheelCandidateId = 0;
    ApplyCursor();
}

void UIWidgets::PushID(std::string_view label) {
    uint64_t parent = m_IDStack.empty() ? 0 : m_IDStack.back();
    uint64_t id = burnhope::hash::HashString(label, parent ? parent : 0xB00B1E5ULL);
    m_IDStack.push_back(id);
}

void UIWidgets::PushIDInt(uint64_t id) {
    uint64_t parent = m_IDStack.empty() ? 0 : m_IDStack.back();
    m_IDStack.push_back(burnhope::hash::Murmur3_64(&id, sizeof(id), parent));
}

void UIWidgets::PopID() {
    if (!m_IDStack.empty()) m_IDStack.pop_back();
}

uint64_t UIWidgets::CurrentID(std::string_view label) const {
    uint64_t parent = m_IDStack.empty() ? 0 : m_IDStack.back();
    return burnhope::hash::HashString(label, parent ? parent : 0xB00B1E5ULL);
}

float UIWidgets::LineStartX() const {
    if (m_Regions.empty()) return m_IndentLevel;
    const RegionState& r = m_Regions.back();
    return r.rect.x + r.padding + m_IndentLevel;
}

Rect UIWidgets::CurrentClip() const {
    if (m_Regions.empty()) return {0, 0, m_ScreenSize.x, m_ScreenSize.y};
    return m_Regions.back().clip;
}

float UIWidgets::ScrollbarReserve() const {
    if (m_Regions.empty() || !m_Regions.back().scroll) return 0.0f;
    return 12.0f;
}

glm::vec2 UIWidgets::ContentAvail() const {
    if (m_Regions.empty()) return m_ScreenSize;
    const RegionState& r = m_Regions.back();
    return {
        std::max(0.0f, r.rect.x + r.rect.w - r.padding - ScrollbarReserve() - m_Cursor.x),
        std::max(0.0f, r.rect.y + r.rect.h - r.padding - m_Cursor.y)
    };
}

bool UIWidgets::MouseOver(Rect rect) const {
    const glm::vec2 mouse = m_Input.MousePos();
    if (!rect.Contains(mouse.x, mouse.y)) return false;
    if (!CurrentClip().Contains(mouse.x, mouse.y)) return false;
    if (!m_InOverlay && m_OverlayHitBlock.w > 0.0f && m_OverlayHitBlock.Contains(mouse.x, mouse.y)) {
        return false;
    }
    return true;
}

std::string_view UIWidgets::VisibleLabel(std::string_view label) const {
    if (label.size() >= 2 && label[0] == '#' && label[1] == '#') return {};
    auto pos = label.find("##");
    if (pos != std::string_view::npos) return label.substr(0, pos);
    return label;
}

void UIWidgets::Advance(glm::vec2 size) {
    m_LastItemPos = m_Cursor;
    m_LastItemSize = size;
    m_LineMaxH = std::max(m_LineMaxH, size.y);
    m_Cursor.x = LineStartX();
    m_Cursor.y = m_LastItemPos.y + m_LineMaxH + kItemGap;
    m_PrevLineMaxH = m_LineMaxH;
    m_LineMaxH = 0.0f;
    NoteContentY(m_Cursor.y);
}

void UIWidgets::NoteContentY(float y) {
    if (m_Regions.empty()) return;
    RegionState& r = m_Regions.back();
    const float originY = r.rect.y + r.padding - r.scrollY;
    r.contentH = std::max(r.contentH, y - originY);
}

void UIWidgets::SetCursor(glm::vec2 pos) {
    m_Cursor = pos;
    NoteContentY(pos.y);
}

void UIWidgets::BeginRegion(Rect rect, float padding, bool scroll) {
    BeginRegion("##region", rect, padding, scroll);
}

void UIWidgets::BeginRegion(std::string_view id, Rect rect, float padding, bool scroll) {
    m_CursorStack.push_back(m_Cursor);
    m_IndentStack.push_back(m_IndentLevel);

    RegionState st;
    st.rect = rect;
    Rect parentClip = CurrentClip();
    if (m_InOverlay) parentClip = {0, 0, m_ScreenSize.x, m_ScreenSize.y};
    st.clip = rect.Intersect(parentClip);
    st.padding = padding;
    st.id = CurrentID(id);
    st.scroll = scroll;
    st.scrollY = scroll ? m_ScrollByRegion[st.id] : 0.0f;
    st.contentH = 0.0f;

    const glm::vec2 mouse = m_Input.MousePos();
    const bool hovered = rect.Contains(mouse.x, mouse.y)
        && !(m_OverlayHitBlock.w > 0.0f && !m_InOverlay && m_OverlayHitBlock.Contains(mouse.x, mouse.y));
    // Innermost hovered scroll region wins: later nested BeginRegion overwrites.
    if (scroll && hovered) m_WheelCandidateId = st.id;

    m_Regions.push_back(st);
    m_ScrollOffset = st.scrollY;
    m_Cursor = {rect.x + padding, rect.y + padding - st.scrollY};
    m_IndentLevel = 0;
    m_LineMaxH = 0.0f;
}

void UIWidgets::EndRegion() {
    if (m_Regions.empty()) return;
    NoteContentY(m_Cursor.y);
    RegionState st = m_Regions.back();

    const float viewH = std::max(1.0f, st.rect.h - 2.0f * st.padding);
    const float maxScroll = st.scroll ? std::max(0.0f, st.contentH - viewH) : 0.0f;
    m_MaxScrollByRegion[st.id] = maxScroll;
    if (st.scroll) {
        float scrollY = Clamp(st.scrollY, 0.0f, maxScroll);
        m_ScrollByRegion[st.id] = scrollY;
        st.scrollY = scrollY;
        DrawScrollbar(st, st.contentH, maxScroll);
    }

    const float nestedBottom = st.rect.y + st.padding - st.scrollY + st.contentH;

    m_Regions.pop_back();
    m_Cursor = m_CursorStack.back();
    m_IndentLevel = m_IndentStack.back();
    m_CursorStack.pop_back();
    m_IndentStack.pop_back();
    m_LineMaxH = 0.0f;
    m_ScrollOffset = m_Regions.empty() ? 0.0f : m_Regions.back().scrollY;

    if (!m_Regions.empty()) NoteContentY(nestedBottom);
}

void UIWidgets::DrawScrollbar(const RegionState& st, float contentH, float maxScroll) {
    if (maxScroll <= 1.0f) return;
    constexpr float kBarW = 10.0f;
    Rect track{st.rect.x + st.rect.w - kBarW - 1.0f, st.rect.y + 2.0f, kBarW, st.rect.h - 4.0f};
    if (track.h < 8.0f) return;
    DrawQuad(track, Color::RGBA8(12, 12, 14, 180), 4.0f, st.clip);

    const float viewH = std::max(1.0f, st.rect.h - 2.0f * st.padding);
    const float minThumb = std::min(18.0f, track.h);
    float thumbH = Clamp(track.h * (viewH / std::max(viewH, contentH)), minThumb, track.h);
    float t = (maxScroll > 0.0f) ? (st.scrollY / maxScroll) : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    Rect thumb{track.x + 1.0f, track.y + t * (track.h - thumbH), track.w - 2.0f, thumbH};

    uint64_t barId = st.id ^ 0x5C011BULL;
    bool hovered = MouseOver(track);
    if (hovered && m_Input.MouseClicked(0)) m_Input.SetActive(barId);
    if (m_Input.IsActive(barId) && m_Input.MouseDown(0)) {
        float rel = (m_Input.MousePos().y - track.y - thumbH * 0.5f) / std::max(1.0f, track.h - thumbH);
        m_ScrollByRegion[st.id] = Clamp(rel, 0.0f, 1.0f) * maxScroll;
        t = Clamp(rel, 0.0f, 1.0f);
        thumb.y = track.y + t * (track.h - thumbH);
    }
    if (m_Input.MouseReleased(0) && m_Input.IsActive(barId)) m_Input.ClearActive();

    DrawQuad(thumb, hovered || m_Input.IsActive(barId) ? kTheme.buttonHover : kTheme.button, 3.0f, st.clip);
}

void UIWidgets::BeginChild(std::string_view id, Rect rect, bool scroll) {
    PushID(id);
    BeginRegion("##body", rect, kRegionPad, scroll);
}

void UIWidgets::EndChild() {
    EndRegion();
    PopID();
}

void UIWidgets::Background(Rect rect, Color color, float radius) {
    DrawQuad(rect, color, radius);
}

void UIWidgets::Image(VkImageView view, VkSampler sampler, glm::vec2 size, Color tint) {
    Rect rect = NextItemRect(size);
    ImageAt(rect, view, sampler, 4.0f, tint);
    InvisibleButton(CurrentID("##image"), rect);
    Advance(size);
}

void UIWidgets::ImageAt(Rect rect, VkImageView view, VkSampler sampler, float radius, Color tint, VkImageLayout layout) {
    if (!view || rect.w < 1.0f || rect.h < 1.0f) {
        DrawQuad(rect, kTheme.input, radius);
        return;
    }
    UIInstance instance{};
    instance.position = {rect.x, rect.y};
    instance.size = {rect.w, rect.h};
    instance.color = {tint.r, tint.g, tint.b, tint.a};
    instance.mode = DrawMode::Texture;
    instance.cornerRadius = radius;
    instance.textureIndex = m_Renderer.RegisterTexture(view, sampler, layout);
    Rect clip = CurrentClip();
    instance.clipRect = {clip.x, clip.y, clip.w, clip.h};
    m_Renderer.PushInstance(instance);
}

void UIWidgets::SetTooltip(std::string_view text) {
    if (text.empty() || m_DragPayload.active) return;
    m_Tooltip = std::string(text);
    m_TooltipPos = m_Input.MousePos();
}

void UIWidgets::SameLine(float spacing) {
    m_Cursor.x = m_LastItemPos.x + m_LastItemSize.x + spacing;
    m_Cursor.y = m_LastItemPos.y;
    m_LineMaxH = m_PrevLineMaxH;
}

Rect UIWidgets::NextItemRect(glm::vec2 size) {
    Rect r{m_Cursor.x, m_Cursor.y, size.x, size.y};
    if (r.w <= 0 && !m_Regions.empty()) {
        const RegionState& rg = m_Regions.back();
        r.w = std::max(8.0f, rg.rect.x + rg.rect.w - r.x - rg.padding - ScrollbarReserve());
    }
    if (r.h <= 0) r.h = m_RowHeight;
    return r;
}

void UIWidgets::DrawQuad(Rect rect, Color color, float cornerRadius, Rect clipOverride) {
    UIInstance inst{};
    inst.position = {rect.x, rect.y};
    inst.size = {rect.w, rect.h};
    inst.color = {color.r, color.g, color.b, color.a};
    inst.mode = DrawMode::SolidColor;
    inst.cornerRadius = cornerRadius;
    Rect clip = (clipOverride.w > 0 && clipOverride.h > 0) ? clipOverride : CurrentClip();
    inst.clipRect = {clip.x, clip.y, clip.w, clip.h};
    m_Renderer.PushInstance(inst);
}

float UIWidgets::TextWidth(std::string_view text) {
    if (!m_Font) return text.size() * m_FontSizePx * 0.55f;
    return m_Text.Measure(m_Font, text, m_FontSizePx).x;
}

void UIWidgets::DrawLabel(Rect rect, std::string_view label, Color color) {
    if (!m_Font || label.empty()) return;
    Rect clip = CurrentClip().Intersect(rect);
    if (clip.w <= 1.0f || clip.h <= 1.0f) return;
    float baseline = rect.y + (rect.h + m_FontSizePx * 0.72f) * 0.5f;
    m_Text.DrawText(m_Renderer, m_Device, m_Font, label,
                     {rect.x, baseline}, m_FontSizePx, color, clip);
}

void UIWidgets::Text(std::string_view text, Color color) {
    std::string_view vis = VisibleLabel(text);
    Rect r = NextItemRect({TextWidth(vis) + 2.0f, m_RowHeight});
    DrawLabel(r, vis, color);
    Advance({r.w, r.h});
}

void UIWidgets::TextClipped(std::string_view text, float maxWidth, Color color) {
    Rect r = NextItemRect({maxWidth, m_RowHeight});
    DrawLabel(r, VisibleLabel(text), color);
    Advance({r.w, r.h});
}

void UIWidgets::TextClippedCentered(std::string_view text, float maxWidth, Color color) {
    std::string_view vis = VisibleLabel(text);
    Rect r = NextItemRect({maxWidth, m_RowHeight});
    float tw = std::min(TextWidth(vis), maxWidth);
    DrawLabel({r.x + (r.w - tw) * 0.5f, r.y, tw, r.h}, vis, color);
    Advance({r.w, r.h});
}

void UIWidgets::Separator() {
    Rect region = m_Regions.empty() ? Rect{} : m_Regions.back().rect;
    float pad = m_Regions.empty() ? kRegionPad : m_Regions.back().padding;
    Rect r = NextItemRect({region.w - (m_Cursor.x - region.x) - pad, 1.0f});
    DrawQuad(r, kTheme.separator);
    Advance({r.w, 6.0f});
}

void UIWidgets::Dummy(glm::vec2 size) { Advance(size); }
void UIWidgets::Indent(float width) { m_IndentLevel += width; m_Cursor.x += width; }
void UIWidgets::Unindent(float width) {
    m_IndentLevel = std::max(0.0f, m_IndentLevel - width);
    m_Cursor.x = LineStartX();
}

bool UIWidgets::InvisibleButton(uint64_t id, Rect rect) {
    m_LastItemId = id;
    m_LastItemPos = {rect.x, rect.y};
    m_LastItemSize = {rect.w, rect.h};

    bool hovered = MouseOver(rect);
    bool clicked = false;
    bool rightClicked = false;
    if (hovered) {
        m_Input.SetHot(id);
        if (m_Input.MouseClicked(0) && !m_DragPayload.active) {
            m_Input.SetActive(id);
            m_DragStartMouse = m_Input.MousePos();
            clicked = true;
        }
        rightClicked = m_Input.MouseRightReleased();
    }
    if (m_Input.IsActive(id) && m_Input.MouseReleased(0)) {
        m_Input.ClearActive();
    }
    m_LastItemHovered = hovered;
    m_LastItemClicked = clicked;
    m_LastItemDoubleClicked = hovered && m_Input.MouseDoubleClicked(0);
    m_LastItemRightClicked = rightClicked;
    return clicked;
}

bool UIWidgets::InvisibleHit(std::string_view id, Rect rect) {
    return InvisibleButton(CurrentID(id), rect);
}

bool UIWidgets::SliderFloat(std::string_view id, float* value, float vMin, float vMax, glm::vec2 size) {
    glm::vec2 sz = size;
    if (sz.x <= 0.0f) sz.x = 96.0f;
    if (sz.y <= 0.0f) sz.y = 16.0f;
    Rect r = NextItemRect(sz);
    uint64_t sid = CurrentID(id);
    InvisibleButton(sid, r);
    DrawQuad(r, kTheme.input, 4.0f);

    const float span = std::max(0.0001f, vMax - vMin);
    float t = Clamp((*value - vMin) / span, 0.0f, 1.0f);
    const float thumbW = 12.0f;
    bool changed = false;
    if (m_Input.IsActive(sid) && m_Input.MouseDown(0)) {
        float rel = (m_Input.MousePos().x - r.x) / std::max(1.0f, r.w);
        float next = vMin + Clamp(rel, 0.0f, 1.0f) * span;
        changed = next != *value;
        *value = next;
        t = Clamp(rel, 0.0f, 1.0f);
    }
    Rect thumb{r.x + t * std::max(0.0f, r.w - thumbW), r.y, thumbW, r.h};
    DrawQuad(thumb, m_Input.IsHot(sid) || m_Input.IsActive(sid) ? kTheme.accent : kTheme.button, 3.0f);
    Advance({r.w, r.h});
    return changed;
}

void UIWidgets::ItemTooltip(std::string_view text) {
    if (!m_LastItemHovered || text.empty() || m_DragPayload.active) return;
    m_Tooltip = std::string(text);
    m_TooltipPos = m_Input.MousePos();
}

bool UIWidgets::Button(std::string_view label, glm::vec2 size) {
    uint64_t id = CurrentID(label);
    glm::vec2 sz = size;
    if (sz.x <= 0) sz.x = TextWidth(label) + 18.0f;
    if (sz.y <= 0) sz.y = m_RowHeight;
    Rect r = NextItemRect(sz);

    bool clicked = InvisibleButton(id, r);
    Color bg = m_Input.IsActive(id) ? kTheme.buttonActive
             : (m_Input.IsHot(id) ? kTheme.buttonHover : kTheme.button);
    DrawQuad(r, bg, 4.0f);
    std::string_view vis = VisibleLabel(label);
    float textW = TextWidth(vis);
    DrawLabel({r.x + (r.w - textW) * 0.5f, r.y, std::min(textW, r.w), r.h}, vis, kTheme.text);

    Advance({r.w, r.h});
    return clicked;
}

bool UIWidgets::TabItem(std::string_view label, bool selected, float height) {
    uint64_t id = CurrentID(label);
    float w = TextWidth(label) + 22.0f;
    Rect r = NextItemRect({w, height});
    bool clicked = InvisibleButton(id, r);
    Color bg = selected ? kTheme.tabActive
             : (m_Input.IsHot(id) ? kTheme.tabHover : Color{0, 0, 0, 0});
    DrawQuad(r, bg, 3.0f);
    if (selected) {
        DrawQuad({r.x + 6.0f, r.y + r.h - 3.0f, r.w - 12.0f, 2.0f}, kTheme.accent);
    }
    DrawLabel({r.x + 11.0f, r.y, r.w - 12.0f, r.h}, label, selected ? kTheme.text : kTheme.textMuted);
    Advance({r.w, r.h});
    return clicked;
}

bool UIWidgets::Checkbox(std::string_view label, bool* value) {
    uint64_t id = CurrentID(label);
    float labelW = TextWidth(label);
    Rect row = NextItemRect({16.0f + 8.0f + labelW, m_RowHeight});
    bool clicked = InvisibleButton(id, row);
    if (clicked) *value = !*value;

    Rect box{row.x, row.y + (row.h - 16.0f) * 0.5f, 16.0f, 16.0f};
    DrawQuad(box, *value ? kTheme.accent : kTheme.input, 3.0f);
    if (*value) DrawQuad({box.x + 4, box.y + 4, 8, 8}, kTheme.text, 1.0f);
    DrawLabel({box.x + 22.0f, row.y, labelW, row.h}, label, kTheme.text);

    Advance({row.w, row.h});
    return clicked;
}

bool UIWidgets::InputText(std::string_view label, std::string& value, size_t maxLength, glm::vec2 size) {
    const uint64_t id = CurrentID(label);
    glm::vec2 sz = size;
    if (sz.y <= 0) sz.y = m_RowHeight;
    Rect r = NextItemRect(sz);
    bool clicked = InvisibleButton(id, r);
    if (clicked) {
        m_ActiveTextId = id;
        m_TextEditBuffer = value;
    }

    if (m_ActiveTextId == id) {
        if (m_Input.KeyPressed(SDL_SCANCODE_ESCAPE)) {
            m_ActiveTextId = 0;
            m_TextEditBuffer.clear();
        } else {
            if (m_Input.KeyPressed(SDL_SCANCODE_BACKSPACE) && !m_TextEditBuffer.empty()) {
                m_TextEditBuffer.pop_back();
            }
            if (!m_Input.TextInput().empty() && m_TextEditBuffer.size() < maxLength) {
                m_TextEditBuffer.append(m_Input.TextInput());
                if (m_TextEditBuffer.size() > maxLength) m_TextEditBuffer.resize(maxLength);
            }
            value = m_TextEditBuffer;
            if (m_Input.KeyPressed(SDL_SCANCODE_RETURN) ||
                (m_Input.MouseClicked(0) && !m_LastItemHovered)) {
                m_ActiveTextId = 0;
                m_TextEditBuffer.clear();
            }
        }
    }

    bool focused = m_ActiveTextId == id;
    DrawQuad(r, kTheme.input, 3.0f);
    if (focused) {
        DrawQuad({r.x, r.y + r.h - 2.0f, r.w, 2.0f}, kTheme.accent);
    }
    const std::string& shown = focused ? m_TextEditBuffer : value;
    std::string_view placeholder = VisibleLabel(label);
    DrawLabel({r.x + 8, r.y, r.w - 16, r.h}, shown.empty() ? placeholder : shown,
              shown.empty() ? kTheme.textMuted : kTheme.text);
    Advance({r.w, r.h});
    return clicked;
}

bool UIWidgets::Selectable(std::string_view label, bool selected, glm::vec2 size) {
    uint64_t id = CurrentID(label);
    glm::vec2 sz = size;
    if (sz.y <= 0) sz.y = m_RowHeight;
    Rect r = NextItemRect(sz);

    bool clicked = InvisibleButton(id, r);
    if (selected) DrawQuad(r, kTheme.rowSelected, 3.0f);
    else if (m_Input.IsHot(id)) DrawQuad(r, kTheme.rowHover, 3.0f);
    DrawLabel({r.x + 8, r.y, std::max(0.0f, r.w - 12), r.h}, VisibleLabel(label), kTheme.text);

    Advance({r.w, r.h});
    return clicked;
}

bool UIWidgets::TreeNode(std::string_view label, bool selected, bool leaf, bool defaultOpen) {
    uint64_t id = CurrentID(label);
    Rect r = NextItemRect({0, m_RowHeight});

    bool clicked = InvisibleButton(id, r);
    glm::vec2 mouse = m_Input.MousePos();
    bool onArrow = mouse.x < r.x + 18.0f;
    if (defaultOpen && m_TreeDefaultsApplied.insert(id).second) {
        m_OpenTreeNodes.insert(id);
    }
    const bool wasOpen = m_OpenTreeNodes.contains(id);
    if (!leaf && clicked && onArrow) {
        if (wasOpen) m_OpenTreeNodes.erase(id);
        else m_OpenTreeNodes.insert(id);
    }
    if (!leaf && m_LastItemDoubleClicked) {
        if (wasOpen) m_OpenTreeNodes.erase(id);
        else m_OpenTreeNodes.insert(id);
    }

    if (selected) DrawQuad(r, kTheme.rowSelected, 3.0f);
    else if (m_Input.IsHot(id)) DrawQuad(r, kTheme.rowHover, 3.0f);

    const bool open = !leaf && m_OpenTreeNodes.contains(id);
    if (!leaf) {
        DrawLabel({r.x + 4, r.y, 14, r.h}, open ? "v" : ">", kTheme.textMuted);
    }
    DrawLabel({r.x + 18, r.y, std::max(0.0f, r.w - 22), r.h}, label, kTheme.text);

    Advance({r.w, r.h});
    if (open) {
        PushIDInt(id);
        Indent(14.0f);
    }
    return open;
}

void UIWidgets::TreePop() {
    Unindent(14.0f);
    PopID();
}

bool UIWidgets::BeginCombo(std::string_view label, std::string_view previewValue) {
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    Rect field{row.x + labelW, row.y, std::max(48.0f, row.w - labelW), row.h};
    uint64_t id = CurrentID("##combo");
    bool clicked = InvisibleButton(id, field);
    DrawQuad(field, kTheme.input, 3.0f);
    DrawLabel({field.x + 8, field.y, field.w - 24, field.h}, previewValue, kTheme.text);
    DrawLabel({field.x + field.w - 16, field.y, 14, field.h}, "v", kTheme.textMuted);
    m_LastItemPos = {field.x, field.y};
    m_LastItemSize = {field.w, field.h};
    Advance({row.w, row.h});
    if (clicked) OpenPopup("##combo");
    bool open = BeginPopup("##combo");
    if (!open) PopID();
    return open;
}

bool UIWidgets::ComboItem(std::string_view label, bool selected) {
    return Selectable(label, selected);
}

void UIWidgets::EndCombo() {
    EndPopup();
    PopID();
}

bool UIWidgets::DragFloatAt(uint64_t id, float* value, float speed, Rect rect, Color badge, std::string_view badgeText) {
    bool hovered = MouseOver(rect);
    if (hovered && m_Input.MouseClicked(0)) {
        m_Input.SetActive(id);
        m_DragActiveFieldId = id;
        m_DragLastMouse = m_Input.MousePos();
    }
    if (hovered && m_Input.MouseDoubleClicked(0)) {
        *value = 0.0f;
        m_Input.ClearActive();
        m_DragActiveFieldId = 0;
        DrawQuad(rect, kTheme.input, 3.0f);
        return true;
    }
    bool active = m_Input.IsActive(id) && m_DragActiveFieldId == id;
    bool changed = false;
    if (active) {
        if (m_Input.MouseDown(0)) {
            float delta = m_Input.MousePos().x - m_DragLastMouse.x;
            if (delta != 0.0f) {
                *value += delta * speed;
                m_DragLastMouse = m_Input.MousePos();
                changed = true;
            }
        }
        if (m_Input.MouseReleased(0)) { m_Input.ClearActive(); m_DragActiveFieldId = 0; }
    }

    DrawQuad(rect, active ? kTheme.buttonActive : (hovered ? kTheme.buttonHover : kTheme.input), 3.0f);
    if (!badgeText.empty()) {
        Rect badgeRect{rect.x, rect.y, 16.0f, rect.h};
        DrawQuad(badgeRect, badge, 3.0f);
        DrawLabel({badgeRect.x + 4, badgeRect.y, 12, badgeRect.h}, badgeText, kTheme.text);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", *value);
    float textPad = badgeText.empty() ? 6.0f : 20.0f;
    DrawLabel({rect.x + textPad, rect.y, rect.w - textPad - 4, rect.h}, buf, kTheme.text);
    return changed;
}

bool UIWidgets::DrawFloatControl(std::string_view label, float* value, float resetValue, float speed) {
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    Rect field{row.x + labelW, row.y, std::max(48.0f, row.w - labelW), row.h};
    bool changed = DragFloatAt(CurrentID("##drag"), value, speed, field, {}, "");
    if (m_LastItemDoubleClicked) { *value = resetValue; changed = true; }
    Advance({row.w, row.h});
    PopID();
    return changed;
}

bool UIWidgets::DrawVec3Control(std::string_view label, glm::vec3& values, float resetValue) {
    (void)resetValue;
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    float avail = std::max(60.0f, row.w - labelW);
    float gap = 4.0f;
    float colW = (avail - 2.0f * gap) / 3.0f;
    const char* axes[] = {"X", "Y", "Z"};
    Color colors[] = {kTheme.axisX, kTheme.axisY, kTheme.axisZ};
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        Rect cell{row.x + labelW + i * (colW + gap), row.y, colW, row.h};
        changed |= DragFloatAt(CurrentID(axes[i]), &values[i], 0.1f, cell, colors[i], axes[i]);
    }
    Advance({row.w, row.h});
    PopID();
    return changed;
}

bool UIWidgets::DrawVec2Control(std::string_view label, glm::vec2& values, float resetValue) {
    (void)resetValue;
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    float avail = std::max(40.0f, row.w - labelW);
    float gap = 4.0f;
    float colW = (avail - gap) / 2.0f;
    const char* axes[] = {"X", "Y"};
    Color colors[] = {kTheme.axisX, kTheme.axisY};
    bool changed = false;
    for (int i = 0; i < 2; ++i) {
        Rect cell{row.x + labelW + i * (colW + gap), row.y, colW, row.h};
        changed |= DragFloatAt(CurrentID(axes[i]), &values[i], 0.1f, cell, colors[i], axes[i]);
    }
    Advance({row.w, row.h});
    PopID();
    return changed;
}

bool UIWidgets::DrawVec4Control(std::string_view label, glm::vec4& values, float resetValue) {
    (void)resetValue;
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    float avail = std::max(80.0f, row.w - labelW);
    float gap = 3.0f;
    float colW = (avail - 3.0f * gap) / 4.0f;
    const char* axes[] = {"X", "Y", "Z", "W"};
    Color colors[] = {kTheme.axisX, kTheme.axisY, kTheme.axisZ, kTheme.axisW};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        Rect cell{row.x + labelW + i * (colW + gap), row.y, colW, row.h};
        changed |= DragFloatAt(CurrentID(axes[i]), &values[i], 0.1f, cell, colors[i], axes[i]);
    }
    Advance({row.w, row.h});
    PopID();
    return changed;
}

bool UIWidgets::DrawColorControl(std::string_view label, glm::vec3& color) {
    PushID(label);
    Rect row = NextItemRect({0, m_RowHeight});
    const float labelW = 108.0f;
    DrawLabel({row.x, row.y, labelW, row.h}, label, kTheme.textMuted);
    Rect swatch{row.x + labelW, row.y, 28.0f, row.h};
    uint64_t id = CurrentID("##swatch");
    bool clicked = InvisibleButton(id, swatch);
    DrawQuad(swatch, Color{color.r, color.g, color.b, 1.0f}, 3.0f);

    float avail = std::max(60.0f, row.w - labelW - 32.0f);
    float gap = 4.0f;
    float colW = (avail - 2.0f * gap) / 3.0f;
    const char* axes[] = {"R", "G", "B"};
    Color colors[] = {kTheme.axisX, kTheme.axisY, kTheme.axisZ};
    bool changed = clicked;
    for (int i = 0; i < 3; ++i) {
        Rect cell{swatch.x + 32.0f + i * (colW + gap), row.y, colW, row.h};
        changed |= DragFloatAt(CurrentID(axes[i]), &color[i], 0.005f, cell, colors[i], axes[i]);
        color[i] = Clamp(color[i], 0.0f, 1.0f);
    }
    Advance({row.w, row.h});
    PopID();
    return changed;
}

bool UIWidgets::DrawIntControl(std::string_view label, int* value) {
    float f = static_cast<float>(*value);
    bool changed = DrawFloatControl(label, &f, 0.0f, 0.25f);
    if (changed) *value = static_cast<int>(std::lround(f));
    return changed;
}

bool UIWidgets::DrawCheckboxControl(std::string_view label, bool* value) {
    return Checkbox(label, value);
}

void UIWidgets::BeginPropertyGrid() { Indent(2.0f); }
void UIWidgets::EndPropertyGrid() { Unindent(2.0f); }

bool UIWidgets::BeginMenuBar() { m_InMenuBar = true; return true; }
void UIWidgets::EndMenuBar() { m_InMenuBar = false; }

Rect UIWidgets::PlacePopupRect(float width, float height) {
    Rect popup{m_PopupAnchor.x, m_PopupAnchor.y + m_PopupAnchorSize.y + 2.0f, width, height};
    if (m_PopupAnchorSize.x <= 0.0f && m_PopupAnchorSize.y <= 0.0f) {
        popup.x = m_PopupAnchor.x;
        popup.y = m_PopupAnchor.y;
    }
    if (popup.x + popup.w > m_ScreenSize.x - 8.0f) popup.x = m_ScreenSize.x - popup.w - 8.0f;
    if (popup.y + popup.h > m_ScreenSize.y - 8.0f) {
        float above = m_PopupAnchor.y - height - 2.0f;
        popup.y = above > 8.0f ? above : m_ScreenSize.y - popup.h - 8.0f;
    }
    if (popup.x < 8.0f) popup.x = 8.0f;
    if (popup.y < 8.0f) popup.y = 8.0f;
    return popup;
}

bool UIWidgets::BeginOverlayPanel(Rect rect, uint64_t id) {
    (void)id;
    m_InOverlay = true;
    m_OverlayHitBlock = rect;
    m_Renderer.SetOverlay(true);
    Rect screen{0, 0, m_ScreenSize.x, m_ScreenSize.y};
    DrawQuad({rect.x + 3, rect.y + 4, rect.w, rect.h}, kTheme.shadow, 6.0f, screen);
    DrawQuad(rect, kTheme.popup, 6.0f, screen);
    DrawQuad({rect.x, rect.y, rect.w, 1.0f}, kTheme.accent, 0.0f, screen);
    BeginRegion("##overlay", rect, 6.0f, false);
    return true;
}

void UIWidgets::EndOverlayPanel() {
    EndRegion();
    m_InOverlay = false;
    m_Renderer.SetOverlay(false);
}

bool UIWidgets::BeginMenu(std::string_view label) {
    uint64_t id = CurrentID(label);
    glm::vec2 sz{TextWidth(label) + 20.0f, m_RowHeight};
    Rect r = NextItemRect(sz);
    bool clicked = InvisibleButton(id, r);
    DrawQuad(r, m_OpenMenuId == id ? kTheme.tabActive : (m_Input.IsHot(id) ? kTheme.tabHover : Color{0, 0, 0, 0}), 3.0f);
    DrawLabel({r.x + 10, r.y, r.w, r.h}, label, kTheme.text);
    Advance({r.w, r.h});
    if (m_InMenuBar) SameLine(2.0f);

    if (clicked) m_OpenMenuId = (m_OpenMenuId == id) ? 0 : id;
    if (m_OpenMenuId != id) return false;

    m_PopupAnchor = {r.x, r.y};
    m_PopupAnchorSize = {r.w, r.h};
    float height = m_PopupHeightById.count(id) ? m_PopupHeightById[id] : 160.0f;
    Rect menuRect = PlacePopupRect(220.0f, height);
    m_PopupRect = menuRect;
    m_PopupBegunThisFrame = true;

    glm::vec2 mouse = m_Input.MousePos();
    bool overTitle = r.Contains(mouse.x, mouse.y);
    bool overMenu = menuRect.Contains(mouse.x, mouse.y);
    if (!m_PopupJustOpened && m_Input.MouseClicked(0) && !overTitle && !overMenu) {
        m_OpenMenuId = 0;
        return false;
    }
    m_PopupJustOpened = false;

    BeginOverlayPanel(menuRect, id);
    m_MenuRegionActive = true;
    return true;
}

void UIWidgets::EndMenu() {
    if (m_MenuRegionActive) {
        float h = Clamp(m_Cursor.y - m_PopupRect.y + 8.0f, 36.0f, 420.0f);
        m_PopupHeightById[m_OpenMenuId] = h;
        EndOverlayPanel();
        m_MenuRegionActive = false;
    }
}

bool UIWidgets::MenuItem(std::string_view label, std::string_view shortcut, bool enabled, bool checked) {
    if (!enabled) {
        Text(label, kTheme.textMuted);
        return false;
    }
    std::string shown = checked ? std::string("* ") + std::string(label) : std::string(label);
    bool clicked = Selectable(shown, checked);
    if (!shortcut.empty() && m_LastItemSize.x > 80.0f) {
        Rect last{m_LastItemPos.x, m_LastItemPos.y, m_LastItemSize.x, m_LastItemSize.y};
        float sw = TextWidth(shortcut);
        DrawLabel({last.x + last.w - sw - 10, last.y, sw, last.h}, shortcut, kTheme.textMuted);
    }
    if (clicked) {
        m_OpenMenuId = 0;
        CloseCurrentPopup();
    }
    return clicked;
}

void UIWidgets::OpenPopup(std::string_view id) {
    uint64_t pid = CurrentID(id);
    m_OpenPopupId = pid;
    m_PopupPlaced = false;
    m_PopupJustOpened = true;
    if (m_LastItemRightClicked || m_Input.MouseRightReleased()) {
        m_PopupAnchor = m_Input.MousePos();
        m_PopupAnchorSize = {0, 0};
    } else {
        m_PopupAnchor = m_LastItemPos;
        m_PopupAnchorSize = m_LastItemSize;
        if (m_PopupAnchorSize.x <= 1.0f && m_PopupAnchorSize.y <= 1.0f) {
            m_PopupAnchor = m_Input.MousePos();
            m_PopupAnchorSize = {0, 0};
        }
    }
}

bool UIWidgets::BeginPopup(std::string_view id, float minWidth) {
    uint64_t pid = CurrentID(id);
    if (m_OpenPopupId != pid) return false;
    m_PopupBegunThisFrame = true;

    float width = std::max(minWidth, m_PopupAnchorSize.x);
    float height = m_PopupHeightById.count(pid) ? m_PopupHeightById[pid] : 120.0f;
    height = Clamp(height, 40.0f, 480.0f);

    if (!m_PopupPlaced) {
        m_PopupRect = PlacePopupRect(width, height);
        m_PopupPlaced = true;
    } else {
        m_PopupRect.w = width;
        m_PopupRect.h = height;
        // Keep x/y from the frame it opened — never follow the mouse.
        if (m_PopupRect.x + m_PopupRect.w > m_ScreenSize.x - 8.0f) {
            m_PopupRect.x = m_ScreenSize.x - m_PopupRect.w - 8.0f;
        }
    }

    glm::vec2 mouse = m_Input.MousePos();
    if (!m_PopupJustOpened && m_Input.MouseClicked(0) && !m_PopupRect.Contains(mouse.x, mouse.y)) {
        CloseCurrentPopup();
        return false;
    }
    if (m_Input.KeyPressed(SDL_SCANCODE_ESCAPE)) {
        CloseCurrentPopup();
        return false;
    }
    m_PopupJustOpened = false;

    BeginOverlayPanel(m_PopupRect, pid);
    m_PopupRegionActive = true;
    return true;
}

void UIWidgets::EndPopup() {
    if (m_PopupRegionActive) {
        float h = Clamp(m_Cursor.y - m_PopupRect.y + 8.0f, 40.0f, 480.0f);
        m_PopupHeightById[m_OpenPopupId] = h;
        EndOverlayPanel();
        m_PopupRegionActive = false;
    }
}

void UIWidgets::CloseCurrentPopup() {
    m_OpenPopupId = 0;
    m_PopupPlaced = false;
    m_PopupJustOpened = false;
    m_PopupRegionActive = false;
}

bool UIWidgets::BeginDragDropSource() {
    if (m_LastItemId == 0) return false;
    if (!m_Input.MouseDown(0)) return false;

    const bool holdingThis = m_Input.IsActive(m_LastItemId) || m_DragSourceId == m_LastItemId;
    if (!holdingThis) return m_DragPayload.active && m_DragSourceId == m_LastItemId;

    if (!m_DragPayload.active) {
        glm::vec2 d = m_Input.MousePos() - m_DragStartMouse;
        if (d.x * d.x + d.y * d.y <= 36.0f) return false;
        m_DragPayload.active = true;
        m_DragSourceId = m_LastItemId;
    }
    return m_DragSourceId == m_LastItemId;
}

void UIWidgets::SetDragDropPayload(std::string_view type, std::any data) {
    m_DragPayload.type = std::string(type);
    m_DragPayload.data = std::move(data);
    m_DragPayload.active = true;
    m_DragPreview.clear();
    m_DragIcon = "FILE";
    m_DragIconColor = kTheme.button;
    if (const std::string* path = std::any_cast<std::string>(&m_DragPayload.data)) {
        std::filesystem::path p(*path);
        m_DragPreview = p.filename().string();
        bool isDir = std::filesystem::is_directory(p);
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (isDir) {
            m_DragIcon = "DIR";
            m_DragIconColor = Color::RGBA8(92, 168, 230);
        } else if (ext == ".bhmat" || ext == ".json") {
            m_DragIcon = "MAT";
            m_DragIconColor = Color::RGBA8(210, 120, 70);
        } else if (ext == ".bhmesh" || ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
            m_DragIcon = "MESH";
            m_DragIconColor = Color::RGBA8(90, 190, 170);
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
            m_DragIcon = "TEX";
            m_DragIconColor = Color::RGBA8(180, 110, 200);
        } else if (ext == ".bhscene") {
            m_DragIcon = "SCN";
            m_DragIconColor = Color::RGBA8(120, 200, 110);
        } else if (!ext.empty()) {
            m_DragIcon = ext.front() == '.' ? ext.substr(1, 4) : ext.substr(0, 4);
            for (char& c : m_DragIcon) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
}

void UIWidgets::EndDragDropSource() {}

bool UIWidgets::BeginDragDropTarget() {
    if (!m_DragPayload.active) return false;
    const glm::vec2 mouse = m_Input.MousePos();
    Rect last = LastItemRect();
    if (!m_LastItemHovered && !last.Contains(mouse.x, mouse.y)) return false;
    DrawQuad(last, Color::RGBA8(56, 132, 230, 70), 3.0f);
    return true;
}

const std::any* UIWidgets::AcceptDragDropPayload(std::string_view type) {
    if (!m_DragPayload.active || m_DragPayload.type != type) return nullptr;
    if (!m_Input.MouseReleased(0)) return nullptr;
    return &m_DragPayload.data;
}

const std::any* UIWidgets::AcceptDragDropOnRect(std::string_view type, Rect rect) {
    if (!m_DragPayload.active || m_DragPayload.type != type) return nullptr;
    const glm::vec2 mouse = m_Input.MousePos();
    if (!rect.Contains(mouse.x, mouse.y)) return nullptr;
    if (!m_Input.MouseReleased(0)) {
        DrawQuad(rect, Color::RGBA8(56, 132, 230, 40), 0.0f);
        return nullptr;
    }
    return &m_DragPayload.data;
}

void UIWidgets::EndDragDropTarget() {}

void UIWidgets::DrawQueuedOverlays() {
    m_Renderer.SetOverlay(true);
    const glm::vec2 mouse = m_Input.MousePos();
    Rect screen{0, 0, m_ScreenSize.x, m_ScreenSize.y};

    if (m_DragPayload.active) {
        constexpr float kIcon = 28.0f;
        float textW = m_DragPreview.empty() ? 0.0f : TextWidth(m_DragPreview) + 10.0f;
        Rect icon{mouse.x + 14.0f, mouse.y + 16.0f, kIcon, kIcon};
        Rect tip{icon.x + kIcon + 4.0f, icon.y + 3.0f, std::max(8.0f, textW), 22.0f};
        DrawQuad({icon.x + 2, icon.y + 3, icon.w, icon.h}, kTheme.shadow, 5.0f, screen);
        DrawQuad(icon, m_DragIconColor, 5.0f, screen);
        DrawLabel({icon.x, icon.y, icon.w, icon.h}, m_DragIcon, kTheme.text);
        if (!m_DragPreview.empty()) {
            DrawQuad({tip.x + 2, tip.y + 3, tip.w, tip.h}, kTheme.shadow, 4.0f, screen);
            DrawQuad(tip, kTheme.popup, 4.0f, screen);
            DrawLabel({tip.x + 6, tip.y, tip.w - 8, tip.h}, m_DragPreview, kTheme.text);
        }
    } else if (!m_Tooltip.empty()) {
        float w = std::min(420.0f, TextWidth(m_Tooltip) + 16.0f);
        Rect tip{m_TooltipPos.x + 12.0f, m_TooltipPos.y + 18.0f, w, 22.0f};
        if (tip.x + tip.w > m_ScreenSize.x - 6.0f) tip.x = m_ScreenSize.x - tip.w - 6.0f;
        if (tip.y + tip.h > m_ScreenSize.y - 6.0f) tip.y = m_TooltipPos.y - tip.h - 8.0f;
        DrawQuad({tip.x + 2, tip.y + 3, tip.w, tip.h}, kTheme.shadow, 4.0f, screen);
        DrawQuad(tip, kTheme.popup, 4.0f, screen);
        DrawLabel({tip.x + 8, tip.y, tip.w - 12, tip.h}, m_Tooltip, kTheme.text);
    }
    m_Tooltip.clear();
}

void UIWidgets::ApplyCursor() {
    static SDL_Cursor* arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    static SDL_Cursor* ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    static SDL_Cursor* ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    SDL_Cursor* cur = arrow;
    if (m_CursorRequest == MouseCursor::EwResize) cur = ew;
    else if (m_CursorRequest == MouseCursor::NsResize) cur = ns;
    if (cur) SDL_SetCursor(cur);
}

} // namespace burnhope::ui
