#pragma once
// This is the ONLY file in the codebase allowed to #include <imgui.h> /
// <ImGuizmo.h>. Everything else in the engine went through the ImGui ->
// nfd-extended/Yoga/HarfBuzz rewrite; ImGuizmo stays (previous from-scratch
// gizmo attempts were unsuccessful) running behind a private, minimal,
// otherwise-invisible ImGui context whose only job is feeding ImGuizmo
// mouse/keyboard state and compositing its draw list as a thin overlay pass
// after the new UI renders.
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <string>

namespace burnhope {
    class BurnhopeDevice;
    class BurnhopeWindow;
}

namespace burnhope::ui {

    enum class GizmoOperation { Translate, Rotate, Scale };
    enum class GizmoMode { Local, World };

    class GizmoBridge {
    public:
        GizmoBridge(burnhope::BurnhopeWindow& window, burnhope::BurnhopeDevice& device, VkFormat colorFormat);
        ~GizmoBridge();

        void ProcessSDLEvent(const SDL_Event& event);

        // Must be called once per frame, before Manipulate(); sets up the
        // private ImGui frame and ImGuizmo's draw target/viewport rect.
        void BeginFrame(burnhope::BurnhopeWindow& window);

        // Ends the private ImGui frame (ImGui::Render()) — call after all
        // Manipulate() calls for the frame.
        void EndFrame();

        // Composites ImGuizmo's draw data on top of whatever is already in
        // the swapchain image (call after the main UIRenderer::Render pass,
        // inside the same dynamic rendering scope).
        void RenderOverlay(VkCommandBuffer cmd);

        void SetOperation(GizmoOperation op) { m_Operation = op; }
        void SetMode(GizmoMode mode) { m_Mode = mode; }
        GizmoOperation Operation() const { return m_Operation; }
        GizmoMode Mode() const { return m_Mode; }

        // Draws/manipulates a gizmo for `matrix` (world-space). Returns true
        // while the user is actively dragging it (mirrors ImGuizmo::IsUsing()).
        bool Manipulate(const glm::mat4& view, const glm::mat4& proj, glm::mat4& matrix,
                        const float* snap = nullptr);

        bool IsOver() const;
        bool IsUsing() const;

        // True when the mouse is over any part of the private ImGui frame
        // (i.e. over the gizmo) — used by the main UI's raycast-select logic
        // to avoid picking through the gizmo handles.
        bool WantsMouseCapture() const;

    private:
        void InitImGui(burnhope::BurnhopeWindow& window, burnhope::BurnhopeDevice& device, VkFormat colorFormat);

        burnhope::BurnhopeDevice& m_Device;
        void* m_ImGuiContext = nullptr; // ImGuiContext*, opaque here to avoid leaking <imgui.h> into the header
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

        GizmoOperation m_Operation = GizmoOperation::Translate;
        GizmoMode m_Mode = GizmoMode::World;
    };
}
