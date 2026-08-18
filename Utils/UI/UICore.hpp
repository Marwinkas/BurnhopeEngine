#pragma once
#include <cstdint>
#include <algorithm>
#include <glm/glm.hpp>

// Shared primitive types for the custom editor UI stack (replaces ImGui).
// Kept intentionally small and POD so it can be pushed straight into a GPU
// instance buffer (see UIRenderer::UIInstance below).
namespace burnhope::ui {

    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;

        bool Contains(float px, float py) const {
            return px >= x && py >= y && px < x + w && py < y + h;
        }

        Rect Intersect(const Rect& o) const {
            float x1 = std::max(x, o.x);
            float y1 = std::max(y, o.y);
            float x2 = std::min(x + w, o.x + o.w);
            float y2 = std::min(y + h, o.y + o.h);
            return {x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
        }

        bool Overlaps(const Rect& o) const {
            return Intersect(o).w > 0.0f && Intersect(o).h > 0.0f;
        }
    };

    inline float Clamp(float v, float lo, float hi) {
        if (hi < lo) std::swap(hi, lo);
        return std::min(hi, std::max(lo, v));
    }

    struct Color {
        float r = 1, g = 1, b = 1, a = 1;

        static constexpr Color RGBA8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) {
            return {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, a8 / 255.0f};
        }
    };

    // Shared editor chrome. Widgets, dockspace and modals all read this so
    // panels don't each invent their own gray.
    struct Theme {
        Color bg             = Color::RGBA8(18, 19, 22);
        Color panel          = Color::RGBA8(30, 31, 36);
        Color panelInner     = Color::RGBA8(36, 37, 43);
        Color title          = Color::RGBA8(22, 23, 27);
        Color tabActive      = Color::RGBA8(44, 46, 54);
        Color tabHover       = Color::RGBA8(40, 42, 50);
        Color menuBar        = Color::RGBA8(20, 21, 25);
        Color button         = Color::RGBA8(52, 54, 63);
        Color buttonHover    = Color::RGBA8(68, 72, 86);
        Color buttonActive   = Color::RGBA8(40, 108, 198);
        Color input          = Color::RGBA8(20, 21, 26);
        Color rowHover       = Color::RGBA8(48, 52, 64, 180);
        Color rowSelected    = Color::RGBA8(38, 86, 156);
        Color accent         = Color::RGBA8(56, 132, 230);
        Color text           = Color::RGBA8(230, 230, 234);
        Color textMuted      = Color::RGBA8(138, 140, 148);
        Color separator      = Color::RGBA8(58, 60, 70);
        Color popup          = Color::RGBA8(26, 27, 33, 252);
        Color shadow         = Color::RGBA8(0, 0, 0, 110);
        Color modalDim       = Color::RGBA8(0, 0, 0, 150);
        Color border         = Color::RGBA8(12, 12, 14);
        Color splitter       = Color::RGBA8(12, 12, 14);
        Color splitterHover  = Color::RGBA8(70, 140, 230);
        Color axisX          = Color::RGBA8(212, 76, 76);
        Color axisY          = Color::RGBA8(76, 172, 92);
        Color axisZ          = Color::RGBA8(66, 130, 220);
        Color axisW          = Color::RGBA8(196, 168, 64);
    };

    inline constexpr Theme kTheme{};
    inline constexpr float kRowHeight = 24.0f;
    inline constexpr float kItemGap = 4.0f;
    inline constexpr float kRegionPad = 8.0f;
    inline constexpr float kTabBarHeight = 28.0f;
    inline constexpr float kMenuBarHeight = 28.0f;

    // Draw kinds selected in the fragment shader via `mode`. Kept as a plain
    // uint32 (not enum class) since it is written directly into the GPU
    // instance buffer.
    namespace DrawMode {
        inline constexpr uint32_t SolidColor = 0;
        inline constexpr uint32_t Texture = 1;
        inline constexpr uint32_t SDFGlyph = 2;
    }

    // One instance == one rectangle (panel background, button, glyph quad).
    // std430-friendly layout, 16-byte aligned; matches shaders/ui_quad.vert.
    struct alignas(16) UIInstance {
        glm::vec2 position{0.0f};
        glm::vec2 size{0.0f};
        glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec4 color{1.0f};
        glm::vec4 clipRect{0.0f, 0.0f, 1e6f, 1e6f}; // x,y,w,h in framebuffer pixels
        float cornerRadius = 0.0f;
        uint32_t mode = DrawMode::SolidColor;
        uint32_t textureIndex = 0;
        float _pad = 0.0f;
    };
}
