#include "NativeDialogs.hpp"
#include <nfd.h>
#include <vector>

namespace burnhope::ui {

    void NativeDialogs::Init() { NFD_Init(); }
    void NativeDialogs::Shutdown() { NFD_Quit(); }

    namespace {
        std::vector<nfdu8filteritem_t> ToNfdFilters(const std::vector<FileFilter>& filters) {
            std::vector<nfdu8filteritem_t> out;
            out.reserve(filters.size());
            for (const auto& f : filters) out.push_back({f.name.c_str(), f.extensions.c_str()});
            return out;
        }
    }

    std::optional<std::string> NativeDialogs::OpenFile(const std::vector<FileFilter>& filters, const std::string& defaultPath) {
        auto nfdFilters = ToNfdFilters(filters);
        nfdu8char_t* outPath = nullptr;
        nfdresult_t result = NFD_OpenDialogU8(&outPath, nfdFilters.empty() ? nullptr : nfdFilters.data(),
                                               static_cast<nfdfiltersize_t>(nfdFilters.size()),
                                               defaultPath.empty() ? nullptr : defaultPath.c_str());
        if (result == NFD_OKAY && outPath) {
            std::string path(outPath);
            NFD_FreePathU8(outPath);
            return path;
        }
        return std::nullopt;
    }

    std::optional<std::string> NativeDialogs::SaveFile(const std::vector<FileFilter>& filters, const std::string& defaultPath, const std::string& defaultName) {
        auto nfdFilters = ToNfdFilters(filters);
        nfdu8char_t* outPath = nullptr;
        nfdresult_t result = NFD_SaveDialogU8(&outPath, nfdFilters.empty() ? nullptr : nfdFilters.data(),
                                               static_cast<nfdfiltersize_t>(nfdFilters.size()),
                                               defaultPath.empty() ? nullptr : defaultPath.c_str(),
                                               defaultName.empty() ? nullptr : defaultName.c_str());
        if (result == NFD_OKAY && outPath) {
            std::string path(outPath);
            NFD_FreePathU8(outPath);
            return path;
        }
        return std::nullopt;
    }

    std::optional<std::string> NativeDialogs::PickFolder(const std::string& defaultPath) {
        nfdu8char_t* outPath = nullptr;
        nfdresult_t result = NFD_PickFolderU8(&outPath, defaultPath.empty() ? nullptr : defaultPath.c_str());
        if (result == NFD_OKAY && outPath) {
            std::string path(outPath);
            NFD_FreePathU8(outPath);
            return path;
        }
        return std::nullopt;
    }
}
