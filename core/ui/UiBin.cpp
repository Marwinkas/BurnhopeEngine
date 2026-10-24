#include "ui/UiBin.hpp"
#include "ui/FlexAttr.hpp"
#include "ui/State.hpp"
#include "io/File.hpp"

#include <cstddef>
#include <cstring>

namespace burnhope {

void uiRegisterCustomBinNode(UiState& s, uint8_t role, UiCustomNodeLoader loader, void* user) {
    if (role < kUiBinRoleCap) {
        s.binHooks[role] = { .fn = loader, .user = user };
    }
}

void uiBinRole(UiState& s, uint8_t role, const char* name, uint8_t token, uint8_t flags) {
    if (role < kUiBinRoleCap) {
        s.binRole[role] = { .name = name, .token = token, .flags = flags };
    }
}

uint8_t uiBinRoleFind(const UiState& s, const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
    for (uint8_t role = 1; role < kUiBinRoleCap; ++role) {
        const char* known = s.binRole[role].name;
        if (known != nullptr && std::strcmp(known, name) == 0) {
            return role;
        }
    }
    return 0;
}

void uiBinBuiltin(UiState& s) {
    uiBinRole(s, 8, "hidden", 0, static_cast<uint8_t>(kBinRoleClear | kBinRolePass));
}

namespace {

static_assert(sizeof(BinHead) == 12, "BinHead size must be exactly 12 bytes");
static_assert(sizeof(BinNode) == 178, "BinNode size must be exactly 178 bytes");
static_assert(sizeof(BinTail) == kBinTailBytes, "bin tail is 26 bytes");

void copyPool(char* dst, uint8_t& len, int cap, const char* pool, uint32_t poolN, uint16_t off, uint16_t n) noexcept {
    len = 0;
    if (dst == nullptr || cap <= 0) {
        return;
    }
    dst[0] = '\0';
    if (pool == nullptr || n == 0 || static_cast<uint32_t>(off) + n > poolN) {
        return;
    }
    const int take = (n < cap - 1) ? n : (cap - 1);
    std::memcpy(dst, pool + off, static_cast<size_t>(take));
    dst[take] = '\0';
    len = static_cast<uint8_t>(take);
}

void finishDescribed(UiState& s, uint16_t id, const BinNode& bin, const char* name) noexcept {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    if ((bin.marks & kBinMarkPopup) != 0) {
        s.ent[id].set<UiPopup>({});
    }
    if (bin.shown == 0) {
        uiShow(s, id, false);
    }
}

} // namespace

uint16_t uiSpawnDescribed(UiState& s, uint16_t host, const BinNode& bin, const BinTail& part, const char* name, const char* text, const char* hint) {
    if (bin.role < kUiBinRoleCap && s.binHooks[bin.role].fn != nullptr) {
        const uint16_t id = s.binHooks[bin.role].fn(s, host, bin, part, name, text, hint, s.binHooks[bin.role].user);
        finishDescribed(s, id, bin, name);
        return id;
    }

    UiFlex flex{};
    UiFlexAttr attr{
        .direction = bin.direction,
        .wrap = bin.wrap,
        .justify = bin.justify,
        .align = bin.align,
        .widthMode = bin.widthMode,
        .heightMode = bin.heightMode,
        .position = bin.position,
        .width = bin.width,
        .height = bin.height,
        .grow = bin.grow,
        .shrink = bin.shrink,
        .pad = bin.pad,
        .padL = bin.padL,
        .padR = bin.padR,
        .padT = bin.padT,
        .padB = bin.padB,
        .gap = bin.gap,
        .posX = bin.posX,
        .posY = bin.posY,
        .minW = bin.minW,
        .minH = bin.minH,
        .marginL = bin.marginL,
        .marginR = bin.marginR,
        .marginT = bin.marginT,
        .marginB = bin.marginB,
        .drop = bin.drop,
        .content = bin.content,
        .self = bin.self,
        .basisMode = bin.basisMode,
        .maxWMode = bin.maxWMode,
        .maxHMode = bin.maxHMode,
        .overflow = bin.overflow,
        .box = bin.box,
        .inset = bin.inset,
        .basis = bin.basis,
        .maxW = bin.maxW,
        .maxH = bin.maxH,
        .gapRow = bin.gapRow,
        .aspect = bin.aspect,
        .posR = bin.posR,
        .posB = bin.posB
    };
    uiFlexApplyAttr(flex, attr);

    const UiBinRole role = bin.role < kUiBinRoleCap ? s.binRole[bin.role] : UiBinRole{};
    const bool marked = (bin.marks & kBinMarkToken) != 0 && bin.token != 0;
    UiToken inkToken = role.token != 0 ? static_cast<UiToken>(role.token) : UiToken::Custom;
    if (marked) {
        inkToken = static_cast<UiToken>(bin.token);
    } else if (inkToken == UiToken::Custom && bin.token != 0) {
        inkToken = static_cast<UiToken>(bin.token);
    }
    float r = bin.r;
    float g = bin.g;
    float b = bin.b;
    float borderW = bin.borderW;
    float br = bin.br;
    float bg = bin.bg;
    float bb = bin.bb;
    float shadow = bin.shadow;
    if (marked) {
        tokenRgb(s.look, inkToken, r, g, b);
    }
    if (marked && (role.flags & kBinRoleRaised) != 0) {
        UiPaint raised = paintToken(s.look, inkToken, bin.radius);
        paintRaised(raised);
        r = raised.r;
        g = raised.g;
        b = raised.b;
        borderW = raised.borderW;
        br = raised.br;
        bg = raised.bg;
        bb = raised.bb;
        shadow = raised.shadow;
    }
    float alpha = (role.flags & kBinRoleClear) != 0 ? 0.0f : bin.a;
    if (marked && (role.flags & kBinRoleGhost) != 0) {
        alpha = 0.0f;
        borderW = 0.0f;
        shadow = 0.0f;
    }

    UiPaint ink{
        .r = r,
        .g = g,
        .b = b,
        .a = alpha,
        .radius = bin.radius,
        .borderW = borderW,
        .br = br,
        .bg = bg,
        .bb = bb,
        .token = inkToken,
        .icon = bin.icon,
        .disabled = (part.raw[1] & kBinDisabled) != 0,
        .shadow = shadow
    };

    const uint16_t id = spawn(s, host, flex, ink);
    if (id == kUiNone) {
        return kUiNone;
    }
    if (text != nullptr && text[0] != '\0') {
        uiText(s, id, text);
    }
    uiTextReadTail(s, id, part);
    if (bin.role < kUiBinRoleCap && s.onSpawn[bin.role] != nullptr) {
        s.onSpawn[bin.role](s, id);
    }
    if (bin.role < kUiBinRoleCap && s.roleFinish[bin.role] != nullptr) {
        s.roleFinish[bin.role](s, id, &part, text, hint != nullptr && hint[0] != '\0' ? hint : nullptr);
    }
    if ((role.flags & kBinRolePass) != 0) {
        s.ent[id].add<UiPass>();
    }
    finishDescribed(s, id, bin, name);
    return id;
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

    const uint32_t body = static_cast<uint32_t>(sizeof(BinHead) + (sizeof(BinNode) + sizeof(BinTail)) * head.count);
    if (body + head.pool > size) {
        return kUiNone;
    }

    const auto* nodes = reinterpret_cast<const BinNode*>(static_cast<const std::byte*>(bytes) + sizeof(BinHead));
    const auto* roles = reinterpret_cast<const BinTail*>(nodes + head.count);
    const uint16_t ncheck = (head.count < kUiCap) ? head.count : kUiCap;

    for (uint16_t i = 0; i < ncheck; ++i) {
        const BinNode& bin = nodes[i];
        const auto past = [&](uint16_t off, uint16_t len) noexcept {
            return len > 0 && static_cast<uint32_t>(off) + len > head.pool;
        };
        if (past(bin.nameOff, bin.nameLen) || past(bin.textOff, bin.textLen) || past(bin.hintOff, bin.hintLen)) {
            return kUiNone;
        }
    }

    const char* pool = reinterpret_cast<const char*>(static_cast<const std::byte*>(bytes) + body);
    uint16_t map[kUiCap];
    const uint16_t n = (head.count < kUiCap) ? head.count : kUiCap;
    for (uint16_t i = 0; i < n; ++i) {
        map[i] = kUiNone;
    }

    uint16_t root = kUiNone;

    for (uint16_t i = 0; i < n; ++i) {
        const BinNode& bin = nodes[i];
        uint16_t host = kUiNone;
        if (bin.parent == kUiNone) {
            host = parent;
        } else if (bin.parent == kBinParentRoot) {
            host = s.count > 0 ? static_cast<uint16_t>(0) : kUiNone;
        } else if (bin.parent < n) {
            host = map[bin.parent];
        }

        char name[kUiNameCap]{};
        char text[kUiTextCap]{};
        char hint[kUiHintCap]{};
        uint8_t nameLen = 0;
        uint8_t textLen = 0;
        uint8_t hintLen = 0;

        copyPool(name, nameLen, kUiNameCap, pool, head.pool, bin.nameOff, bin.nameLen);
        copyPool(text, textLen, kUiTextCap, pool, head.pool, bin.textOff, bin.textLen);
        copyPool(hint, hintLen, kUiHintCap, pool, head.pool, bin.hintOff, bin.hintLen);

        const uint16_t id = uiSpawnDescribed(s, host, bin, roles[i], name, text, hint);
        if (id == kUiNone) {
            continue;
        }
        map[i] = id;
        if (root == kUiNone && bin.parent == kUiNone) {
            root = id;
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
    if (mapped.size < sizeof(BinHead) || mapped.size > kUiMaxBinFileSize || mapped.data == nullptr) {
        fileUnmap(&mapped);
        return kUiNone;
    }
    const uint16_t root = uiImportBin(s, parent, mapped.data, static_cast<uint32_t>(mapped.size));
    fileUnmap(&mapped);
    return root;
}

} // namespace burnhope
