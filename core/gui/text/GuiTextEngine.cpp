#include "gui/text/GuiTextEngine.hpp"

#include <cstring>

namespace burnhope::gui {
namespace {

constexpr uint32_t kQuestionSlot = '?' - 32;
constexpr uint32_t kReplacement = 0xFFFDu;

bool isContinuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

bool inSet(char c, const char* set) {
    return c != '\0' && std::strchr(set, c) != nullptr;
}

// Analytic fallback shape of an ASCII glyph, in em.
GuiGlyph asciiGlyph(char c) {
    float adv = 0.54f;
    float top = 0.55f;    // ink above the baseline
    float bottom = 0.0f;  // ink below the baseline
    if (c == ' ') {
        adv = 0.28f;
        top = 0.0f;
    } else if (inSet(c, "il!.,:;'|`")) {
        adv = 0.27f;
        top = (c == '.' || c == ',') ? 0.12f : 0.74f;
        bottom = (c == ',' || c == ';') ? 0.1f : 0.0f;
    } else if (c == 'I' || inSet(c, "()[]{}/\\tfr-\"")) {
        adv = 0.36f;
        top = 0.72f;
    } else if (inSet(c, "mw")) {
        adv = 0.84f;
    } else if (inSet(c, "MW@%")) {
        adv = 0.92f;
        top = 0.72f;
    } else if (c >= 'A' && c <= 'Z') {
        adv = 0.64f;
        top = 0.72f;
    } else if (c >= '0' && c <= '9') {
        adv = 0.56f;
        top = 0.72f;
    } else if (c >= 'a' && c <= 'z') {
        adv = 0.54f;
        top = inSet(c, "bdhkt") ? 0.74f : 0.55f;
        bottom = inSet(c, "gjpqy") ? 0.2f : 0.0f;
    } else {
        adv = 0.5f;
        top = 0.6f;
    }
    GuiGlyph g;
    g.advance = adv;
    g.bearingX = 0.04f;
    g.bearingY = top;
    g.w = adv > 0.12f ? adv - 0.08f : 0.0f;
    g.h = top + bottom;
    if (c == ' ') {
        g.w = 0.0f;
        g.h = 0.0f;
    }
    return g;
}

GuiGlyph cyrillicGlyph(uint32_t cp) {
    GuiGlyph g;
    const bool upper = cp < 0x0430u && cp != 0x0451u;
    float adv = upper ? 0.66f : 0.54f;
    if (!upper && (cp == 0x0436u || cp == 0x0448u || cp == 0x0449u || cp == 0x044Eu || cp == 0x043Cu || cp == 0x044Bu || cp == 0x0444u)) {
        adv = 0.72f;
    }
    if (upper && (cp == 0x0428u || cp == 0x0429u || cp == 0x042Eu || cp == 0x042Bu || cp == 0x0416u)) {
        adv = 0.86f;
    }
    const float top = upper ? 0.72f : 0.55f;
    const bool desc = cp == 0x0440u || cp == 0x0443u || cp == 0x0444u || cp == 0x0449u || cp == 0x0426u || cp == 0x0429u;
    g.advance = adv;
    g.bearingX = 0.04f;
    g.bearingY = top + (cp == 0x0401u || cp == 0x0451u ? 0.12f : 0.0f);
    g.w = adv - 0.08f;
    g.h = g.bearingY + (desc ? 0.2f : 0.0f);
    return g;
}

float advanceOf(const GuiTextMetrics& m, uint32_t cp) {
    if (cp < 32u) {
        return 0.0f; // control characters (newline, tab) take no room
    }
    const int slot = guiTextSlot(cp);
    return m.glyphs[slot >= 0 ? static_cast<uint32_t>(slot) : kQuestionSlot].advance;
}

float rangeWidth(const GuiTextMetrics& m, std::string_view s, uint32_t from, uint32_t to, float px) {
    float w = 0.0f;
    uint32_t i = from;
    while (i < to) {
        uint32_t len = 0;
        const uint32_t cp = guiTextDecode(s, i, len);
        if (len == 0) {
            break;
        }
        w += advanceOf(m, cp);
        i += len;
    }
    return w * px;
}

} // namespace

void guiTextMetricsDefault(GuiTextMetrics& m) {
    m = GuiTextMetrics{};
    for (uint32_t i = 0; i < 95; ++i) {
        m.glyphs[i] = asciiGlyph(static_cast<char>(32 + i));
    }
    m.glyphs[95] = cyrillicGlyph(0x0401u);
    for (uint32_t i = 0; i < 64; ++i) {
        m.glyphs[96 + i] = cyrillicGlyph(0x0410u + i);
    }
    m.glyphs[160] = cyrillicGlyph(0x0451u);
}

bool guiTextMetricsSet(GuiTextMetrics& m, const GuiGlyph* table, uint32_t first, uint32_t count) {
    if (table == nullptr || first > kGuiGlyphSlots || count > kGuiGlyphSlots - first) {
        return false;
    }
    std::memcpy(m.glyphs + first, table, sizeof(GuiGlyph) * count);
    return true;
}

bool guiTextMetricsSetAdvances(GuiTextMetrics& m, const float* advances, uint32_t count, float pixelSize) {
    if (advances == nullptr || count > kGuiGlyphSlots || !(pixelSize > 0.0f)) {
        return false;
    }
    const float inv = 1.0f / pixelSize;
    for (uint32_t i = 0; i < count; ++i) {
        m.glyphs[i].advance = advances[i] * inv;
    }
    return true;
}

int guiTextSlot(uint32_t cp) {
    if (cp >= 32u && cp <= 126u) {
        return static_cast<int>(cp - 32u);
    }
    if (cp == 0x0401u) {
        return 95;
    }
    if (cp >= 0x0410u && cp <= 0x044Fu) {
        return 96 + static_cast<int>(cp - 0x0410u);
    }
    if (cp == 0x0451u) {
        return 160;
    }
    return -1;
}

uint32_t guiTextDecode(std::string_view s, uint32_t at, uint32_t& len) {
    len = 0;
    if (at >= s.size()) {
        return 0;
    }
    const auto b = [&](uint32_t k) { return static_cast<unsigned char>(s[at + k]); };
    const uint32_t left = static_cast<uint32_t>(s.size()) - at;
    const unsigned char c0 = b(0);
    len = 1;
    if (c0 < 0x80u) {
        return c0;
    }
    if (c0 >= 0xC2u && c0 <= 0xDFu && left >= 2 && isContinuation(b(1))) {
        len = 2;
        return (static_cast<uint32_t>(c0 & 0x1Fu) << 6) | (b(1) & 0x3Fu);
    }
    if (c0 >= 0xE0u && c0 <= 0xEFu && left >= 3 && isContinuation(b(1)) && isContinuation(b(2))) {
        len = 3;
        return (static_cast<uint32_t>(c0 & 0x0Fu) << 12) | (static_cast<uint32_t>(b(1) & 0x3Fu) << 6) | (b(2) & 0x3Fu);
    }
    if (c0 >= 0xF0u && c0 <= 0xF4u && left >= 4 && isContinuation(b(1)) && isContinuation(b(2)) && isContinuation(b(3))) {
        len = 4;
        return (static_cast<uint32_t>(c0 & 0x07u) << 18) | (static_cast<uint32_t>(b(1) & 0x3Fu) << 12) |
               (static_cast<uint32_t>(b(2) & 0x3Fu) << 6) | (b(3) & 0x3Fu);
    }
    return kReplacement;
}

bool guiTextGlyph(const GuiTextMetrics& m, uint32_t cp, GuiGlyph& out) {
    const int slot = guiTextSlot(cp);
    if (slot < 0) {
        out = m.glyphs[kQuestionSlot];
        return false;
    }
    out = m.glyphs[slot];
    return true;
}

float guiTextWidth(const GuiTextMetrics& m, std::string_view utf8, float px) {
    return rangeWidth(m, utf8, 0, static_cast<uint32_t>(utf8.size()), px);
}

float guiTextLineHeight(const GuiTextMetrics& m, float px) {
    return m.lineHeight * px;
}

uint32_t guiTextWrap(const GuiTextMetrics& m, std::string_view s, float px, float maxW, GuiTextLine* out, uint32_t cap) {
    const uint32_t size = static_cast<uint32_t>(s.size());
    const bool wrap = maxW > 0.0f;
    uint32_t lines = 0;
    const auto emit = [&](uint32_t begin, uint32_t end, float width) {
        if (out != nullptr && lines < cap) {
            out[lines] = GuiTextLine{.begin = begin, .end = end, .width = width};
        }
        ++lines;
    };

    uint32_t lineBegin = 0;
    float lineW = 0.0f;       // pixels of [lineBegin, i)
    bool haveBreak = false;   // a space inside the current line
    uint32_t breakAt = 0;     // byte of that space
    float breakW = 0.0f;      // line width before the space
    uint32_t i = 0;
    while (i < size) {
        uint32_t len = 0;
        const uint32_t cp = guiTextDecode(s, i, len);
        if (cp == '\n') {
            emit(lineBegin, i, lineW);
            i += len;
            lineBegin = i;
            lineW = 0.0f;
            haveBreak = false;
            continue;
        }
        const float adv = advanceOf(m, cp) * px;
        if (wrap && cp != ' ' && lineW + adv > maxW && i > lineBegin) {
            if (haveBreak) {
                emit(lineBegin, breakAt, breakW);
                lineBegin = breakAt + 1; // the wrapping space is dropped
                lineW = rangeWidth(m, s, lineBegin, i, px);
                haveBreak = false;
                // The remaining words may still be too wide after the move; the next glyph re-checks.
                continue;
            }
            emit(lineBegin, i, lineW);
            lineBegin = i;
            lineW = 0.0f;
        }
        if (cp == ' ' && i > lineBegin) {
            haveBreak = true;
            breakAt = i;
            breakW = lineW;
        }
        lineW += adv;
        i += len;
    }
    emit(lineBegin, size, lineW);
    return out != nullptr && lines > cap ? cap : lines;
}

uint32_t guiTextCaretFromX(const GuiTextMetrics& m, std::string_view s, float px, float x) {
    if (x <= 0.0f) {
        return 0;
    }
    float pen = 0.0f;
    uint32_t i = 0;
    const uint32_t size = static_cast<uint32_t>(s.size());
    while (i < size) {
        uint32_t len = 0;
        const uint32_t cp = guiTextDecode(s, i, len);
        const float adv = advanceOf(m, cp) * px;
        if (x < pen + adv * 0.5f) {
            return i;
        }
        pen += adv;
        i += len;
    }
    return size;
}

float guiTextXFromCaret(const GuiTextMetrics& m, std::string_view s, float px, uint32_t caret) {
    const uint32_t size = static_cast<uint32_t>(s.size());
    return rangeWidth(m, s, 0, caret < size ? caret : size, px);
}

uint32_t guiTextNextBoundary(std::string_view s, uint32_t byte) {
    const uint32_t size = static_cast<uint32_t>(s.size());
    if (byte >= size) {
        return size;
    }
    uint32_t len = 0;
    guiTextDecode(s, byte, len);
    return byte + (len != 0 ? len : 1u);
}

uint32_t guiTextPrevBoundary(std::string_view s, uint32_t byte) {
    uint32_t i = byte < s.size() ? byte : static_cast<uint32_t>(s.size());
    if (i == 0) {
        return 0;
    }
    --i;
    while (i > 0 && isContinuation(static_cast<unsigned char>(s[i]))) {
        --i;
    }
    return i;
}

} // namespace burnhope::gui
