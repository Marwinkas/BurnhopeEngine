#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "app/BrowserFiles.hpp"
#include "app/BrowserModel.hpp"

using namespace burnhope;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / "bh_browser_files_test";
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void touch(const char* name, const char* body = "x") const {
        std::ofstream(path / name) << body;
    }
};

bool listed(const std::vector<BrowserItem>& items, const char* name) {
    for (const BrowserItem& item : items) {
        if (item.path.filename() == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("browser: a .gltf and its same-stem .bin are one listed asset") {
    TempDir dir;
    dir.touch("Bistro.gltf", "{\"buffers\":[{\"uri\":\"Bistro.bin\"}]}");
    dir.touch("Bistro.bin", "0123456789");
    dir.touch("Other.bin", "orphan");
    std::vector<BrowserItem> items;
    browserScan(dir.path, BrowserQuery{}, items);
    CHECK(listed(items, "Bistro.gltf"));
    CHECK(!listed(items, "Bistro.bin"));
    CHECK(listed(items, "Other.bin"));
    for (const BrowserItem& item : items) {
        if (item.path.filename() == "Bistro.gltf") {
            CHECK(item.size > 10); // the buffer's bytes count too
            CHECK(std::string(item.name) == "Bistro");
        }
    }
}

TEST_CASE("browser: duplicate, move and trash carry the .bin along") {
    TempDir dir;
    dir.touch("Bistro.gltf", "{\"buffers\":[{\"uri\":\"Bistro.bin\"}]}");
    dir.touch("Bistro.bin", "0123456789");
    fs::create_directories(dir.path / "sub");

    const fs::path copy = browserDuplicate(dir.path / "Bistro.gltf");
    CHECK(!copy.empty());
    if (!(!copy.empty())) {
        return;
    }
    fs::path copyBin = copy;
    copyBin.replace_extension(".bin");
    CHECK(fs::exists(copyBin));
    std::ifstream in(copy);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find(copyBin.filename().string()) != std::string::npos);

    CHECK(browserMove(dir.path / "Bistro.gltf", dir.path / "sub" / "Bistro.gltf"));
    CHECK(fs::exists(dir.path / "sub" / "Bistro.bin"));
    CHECK(!fs::exists(dir.path / "Bistro.bin"));

    CHECK(browserTrash(dir.path, dir.path / "sub" / "Bistro.gltf"));
    CHECK(fs::exists(dir.path / ".trash" / "Bistro.bin"));
    CHECK(!fs::exists(dir.path / "sub" / "Bistro.bin"));
}
