#include "RecentFilesStore.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>

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

    nlohmann::json ToJson(const std::vector<RecentEntry>& list) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : list) {
            arr.push_back({{"path", e.path}, {"displayName", e.displayName}, {"lastOpened", e.lastOpenedIso8601}});
        }
        return arr;
    }

    std::vector<RecentEntry> FromJson(const nlohmann::json& j) {
        std::vector<RecentEntry> list;
        for (const auto& item : j) {
            RecentEntry e;
            e.path = item.value("path", "");
            e.displayName = item.value("displayName", "");
            e.lastOpenedIso8601 = item.value("lastOpened", "");
            list.push_back(std::move(e));
        }
        return list;
    }
}

fs::path RecentFilesStore::ConfigFilePath() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    fs::path base = appData ? fs::path(appData) : fs::path(".");
#else
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    fs::path base;
    if (xdgConfig && *xdgConfig) {
        base = fs::path(xdgConfig);
    } else {
        const char* home = std::getenv("HOME");
        base = (home ? fs::path(home) : fs::path(".")) / ".config";
    }
#endif
    return base / "BurnhopeEngine" / "recent.json";
}

void RecentFilesStore::Load() {
    fs::path path = ConfigFilePath();
    std::ifstream file(path);
    if (!file.is_open()) return;

    nlohmann::json j;
    try { file >> j; } catch (...) { return; }

    if (j.contains("recentProjects")) m_RecentProjects = FromJson(j["recentProjects"]);
    if (j.contains("recentScenes")) m_RecentScenes = FromJson(j["recentScenes"]);
    PruneMissing();
}

bool RecentFilesStore::Save() const {
    fs::path path = ConfigFilePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    nlohmann::json j;
    j["recentProjects"] = ToJson(m_RecentProjects);
    j["recentScenes"] = ToJson(m_RecentScenes);

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

void RecentFilesStore::Touch(std::vector<RecentEntry>& list, const std::string& path, const std::string& displayName, size_t cap) {
    auto it = std::find_if(list.begin(), list.end(), [&](const RecentEntry& e) { return e.path == path; });
    if (it != list.end()) list.erase(it);
    RecentEntry entry{path, displayName, NowIso8601()};
    list.insert(list.begin(), std::move(entry));
    if (list.size() > cap) list.resize(cap);
}

void RecentFilesStore::Remove(std::vector<RecentEntry>& list, const std::string& path) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const RecentEntry& e) { return e.path == path; }), list.end());
}

void RecentFilesStore::TouchProject(const fs::path& projectDir, const std::string& displayName) {
    Touch(m_RecentProjects, fs::absolute(projectDir).string(), displayName);
    Save();
}

void RecentFilesStore::TouchScene(const fs::path& sceneFile, const std::string& displayName) {
    Touch(m_RecentScenes, fs::absolute(sceneFile).string(), displayName);
    Save();
}

void RecentFilesStore::RemoveProject(const fs::path& projectDir) {
    Remove(m_RecentProjects, fs::absolute(projectDir).string());
    Save();
}

void RecentFilesStore::RemoveScene(const fs::path& sceneFile) {
    Remove(m_RecentScenes, fs::absolute(sceneFile).string());
    Save();
}

void RecentFilesStore::PruneMissing() {
    auto missing = [](const RecentEntry& e) { return !fs::exists(e.path); };
    m_RecentProjects.erase(std::remove_if(m_RecentProjects.begin(), m_RecentProjects.end(), missing), m_RecentProjects.end());
    m_RecentScenes.erase(std::remove_if(m_RecentScenes.begin(), m_RecentScenes.end(), missing), m_RecentScenes.end());
}
}
