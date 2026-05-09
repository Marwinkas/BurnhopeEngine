#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace burnhope::UIUtils {

    // Универсальная функция для 1-4 float значений
    inline bool DrawFloatControl(const std::string& label, float* values, int count, float resetValue = 0.0f, float columnWidth = 100.0f) {
        bool changed = false;
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(count, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 4 });

        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        const char* buttonLabels[] = { "X", "Y", "Z", "W" };
        ImVec4 buttonColors[] = {
            ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f },
            ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f },
            ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f },
            ImVec4{ 0.8f, 0.8f, 0.1f, 1.0f }
        };

        for (int i = 0; i < count; i++) {
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColors[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ buttonColors[i].x + 0.1f, buttonColors[i].y + 0.1f, buttonColors[i].z + 0.1f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColors[i]);
            
            if (ImGui::Button(buttonLabels[i], buttonSize)) {
                values[i] = resetValue;
                changed = true;
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat(("##" + std::to_string(i)).c_str(), &values[i], 0.1f, 0.0f, 0.0f, "%.2f")) {
                changed = true;
            }
            if (i < count - 1) ImGui::SameLine();
            ImGui::PopItemWidth();
        }

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();

        return changed;
    }

    // Готовые обертки:
    inline bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f) {
        return DrawFloatControl(label, glm::value_ptr(values), 3, resetValue);
    }
    
    inline bool DrawVec2Control(const std::string& label, glm::vec2& values, float resetValue = 0.0f) {
        return DrawFloatControl(label, glm::value_ptr(values), 2, resetValue);
    }
}