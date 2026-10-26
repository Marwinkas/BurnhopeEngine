#include "Str.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/HeapBind.hpp"
#include "render/SceneFrameImpl.hpp"

#include <spdlog/spdlog.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace burnhope {

namespace {

bool fileReadable(const char* path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}



void resolveTexturePath(char* path, size_t cap, const char* root, const char* name) {
    std::snprintf(path, cap, "%s/%s", root, name);
    if (fileReadable(path) || name == nullptr || name[0] == '\0') {
        return;
    }
    const char* cut[] = {".d", ".dd", ".pn", ".jp"};
    const char* full[] = {".dds", ".dds", ".png", ".jpg"};
    for (uint32_t i = 0; i < 4; ++i) {
        if (!strExtIs(name, cut[i])) {
            continue;
        }
        const size_t n = std::strlen(name);
        const size_t c = std::strlen(cut[i]);
        std::snprintf(path, cap, "%s/%.*s%s", root, static_cast<int>(n - c), name, full[i]);
        if (fileReadable(path)) {
            return;
        }
    }
    std::error_code ec;
    const std::filesystem::path dir(root);
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    const size_t n = std::strlen(name);
    std::string only;
    uint32_t hits = 0;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string file = it->path().filename().string();
        if (file.empty() || file[0] == '.') {
            continue;
        }
        if (file.compare(0, n, name) != 0) {
            continue;
        }
        only = file;
        hits += 1;
        if (hits > 1) {
            break;
        }
    }
    if (hits == 1 && !only.empty()) {
        std::snprintf(path, cap, "%s/%s", root, only.c_str());
    }
}

bool uploadDummy(GpuImage& img, Device& device, const char* name) {
    uint8_t px[4] = {0xff, 0xff, 0xff, 0xff};
    if (nameHas(name, "normal") || nameHas(name, "nrm")) {
        px[0] = 0x80;
        px[1] = 0x80;
        px[2] = 0xff;
        px[3] = 0xff;
    } else if (nameHas(name, "orm") || nameHas(name, "spec") || nameHas(name, "rough") || nameHas(name, "metal")) {
        px[0] = 0xff;
        px[1] = 0x80;
        px[2] = 0x00;
        px[3] = 0xff;
    }
    return imageUploadUncompressed(img, device, px, 1, 1);
}

void ddsCachePath(char* out, size_t cap, const char* resolved) {
    std::snprintf(out, cap, "cache/textures/");
    size_t n = std::strlen(out);
    if (resolved != nullptr) {
        for (const char* p = resolved; *p != '\0' && n + 6 < cap; ++p) {
            const char c = (*p == '/' || *p == ' ') ? '_' : *p;
            out[n++] = c;
        }
    }
    if (n + 5 < cap) {
        out[n++] = '.';
        out[n++] = 'd';
        out[n++] = 'd';
        out[n++] = 's';
    }
    out[n] = '\0';
}

bool imageName(const std::filesystem::path& path) {
    const std::string file = path.string();
    return strIsImagePath(file.c_str()) || strExtIs(file.c_str(), ".dds");
}

bool skipTextureDir(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == "cache" || name == "build" || name == "build-core" || name == ".git" || name == "shaders"
        || name == ".cache";
}

void indexTextureDir(const std::filesystem::path& dir, std::unordered_map<std::string, std::vector<std::string>>& index,
    int depth) {
    if (depth < 0 || skipTextureDir(dir)) {
        return;
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& child = it->path();
        std::error_code kind;
        if (it->is_directory(kind)) {
            indexTextureDir(child, index, depth - 1);
            continue;
        }
        if (kind || !it->is_regular_file(kind) || !imageName(child)) {
            continue;
        }
        auto& hits = index[child.filename().string()];
        if (hits.size() < 8u) {
            hits.push_back(child.string());
        }
    }
}

const char* packHint(const char* name) {
    if (name == nullptr || name[0] == '\0' || name[1] != '/') {
        return nullptr;
    }
    switch (name[0]) {
    case 'b':
        return "curtain";
    case 'c':
        return "ivy";
    case 'd':
        return "tree";
    case 'e':
        return "candle";
    default:
        return "sponza";
    }
}

const std::string* pickTexture(const std::vector<std::string>& hits, const char* hint) {
    const std::string* fallback = nullptr;
    for (const std::string& path : hits) {
        if (hint != nullptr && nameHas(path.c_str(), hint)) {
            return &path;
        }
        if (fallback == nullptr && nameHas(path.c_str(), "texture")) {
            fallback = &path;
        }
    }
    if (fallback != nullptr) {
        return fallback;
    }
    return hits.empty() ? nullptr : &hits.front();
}

void indexSceneTextures(std::unordered_map<std::string, std::vector<std::string>>& index) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        indexTextureDir(cwd, index, 4);
    }
    char exe[512]{};
    if (!platformExePath(exe, sizeof(exe))) {
        return;
    }
    const std::filesystem::path project = std::filesystem::path(exe).parent_path().parent_path();
    if (project.empty() || project == cwd || project == project.root_path()) {
        return;
    }
    indexTextureDir(project, index, 4);
}

const char* textureLeaf(const char* name) {
    if (name != nullptr && name[0] != '\0' && name[1] == '/') {
        return name + 2;
    }
    return name;
}

uint32_t actorMaterialSlot(const SceneFrame& scene, uint8_t actor) {
    if (actor >= scene.actors.count) {
        return 0xffffffffu;
    }
    flecs::entity ent = scene.actors.ent[actor];
    if (ent.is_alive() && ent.has<MeshBind>()) {
        return ent.get<MeshBind>().material;
    }
    for (uint8_t n = 0; n < scene.propCount; ++n) {
        if (scene.props[n].actor == actor) {
            return scene.props[n].material;
        }
    }
    return 0xffffffffu;
}

void queueDocMap(SceneFrame& scene, uint8_t actor, uint8_t channel, const char* path, const char* previous) {
    if (path != nullptr && path[0] != '\0') {
        if (previous != nullptr && std::strcmp(path, previous) == 0) {
            return;
        }
        sceneFrameQueueTexture(&scene, actor, channel, path);
        return;
    }
    const uint32_t id = actorMaterialSlot(scene, actor);
    if (id >= scene.materialCount || id >= scene.ownedMat.size()) {
        return;
    }
    MaterialGpuData& mat = scene.ownedMat[id];
    uint32_t none = 0u;
    uint32_t* slot = materialTexSlot(mat, channel, &none);
    if (slot == nullptr) {
        return;
    }
    if (*slot == none && (channel != 2u || (mat.flags & kMatOrm) == 0u)) {
        return;
    }
    *slot = none;
    if (channel == 2u) {
        mat.flags &= ~kMatOrm;
    }
    if (scene.materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene.materialsBuf.mapped)[id] = mat;
    }
}

} // namespace

MaterialGpuData* materialAt(SceneFrameImpl& s, uint32_t id) {
    if (s.materialSrc == nullptr || id >= s.materialCount) {
        return nullptr;
    }
    if (!s.ownedMat.empty() && s.materialSrc == s.ownedMat.data() && id < s.ownedMat.size()) {
        return &s.ownedMat[id];
    }
    if (s.materialSrc == s.fallbackMat && id < 4u) {
        return &s.fallbackMat[id];
    }
    return nullptr;
}

void materialOwnFromView(SceneFrameImpl& s, const SceneBlobView& view) {
    const uint32_t count = view.info != nullptr ? view.info->materialCount : 0u;
    s.ownedMat.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* src = nullptr;
        if (view.materialRaw != nullptr && view.materialStride > 0u) {
            src = view.materialRaw + static_cast<size_t>(i) * view.materialStride;
        }
        materialAdoptRecord(s.ownedMat[i], src, view.materialStride);
    }
    s.materialSrc = count > 0u ? s.ownedMat.data() : nullptr;
    s.materialCount = count;
}

bool nameHas(const char* text, const char* needle) {
    return strContainsCi(text, needle);
}

bool loadTextures(SceneFrameImpl& s, Device& device) {
    uint32_t count = s.texNameCount == 0 ? 1u : s.texNameCount;
    if (count > 768u) {
        count = 768u;
    }
    s.textures.assign(count, GpuImage{});
    s.textureSlots = count;
    const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    if (!imageUploadUncompressed(s.textures[0], device, white, 1, 1)) {
        return false;
    }
    std::unordered_map<std::string, std::vector<std::string>> indexed;
    indexSceneTextures(indexed);
    for (uint32_t i = 1; i < count; ++i) {
        char path[512];
        const char* name = s.texNames[i].file;
        const char* leaf = textureLeaf(name);
        path[0] = '\0';
        auto tryRoot = [&](const char* root, const char* file) {
            if (root == nullptr || root[0] == '\0' || file == nullptr || file[0] == '\0') {
                return false;
            }
            resolveTexturePath(path, sizeof(path), root, file);
            return fileReadable(path);
        };
        char beside[512]{};
        char textures[540]{};
        bool found = false;
        if (s.bhopPath[0] != '\0' && pathParent(s.bhopPath, beside, sizeof(beside))) {
            found = tryRoot(beside, name);
            if (!found) {
                std::snprintf(textures, sizeof(textures), "%s/textures", beside);
                found = tryRoot(textures, leaf);
            }
            if (!found) {
                found = tryRoot(beside, leaf);
            }
        }
        if (!found) {
            found = tryRoot("assets/textures", leaf);
        }
        if (!found && leaf != nullptr) {
            const auto hit = indexed.find(leaf);
            if (hit != indexed.end()) {
                const std::string* picked = pickTexture(hit->second, packHint(name));
                if (picked != nullptr && picked->size() + 1 < sizeof(path)) {
                    std::memcpy(path, picked->c_str(), picked->size() + 1);
                    found = fileReadable(path);
                }
            }
        }
        if (!found) {
            uploadDummy(s.textures[i], device, name);
            continue;
        }
        char cache[512];
        ddsCachePath(cache, sizeof(cache), path);
        bool dataMap = false;
        if (s.materialSrc != nullptr) {
            for (uint32_t m = 0; m < s.materialCount; ++m) {
                const MaterialGpuData& mat = s.materialSrc[m];
                if (mat.normalTexIndex == i || mat.ormTexIndex == i || mat.pad[0] == i || mat.pad[2] == i || mat.pad[3] == i) {
                    dataMap = true;
                    break;
                }
            }
        }
        const bool precise = dataMap || nameHas(name, "normal") || nameHas(name, "nrm") || nameHas(name, "orm")
            || nameHas(name, "rough") || nameHas(name, "metal") || nameHas(name, "spec");
        const bool srgb = !precise;
        bool loaded = imageLoadDds(s.textures[i], device, cache, srgb);
        if (loaded && precise && s.textures[i].format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
            gpuImageDestroy(s.textures[i], device);
            loaded = false;
        }
        if (!loaded) {
            loaded = imageLoadDds(s.textures[i], device, path, srgb);
        }
        if (!loaded) {
            ImageCpu cpu{};
            if (imageLoadFile(cpu, path)) {
                if (cpu.hdr != 0 && cpu.rgb != nullptr && srgb) {
                    loaded = gpuImageUploadHdr(s.textures[i], device, cpu.rgb, cpu.width, cpu.height);
                } else if (cpu.rgba != nullptr) {
                    loaded = gpuImageUploadPbr(
                        s.textures[i], device, cpu.rgba, cpu.width, cpu.height, srgb ? PbrTex::Color : PbrTex::Data, cache);
                }
            }
            imageCpuFree(cpu);
        }
        if (!loaded && !uploadDummy(s.textures[i], device, name)) {
            uploadDummy(s.textures[i], device, "");
        }
    }
    return true;
}

void modelLabel(const SceneFrameImpl& scene, uint32_t mat, char* dst, uint32_t cap) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (scene.materialSrc != nullptr && mat < scene.materialCount && scene.texNames != nullptr) {
        const uint32_t tex = scene.materialSrc[mat].albedoTexIndex;
        if (tex > 0u && tex < scene.texNameCount) {
            const char* file = scene.texNames[tex].file;
            strPathStem(file, dst, cap);
        }
    }
    if (dst[0] == '\0') {
        std::snprintf(dst, cap, "Mesh %u", mat);
    }
}

void syncPropMaterial(SceneFrame& scene) {
    auto* gpu = scene.materialsBuf.mapped != nullptr ? static_cast<MaterialGpuData*>(scene.materialsBuf.mapped) : nullptr;
    auto store = [](MaterialGpuData& dst, const Material& src) {
        if (dst.baseColor[0] == src.base[0] && dst.baseColor[1] == src.base[1] && dst.baseColor[2] == src.base[2]
            && dst.roughness == src.roughness && dst.metallic == src.metallic && dst.emissiveIntensity == src.emissive) {
            return false;
        }
        dst.baseColor[0] = src.base[0];
        dst.baseColor[1] = src.base[1];
        dst.baseColor[2] = src.base[2];
        dst.roughness = src.roughness;
        dst.metallic = src.metallic;
        dst.emissiveIntensity = src.emissive;
        return true;
    };
    for (uint8_t n = 0; n < scene.propCount; ++n) {
        SceneFrameImpl::PropLink& link = scene.props[n];
        if (link.actor >= scene.actors.count || link.material >= scene.materialCount) {
            continue;
        }
        flecs::entity ent = scene.actors.ent[link.actor];
        if (!ent.is_alive() || !ent.has<Material>()) {
            continue;
        }
        const Material& pbr = ent.get<Material>();
        bool dirty = false;
        if (MaterialGpuData* cpu = materialAt(scene, link.material)) {
            if (store(*cpu, pbr)) {
                dirty = true;
            }
        }
        if (gpu != nullptr && store(gpu[link.material], pbr)) {
            dirty = true;
        }
        if (dirty) {
            scene.editorDirty = static_cast<uint8_t>(scene.editorDirty | 1u);
        }
    }
    const uint8_t actors = scene.actors.count;
    for (uint8_t n = 0; n < actors; ++n) {
        flecs::entity ent = scene.actors.ent[n];
        if (!ent.is_alive() || !ent.has<MeshBind>() || !ent.has<Material>()) {
            continue;
        }
        const MeshBind bind = ent.get<MeshBind>();
        if (bind.material >= scene.materialCount) {
            continue;
        }
        const Material& pbr = ent.get<Material>();
        bool dirty = false;
        if (MaterialGpuData* cpu = materialAt(scene, bind.material)) {
            if (store(*cpu, pbr)) {
                dirty = true;
            }
        }
        if (gpu != nullptr && store(gpu[bind.material], pbr)) {
            dirty = true;
        }
        if (dirty) {
            scene.editorDirty = static_cast<uint8_t>(scene.editorDirty | 1u);
        }
    }
}

bool ensureOwnedMaterials(SceneFrameImpl& s) {
    if (s.materialCount == 0 || s.materialSrc == nullptr) {
        return false;
    }
    if (!s.ownedMat.empty() && s.materialSrc == s.ownedMat.data()) {
        return true;
    }
    s.ownedMat.assign(s.materialSrc, s.materialSrc + s.materialCount);
    s.ownedMat.reserve(s.materialCount + kEditorSpare);
    s.materialSrc = s.ownedMat.data();
    if (s.materialCap < s.materialCount + kEditorSpare) {
        s.materialCap = s.materialCount + kEditorSpare;
    }
    return true;
}

bool sceneFrameCloneMaterial(SceneFrame* scene, uint8_t actor) {
    if (scene == nullptr || actor >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[actor];
    if (!ent.is_alive() || !ent.has<MeshBind>()) {
        spdlog::error("clone material: actor {} has no mesh", actor);
        return false;
    }
    if (!ensureOwnedMaterials(*scene) || scene->materialCount >= scene->materialCap) {
        spdlog::error("clone material: no spare slot {} / {}", scene->materialCount, scene->materialCap);
        return false;
    }
    const MeshBind bind = ent.get<MeshBind>();
    if (bind.material >= scene->materialCount) {
        return false;
    }
    const uint32_t id = scene->materialCount;
    scene->ownedMat.push_back(scene->ownedMat[bind.material]);
    scene->materialSrc = scene->ownedMat.data();
    scene->materialCount = id + 1u;
    if (scene->materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene->materialsBuf.mapped)[id] = scene->ownedMat[id];
    }
    if (!ensureOwnedInstances(*scene)) {
        return false;
    }
    MeshSpan span{};
    span.begin = bind.instance;
    span.count = 1;
    if (ent.has<MeshSpan>()) {
        span = ent.get<MeshSpan>();
    }
    auto* gpu = scene->instances.mapped != nullptr ? static_cast<GpuInstance*>(scene->instances.mapped) : nullptr;
    const uint32_t end = span.begin + span.count;
    for (uint32_t i = span.begin; i < end && i < scene->instanceCount; ++i) {
        scene->ownedInst[i].materialId = id;
        if (gpu != nullptr) {
            gpu[i].materialId = id;
        }
    }
    MeshBind next = bind;
    next.material = id;
    ent.set<MeshBind>(next);
    scene->editorDirty = static_cast<uint8_t>(scene->editorDirty | 1u);
    spdlog::info("material {} cloned from {} for actor {}", id, bind.material, actor);
    return true;
}

void sceneFrameQueueTexture(SceneFrame* scene, uint8_t actor, uint8_t channel, const char* path) {
    if (scene == nullptr || path == nullptr || path[0] == '\0' || channel > 7u || scene->texJobN >= 8u) {
        return;
    }
    SceneFrameImpl::TexJob& job = scene->texJob[scene->texJobN];
    std::snprintf(job.path, sizeof(job.path), "%s", path);
    job.actor = actor;
    job.channel = channel;
    scene->texJobN = static_cast<uint8_t>(scene->texJobN + 1u);
    scene->texPath[0] = '\0';
    scene->texChannel = 0xff;
    spdlog::info("texture queued ch {} actor {} {}", channel, actor, path);
}

bool sceneFrameApplyDoc(SceneFrame* scene, uint8_t actor, const BhmatDoc& doc) {
    if (scene == nullptr || actor >= scene->actors.count || !ensureOwnedMaterials(*scene)) {
        return false;
    }
    const uint32_t id = actorMaterialSlot(*scene, actor);
    if (id >= scene->materialCount || id >= scene->ownedMat.size()) {
        return false;
    }
    BhmatDoc previous{};
    flecs::entity ent = scene->actors.ent[actor];
    if (ent.is_alive() && ent.has<ActorMat>()) {
        previous = ent.get<ActorMat>().doc;
    }
    MaterialGpuData& mat = scene->ownedMat[id];
    materialGpuFromDoc(mat, doc);
    if (scene->materialsBuf.mapped != nullptr) {
        static_cast<MaterialGpuData*>(scene->materialsBuf.mapped)[id] = mat;
    }
    if (ent.is_alive()) {
        ActorMat stored{};
        stored.doc = doc;
        ent.set<ActorMat>(stored);
        Material pbr{};
        pbr.base[0] = doc.base[0];
        pbr.base[1] = doc.base[1];
        pbr.base[2] = doc.base[2];
        pbr.base[3] = doc.baseA;
        pbr.roughness = doc.roughness;
        pbr.metallic = doc.metallic;
        pbr.emissive = doc.emissiveGain > 0.0f ? doc.emissiveGain : doc.emissive;
        pbr.reflectance = doc.reflectance;
        ent.set<Material>(pbr);
    }
    queueDocMap(*scene, actor, 0, doc.albedoPath, previous.albedoPath);
    queueDocMap(*scene, actor, 1, doc.normalPath, previous.normalPath);
    queueDocMap(*scene, actor, 2, doc.ormPath, previous.ormPath);
    queueDocMap(*scene, actor, 3, doc.emissivePath, previous.emissivePath);
    if (doc.ormPath[0] == '\0') {
        queueDocMap(*scene, actor, 4, doc.roughPath, previous.roughPath);
        queueDocMap(*scene, actor, 5, doc.metalPath, previous.metalPath);
        queueDocMap(*scene, actor, 6, doc.aoPath, previous.aoPath);
        queueDocMap(*scene, actor, 7, doc.heightPath, previous.heightPath);
    } else {
        queueDocMap(*scene, actor, 4, "", previous.roughPath[0] != '\0' ? previous.roughPath : "x");
        queueDocMap(*scene, actor, 5, "", previous.metalPath[0] != '\0' ? previous.metalPath : "x");
        queueDocMap(*scene, actor, 6, "", previous.aoPath[0] != '\0' ? previous.aoPath : "x");
        queueDocMap(*scene, actor, 7, "", previous.heightPath[0] != '\0' ? previous.heightPath : "x");
    }
    queueDocMap(*scene, actor, 8, doc.alphaPath, previous.alphaPath);
    scene->editorDirty = static_cast<uint8_t>(scene->editorDirty | 1u);
    return true;
}

bool sceneFrameReadDoc(const SceneFrame* scene, uint8_t actor, BhmatDoc& doc) {
    doc = {};
    if (scene == nullptr || actor >= scene->actors.count) {
        return false;
    }
    flecs::entity ent = scene->actors.ent[actor];
    if (ent.is_alive() && ent.has<ActorMat>()) {
        doc = ent.get<ActorMat>().doc;
        return true;
    }
    const uint32_t id = actorMaterialSlot(*scene, actor);
    if (scene->materialSrc == nullptr || id >= scene->materialCount) {
        return false;
    }
    materialDocFromGpu(doc, scene->materialSrc[id]);
    return true;
}

void takeQueuedTexture(SceneFrameImpl& s, Device& device) {
    if (s.texJobN == 0u) {
        if (s.texChannel > 7u || s.texPath[0] == '\0') {
            return;
        }
        if (s.texJobN < 8u) {
            SceneFrameImpl::TexJob& job = s.texJob[s.texJobN];
            std::snprintf(job.path, sizeof(job.path), "%s", s.texPath);
            job.actor = s.texActor;
            job.channel = s.texChannel;
            s.texJobN = static_cast<uint8_t>(s.texJobN + 1u);
        }
        s.texPath[0] = '\0';
        s.texChannel = 0xff;
    }
    while (s.texJobN > 0u && s.ready) {
        SceneFrameImpl::TexJob job = s.texJob[0];
        for (uint8_t i = 1; i < s.texJobN; ++i) {
            s.texJob[i - 1u] = s.texJob[i];
        }
        s.texJobN = static_cast<uint8_t>(s.texJobN - 1u);
        s.texPath[0] = '\0';
        s.texChannel = 0xff;
        if (job.channel > 7u || job.path[0] == '\0') {
            continue;
        }
        if (job.actor >= s.actors.count || s.textureSlots >= 768u) {
            spdlog::error("texture rejected slots {} actor {}", s.textureSlots, job.actor);
            continue;
        }
        flecs::entity ent = s.actors.ent[job.actor];
        uint32_t matId = 0xffffffffu;
        if (ent.is_alive() && ent.has<MeshBind>()) {
            matId = ent.get<MeshBind>().material;
        }
        if (matId == 0xffffffffu) {
            for (uint8_t n = 0; n < s.propCount; ++n) {
                if (s.props[n].actor == job.actor) {
                    matId = s.props[n].material;
                    break;
                }
            }
        }
        if (!ent.is_alive() || matId == 0xffffffffu || !ensureOwnedMaterials(s)) {
            spdlog::error("texture rejected, actor {} has no material", job.actor);
            continue;
        }
        const bool color = job.channel == 0u || job.channel == 3u;
        const PbrTex kind = color ? PbrTex::Color : (job.channel == 1u ? PbrTex::Normal : PbrTex::Data);
        const bool dds = strExtIs(job.path, ".dds");
        GpuImage img{};
        bool loaded = dds && imageLoadDds(img, device, job.path, color);
        if (!loaded) {
            char cache[512]{};
            ddsCachePath(cache, sizeof(cache), job.path);
            if (kind == PbrTex::Normal) {
                const size_t n = std::strlen(cache);
                if (n >= 4 && n + 5 < sizeof(cache) && std::strcmp(cache + n - 4, ".dds") == 0) {
                    std::memcpy(cache + n - 4, ".bc5.dds", 9);
                }
            }
            loaded = !dds && imageLoadDds(img, device, cache, color);
            if (loaded && kind != PbrTex::Color && img.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
                gpuImageDestroy(img, device);
                loaded = false;
            }
            if (!loaded) {
                ImageCpu cpu{};
                if (imageLoadFile(cpu, job.path)) {
                    if (cpu.hdr != 0 && cpu.rgb != nullptr && color) {
                        loaded = gpuImageUploadHdr(img, device, cpu.rgb, cpu.width, cpu.height);
                    } else if (cpu.rgba != nullptr) {
                        loaded = gpuImageUploadPbr(img, device, cpu.rgba, cpu.width, cpu.height, kind, cache);
                    }
                }
                imageCpuFree(cpu);
            }
        }
        if (!loaded || img.image == VK_NULL_HANDLE) {
            spdlog::error("texture load failed {}", job.path);
            continue;
        }
        const uint32_t slot = s.textureSlots;
        s.textures.push_back(img);
        s.textureSlots = slot + 1u;
        if (!heapImage(s.gpuHeap, device, s, static_cast<uint32_t>(HeapImg::Textures) + slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                s.textures[slot], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT)) {
            spdlog::error("texture bind failed slot {}", slot);
            continue;
        }
        for (uint32_t flight = 0; flight < kFramesInFlight; ++flight) {
            gpuBufferFlush(s.gpuHeap.resources[flight], device, 0, s.gpuHeap.resources[flight].size);
        }
        if (s.texPublish == 0 || slot < s.texPublishBase) {
            s.texPublishBase = slot;
        }
        s.texPublish = 2;
        if (matId >= s.materialCount) {
            continue;
        }
        MaterialGpuData& mat = s.ownedMat[matId];
        if (uint32_t* texSlot = materialTexSlot(mat, job.channel)) {
            *texSlot = slot;
        }
        if (job.channel == 2u) {
            mat.flags |= kMatOrm;
        }
        if (s.materialsBuf.mapped != nullptr) {
            static_cast<MaterialGpuData*>(s.materialsBuf.mapped)[matId] = mat;
            gpuBufferFlush(s.materialsBuf, device, 0, sizeof(MaterialGpuData) * s.materialCount);
        }
        s.editorDirty = static_cast<uint8_t>(s.editorDirty | 1u);
        spdlog::info("texture {} ch {} mat {} slot {} {}x{} mips {} fmt {}",
            job.path, job.channel, matId, slot, img.extent.width, img.extent.height, img.mips, static_cast<int>(img.format));
    }
}

} // namespace burnhope
