#include "app/BrowserFiles.hpp"

#include "asset/ProjectDoc.hpp"
#include "gfx/Material.hpp"

#include <fstream>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;
namespace {

fs::path uniqueIn(const fs::path& dir, const std::string& stem, const std::string& ext) {
    std::error_code ec;
    fs::path candidate = dir / (stem + ext);
    for (uint32_t n = 2; fs::exists(candidate, ec) && n < 10000; ++n) {
        candidate = dir / (stem + " " + std::to_string(n) + ext);
    }
    return candidate;
}

} // namespace

fs::path browserCreate(const fs::path& dir, NewFile what) {
    std::error_code ec;
    fs::path made;
    switch (what) {
    case NewFile::Folder:
        made = uniqueIn(dir, "New Folder", "");
        if (!fs::create_directory(made, ec)) {
            return {};
        }
        return made;
    case NewFile::Material: {
        made = uniqueIn(dir, "New Material", ".bhmat");
        const BhmatDoc doc{};
        return materialSaveBhmat(made.string().c_str(), doc) ? made : fs::path{};
    }
    case NewFile::Scene:
        made = uniqueIn(dir, "New Scene", ".bscene");
        return sceneDocWrite(made.string().c_str(), nullptr, 0) ? made : fs::path{};
    case NewFile::Plain: {
        made = uniqueIn(dir, "New File", ".txt");
        std::ofstream out(made);
        return out.good() ? made : fs::path{};
    }
    }
    return {};
}

fs::path browserDuplicate(const fs::path& path) {
    std::error_code ec;
    const std::string stem = path.stem().string() + " copy";
    const fs::path target = uniqueIn(path.parent_path(), stem, path.extension().string());
    fs::copy(path, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    return ec ? fs::path{} : target;
}

bool browserTrash(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path bin = root / ".trash";
    fs::create_directories(bin, ec);
    if (ec) {
        return false;
    }
    const fs::path target = uniqueIn(bin, path.stem().string(), path.extension().string());
    fs::rename(path, target, ec);
    return !ec;
}

} // namespace burnhope
