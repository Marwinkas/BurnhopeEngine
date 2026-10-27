#include "layout/GuiLayoutEngine.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>

#include "text/GuiTextEngine.hpp"
#include "platform/Check.hpp"

namespace burnhope::gui {
namespace {

constexpr uint32_t kMeasureLineCap = 256;

// Per-world state shared by every node context. `metrics` is only valid while guiLayoutRun executes.
struct Shared {
    ecs_world_t* world = nullptr;
    const GuiTextMetrics* metrics = nullptr;
    uint32_t epoch = 0;
};

// Heap record behind YGNodeGetContext. textStamp != 0 means a measure function is installed.
struct NodeCtx {
    uint64_t entity = 0;
    Shared* shared = nullptr;
    uint64_t textStamp = 0;
};

// Move-only owner of Shared, stored as a world singleton so it dies with the world.
struct GuiLayoutState {
    Shared* shared = nullptr;

    GuiLayoutState() = default;
    explicit GuiLayoutState(Shared* s) : shared(s) {}
    GuiLayoutState(const GuiLayoutState&) = delete;
    GuiLayoutState& operator=(const GuiLayoutState&) = delete;
    GuiLayoutState(GuiLayoutState&& o) noexcept : shared(o.shared) { o.shared = nullptr; }
    GuiLayoutState& operator=(GuiLayoutState&& o) noexcept {
        if (this != &o) {
            delete shared;
            shared = o.shared;
            o.shared = nullptr;
        }
        return *this;
    }
    ~GuiLayoutState() { delete shared; }
};

static_assert(sizeof(void*) == sizeof(uint64_t));

NodeCtx* ctxOf(YGNodeConstRef node) {
    return static_cast<NodeCtx*>(YGNodeGetContext(node));
}

flecs::entity entityOf(ecs_world_t* world, const NodeCtx* ctx) {
    return ctx != nullptr ? flecs::entity(world, ctx->entity) : flecs::entity();
}

void freeNode(YGNodeRef node) {
    delete ctxOf(node);
    YGNodeSetContext(node, nullptr);
    YGNodeFree(node);
}

Shared* sharedOf(flecs::world& w) {
    if (const GuiLayoutState* st = w.try_get<GuiLayoutState>(); st != nullptr && st->shared != nullptr) {
        return st->shared;
    }
    Shared* s = new (std::nothrow) Shared();
    if (s == nullptr) {
        return nullptr;
    }
    s->world = w.c_ptr();
    w.component<UiFlex>().on_remove([](flecs::entity, UiFlex& f) {
        if (f.node != nullptr) {
            freeNode(f.node);
            f.node = nullptr;
        }
    });
    w.set<GuiLayoutState>(GuiLayoutState{s});
    return s;
}

// ---- text measuring ----

uint64_t textStamp(const UiText& t, uint32_t epoch) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < t.len; ++i) {
        h = (h ^ static_cast<uint8_t>(t.text[i])) * 1099511628211ull;
    }
    h = (h ^ t.size) * 1099511628211ull;
    h = (h ^ epoch) * 1099511628211ull;
    return h | 1ull;
}

YGSize measureText(YGNodeConstRef node, float width, YGMeasureMode widthMode, float, YGMeasureMode) {
    const NodeCtx* ctx = ctxOf(node);
    if (ctx == nullptr || ctx->shared == nullptr || ctx->shared->metrics == nullptr) {
        return {0.0f, 0.0f};
    }
    const flecs::entity e = entityOf(ctx->shared->world, ctx);
    const UiText* t = e.try_get<UiText>();
    if (t == nullptr) {
        return {0.0f, 0.0f};
    }
    const GuiTextMetrics& m = *ctx->shared->metrics;
    const std::string_view sv(t->text, t->len);
    const float px = static_cast<float>(t->size);
    const float maxW = widthMode == YGMeasureModeUndefined ? 0.0f : std::max(width, 1.0f);

    GuiTextLine lines[kMeasureLineCap];
    uint32_t n = guiTextWrap(m, sv, px, maxW, lines, kMeasureLineCap);
    float widest = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        widest = std::max(widest, lines[i].width);
    }
    if (n == kMeasureLineCap) {
        n = guiTextWrap(m, sv, px, maxW, nullptr, 0);
    }
    const float h = static_cast<float>(std::max(n, 1u)) * guiTextLineHeight(m, px);
    float w = std::ceil(widest);
    if (widthMode != YGMeasureModeUndefined) {
        w = std::min(w, width);
    }
    return {w, std::ceil(h)};
}

// Installs / removes the measure function of one node and flags it dirty when its text changed.
void prepareMeasure(YGNodeRef node, NodeCtx& ctx) {
    const flecs::entity e = entityOf(ctx.shared->world, &ctx);
    const UiText* t = e.is_alive() ? e.try_get<UiText>() : nullptr;
    if (t != nullptr && YGNodeGetChildCount(node) == 0) {
        const uint64_t stamp = textStamp(*t, ctx.shared->epoch);
        YGNodeSetMeasureFunc(node, measureText);
        if (stamp != ctx.textStamp) {
            YGNodeMarkDirty(node);
            ctx.textStamp = stamp;
        }
    } else if (ctx.textStamp != 0) {
        if (YGNodeHasMeasureFunc(node)) {
            YGNodeMarkDirty(node);
        }
        YGNodeSetMeasureFunc(node, nullptr);
        ctx.textStamp = 0;
    }
}

void prepareTree(YGNodeRef node) {
    NodeCtx* ctx = ctxOf(node);
    if (ctx == nullptr) {
        return;
    }
    prepareMeasure(node, *ctx);
    const size_t count = YGNodeGetChildCount(node);
    for (size_t i = 0; i < count; ++i) {
        prepareTree(YGNodeGetChild(node, i));
    }
}

// ---- box write ----

struct Extent {
    float right = 0.0f;
    float bottom = 0.0f;
};

// ox/oy: absolute origin of the parent's content (parent box minus parent scroll offset).
Extent placeTree(ecs_world_t* world, YGNodeRef node, float ox, float oy) {
    const NodeCtx* ctx = ctxOf(node);
    const flecs::entity e = entityOf(world, ctx);
    const float left = YGNodeLayoutGetLeft(node);
    const float top = YGNodeLayoutGetTop(node);
    const float w = YGNodeLayoutGetWidth(node);
    const float h = YGNodeLayoutGetHeight(node);
    Extent self{left + w, top + h};
    if (!e.is_alive()) {
        return self;
    }

    const float x = ox + left;
    const float y = oy + top;
    if (UiBox* box = e.try_get_mut<UiBox>()) {
        *box = UiBox{.x = x, .y = y, .w = w, .h = h};
    }
    UiScroll* scroll = e.try_get_mut<UiScroll>();
    const float cx = x - (scroll != nullptr ? scroll->offX : 0.0f);
    const float cy = y - (scroll != nullptr ? scroll->offY : 0.0f);

    Extent content{};
    const size_t count = YGNodeGetChildCount(node);
    for (size_t i = 0; i < count; ++i) {
        const Extent c = placeTree(world, YGNodeGetChild(node, i), cx, cy);
        content.right = std::max(content.right, c.right);
        content.bottom = std::max(content.bottom, c.bottom);
    }

    if (scroll != nullptr) {
        scroll->viewW = w;
        scroll->viewH = h;
        if (!e.has<UiVirtual>()) {
            scroll->contentW = content.right + YGNodeLayoutGetPadding(node, YGEdgeRight);
            scroll->contentH = content.bottom + YGNodeLayoutGetPadding(node, YGEdgeBottom);
        }
    }
    // A node that does not clip its children also reaches as far as its deep descendants, so an enclosing scroll
    // container measures the whole overflowing subtree, not just its direct children.
    if (!e.has<UiClipChildren>() && scroll == nullptr) {
        self.right = std::max(self.right, left + content.right);
        self.bottom = std::max(self.bottom, top + content.bottom);
    }
    return self;
}

void layoutRoot(flecs::entity root, ecs_world_t* world, float screenW, float screenH) {
    const UiFlex* flex = root.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    YGNodeRef node = flex->node;
    prepareTree(node);
    if (root.has<UiOverlay>()) {
        YGNodeCalculateLayout(node, YGUndefined, YGUndefined, YGDirectionLTR);
    } else {
        YGNodeStyleSetWidth(node, screenW);
        YGNodeStyleSetHeight(node, screenH);
        YGNodeCalculateLayout(node, screenW, screenH, YGDirectionLTR);
    }
    placeTree(world, node, 0.0f, 0.0f);
}

// ---- teardown ----

void destroyTree(ecs_world_t* world, YGNodeRef node) {
    for (size_t n = YGNodeGetChildCount(node); n > 0; n = YGNodeGetChildCount(node)) {
        destroyTree(world, YGNodeGetChild(node, n - 1));
    }
    const flecs::entity e = entityOf(world, ctxOf(node));
    if (e.is_alive()) {
        if (UiFlex* flex = e.try_get_mut<UiFlex>()) {
            flex->node = nullptr;
        }
        e.remove<UiFlex>();
    }
    freeNode(node);
}

} // namespace

bool guiLayoutInit(flecs::world& w) {
    return sharedOf(w) != nullptr;
}

bool guiLayoutAttach(flecs::entity e) {
    if (!e.is_alive()) {
        BH_ASSERT(false, "guiLayoutAttach: dead entity");
        return false;
    }
    flecs::world w = e.world();
    Shared* shared = sharedOf(w);
    if (shared == nullptr) {
        BH_ASSERT(false, "guiLayoutAttach: shared state allocation failed");
        return false;
    }
    if (const UiFlex* existing = e.try_get<UiFlex>(); existing != nullptr && existing->node != nullptr) {
        return true;
    }

    NodeCtx* ctx = new (std::nothrow) NodeCtx{.entity = e.id(), .shared = shared, .textStamp = 0};
    YGNodeRef node = ctx != nullptr ? YGNodeNew() : nullptr;
    if (node == nullptr) {
        delete ctx;
        BH_ASSERT(false, "guiLayoutAttach: YGNode allocation failed");
        return false;
    }
    YGNodeSetContext(node, ctx);

    const flecs::entity parent = e.parent();
    if (parent.is_alive()) {
        if (const UiFlex* pf = parent.try_get<UiFlex>(); pf != nullptr && pf->node != nullptr) {
            YGNodeSetMeasureFunc(pf->node, nullptr);
            if (NodeCtx* pc = ctxOf(pf->node)) {
                pc->textStamp = 0;
            }
            YGNodeInsertChild(pf->node, node, YGNodeGetChildCount(pf->node));
        }
    }

    e.set<UiFlex>(UiFlex{.node = node});
    if (!e.has<UiBox>()) {
        e.set<UiBox>(UiBox{});
    }
    return true;
}

void guiLayoutDetach(flecs::entity e) {
    if (!e.is_alive()) {
        return;
    }
    const UiFlex* flex = e.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    destroyTree(e.world().c_ptr(), flex->node);
}

void guiLayoutInvalidate(flecs::world& w) {
    if (Shared* s = sharedOf(w)) {
        ++s->epoch;
    }
}

void guiLayoutRun(flecs::world& w, float screenW, float screenH) {
    Shared* shared = sharedOf(w);
    if (shared == nullptr) {
        BH_ASSERT(false, "guiLayoutRun: shared state allocation failed");
        return;
    }
    if (!w.has<GuiTextMetrics>()) {
        w.add<GuiTextMetrics>();
        guiTextMetricsDefault(*w.try_get_mut<GuiTextMetrics>());
    }
    shared->world = w.c_ptr();
    shared->metrics = w.try_get<GuiTextMetrics>();
    ecs_world_t* world = w.c_ptr();
    w.each(w.component<UiRoot>().id(), [&](flecs::entity root) { layoutRoot(root, world, screenW, screenH); });
    shared->metrics = nullptr;
}

void guiSetShown(flecs::entity e, bool shown) {
    if (!e.is_alive()) {
        return;
    }
    if (shown) {
        e.remove<UiHidden>();
    } else {
        e.add<UiHidden>();
    }
    if (const UiFlex* flex = e.try_get<UiFlex>(); flex != nullptr && flex->node != nullptr) {
        YGNodeStyleSetDisplay(flex->node, shown ? YGDisplayFlex : YGDisplayNone);
    }
}

void guiLayoutSetRect(flecs::entity e, float x, float y, float w, float h) {
    const UiFlex* flex = e.is_alive() ? e.try_get<UiFlex>() : nullptr;
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    YGNodeStyleSetPositionType(flex->node, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(flex->node, YGEdgeLeft, x);
    YGNodeStyleSetPosition(flex->node, YGEdgeTop, y);
    YGNodeStyleSetWidth(flex->node, w);
    YGNodeStyleSetHeight(flex->node, h);
}

flecs::entity guiLayoutEntity(flecs::world& w, YGNodeConstRef node) {
    return entityOf(w.c_ptr(), ctxOf(node));
}

} // namespace burnhope::gui
