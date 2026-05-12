#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <imgui_impl_vulkan.h>
#include "../Render/Texture.hpp"
// #include "../Render/ModelImporter.h" // Раскомментируй, если нужен импорт моделей

namespace burnhope {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    class ContentBrowserWindow : public IUIWindow {
    private:
        char searchBuffer[256] = "";
        char inlineRenameBuf[256] = "";
        bool focusRename = false;
        int lastClickedIndex = -1;
        float thumbnailSize = 80.0f;

        struct PreviewData {
            std::shared_ptr<BurnhopeTexture> texture;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };
        std::unordered_map<std::string, PreviewData> previewCache;

    public:
        ContentBrowserWindow() : IUIWindow("Content Browser") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

            HandleHotkeys(context);
            DrawNavigationBar(context);
            
            ImGui::Separator();

            // Используем современные таблицы ImGui для изменяемого сплиттера
            if (ImGui::BeginTable("CB_Layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Grid", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("LeftTreePanel");
                DrawFolderTree(context, context.projectDirectory);
                ImGui::EndChild();
                
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("RightGridPanel");
                DrawGrid(context);
                ImGui::EndChild();
                
                ImGui::EndTable();
            }
            ImGui::End();
        }

    private:
        void DeleteSelected(UIContext& context) {
            if (context.selectedAssets.empty()) return;
            for (const auto& path : context.selectedAssets) {
                try { fs::remove_all(path); } catch (...) {}
            }
            context.selectedAssets.clear();
            lastClickedIndex = -1;
        }

        VkDescriptorSet GetOrLoadPreview(UIContext& context, const std::string& path) {
            if (previewCache.find(path) != previewCache.end()) return previewCache[path].descriptorSet;

            std::string imageToLoad = path;
            fs::path p(path);
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            // Если это материал - пытаемся вытащить Albedo текстуру для превью
            if (ext == ".bhmat" || ext == ".json") {
                std::ifstream f(path);
                if (f.is_open()) {
                    json j;
                    try { f >> j; } catch(...) { previewCache[path] = {nullptr, VK_NULL_HANDLE}; return VK_NULL_HANDLE; }
                    if (j.is_object()) {
                        std::string albedo = j.value("albedoPath", "");
                        if (!albedo.empty() && fs::exists(albedo)) {
                            imageToLoad = albedo;
                        } else { previewCache[path] = {nullptr, VK_NULL_HANDLE}; return VK_NULL_HANDLE; }
                    } else { previewCache[path] = {nullptr, VK_NULL_HANDLE}; return VK_NULL_HANDLE; }
                } else { previewCache[path] = {nullptr, VK_NULL_HANDLE}; return VK_NULL_HANDLE; }
            } else if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga") {
                previewCache[path] = {nullptr, VK_NULL_HANDLE};
                return VK_NULL_HANDLE;
            }

            try {
                auto tex = std::make_shared<BurnhopeTexture>(*context.device, imageToLoad, true);
                VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(tex->getSampler(), tex->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                previewCache[path] = {tex, ds};
                return ds;
            } catch (...) {
                previewCache[path] = {nullptr, VK_NULL_HANDLE};
                return VK_NULL_HANDLE;
            }
        }

        void NavigateTo(UIContext& context, const fs::path& target) {
            if (context.currentDirectory == target) return;
            if (context.dirHistoryIndex < context.dirHistory.size() - 1) {
                context.dirHistory.erase(context.dirHistory.begin() + context.dirHistoryIndex + 1, context.dirHistory.end());
            }
            context.dirHistory.push_back(target);
            context.dirHistoryIndex++;
            context.currentDirectory = target;
            context.selectedAssets.clear();
            context.renamingPath = "";
            lastClickedIndex = -1;
        }

        void HandleHotkeys(UIContext& context) {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
            ImGuiIO& io = ImGui::GetIO();

            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !context.selectedAssets.empty() && context.renamingPath.empty()) {
                DeleteSelected(context);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2) && context.selectedAssets.size() == 1 && context.renamingPath.empty()) {
                StartRename(context, context.selectedAssets[0]);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !context.selectedAssets.empty()) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = false;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !context.selectedAssets.empty()) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = true;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !context.clipboardPaths.empty()) {
                PasteCopiedItems(context, context.currentDirectory);
            }
        }

        void DrawNavigationBar(UIContext& context) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            
            if (context.dirHistoryIndex > 0) {
                if (ImGui::Button("◄")) {
                    context.dirHistoryIndex--;
                    context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                    context.selectedAssets.clear();
                }
            } else ImGui::TextDisabled("◄");
            
            ImGui::SameLine();
            
            if (context.dirHistoryIndex < context.dirHistory.size() - 1) {
                if (ImGui::Button("►")) {
                    context.dirHistoryIndex++;
                    context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                    context.selectedAssets.clear();
                }
            } else ImGui::TextDisabled("►");

            ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
            ImGui::PopStyleColor();

            if (ImGui::Button("➕ Create ")) ImGui::OpenPopup("CreateMenuPopup");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##Search", "🔍 Search...", searchBuffer, sizeof(searchBuffer));
            
            ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

            // Хлебные крошки (Breadcrumbs)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button("🏠 All")) NavigateTo(context, context.projectDirectory);
            
            fs::path rel = fs::relative(context.currentDirectory, context.projectDirectory);
            fs::path accum = context.projectDirectory;
            if (rel.string() != ".") {
                for (auto it = rel.begin(); it != rel.end(); ++it) {
                    ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine();
                    accum /= *it;
                    if (ImGui::Button(it->string().c_str())) NavigateTo(context, accum);
                }
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > 100.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 100.0f);
                ImGui::SetNextItemWidth(100.0f);
                ImGui::SliderFloat("##IconSize", &thumbnailSize, 32.0f, 256.0f, "Zoom");
            }

            // Меню создания
            if (ImGui::BeginPopup("CreateMenuPopup")) {
                if (ImGui::MenuItem("New Folder")) {
                    fs::path newPath = context.currentDirectory / "New Folder";
                    int count = 1;
                    while (fs::exists(newPath)) { newPath = context.currentDirectory / ("New Folder " + std::to_string(count++)); }
                    fs::create_directory(newPath);
                    context.selectedAssets = { newPath.string() };
                    StartRename(context, newPath.string());
                }
                if (ImGui::MenuItem("Material (.bhmat)")) {
                    fs::path newPath = context.currentDirectory / "New Material.bhmat";
                    int count = 1;
                    while (fs::exists(newPath)) { newPath = context.currentDirectory / ("New Material " + std::to_string(count++) + ".bhmat"); }
                    
                    json j; j["name"] = "New Material";
                    std::ofstream file(newPath); file << j.dump(4);
                    
                    context.selectedAssets = { newPath.string() };
                    StartRename(context, newPath.string());
                }
                if (ImGui::MenuItem("Scene (.burnscene)")) {
                    fs::path newPath = context.currentDirectory / "New Scene.burnscene";
                    int count = 1;
                    while (fs::exists(newPath)) { newPath = context.currentDirectory / ("New Scene " + std::to_string(count++) + ".burnscene"); }
                    
                    json j; j["Entities"] = json::array();
                    std::ofstream file(newPath); file << j.dump(4);
                    
                    context.selectedAssets = { newPath.string() };
                    StartRename(context, newPath.string());
                }
                ImGui::EndPopup();
            }
        }

        void DrawFolderTree(UIContext& context, const fs::path& dir) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (context.currentDirectory == dir) flags |= ImGuiTreeNodeFlags_Selected;
            
            bool isLeaf = true;
            for (auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_directory()) { isLeaf = false; break; }
            }
            if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf;

            std::string nodeName = dir == context.projectDirectory ? "📁 Project" : "📁 " + dir.filename().string();
            
            ImGui::PushID(dir.string().c_str());
            bool isOpen = ImGui::TreeNodeEx("##node", flags, "%s", nodeName.c_str());
            
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo(context, dir);

            if (isOpen) {
                for (auto& entry : fs::directory_iterator(dir)) {
                    if (entry.is_directory()) DrawFolderTree(context, entry.path());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        void DrawGrid(UIContext& context) {
            std::vector<fs::directory_entry> items;
            std::string searchStr(searchBuffer);
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

            // Сбор файлов
            if (!searchStr.empty()) {
                for (auto& entry : fs::recursive_directory_iterator(context.projectDirectory)) {
                    std::string lowerName = entry.path().filename().string();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find(searchStr) != std::string::npos) items.push_back(entry);
                }
            } else {
                for (auto& entry : fs::directory_iterator(context.currentDirectory)) items.push_back(entry);
            }

            // Сортировка: папки сначала
            std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (a.is_directory() && !b.is_directory()) return true;
                if (!a.is_directory() && b.is_directory()) return false;
                return a.path().filename().string() < b.path().filename().string();
            });

            // Настройка сетки
            float padding = 16.0f;
            float thumbnailSize = 64.0f;
            float itemWidth = thumbnailSize + 16.0f;
            float itemHeight = thumbnailSize + 45.0f;
            int columnCount = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (itemWidth + padding)));

            ImGui::Columns(columnCount, 0, false);

            for (int i = 0; i < items.size(); ++i) {
                const auto& path = items[i].path();
                std::string pathStr = path.string();
                bool isDir = items[i].is_directory();

                ImGui::PushID(pathStr.c_str());
                
                // Рендер кнопки-хитбокса и логика выделения...
                bool isSel = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr) != context.selectedAssets.end();
                
                ImGui::PushStyleColor(ImGuiCol_Button, isSel ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f) : ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                
                VkDescriptorSet preview = isDir ? VK_NULL_HANDLE : GetOrLoadPreview(context, pathStr);
                
                if (preview != VK_NULL_HANDLE) {
                    if (ImGui::ImageButton(pathStr.c_str(), (ImTextureID)preview, ImVec2(thumbnailSize, thumbnailSize))) {
                        if (!ImGui::GetIO().KeyCtrl) context.selectedAssets.clear();
                        context.selectedAssets.push_back(pathStr);
                    }
                } else {
                    if (ImGui::Button(isDir ? "DIR" : "FILE", ImVec2(thumbnailSize, thumbnailSize))) {
                        if (!ImGui::GetIO().KeyCtrl) context.selectedAssets.clear();
                        context.selectedAssets.push_back(pathStr);
                    }
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr.c_str(), pathStr.size() + 1);
                    ImGui::Text("%s", path.filename().string().c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    if (isDir) {
                        NavigateTo(context, path);
                    } else if (path.extension() == ".burnscene" || path.extension() == ".json") {
                        context.pendingSceneLoadPath = pathStr;
                    }
                }

                // Имя файла или инпут переименования
                if (context.renamingPath == pathStr) {
                    ImGui::SetNextItemWidth(itemWidth);
                    if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; }
                    if (ImGui::InputText("##rename", inlineRenameBuf, sizeof(inlineRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        ApplyRename(context);
                    }
                    if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) ApplyRename(context);
                } else {
                    ImGui::TextWrapped("%s", path.filename().string().c_str());
                }

                ImGui::NextColumn();
                ImGui::PopID();
            }
            ImGui::Columns(1);
        }

        void StartRename(UIContext& context, const std::string& path) {
            context.renamingPath = path;
            strncpy(inlineRenameBuf, fs::path(path).stem().string().c_str(), sizeof(inlineRenameBuf));
            focusRename = true;
        }

        void ApplyRename(UIContext& context) {
            if (!context.renamingPath.empty() && strlen(inlineRenameBuf) > 0) {
                fs::path oldP(context.renamingPath);
                fs::path newP = oldP.parent_path() / (std::string(inlineRenameBuf) + oldP.extension().string());
                if (oldP != newP && !fs::exists(newP)) {
                    fs::rename(oldP, newP);
                    auto it = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), context.renamingPath);
                    if (it != context.selectedAssets.end()) *it = newP.string();
                }
            }
            context.renamingPath = "";
        }

        void PasteCopiedItems(UIContext& context, const fs::path& targetDir) {
            if (context.clipboardPaths.empty()) return;
            for (const auto& cbPath : context.clipboardPaths) {
                if (!fs::exists(cbPath)) continue;
                fs::path src(cbPath);
                fs::path dst = targetDir / src.filename();
                int copyCount = 1;
                while (fs::exists(dst)) {
                    dst = targetDir / (src.stem().string() + " " + std::to_string(copyCount++) + src.extension().string());
                }
                if (context.isCut) fs::rename(src, dst);
                else fs::copy(src, dst, fs::copy_options::recursive);
            }
            if (context.isCut) { context.clipboardPaths.clear(); context.isCut = false; }
        }
    };
}