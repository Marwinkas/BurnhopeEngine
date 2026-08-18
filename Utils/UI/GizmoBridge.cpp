#include "GizmoBridge.hpp"
#include "../Window.hpp"
#include "../Device.hpp"
#ifndef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#endif
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl3.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <array>

namespace burnhope::ui {

GizmoBridge::GizmoBridge(burnhope::BurnhopeWindow& window, burnhope::BurnhopeDevice& device, VkFormat colorFormat)
    : m_Device(device) {
    InitImGui(window, device, colorFormat);
}

GizmoBridge::~GizmoBridge() {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    vkDeviceWaitIdle(m_Device.device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    if (m_DescriptorPool) vkDestroyDescriptorPool(m_Device.device(), m_DescriptorPool, nullptr);
}

void GizmoBridge::InitImGui(burnhope::BurnhopeWindow& window, burnhope::BurnhopeDevice& device, VkFormat colorFormat) {
    IMGUI_CHECKVERSION();
    m_ImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    ImGuizmo::Enable(true);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr; // headless: never persist layout/state to disk

    std::array<VkDescriptorPoolSize, 1> poolSizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8}}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 8;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &m_DescriptorPool);

    ImGui_ImplSDL3_InitForVulkan(window.getSDLWindow());
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = device.getInstance();
    initInfo.PhysicalDevice = device.getPhysicalDevice();
    initInfo.Device = device.device();
    initInfo.Queue = device.graphicsQueue();
    initInfo.DescriptorPool = m_DescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    ImGui_ImplVulkan_Init(&initInfo);
}

void GizmoBridge::ProcessSDLEvent(const SDL_Event& event) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void GizmoBridge::BeginFrame(burnhope::BurnhopeWindow& window) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    auto extent = window.getExtent();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height));
}

void GizmoBridge::EndFrame() {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    ImGui::Render();
}

void GizmoBridge::RenderOverlay(VkCommandBuffer cmd) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

bool GizmoBridge::Manipulate(const glm::mat4& view, const glm::mat4& proj, glm::mat4& matrix, const float* snap) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));

    glm::mat4 gizmoProj = proj;
    gizmoProj[1][1] *= -1.0f; // Vulkan clip-space fix, same as the old UIManager code

    ImGuizmo::OPERATION op = m_Operation == GizmoOperation::Translate ? ImGuizmo::TRANSLATE
                            : m_Operation == GizmoOperation::Rotate    ? ImGuizmo::ROTATE
                                                                        : ImGuizmo::SCALE;
    ImGuizmo::MODE mode = m_Mode == GizmoMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), op, mode,
                         glm::value_ptr(matrix), nullptr, snap);
    return ImGuizmo::IsUsing();
}

bool GizmoBridge::IsOver() const {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    return ImGuizmo::IsOver();
}

bool GizmoBridge::IsUsing() const {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
    return ImGuizmo::IsUsing();
}

bool GizmoBridge::WantsMouseCapture() const {
    return IsOver() || IsUsing();
}
}
