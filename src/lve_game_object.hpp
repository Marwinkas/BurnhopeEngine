#pragma once
#include "Material.hpp"
#include "lve_model.hpp"
#include "lve_swap_chain.hpp"
#include "lve_texture.hpp"

// libs
#include <glm/gtc/matrix_transform.hpp>

// std
#include <memory>
#include <unordered_map>

namespace burnhope {

struct TransformComponent {
  glm::vec3 translation{};
  glm::vec3 scale{1.f, 1.f, 1.f};
  glm::vec3 rotation{};

  // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
  // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
  // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
  glm::mat4 mat4();

  glm::mat3 normalMatrix();
};

struct PointLightComponent {
  float lightIntensity = 1.0f;
};

struct GameObjectBufferData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

class BurnhopeGameObjectManager;  // forward declare game object manager class

class BurnhopeGameObject {
 public:
  using id_t = unsigned int;
  using Map = std::unordered_map<id_t, BurnhopeGameObject>;

  BurnhopeGameObject(BurnhopeGameObject &&) = default;
  BurnhopeGameObject(const BurnhopeGameObject &) = delete;
  BurnhopeGameObject &operator=(const BurnhopeGameObject &) = delete;
  BurnhopeGameObject &operator=(BurnhopeGameObject &&) = delete;

  id_t getId() { return id; }

  VkDescriptorBufferInfo getBufferInfo(int frameIndex);

  glm::vec3 color{};
  TransformComponent transform{};

  // Optional pointer components
  std::shared_ptr<BurnhopeModel> model{};
  std::shared_ptr<Material> material;
  

  std::unique_ptr<PointLightComponent> pointLight = nullptr;

 private:
  BurnhopeGameObject(id_t objId, const BurnhopeGameObjectManager &manager);

  id_t id;
  const BurnhopeGameObjectManager &gameObjectManager;

  friend class BurnhopeGameObjectManager;
};

class BurnhopeGameObjectManager {
 public:
  static constexpr int MAX_GAME_OBJECTS = 1000;

  BurnhopeGameObjectManager(BurnhopeDevice &device);
  BurnhopeGameObjectManager(const BurnhopeGameObjectManager &) = delete;
  BurnhopeGameObjectManager &operator=(const BurnhopeGameObjectManager &) = delete;
  BurnhopeGameObjectManager(BurnhopeGameObjectManager &&) = delete;
  BurnhopeGameObjectManager &operator=(BurnhopeGameObjectManager &&) = delete;

  BurnhopeGameObject &createGameObject() {
    assert(currentId < MAX_GAME_OBJECTS && "Max game object count exceeded!");
    auto gameObject = BurnhopeGameObject{currentId++, *this};
    auto gameObjectId = gameObject.getId();
    gameObject.material = std::make_shared<Material>();
    gameObject.material->diffuseMap = textureDefault;
    gameObjects.emplace(gameObjectId, std::move(gameObject));
    return gameObjects.at(gameObjectId);
  }

  BurnhopeGameObject &makePointLight(
      float intensity = 10.f, float radius = 0.1f, glm::vec3 color = glm::vec3(1.f));

  VkDescriptorBufferInfo getBufferInfoForGameObject(
      int frameIndex, BurnhopeGameObject::id_t gameObjectId) const {
    return uboBuffers[frameIndex]->descriptorInfoForIndex(gameObjectId);
  }

  void updateBuffer(int frameIndex);

  BurnhopeGameObject::Map gameObjects{};
  std::vector<std::unique_ptr<BurnhopeBuffer>> uboBuffers{BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT};

 private:
  BurnhopeGameObject::id_t currentId = 0;
  std::shared_ptr<BurnhopeTexture> textureDefault;
};

// template <typename T>
// class UboArraySystem {
//  public:
//   static constexpr int SIZE = 1000;

//   UboArraySystem(BurnhopeDevice &device, int length) {
//     // including nonCoherentAtomSize allows us to flush a specific index at once
//     int alignment = std::lcm(
//         device.properties.limits.nonCoherentAtomSize,
//         device.properties.limits.minUniformBufferOffsetAlignment);
//     for (int i = 0; i < uboBuffers.size(); i++) {
//       uboBuffers[i] = std::make_unique<BurnhopeBuffer>(
//           device,
//           sizeof(T),
//           length,
//           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
//           alignment);
//       uboBuffers[i]->map();
//     }
//   }

//   UboArraySystem(const UboArraySystem &) = delete;
//   UboArraySystem &operator=(const UboArraySystem &) = delete;
//   UboArraySystem(UboArraySystem &&) = delete;
//   UboArraySystem &operator=(UboArraySystem &&) = delete;

//   void updateBuffer(FrameInfo &frameInfo);  // query all with Transform component, write
//   VkDescriptorBufferInfo getDescriptorInfo(Ent::id_t entID) const;

//   // vector of pairs that we iterator over? or unordered map
//   // if we're always writing all entries than
//   std::unordered_map<Ent::id_t, int> entIndexMap{};
//   std::vector<std::unique_ptr<BurnhopeBuffer>> uboBuffers{BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT};

//  private:
// };

}  // namespace burnhope
