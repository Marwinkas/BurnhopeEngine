#pragma once
#include <string>
#include <vector>
#include <optional>

// Thin wrapper around nfd-extended for native OS file/folder pickers
// (replaces ImGui popup-based file browsing for Import Asset, New/Open
// Project folder pick, Save Scene As, etc).
namespace burnhope::ui {

    struct FileFilter {
        std::string name;        // e.g. "Burnhope Scene"
        std::string extensions;  // comma-separated, no dots, e.g. "bhscene"
    };

    class NativeDialogs {
    public:
        static void Init();
        static void Shutdown();

        // Returns std::nullopt if the user cancelled.
        static std::optional<std::string> OpenFile(const std::vector<FileFilter>& filters = {},
                                                     const std::string& defaultPath = "");
        static std::optional<std::string> SaveFile(const std::vector<FileFilter>& filters = {},
                                                     const std::string& defaultPath = "",
                                                     const std::string& defaultName = "");
        static std::optional<std::string> PickFolder(const std::string& defaultPath = "");
    };
}
