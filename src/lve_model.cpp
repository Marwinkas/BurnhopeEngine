#include "lve_model.hpp"

#include "lve_utils.hpp"

// libs
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// std
#include <cassert>
#include <cstring>
#include <unordered_map>

#ifndef ENGINE_DIR
#define ENGINE_DIR "../"
#endif

namespace std {
template <>
struct hash<burnhope::BurnhopeModel::Vertex> {
  size_t operator()(burnhope::BurnhopeModel::Vertex const &vertex) const {
    size_t seed = 0;
    burnhope::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
    return seed;
  }
};
}  // namespace std

namespace burnhope {

BurnhopeModel::BurnhopeModel(BurnhopeDevice &device, const BurnhopeModel::Builder &builder) : lveDevice{device} {
  createVertexBuffers(builder.vertices);
  createIndexBuffers(builder.indices);
}

BurnhopeModel::~BurnhopeModel() {}

std::unique_ptr<BurnhopeModel> BurnhopeModel::createModelFromFile(
    BurnhopeDevice &device, const std::string &filepath) {
  Builder builder{};
  builder.loadModel(ENGINE_DIR + filepath);
  return std::make_unique<BurnhopeModel>(device, builder);
}

void BurnhopeModel::createVertexBuffers(const std::vector<Vertex> &vertices) {
  vertexCount = static_cast<uint32_t>(vertices.size());
  assert(vertexCount >= 3 && "Vertex count must be at least 3");
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
  uint32_t vertexSize = sizeof(vertices[0]);

  BurnhopeBuffer stagingBuffer{
      lveDevice,
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)vertices.data());

  vertexBuffer = std::make_unique<BurnhopeBuffer>(
      lveDevice,
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  lveDevice.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
}

void BurnhopeModel::createIndexBuffers(const std::vector<uint32_t> &indices) {
  indexCount = static_cast<uint32_t>(indices.size());
  hasIndexBuffer = indexCount > 0;

  if (!hasIndexBuffer) {
    return;
  }

  VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
  uint32_t indexSize = sizeof(indices[0]);

  BurnhopeBuffer stagingBuffer{
      lveDevice,
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)indices.data());

  indexBuffer = std::make_unique<BurnhopeBuffer>(
      lveDevice,
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  lveDevice.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
}

void BurnhopeModel::draw(VkCommandBuffer commandBuffer) {
  if (hasIndexBuffer) {
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
  } else {
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
  }
}

void BurnhopeModel::bind(VkCommandBuffer commandBuffer) {
  VkBuffer buffers[] = {vertexBuffer->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

  if (hasIndexBuffer) {
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  }
}

std::vector<VkVertexInputBindingDescription> BurnhopeModel::Vertex::getBindingDescriptions() {
  std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(Vertex);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> BurnhopeModel::Vertex::getAttributeDescriptions() {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

  attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
  attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
  attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
  attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
  attributeDescriptions.push_back({4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, tangent)});
  attributeDescriptions.push_back({5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, bitangent)});

  return attributeDescriptions;
}

void BurnhopeModel::Builder::loadModel(const std::string &filepath) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
    throw std::runtime_error(warn + err);
  }

  vertices.clear();
  indices.clear();

  std::unordered_map<Vertex, uint32_t> uniqueVertices{};
  for (const auto &shape : shapes) {
    for (size_t i = 0; i < shape.mesh.indices.size(); i += 3) {
      Vertex verticesTri[3];

      for (int j = 0; j < 3; ++j) {
        const auto &index = shape.mesh.indices[i + j];
        Vertex &v = verticesTri[j];

        if (index.vertex_index >= 0) {
          v.position = {
              attrib.vertices[3 * index.vertex_index + 0],
              attrib.vertices[3 * index.vertex_index + 1],
              attrib.vertices[3 * index.vertex_index + 2],
          };

          if (!attrib.colors.empty()) {
            v.color = {
                attrib.colors[3 * index.vertex_index + 0],
                attrib.colors[3 * index.vertex_index + 1],
                attrib.colors[3 * index.vertex_index + 2],
            };
          }
        }

        if (index.normal_index >= 0) {
          v.normal = {
              attrib.normals[3 * index.normal_index + 0],
              attrib.normals[3 * index.normal_index + 1],
              attrib.normals[3 * index.normal_index + 2],
          };
        }

        if (index.texcoord_index >= 0) {
          v.uv = {
              attrib.texcoords[2 * index.texcoord_index + 0],
              attrib.texcoords[2 * index.texcoord_index + 1],
          };
        }
      }

      // расчёт tangent/bitangent
      glm::vec3 edge1 = verticesTri[1].position - verticesTri[0].position;
      glm::vec3 edge2 = verticesTri[2].position - verticesTri[0].position;

      glm::vec2 deltaUV1 = verticesTri[1].uv - verticesTri[0].uv;
      glm::vec2 deltaUV2 = verticesTri[2].uv - verticesTri[0].uv;

      float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

      glm::vec3 tangent{
          f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
          f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
          f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)};
      glm::vec3 bitangent{
          f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x),
          f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y),
          f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z)};

      for (int j = 0; j < 3; ++j) {
        Vertex &v = verticesTri[j];
        if (uniqueVertices.count(v) == 0) {
          v.tangent = tangent;
          v.bitangent = bitangent;
          uniqueVertices[v] = static_cast<uint32_t>(vertices.size());
          vertices.push_back(v);
        }
        indices.push_back(uniqueVertices[v]);
      }
    }
  }
}

}  // namespace burnhope
