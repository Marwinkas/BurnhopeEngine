#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/Domain.hpp"
#include "files/Model.hpp"
#include "ui/State.hpp"

#include <cstdio>
#include <filesystem>

namespace burnhope {
namespace fs = std::filesystem;

namespace {

bool hasProjectFile(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "project.bhproject", ec);
}

void note(UiState& s, const char* text) {
    if (uiFiles(s).files != nullptr) {
        static_cast<FileModel*>(uiFiles(s).files)->setStatus(text);
    }
}

} // namespace

void filesNewProject(UiState& s, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::error_code ec;
    fs::create_directories(fs::path(path), ec);
    const fs::path dir = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        note(s, "cannot create project");
        return;
    }
    std::FILE* out = std::fopen((dir / "project.bhproject").string().c_str(), "w");
    if (out != nullptr) {
        std::fprintf(out, "{\"name\":\"%s\",\"version\":1}\n", dir.filename().string().c_str());
        std::fclose(out);
    }
    filesRoot(s, dir.string().c_str());
}

void filesOpenProject(UiState& s, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::error_code ec;
    fs::path dir(path);
    if (fs::is_regular_file(dir, ec) && dir.filename() == "project.bhproject") {
        dir = dir.parent_path();
    }
    dir = fs::weakly_canonical(dir, ec);
    if (ec || !hasProjectFile(dir)) {
        note(s, "no project.bhproject");
        return;
    }
    filesRoot(s, dir.string().c_str());
}

} // namespace burnhope
