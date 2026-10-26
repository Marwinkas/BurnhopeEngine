#include "ui/widget/Choice.hpp"

#include "ui/widget/Menu.hpp"
#include "ui/State.hpp"

#include <cstring>
#include <cstdint>

namespace burnhope {
namespace {

constexpr uint32_t kChoiceRowCap = 128;

const char* choiceLabel(const UiState& s, uint16_t menu, uint8_t index) {
    const uint16_t child = uiNodeChild(s, menu, index);
    if (child >= s.count) {
        return nullptr;
    }
    const UiText* text = s.ent[child].try_get<UiText>();
    return text != nullptr ? text->bytes : nullptr;
}

uint16_t loadChoice(UiState& s, uint16_t parent, const BinNode& node, const BinTail& role, const char*, const char* text, const char*, void*) {
    if (text == nullptr || std::strchr(text, '\n') == nullptr) {
        return kUiNone;
    }
    char store[2048]{};
    const char* labels[255];
    uint8_t count = 0;
    uint16_t used = 0;
    const char* cursor = text;
    while (*cursor != '\0' && count < 255 && used + 1u < sizeof(store)) {
        const uint16_t at = used;
        uint8_t len = 0;
        while (*cursor != '\0' && *cursor != '\n' && used + 1u < sizeof(store)) {
            store[used++] = *cursor++;
            ++len;
        }
        store[used++] = '\0';
        if (*cursor == '\n') {
            ++cursor;
        }
        if (len > 0) {
            labels[count++] = store + at;
        }
    }
    if (count == 0) {
        return kUiNone;
    }
    float picked = 0.0f;
    std::memcpy(&picked, role.raw + 18, sizeof(picked));
    uint8_t selected = static_cast<uint8_t>(picked);
    if (selected >= count) {
        selected = 0;
    }
    return uiChoice(s, parent, {
        .labels = labels,
        .count = count,
        .selected = selected,
        .width = node.width > 48.0f ? node.width : 140.0f,
    });
}

} // namespace

void uiChoiceTake(UiState& state, uint16_t owner, uint16_t row, uint16_t item) {
    if (owner >= state.count) {
        return;
    }
    auto* choice = state.ent[owner].try_get_mut<UiChoice>();
    if (choice == nullptr) {
        return;
    }
    choice->index = item > 255 ? uint8_t{255} : static_cast<uint8_t>(item);
    const uint16_t caption = choice->caption < state.count ? choice->caption : owner;
    const UiText* text = row < state.count ? state.ent[row].try_get<UiText>() : nullptr;
    if (text != nullptr && caption < state.count) {
        uiText(state, caption, text->bytes);
    }
    if (choice->onPick != nullptr) {
        choice->onPick(choice->user, choice->index);
    }
}

uint16_t uiChoice(UiState& s, uint16_t parent, const UiChoiceConfig& cfg) {
    if (cfg.labels == nullptr || cfg.count == 0) {
        return kUiNone;
    }
    const uint8_t selected = cfg.selected < cfg.count ? cfg.selected : 0;
    const uint16_t menu = uiMakeMenu(s);
    if (menu == kUiNone) {
        return kUiNone;
    }
    const uint16_t id = uiMenuButton(s, parent, cfg.labels[selected] != nullptr ? cfg.labels[selected] : "", menu);
    if (id == kUiNone) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->widthMode = UiSize::Px;
        flex->width = cfg.width > 1.0f ? cfg.width : 140.0f;
        flex->grow = 0.0f;
        flex->shrink = 0.0f;
    }
    for (uint8_t i = 0; i < cfg.count; ++i) {
        (void)uiMenuRow(s, menu, cfg.labels[i] != nullptr ? cfg.labels[i] : "", nullptr, nullptr, i, id, kUiNone);
    }
    s.ent[id].set<UiChoice>({
        .menu = menu,
        .caption = id,
        .index = selected,
        .onPick = cfg.onPick,
        .user = cfg.user,
    });
    return id;
}

void uiChoiceSync(UiState& s, uint16_t id, uint16_t count, const char* (*labelAt)(void* user, uint16_t index), void* user, const char* caption) {
    const UiChoice* choice = id < s.count ? s.ent[id].try_get<UiChoice>() : nullptr;
    if (choice == nullptr || choice->menu >= s.count || labelAt == nullptr) {
        return;
    }
    const uint16_t menu = choice->menu;
    uint32_t rows = uiNodeChildCount(s, menu);
    while (rows < count && rows < kChoiceRowCap && s.count + 4u < kUiCap) {
        (void)uiMenuRow(s, menu, labelAt(user, static_cast<uint16_t>(rows)), nullptr, nullptr, static_cast<uint16_t>(rows), id, kUiNone);
        ++rows;
    }
    for (uint32_t i = 0; i < rows; ++i) {
        const uint16_t row = uiNodeChild(s, menu, i);
        const bool live = i < count;
        uiText(s, row, live ? labelAt(user, static_cast<uint16_t>(i)) : "");
        uiShow(s, row, live);
    }
    if (caption != nullptr) {
        uiMenuCaption(s, id, caption);
    }
}

void uiBindChoice(UiState& s, const char* name, uint8_t selected, void (*onPick)(void* user, uint8_t index), void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* choice = id < s.count ? s.ent[id].try_get_mut<UiChoice>() : nullptr;
    if (choice == nullptr) {
        return;
    }
    choice->onPick = onPick;
    choice->user = user;
    if (selected == choice->index) {
        return;
    }
    choice->index = selected;
    const char* label = choiceLabel(s, choice->menu, selected);
    if (label != nullptr && choice->caption < s.count) {
        uiText(s, choice->caption, label);
    }
}

float uiChoiceWidth(const UiState& s, const char* const* labels, uint8_t count) {
    float widest = 0.0f;
    if (labels == nullptr) {
        return 26.0f;
    }
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t len = 0;
        const char* label = labels[i];
        while (label != nullptr && label[len] != '\0' && len < 48) {
            ++len;
        }
        const float measured = uiMeasure(s, label, len, len);
        if (measured > widest) {
            widest = measured;
        }
    }
    return widest + 26.0f;
}

void uiChoiceFit(UiState& s, uint16_t button, float width) {
    if (button >= s.count) {
        return;
    }
    if (auto* flex = s.ent[button].try_get_mut<UiFlex>()) {
        flex->widthMode = UiSize::Px;
        flex->width = width;
        flex->minW = 0.0f;
        flex->grow = 0.0f;
        flex->shrink = 0.0f;
        flex->heightMode = UiSize::Px;
        flex->height = 28.0f;
        flex->self = UiAlignSelf::Center;
        flex->padL = 6.0f;
        flex->padR = 22.0f;
        flex->gap = 2.0f;
    }
    const UiChoice* choice = s.ent[button].try_get<UiChoice>();
    if (choice != nullptr && choice->menu < s.count) {
        s.ent[choice->menu].add<UiMenuSlide>();
        if (auto* flex = s.ent[choice->menu].try_get_mut<UiFlex>()) {
            flex->width = width;
        }
    }
}

void uiChoiceBind(UiState& s) {
    uiBinRole(s, kBinSlotChoice, "choice", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotChoice, loadChoice, nullptr);
}

} // namespace burnhope
