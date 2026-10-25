#include "host/PropGrid.hpp"
#include "host/PropRoll.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Search.hpp"

namespace burnhope {

namespace {

void propLinkSection(UiState& s, uint16_t wrap, uint8_t cat, uint8_t owner) {
    UiPropRoll& roll = s.world.ensure<UiPropRoll>();
    uint16_t tail = roll.sectionTail;
    if (tail >= s.count || !s.ent[tail].is_alive() || !s.ent[tail].has<UiPropSection>()) {
        tail = kUiNone;
        roll.sections = kUiNone;
    }
    s.ent[wrap].set<UiPropSection>({
        .next = kUiNone,
        .cat = cat,
        .owner = owner,
        .live = 1,
    });
    if (tail == kUiNone) {
        roll.sections = wrap;
    } else if (auto* prev = s.ent[tail].try_get_mut<UiPropSection>()) {
        prev->next = wrap;
    }
    roll.sectionTail = wrap;
}

template <typename Fn>
void propEachSection(UiState& s, Fn&& fn) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    uint16_t id = roll != nullptr ? roll->sections : kUiNone;
    while (id != kUiNone && id < s.count && s.ent[id].is_alive()) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            break;
        }
        const uint16_t next = section->next;
        if (!fn(id)) {
            break;
        }
        id = next;
    }
}

} // namespace

uint16_t uiPropSection(UiState& s, uint16_t parent, const char* title, uint8_t cat, uint8_t owner) {
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    const uint16_t wrap = uiPanel(s, parent, column, paintClear());
    if (wrap == kUiNone) {
        return wrap;
    }
    propLinkSection(s, wrap, cat, owner);
    const uint16_t body = uiFoldBody(s, wrap, title);
    propEachChild(s, wrap, [&](uint16_t id) {
        const UiFold* fold = s.ent[id].try_get<UiFold>();
        if (fold == nullptr || fold->body != body) {
            return true;
        }
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->padL = 0.0f;
            flex->marginL = 0.0f;
        }
        return false;
    });
    if (body < s.count) {
        if (auto* flex = s.ent[body].try_get_mut<UiFlex>()) {
            flex->pad = 0.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
            flex->padT = 0.0f;
            flex->padB = 0.0f;
        }
    }
    return body;
}

const char* propSectionTitle(const UiState& s, uint16_t wrap) {
    const char* title = "Component";
    propEachChild(s, wrap, [&](uint16_t head) {
        const UiText* text = s.ent[head].try_get<UiText>();
        if (text == nullptr || text->len == 0) {
            return true;
        }
        title = text->bytes;
        return false;
    });
    return title;
}

void onPropFilter(void* user, uint16_t id);

uint16_t propSpawnChip(UiState& s, uint16_t chips, uint16_t section, uint8_t all) {
    const uint16_t button = uiButton(s, chips, {.label = ""});
    if (button == kUiNone) {
        return button;
    }
    uiSetHeight(s, button, 22.0f);
    uiBind(s, button, onPropFilter, &s);
    s.ent[button].set<UiPropFilterChip>({
        .section = section,
        .all = all,
    });
    return button;
}

uint16_t propFindChip(const UiState& s, uint16_t chips, uint16_t section, uint8_t all) {
    uint16_t found = kUiNone;
    propEachChild(s, chips, [&](uint16_t child) {
        const UiPropFilterChip* chip = s.ent[child].try_get<UiPropFilterChip>();
        if (chip == nullptr) {
            return true;
        }
        if (all != 0) {
            if (chip->all == 0) {
                return true;
            }
            found = child;
            return false;
        }
        if (chip->all == 0 && chip->section == section) {
            found = child;
            return false;
        }
        return true;
    });
    return found;
}

uint16_t propReuseChip(UiState& s, uint16_t chips) {
    uint16_t found = kUiNone;
    propEachChild(s, chips, [&](uint16_t child) {
        auto* chip = s.ent[child].try_get_mut<UiPropFilterChip>();
        if (chip == nullptr || chip->all != 0 || chip->live != 0) {
            return true;
        }
        found = child;
        return false;
    });
    return found;
}

void uiPropRebuildFilters(UiState& s) {
    const uint16_t id = propBar(s);
    if (auto* bar = id < s.count ? s.ent[id].try_get_mut<UiPropFilterBar>() : nullptr) {
        const uint16_t chips = bar->chips;
        if (chips >= s.count) {
            return;
        }
        propEachChild(s, chips, [&](uint16_t child) {
            if (auto* chip = s.ent[child].try_get_mut<UiPropFilterChip>()) {
                chip->live = 0;
                uiShow(s, child, false);
            }
            return true;
        });
        bool whichLive = bar->which == kUiNone;
        propEachSection(s, [&](uint16_t section) {
            const UiPropSection* prop = s.ent[section].try_get<UiPropSection>();
            if (prop == nullptr || prop->live == 0) {
                return true;
            }
            if (section == bar->which) {
                whichLive = true;
            }
            uint16_t button = propFindChip(s, chips, section, 0);
            if (button == kUiNone) {
                button = propReuseChip(s, chips);
            }
            if (button == kUiNone) {
                button = propSpawnChip(s, chips, section, 0);
            }
            if (button == kUiNone) {
                return true;
            }
            if (auto* chip = s.ent[button].try_get_mut<UiPropFilterChip>()) {
                chip->section = section;
                chip->all = 0;
                chip->live = 1;
            }
            uiShow(s, button, true);
            uiText(s, button, propSectionTitle(s, section));
            return true;
        });
        uint16_t all = propFindChip(s, chips, kUiNone, 1);
        if (all == kUiNone) {
            all = propSpawnChip(s, chips, kUiNone, 1);
        }
        if (all != kUiNone) {
            if (auto* chip = s.ent[all].try_get_mut<UiPropFilterChip>()) {
                chip->live = 1;
                chip->all = 1;
            }
            uiShow(s, all, true);
            uiText(s, all, "All");
        }
        if (!whichLive) {
            bar->which = kUiNone;
        }
    }
}

bool propQueryHas(const char* text, uint8_t len, const char* query, uint8_t qlen) {
    if (qlen == 0) {
        return true;
    }
    if (text == nullptr || len < qlen) {
        return false;
    }
    for (uint8_t i = 0; i + qlen <= len; ++i) {
        bool same = true;
        for (uint8_t k = 0; k < qlen; ++k) {
            char a = text[i + k];
            char b = query[k];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<char>(b - 'A' + 'a');
            }
            if (a != b) {
                same = false;
                break;
            }
        }
        if (same) {
            return true;
        }
    }
    return false;
}

void propQueryOf(const UiState& s, const UiPropFilterBar& bar, const char*& text, uint8_t& len) {
    text = nullptr;
    len = 0;
    if (bar.search >= s.count) {
        return;
    }
    const UiField* field = s.ent[bar.search].try_get<UiField>();
    if (field == nullptr || field->len == 0) {
        return;
    }
    text = field->bytes;
    len = field->len;
}

void uiPropFilterApply(UiState& s) {
    uint16_t which = kUiNone;
    uint16_t chips = kUiNone;
    const char* query = nullptr;
    uint8_t qlen = 0;
    bool searching = false;
    if (const uint16_t id = propBar(s); id < s.count) {
        const UiPropFilterBar* bar = s.ent[id].try_get<UiPropFilterBar>();
        if (bar != nullptr) {
            which = bar->which;
            chips = bar->chips;
            propQueryOf(s, *bar, query, qlen);
            searching = qlen > 0;
            propEachChild(s, chips, [&](uint16_t child) {
                const UiPropFilterChip* chip = s.ent[child].try_get<UiPropFilterChip>();
                if (chip == nullptr || chip->live == 0) {
                    return true;
                }
                const bool on = chip->all != 0 ? which == kUiNone : chip->section == which;
                auto* paint = s.ent[child].try_get_mut<UiPaint>();
                if (paint == nullptr) {
                    return true;
                }
                *paint = paintToken(s.look, on ? UiToken::Accent : UiToken::Button, s.look.radiusSm);
                return true;
            });
        }
    }
    propEachLabel(s, [&](uint16_t id) {
        const uint16_t row = s.parentOf[id];
        if (row < s.count) {
            const UiText* text = s.ent[id].try_get<UiText>();
            const bool match = !searching || (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen));
            uiShow(s, row, match);
        }
        return true;
    });
    propEachSection(s, [&](uint16_t id) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            return true;
        }
        if (searching) {
            bool any = false;
            propEachLabel(s, [&](uint16_t label) {
                if (!propUnder(s, label, id)) {
                    return true;
                }
                const UiText* text = s.ent[label].try_get<UiText>();
                if (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen)) {
                    any = true;
                    return false;
                }
                return true;
            });
            uiShow(s, id, section->live != 0 && any);
            return true;
        }
        const bool match = which == kUiNone || which == id;
        uiShow(s, id, section->live != 0 && match);
        return true;
    });
}

void onPropSearch(void* user, const char*) {
    auto* s = static_cast<UiState*>(user);
    if (s != nullptr) {
        uiPropFilterApply(*s);
    }
}

void onPropFilter(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    uint16_t bar = s->parentOf[id];
    while (bar != kUiNone && bar < s->count && !s->ent[bar].has<UiPropFilterBar>()) {
        bar = s->parentOf[bar];
    }
    auto* filt = bar < s->count ? s->ent[bar].try_get_mut<UiPropFilterBar>() : nullptr;
    if (filt == nullptr) {
        return;
    }
    const UiPropFilterChip* chip = s->ent[id].try_get<UiPropFilterChip>();
    if (chip == nullptr) {
        return;
    }
    filt->which = chip->all != 0 ? kUiNone : chip->section;
    uiPropFilterApply(*s);
}

uint16_t uiPropFilterBar(UiState& s, uint16_t parent) {
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    column.padL = 12.0f;
    column.padR = 8.0f;
    column.padT = 10.0f;
    column.padB = 8.0f;
    column.gap = 8.0f;
    const uint16_t bar = uiPanel(s, parent, column, paintToken(s.look, UiToken::Content, 0.0f));
    if (bar == kUiNone) {
        return bar;
    }
    s.ent[bar].set<UiPropFilterBar>({});
    s.world.ensure<UiPropRoll>().bar = bar;
    auto* filt = s.ent[bar].try_get_mut<UiPropFilterBar>();
    const uint16_t search = uiSearchField(s, bar, "Search", onPropSearch, &s, -1.0f);
    if (filt != nullptr) {
        filt->search = search;
    }
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.wrap = UiFlexWrap::Wrap;
    row.gap = 4.0f;
    row.shrink = 0.0f;
    row.minH = 22.0f;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    const uint16_t chips = uiPanel(s, bar, row, paintClear());
    if (filt != nullptr) {
        filt->chips = chips;
        filt->which = kUiNone;
    }
    return bar;
}

void uiPropShowMask(UiState& s, uint8_t mask) {
    propEachSection(s, [&](uint16_t id) {
        auto* section = s.ent[id].try_get_mut<UiPropSection>();
        if (section != nullptr) {
            section->live = (section->owner & mask) != 0 ? 1 : 0;
        }
        return true;
    });
    uiPropRebuildFilters(s);
    uiPropFilterApply(s);
}

} // namespace burnhope
