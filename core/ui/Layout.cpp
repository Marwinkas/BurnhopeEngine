#include "ui/Layout.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/widget/Detail.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace burnhope {

void uiRegisterCustomBinNode(UiState& s, uint8_t role, UiCustomNodeLoader loader, void* user) {
    if (role < 16) {
        s.binHooks[role] = {loader, user};
    }
}

namespace {

static_assert(sizeof(BinHead) == 12, "BinHead");
static_assert(sizeof(BinNode) == 204, "BinNode");

void copyPool(char* dst, uint8_t& len, int cap, const char* pool, uint32_t poolN, uint16_t off, uint16_t n) {
    len = 0;
    if (dst == nullptr || cap <= 0) {
        return;
    }
    dst[0] = '\0';
    if (pool == nullptr || n == 0 || static_cast<uint32_t>(off) + n > poolN) {
        return;
    }
    const int take = n < cap - 1 ? n : cap - 1;
    std::memcpy(dst, pool + off, static_cast<size_t>(take));
    dst[take] = '\0';
    len = static_cast<uint8_t>(take);
}

void nameOf(UiState& s, uint16_t id, const char* name) {
    if (id == kUiNone || name == nullptr || name[0] == '\0') {
        return;
    }
    UiName stored{};
    uint8_t n = 0;
    bool plain = true;
    while (name[n] != '\0' && n < 31) {
        const char ch = name[n];
        stored.bytes[n] = ch;
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            plain = false;
        }
        ++n;
    }
    stored.bytes[n] = '\0';
    stored.len = n;
    s.ent[id].set<UiName>(stored);
    if (plain) {
        s.ent[id].set_name(stored.bytes);
    }
}

} // namespace

void uiName(UiState& s, uint16_t id, const char* name) {
    nameOf(s, id, name);
}

uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size) {
    if (bytes == nullptr || size < sizeof(BinHead)) {
        return kUiNone;
    }
    BinHead head{};
    std::memcpy(&head, bytes, sizeof(head));
    if (head.magic != kUiBinMagic || head.version != kUiBinVersion || head.count == 0) {
        return kUiNone;
    }
    const uint32_t body = static_cast<uint32_t>(sizeof(BinHead) + sizeof(BinNode) * head.count);
    if (body + head.pool > size) {
        return kUiNone;
    }
    const auto* nodes = reinterpret_cast<const BinNode*>(static_cast<const std::byte*>(bytes) + sizeof(BinHead));
    const uint16_t ncheck = head.count < 1024 ? head.count : 1024;
    for (uint16_t i = 0; i < ncheck; ++i) {
        const BinNode& bin = nodes[i];
        const auto past = [&](uint16_t off, uint16_t len) {
            return len > 0 && static_cast<uint32_t>(off) + len > head.pool;
        };
        if (past(bin.nameOff, bin.nameLen) || past(bin.textOff, bin.textLen) || past(bin.hintOff, bin.hintLen)) {
            return kUiNone;
        }
    }
    const char* pool = reinterpret_cast<const char*>(static_cast<const std::byte*>(bytes) + body);
    uint16_t map[1024];
    const uint16_t n = head.count < 1024 ? head.count : 1024;
    for (uint16_t i = 0; i < n; ++i) {
        map[i] = kUiNone;
    }
    uint16_t root = kUiNone;
    for (uint16_t i = 0; i < n; ++i) {
        const BinNode& bin = nodes[i];
        const uint16_t host = bin.parent == 0xffff ? parent : (bin.parent < n ? map[bin.parent] : kUiNone);
        char name[32]{};
        char text[96]{};
        char hint[40]{};
        uint8_t nameLen = 0;
        uint8_t textLen = 0;
        uint8_t hintLen = 0;
        copyPool(name, nameLen, 32, pool, head.pool, bin.nameOff, bin.nameLen);
        copyPool(text, textLen, 96, pool, head.pool, bin.textOff, bin.textLen);
        copyPool(hint, hintLen, 40, pool, head.pool, bin.hintOff, bin.hintLen);
        if (bin.role < 16 && s.binHooks[bin.role].fn != nullptr) {
            map[i] = s.binHooks[bin.role].fn(s, host, bin, name, s.binHooks[bin.role].user);
            continue;
        }
        BinNode plain = bin;
        if (plain.role > static_cast<uint8_t>(UiRole::Progress)) {
            plain.role = static_cast<uint8_t>(UiRole::Panel);
        }
        UiFlex flex{};
        UiFlexAttr attr{};
        attr.direction = bin.direction;
        attr.wrap = bin.wrap;
        attr.justify = bin.justify;
        attr.align = bin.align;
        attr.widthMode = bin.widthMode;
        attr.heightMode = bin.heightMode;
        attr.width = bin.width;
        attr.height = bin.height;
        attr.grow = bin.grow;
        attr.shrink = bin.shrink;
        attr.pad = bin.pad;
        attr.gap = bin.gap;
        attr.position = bin.position;
        attr.posX = bin.posX;
        attr.posY = bin.posY;
        attr.padT = bin.padT;
        attr.padB = bin.padB;
        attr.minW = bin.minW;
        attr.minH = bin.minH;
        attr.marginL = bin.marginL;
        attr.marginR = bin.marginR;
        attr.marginT = bin.marginT;
        attr.marginB = bin.marginB;
        attr.drop = bin.drop;
        attr.content = bin.content;
        attr.self = bin.self;
        attr.basisMode = bin.basisMode;
        attr.maxWMode = bin.maxWMode;
        attr.maxHMode = bin.maxHMode;
        attr.overflow = bin.overflow;
        attr.box = bin.box;
        attr.inset = bin.inset;
        attr.basis = bin.basis;
        attr.maxW = bin.maxW;
        attr.maxH = bin.maxH;
        attr.gapRow = bin.gapRow;
        attr.aspect = bin.aspect;
        attr.posR = bin.posR;
        attr.posB = bin.posB;
        uiFlexApplyAttr(flex, attr);
        const uint8_t role = plain.role;
        UiPaint ink = paint(static_cast<UiRole>(role), bin.r, bin.g, bin.b, bin.radius, bin.tag);
        ink.a = bin.a;
        ink.icon = bin.icon;
        ink.shadow = bin.shadow;
        ink.borderW = bin.borderW;
        ink.br = bin.br;
        ink.bg = bin.bg;
        ink.bb = bin.bb;
        ink.disabled = (bin.flags & kBinDisabled) != 0 ? 1 : 0;
        const uint16_t id = spawn(s, host, flex, ink);
        if (id == kUiNone) {
            continue;
        }
        map[i] = id;
        if (root == kUiNone) {
            root = id;
        }
        nameOf(s, id, name);
        if (text[0] != '\0') {
            addText(s, id, text);
        }
        if (bin.textWrap != 0 || bin.textDeco != 0 || bin.textLeading != 0.0f || bin.textAlign != 0 || bin.textEllipsis != 0) {
            const uint8_t valign = bin.textValign != 0 ? bin.textValign : (bin.textWrap != 0 ? 1 : 0);
            uiTextStyle(s, id, bin.textAlign, bin.textEllipsis, valign, bin.textWrap, bin.textDeco, bin.textLeading);
        }
        if (role == static_cast<uint8_t>(UiRole::Field)) {
            UiField field{};
            uint8_t len = textLen < 95 ? textLen : 95;
            std::memcpy(field.bytes, text, len);
            field.bytes[len] = '\0';
            field.len = len;
            field.caret = len;
            field.anchor = len;
            field.filter = bin.filter;
            field.clear = (bin.flags & kBinClear) != 0 ? 1 : 0;
            field.required = (bin.flags & kBinRequired) != 0 ? 1 : 0;
            field.readOnly = (bin.flags & kBinReadOnly) != 0 ? 1 : 0;
            field.password = (bin.flags & kBinPassword) != 0 ? 1 : 0;
            field.inForm = (bin.flags & kBinForm) != 0 ? 1 : 0;
            field.maxChars = bin.maxChars;
            field.padL = bin.padL > 0.0f ? bin.padL : 12.0f;
            field.padR = bin.padR > 0.0f ? bin.padR : 12.0f;
            if (hint[0] != '\0') {
                field.hintLen = hintLen < 39 ? hintLen : 39;
                std::memcpy(field.hint, hint, field.hintLen);
                field.hint[field.hintLen] = '\0';
            }
            s.ent[id].set<UiField>(field);
            if (field.inForm != 0) {
                fieldMark(s, id);
            }
        } else if (role == static_cast<uint8_t>(UiRole::Slider) || role == static_cast<uint8_t>(UiRole::Progress)) {
            s.ent[id].set<UiRange>({bin.value, bin.minV, bin.maxV});
        } else if (role == static_cast<uint8_t>(UiRole::Check)) {
            UiCheck check{};
            check.on = (bin.flags & kBinCheckOn) != 0 ? 1 : 0;
            check.kind = bin.checkKind;
            check.group = bin.checkGroup;
            s.ent[id].set<UiCheck>(check);
        }
        if (bin.shown == 0) {
            uiShow(s, id, false);
        }
    }
    for (uint16_t i = 0; i < n; ++i) {
        if (nodes[i].role != static_cast<uint8_t>(UiRole::Scroll) || map[i] == kUiNone) {
            continue;
        }
        for (uint16_t c = 0; c < n; ++c) {
            if (nodes[c].parent == i && map[c] != kUiNone) {
                s.ent[map[i]].set<UiScroll>({0.0f, map[c]});
                break;
            }
        }
    }
    return root;
}

uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path) {
    if (path == nullptr) {
        return kUiNone;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return kUiNone;
    }
    std::fseek(file, 0, SEEK_END);
    const long n = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        std::fclose(file);
        return kUiNone;
    }
    std::vector<std::byte> buf(static_cast<size_t>(n));
    const size_t got = std::fread(buf.data(), 1, buf.size(), file);
    std::fclose(file);
    if (got != buf.size()) {
        return kUiNone;
    }
    return uiImportBin(s, parent, buf.data(), static_cast<uint32_t>(got));
}

} // namespace burnhope
