#include "gui/GuiHost.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <new>
#include <string_view>
#include <vector>

#include <flecs.h>
#include <yoga/Yoga.h>
#include <spdlog/spdlog.h>

#include "Str.hpp"
#include "Gui.hpp"
#include "interaction/InteractDragDrop.hpp"
#include "layout/GuiDock.hpp"
#include "gui/GuiIcons.hpp"
#include "input/GuiHitTest.hpp"
#include "layout/GuiLayoutEngine.hpp"
#include "style/GuiStyleResolver.hpp"
#include "style/GuiStyleSheet.hpp"
#include "loader/GuiLoader.hpp"
#include "overlay/GuiColorPicker.hpp"
#include "text/GuiTextEngine.hpp"
#include "image/ImageDecode.hpp"
#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"

namespace burnhope {

namespace {

constexpr uint32_t kBindCap = 320;
constexpr uint32_t kEventCap = 128;
constexpr uint32_t kTextCap = gui::kGuiTextCap;

constexpr uint32_t kListCap = 4;
constexpr float kListHoverPlate = 0.18f;
constexpr float kListSelectedPlate = 0.9f;
constexpr float kListCardPlate = 1.0f;
constexpr float kListTonePlate = 0.12f;

enum class BindKind : uint8_t { None = 0, Click, Drag, Drag2, Edit, List, Crumbs };

// Crumb buttons of every strip click with action kCrumbAction | strip bind << 16 | crumb index.
constexpr uint32_t kCrumbAction = 0x40000000u;
constexpr uint32_t kCrumbIndexMask = 0xFFFFu;
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
    GuiCrumbFn crumb = nullptr;
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
    bool zoom = false; // Ctrl + wheel over it is reported through guiHostTakeZoom instead of scrolling
    GuiTipFn tip = nullptr;
    void* tipUser = nullptr;
};

// One node addressed by its JSON "bind" key (UiBindKey): hash of the key and the entity.
struct KeyEntry {
    uint32_t hash = 0;
    uint64_t entity = 0;
};
constexpr uint32_t kKeyCap = 1024;

struct Event {
    uint32_t bind = 0;
    float value = 0.0f;
    float valueY = 0.0f;
    Gesture gesture = Gesture::Click;
    char text[kTextCap] = {};
};

} // namespace

struct CrumbStrip {
    uint64_t pill = 0;
    uint32_t bind = 0;
    std::vector<uint64_t> nodes; // crumb buttons and separators spawned below the pill
};

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
    uint8_t pressClicks = 0; // click count of the last left press (the click action fires on release, when `clicks` is already 0)
    uint8_t mods = 0;        // bit 0 Ctrl, bit 1 Shift at the last left press
    float pressX = 0.0f;     // pointer x at the last left press
    float zoomWheel = 0.0f;  // Ctrl + wheel turns not yet taken
    gui::GuiDragDropState drag{};
    gui::GuiDragDropOps dragOps{};
    mutable KeyEntry keys[kKeyCap]{}; // index of the bind keys, rebuilt when a key is not found (panels mount at run time)
    mutable uint32_t keyCount = 0;
    std::vector<CrumbStrip> strips;
    std::unique_ptr<gui::GuiColorPicker> picker = std::make_unique<gui::GuiColorPicker>();
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
                    push(g, b, static_cast<float>(at->index), nullptr, g.pressClicks >= 2 ? Gesture::Activate : Gesture::Click);
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
        if (bind.kind == BindKind::Crumbs) {
            for (uint32_t i = 0; i < in.actionCount && i < gui::kGuiActionCap; ++i) {
                if ((in.actions[i].action & ~kCrumbIndexMask) == bind.action) {
                    push(g, b, static_cast<float>(in.actions[i].action & kCrumbIndexMask), nullptr);
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
            const gui::GuiRow* shown = item.try_get<gui::GuiRow>();
            if (shown == nullptr ? gui::guiRowActive(row.row) : std::memcmp(shown, &row.row, sizeof(gui::GuiRow)) != 0) {
                item.set<gui::GuiRow>(row.row);
            }
            gui::UiPaint* paint = item.try_get_mut<gui::UiPaint>();
            if (paint != nullptr) {
                const bool selected = at->index == list.selected || row.mark != 0;
                const bool card = list.thumbSlot != gui::kGuiNoPhoto; // file tiles are always-visible cards
                const bool hovered = g.ctx.interact.hover == item.id();
                paint->token = selected ? gui::kTokAccent : ((card && hovered) ? gui::kTokChrome : gui::kTokItem);
                paint->tint[3] = selected ? kListSelectedPlate
                                          : (card ? kListCardPlate : (hovered ? kListHoverPlate : kListTonePlate * static_cast<float>(row.tone)));
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

// True when the node under the pointer (last frame) is a zoom list or one of its descendants.
bool zoomUnder(GuiHost& g) {
    flecs::entity at = gui::guiEntityOf(g.world, g.ctx.interact.under);
    while (at.id() != 0 && at.is_alive()) {
        for (uint32_t i = 0; i < g.listCount; ++i) {
            if (g.lists[i].zoom && g.lists[i].entity == at.id()) {
                return true;
            }
        }
        at = at.parent();
    }
    return false;
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
    if ((input.pointer.pressed & kPointerLeft) != 0) {
        g->pressClicks = input.clicks;
        g->pressX = input.pointer.x;
        g->mods = static_cast<uint8_t>((input.ctrl != 0 ? 1u : 0u) | (input.shift != 0 ? 2u : 0u));
    }
    InputFrame frame = input;
    if (input.ctrl != 0 && input.wheel != 0.0f && zoomUnder(*g)) {
        g->zoomWheel += input.wheel;
        frame.wheel = 0.0f;
    }
    gui::guiConsumeInput(g->ctx, frame);
    gui::guiLayout(g->ctx, w, h);
    refreshLists(*g);
    gui::guiRender(g->ctx, VK_NULL_HANDLE);
    collect(*g);

    const gui::GuiInteractState& in = g->ctx.interact;
    fb.pointerOver = in.under != 0 || in.capture != 0;
    fb.cursor = in.cursor;
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

flecs::entity target(const GuiHost* g, const char* path);

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
        const flecs::entity e = target(g, path);
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

uint32_t keyHash(const char* key) {
    uint32_t h = 2166136261u;
    for (; *key != '\0'; ++key) {
        h = (h ^ static_cast<uint8_t>(*key)) * 16777619u;
    }
    return h;
}

void indexKeys(const GuiHost* g) {
    g->keyCount = 0;
    g->world.each([&](flecs::entity e, const gui::UiBindKey& k) {
        if (g->keyCount < kKeyCap) {
            g->keys[g->keyCount++] = {keyHash(k.key), e.id()};
        }
    });
}

flecs::entity keyed(const GuiHost* g, const char* key) {
    const uint32_t hash = keyHash(key);
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < g->keyCount; ++i) {
            const flecs::entity e = g->keys[i].hash == hash ? g->world.entity(g->keys[i].entity) : flecs::entity();
            const gui::UiBindKey* k = e.is_valid() && e.is_alive() ? e.try_get<gui::UiBindKey>() : nullptr;
            if (k != nullptr && std::strcmp(k->key, key) == 0) {
                return e;
            }
        }
        if (pass == 0) {
            indexKeys(g);
        }
    }
    return {};
}

// A path with a '.' and no "::" is a semantic bind key ("actor.pos.x"); anything else is an entity path under the screen root.
flecs::entity target(const GuiHost* g, const char* path) {
    if (g == nullptr || path == nullptr) {
        return {};
    }
    return std::strchr(path, '.') != nullptr && std::strstr(path, "::") == nullptr ? keyed(g, path) : g->root.lookup(path);
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
    d->value = std::clamp((value - d->minV) / (d->maxV - d->minV), 0.0f, 1.0f);
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

namespace {

bool tipText(void* user, uint64_t host, uint32_t index, char* out, uint32_t cap) {
    const auto* g = static_cast<const GuiHost*>(user);
    for (uint32_t i = 0; i < g->listCount; ++i) {
        const List& list = g->lists[i];
        if (list.entity == host && list.tip != nullptr && index < list.count) {
            return list.tip(list.tipUser, index, out, cap);
        }
    }
    return false;
}

} // namespace

bool guiHostBindTooltip(GuiHost* gui, const char* path, GuiTipFn fn, void* user) {
    const flecs::entity e = target(gui, path);
    for (uint32_t i = 0; e.is_valid() && i < gui->listCount; ++i) {
        if (gui->lists[i].entity == e.id()) {
            gui->lists[i].tip = fn;
            gui->lists[i].tipUser = user;
            gui->ctx.tooltip.text = tipText;
            gui->ctx.tooltip.user = gui;
            return true;
        }
    }
    return false;
}

void guiHostSetListSelected(GuiHost* gui, const char* path, uint32_t index) {
    const flecs::entity e = target(gui, path);
    for (uint32_t i = 0; e.is_valid() && i < gui->listCount; ++i) {
        if (gui->lists[i].entity == e.id()) {
            gui->lists[i].selected = index;
        }
    }
}

namespace {
List* listOf(GuiHost* gui, const char* path);
} // namespace

void guiHostUpdateList(GuiHost* gui, const char* path, const GuiListEntry* entries, uint32_t count) {
    List* list = listOf(gui, path);
    if (list == nullptr || entries == nullptr || count != list->count) {
        return;
    }
    std::copy_n(entries, count, list->rows.get());
}

void guiHostSetListMark(GuiHost* gui, const char* path, uint32_t index, bool marked) {
    if (List* list = listOf(gui, path); list != nullptr && index < list->count) {
        list->rows[index].mark = marked ? 1 : 0;
    }
}

bool guiHostDockOpen(GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || !e.has<gui::UiDockPanel>()) {
        return false;
    }
    gui::guiDockOpen(gui->world, e);
    return true;
}

bool guiHostDockClose(GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || !e.has<gui::UiDockPanel>()) {
        return false;
    }
    gui::guiDockClose(e);
    return true;
}

bool guiHostDockIsOpen(const GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    const gui::UiDockPanel* p = e.is_valid() ? e.try_get<gui::UiDockPanel>() : nullptr;
    return p != nullptr && p->closed == 0;
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

float guiHostToken(const GuiHost* gui, const char* name) {
    float value = 0.0f;
    if (gui == nullptr || name == nullptr || !gui::guiStyleNumber(gui->style, name, value)) {
        spdlog::warn("gui: style token {} is not defined", name != nullptr ? name : "(null)");
    }
    return value;
}

bool guiHostColor8(const GuiHost* gui, const char* role, uint8_t (&rgb)[3]) {
    return gui != nullptr && role != nullptr && gui::guiStyleColor8(gui->style, role, rgb);
}

uint8_t guiHostIconCell(const char* name) {
    return name != nullptr ? gui::guiIconFind(name) : 0;
}

void guiHostSetListItemClass(GuiHost* gui, const char* path, const char* styleClass) {
    const flecs::entity e = target(gui, path);
    gui::UiVirtual* v = e.is_valid() ? e.try_get_mut<gui::UiVirtual>() : nullptr;
    if (v != nullptr && styleClass != nullptr) {
        v->itemClass = gui::guiStyleFind(gui->style, styleClass);
    }
}

void guiHostSetListZoom(GuiHost* gui, const char* path, bool on) {
    if (List* list = listOf(gui, path)) {
        list->zoom = on;
    }
}

float guiHostTakeZoom(GuiHost* gui) {
    if (gui == nullptr) {
        return 0.0f;
    }
    const float w = gui->zoomWheel;
    gui->zoomWheel = 0.0f;
    return w;
}

bool guiHostPressOnChevron(const GuiHost* gui, const char* path, uint32_t index) {
    const flecs::entity host = target(gui, path);
    const gui::GuiVirtualSlots* slots = host.is_valid() ? host.try_get<gui::GuiVirtualSlots>() : nullptr;
    for (uint32_t s = 0; slots != nullptr && s < gui::kGuiVirtualCap; ++s) {
        const flecs::entity item = slots->item[s] != 0 ? gui->world.entity(slots->item[s]) : flecs::entity();
        const gui::UiVirtualItem* at = item.is_valid() && item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
        const gui::UiBox* box = at != nullptr ? item.try_get<gui::UiBox>() : nullptr;
        const gui::UiFlex* flex = at != nullptr ? item.try_get<gui::UiFlex>() : nullptr;
        if (box == nullptr || flex == nullptr || flex->node == nullptr || at->index != index) {
            continue;
        }
        for (const List& list : std::span<const List>(gui->lists, gui->listCount)) {
            if (list.entity != host.id() || index >= list.count || list.rows[index].row.chevron == 0) {
                continue;
            }
            const float left = box->x + YGNodeStyleGetPadding(flex->node, YGEdgeLeft).value +
                               static_cast<float>(list.rows[index].row.depth) * gui::kGuiRowIndentPx;
            return gui->pressX >= left && gui->pressX < left + gui::kGuiRowCellPx;
        }
    }
    return false;
}

float guiHostPressX(const GuiHost* gui) {
    return gui != nullptr ? gui->pressX : 0.0f;
}

uint8_t guiHostPressMods(const GuiHost* gui) {
    return gui != nullptr ? gui->mods : 0;
}

void guiHostPointer(const GuiHost* gui, float& x, float& y, uint32_t& down) {
    x = gui->ctx.interact.pointerX;
    y = gui->ctx.interact.pointerY;
    down = gui->ctx.in.pointer.down;
}

bool guiHostPointerOn(const GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    return e.is_valid() && e.id() == gui->ctx.interact.under;
}

bool guiHostPointerWithin(const GuiHost* gui, const char* path) {
    const flecs::entity node = target(gui, path);
    if (!node.is_valid()) {
        return false;
    }
    for (flecs::entity e = gui->world.entity(gui->ctx.interact.under); e.id() != 0 && e.is_alive(); e = e.parent()) {
        if (e == node) {
            return true;
        }
    }
    return false;
}

bool guiHostBox(const GuiHost* gui, const char* path, float& x, float& y, float& w, float& h) {
    const flecs::entity e = target(gui, path);
    const gui::UiBox* box = e.is_valid() ? e.try_get<gui::UiBox>() : nullptr;
    if (box == nullptr) {
        return false;
    }
    x = box->x;
    y = box->y;
    w = box->w;
    h = box->h;
    return true;
}

void guiHostShowMarquee(GuiHost* gui, const char* path, float x0, float y0, float x1, float y1) {
    const flecs::entity e = target(gui, path);
    gui::guiLayoutSetRect(e, std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0));
    gui::guiSetShown(e, true);
}

uint32_t guiHostListHits(const GuiHost* gui, const char* path, float x0, float y0, float x1, float y1, uint32_t* out, uint32_t cap) {
    const flecs::entity host = target(gui, path);
    const gui::GuiVirtualSlots* slots = host.is_valid() ? host.try_get<gui::GuiVirtualSlots>() : nullptr;
    uint32_t n = 0;
    for (uint32_t s = 0; slots != nullptr && s < gui::kGuiVirtualCap && n < cap; ++s) {
        const flecs::entity item = slots->item[s] != 0 ? gui->world.entity(slots->item[s]) : flecs::entity();
        const gui::UiVirtualItem* at = item.is_valid() && item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
        const gui::UiBox* box = at != nullptr ? item.try_get<gui::UiBox>() : nullptr;
        if (box == nullptr || at->index == gui::kGuiVirtualUnbound || item.has<gui::UiHidden>()) {
            continue;
        }
        if (box->x < x1 && box->x + box->w > x0 && box->y < y1 && box->y + box->h > y0) {
            out[n++] = at->index;
        }
    }
    return n;
}

bool guiHostListItemBox(const GuiHost* gui, const char* path, uint32_t index, float& x, float& y, float& w, float& h) {
    float bx = 0.0f;
    float by = 0.0f;
    float bw = 0.0f;
    float bh = 0.0f;
    if (!guiHostBox(gui, path, bx, by, bw, bh)) {
        return false;
    }
    const flecs::entity host = target(gui, path);
    const gui::GuiVirtualSlots* slots = host.is_valid() ? host.try_get<gui::GuiVirtualSlots>() : nullptr;
    for (uint32_t s = 0; slots != nullptr && s < gui::kGuiVirtualCap; ++s) {
        const flecs::entity item = slots->item[s] != 0 ? gui->world.entity(slots->item[s]) : flecs::entity();
        const gui::UiVirtualItem* at = item.is_valid() && item.is_alive() ? item.try_get<gui::UiVirtualItem>() : nullptr;
        const gui::UiBox* box = at != nullptr ? item.try_get<gui::UiBox>() : nullptr;
        if (box != nullptr && at->index == index && !item.has<gui::UiHidden>()) {
            x = box->x;
            y = box->y;
            w = box->w;
            h = box->h;
            return true;
        }
    }
    return false;
}

void guiHostSetPlaceholder(GuiHost* gui, const char* path, const char* text) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || text == nullptr) {
        return;
    }
    gui::UiPlaceholder hint{};
    (void)strCopyCap(hint.text, sizeof(hint.text), text);
    e.set<gui::UiPlaceholder>(hint);
}

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
    if (itemW <= 0.0f) { // full-width rows
        v->columns = 1;
    }
    list->fitColumns = fitColumns && itemW > 0.0f;
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

bool guiHostBindDragDrop(GuiHost* gui, const char* ghost, const char* box, const gui::GuiDragDropOps& ops) {
    const flecs::entity g = target(gui, ghost);
    const flecs::entity b = target(gui, box);
    if (!g.is_valid() || !b.is_valid()) {
        spdlog::error("gui: drag & drop nodes {} / {} are missing", ghost != nullptr ? ghost : "?", box != nullptr ? box : "?");
        return false;
    }
    gui->drag = {};
    gui->drag.ghost = g.id();
    gui->drag.box = b.id();
    gui->dragOps = ops;
    return true;
}

void guiHostDragDropStep(GuiHost* gui) {
    if (gui == nullptr || gui->drag.ghost == 0) {
        return;
    }
    const gui::GuiInteractState& in = gui->ctx.interact;
    const bool left = (gui->ctx.in.pointer.down & kPointerLeft) != 0;
    gui::guiDragDropRun(gui->world, gui->drag, gui->dragOps, in.pointerX, in.pointerY, left, gui->ctx.in.escape != 0, gui->ctx.screenW,
                        gui->ctx.screenH);
}

bool guiHostDragDropBusy(const GuiHost* gui) {
    return gui != nullptr && gui::guiDragDropBusy(gui->drag);
}

bool guiHostCovers(GuiHost* gui, float x, float y) {
    if (gui == nullptr) {
        return false;
    }
    flecs::entity under;
    (void)gui::guiHitTestEx(gui->world, x, y, &under);
    return under.id() != 0;
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

void guiHostOpenAt(GuiHost* gui, const char* popup, const char* anchor) {
    const flecs::entity e = target(gui, popup);
    gui::UiOverlay* o = e.is_valid() ? e.try_get_mut<gui::UiOverlay>() : nullptr;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    if (o == nullptr || !guiHostBox(gui, anchor, x, y, w, h)) {
        return;
    }
    o->anchorX = x;
    o->anchorY = y;
    o->anchorW = w;
    o->anchorH = h;
    o->open = 1;
}

bool guiHostIsOpen(const GuiHost* gui, const char* popup) {
    const flecs::entity e = target(gui, popup);
    const gui::UiOverlay* o = e.is_valid() ? e.try_get<gui::UiOverlay>() : nullptr;
    return o != nullptr && o->open != 0;
}

void guiHostClose(GuiHost* gui, const char* popup) {
    const flecs::entity e = target(gui, popup);
    if (gui::UiOverlay* o = e.is_valid() ? e.try_get_mut<gui::UiOverlay>() : nullptr) {
        o->open = 0;
    }
}

bool guiHostOpenColorPicker(GuiHost* gui, const char* anchor, const float (&rgb)[3], GuiColorFn fn, void* user) {
    return gui != nullptr && gui::guiColorPickerOpen(gui, *gui->picker, anchor, rgb, fn, user);
}

bool guiHostSpawnSnippet(GuiHost* gui, const char* parentPath, const char* snippetPath) {
    if (gui == nullptr || snippetPath == nullptr) {
        return false;
    }
    const flecs::entity parent = parentPath != nullptr && parentPath[0] != '\0' ? target(gui, parentPath) : gui->root;
    if (!parent.is_valid()) {
        spdlog::error("gui: snippet parent {} not found", parentPath);
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(snippetPath, &file) || file.data == nullptr) {
        return false;
    }
    flecs::entity spawned;
    const bool ok = gui::guiLoadScreen(gui->world, gui->style, std::string_view{reinterpret_cast<const char*>(file.data), file.size}, parent, &spawned);
    fileUnmap(&file);
    gui->keyCount = 0;
    return ok;
}

bool guiHostDestroyNode(GuiHost* gui, const char* path) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || !e.is_alive() || e == gui->root) {
        return false;
    }
    gui::guiLayoutDetach(e);
    e.destruct();
    gui->keyCount = 0;
    return true;
}

bool guiHostSetClass(GuiHost* gui, const char* path, const char* styleClass) {
    const flecs::entity e = target(gui, path);
    if (!e.is_valid() || styleClass == nullptr) {
        return false;
    }
    const uint32_t id = gui::guiStyleFind(gui->style, styleClass);
    return id != gui::kGuiNoClass && gui::guiStyleApply(gui->style, e, id);
}

bool guiHostSetTheme(GuiHost* gui, const char* name) {
    char resolved[640]{};
    MappedFile file{};
    if (gui == nullptr || name == nullptr || !platformResolveAsset("assets/ui/theme.json", resolved, sizeof(resolved)) ||
        !fileMapReadOnly(resolved, &file) || file.data == nullptr) {
        return false;
    }
    const bool ok = gui::guiStyleLoadTheme(gui->style, std::string_view{reinterpret_cast<const char*>(file.data), file.size}, name);
    fileUnmap(&file);
    return ok;
}

bool guiHostBindCrumbs(GuiHost* gui, const char* path, GuiCrumbFn fn, void* user) {
    const uint32_t index = gui != nullptr ? gui->bindCount : 0;
    Bind* b = addBind(gui, BindKind::Crumbs, path, fn != nullptr, user);
    if (b == nullptr) {
        return false;
    }
    b->crumb = fn;
    b->action = kCrumbAction | (index << 16);
    gui->strips.push_back(CrumbStrip{.pill = b->entity, .bind = index});
    return true;
}

namespace {

// Appends `text` as a JSON string body: quotes and backslashes escaped, control bytes dropped, cut at kTextCap - 1 bytes on a
// UTF-8 boundary.
void appendJsonText(char* out, size_t cap, size_t& at, std::string_view text) {
    size_t take = std::min<size_t>(text.size(), kTextCap - 1);
    while (take > 0 && take < text.size() && (static_cast<uint8_t>(text[take]) & 0xC0u) == 0x80u) {
        --take;
    }
    for (size_t i = 0; i < take && at + 3 < cap; ++i) {
        const char c = text[i];
        if (static_cast<uint8_t>(c) < 0x20u) {
            continue;
        }
        if (c == '"' || c == '\\') {
            out[at++] = '\\';
        }
        out[at++] = c;
    }
    out[at] = '\0';
}

bool spawnCrumbNode(GuiHost& g, CrumbStrip& strip, const char* cls, std::string_view text, uint32_t action) {
    char json[2 * kTextCap + 192];
    size_t at = 0;
    const int head = action != 0
        ? std::snprintf(json, sizeof(json), R"({"class":"%s","behaviors":["hover","click"],"action":%u,"text":")", cls, action)
        : std::snprintf(json, sizeof(json), R"({"class":"%s","text":")", cls);
    if (head <= 0) {
        return false;
    }
    at = static_cast<size_t>(head);
    appendJsonText(json, sizeof(json) - 4, at, text);
    json[at++] = '"';
    json[at++] = '}';
    json[at] = '\0';
    flecs::entity node;
    if (!gui::guiLoadScreen(g.world, g.style, std::string_view{json, at}, g.world.entity(strip.pill), &node) || !node.is_valid()) {
        return false;
    }
    strip.nodes.push_back(node.id());
    return true;
}

} // namespace

bool guiHostSetCrumbs(GuiHost* gui, const char* path, const char* const* names, uint32_t count) {
    const flecs::entity pill = target(gui, path);
    if (!pill.is_valid()) {
        return false;
    }
    CrumbStrip* strip = nullptr;
    for (CrumbStrip& s : gui->strips) {
        if (s.pill == pill.id()) {
            strip = &s;
        }
    }
    if (strip == nullptr || count > kCrumbIndexMask) {
        spdlog::error("gui: {} has no crumb binding", path);
        return false;
    }
    for (const uint64_t id : strip->nodes) {
        const flecs::entity old = gui->world.entity(id);
        if (old.is_alive()) {
            gui::guiLayoutDetach(old);
            old.destruct();
        }
    }
    strip->nodes.clear();
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
        ok = (i == 0 || spawnCrumbNode(*gui, *strip, "crumb_sep", ">", 0)) && ok;
        ok = spawnCrumbNode(*gui, *strip, "crumb", names[i], gui->binds[strip->bind].action | i) && ok;
    }
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
        case BindKind::Crumbs:
            b.crumb(b.user, static_cast<uint32_t>(ev.value));
            break;
        case BindKind::None:
            break;
        }
    }
}

} // namespace burnhope
