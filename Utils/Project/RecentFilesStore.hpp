#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace burnhope::project {

    namespace fs = std::filesystem;

    struct RecentEntry {
        std::string path;          // absolute path to project dir or .bhscene file
        std::string displayName;
        std::string lastOpenedIso8601;
    };

    // Global (user-wide) list of recently opened projects and scenes, used to
    // populate the Project Launcher / File > Recent menus. Stored outside the
    // repo, under the OS user config directory — editor tooling state, not
    // gameplay data.
    class RecentFilesStore {
    public:
        static fs::path ConfigFilePath();

        void Load();
        bool Save() const;

        void TouchProject(const fs::path& projectDir, const std::string& displayName);
        void TouchScene(const fs::path& sceneFile, const std::string& displayName);

        void RemoveProject(const fs::path& projectDir);
        void RemoveScene(const fs::path& sceneFile);

        // Drops entries whose paths no longer exist on disk.
        void PruneMissing();

        const std::vector<RecentEntry>& RecentProjects() const { return m_RecentProjects; }
        const std::vector<RecentEntry>& RecentScenes() const { return m_RecentScenes; }

    private:
        static void Touch(std::vector<RecentEntry>& list, const std::string& path, const std::string& displayName, size_t cap = 20);
        static void Remove(std::vector<RecentEntry>& list, const std::string& path);

        std::vector<RecentEntry> m_RecentProjects;
        std::vector<RecentEntry> m_RecentScenes;
    };
}
