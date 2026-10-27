#include "host/GuiHost.hpp"

#include <cstring>
#include <new>
#include <string_view>

#include <flecs.h>
#include <spdlog/spdlog.h>

#include "Str.hpp"
#include "gui/Gui.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/loader/GuiLoader.hpp"
#include "gui/text/GuiTextEngine.hpp"
#include "io/File.hpp"

namespace burnhope {

namespace {

constexpr uint32_t kBindCap = 64;
constexpr uint32_t kEventCap = 64;
constexpr uint32_t kTextCap = gui::kGuiTextCap;

enum class BindKind : uint8_t { None = 0, Click, Drag, Edit };

struct Bind {
    BindKind kind = BindKind::None;
    uint32_t action = 0;
    uint64_t entity = 0;
    GuiClickFn click = nullptr;
    GuiValueFn value = nullptr;
    GuiTextFn text = nullptr;
    void* user = nullptr;
    float last = 0.0f;
    char lastText[kTextCap] = {};
};

struct Event {
    uint32_t bind = 0;
    float value = 0.0f;
    char text[kTextCap] = {};
};

} // namespace

struct GuiHost {
    flecs::world world;
    gui::GuiStyleSheet style{};
    gui::GuiContext ctx{};
    flecs::entity root;
    Canvas* canvas = nullptr;
    float appliedPx = 0.0f; // atlas size the text metrics were last taken from
    Bind binds[kBindCap]{};
    uint32_t bindCount = 0;
    Event events[kEventCap]{};
    uint32_t eventCount = 0;
};

namespace {

bool applyFontMetrics(const UiFontAtlas& atlas, gui::GuiStyleSheet& sheet) {
    float advances[kUiFontGlyphs]{};
    for (uint32_t i = 0; i < kUiFontGlyphs; ++i) {
        advances[i] = atlas.glyphs[i].advance;
    }
    return gui::guiTextMetricsSetAdvances(sheet.text, advances, kUiFontGlyphs, atlas.fontPx);
}

void push(GuiHost& g, uint32_t bind, float value, const char* text) {
    if (g.eventCount >= kEventCap) {
        return;
    }
    Event& e = g.events[g.eventCount++];
    e.bind = bind;
    e.value = value;
    e.text[0] = '\0';
    if (text != nullptr) {
        (void)strCopyCap(e.text, sizeof(e.text), text);
    }
}

float dragReal(const gui::UiDraggable& d) {
    return d.minV + d.value * (d.maxV - d.minV);
}

// Queues the events of this frame: click actions, changed drag values and changed edit texts.
void collect(GuiHost& g) {
    const gui::GuiInteractState& in = g.ctx.interact;
    for (uint32_t b = 0; b < g.bindCount; ++b) {
        Bind& bind = g.binds[b];
        if (bind.kind == BindKind::Click) {
            for (uint32_t i = 0; i < in.actionCount && i < gui::kGuiActionCap; ++i) {
                if (in.actions[i].action == bind.action) {
                    push(g, b, 0.0f, nullptr);
                }
            }
            continue;
        }
        const flecs::entity e = g.world.entity(bind.entity);
        if (!e.is_alive()) {
            continue;
        }
        if (bind.kind == BindKind::Drag) {
            const gui::UiDraggable* d = e.try_get<gui::UiDraggable>();
            if (d != nullptr && dragReal(*d) != bind.last) {
                bind.last = dragReal(*d);
                push(g, b, bind.last, nullptr);
            }
        } else if (bind.kind == BindKind::Edit) {
            const gui::UiText* t = e.try_get<gui::UiText>();
            if (t != nullptr && std::strncmp(t->text, bind.lastText, kTextCap) != 0) {
                (void)strCopyCap(bind.lastText, sizeof(bind.lastText), t->text);
                push(g, b, 0.0f, bind.lastText);
            }
        }
    }
}

// Runs inside canvasConsume: the GUI appends its primitives to the shared slot-48 buffer.
uint32_t guiLayer(void* user, const InputFrame& input, float w, float h, UiPrimitive* dst, uint32_t cap, CanvasFeedback& fb) {
    auto* g = static_cast<GuiHost*>(user);
    const UiFontAtlas& font = g->canvas->font;
    if (font.ready && font.fontPx != g->appliedPx && applyFontMetrics(font, g->style)) {
        g->world.set<gui::GuiTextMetrics>(g->style.text);
        g->appliedPx = font.fontPx;
    }
    gui::guiSetPrimBuffer(g->ctx, dst, cap);
    gui::guiConsumeInput(g->ctx, input);
    gui::guiLayout(g->ctx, w, h);
    gui::guiRender(g->ctx, VK_NULL_HANDLE);
    collect(*g);

    const gui::GuiInteractState& in = g->ctx.interact;
    fb.pointerOver = in.under != 0 || in.capture != 0;
    fb.wantsText = in.focus != 0 && g->world.entity(in.focus).has<gui::UiEditable>();
    return g->ctx.primCount;
}

bool loadScreen(GuiHost& g, const char* path) {
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        return false;
    }
    const bool ok = gui::guiLoadScreen(g.world, g.style, std::string_view{reinterpret_cast<const char*>(file.data), file.size},
                                       flecs::entity(), &g.root);
    fileUnmap(&file);
    return ok;
}

Bind* addBind(GuiHost* g, BindKind kind, const char* path, bool hasFn, void* user) {
    if (g == nullptr || !hasFn || g->bindCount >= kBindCap) {
        return nullptr;
    }
    uint64_t entity = 0;
    if (kind != BindKind::Click) {
        const flecs::entity e = path != nullptr ? g->root.lookup(path) : flecs::entity();
        if (!e.is_valid()) {
            spdlog::error("gui: bind target {} not found", path != nullptr ? path : "(null)");
            return nullptr;
        }
        entity = e.id();
    }
    Bind& b = g->binds[g->bindCount++];
    b = {};
    b.kind = kind;
    b.entity = entity;
    b.user = user;
    return &b;
}

flecs::entity target(GuiHost* g, const char* path) {
    return g != nullptr && path != nullptr ? g->root.lookup(path) : flecs::entity();
}

} // namespace

GuiHost* guiHostCreate(Canvas& canvas, const char* stylesPath, const char* screenPath) {
    auto* g = new (std::nothrow) GuiHost();
    if (g == nullptr) {
        return nullptr;
    }
    g->canvas = &canvas;
    g->ctx.style = &g->style;
    g->ctx.writeHeader = false;
    if (!gui::guiStyleLoadFile(g->style, stylesPath) || !gui::guiInit(g->ctx, g->world) || !loadScreen(*g, screenPath)) {
        spdlog::error("gui: failed to load {} / {}", stylesPath, screenPath);
        guiHostDestroy(g);
        return nullptr;
    }
    canvas.layer = guiLayer;
    canvas.layerUser = g;
    return g;
}

void guiHostDestroy(GuiHost* gui) {
    if (gui == nullptr) {
        return;
    }
    if (gui->canvas != nullptr && gui->canvas->layerUser == gui) {
        gui->canvas->layer = nullptr;
        gui->canvas->layerUser = nullptr;
    }
    if (gui->root.is_valid()) {
        gui::guiLayoutDetach(gui->root);
    }
    delete gui;
}

bool guiHostBindClick(GuiHost* gui, uint32_t action, GuiClickFn fn, void* user) {
    Bind* b = addBind(gui, BindKind::Click, nullptr, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    b->click = fn;
    b->action = action;
    return true;
}

bool guiHostBindDrag(GuiHost* gui, const char* path, GuiValueFn fn, void* user) {
    Bind* b = addBind(gui, BindKind::Drag, path, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    b->value = fn;
    const gui::UiDraggable* d = gui->world.entity(b->entity).try_get<gui::UiDraggable>();
    if (d == nullptr) {
        spdlog::error("gui: {} is not draggable", path);
        --gui->bindCount;
        return false;
    }
    b->last = dragReal(*d);
    return true;
}

bool guiHostBindEdit(GuiHost* gui, const char* path, GuiTextFn fn, void* user) {
    Bind* b = addBind(gui, BindKind::Edit, path, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    b->text = fn;
    const gui::UiText* t = gui->world.entity(b->entity).try_get<gui::UiText>();
    if (t == nullptr || !gui->world.entity(b->entity).has<gui::UiEditable>()) {
        spdlog::error("gui: {} is not editable", path);
        --gui->bindCount;
        return false;
    }
    (void)strCopyCap(b->lastText, sizeof(b->lastText), t->text);
    return true;
}

void guiHostSetValue(GuiHost* gui, const char* path, float value) {
    flecs::entity e = target(gui, path);
    if (!e.is_valid()) {
        return;
    }
    const gui::UiDragState* state = e.try_get<gui::UiDragState>();
    gui::UiDraggable* d = e.try_get_mut<gui::UiDraggable>();
    if (d == nullptr || (state != nullptr && state->active != 0) || d->maxV == d->minV) {
        return;
    }
    d->value = (value - d->minV) / (d->maxV - d->minV);
    for (uint32_t b = 0; b < gui->bindCount; ++b) {
        if (gui->binds[b].kind == BindKind::Drag && gui->binds[b].entity == e.id()) {
            gui->binds[b].last = dragReal(*d);
        }
    }
}

void guiHostSetText(GuiHost* gui, const char* path, const char* text) {
    flecs::entity e = target(gui, path);
    gui::UiText* t = e.is_valid() ? e.try_get_mut<gui::UiText>() : nullptr;
    if (t == nullptr || text == nullptr || gui->world.entity(gui->ctx.interact.focus) == e) {
        return;
    }
    (void)strCopyCap(t->text, sizeof(t->text), text);
    t->len = static_cast<uint8_t>(std::strlen(t->text));
    for (uint32_t b = 0; b < gui->bindCount; ++b) {
        if (gui->binds[b].kind == BindKind::Edit && gui->binds[b].entity == e.id()) {
            (void)strCopyCap(gui->binds[b].lastText, sizeof(gui->binds[b].lastText), t->text);
        }
    }
}

void guiHostPump(GuiHost* gui) {
    if (gui == nullptr) {
        return;
    }
    const uint32_t n = gui->eventCount;
    gui->eventCount = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const Event& ev = gui->events[i];
        const Bind& b = gui->binds[ev.bind];
        switch (b.kind) {
        case BindKind::Click:
            b.click(b.user, b.action);
            break;
        case BindKind::Drag:
            b.value(b.user, ev.value);
            break;
        case BindKind::Edit:
            b.text(b.user, ev.text);
            break;
        case BindKind::None:
            break;
        }
    }
}

} // namespace burnhope
