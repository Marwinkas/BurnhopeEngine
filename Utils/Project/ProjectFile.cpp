#include "ProjectFile.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace burnhope::project {
namespace {
    std::string NowIso8601() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::tm tmBuf{};
#if defined(_WIN32)
        gmtime_s(&tmBuf, &t);
#else
        gmtime_r(&t, &tmBuf);
#endif
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
        return std::string(buf);
    }
}

void ProjectFile::TouchRecentScene(const std::string& relativeScenePath) {
    auto it = std::find(recentScenes.begin(), recentScenes.end(), relativeScenePath);
    if (it != recentScenes.end()) recentScenes.erase(it);
    recentScenes.insert(recentScenes.begin(), relativeScenePath);
    if (recentScenes.size() > 20) recentScenes.resize(20);
}

bool ProjectFile::Save() const {
    nlohmann::json j;
    j["projectName"] = projectName;
    j["engineVersion"] = engineVersion;
    j["createdDate"] = createdDateIso8601;
    j["startupScenePath"] = startupScenePath;
    j["recentScenes"] = recentScenes;

    std::ofstream file(FilePath());
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

bool ProjectFile::Load(const fs::path& projectDir, ProjectFile& outProject) {
    fs::path filePath = projectDir / kProjectFileName;
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    nlohmann::json j;
    try { file >> j; } catch (...) { return false; }

    outProject.rootDirectory = projectDir;
    outProject.projectName = j.value("projectName", projectDir.filename().string());
    outProject.engineVersion = j.value("engineVersion", kEngineVersion);
    outProject.createdDateIso8601 = j.value("createdDate", "");
    outProject.startupScenePath = j.value("startupScenePath", "");
    if (j.contains("recentScenes")) {
        for (const auto& s : j["recentScenes"]) outProject.recentScenes.push_back(s.get<std::string>());
    }
    return true;
}

bool ProjectFile::Create(const fs::path& parentDir, const std::string& name, ProjectFile& outProject) {
    fs::path root = parentDir / name;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) { std::cerr << "[ProjectFile] Failed to create " << root << ": " << ec.message() << "\n"; return false; }

    outProject.rootDirectory = root;
    outProject.projectName = name;
    outProject.engineVersion = kEngineVersion;
    outProject.createdDateIso8601 = NowIso8601();
    outProject.startupScenePath = "Scenes/Main.bhscene";
    outProject.recentScenes.clear();

    fs::create_directories(outProject.ScenesDir(), ec);
    fs::create_directories(outProject.AssetsDir(), ec);

    return outProject.Save();
}

bool ProjectFile::IsProjectDirectory(const fs::path& dir) {
    return fs::exists(dir / kProjectFileName);
}
}
