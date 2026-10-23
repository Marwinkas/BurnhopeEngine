#include "ui/Command.hpp"

#include "Str.hpp"
#include "ui/Hover.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

void commandInvoke(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    void (*hook)(void*, uint16_t) = uiMenuHooks(s).invoke;
    if (hook != nullptr) {
        hook(user, id);
        return;
    }
    const UiMenuCmd* row = id < s.count ? s.ent[id].try_get<UiMenuCmd>() : nullptr;
    if (row != nullptr && row->fn != nullptr) {
        row->fn(row->user, row->item);
    }
}

uint16_t siblingIndex(const UiState& s, uint16_t menu) {
    uint16_t n = 0;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == menu) {
            ++n;
        }
    }
    return n;
}

} // namespace

void uiMenuRowBind(UiState& s, uint16_t row, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command) {
    if (row >= s.count) {
        return;
    }
    if (auto* flex = s.ent[row].try_get_mut<UiFlex>()) {
        flex->heightMode = UiSize::Px;
        flex->height = uiMenuMetrics(s).itemH;
        flex->widthMode = UiSize::Percent;
        flex->width = 100.0f;
        flex->shrink = 0.0f;
        flex->align = UiAlign::Start;
        flex->justify = UiJustify::Start;
        flex->padL = 8.0f;
    }
    if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
        const float radius = paint->radius;
        *paint = paintToken(s.look, UiToken::Item, radius > 0.0f ? radius : s.look.radiusSm);
        paint->a = 0.0f;
        paint->borderW = 0.0f;
        paint->disabled = false;
    } else {
        s.ent[row].set<UiPaint>(paintToken(s.look, UiToken::Item, s.look.radiusSm));
        if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
        }
    }
    uiText(s, row, label != nullptr ? label : "");
    uiTextStyle(s, row, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    s.ent[row].set<UiMenuCmd>({
        .item = item,
        .command = command,
        .owner = owner,
        .fn = fn,
        .user = user,
    });
    s.ent[row].add<UiFocusable>();
    uiHover(s, row);
    uiBind(s, row, commandInvoke, &s);
}

uint16_t uiMenuRow(UiState& s, uint16_t menu, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command) {
    if (menu >= s.count) {
        return kUiNone;
    }
    UiFlex itemF{};
    itemF.heightMode = UiSize::Px;
    itemF.height = uiMenuMetrics(s).itemH;
    itemF.widthMode = UiSize::Percent;
    itemF.width = 100.0f;
    itemF.shrink = 0.0f;
    itemF.align = UiAlign::Start;
    itemF.justify = UiJustify::Start;
    itemF.padL = 8.0f;
    const uint16_t row = spawn(s, menu, itemF, paintToken(s.look, UiToken::Item, s.look.radiusSm));
    if (row == kUiNone || row >= s.count) {
        return kUiNone;
    }
    uiMenuRowBind(s, row, label, fn, user, item, owner, command);
    return row;
}

bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user) {
    if (s.cmdCount >= kUiCmdCap || menu == kUiNone || menu >= s.count) {
        if (s.cmdCount >= kUiCmdCap) {
            spdlog::warn("ui command cap {} full, dropped {}", static_cast<unsigned>(kUiCmdCap), name != nullptr ? name : "");
        }
        return false;
    }
    const uint8_t slot = s.cmdCount;
    UiCommand& cmd = s.cmds[slot];
    cmd.len = uiStrCopy(cmd.name, name);
    cmd.fn = fn;
    cmd.user = user;
    const uint16_t item = siblingIndex(s, menu);
    const uint16_t row = uiMenuRow(s, menu, name, fn, user, item, kUiNone, slot);
    if (row == kUiNone) {
        return false;
    }
    ++s.cmdCount;
    uiNotifyLayoutDirty(s);
    return true;
}

bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user) {
    return uiCommandAddTo(s, s.menu, name, fn, user);
}

} // namespace burnhope
