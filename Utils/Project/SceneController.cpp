#include "SceneController.hpp"
#include "../UIContext.h"
#include "../Device.hpp"
#include "../../Render/Scene/BHSceneWriter.hpp"
#include "../../Render/Scene/BHSceneLoader.hpp"

#include <filesystem>
#include <iostream>

namespace burnhope::project {
    namespace fs = std::filesystem;

    SceneController::SceneController(UIContext& context, ProjectFile& project, RecentFilesStore& recent)
        : m_Context(context), m_Project(project), m_Recent(recent) {}

    void SceneController::ResetWorld() {
        if (m_Context.device) vkDeviceWaitIdle(m_Context.device->device());
        m_Context.world->reset();
        m_Context.undoStack.clear();
        m_Context.redoStack.clear();
        m_Context.safeDeleteQueue.clear();
        m_Context.pendingDeletions.clear();
        m_Context.ClearSelection();
        m_Context.needsRebuild = true;
    }

    void SceneController::NewScene() {
        ResetWorld();
        m_Context.currentScenePath = "";
    }

    bool SceneController::SaveScene() {
        if (m_Context.currentScenePath.empty()) {
            std::error_code ec;
            fs::create_directories(m_Project.ScenesDir(), ec);
            return SaveSceneAs((m_Project.ScenesDir() / "Untitled.bhscene").string());
        }
        return burnhope::scene::BHSceneWriter::Save(*m_Context.world, m_Context.currentScenePath);
    }

    bool SceneController::SaveSceneAs(const std::string& absolutePath) {
        if (!burnhope::scene::BHSceneWriter::Save(*m_Context.world, absolutePath)) {
            std::cerr << "[SceneController] Failed to save scene to " << absolutePath << "\n";
            return false;
        }
        m_Context.currentScenePath = absolutePath;

        fs::path rel = fs::relative(absolutePath, m_Project.rootDirectory);
        m_Project.TouchRecentScene(rel.string());
        m_Project.Save();
        m_Recent.TouchScene(absolutePath, fs::path(absolutePath).filename().string());
        return true;
    }

    bool SceneController::OpenScene(const std::string& absolutePath) {
        ResetWorld();
        if (!burnhope::scene::BHSceneLoader::Load(absolutePath, *m_Context.world, m_Context.device)) {
            std::cerr << "[SceneController] Failed to load scene " << absolutePath << "\n";
            return false;
        }
        m_Context.currentScenePath = absolutePath;
        m_Context.needsRebuild = true;

        fs::path rel = fs::relative(absolutePath, m_Project.rootDirectory);
        m_Project.TouchRecentScene(rel.string());
        m_Project.Save();
        m_Recent.TouchScene(absolutePath, fs::path(absolutePath).filename().string());
        return true;
    }

    bool SceneController::DeleteScene(const std::string& absolutePath) {
        std::error_code ec;
        fs::path trashDir = m_Project.rootDirectory / ".trash";
        fs::create_directories(trashDir, ec);

        fs::path dest = trashDir / fs::path(absolutePath).filename();
        fs::rename(absolutePath, dest, ec);
        if (ec) {
            // Cross-device fallback (e.g. trash on a different filesystem).
            fs::copy_file(absolutePath, dest, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::remove(absolutePath, ec);
        }
        if (ec) {
            std::cerr << "[SceneController] Failed to delete scene " << absolutePath << ": " << ec.message() << "\n";
            return false;
        }

        m_Recent.RemoveScene(absolutePath);
        if (m_Context.currentScenePath == absolutePath) {
            NewScene();
        }
        return true;
    }
}
