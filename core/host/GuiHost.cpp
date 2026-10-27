#include "host/GuiHost.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include <flecs.h>
#include <spdlog/spdlog.h>

#include "Str.hpp"
#include "gui/Gui.hpp"
#include "gui/data/GuiIcons.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/style/GuiStyleResolver.hpp"
#include "gui/style/GuiStyleSheet.hpp"
#include "gui/loader/GuiLoader.hpp"
#include "gui/text/GuiTextEngine.hpp"
#include "image/ImageDecode.hpp"
#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"

namespace burnhope {

namespace {

constexpr uint32_t kBindCap = 192;
constexpr uint32_t kEventCap = 128;
constexpr uint32_t kTextCap = gui::kGuiTextCap;

constexpr uint32_t kListCap = 4;
constexpr float kListHoverPlate = 0.18f;
constexpr float kListSelectedPlate = 0.5f;
constexpr float kListTonePlate = 0.12f;

enum class BindKind : uint8_t { None = 0, Click, Drag, Drag2, Edit, List };
enum class Gesture : uint8_t { Click = 0, Activate, Context };

struct Bind {
    BindKind kind = BindKind::None;
    uint32_t action = 0;
    uint64_t entity = 0;
    GuiClickFn click = nullptr;
    GuiValueFn value = nullptr;
    GuiTextFn text = nullptr;
    GuiListFn list = nullptr;
    GuiContextFn context = nullptr;
    GuiValue2Fn value2 = nullptr;
    void* user = nullptr;
    float last = 0.0f;
    float lastY = 0.0f;
    char lastText[kTextCap] = {};
    char caption[kTextCap] = {}; // Drag: text of the node at bind time
};

struct List {
    uint64_t entity = 0;
    std::unique_ptr<GuiListEntry[]> rows;
    uint32_t count = 0;
    uint32_t selected = kGuiNoSelection;
    uint8_t thumbSlot = gui::kGuiNoPhoto;
    uint8_t thumbCols = 0;
    uint8_t thumbRows = 0;
    uint8_t tilePx = 0;
    bool fitColumns = false;
};

struct Event {
    uint32_t bind = 0;
    float value = 0.0f;
    float valueY = 0.0f;
    Gesture gesture = Gesture::Click;
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
    List lists[kListCap]{};
    uint32_t listCount = 0;
    Event events[kEventCap]{};
    uint32_t eventCount = 0;
};

namespace {

// Takes advances and glyph boxes from the SDF atlas (FreeType, in atlas pixels) as fractions of the atlas size.
bool applyFontMetrics(const UiFontAtlas& atlas, gui::GuiStyleSheet& sheet) {
    float advances[kUiFontGlyphs]{};
    for (uint32_t i = 0; i < kUiFontGlyphs; ++i) {
        advances[i] = atlas.glyphs[i].advance;
    }
    if (!gui::guiTextMetricsSetAdvances(sheet.text, advances, kUiFontGlyphs, atlas.fontPx)) {
        return false;
    }
    const float inv = 1.0f / atlas.fontPx;
    for (uint32_t i = 0; i < kUiFontGlyphs; ++i) {
        gui::GuiGlyph& g = sheet.text.glyphs[i];
        const UiGlyphMetric& m = atlas.glyphs[i];
        g.bearingX = m.bx * inv;
        g.bearingY = m.by * inv;
        g.w = m.w * inv;
        g.h = m.h * inv;
    }
    return true;
}

void push(GuiHost& g, uint32_t bind, float value, const char* text, Gesture gesture = Gesture::Click, float valueY = 0.0f) {
    if (g.eventCount >= kEventCap) {
        return;
    }
    Event& e = g.events[g.eventCount++];
    e.bind = bind;
    e.value = value;
    e.valueY = valueY;
    e.gesture = gesture;
    e.text[0] = '\0';
    if (text != nullptr) {
        (void)strCopyCap(e.text, sizeof(e.text), text);
    }
}

float dragReal(const gui::UiDraggable& d) {
    return d.minV + d.value * (d.maxV - d.minV);
}

// Writes "caption  value" into a draggable node; the decimals follow the size of its range.
void writeDragLabel(const Bind& bind, flecs::entity e, const gui::UiDraggable& d) {
    gui::UiText* t = e.try_get_mut<gui::UiText>();
    if (t == nullptr) {
        return;
    }
    const float range = d.maxV - d.minV;
    const int decimals = range >= 1000.0f ? 0 : (range >= 100.0f ? 1 : 2);
    char* at = t->text;
    char* const end = t->text + kTextCap - 1;
    const size_t capLen = std::strlen(bind.caption);
    if (capLen > 0 && capLen + 3 < kTextCap) {
        std::memcpy(at, bind.caption, capLen);
        at += capLen;
        *at++ = ' ';
        *at++ = ' ';
    }
    const auto res = std::to_chars(at, end, dragReal(d), std::chars_format::fixed, decimals);
    *res.ptr = '\0';
    t->len = static_cast<uint8_t>(res.ptr - t->text);
}

// Queues the events of this frame: click actions, changed drag values and changed edit texts.
void collect(GuiHost& g) {
    const gui::GuiInteractState& in = g.ctx.interact;
    for (uint32_t b = 0; b < g.bindCount; ++b) {
        Bind& bind = g.binds[b];
        if (bind.kind == BindKind::List) {
            for (uint32_t i = 0; i < in.actionCount && i < gui::kGuiActionCap; ++i) {
                if (in.actions[i].action != bind.action) {
                    continue;
                }
                const flecs::entity item = g.world.entity(in.actions[i].entity);
                const gui::UiVirtualItem* at = item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
                if (at != nullptr && at->index != gui::kGuiVirtualUnbound) {
                    push(g, b, static_cast<float>(at->index), nullptr, g.ctx.in.clicks >= 2 ? Gesture::Activate : Gesture::Click);
                }
            }
            if (bind.context != nullptr) {
                for (uint32_t i = 0; i < in.actionCount && i < gui::kGuiActionCap; ++i) {
                    if (in.actions[i].action != gui::kGuiActionSecondary) {
                        continue;
                    }
                    const flecs::entity item = g.world.entity(in.actions[i].entity);
                    const gui::UiVirtualItem* at = item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
                    if (at != nullptr && at->index != gui::kGuiVirtualUnbound && item.parent().id() == bind.entity) {
                        push(g, b, static_cast<float>(at->index), nullptr, Gesture::Context);
                    }
                }
            }
            continue;
        }
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
                writeDragLabel(bind, e, *d);
                push(g, b, bind.last, nullptr);
            }
        } else if (bind.kind == BindKind::Drag2) {
            const gui::UiDraggable* d = e.try_get<gui::UiDraggable>();
            if (d != nullptr && (d->value != bind.last || d->valueY != bind.lastY)) {
                bind.last = d->value;
                bind.lastY = d->valueY;
                push(g, b, d->value, nullptr, Gesture::Click, d->valueY);
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

// Fills the live pooled items of every bound list from its rows and paints the hover / selection plates.
void refreshLists(GuiHost& g) {
    for (uint32_t li = 0; li < g.listCount; ++li) {
        List& list = g.lists[li];
        const flecs::entity host = g.world.entity(list.entity);
        const gui::GuiVirtualSlots* slots = host.try_get<gui::GuiVirtualSlots>();
        const gui::UiVirtual* v = host.try_get<gui::UiVirtual>();
        if (slots == nullptr || v == nullptr) {
            continue;
        }
        if (list.fitColumns && v->itemW > 0.0f) {
            const gui::UiBox* box = host.try_get<gui::UiBox>();
            const uint32_t cols = box != nullptr ? std::max(1u, static_cast<uint32_t>(box->w / v->itemW)) : 1u;
            if (cols != v->columns) {
                host.try_get_mut<gui::UiVirtual>()->columns = cols;
            }
        }
        for (uint32_t s = 0; s < gui::kGuiVirtualCap; ++s) {
            flecs::entity item = slots->item[s] != 0 ? g.world.entity(slots->item[s]) : flecs::entity();
            const gui::UiVirtualItem* at = item.is_valid() && item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
            if (at == nullptr || at->index >= list.count) {
                continue;
            }
            const gui::UiClass* cls = item.try_get<gui::UiClass>();
            if (v->itemClass != gui::kGuiNoClass && (cls == nullptr || cls->id != v->itemClass)) {
                (void)gui::guiStyleApply(g.style, item, v->itemClass);
            }
            const GuiListEntry& row = list.rows[at->index];
            gui::UiText* text = item.try_get_mut<gui::UiText>();
            if (text != nullptr && std::strncmp(text->text, row.text, kGuiListTextCap) != 0) {
                (void)strCopyCap(text->text, sizeof(text->text), row.text);
                text->len = static_cast<uint8_t>(std::strlen(text->text));
            }
            gui::UiPaint* paint = item.try_get_mut<gui::UiPaint>();
            if (paint != nullptr) {
                const bool selected = at->index == list.selected;
                paint->token = selected ? gui::kTokAccent : gui::kTokItem;
                paint->tint[3] = selected ? kListSelectedPlate
                                          : (g.ctx.interact.hover == item.id() ? kListHoverPlate : kListTonePlate * static_cast<float>(row.tone));
                const bool thumbed = list.thumbSlot != gui::kGuiNoPhoto && row.thumb != 0;
                paint->photo = thumbed ? list.thumbSlot : gui::kGuiNoPhoto;
                paint->cell = thumbed ? static_cast<uint8_t>(row.thumb - 1u) : 0;
                paint->cols = thumbed ? list.thumbCols : 0;
                paint->rows = thumbed ? list.thumbRows : 0;
                paint->tile = list.thumbSlot != gui::kGuiNoPhoto ? list.tilePx : 0;
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
    refreshLists(*g);
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
    if (g == nullptr || !hasFn) {
        return nullptr;
    }
    if (g->bindCount >= kBindCap) {
        spdlog::error("gui: bind table is full ({} entries)", kBindCap);
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

// Rasterizes every registered icon SVG into its cell of one atlas and uploads it to kPhotoIconAtlas.
bool buildIconAtlas(Canvas& canvas) {
    std::unique_ptr<uint8_t[]> atlas(new (std::nothrow) uint8_t[static_cast<size_t>(gui::kGuiIconEdge) * gui::kGuiIconEdge * 4u]());
    if (atlas == nullptr) {
        return false;
    }
    uint32_t drawn = 0;
    for (uint32_t cell = 1; cell < gui::guiIconCount(); ++cell) {
        char resolved[640]{};
        const char* asset = gui::guiIconPath(cell);
        if (asset == nullptr || !platformResolveAsset(asset, resolved, sizeof(resolved))) {
            spdlog::warn("gui icon: asset {} not found", asset != nullptr ? asset : "(null)");
            continue;
        }
        const uint32_t x = (cell % gui::kGuiIconCells) * gui::kGuiIconCell;
        const uint32_t y = (cell / gui::kGuiIconCells) * gui::kGuiIconCell;
        if (imageRasterSvgCell(resolved, atlas.get(), gui::kGuiIconEdge, x, y, gui::kGuiIconCell)) {
            ++drawn;
        } else {
            spdlog::warn("gui icon: failed to rasterize {}", asset);
        }
    }
    spdlog::info("gui icon atlas: {} glyphs", drawn);
    return canvasUploadPhoto(canvas, kPhotoIconAtlas, atlas.get(), gui::kGuiIconEdge, gui::kGuiIconEdge);
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
    if (!buildIconAtlas(canvas)) {
        spdlog::warn("gui: icon atlas upload failed, icons stay blank");
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
    if (const gui::UiText* t = gui->world.entity(b->entity).try_get<gui::UiText>()) {
        (void)strCopyCap(b->caption, sizeof(b->caption), t->text);
    }
    writeDragLabel(*b, gui->world.entity(b->entity), *d);
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
            writeDragLabel(gui->binds[b], e, *d);
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

bool guiHostBindList(GuiHost* gui, const char* path, GuiListFn fn, void* user) {
    if (gui == nullptr || gui->listCount >= kListCap) {
        return false;
    }
    Bind* b = addBind(gui, BindKind::List, path, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    const flecs::entity e = gui->world.entity(b->entity);
    const gui::UiVirtual* v = e.try_get<gui::UiVirtual>();
    auto rows = std::unique_ptr<GuiListEntry[]>(new (std::nothrow) GuiListEntry[kGuiListCap]);
    if (v == nullptr || v->itemAction == 0 || rows == nullptr) {
        spdlog::error("gui: {} is not a clickable virtual list", path);
        --gui->bindCount;
        return false;
    }
    b->list = fn;
    b->action = v->itemAction;
    List& list = gui->lists[gui->listCount++];
    list.entity = e.id();
    list.rows = std::move(rows);
    return true;
}

void guiHostSetList(GuiHost* gui, const char* path, const GuiListEntry* entries, uint32_t count) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || (entries == nullptr && count > 0)) {
        return;
    }
    for (uint32_t i = 0; i < gui->listCount; ++i) {
        List& list = gui->lists[i];
        if (list.entity != e.id()) {
            continue;
        }
        list.count = count < kGuiListCap ? count : kGuiListCap;
        std::copy_n(entries, list.count, list.rows.get());
        list.selected = kGuiNoSelection;
        if (gui::UiVirtual* v = e.try_get_mut<gui::UiVirtual>()) {
            v->count = list.count;
        }
        if (gui::UiScroll* scroll = e.try_get_mut<gui::UiScroll>()) {
            scroll->offY = 0.0f;
        }
        return;
    }
}

void guiHostSetListSelected(GuiHost* gui, const char* path, uint32_t index) {
    const flecs::entity e = target(gui, path);
    for (uint32_t i = 0; e.is_valid() && i < gui->listCount; ++i) {
        if (gui->lists[i].entity == e.id()) {
            gui->lists[i].selected = index;
        }
    }
}

void guiHostSetShown(GuiHost* gui, const char* path, bool shown) {
    const flecs::entity e = target(gui, path);
    if (e.is_valid() && e.has<gui::UiHidden>() == shown) {
        gui::guiSetShown(e, shown);
    }
}

void guiHostSetColor(GuiHost* gui, const char* path, float r, float g, float b) {
    const flecs::entity e = target(gui, path);
    gui::UiPaint* paint = e.is_valid() ? e.try_get_mut<gui::UiPaint>() : nullptr;
    if (paint == nullptr) {
        return;
    }
    paint->token = gui::kTokCustom;
    paint->tint[0] = r;
    paint->tint[1] = g;
    paint->tint[2] = b;
    paint->tint[3] = 1.0f;
}

void guiHostSetIcon(GuiHost* gui, const char* path, const char* name) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid()) {
        return;
    }
    gui::UiPaint* paint = e.try_get_mut<gui::UiPaint>();
    if (paint != nullptr) {
        paint->icon = name != nullptr ? gui::guiIconFind(name) : 0;
    }
}

void guiHostSetActive(GuiHost* gui, const char* path, bool active) {
    const flecs::entity e = target(gui, path);
    const gui::UiClass* cls = e.is_valid() ? e.try_get<gui::UiClass>() : nullptr;
    const gui::GuiStyle* style = cls != nullptr ? gui::guiStyleGet(gui->style, cls->id) : nullptr;
    gui::UiPaint* paint = style != nullptr ? e.try_get_mut<gui::UiPaint>() : nullptr;
    if (paint == nullptr) {
        return;
    }
    if (active && gui::guiStyleHas(*style, gui::kSbActiveToken)) {
        paint->token = style->activeToken;
    } else if (gui::guiStyleHas(*style, gui::kSbToken)) {
        paint->token = style->token;
    }
}

bool guiHostBindDrag2(GuiHost* gui, const char* path, GuiValue2Fn fn, void* user) {
    Bind* b = addBind(gui, BindKind::Drag2, path, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    b->value2 = fn;
    const gui::UiDraggable* d = gui->world.entity(b->entity).try_get<gui::UiDraggable>();
    if (d == nullptr || d->axis != 2) {
        spdlog::error("gui: {} is not an xy draggable", path);
        --gui->bindCount;
        return false;
    }
    b->last = d->value;
    b->lastY = d->valueY;
    return true;
}

namespace {

List* listOf(GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    for (uint32_t i = 0; e.is_valid() && i < gui->listCount; ++i) {
        if (gui->lists[i].entity == e.id()) {
            return &gui->lists[i];
        }
    }
    return nullptr;
}

} // namespace

bool guiHostBindListContext(GuiHost* gui, const char* path, GuiContextFn fn, void* user) {
    const flecs::entity e = target(gui, path);
    for (uint32_t b = 0; fn != nullptr && e.is_valid() && b < gui->bindCount; ++b) {
        Bind& bind = gui->binds[b];
        if (bind.kind == BindKind::List && bind.entity == e.id()) {
            bind.context = fn;
            if (bind.user == nullptr) {
                bind.user = user;
            }
            return true;
        }
    }
    return false;
}

void guiHostSetListThumbs(GuiHost* gui, const char* path, uint8_t slot, uint8_t cols, uint8_t rows, uint8_t tilePx) {
    if (List* list = listOf(gui, path)) {
        list->thumbSlot = slot;
        list->thumbCols = cols;
        list->thumbRows = rows;
        list->tilePx = tilePx;
    }
}

void guiHostSetListThumb(GuiHost* gui, const char* path, uint32_t index, uint8_t thumb) {
    if (List* list = listOf(gui, path); list != nullptr && index < list->count) {
        list->rows[index].thumb = thumb;
    }
}

void guiHostSetListMetrics(GuiHost* gui, const char* path, float itemW, float itemH, bool fitColumns) {
    List* list = listOf(gui, path);
    gui::UiVirtual* v = list != nullptr ? gui->world.entity(list->entity).try_get_mut<gui::UiVirtual>() : nullptr;
    if (v == nullptr || itemH <= 0.0f) {
        return;
    }
    v->itemW = itemW;
    v->itemH = itemH;
    list->fitColumns = fitColumns;
}

void guiHostListWindow(GuiHost* gui, const char* path, uint32_t& first, uint32_t& live) {
    first = 0;
    live = 0;
    const flecs::entity e = target(gui, path);
    const gui::UiVirtual* v = e.is_valid() ? e.try_get<gui::UiVirtual>() : nullptr;
    if (v != nullptr) {
        first = v->first;
        live = v->live;
    }
}

void guiHostOpenAtPointer(GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    gui::UiOverlay* o = e.is_valid() ? e.try_get_mut<gui::UiOverlay>() : nullptr;
    if (o == nullptr) {
        return;
    }
    o->anchorX = gui->ctx.interact.pointerX;
    o->anchorY = gui->ctx.interact.pointerY;
    o->anchorW = 1.0f;
    o->anchorH = 1.0f;
    o->open = 1;
}

bool guiHostMount(GuiHost* gui, const char* screenPath, const char* parentPath) {
    if (gui == nullptr || screenPath == nullptr) {
        return false;
    }
    const flecs::entity parent = parentPath != nullptr && parentPath[0] != '\0' ? gui->root.lookup(parentPath) : gui->root;
    if (!parent.is_valid()) {
        spdlog::error("gui: mount parent {} not found", parentPath);
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(screenPath, &file) || file.data == nullptr) {
        return false;
    }
    flecs::entity mounted;
    const bool ok = gui::guiLoadScreen(gui->world, gui->style, std::string_view{reinterpret_cast<const char*>(file.data), file.size}, parent, &mounted);
    fileUnmap(&file);
    return ok;
}

void guiHostSetPhoto(GuiHost* gui, const char* path, uint8_t slot, uint8_t cell, uint8_t cols, uint8_t rows) {
    const flecs::entity e = target(gui, path);
    gui::UiPaint* paint = e.is_valid() ? e.try_get_mut<gui::UiPaint>() : nullptr;
    if (paint == nullptr) {
        return;
    }
    paint->photo = slot;
    paint->cell = cell;
    paint->cols = cols;
    paint->rows = rows;
    if (slot != gui::kGuiNoPhoto) {
        paint->token = gui::kTokCustom;
        paint->tint[0] = paint->tint[1] = paint->tint[2] = paint->tint[3] = 1.0f;
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
        case BindKind::Drag2:
            b.value2(b.user, ev.value, ev.valueY);
            break;
        case BindKind::List:
            if (ev.gesture == Gesture::Context) {
                b.context(b.user, static_cast<uint32_t>(ev.value));
            } else {
                b.list(b.user, static_cast<uint32_t>(ev.value), ev.gesture == Gesture::Activate);
            }
            break;
        case BindKind::None:
            break;
        }
    }
}

} // namespace burnhope
