#pragma once
#include "IUIWindow.h"
#include <imgui.h>

namespace burnhope {

class SceneViewportWindow : public IUIWindow {
public:
    SceneViewportWindow() : IUIWindow("Scene Viewport") {}

    void Draw(UIContext& context) override {
        if (!m_IsOpen) {
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin(m_Name.c_str(), &m_IsOpen,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (context.sceneViewImguiSet != VK_NULL_HANDLE && avail.x >= 1.0f && avail.y >= 1.0f) {
                ImGui::Image(reinterpret_cast<ImTextureID>(context.sceneViewImguiSet), avail);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
};

} // namespace burnhope
