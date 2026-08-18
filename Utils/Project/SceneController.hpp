#pragma once
#include <string>
#include "ProjectFile.hpp"
#include "RecentFilesStore.hpp"

namespace burnhope {
    class UIContext;
}

namespace burnhope::project {

    // Owns the New/Save/SaveAs/Open/Delete scene lifecycle for the active
    // project, on top of the binary `.bhscene` reader/writer. Keeps
    // `ProjectFile::recentScenes` and the global `RecentFilesStore` in sync.
    class SceneController {
    public:
        SceneController(UIContext& context, ProjectFile& project, RecentFilesStore& recent);

        void NewScene();
        bool SaveScene();                        // saves to context.currentScenePath, or SaveAs if empty
        bool SaveSceneAs(const std::string& absolutePath);
        bool OpenScene(const std::string& absolutePath);
        bool DeleteScene(const std::string& absolutePath); // moves file into <project>/.trash

        std::string DefaultSceneDir() const { return m_Project.ScenesDir().string(); }

    private:
        void ResetWorld();

        UIContext& m_Context;
        ProjectFile& m_Project;
        RecentFilesStore& m_Recent;
    };
}
