#pragma once
#include <imgui.h>
#include "DirectXMathCompat.hpp"
#include <string>
#include <vector>
#include "../Render/Material.hpp"

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
    inline bool DrawVec3Control(const std::string& label, float3& values, float resetValue = 0.0f) {
        float v[3] = {values.x, values.y, values.z};
        bool changed = DrawFloatControl(label, v, 3, resetValue);
        values.x = v[0]; values.y = v[1]; values.z = v[2];
        return changed;
    }
    
    inline bool DrawVec2Control(const std::string& label, float2& values, float resetValue = 0.0f) {
        float v[2] = {values.x, values.y};
        bool changed = DrawFloatControl(label, v, 2, resetValue);
        values.x = v[0]; values.y = v[1];
        return changed;
    }
    inline bool DrawVec4Control(const std::string& label, float4& values, float resetValue = 0.0f) {
        float v[4] = {values.x, values.y, values.z, values.w};
        bool changed = DrawFloatControl(label, v, 4, resetValue);
        values.x = v[0]; values.y = v[1]; values.z = v[2]; values.w = v[3];
        return changed;
    }
    // --- Compact Property Grid System ---
    inline bool BeginPropertyGrid(const std::string& name) {
        if (!ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return false;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
        ImGui::BeginTable((name + "Table").c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    inline void EndPropertyGrid() {
        ImGui::EndTable();
        ImGui::PopStyleVar(2);
        ImGui::Spacing();
    }

    inline bool DrawProperty(const std::string& label, bool* value) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str()); ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::Checkbox(("##" + label).c_str(), value);
    }

    inline bool DrawProperty(const std::string& label, float* value, float min = 0.0f, float max = 0.0f, const char* format = "%.3f") {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str()); ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderFloat(("##" + label).c_str(), value, min, max, format);
    }

    inline bool DrawProperty(const std::string& label, int* value, int min = 0, int max = 0) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str()); ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderInt(("##" + label).c_str(), value, min, max);
    }

    inline bool DrawPropertyColor(const std::string& label, float* value) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str()); ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::ColorEdit3(("##" + label).c_str(), value);
    }

    inline bool DrawPropertyCombo(const std::string& label, int* current_item, const char* items_separated_by_zeros) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str()); ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::Combo(("##" + label).c_str(), current_item, items_separated_by_zeros);
    }
}