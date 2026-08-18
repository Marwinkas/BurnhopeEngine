#pragma once
#include "UIWidgets.hpp"
#include "UIScope.hpp"
#include "NativeDialogs.hpp"
#include "../Project/ProjectFile.hpp"
#include "../Project/RecentFilesStore.hpp"
#include <string>
#include <filesystem>

// Startup screen shown when no project path was passed on the command line:
// lists recent projects, lets the user create or open a project, or remove/
// delete entries. Drawn full-screen via UIWidgets before the main editor UI
// (UIManager/UIDockspace) ever starts up.
namespace burnhope::ui {

    class ProjectLauncherScreen {
    public:
        struct Result {
            bool openProject = false;
            std::string projectPath;
        };

        Result Draw(UIWidgets& widgets, project::RecentFilesStore& recent, Rect fullRect) {
            Result result;
            ui::Panel panel(widgets, "##launcher", fullRect, ui::kRegionPad, true);

            widgets.Text("Burnhope Engine", {0.95f, 0.95f, 1.0f, 1.0f});
            widgets.Text("Select a project to open, or create a new one.");
            widgets.Separator();

            if (widgets.Button("New Project...", {180, 30})) {
                if (auto folder = NativeDialogs::PickFolder()) {
                    std::filesystem::path chosen(*folder);
                    project::ProjectFile newProject;
                    if (project::ProjectFile::Create(chosen.parent_path(), chosen.filename().string(), newProject)) {
                        recent.TouchProject(newProject.rootDirectory, newProject.projectName);
                        result.openProject = true;
                        result.projectPath = newProject.rootDirectory.string();
                    }
                }
            }
            widgets.SameLine();
            if (widgets.Button("Open Project...", {180, 30})) {
                if (auto folder = NativeDialogs::PickFolder()) {
                    if (project::ProjectFile::IsProjectDirectory(*folder)) {
                        recent.TouchProject(*folder, std::filesystem::path(*folder).filename().string());
                        result.openProject = true;
                        result.projectPath = *folder;
                    }
                }
            }

            widgets.Separator();
            widgets.Text("Recent Projects");

            for (const auto& entry : recent.RecentProjects()) {
                widgets.PushID(entry.path);
                bool selected = (m_SelectedPath == entry.path);
                if (widgets.Selectable(entry.displayName.empty() ? entry.path : entry.displayName, selected, {0, 26})) {
                    m_SelectedPath = entry.path;
                }
                if (selected) {
                    widgets.SameLine();
                    if (widgets.Button("Open", {70, 26})) {
                        result.openProject = true;
                        result.projectPath = entry.path;
                        recent.TouchProject(entry.path, entry.displayName);
                    }
                    widgets.SameLine();
                    if (widgets.Button("Remove", {80, 26})) {
                        recent.RemoveProject(entry.path);
                        m_SelectedPath.clear();
                    }
                }
                widgets.PopID();
            }

            return result;
        }

    private:
        std::string m_SelectedPath;
    };
}
