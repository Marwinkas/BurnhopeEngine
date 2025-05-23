#pragma once
#include "lve_texture.hpp"

// libs
#include <glm/gtc/matrix_transform.hpp>

// std
#include <memory>
#include <unordered_map>
namespace burnhope {
	class Material {
	 public:
	  std::shared_ptr<BurnhopeTexture> diffuseMap = nullptr;
	  std::shared_ptr<BurnhopeTexture> normalMap = nullptr;
	  std::shared_ptr<BurnhopeTexture> AOMap = nullptr;
	  std::shared_ptr<BurnhopeTexture> RoughnessMap = nullptr;
	  std::shared_ptr<BurnhopeTexture> MetallicMap = nullptr;
	};
}  // namespace burnhope