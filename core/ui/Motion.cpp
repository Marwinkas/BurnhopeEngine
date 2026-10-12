#include "ui/State.hpp"

namespace burnhope {
namespace {

void tickPhoto(UiState& s) {
    float dt = s.time - s.prevTime;
    if (s.prevTime <= 0.0f || dt < 0.0f || dt > 0.5f) {
        dt = 0.0f;
    }
    s.prevTime = s.time;
    s.world.each([&](flecs::entity, UiImage& image) {
        if (image.frames < 2) {
            return;
        }
        image.acc += dt * 1000.0f;
        uint16_t delay = image.delayMs[image.frame];
        if (delay < 20) {
            delay = 100;
        }
        if (image.acc >= static_cast<float>(delay)) {
            image.acc = 0.0f;
            image.frame = static_cast<uint8_t>((image.frame + 1u) % image.frames);
            s.visualDirty = true;
        }
    });
}

} // namespace

void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a) {
    if (id >= kUiCap) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    UiAnim& anim = s.anim[id];
    anim.fr = paint != nullptr ? paint->r : r;
    anim.fg = paint != nullptr ? paint->g : g;
    anim.fb = paint != nullptr ? paint->b : b;
    anim.fa = paint != nullptr ? paint->a : a;
    if (anim.on != 0 && anim.dur > 0.0f) {
        float u = (s.time - anim.t0) / anim.dur;
        if (u < 0.0f) {
            u = 0.0f;
        }
        if (u > 1.0f) {
            u = 1.0f;
        }
        anim.fr += (anim.tr - anim.fr) * u;
        anim.fg += (anim.tg - anim.fg) * u;
        anim.fb += (anim.tb - anim.fb) * u;
        anim.fa += (anim.ta - anim.fa) * u;
    }
    anim.tr = r;
    anim.tg = g;
    anim.tb = b;
    anim.ta = a;
    anim.t0 = s.time;
    anim.dur = s.look.motion > 0.0f ? s.look.motion : 0.01f;
    anim.on = 1;
}

bool uiTickMotion(UiState& s) {
    tickPhoto(s);
    bool live = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.anim[id].on != 0) {
            live = true;
            if (s.time - s.anim[id].t0 >= s.anim[id].dur) {
                s.anim[id].on = 0;
            }
        }
    }
    return live;
}

} // namespace burnhope
