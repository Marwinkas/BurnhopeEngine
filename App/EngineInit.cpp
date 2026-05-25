#include "Engine.hpp"
#include "../Render/Material.hpp"
#include "../Render/Model.hpp"
#include "../Render/SceneManager.hpp"
#include "../Utils/Components.hpp"
#include <filesystem>
#include <iostream>
#include <random>

namespace burnhope {

namespace {

uint64_t randomEntityId() {
  static std::mt19937_64 rng{std::random_device{}()};
  return rng();
}

void spawnDefaultTestMesh(BurnhopeDevice& device, flecs::world& world,
  const std::shared_ptr<Material>& defaultMaterial) {
static constexpr const char* kMeshPaths[] = {
"models/cube.bhmesh",
"../models/cube.bhmesh",
"models/cube.obj",
};
for (const char* path : kMeshPaths) {
if (!std::filesystem::exists(path)) {
continue;
}
flecs::entity ent = world.entity();
ent.set<IDComponent>({randomEntityId()});
ent.set<TagComponent>({std::string{"Cube"}});

TransformComponent tc{};
tc.transform.position = float3{0.0f, 0.0f, 0.0f};
tc.transform.rotation = float3{0.0f, 0.0f, 0.0f};
tc.transform.scale = float3{1.0f, 1.0f, 1.0f};
tc.transform.updatematrix = true;
tc.transform.updateMatrixIfNeeded();
ent.set<TransformComponent>(tc);

MeshComponent mc{};
mc.modelPath = path;
mc.isVisible = true;

try {
mc.model = BurnhopeModel::createModelFromFile(device, path);
// Model loads on a background thread — subMeshes are empty here; rebuildBatches uses
// defaultWhiteMaterial when materials[i] is missing (see EngineScene.cpp).
if (defaultMaterial) {
  mc.materials = {defaultMaterial};
}
ent.set<MeshComponent>(mc);
std::cerr << "[Minimal] default mesh entity: " << path << '\n';
return;
} catch (const std::exception& e) {
std::cerr << "[ERROR] default mesh " << path << ": " << e.what() << '\n';
}
}
std::cerr << "[WARN] no default test mesh (expected models/cube.bhmesh)\n";
}

} // namespace

void Engine::loadGameObjects(flecs::world& world) {
  namespace fs = std::filesystem;
  const char* candidates[] = {"level_1.json", "scenes/level_1.json", "../level_1.json"};
  for (const char* path : candidates) {
    if (fs::exists(path)) {
      SceneManager::loadScene(device_, bindless_.textures(), bindless_, world, path);
      return;
    }
  }
  spawnDefaultTestMesh(device_, world, defaultWhiteMaterial);
}

} // namespace burnhope
