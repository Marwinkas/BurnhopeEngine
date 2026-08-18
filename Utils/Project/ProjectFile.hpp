#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace burnhope::project {

    namespace fs = std::filesystem;

    inline constexpr const char* kProjectFileName = "project.bhproject";
    inline constexpr const char* kEngineVersion = "0.1.0";

    // Editor-tool metadata for a single project folder. JSON is fine here —
    // this is off the hot path (loaded once at project open), matching the
    // "Cooker/editor only" JSON allowance in the engine rules.
    struct ProjectFile {
        std::string projectName;
        std::string engineVersion = kEngineVersion;
        std::string createdDateIso8601;
        std::string startupScenePath;      // relative to project root, e.g. "Scenes/Main.bhscene"
        std::vector<std::string> recentScenes; // relative paths, most-recent first

        fs::path rootDirectory;

        fs::path ScenesDir() const { return rootDirectory / "Scenes"; }
        fs::path AssetsDir() const { return rootDirectory / "Assets"; }
        fs::path FilePath() const { return rootDirectory / kProjectFileName; }

        void TouchRecentScene(const std::string& relativeScenePath);

        bool Save() const;
        static bool Load(const fs::path& projectDir, ProjectFile& outProject);
        static bool Create(const fs::path& parentDir, const std::string& name, ProjectFile& outProject);
        static bool IsProjectDirectory(const fs::path& dir);
    };
}
