#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <yoga/Yoga.h>

#include <cstdint>
#include <cstring>

namespace burnhope {

static void nodeLabel(const UiState& s, uint16_t id, char* dst, int cap) {
    dst[0] = '-';
    if (cap > 1) {
        dst[1] = '\0';
    }
    if (id >= s.count || !s.ent[id].is_alive() || cap < 2) {
        return;
    }
    if (const UiName* name = s.ent[id].try_get<UiName>(); name != nullptr && name->len > 0) {
        const int n = name->len < cap - 1 ? name->len : cap - 1;
        std::memcpy(dst, name->bytes, static_cast<size_t>(n));
        dst[n] = '\0';
        return;
    }
    if (const UiText* text = s.ent[id].try_get<UiText>(); text != nullptr && text->len > 0 && text->len < 24) {
        const int n = text->len < cap - 1 ? text->len : cap - 1;
        std::memcpy(dst, text->bytes, static_cast<size_t>(n));
        dst[n] = '\0';
    }
}

static std::shared_ptr<spdlog::logger> uiLogger() {
    if (auto channel = spdlog::get("UI")) {
        return channel;
    }
    return spdlog::default_logger();
}

void uiTrace(const UiState& s, const char* fn, uint16_t id) {
    auto log = uiLogger();
    if (log == nullptr || !log->should_log(spdlog::level::trace)) {
        return;
    }
    if (id >= s.count || !s.ent[id].is_alive()) {
        log->trace("ui fn={} id={}", fn != nullptr ? fn : "-", static_cast<unsigned>(id));
        return;
    }
    char name[32];
    nodeLabel(s, id, name, 32);
    const UiBox& box = s.box[id];
    log->trace(
        "ui fn={} id={} name={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
        fn != nullptr ? fn : "-",
        static_cast<unsigned>(id),
        name,
        static_cast<unsigned>(s.parentOf[id]),
        box.x,
        box.y,
        box.w,
        box.h);
}

void uiDumpLayout(const UiState& s) {
    auto log = uiLogger();
    if (log == nullptr) {
        return;
    }
    log->info("ui layout nodes={} laid={:.0f}x{:.0f} dpi={:.2f}", static_cast<unsigned>(s.count), s.laidW, s.laidH, s.dpi);
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || s.yoga[id] == nullptr) {
            continue;
        }
        if (YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone) {
            continue;
        }
        bool buried = false;
        for (uint16_t up = s.parentOf[id]; up != kUiNone && up < s.count; up = s.parentOf[up]) {
            if (s.yoga[up] != nullptr && YGNodeStyleGetDisplay(s.yoga[up]) == YGDisplayNone) {
                buried = true;
                break;
            }
        }
        if (buried) {
            continue;
        }
        const UiName* name = s.ent[id].try_get<UiName>();
        const bool named = name != nullptr && name->len > 0;
        const bool control = s.ent[id].has<UiFocusable>();
        if (!named && !control) {
            continue;
        }
        char label[32];
        nodeLabel(s, id, label, 32);
        const UiBox& box = s.box[id];
        const bool bad = box.w != box.w || box.h != box.h || box.w < 1.0f || box.h < 1.0f || box.x + box.w < 0.0f || box.y + box.h < 0.0f
            || (s.laidW > 0.0f && box.x > s.laidW) || (s.laidH > 0.0f && box.y > s.laidH);
        log->log(
            bad ? spdlog::level::warn : spdlog::level::info,
            "ui {} id={} name={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
            bad ? "miss" : "box",
            static_cast<unsigned>(id),
            label,
            static_cast<unsigned>(s.parentOf[id]),
            box.x,
            box.y,
            box.w,
            box.h);
    }
}

} // namespace burnhope
