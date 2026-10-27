#include "layout/GuiDock.hpp"

#include <algorithm>
#include <cmath>

#include "gui/GuiIcons.hpp"
#include "interaction/InteractState.hpp"
#include "layout/GuiLayoutEngine.hpp"
#include "style/GuiStyleResolver.hpp"
#include "text/GuiTextEngine.hpp"

namespace burnhope::gui {
namespace {

struct DockQuery {
    flecs::query<UiDock> docks;
};

const DockQuery& dockQuery(flecs::world& world) {
    if (const DockQuery* cached = world.try_get<DockQuery>()) {
        return *cached;
    }
    world.set<DockQuery>({world.query<UiDock>()});
    return *world.try_get<DockQuery>();
}

flecs::entity spawn(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity parent, const char* name, const char* cls) {
    flecs::entity e = world.entity().child_of(parent);
    e.add(flecs::OrderedChildren);
    if (name != nullptr) {
        e.set_name(name);
    }
    e.set<UiBox>(UiBox{});
    e.set<UiPaint>(UiPaint{});
    e.set<UiText>(UiText{});
    if (!guiLayoutAttach(e)) {
        return e;
    }
    const uint32_t id = guiStyleFind(sheet, cls);
    if (id != kGuiNoClass) {
        guiStyleApply(sheet, e, id);
    }
    if (UiPaint* p = e.try_get_mut<UiPaint>(); p != nullptr && p->token == 0) {
        p->tint[3] = 0.0f;
    }
    return e;
}

// Child entities of `parent` in layout order.
uint32_t childrenOf(flecs::world& world, flecs::entity parent, flecs::entity* out, uint32_t cap) {
    const UiFlex* flex = parent.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return 0;
    }
    const size_t count = std::min<size_t>(YGNodeGetChildCount(flex->node), cap);
    for (size_t i = 0; i < count; ++i) {
        out[i] = guiLayoutEntity(world, YGNodeGetChild(flex->node, i));
    }
    return static_cast<uint32_t>(count);
}

bool isOpenPanel(flecs::entity e) {
    const UiDockPanel* p = e.id() != 0 && e.is_alive() ? e.try_get<UiDockPanel>() : nullptr;
    return p != nullptr && p->closed == 0;
}

void setClass(const GuiStyleSheet& sheet, flecs::entity e, const char* cls) {
    const uint32_t id = guiStyleFind(sheet, cls);
    const UiClass* current = e.try_get<UiClass>();
    if (id != kGuiNoClass && (current == nullptr || current->id != id)) {
        guiStyleApply(sheet, e, id);
    }
}

void sizeTab(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity tab) {
    const UiFlex* flex = tab.try_get<UiFlex>();
    const UiText* text = tab.try_get<UiText>();
    if (flex == nullptr || flex->node == nullptr || text == nullptr) {
        return;
    }
    const float textW = guiTextXFromCaret(sheet.text, std::string_view(text->text, text->len), static_cast<float>(text->size), text->len);
    YGNodeStyleSetWidth(flex->node, std::ceil(kGuiTabPadL + textW + kGuiTabTextSlack + kGuiTabPadR));
    (void)world;
}

void refreshDock(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity dock) {
    UiDock* d = dock.try_get_mut<UiDock>();
    if (d == nullptr) {
        return;
    }
    flecs::entity kids[kGuiDockPanelCap + 1];
    const uint32_t n = childrenOf(world, dock, kids, kGuiDockPanelCap + 1);
    flecs::entity active = guiEntityOf(world, d->active);
    if (!isOpenPanel(active) || active.parent() != dock) {
        active = flecs::entity();
        for (uint32_t i = 0; i < n && active.id() == 0; ++i) {
            if (kids[i].has<UiDockPanel>() && isOpenPanel(kids[i])) {
                active = kids[i];
            }
        }
    }
    if (active.id() == 0 && !dock.has<UiHidden>()) {
        if (d->shown == 0) { // something showed an empty dock: bring its first panel back instead of leaving a dead frame
            for (uint32_t i = 0; i < n && active.id() == 0; ++i) {
                if (kids[i].has<UiDockPanel>()) {
                    kids[i].try_get_mut<UiDockPanel>()->closed = 0;
                    active = kids[i];
                }
            }
        }
        if (active.id() == 0) {
            guiSetShown(dock, false);
        }
    }
    d->active = active.id();
    d->shown = dock.has<UiHidden>() ? 0 : 1;
    for (uint32_t i = 0; i < n; ++i) {
        if (!kids[i].has<UiDockPanel>()) {
            continue;
        }
        const bool want = kids[i] == active;
        if (kids[i].has<UiHidden>() == want) {
            guiSetShown(kids[i], want);
        }
    }
    const flecs::entity bar = guiDockBar(dock);
    flecs::entity tabs[kGuiDockPanelCap + 1];
    const uint32_t tabCount = bar.id() != 0 ? childrenOf(world, bar, tabs, kGuiDockPanelCap + 1) : 0;
    for (uint32_t i = 0; i < tabCount; ++i) {
        const UiTab* tab = tabs[i].try_get<UiTab>();
        if (tab == nullptr) {
            continue;
        }
        const flecs::entity panel = guiEntityOf(world, tab->panel);
        const bool open = isOpenPanel(panel);
        if (tabs[i].has<UiHidden>() == open) {
            guiSetShown(tabs[i], open);
        }
        setClass(sheet, tabs[i], open && panel == active ? "tab_active" : "tab");
        sizeTab(world, sheet, tabs[i]);
    }
}

} // namespace

flecs::entity guiDockBar(flecs::entity dock) {
    return dock.has<UiDock>() ? dock.lookup("tabs") : flecs::entity();
}

flecs::entity guiDockCreateBar(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity dock) {
    flecs::entity bar = spawn(world, sheet, dock, "tabs", "tab_bar");
    bar.add<UiTabBar>();
    return bar;
}

bool guiDockAddPanel(flecs::world& world, const GuiStyleSheet& sheet, flecs::entity dock, flecs::entity panel, std::string_view title,
                     uint8_t icon, bool closed) {
    const flecs::entity bar = guiDockBar(dock);
    if (bar.id() == 0) {
        return false;
    }
    panel.set<UiDockPanel>(UiDockPanel{.closed = static_cast<uint8_t>(closed ? 1 : 0)});
    flecs::entity tab = spawn(world, sheet, bar, nullptr, "tab");
    tab.set<UiTab>(UiTab{.panel = panel.id()});
    tab.add<UiHoverable>();
    UiText* text = tab.try_get_mut<UiText>();
    UiPaint* paint = tab.try_get_mut<UiPaint>();
    if (text == nullptr || paint == nullptr) {
        return false;
    }
    const size_t len = std::min<size_t>(title.size(), kGuiTextCap - 1);
    std::copy_n(title.data(), len, text->text);
    text->len = static_cast<uint8_t>(len);
    paint->icon = icon;
    flecs::entity close = spawn(world, sheet, tab, nullptr, "tab_close");
    close.add<UiTabClose>();
    close.add<UiHoverable>();
    if (UiPaint* cp = close.try_get_mut<UiPaint>()) {
        cp->icon = guiIconFind("x");
    }
    return true;
}

void guiDockRefresh(flecs::world& world, const GuiStyleSheet& sheet) {
    flecs::entity docks[kGuiDockCap];
    uint32_t n = 0;
    dockQuery(world).docks.each([&](flecs::entity e, UiDock&) {
        if (n < kGuiDockCap) {
            docks[n++] = e;
        }
    });
    for (uint32_t i = 0; i < n; ++i) {
        refreshDock(world, sheet, docks[i]);
    }
}

void guiDockSelect(flecs::entity panel) {
    const flecs::entity dock = panel.parent();
    UiDock* d = dock.id() != 0 && dock.is_alive() ? dock.try_get_mut<UiDock>() : nullptr;
    if (d != nullptr && isOpenPanel(panel)) {
        d->active = panel.id();
    }
}

void guiDockClose(flecs::entity panel) {
    if (UiDockPanel* p = panel.id() != 0 && panel.is_alive() ? panel.try_get_mut<UiDockPanel>() : nullptr) {
        p->closed = 1;
    }
}

void guiDockOpen(flecs::world& world, flecs::entity panel) {
    UiDockPanel* p = panel.id() != 0 && panel.is_alive() ? panel.try_get_mut<UiDockPanel>() : nullptr;
    const flecs::entity dock = panel.parent();
    if (p == nullptr || !dock.has<UiDock>()) {
        return;
    }
    p->closed = 0;
    dock.try_get_mut<UiDock>()->active = panel.id();
    if (dock.has<UiHidden>()) {
        guiSetShown(dock, true);
        dock.try_get_mut<UiDock>()->shown = 1;
    }
    (void)world;
}

uint32_t guiDockDropIndex(flecs::world& world, flecs::entity tab, flecs::entity bar, float px) {
    flecs::entity kids[kGuiDockPanelCap + 1];
    const uint32_t n = childrenOf(world, bar, kids, kGuiDockPanelCap + 1);
    uint32_t index = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (kids[i] == tab) {
            continue;
        }
        const UiBox* b = kids[i].try_get<UiBox>();
        if (!kids[i].has<UiHidden>() && b != nullptr && px < b->x + b->w * 0.5f) {
            return index;
        }
        ++index;
    }
    return index;
}

bool guiDockMoveTab(flecs::world& world, flecs::entity tab, flecs::entity bar, uint32_t index) {
    const UiTab* t = tab.is_alive() ? tab.try_get<UiTab>() : nullptr;
    const flecs::entity panel = t != nullptr ? guiEntityOf(world, t->panel) : flecs::entity();
    if (t == nullptr || panel.id() == 0 || !bar.has<UiTabBar>()) {
        return false;
    }
    const flecs::entity newDock = bar.parent();
    const flecs::entity oldDock = panel.parent();
    const UiFlex* barFlex = bar.try_get<UiFlex>();
    const UiFlex* tabFlex = tab.try_get<UiFlex>();
    if (!newDock.has<UiDock>() || barFlex == nullptr || tabFlex == nullptr || barFlex->node == nullptr || tabFlex->node == nullptr) {
        return false;
    }
    const flecs::entity oldBar = tab.parent();
    if (const UiFlex* of = oldBar.try_get<UiFlex>(); of != nullptr && of->node != nullptr) {
        YGNodeRemoveChild(of->node, tabFlex->node);
    }
    if (oldBar != bar) {
        tab.remove(flecs::ChildOf, oldBar);
        tab.child_of(bar);
    }
    YGNodeInsertChild(barFlex->node, tabFlex->node, std::min<size_t>(index, YGNodeGetChildCount(barFlex->node)));

    if (newDock != oldDock) {
        const UiFlex* newFlex = newDock.try_get<UiFlex>();
        const UiFlex* panelFlex = panel.try_get<UiFlex>();
        if (newFlex == nullptr || panelFlex == nullptr || newFlex->node == nullptr || panelFlex->node == nullptr) {
            return false;
        }
        if (const UiFlex* of = oldDock.try_get<UiFlex>(); of != nullptr && of->node != nullptr) {
            YGNodeRemoveChild(of->node, panelFlex->node);
        }
        panel.remove(flecs::ChildOf, oldDock);
        panel.child_of(newDock);
        YGNodeInsertChild(newFlex->node, panelFlex->node, YGNodeGetChildCount(newFlex->node));
    }
    guiDockOpen(world, panel);
    return true;
}

} // namespace burnhope::gui
