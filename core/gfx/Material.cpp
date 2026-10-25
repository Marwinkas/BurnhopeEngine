#include "gfx/Material.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

void quote(std::FILE* out, const char* text) {
    std::fputc('"', out);
    if (text == nullptr) {
        std::fputc('"', out);
        return;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            std::fputc('\\', out);
        }
        if (*p != '\n' && *p != '\r') {
            std::fputc(*p, out);
        }
    }
    std::fputc('"', out);
}

void putNum(std::FILE* out, const char* key, double value) {
    std::fprintf(out, ",\"%s\":%.6g", key, value);
}

bool keyIs(const char* key, const char* want) {
    return std::strcmp(key, want) == 0;
}

void takeNum(BhmatDoc& doc, const char* key, double value) {
    const float n = static_cast<float>(value);
    if (keyIs(key, "version")) doc.version = static_cast<uint32_t>(value);
    else if (keyIs(key, "r")) doc.base[0] = n;
    else if (keyIs(key, "g")) doc.base[1] = n;
    else if (keyIs(key, "b")) doc.base[2] = n;
    else if (keyIs(key, "a")) doc.baseA = n;
    else if (keyIs(key, "rough")) doc.roughness = n;
    else if (keyIs(key, "metal")) doc.metallic = n;
    else if (keyIs(key, "emit")) doc.emissive = n;
    else if (keyIs(key, "albedoGain")) doc.albedoGain = n;
    else if (keyIs(key, "normalGain")) doc.normalGain = n;
    else if (keyIs(key, "ormGain")) doc.ormGain = n;
    else if (keyIs(key, "emissiveGain")) doc.emissiveGain = n;
    else if (keyIs(key, "reflect")) doc.reflectance = n;
    else if (keyIs(key, "ao")) doc.aoStrength = n;
    else if (keyIs(key, "nScale")) doc.normalScale = n;
    else if (keyIs(key, "coat")) doc.clearcoat = n;
    else if (keyIs(key, "coatRough")) doc.clearcoatRoughness = n;
    else if (keyIs(key, "cutoff")) doc.alphaCutoff = n;
    else if (keyIs(key, "tileX")) doc.uvTiling[0] = n;
    else if (keyIs(key, "tileY")) doc.uvTiling[1] = n;
    else if (keyIs(key, "offX")) doc.uvOffset[0] = n;
    else if (keyIs(key, "offY")) doc.uvOffset[1] = n;
    else if (keyIs(key, "rot")) doc.uvRotation = n;
    else if (keyIs(key, "grid")) doc.worldGridSize = n;
    else if (keyIs(key, "sharp")) doc.triplanarSharpness = n;
    else if (keyIs(key, "wox")) doc.worldOffset[0] = n;
    else if (keyIs(key, "woy")) doc.worldOffset[1] = n;
    else if (keyIs(key, "woz")) doc.worldOffset[2] = n;
    else if (keyIs(key, "er")) doc.emissiveColor[0] = n;
    else if (keyIs(key, "eg")) doc.emissiveColor[1] = n;
    else if (keyIs(key, "eb")) doc.emissiveColor[2] = n;
    else if (keyIs(key, "eGain")) doc.emissiveGain = n;
    else if (keyIs(key, "world")) doc.worldAligned = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "twoSide")) doc.doubleSided = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "cutout")) doc.alphaCut = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "flipY")) doc.flipNormalY = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "unlit")) doc.unlit = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "repeat")) doc.repeat = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "pack")) doc.mapPack = n > 0.5 ? 1u : 0u;
    else if (keyIs(key, "hScale")) doc.heightScale = n;
}

void takeStr(BhmatDoc& doc, const char* key, const char* text) {
    char* dst = nullptr;
    if (keyIs(key, "albedo")) dst = doc.albedoPath;
    else if (keyIs(key, "normal")) dst = doc.normalPath;
    else if (keyIs(key, "orm")) dst = doc.ormPath;
    else if (keyIs(key, "emissive")) dst = doc.emissivePath;
    else if (keyIs(key, "roughPath")) dst = doc.roughPath;
    else if (keyIs(key, "metalPath")) dst = doc.metalPath;
    else if (keyIs(key, "aoPath")) dst = doc.aoPath;
    else if (keyIs(key, "height")) dst = doc.heightPath;
    else if (keyIs(key, "alpha")) dst = doc.alphaPath;
    if (dst == nullptr) {
        return;
    }
    std::snprintf(dst, 260, "%s", text != nullptr ? text : "");
}

} // namespace

bool materialLoadBhmat(const char* path, BhmatDoc& doc) {
    doc = {};
    doc.version = 1;
    doc.base[0] = doc.base[1] = doc.base[2] = doc.baseA = 1.0f;
    doc.roughness = 0.5f;
    doc.reflectance = 0.5f;
    doc.aoStrength = doc.albedoGain = doc.normalGain = doc.ormGain = 1.0f;
    doc.emissiveGain = 0.0f;
    doc.normalScale = doc.worldGridSize = doc.uvTiling[0] = doc.uvTiling[1] = 1.0f;
    doc.triplanarSharpness = 4.0f;
    doc.clearcoatRoughness = 0.04f;
    doc.alphaCutoff = 0.5f;
    doc.emissiveColor[0] = doc.emissiveColor[1] = doc.emissiveColor[2] = 1.0f;
    doc.flipNormalY = 1;
    doc.repeat = 1;
    doc.mapPack = 255;
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped) || mapped.data == nullptr || mapped.size == 0) {
        return false;
    }
    const char* begin = reinterpret_cast<const char*>(mapped.data);
    const char* end = begin + mapped.size;
    if (mapped.size >= 3 && static_cast<unsigned char>(begin[0]) == 0xEFu) {
        begin += 3;
    }
    json::Cur c{begin, end};
    if (!json::eat(c, '{')) {
        fileUnmap(&mapped);
        return false;
    }
    while (c.p < c.end) {
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            break;
        }
        char key[48]{};
        uint32_t keyLen = 0;
        if (!json::parseStringCap(c, key, sizeof(key), keyLen) || !json::eat(c, ':')) {
            fileUnmap(&mapped);
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '"') {
            char text[260]{};
            uint32_t n = 0;
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                fileUnmap(&mapped);
                return false;
            }
            takeStr(doc, key, text);
        } else {
            double number = 0.0;
            if (json::parseNumber(c, number)) {
                takeNum(doc, key, number);
            } else {
                json::skipValue(c);
            }
        }
        json::eat(c, ',');
    }
    if (doc.mapPack > 1u) {
        const bool split = doc.metalPath[0] != '\0' || doc.roughPath[0] != '\0' || doc.aoPath[0] != '\0' || doc.heightPath[0] != '\0';
        doc.mapPack = (!split && doc.ormPath[0] != '\0') ? 1u : 0u;
    }
    if (doc.mapPack == 0u) {
        doc.ormPath[0] = '\0';
    }
    fileUnmap(&mapped);
    return true;
}

bool materialSaveBhmat(const char* path, const BhmatDoc& doc) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::FILE* out = std::fopen(path, "w");
    if (out == nullptr) {
        return false;
    }
    std::fprintf(out, "{\"version\":%u", doc.version == 0 ? 1u : doc.version);
    putNum(out, "r", doc.base[0]);
    putNum(out, "g", doc.base[1]);
    putNum(out, "b", doc.base[2]);
    putNum(out, "a", doc.baseA);
    putNum(out, "rough", doc.roughness);
    putNum(out, "metal", doc.metallic);
    putNum(out, "emit", doc.emissive);
    putNum(out, "albedoGain", doc.albedoGain);
    putNum(out, "normalGain", doc.normalGain);
    putNum(out, "ormGain", doc.ormGain);
    putNum(out, "emissiveGain", doc.emissiveGain);
    putNum(out, "reflect", doc.reflectance);
    putNum(out, "ao", doc.aoStrength);
    putNum(out, "nScale", doc.normalScale);
    putNum(out, "coat", doc.clearcoat);
    putNum(out, "coatRough", doc.clearcoatRoughness);
    putNum(out, "cutoff", doc.alphaCutoff);
    putNum(out, "tileX", doc.uvTiling[0]);
    putNum(out, "tileY", doc.uvTiling[1]);
    putNum(out, "offX", doc.uvOffset[0]);
    putNum(out, "offY", doc.uvOffset[1]);
    putNum(out, "rot", doc.uvRotation);
    putNum(out, "grid", doc.worldGridSize);
    putNum(out, "sharp", doc.triplanarSharpness);
    putNum(out, "wox", doc.worldOffset[0]);
    putNum(out, "woy", doc.worldOffset[1]);
    putNum(out, "woz", doc.worldOffset[2]);
    putNum(out, "er", doc.emissiveColor[0]);
    putNum(out, "eg", doc.emissiveColor[1]);
    putNum(out, "eb", doc.emissiveColor[2]);
    putNum(out, "eGain", doc.emissiveGain);
    putNum(out, "world", doc.worldAligned);
    putNum(out, "twoSide", doc.doubleSided);
    putNum(out, "cutout", doc.alphaCut);
    putNum(out, "flipY", doc.flipNormalY);
    putNum(out, "unlit", doc.unlit);
    putNum(out, "repeat", doc.repeat);
    putNum(out, "pack", doc.mapPack);
    putNum(out, "hScale", doc.heightScale);
    std::fputs(",\"albedo\":", out);
    quote(out, doc.albedoPath);
    std::fputs(",\"normal\":", out);
    quote(out, doc.normalPath);
    std::fputs(",\"orm\":", out);
    quote(out, doc.ormPath);
    std::fputs(",\"emissive\":", out);
    quote(out, doc.emissivePath);
    std::fputs(",\"roughPath\":", out);
    quote(out, doc.roughPath);
    std::fputs(",\"metalPath\":", out);
    quote(out, doc.metalPath);
    std::fputs(",\"aoPath\":", out);
    quote(out, doc.aoPath);
    std::fputs(",\"height\":", out);
    quote(out, doc.heightPath);
    std::fputs(",\"alpha\":", out);
    quote(out, doc.alphaPath);
    std::fputs("}\n", out);
    std::fclose(out);
    return true;
}

} // namespace burnhope
