#pragma once

#include <cstdint>
#include <string_view>

namespace burnhope::gui {

// Pure text measurement for the gui module. No Flecs, no RHI, no allocation.
// All metrics are stored for a 1.0 em font and scaled by the caller's pixel size.
//
// Glyph slots follow the legacy atlas layout (core/ui/Typography.hpp uiGlyphIndex): slots 0..94 are ASCII 32..126,
// 95 is U+0401, 96..159 are U+0410..U+044F, 160 is U+0451. Every other codepoint falls back to '?'.
constexpr uint32_t kGuiGlyphSlots = 192;

struct GuiGlyph {
    float advance = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f; // distance from the baseline up to the glyph top
    float w = 0.0f;
    float h = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

struct GuiTextLine {
    uint32_t begin = 0; // byte range [begin, end) of the line, line break and the wrapping space excluded
    uint32_t end = 0;
    float width = 0.0f; // pixel width of the range
};

// Per-context glyph table. The default fills proportional analytic advances (the legacy atlas is generated from
// FreeType at runtime and has no data file, so real advances only arrive through guiTextMetricsSet).
struct GuiTextMetrics {
    GuiGlyph glyphs[kGuiGlyphSlots] = {};
    float lineHeight = 1.25f; // em
    float ascent = 0.95f;     // em, baseline offset from the line top
};

void guiTextMetricsDefault(GuiTextMetrics& m);

// Replaces slots [first, first + count) with a host-provided table (e.g. the font atlas metrics). Returns false when
// the range leaves 0..kGuiGlyphSlots or table is null; the metrics are untouched then.
bool guiTextMetricsSet(GuiTextMetrics& m, const GuiGlyph* table, uint32_t first, uint32_t count);

// Installs real font advances: advances[i] is the advance in pixels of glyph slot i when the font is rasterized at
// pixelSize (the FreeType advance table of the host's font atlas, slot order as above). Only the advance changes; the
// other glyph fields are kept. Returns false for a null table, count > kGuiGlyphSlots or pixelSize <= 0 (untouched then).
bool guiTextMetricsSetAdvances(GuiTextMetrics& m, const float* advances, uint32_t count, float pixelSize);

// Slot of a codepoint, or -1 when the atlas has none.
int guiTextSlot(uint32_t codepoint);

// Reads one codepoint at byte offset `at`; `len` receives the bytes consumed (1 for malformed input, 0 at the end).
uint32_t guiTextDecode(std::string_view utf8, uint32_t at, uint32_t& len);

// Glyph for a codepoint scaled to 1 em. Returns false (and fills '?') when the atlas has no such glyph.
bool guiTextGlyph(const GuiTextMetrics& m, uint32_t codepoint, GuiGlyph& out);

float guiTextWidth(const GuiTextMetrics& m, std::string_view utf8, float px);

// Line height in pixels.
float guiTextLineHeight(const GuiTextMetrics& m, float px);

// Greedy word wrap on spaces and '\n'; a word wider than maxW breaks between codepoints. maxW <= 0 disables wrapping
// (newlines still break). Writes at most `cap` lines into `out` and returns the number written; with out == nullptr it
// returns the total line count. Empty text is one empty line.
uint32_t guiTextWrap(const GuiTextMetrics& m, std::string_view utf8, float px, float maxW, GuiTextLine* out, uint32_t cap);

// Byte offset on a codepoint boundary nearest to pixel x measured from the start of the text.
uint32_t guiTextCaretFromX(const GuiTextMetrics& m, std::string_view utf8, float px, float x);
float guiTextXFromCaret(const GuiTextMetrics& m, std::string_view utf8, float px, uint32_t caret);

// Next / previous codepoint boundary of a byte offset (clamped to 0..size).
uint32_t guiTextNextBoundary(std::string_view utf8, uint32_t byte);
uint32_t guiTextPrevBoundary(std::string_view utf8, uint32_t byte);

} // namespace burnhope::gui
