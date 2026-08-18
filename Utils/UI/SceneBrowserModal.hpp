#pragma once
#include "UIWidgets.hpp"
#include "UIScope.hpp"
#include "NativeDialogs.hpp"
#include "../Project/ProjectFile.hpp"
#include "../Project/RecentFilesStore.hpp"
#include "../Project/SceneController.hpp"
#include <filesystem>
#include <vector>
#include <string>

// Replaces the old ImGui "Load Scene" popup: lists recent scenes plus every
// `.bhscene` file under the project's Scenes/ folder, with New/Open/Delete/
// Duplicate actions. Opened from the File menu and Ctrl+O.
namespace burnhope::ui {

    class SceneBrowserModal {
    public:
        void Open() { m_IsOpen = true; }
        void Close() { m_IsOpen = false; }
        bool IsOpen() const { return m_IsOpen; }

        enum class Action { None, Open, Delete, Duplicate };

        Action Draw(UIWidgets& widgets, project::ProjectFile& project, project::RecentFilesStore& recent,
                    Rect fullRect, std::string& outPath) {
            if (!m_IsOpen) return Action::None;
            Action action = Action::None;

            widgets.Background(fullRect, kTheme.modalDim);
            Rect modal{
                fullRect.x + fullRect.w * 0.5f - 280.0f,
                fullRect.y + fullRect.h * 0.5f - 220.0f,
                560.0f, 440.0f
            };
            widgets.Background({modal.x + 4, modal.y + 6, modal.w, modal.h}, kTheme.shadow, 8.0f);
            widgets.Background(modal, kTheme.popup, 8.0f);
            widgets.Background({modal.x, modal.y, modal.w, 36.0f}, kTheme.title, 8.0f);
            widgets.Background({modal.x, modal.y + 34.0f, modal.w, 2.0f}, kTheme.accent);

            ui::Panel panel(widgets, "##scene_browser", modal, 12.0f, true);
            widgets.Text("Open Scene", kTheme.text);
            widgets.Dummy({1, 8});

            if (widgets.Button("New Scene", {120, 26})) {
                outPath.clear();
                action = Action::Open;
                m_IsOpen = false;
            }
            widgets.SameLine();
            if (widgets.Button("Close", {80, 26})) {
                m_IsOpen = false;
            }

            widgets.Separator();
            widgets.Text("Scenes in project", kTheme.textMuted);

            std::error_code ec;
            if (std::filesystem::exists(project.ScenesDir(), ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(project.ScenesDir(), ec)) {
                    if (entry.path().extension() != ".bhscene") continue;
                    std::string path = entry.path().string();
                    widgets.PushID(path);
                    bool selected = (m_Selected == path);
                    if (widgets.Selectable(entry.path().filename().string(), selected, {0, 26})) {
                        m_Selected = path;
                    }
                    if (selected) {
                        widgets.SameLine();
                        if (widgets.Button("Open", {64, 24})) {
                            outPath = path;
                            action = Action::Open;
                            m_IsOpen = false;
                        }
                        widgets.SameLine();
                        if (widgets.Button("Delete", {64, 24})) {
                            outPath = path;
                            action = Action::Delete;
                        }
                    }
                    widgets.PopID();
                }
            }

            widgets.Separator();
            widgets.Text("Recent scenes", kTheme.textMuted);
            for (const auto& entry : recent.RecentScenes()) {
                widgets.PushID(entry.path);
                if (widgets.Selectable(entry.displayName.empty() ? entry.path : entry.displayName, m_Selected == entry.path, {0, 24})) {
                    outPath = entry.path;
                    action = Action::Open;
                    m_IsOpen = false;
                }
                widgets.PopID();
            }

            return action;
        }

    private:
        bool m_IsOpen = false;
        std::string m_Selected;
    };
}
