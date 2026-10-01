#include "ui/State.hpp"

namespace burnhope {
namespace {

struct Cp {
    uint8_t at = 0;
    uint8_t n = 1;
    float adv = 0;
    uint8_t kind = 0; // 1 newline, 2 space
};

uint32_t foldCss(uint32_t cp, uint8_t mode, bool capNext) {
    auto upper = [](uint32_t c) -> uint32_t {
        if (c >= 'a' && c <= 'z') {
            return c - 32;
        }
        if (c >= 0x0430 && c <= 0x044F) {
            return c - 0x20;
        }
        if (c == 0x0451) {
            return 0x0401;
        }
        return c;
    };
    auto lower = [](uint32_t c) -> uint32_t {
        if (c >= 'A' && c <= 'Z') {
            return c + 32;
        }
        if (c >= 0x0410 && c <= 0x042F) {
            return c + 0x20;
        }
        if (c == 0x0401) {
            return 0x0451;
        }
        return c;
    };
    if (mode == 1) {
        return upper(cp);
    }
    if (mode == 2) {
        return lower(cp);
    }
    if (mode == 3 && capNext) {
        return upper(cp);
    }
    return cp;
}

void pushCp(char* dst, uint8_t& n, uint32_t cp) {
    if (cp < 0x80) {
        if (n < 95) {
            dst[n++] = static_cast<char>(cp);
        }
        return;
    }
    if (cp < 0x800) {
        if (n < 94) {
            dst[n++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        return;
    }
    if (n < 93) {
        dst[n++] = static_cast<char>(0xE0 | (cp >> 12));
        dst[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        dst[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void copyRange(TextRun& run, const char* src, const Cp* cp, int from, int to, bool dots, float width) {
    if (run.lines >= kUiLineCap) {
        return;
    }
    TextLine& line = run.line[run.lines];
    line.begin = run.len;
    for (int k = from; k < to; ++k) {
        for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
            run.bytes[run.len++] = src[cp[k].at + t];
        }
    }
    if (dots && run.len <= 92) {
        run.bytes[run.len++] = '.';
        run.bytes[run.len++] = '.';
        run.bytes[run.len++] = '.';
    }
    line.len = static_cast<uint8_t>(run.len - line.begin);
    line.w = width;
    ++run.lines;
}

void ellipsisEnd(TextRun& run, const char* src, const Cp* cp, const float* ps, int from, int to, float budget, float dots) {
    int keep = from;
    float acc = 0.0f;
    if (budget > 1.0f) {
        for (int k = from; k < to; ++k) {
            if (acc + cp[k].adv > budget) {
                break;
            }
            acc += cp[k].adv;
            keep = k + 1;
        }
    }
    const float width = (keep > from ? ps[keep] - ps[from] : 0.0f) + dots;
    copyRange(run, src, cp, from, keep, true, width);
}

} // namespace

TextRun textRun(const UiState& s, const UiText& text, const UiBox& slot) {
    TextRun run{};
    run.box = slot;
    run.deco = text.deco;
    run.scale = 1.0f;
    if (text.len == 0 || slot.w < 1.0f || slot.h < 1.0f) {
        run.box.w = 0.0f;
        return run;
    }
    const float em = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
    float scale = text.size > 0.05f ? text.size : 1.0f;
    if (em * scale > slot.h && em > 0.5f) {
        scale = slot.h / em;
    }
    run.scale = scale;
    run.tracking = text.tracking;
    run.wordGap = text.wordGap;
    run.thick = text.thick > 0.4f ? text.thick : 1.0f;
    run.under = text.under;
    run.shadowX = text.shadowX;
    run.shadowY = text.shadowY;

    char raw[96]{};
    uint8_t rawLen = 0;
    bool capNext = text.transform == 3;
    const int tabN = text.tabs == 0 ? 4 : static_cast<int>(text.tabs);
    uint8_t at = 0;
    while (at < text.len && rawLen < 95) {
        uint8_t step = 1;
        uint32_t code = uiReadUtf8(text.bytes, text.len, at, step);
        if (step == 0) {
            break;
        }
        if (code == '\t') {
            for (int t = 0; t < tabN && rawLen < 95; ++t) {
                raw[rawLen++] = ' ';
            }
            capNext = false;
        } else {
            code = foldCss(code, text.transform, capNext);
            capNext = code == ' ' || code == '\n';
            pushCp(raw, rawLen, code);
        }
        at = static_cast<uint8_t>(at + step);
    }
    raw[rawLen] = '\0';

    Cp cp[96]{};
    int ncp = 0;
    uint8_t i = 0;
    while (i < rawLen && ncp < 96) {
        uint8_t step = 1;
        const uint32_t code = uiReadUtf8(raw, rawLen, i, step);
        if (step == 0) {
            break;
        }
        uint8_t kind = 0;
        if (code == '\n') {
            kind = 1;
        } else if (code == ' ') {
            kind = 2;
        }
        float adv = uiAdvance(s, code) * scale + text.tracking;
        if (kind == 2) {
            adv += text.wordGap;
        }
        cp[ncp] = {i, step, adv, kind};
        i = static_cast<uint8_t>(i + step);
        ++ncp;
    }
    float ps[97]{};
    for (int k = 0; k < ncp; ++k) {
        ps[k + 1] = ps[k] + cp[k].adv;
    }
    const float dots = uiMeasure(s, "...", 3, 3) * scale + text.tracking * 3.0f;
    const float full = ps[ncp];
    const float firstW = text.indent > 0.0f && text.indent < slot.w ? slot.w - text.indent : slot.w;

    if (text.wrap == 0) {
        if (text.ellipsis != 0 && full > firstW) {
            if (text.ellipsis == 2) {
                int gapL = ncp / 2;
                int gapR = gapL;
                while ((ps[gapL] + dots + (ps[ncp] - ps[gapR])) > firstW && (gapL > 0 || gapR < ncp)) {
                    if (gapL >= ncp - gapR && gapL > 0) {
                        --gapL;
                    } else if (gapR < ncp) {
                        ++gapR;
                    } else {
                        break;
                    }
                }
                const float width = ps[gapL] + dots + (ps[ncp] - ps[gapR]);
                TextLine& line = run.line[0];
                line.begin = 0;
                for (int k = 0; k < gapL && run.len < 95; ++k) {
                    for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
                        run.bytes[run.len++] = raw[cp[k].at + t];
                    }
                }
                if (run.len <= 92) {
                    run.bytes[run.len++] = '.';
                    run.bytes[run.len++] = '.';
                    run.bytes[run.len++] = '.';
                }
                for (int k = gapR; k < ncp && run.len < 95; ++k) {
                    for (uint8_t t = 0; t < cp[k].n && run.len < 95; ++t) {
                        run.bytes[run.len++] = raw[cp[k].at + t];
                    }
                }
                line.len = run.len;
                line.w = width;
                run.lines = 1;
            } else {
                ellipsisEnd(run, raw, cp, ps, 0, ncp, firstW - dots, dots);
            }
        } else {
            copyRange(run, raw, cp, 0, ncp, false, full);
        }
    } else {
        int at = 0;
        while (at < ncp && run.lines < kUiLineCap) {
            if (cp[at].kind == 1) {
                copyRange(run, raw, cp, at, at, false, 0.0f);
                ++at;
                continue;
            }
            int j = at;
            int lastSpace = -1;
            const float limit = run.lines == 0 ? firstW : slot.w;
            while (j < ncp && cp[j].kind != 1 && (ps[j + 1] - ps[at]) <= limit + 0.01f) {
                if (text.breakAll == 0 && cp[j].kind == 2) {
                    lastSpace = j;
                }
                ++j;
            }
            int end = j;
            int next = j;
            if (j < ncp && cp[j].kind == 1) {
                next = j + 1;
            } else if (j < ncp && lastSpace > at) {
                end = lastSpace;
                next = lastSpace + 1;
            }
            if (end == at) {
                end = at + 1;
                next = end;
            }
            const bool more = next < ncp;
            const bool lastSlot = static_cast<uint8_t>(run.lines + 1) >= kUiLineCap;
            if (lastSlot && more && text.ellipsis != 0) {
                ellipsisEnd(run, raw, cp, ps, at, ncp, (run.lines == 0 ? firstW : slot.w) - dots, dots);
                break;
            }
            copyRange(run, raw, cp, at, end, false, ps[end] - ps[at]);
            at = next;
        }
    }

    if (text.align == 3) {
        for (uint8_t li = 0; li < run.lines; ++li) {
            const bool last = static_cast<uint8_t>(li + 1) == run.lines;
            if (text.wrap != 0 && last) {
                continue;
            }
            int spaces = 0;
            const TextLine& row = run.line[li];
            for (uint8_t k = 0; k < row.len; ++k) {
                if (run.bytes[row.begin + k] == ' ') {
                    ++spaces;
                }
            }
            float room = slot.w - row.w;
            if (li == 0 && text.indent > 0.0f) {
                room -= text.indent;
            }
            if (spaces > 0 && room > 0.5f) {
                run.line[li].gap = room / static_cast<float>(spaces);
                run.line[li].w += room;
            }
        }
    }

    const float lineH = em * scale;
    const float gap = text.leading > 0.0f ? text.leading : 0.0f;
    const float block = run.lines > 0
        ? static_cast<float>(run.lines) * lineH + static_cast<float>(run.lines - 1) * gap
        : 0.0f;
    float y = slot.y;
    const float shift = text.wrap != 0 ? text.scroll : 0.0f;
    if (text.valign == 2 && block < slot.h) {
        y = slot.y + slot.h - block;
    } else if (text.valign == 1 && block < slot.h) {
        y = slot.y + (slot.h - block) * 0.5f;
    }
    y -= shift;
    const float xScroll = text.wrap == 0 ? text.scroll : 0.0f;
    for (uint8_t line = 0; line < run.lines; ++line) {
        TextLine& row = run.line[line];
        float x = slot.x - (text.align == 0 || text.align == 3 ? xScroll : 0.0f);
        if (text.align == 1 && row.w < slot.w) {
            x = slot.x + (slot.w - row.w) * 0.5f;
        } else if (text.align == 2 && row.w < slot.w) {
            x = slot.x + slot.w - row.w;
        }
        if (line == 0 && text.indent > 0.0f && (text.align == 0 || text.align == 3)) {
            x += text.indent;
        }
        row.x = x;
        row.y = y;
        row.h = lineH;
        y += lineH + gap;
    }
    run.bytes[run.len < 95 ? run.len : 95] = '\0';
    if (run.lines > 0) {
        run.box.x = run.line[0].x;
        run.box.y = run.line[0].y;
        run.box.w = run.line[0].w + 1.0f;
        run.box.h = block > 0.0f ? block : lineH;
    }
    return run;
}

} // namespace burnhope
