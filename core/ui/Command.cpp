#include "ui/Command.hpp"

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

void commandListClear(UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (s.ent[i].is_alive() && s.ent[i].has<UiCommandList>()) {
            s.ent[i].remove<UiCommandList>();
        }
    }
}

} // namespace

void uiCommandStore(UiState& s, uint16_t row, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command) {
    if (row >= s.count) {
        return;
    }
    s.ent[row].set<UiMenuCmd>({
        .item = item,
        .command = command,
        .owner = owner,
        .fn = fn,
        .user = user,
    });
    uiBind(s, row, commandInvoke, &s);
}

void uiCommandBind(UiState& s) {
    uiClearAdd(s, commandListClear);
}

} // namespace burnhope
