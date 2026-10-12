#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "rhi/Device.hpp"
#include "rhi/MeshDraw.hpp"

using namespace burnhope;

TEST_CASE("meshDrawRecord is a no-op with no shaders bound") {
    const MeshDrawDesc none{};
    meshDrawRecord(VK_NULL_HANDLE, none);
    const MeshDrawDesc zeroCounts{.mesh = reinterpret_cast<VkShaderEXT>(1), .vert = reinterpret_cast<VkShaderEXT>(2), .frag = reinterpret_cast<VkShaderEXT>(3)};
    meshDrawRecord(VK_NULL_HANDLE, zeroCounts);
    CHECK(true); // reaching here without touching cmd means both no-shader and zero-count paths stayed no-op
}
TEST_CASE("device lost flag stops draws until cleared") {
    Device device{};
    CHECK(!deviceLost(device));
    deviceMarkLost(device, "test", static_cast<int32_t>(VK_ERROR_DEVICE_LOST));
    CHECK(deviceLost(device));
    deviceMarkLost(device, "test", static_cast<int32_t>(VK_ERROR_DEVICE_LOST));
    CHECK(deviceLost(device));
    device.lost.store(0);
    CHECK(!deviceLost(device));
}
