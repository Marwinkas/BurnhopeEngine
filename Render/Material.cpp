#include "Material.hpp"
#include "../Utils/BindlessRegistry.hpp"

namespace burnhope {

std::shared_ptr<Material> Material::loadFromJson(
    BurnhopeDevice& device,
    TexturePool& pool,
    BindlessRegistry& bindless,
    const std::string& filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return nullptr;
  }

  json j;
  try {
    file >> j;
  } catch (...) {
    return nullptr;
  }
  if (!j.is_object()) {
    return nullptr;
  }

  auto mat = std::make_shared<Material>();

  if (j.contains("albedoColor")) {
    if (j["albedoColor"].size() == 4) {
      mat->albedoColor = float4{j["albedoColor"][0], j["albedoColor"][1], j["albedoColor"][2],
                                j["albedoColor"][3]};
    } else {
      mat->albedoColor =
          float4{j["albedoColor"][0], j["albedoColor"][1], j["albedoColor"][2], 1.0f};
    }
  }
  if (j.contains("emissiveColor")) {
    mat->emissiveColor = float3{j["emissiveColor"][0], j["emissiveColor"][1], j["emissiveColor"][2]};
  }
  if (j.contains("metallicStrength")) {
    mat->metallicStrength = j["metallicStrength"];
  }
  if (j.contains("roughnessStrength")) {
    mat->roughnessStrength = j["roughnessStrength"];
  }
  if (j.contains("normalStrength")) {
    mat->normalStrength = j["normalStrength"];
  }
  if (j.contains("heightStrength")) {
    mat->heightStrength = j["heightStrength"];
  }
  if (j.contains("aoStrength")) {
    mat->aoStrength = j["aoStrength"];
  } else if (j.contains("aoFactor")) {
    mat->aoStrength = j["aoFactor"];
  }
  if (j.contains("repeatTexture")) {
    mat->repeatTexture = j["repeatTexture"];
  }
  if (j.contains("useTriplanar")) {
    mat->useTriplanar = j["useTriplanar"];
  }
  if (j.contains("triplanarScale")) {
    mat->triplanarScale = j["triplanarScale"];
  }
  if (j.contains("uvScale")) {
    mat->uvScale = float2{j["uvScale"][0], j["uvScale"][1]};
  }
  if (j.contains("emissiveIntensity")) {
    mat->emissiveIntensity = j["emissiveIntensity"];
  }
  if (j.contains("isTransparent")) {
    mat->isTransparent = j["isTransparent"];
  }

  const auto loadColor = [&](const char* key, auto setter) {
    const std::string path = j.value(key, "");
    if (!path.empty()) {
      try {
        setter(path);
      } catch (const std::exception& e) {
        std::cerr << "[Material] " << key << " load failed: " << e.what() << "\n";
      }
    }
  };

  loadColor("albedoPath", [&](const std::string& p) {
    mat->setAlbedo(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, true), p);
  });
  loadColor("normalPath", [&](const std::string& p) {
    mat->setNormal(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("roughnessPath", [&](const std::string& p) {
    mat->setRoughness(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("metallicPath", [&](const std::string& p) {
    mat->setMetallic(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("aoPath", [&](const std::string& p) {
    mat->setAO(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("heightPath", [&](const std::string& p) {
    mat->setHeight(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("emissivePath", [&](const std::string& p) {
    mat->setEmissive(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, true), p);
  });
  loadColor("ormPath", [&](const std::string& p) {
    mat->setORM(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("alphaPath", [&](const std::string& p) {
    mat->setAlpha(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("lightMaskPath", [&](const std::string& p) {
    mat->setLightMask(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });
  loadColor("rimMaskPath", [&](const std::string& p) {
    mat->setRimMask(pool, bindless, std::make_unique<BurnhopeTexture>(device, p, false), p);
  });

  mat->packTexturesAsync();
  return mat;
}

} // namespace burnhope
