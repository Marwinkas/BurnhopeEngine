#include "app/Engine.hpp"
#include "asset/AssimpImporter.hpp"

#include <sys/stat.h>
#include "app/Shell.hpp"
#include "render/SceneFrame.hpp"
#include "rhi/Device.hpp"
#include "ui/Panel.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {
namespace {

void onColorClose(Host&, int, void* user) {
    auto* colors = static_cast<ColorBook*>(user);
    colors->present = 0;
    ++colors->gen;
}

void openNamed(Host& host, const ViewDesc& desc) {
    (void)hostOpen(host, desc);
}

void onShellOps(Host& host, int, const WindowOps& ops, void* user) {
    auto* colors = static_cast<ColorBook*>(user);
    if (ops.openTool) {
        ViewDesc desc{};
        desc.name = "tool";
        desc.window.title = "Burnhope";
        desc.window.width = 560;
        desc.window.height = 420;
        desc.window.minWidth = 360;
        desc.window.minHeight = 240;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = toolBuild;
        desc.onOps = onShellOps;
        desc.user = colors;
        openNamed(host, desc);
    }
    if (ops.openColor) {
        ViewDesc desc{};
        desc.name = "color";
        desc.window.title = "Color";
        desc.window.width = 440;
        desc.window.height = 720;
        desc.window.minWidth = 360;
        desc.window.minHeight = 480;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = colorBuild;
        desc.book = colors;
        desc.onOps = onShellOps;
        desc.onClose = onColorClose;
        desc.user = colors;
        openNamed(host, desc);
    }
}

bool sceneTick(void* user, const InputFrame& input, float dt) {
    return sceneFrameTick(static_cast<SceneFrame*>(user), input, dt);
}

void sceneRecord(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame) {
    sceneFrameRecord(static_cast<SceneFrame*>(user), cmd, device, heaps, swap, frame);
}

bool wantScene(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--ui") == 0) {
            return false;
        }
    }
    return true;
}

void releaseScene(void* user, Device& device) {
    sceneFrameReleaseGpu(static_cast<SceneFrame*>(user), device);
}

} // namespace

void sceneHud(Canvas& canvas, void*);

bool engineInit(Engine& e, int argc, char** argv) {
    (void)argc;
    (void)argv;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Yoga/Flecs canvas.");
    if (!hostInit(e.host)) {
        return false;
    }
    if (wantScene(argc, argv)) {
        e.scene = sceneFrameCreate();
        const char* cache = "cache/bistro.bhop";
        if (!sceneFrameLoadBhop(e.scene, cache)) {
            mkdir("cache", 0755);
            const char* parts[] = {
                "testscene/BistroExterior.fbx",
                "testscene/BistroInterior.fbx",
                "testscene/BistroInterior_Wine.fbx",
            };
            if (sceneImportFbxList(parts, 3, cache)) {
                (void)sceneFrameLoadBhop(e.scene, cache);
            }
        }
        sceneFrameShift(e.scene, 2500.0f, 0.0f, 0.0f);
        const char* sponza = "cache/sponza.bhop";
        if (!sceneFrameAppendBhop(e.scene, sponza)) {
            mkdir("cache", 0755);
            const char* packs[] = {
                "newscenetest/main_sponza/NewSponza_Main_glTF_003.gltf",
                "newscenetest/pkg_a_curtains/NewSponza_Curtains_glTF.gltf",
                "newscenetest/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf",
                "newscenetest/pkg_c_trees/NewSponza_CypressTree_glTF.gltf",
                "newscenetest/pkg_d_10k_candles/NewSponza_4_Combined_glTF.gltf",
            };
            if (sceneImportFbxList(packs, 5, sponza)) {
                (void)sceneFrameAppendBhop(e.scene, sponza);
            }
        }
        ViewDesc main{};
        main.name = "scene";
        main.window.title = "Burnhope scene  RMB look  WASD fly";
        main.window.width = 1600;
        main.window.height = 900;
        main.window.bordered = true;
        main.window.resizable = true;
        main.frame.present = Present::Immediate;
        main.builder = sceneHud;
        main.sceneTick = sceneTick;
        main.sceneRecord = sceneRecord;
        main.user = e.scene;
        e.host.releaseGpu = releaseScene;
        e.host.releaseUser = e.scene;
        return hostOpen(e.host, main) >= 0;
    }
    ViewDesc main{};
    main.name = "main";
    main.window.title = "Burnhope";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = false;
    main.window.resizable = true;
    main.frame.present = Present::Mailbox;
    main.builder = shellBuild;
    main.book = &e.colors;
    main.onOps = onShellOps;
    main.user = &e.colors;
    if (hostOpen(e.host, main) < 0) {
        return false;
    }
    return true;
}

void engineShutdown(Engine& e) {
    if (e.scene != nullptr) {
        sceneFrameDestroy(e.scene, e.host.device);
        e.scene = nullptr;
    }
    hostShutdown(e.host);
}

Panel rowSlide(Panel panel, const char* text, const char* name, float value, float minV, float maxV) {
    Panel lab = panel.label(text);
    (void)lab;
    Panel slide = panel.slider(value, minV, maxV);
    slide.name(name);
    canvasSetHeight(*panel.canvas, slide.id, 28.0f);
    return slide;
}

void sceneHud(Canvas& canvas, void*) {
    if (!canvasUseFont(canvas, 30.0f) || canvas.state == nullptr) {
        return;
    }
    UiFlex rootFlex{};
    rootFlex.grow = 1.0f;
    rootFlex.direction = 1;
    rootFlex.heightMode = static_cast<uint8_t>(UiSize::Percent);
    rootFlex.height = 100.0f;
    UiPaint clear{};
    clear.a = 0.0f;
    clear.role = static_cast<uint8_t>(UiRole::Hidden);
    Panel root = Panel::make(canvas, kUiNone, rootFlex, clear);
    UiFlex box{};
    box.widthMode = static_cast<uint8_t>(UiSize::Px);
    box.width = 360.0f;
    box.heightMode = static_cast<uint8_t>(UiSize::Percent);
    box.height = 100.0f;
    box.shrink = 0.0f;
    UiPaint scrollPaint{};
    scrollPaint.role = static_cast<uint8_t>(UiRole::Scroll);
    scrollPaint.r = 0.05f;
    scrollPaint.g = 0.06f;
    scrollPaint.b = 0.08f;
    scrollPaint.a = 0.86f;
    scrollPaint.radius = 6.0f;
    Panel viewport = root.child(box, scrollPaint);
    UiFlex columnFlex{};
    columnFlex.gap = 6.0f;
    columnFlex.pad = 12.0f;
    columnFlex.shrink = 0.0f;
    UiPaint columnPaint{};
    columnPaint.a = 0.0f;
    Panel panel = viewport.child(columnFlex, columnPaint);
    canvasScroll(canvas, viewport.id, panel.id);
    UiFlex grip{};
    grip.widthMode = static_cast<uint8_t>(UiSize::Px);
    grip.width = 8.0f;
    grip.heightMode = static_cast<uint8_t>(UiSize::Percent);
    grip.height = 100.0f;
    grip.shrink = 0.0f;
    UiPaint gripPaint{};
    gripPaint.role = static_cast<uint8_t>(UiRole::Panel);
    gripPaint.tag = -5;
    gripPaint.r = 0.35f;
    gripPaint.g = 0.42f;
    gripPaint.b = 0.55f;
    gripPaint.a = 0.45f;
    Panel gripPanel = root.child(grip, gripPaint);
    (void)gripPanel;
    UiFlex rest{};
    rest.grow = 1.0f;
    UiPaint restPaint{};
    restPaint.a = 0.0f;
    restPaint.role = static_cast<uint8_t>(UiRole::Hidden);
    Panel restPanel = root.child(rest, restPaint);
    (void)restPanel;
    panel.label("fps").name("stats");
    panel.label("mem").name("mem");
    panel.label("gpu").name("load");
    auto tick = [&](const char* label, const char* id, bool on) {
        Panel box = panel.check(label);
        box.name(id);
        if (on && panel.canvas != nullptr) {
            canvasCheck(*panel.canvas, box.id, 0, 0, true);
        }
    };
    panel.label("кулинг").name("h1");
    panel.check("Freeze Frustum").name("freeze");
    tick("конус", "cone", true);
    tick("Hi-Z", "hiz", true);
    tick("LOD", "lod", true);
    panel.label("Force LOD  -1..3").name("lodlab");
    panel.spin(-1.0f, -1.0f, 3.0f, 1.0f).name("lodf");
    tick("пирамида", "frust", true);
    tick("мелкие", "small", true);
    panel.label("свет").name("h2");
    tick("кластер ламп", "cluster", true);
    tick("тени", "shadow", true);
    tick("зонды SH", "probes", true);
    panel.label("профиль света").name("h3");
    panel.radio("Низкий", 3).name("prof0");
    panel.radio("Баланс", 3).name("prof1");
    panel.radio("Ультра", 3).name("prof2");
    tick("radiance", "rcen", true);
    rowSlide(panel, "RC лучи", "rcRays", 8.0f, 1.0f, 32.0f);
    rowSlide(panel, "RC клетка м", "rcCell", 0.45f, 0.1f, 4.0f);
    rowSlide(panel, "RC сила", "rcGain", 1.0f, 0.0f, 4.0f);
    rowSlide(panel, "RC длина м", "rcMax", 48.0f, 2.0f, 200.0f);
    tick("GTAO", "gtaoen", true);
    tick("CMAA", "cmaa", true);
    panel.check("SMAA").name("smaa");
    tick("Bloom", "bloomen", true);
    tick("солнце", "sun", true);
    rowSlide(panel, "солнце зенит", "zenith", 35.0f, -10.0f, 80.0f);
    rowSlide(panel, "солнце азимут", "azimuth", 30.0f, 0.0f, 360.0f);
    tick("точки", "points", true);
    tick("прожекторы", "spots", true);
    tick("площадь", "area", true);
    tick("небо", "sky", true);
    tick("облака", "clouden", true);
    tick("туман", "fogen", true);
    tick("отражение", "reflecten", true);
    tick("контакт", "contacten", true);
    panel.check("вирт. текстуры").name("vt");
    panel.radio("Lit", 1).name("mode0");
    panel.radio("Normals", 1).name("mode2");
    panel.radio("Primitive ID", 1).name("mode1");
    panel.radio("GI", 1).name("mode4");
    panel.radio("каскады", 1).name("mode5");
    panel.radio("контакт", 1).name("mode6");
    panel.radio("луч", 1).name("mode7");
    panel.radio("прямой", 1).name("mode8");
    panel.radio("альбедо", 1).name("mode9");
    panel.radio("солнце", 1).name("mode10");
    panel.radio("AgX", 2).name("tone0");
    panel.radio("ACES", 2).name("tone1");
    panel.radio("Reinhard", 2).name("tone2");
    panel.radio("Linear", 2).name("tone3");
    rowSlide(panel, "зерно", "grain", 0.0f, 0.0f, 0.12f);
    rowSlide(panel, "блюм", "bloom", 0.15f, 0.0f, 0.80f);
    rowSlide(panel, "порог блюма", "bloomth", 0.35f, 0.0f, 2.0f);
    rowSlide(panel, "виньетка", "vig", 0.35f, 0.0f, 2.0f);
    rowSlide(panel, "хромат. пкс", "ca", 2.0f, 0.0f, 4.0f);
    rowSlide(panel, "SSCS", "contact", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "SSCS длина м", "sscsR", 0.2f, 0.05f, 2.0f);
    rowSlide(panel, "SSCS толщина", "sscsT", 0.02f, 0.005f, 0.2f);
    rowSlide(panel, "SSCS шаги", "sscsS", 12.0f, 4.0f, 16.0f);
    rowSlide(panel, "SSCS экран px", "sscsP", 128.0f, 4.0f, 256.0f);
    rowSlide(panel, "окклюзия блика", "spec", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "Toksvig", "tok", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "параллакс", "pom", 0.35f, 0.0f, 1.0f);
    rowSlide(panel, "multi-scatter", "ggx", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "радиус GTAO", "gtaor", 2.5f, 0.2f, 20.0f);
    rowSlide(panel, "GTAO срезы", "gtaoSl", 6.0f, 1.0f, 8.0f);
    rowSlide(panel, "GTAO шаги", "gtaoSt", 6.0f, 1.0f, 12.0f);
    rowSlide(panel, "GTAO сила", "gtaoPw", 1.6f, 0.2f, 4.0f);
    rowSlide(panel, "GTAO край", "gtaoFa", 1.0f, 0.1f, 1.0f);
    rowSlide(panel, "GTAO", "gtao", 1.2f, 0.0f, 4.0f);
    rowSlide(panel, "свечи", "lamp", 1.0f, 0.0f, 1.0f);
    rowSlide(panel, "облака", "clouds", 0.42f, 0.0f, 0.95f);
    rowSlide(panel, "плотность", "cdens", 0.08f, 0.0f, 0.3f);
    rowSlide(panel, "туман", "fog", 0.002f, 0.0f, 0.08f);
    rowSlide(panel, "высота тумана", "fogH", 4.0f, -30.0f, 40.0f);
    rowSlide(panel, "свет в тумане", "fogS", 1.0f, 0.0f, 2.0f);
    for (int i = 0; i < 12; ++i) {
        char name[8];
        std::snprintf(name, sizeof(name), "g%d", i);
        panel.label("").name(name);
    }
    const char* on[] = {"hiz", "cone", "lod", "frust", "small", "shadow", "cluster", "probes", "gtaoen", "cmaa", "bloomen"};
    for (const char* name : on) {
        canvasCheck(canvas, canvasFind(canvas, name), 0, 0, true);
    }
    canvasCheck(canvas, canvasFind(canvas, "prof0"), 1, 3, true);
    canvasCheck(canvas, canvasFind(canvas, "mode0"), 1, 1, true);
    canvasCheck(canvas, canvasFind(canvas, "tone0"), 1, 2, true);
}

int readFile(const char* path, char* dst, int cap) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0 || cap <= 1) {
        return 0;
    }
    const int n = static_cast<int>(read(fd, dst, static_cast<size_t>(cap - 1)));
    close(fd);
    if (n <= 0) {
        return 0;
    }
    dst[n] = '\0';
    return n;
}

float slideAt(const Canvas& canvas, const char* name, float fallback) {
    float value = fallback;
    float minV = 0.0f;
    float maxV = 1.0f;
    if (!canvasRange(canvas, canvasFind(canvas, name), value, minV, maxV)) {
        return fallback;
    }
    return value;
}

bool checkOn(const Canvas& canvas, const char* name, bool fallback) {
    const int on = canvasCheckOn(canvas, name);
    return on < 0 ? fallback : on != 0;
}

void hostLoad(float& cpu, float& gpu) {
    char buf[256];
    static uint64_t prevIdle = 0;
    static uint64_t prevTotal = 0;
    if (readFile("/proc/stat", buf, sizeof(buf)) > 0) {
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, soft = 0;
        if (std::sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &soft) == 7) {
            const uint64_t total = user + nice + system + idle + iowait + irq + soft;
            const uint64_t idleNow = idle + iowait;
            if (prevTotal != 0 && total > prevTotal) {
                cpu = 100.0f * static_cast<float>((total - prevTotal) - (idleNow - prevIdle)) / static_cast<float>(total - prevTotal);
            }
            prevIdle = idleNow;
            prevTotal = total;
        }
    }
    gpu = -1.0f;
    if (readFile("/sys/class/drm/card1/device/gpu_busy_percent", buf, sizeof(buf)) > 0
        || readFile("/sys/class/drm/card0/device/gpu_busy_percent", buf, sizeof(buf)) > 0) {
        gpu = static_cast<float>(std::atoi(buf));
    }
}

bool engineTick(Engine& e) {
    const bool ok = hostTick(e.host);
    if (e.scene != nullptr) {
        sceneFrameKick(e.scene, e.host.device);
    }
    if (e.scene != nullptr && e.host.primary >= 0) {
        char fps[96];
        sceneFrameStats(e.scene, fps, sizeof(fps));
        char mem[64];
        uint64_t rss = 0;
        if (readFile("/proc/self/statm", mem, sizeof(mem)) > 0) {
            unsigned long pages = 0;
            unsigned long resident = 0;
            if (std::sscanf(mem, "%lu %lu", &pages, &resident) == 2) {
                rss = static_cast<uint64_t>(resident) * 4096ull;
            }
        }
        float cpu = 0.0f;
        float gpu = -1.0f;
        hostLoad(cpu, gpu);
        const uint64_t vram = deviceMemoryUsed(e.host.device);
        char memLine[96];
        char loadLine[96];
        std::snprintf(memLine, sizeof(memLine), "vram %.0f МБ   ram %.0f МБ", static_cast<double>(vram) / (1024.0 * 1024.0), static_cast<double>(rss) / (1024.0 * 1024.0));
        if (gpu >= 0.0f) {
            std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu %.0f%%", cpu, gpu);
        } else {
            std::snprintf(loadLine, sizeof(loadLine), "cpu %.0f%%   gpu n/a", cpu);
        }
        Panel::find(e.host.view[e.host.primary].canvas, "stats").setText(fps);
        Panel::find(e.host.view[e.host.primary].canvas, "mem").setText(memLine);
        Panel::find(e.host.view[e.host.primary].canvas, "load").setText(loadLine);
        const Canvas& hud = e.host.view[e.host.primary].canvas;
        uint32_t mode = 0;
        for (uint32_t i = 0; i < 11; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "mode%u", i);
            if (canvasCheckOn(hud, name) == 1) {
                mode = i;
            }
        }
        sceneFrameDebug(e.scene,
            checkOn(hud, "hiz", true), checkOn(hud, "cone", true),
            checkOn(hud, "lod", true), checkOn(hud, "frust", true), checkOn(hud, "small", true), checkOn(hud, "shadow", true),
            mode);
        HudPost hudPost{};
        hudPost.grain = slideAt(hud, "grain", hudPost.grain);
        hudPost.bloom = slideAt(hud, "bloom", hudPost.bloom);
        hudPost.vignette = slideAt(hud, "vig", hudPost.vignette);
        hudPost.ca = slideAt(hud, "ca", hudPost.ca);
        hudPost.contact = slideAt(hud, "contact", hudPost.contact);
        hudPost.specOcc = slideAt(hud, "spec", hudPost.specOcc);
        hudPost.toksvig = slideAt(hud, "tok", hudPost.toksvig);
        hudPost.pom = slideAt(hud, "pom", hudPost.pom);
        hudPost.ggx = slideAt(hud, "ggx", hudPost.ggx);
        hudPost.gtao = slideAt(hud, "gtao", hudPost.gtao);
        hudPost.lamp = slideAt(hud, "lamp", hudPost.lamp);
        sceneFrameTune(e.scene, hudPost);
        SceneTweaks tweaks{};
        tweaks.forceLod = static_cast<int32_t>(slideAt(hud, "lodf", -1.0f));
        tweaks.gtaoRadius = slideAt(hud, "gtaor", 2.5f);
        tweaks.gtaoSteps = slideAt(hud, "gtaoSt", 6.0f);
        tweaks.gtaoSlices = slideAt(hud, "gtaoSl", 6.0f);
        tweaks.gtaoPower = slideAt(hud, "gtaoPw", 1.6f);
        tweaks.gtaoFalloff = slideAt(hud, "gtaoFa", 1.0f);
        tweaks.sscsReach = slideAt(hud, "sscsR", 0.2f);
        tweaks.sscsThick = slideAt(hud, "sscsT", 0.02f);
        tweaks.sscsSteps = slideAt(hud, "sscsS", 12.0f);
        tweaks.sscsPx = slideAt(hud, "sscsP", 128.0f);
        tweaks.rcRays = slideAt(hud, "rcRays", 8.0f);
        tweaks.rcSpacing = slideAt(hud, "rcCell", 0.45f);
        tweaks.rcIntensity = slideAt(hud, "rcGain", 1.0f);
        tweaks.rcMax = slideAt(hud, "rcMax", 48.0f);
        tweaks.bloomThreshold = slideAt(hud, "bloomth", 0.35f);
        tweaks.fogDensity = slideAt(hud, "fog", 0.002f);
        tweaks.fogHeight = slideAt(hud, "fogH", 4.0f);
        tweaks.fogScatter = slideAt(hud, "fogS", 1.0f);
        tweaks.sunZenith = slideAt(hud, "zenith", 35.0f);
        tweaks.sunAzimuth = slideAt(hud, "azimuth", 30.0f);
        tweaks.cloudCoverage = slideAt(hud, "clouds", 0.42f);
        tweaks.cloudDensity = slideAt(hud, "cdens", 0.08f);
        tweaks.freeze = checkOn(hud, "freeze", false);
        tweaks.passMask = 0;
        if (checkOn(hud, "cluster", true)) {
            tweaks.passMask |= 1u;
        }
        if (checkOn(hud, "probes", true)) {
            tweaks.passMask |= 2u;
        }
        if (checkOn(hud, "cmaa", true)) {
            tweaks.passMask |= 8u;
        }
        if (checkOn(hud, "gtaoen", true)) {
            tweaks.passMask |= 16u;
        }
        if (checkOn(hud, "bloomen", true)) {
            tweaks.passMask |= 32u;
        }
        if (checkOn(hud, "smaa", false)) {
            tweaks.passMask |= 4u;
        }
        if (checkOn(hud, "sun", true)) {
            tweaks.passMask |= 256u;
        }
        if (checkOn(hud, "points", true)) {
            tweaks.passMask |= 512u;
        }
        if (checkOn(hud, "spots", true)) {
            tweaks.passMask |= 65536u;
        }
        if (checkOn(hud, "area", true)) {
            tweaks.passMask |= 131072u;
        }
        if (checkOn(hud, "sky", true)) {
            tweaks.passMask |= 1024u;
        }
        if (checkOn(hud, "clouden", true)) {
            tweaks.passMask |= 2048u;
        }
        if (checkOn(hud, "fogen", true)) {
            tweaks.passMask |= 4096u;
        }
        if (checkOn(hud, "reflecten", true)) {
            tweaks.passMask |= 8192u;
        }
        if (checkOn(hud, "contacten", true)) {
            tweaks.passMask |= 16384u;
        }
        if (checkOn(hud, "vt", false)) {
            tweaks.passMask |= 32768u;
        }
        tweaks.lightProfile = 0;
        for (uint32_t i = 0; i < 3; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "prof%u", i);
            if (canvasCheckOn(hud, name) == 1) {
                tweaks.lightProfile = i;
            }
        }
        if (checkOn(hud, "rcen", true)) {
            tweaks.passMask |= 128u;
        }
        tweaks.tonemapper = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "tone%u", i);
            if (canvasCheckOn(hud, name) == 1) {
                tweaks.tonemapper = i;
            }
        }
        sceneFrameTweaks(e.scene, tweaks);
        const char* gpuText = sceneFrameGpuText(e.scene);
        char lines[12][96]{};
        uint32_t line = 0;
        const char* cursor = gpuText != nullptr ? gpuText : "";
        while (line < 12 && cursor[0] != '\0') {
            const char* nl = std::strchr(cursor, '\n');
            const size_t n = nl != nullptr ? static_cast<size_t>(nl - cursor) : std::strlen(cursor);
            const size_t copy = n < 95 ? n : 95;
            std::memcpy(lines[line], cursor, copy);
            lines[line][copy] = '\0';
            line += 1;
            if (nl == nullptr) {
                break;
            }
            cursor = nl + 1;
        }
        for (uint32_t i = 0; i < 12; ++i) {
            char name[8];
            std::snprintf(name, sizeof(name), "g%u", i);
            Panel::find(e.host.view[e.host.primary].canvas, name).setText(lines[i]);
        }
        windowTitle(e.host.view[e.host.primary].window, sceneFrameTitle(e.scene));
    }
    return ok;
}

} // namespace burnhope
