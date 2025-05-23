#pragma once

#include "lve_buffer.hpp"
#include "lve_device.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// std
#include <memory>
#include <vector>

namespace burnhope {
class BurnhopeModel {
 public:
  struct Vertex {
    glm::vec3 position{};
    glm::vec3 color{};
    glm::vec3 normal{};
    glm::vec3 tangent{};
    glm::vec3 bitangent{};
    glm::vec2 uv{};

    static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

    bool operator==(const Vertex &other) const {
      return position == other.position && color == other.color && normal == other.normal &&
             uv == other.uv;
    }
  };

  struct Builder {
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    void loadModel(const std::string &filepath);
  };

  BurnhopeModel(BurnhopeDevice &device, const BurnhopeModel::Builder &builder);
  ~BurnhopeModel();

  BurnhopeModel(const BurnhopeModel &) = delete;
  BurnhopeModel &operator=(const BurnhopeModel &) = delete;

  static std::unique_ptr<BurnhopeModel> createModelFromFile(
      BurnhopeDevice &device, const std::string &filepath);

  void bind(VkCommandBuffer commandBuffer);
  void draw(VkCommandBuffer commandBuffer);

 private:
  void createVertexBuffers(const std::vector<Vertex> &vertices);
  void createIndexBuffers(const std::vector<uint32_t> &indices);

  BurnhopeDevice &lveDevice;

  std::unique_ptr<BurnhopeBuffer> vertexBuffer;
  uint32_t vertexCount;

  bool hasIndexBuffer = false;
  std::unique_ptr<BurnhopeBuffer> indexBuffer;
  uint32_t indexCount;
};
}  // namespace burnhope
