#include "ui/Layout.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/State.hpp"
#include "io/File.hpp"

#include <cstddef>
#include <cstring>

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

} // namespace

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
        uiName(s, id, name);
        if (text[0] != '\0') {
            addText(s, id, text);
        }
        if (bin.textWrap != 0 || bin.textDeco != 0 || bin.textLeading != 0.0f || bin.textAlign != 0 || bin.textEllipsis != 0) {
            const uint8_t valign = bin.textValign != 0 ? bin.textValign : (bin.textWrap != 0 ? 1 : 0);
            uiTextStyle(s, id, bin.textAlign, bin.textEllipsis, valign, bin.textWrap, bin.textDeco, bin.textLeading);
        }
        if (role < 16 && s.roleFinish[role] != nullptr) {
            s.roleFinish[role](s, id, &bin, text, hint[0] != '\0' ? hint : nullptr);
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
    if (path == nullptr || path[0] == '\0') {
        return kUiNone;
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        return kUiNone;
    }
    if (mapped.size < sizeof(BinHead) || mapped.size > 8u * 1024u * 1024u || mapped.data == nullptr) {
        fileUnmap(&mapped);
        return kUiNone;
    }
    const uint16_t root = uiImportBin(s, parent, mapped.data, static_cast<uint32_t>(mapped.size));
    fileUnmap(&mapped);
    return root;
}

} // namespace burnhope
