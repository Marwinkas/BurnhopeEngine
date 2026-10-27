#include "app/BrowserFiles.hpp"

#include "app/BrowserModel.hpp"
#include "asset/ProjectDoc.hpp"
#include "gfx/Material.hpp"

#include <iterator>

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

// A copied .gltf must read its own buffer: the old buffer file name inside the JSON becomes the new one.
void retargetBuffer(const fs::path& gltf, const fs::path& from, const fs::path& to) {
    std::ifstream in(gltf, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string oldName = from.filename().string();
    const std::string newName = to.filename().string();
    for (size_t at = text.find(oldName); at != std::string::npos; at = text.find(oldName, at + newName.size())) {
        text.replace(at, oldName.size(), newName);
    }
    std::ofstream out(gltf, std::ios::binary | std::ios::trunc);
    out << text;
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
    if (ec) {
        return {};
    }
    const fs::path buffer = modelCompanion(path);
    if (!buffer.empty()) {
        fs::path copy = target;
        copy.replace_extension(".bin");
        fs::copy_file(buffer, copy, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(target, ec);
            return {};
        }
        retargetBuffer(target, buffer, copy);
    }
    return target;
}

bool browserMove(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    const fs::path buffer = modelCompanion(from);
    fs::path bufferTo = to;
    bufferTo.replace_extension(".bin");
    if (!buffer.empty() && fs::exists(bufferTo, ec)) {
        return false;
    }
    fs::rename(from, to, ec);
    if (ec) {
        return false;
    }
    if (!buffer.empty()) {
        fs::rename(buffer, bufferTo, ec);
    }
    return !ec;
}

fs::path browserImport(const fs::path& dir, const fs::path& src) {
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        return {};
    }
    const fs::path target = uniqueIn(dir, src.stem().string(), src.extension().string());
    if (!fs::copy_file(src, target, fs::copy_options::none, ec) || ec) {
        return {};
    }
    const fs::path buffer = modelCompanion(src);
    if (!buffer.empty()) {
        fs::path copy = target;
        copy.replace_extension(".bin");
        if (!fs::copy_file(buffer, copy, fs::copy_options::overwrite_existing, ec) || ec) {
            fs::remove(target, ec);
            return {};
        }
        retargetBuffer(target, buffer, copy);
    }
    return target;
}

bool browserTrash(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path bin = root / ".trash";
    fs::create_directories(bin, ec);
    if (ec) {
        return false;
    }
    const fs::path target = uniqueIn(bin, path.stem().string(), path.extension().string());
    return browserMove(path, target);
}

} // namespace burnhope
