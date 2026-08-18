#pragma once
#include "IUIWindow.h"
#include "../Render/Texture.hpp"
#include "UI/MaterialPreview.hpp"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cctype>
#include <nlohmann/json.hpp>
#include <any>
#include <unordered_map>

namespace burnhope {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    class ContentBrowserWindow : public IUIWindow {
    public:
        ContentBrowserWindow() : IUIWindow("Content Browser") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            m_WantContext = false;
            ui::Panel panel(widgets, m_Name, contentRect, ui::kRegionPad, false);

            HandleHotkeys(context, widgets);

            constexpr float kToolbarH = 28.0f;
            constexpr float kPathBarH = 28.0f;
            constexpr float kFooterH = 28.0f;
            constexpr float kSplitW = 5.0f;
            constexpr float kGap = 4.0f;

            glm::vec2 origin = widgets.GetCursor();
            glm::vec2 avail = widgets.ContentAvail();

            ui::Rect toolbar{origin.x, origin.y, avail.x, kToolbarH};
            float bodyY = origin.y + kToolbarH + kGap;
            float bodyH = std::max(48.0f, avail.y - kToolbarH - kGap - kFooterH - 2.0f);
            ui::Rect footerRect{origin.x, bodyY + bodyH + 2.0f, avail.x, kFooterH};

            float treeW = ui::Clamp(m_TreeWidth, 120.0f, std::max(120.0f, avail.x - 80.0f));
            ui::Rect treeRect{origin.x, bodyY, treeW, bodyH};
            ui::Rect splitRect{origin.x + treeW, bodyY, kSplitW, bodyH};
            float rightX = origin.x + treeW + kSplitW + 2.0f;
            float rightW = std::max(48.0f, avail.x - treeW - kSplitW - 2.0f);
            ui::Rect pathBar{rightX, bodyY, rightW, kPathBarH};
            ui::Rect gridRect{rightX, bodyY + kPathBarH + 1.0f, rightW, std::max(24.0f, bodyH - kPathBarH - 1.0f)};

            DrawToolbar(context, widgets, toolbar);
            DragTreeSplitter(widgets, splitRect, origin.x, avail.x);

            if (gridRect.Contains(widgets.MousePos().x, widgets.MousePos().y) &&
                widgets.Ctrl() && widgets.MouseWheel() != 0.0f) {
                m_ThumbnailSize = ui::Clamp(m_ThumbnailSize + widgets.MouseWheel() * 10.0f, 36.0f, 160.0f);
                widgets.AbsorbMouseWheel();
            }

            widgets.Background(treeRect, ui::kTheme.title);
            {
                ui::Child tree(widgets, "FolderTree", treeRect, true);
                DrawFolderTree(context, widgets, context.projectDirectory);
            }
            DrawPathBar(context, widgets, pathBar);
            {
                ui::Child grid(widgets, "AssetGrid", gridRect, true);
                DrawGrid(context, widgets, gridRect);
            }
            if (m_WantContext) widgets.OpenPopup("CB_Context");
            DrawContextMenu(context, widgets);
            DrawFooter(context, widgets, footerRect);
            context.PinMaterialFromSelection();
        }

    private:
        std::string m_Search;
        float m_ThumbnailSize = 72.0f;
        float m_TreeWidth = 200.0f;
        int m_LastClickedIndex = -1;
        std::string m_RenameBuffer;
        std::string m_ContextPath;
        bool m_ContextIsDir = false;
        bool m_SplitDragging = false;
        bool m_OpenMatEditorOnRelease = false;
        bool m_WantContext = false;
        std::string m_PendingSingleSelect;
        std::unordered_map<std::string, std::shared_ptr<BurnhopeTexture>> m_ImageThumbs;

        static std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        static std::string DisplayName(const fs::path& path, bool isDir) {
            return isDir ? path.filename().string() : path.stem().string();
        }

        static ui::Color IconColor(const fs::path& path, bool isDir) {
            if (isDir) return ui::Color::RGBA8(92, 168, 230);
            std::string ext = ToLower(path.extension().string());
            if (ext == ".bhmat" || ext == ".json") return ui::Color::RGBA8(210, 120, 70);
            if (ext == ".bhscene" || ext == ".burnscene") return ui::Color::RGBA8(120, 200, 110);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bhtex")
                return ui::Color::RGBA8(180, 110, 200);
            if (ext == ".bhmesh" || ext == ".bhmodel" || ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
                return ui::Color::RGBA8(90, 190, 170);
            return ui::kTheme.button;
        }

        void DrawFolderGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            ui::Color c = ui::Color::RGBA8(92, 168, 230);
            ui::Color tab = ui::Color::RGBA8(72, 140, 200);
            widgets.Background({r.x + 4.0f, r.y + r.h * 0.18f, r.w * 0.42f, r.h * 0.22f}, tab, 3.0f);
            widgets.Background({r.x + 4.0f, r.y + r.h * 0.32f, r.w - 8.0f, r.h * 0.54f}, c, 4.0f);
        }

        void DrawDocGlyph(ui::UIWidgets& widgets, ui::Rect r, ui::Color c) {
            widgets.Background({r.x + 6.0f, r.y + 4.0f, r.w - 12.0f, r.h - 8.0f}, c, 4.0f);
            widgets.Background({r.x + r.w - 16.0f, r.y + 4.0f, 10.0f, 10.0f}, ui::Color::RGBA8(20, 21, 26, 180), 2.0f);
        }

        void DrawMatGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            float s = std::min(r.w, r.h) * 0.72f;
            ui::Rect ball{r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s};
            widgets.Background(ball, ui::Color::RGBA8(210, 120, 70), s * 0.5f);
            widgets.Background({ball.x + s * 0.18f, ball.y + s * 0.16f, s * 0.34f, s * 0.22f},
                               ui::Color::RGBA8(255, 210, 160, 80), s * 0.2f);
        }

        void DrawMeshGlyph(ui::UIWidgets& widgets, ui::Rect r) {
            ui::Color side = ui::Color::RGBA8(70, 160, 145);
            ui::Color top = ui::Color::RGBA8(110, 200, 180);
            ui::Color front = ui::Color::RGBA8(90, 190, 170);
            float s = std::min(r.w, r.h);
            float cx = r.x + r.w * 0.5f;
            float cy = r.y + r.h * 0.52f;
            widgets.Background({cx - s * 0.28f, cy - s * 0.18f, s * 0.40f, s * 0.36f}, front, 3.0f);
            widgets.Background({cx - s * 0.08f, cy - s * 0.30f, s * 0.40f, s * 0.22f}, top, 3.0f);
            widgets.Background({cx + s * 0.12f, cy - s * 0.18f, s * 0.18f, s * 0.36f}, side, 3.0f);
        }

        std::shared_ptr<BurnhopeTexture> LoadImageThumb(UIContext& context, const std::string& pathStr) {
            auto it = m_ImageThumbs.find(pathStr);
            if (it != m_ImageThumbs.end()) return it->second;
            if (!context.device) return nullptr;
            try { m_ImageThumbs[pathStr] = BurnhopeTexture::createTextureFromFile(*context.device, pathStr); }
            catch (...) { m_ImageThumbs[pathStr] = nullptr; }
            return m_ImageThumbs[pathStr];
        }

        void DrawFileIcon(UIContext& context, ui::UIWidgets& widgets, ui::Rect icon, const fs::path& path, bool isDir) {
            widgets.Background(icon, ui::kTheme.input, 5.0f);
            if (isDir) { DrawFolderGlyph(widgets, icon); return; }
            std::string ext = ToLower(path.extension().string());
            std::string pathStr = path.string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bhtex") {
                if (auto tex = LoadImageThumb(context, pathStr)) {
                    widgets.ImageAt(icon, tex->getImageView(), tex->getSampler(), 5.0f);
                    return;
                }
                if (ext == ".bhtex") {
                    for (const char* srcExt : {".png", ".jpg", ".jpeg", ".tga"}) {
                        fs::path src = path;
                        src.replace_extension(srcExt);
                        if (!fs::exists(src)) continue;
                        if (auto tex = LoadImageThumb(context, src.string())) {
                            widgets.ImageAt(icon, tex->getImageView(), tex->getSampler(), 5.0f);
                            return;
                        }
                    }
                }
            }
            if ((ext == ".bhmat" || ext == ".json") && context.materialPreview) {
                if (BurnhopeTexture* thumb = context.materialPreview->Thumb(pathStr)) {
                    widgets.ImageAt(icon, thumb->getImageView(), thumb->getSampler(), 5.0f,
                                    {1, 1, 1, 1}, context.materialPreview->PreviewLayout());
                    return;
                }
                DrawMatGlyph(widgets, icon);
                return;
            }
            if (ext == ".bhmat" || ext == ".json") { DrawMatGlyph(widgets, icon); return; }
            if (ext == ".bhmesh" || ext == ".bhmodel" || ext == ".obj" || ext == ".fbx" ||
                ext == ".gltf" || ext == ".glb") {
                DrawMeshGlyph(widgets, icon);
                return;
            }
            DrawDocGlyph(widgets, icon, IconColor(path, false));
        }

        void HandleHotkeys(UIContext& context, ui::UIWidgets& widgets) {
            if (context.renamingPath.empty() && widgets.KeyPressed(SDL_SCANCODE_DELETE)) {
                DeleteSelected(context);
            }
            if (context.renamingPath.empty() && widgets.KeyPressed(SDL_SCANCODE_F2) &&
                context.selectedAssets.size() == 1) {
                StartRename(context, context.selectedAssets.front());
            }
            if (!context.selectedAssets.empty() && widgets.Ctrl() &&
                widgets.KeyPressed(SDL_SCANCODE_C)) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = false;
            }
            if (!context.selectedAssets.empty() && widgets.Ctrl() &&
                widgets.KeyPressed(SDL_SCANCODE_X)) {
                context.clipboardPaths = context.selectedAssets;
                context.isCut = true;
            }
            if (widgets.Ctrl() && widgets.KeyPressed(SDL_SCANCODE_V) &&
                !context.clipboardPaths.empty()) {
                PasteCopiedItems(context, context.currentDirectory);
            }
            if (widgets.Ctrl() && widgets.KeyPressed(SDL_SCANCODE_D) &&
                context.selectedAssets.size() == 1) {
                DuplicatePath(context, context.selectedAssets.front());
            }
        }

        void DeleteSelected(UIContext& context) {
            for (const auto& p : context.selectedAssets) {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
            context.selectedAssets.clear();
            context.renamingPath.clear();
            m_LastClickedIndex = -1;
        }

        fs::path UniquePath(const fs::path& dir, const std::string& base, const std::string& ext) {
            fs::path result = dir / (base + ext);
            uint32_t suffix = 1;
            while (fs::exists(result)) {
                result = dir / (base + " " + std::to_string(suffix++) + ext);
            }
            return result;
        }

        void StartRename(UIContext& context, const std::string& path) {
            context.renamingPath = path;
            m_RenameBuffer = fs::path(path).stem().string();
        }

        void ApplyRename(UIContext& context) {
            if (context.renamingPath.empty() || m_RenameBuffer.empty()) {
                context.renamingPath.clear();
                return;
            }
            fs::path oldPath(context.renamingPath);
            fs::path newPath = oldPath.parent_path() / (m_RenameBuffer + oldPath.extension().string());
            std::error_code ec;
            if (oldPath != newPath && !fs::exists(newPath)) fs::rename(oldPath, newPath, ec);
            if (!ec) {
                for (auto& selected : context.selectedAssets) {
                    if (selected == context.renamingPath) selected = newPath.string();
                }
            }
            context.renamingPath.clear();
        }

        void DuplicatePath(UIContext& context, const std::string& source) {
            fs::path src(source);
            if (!fs::exists(src)) return;
            fs::path dst = UniquePath(src.parent_path(), src.stem().string(), src.extension().string());
            std::error_code ec;
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            if (!ec) {
                context.selectedAssets = {dst.string()};
                StartRename(context, dst.string());
            }
        }

        void PasteCopiedItems(UIContext& context, const fs::path& targetDir) {
            for (const auto& source : context.clipboardPaths) {
                fs::path src(source);
                if (!fs::exists(src)) continue;
                fs::path dst = UniquePath(targetDir, src.stem().string(), src.extension().string());
                std::error_code ec;
                if (context.isCut) fs::rename(src, dst, ec);
                else fs::copy(src, dst, fs::copy_options::recursive, ec);
            }
            if (context.isCut) {
                context.clipboardPaths.clear();
                context.isCut = false;
            }
        }

        bool IsDescendantOf(const fs::path& child, const fs::path& ancestor) {
            std::error_code ec;
            auto rel = fs::relative(child, ancestor, ec);
            if (ec) return false;
            std::string s = rel.generic_string();
            return !s.empty() && s != "." && !s.starts_with("..");
        }

        void MoveInto(UIContext& context, const fs::path& destDir, const std::string& draggedPath) {
            if (!fs::is_directory(destDir)) return;
            std::vector<std::string> toMove = context.selectedAssets;
            if (std::find(toMove.begin(), toMove.end(), draggedPath) == toMove.end()) {
                toMove = {draggedPath};
            }
            for (const auto& srcStr : toMove) {
                fs::path src(srcStr);
                if (!fs::exists(src)) continue;
                if (src == destDir) continue;
                if (fs::is_directory(src) && IsDescendantOf(destDir, src)) continue;
                fs::path dst = UniquePath(destDir, src.stem().string(), src.extension().string());
                if (src.filename() == dst.filename() && src.parent_path() == destDir) continue;
                std::error_code ec;
                fs::rename(src, dst, ec);
            }
            context.selectedAssets.clear();
        }

        void NavigateTo(UIContext& context, const fs::path& target) {
            if (context.currentDirectory == target) return;
            if (context.dirHistoryIndex < static_cast<int>(context.dirHistory.size()) - 1) {
                context.dirHistory.erase(context.dirHistory.begin() + context.dirHistoryIndex + 1, context.dirHistory.end());
            }
            context.dirHistory.push_back(target);
            context.dirHistoryIndex++;
            context.currentDirectory = target;
            context.selectedAssets.clear();
            m_LastClickedIndex = -1;
        }

        void CreateFolder(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Folder", "");
            std::error_code ec;
            fs::create_directory(newPath, ec);
            if (ec) return;
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void CreateMaterial(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Material", ".bhmat");
            json j;
            j["name"] = "New Material";
            std::ofstream file(newPath);
            if (!file) return;
            file << j.dump(4);
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void CreateScene(UIContext& context) {
            fs::path newPath = UniquePath(context.currentDirectory, "New Scene", ".bhscene");
            std::ofstream file(newPath);
            if (!file) return;
            file << "{\"version\":1,\"entities\":[]}";
            context.selectedAssets = {newPath.string()};
            StartRename(context, newPath.string());
        }

        void DrawCreateItems(UIContext& context, ui::UIWidgets& widgets) {
            if (widgets.MenuItem("New Folder")) CreateFolder(context);
            if (widgets.MenuItem("Material (.bhmat)")) CreateMaterial(context);
            if (widgets.MenuItem("Scene (.bhscene)")) CreateScene(context);
        }

        void DragTreeSplitter(ui::UIWidgets& widgets, ui::Rect handle, float treeLeft, float totalWidth) {
            widgets.InvisibleHit("##cb_split", handle);
            bool hovered = widgets.IsMouseOverItem() || m_SplitDragging;
            if (hovered) widgets.RequestCursor(ui::UIWidgets::MouseCursor::EwResize);
            widgets.Background({handle.x + handle.w * 0.5f - 1.0f, handle.y, 2.0f, handle.h},
                               hovered ? ui::kTheme.splitterHover : ui::kTheme.splitter);
            if (widgets.IsMouseOverItem() && widgets.MouseDown(0) && !widgets.IsDragDropActive()) {
                m_SplitDragging = true;
            }
            if (m_SplitDragging && widgets.MouseDown(0)) {
                widgets.RequestCursor(ui::UIWidgets::MouseCursor::EwResize);
                m_TreeWidth = ui::Clamp(widgets.MousePos().x - treeLeft, 120.0f, std::max(120.0f, totalWidth - 80.0f));
            }
            if (!widgets.MouseDown(0)) m_SplitDragging = false;
        }

        void AcceptFolderDrop(UIContext& context, ui::UIWidgets& widgets, const fs::path& destDir) {
            if (widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    if (const std::string* path = std::any_cast<std::string>(payload)) {
                        MoveInto(context, destDir, *path);
                    }
                }
                widgets.EndDragDropTarget();
            }
        }

        void CollectSearchItems(const fs::path& root, const std::string& query,
                                std::vector<fs::directory_entry>& out) {
            std::error_code ec;
            fs::recursive_directory_iterator it(
                root, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                const auto& entry = *it;
                if (entry.path() == root) continue;
                std::string name = ToLower(entry.path().filename().string());
                std::string stem = ToLower(entry.path().stem().string());
                if (name.find(query) != std::string::npos || stem.find(query) != std::string::npos) {
                    out.push_back(entry);
                }
            }
        }

        void DrawToolbar(UIContext& context, ui::UIWidgets& widgets, ui::Rect toolbar) {
            widgets.SetCursor({toolbar.x, toolbar.y + 2.0f});
            if (widgets.Button("+", {28, 24})) widgets.OpenPopup("CreateMenuPopup");
            if (ui::Popup create(widgets, "CreateMenuPopup"); create) {
                DrawCreateItems(context, widgets);
            }

            const float searchW = ui::Clamp(toolbar.w * 0.42f, 220.0f, std::max(220.0f, toolbar.w - 48.0f));
            widgets.SetCursor({toolbar.x + toolbar.w - searchW, toolbar.y + 2.0f});
            widgets.InputText("Search...", m_Search, 128, {searchW, 24});
        }

        void DrawPathBar(UIContext& context, ui::UIWidgets& widgets, ui::Rect bar) {
            widgets.Background(bar, ui::kTheme.title);
            widgets.Background({bar.x, bar.y + bar.h - 1.0f, bar.w, 1.0f}, ui::kTheme.border);
            widgets.PushID("PathBar");
            widgets.SetCursor({bar.x + 6.0f, bar.y + 2.0f});

            const bool canBack = context.dirHistoryIndex > 0;
            const bool canFwd = context.dirHistoryIndex < static_cast<int>(context.dirHistory.size()) - 1;
            if (widgets.Button("<", {28, 24}) && canBack) {
                context.dirHistoryIndex--;
                context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
            }
            widgets.SameLine(4.0f);
            if (widgets.Button(">", {28, 24}) && canFwd) {
                context.dirHistoryIndex++;
                context.currentDirectory = context.dirHistory[context.dirHistoryIndex];
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
            }
            widgets.SameLine(8.0f);
            if (widgets.Button("Project", {0, 24})) NavigateTo(context, context.projectDirectory);
            AcceptFolderDrop(context, widgets, context.projectDirectory);

            std::error_code ec;
            fs::path rel = fs::relative(context.currentDirectory, context.projectDirectory, ec);
            const bool inside = !ec && IsDescendantOf(context.currentDirectory, context.projectDirectory);
            if (inside && !rel.empty() && rel.generic_string() != ".") {
                fs::path accum = context.projectDirectory;
                for (const auto& part : rel) {
                    std::string name = part.generic_string();
                    if (name.empty() || name == "." || name == ".." || name == "...") continue;
                    accum /= part;
                    widgets.SameLine(4.0f);
                    widgets.Text(">", ui::kTheme.textMuted);
                    widgets.SameLine(4.0f);
                    if (widgets.Button(name, {0, 24})) NavigateTo(context, accum);
                    AcceptFolderDrop(context, widgets, accum);
                }
            }
            widgets.PopID();
        }

        void DrawFolderTree(UIContext& context, ui::UIWidgets& widgets, const fs::path& dir) {
            std::error_code ec;
            bool hasChildDir = false;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_directory()) { hasChildDir = true; break; }
            }
            std::string name = (dir == context.projectDirectory) ? "Project" : dir.filename().string();
            ui::ID id(widgets, dir.string());
            bool selected = context.currentDirectory == dir;
            ui::Tree tree(widgets, name, selected, !hasChildDir, dir == context.projectDirectory);
            if (widgets.WasItemClicked()) NavigateTo(context, dir);
            if (widgets.WasItemRightClicked()) {
                m_ContextPath = dir.string();
                m_ContextIsDir = true;
                m_WantContext = true;
                context.selectedAssets = {dir.string()};
            }
            if (widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    if (const std::string* path = std::any_cast<std::string>(payload)) {
                        MoveInto(context, dir, *path);
                    }
                }
                widgets.EndDragDropTarget();
            }
            if (tree) {
                for (const auto& entry : fs::directory_iterator(dir, ec)) {
                    if (entry.is_directory()) DrawFolderTree(context, widgets, entry.path());
                }
            }
        }

        void ApplyClickSelection(UIContext& context, ui::UIWidgets& widgets,
                                 const std::vector<fs::directory_entry>& items,
                                 int index, const std::string& pathStr) {
            if (widgets.Shift() && m_LastClickedIndex >= 0 && !items.empty()) {
                int a = std::min(m_LastClickedIndex, index);
                int b = std::max(m_LastClickedIndex, index);
                a = std::max(0, a);
                b = std::min(static_cast<int>(items.size()) - 1, b);
                context.selectedAssets.clear();
                for (int i = a; i <= b; ++i) {
                    context.selectedAssets.push_back(items[i].path().string());
                }
            } else if (widgets.Ctrl()) {
                auto it = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr);
                if (it != context.selectedAssets.end()) context.selectedAssets.erase(it);
                else context.selectedAssets.push_back(pathStr);
                m_LastClickedIndex = index;
            } else {
                context.selectedAssets = {pathStr};
                m_LastClickedIndex = index;
            }
        }

        void DrawGrid(UIContext& context, ui::UIWidgets& widgets, ui::Rect gridRect) {
            std::vector<fs::directory_entry> items;
            std::error_code ec;
            std::string query = ToLower(m_Search);
            if (!query.empty()) {
                CollectSearchItems(context.currentDirectory, query, items);
            } else {
                for (const auto& entry : fs::directory_iterator(context.currentDirectory, ec)) {
                    items.push_back(entry);
                }
            }

            std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (a.is_directory() != b.is_directory()) return a.is_directory();
                return a.path().filename().string() < b.path().filename().string();
            });

            widgets.InvisibleHit("##grid_bg", gridRect);
            const bool bgHovered = widgets.IsMouseOverItem();
            bool hitCell = false;
            bool renameFieldHovered = false;

            if (widgets.WasItemRightClicked() && bgHovered) {
                m_ContextPath.clear();
                m_ContextIsDir = false;
                m_WantContext = true;
            }

            m_ThumbnailSize = ui::Clamp(m_ThumbnailSize, 36.0f, 160.0f);
            glm::vec2 avail = widgets.ContentAvail();
            const float cellW = m_ThumbnailSize + 16.0f;
            const float cellH = m_ThumbnailSize + 34.0f;
            const float gap = 8.0f;
            const int cols = std::max(1, static_cast<int>((avail.x + gap) / (cellW + gap)));
            glm::vec2 origin = widgets.GetCursor();
            ui::Rect clip = widgets.ClipRect();
            int col = 0;
            int row = 0;

            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const auto& item = items[i];
                std::string pathStr = item.path().string();
                bool isDir = item.is_directory();
                bool selected = std::find(context.selectedAssets.begin(), context.selectedAssets.end(), pathStr)
                    != context.selectedAssets.end();

                float x = origin.x + col * (cellW + gap);
                float y = origin.y + row * (cellH + gap);
                ui::Rect cell{x, y, cellW, cellH};

                if (cell.Intersect(clip).h > 1.0f && cell.Intersect(clip).w > 1.0f) {
                    widgets.PushID(pathStr);
                    bool renaming = context.renamingPath == pathStr;

                    ui::Rect icon{x + 8.0f, y + 4.0f, cellW - 16.0f, m_ThumbnailSize};
                    ui::Rect nameRect{x + 4.0f, y + 6.0f + m_ThumbnailSize, cellW - 8.0f, 24.0f};

                    bool hovered = false;
                    if (renaming) {
                        widgets.SetCursor({nameRect.x, nameRect.y});
                        widgets.InputText("##rename", m_RenameBuffer, 128, {nameRect.w, 22});
                        renameFieldHovered = widgets.IsMouseOverItem();
                        if (widgets.KeyPressed(SDL_SCANCODE_RETURN)) ApplyRename(context);
                        if (widgets.KeyPressed(SDL_SCANCODE_ESCAPE)) context.renamingPath.clear();
                    } else {
                        bool clicked = widgets.InvisibleHit("##cell", cell);
                        hovered = widgets.IsMouseOverItem();
                        if (hovered) hitCell = true;
                        if (clicked) {
                            hitCell = true;
                            if (widgets.Shift() || widgets.Ctrl() || !selected) {
                                ApplyClickSelection(context, widgets, items, i, pathStr);
                                m_PendingSingleSelect.clear();
                            } else {
                                m_LastClickedIndex = i;
                                m_PendingSingleSelect = pathStr;
                            }
                            std::string ext = ToLower(item.path().extension().string());
                            m_OpenMatEditorOnRelease = (ext == ".bhmat" || ext == ".json");
                        }
                        if (widgets.IsItemDoubleClicked()) {
                            hitCell = true;
                            m_PendingSingleSelect.clear();
                            if (isDir) NavigateTo(context, item.path());
                            else if (item.path().extension() == ".bhscene" ||
                                     item.path().extension() == ".json") {
                                context.pendingSceneLoadPath = pathStr;
                            }
                        }
                        if (widgets.WasItemRightClicked()) {
                            hitCell = true;
                            m_PendingSingleSelect.clear();
                            if (!selected) {
                                context.selectedAssets = {pathStr};
                                m_LastClickedIndex = i;
                            }
                            m_ContextPath = pathStr;
                            m_ContextIsDir = isDir;
                            m_WantContext = true;
                        }
                        if (widgets.BeginDragDropSource()) {
                            m_OpenMatEditorOnRelease = false;
                            m_PendingSingleSelect.clear();
                            widgets.SetDragDropPayload("CONTENT_BROWSER_ITEM", pathStr);
                            widgets.EndDragDropSource();
                        }
                        if (isDir && widgets.BeginDragDropTarget()) {
                            if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                if (const std::string* path = std::any_cast<std::string>(payload)) {
                                    MoveInto(context, item.path(), *path);
                                }
                            }
                            widgets.EndDragDropTarget();
                        }
                    }

                    const glm::vec2 mouse = widgets.MousePos();
                    bool cellHot = cell.Contains(mouse.x, mouse.y) && clip.Contains(mouse.x, mouse.y);
                    if (cellHot) {
                        hovered = true;
                        hitCell = true;
                        widgets.SetTooltip(item.path().filename().string());
                    }

                    ui::Color bg = selected ? ui::kTheme.rowSelected
                                 : (hovered ? ui::kTheme.rowHover : ui::Color{0, 0, 0, 0});
                    if (bg.a > 0.0f) widgets.Background(cell, bg, 4.0f);
                    DrawFileIcon(context, widgets, icon, item.path(), isDir);
                    if (!renaming) {
                        widgets.SetCursor({nameRect.x, nameRect.y});
                        widgets.TextClippedCentered(DisplayName(item.path(), isDir), nameRect.w, ui::kTheme.text);
                    }

                    widgets.PopID();
                }

                ++col;
                if (col >= cols) { col = 0; ++row; }
            }
            int usedRows = row + (col > 0 ? 1 : 0);
            widgets.SetCursor({origin.x, origin.y + usedRows * (cellH + gap)});
            widgets.Dummy({1.0f, 4.0f});

            if (m_OpenMatEditorOnRelease && widgets.MouseReleased(0) && !widgets.IsDragDropActive()) {
                context.requestActivateWindow = "Material Editor";
                m_OpenMatEditorOnRelease = false;
            }
            if (!m_PendingSingleSelect.empty() && widgets.MouseReleased(0) && !widgets.IsDragDropActive()) {
                context.selectedAssets = {m_PendingSingleSelect};
                m_PendingSingleSelect.clear();
            }
            if (!widgets.MouseDown(0)) {
                m_OpenMatEditorOnRelease = false;
                m_PendingSingleSelect.clear();
            }

            if (!hitCell && bgHovered && widgets.MouseClicked(0) && !widgets.IsDragDropActive()) {
                context.selectedAssets.clear();
                m_LastClickedIndex = -1;
            }
            if (!context.renamingPath.empty() && widgets.MouseClicked(0) && !renameFieldHovered) {
                ApplyRename(context);
            }
        }

        void DrawFooter(UIContext& context, ui::UIWidgets& widgets, ui::Rect footer) {
            widgets.Background(footer, ui::kTheme.title);
            widgets.Background({footer.x, footer.y, footer.w, 1.0f}, ui::kTheme.border);

            std::string label;
            ui::Color iconCol = ui::kTheme.button;
            if (context.selectedAssets.size() == 1) {
                fs::path p(context.selectedAssets.front());
                bool isDir = fs::is_directory(p);
                label = p.filename().string();
                iconCol = IconColor(p, isDir);
            } else if (context.selectedAssets.size() > 1) {
                label = std::to_string(context.selectedAssets.size()) + " items selected";
            } else {
                label = fs::relative(context.currentDirectory, context.projectDirectory).string();
                if (label.empty() || label == ".") label = "Project";
                iconCol = IconColor(context.currentDirectory, true);
            }

            ui::Rect iconR{footer.x + 6.0f, footer.y + 5.0f, 18.0f, 18.0f};
            widgets.Background(iconR, iconCol, 3.0f);

            const float sliderW = 96.0f;
            widgets.SetCursor({footer.x + 30.0f, footer.y + 2.0f});
            widgets.TextClipped(label, std::max(40.0f, footer.w - sliderW - 40.0f), ui::kTheme.text);

            widgets.SetCursor({footer.x + footer.w - sliderW - 10.0f, footer.y + 6.0f});
            widgets.SliderFloat("##zoom", &m_ThumbnailSize, 36.0f, 160.0f, {sliderW, 16.0f});
        }

        void DrawContextMenu(UIContext& context, ui::UIWidgets& widgets) {
            if (ui::Popup ctx(widgets, "CB_Context"); ctx) {
                DrawCreateItems(context, widgets);
                if (!m_ContextPath.empty()) {
                    widgets.Separator();
                    if (widgets.MenuItem("Rename", "F2")) StartRename(context, m_ContextPath);
                    if (widgets.MenuItem("Duplicate", "Ctrl+D")) DuplicatePath(context, m_ContextPath);
                    if (widgets.MenuItem("Copy", "Ctrl+C")) {
                        context.clipboardPaths = context.selectedAssets.empty()
                            ? std::vector<std::string>{m_ContextPath} : context.selectedAssets;
                        context.isCut = false;
                    }
                    if (widgets.MenuItem("Cut", "Ctrl+X")) {
                        context.clipboardPaths = context.selectedAssets.empty()
                            ? std::vector<std::string>{m_ContextPath} : context.selectedAssets;
                        context.isCut = true;
                    }
                    if (widgets.MenuItem("Delete", "Del")) {
                        if (context.selectedAssets.empty()) context.selectedAssets = {m_ContextPath};
                        DeleteSelected(context);
                    }
                }
                if (!context.clipboardPaths.empty()) {
                    widgets.Separator();
                    if (widgets.MenuItem("Paste", "Ctrl+V")) {
                        PasteCopiedItems(context, context.currentDirectory);
                    }
                }
            }
        }
    };
}
