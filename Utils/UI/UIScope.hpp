#pragma once
#include "UIWidgets.hpp"

// RAII scopes over the immediate-mode kernel. Editor/tools only — not the
// game hot path. Typical panel:
//
//   ui::Panel panel(widgets, "Inspector", contentRect);
//   widgets.Text("Hello");
//   if (ui::Popup p(widgets, "Add"); p) { widgets.MenuItem("Mesh"); }
//
// Pairing Begin/End by hand is still valid; these exist so a panel body can
// early-return without leaking a region/popup/id.
namespace burnhope::ui {

    class Panel {
    public:
        Panel(UIWidgets& w, std::string_view id, Rect rect,
              float padding = kRegionPad, bool scroll = true)
            : m_W(&w) {
            w.BeginRegion(id, rect, padding, scroll);
        }
        ~Panel() { if (m_W) m_W->EndRegion(); }
        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;
        Panel(Panel&& o) noexcept : m_W(o.m_W) { o.m_W = nullptr; }
        Panel& operator=(Panel&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
    };

    class Child {
    public:
        Child(UIWidgets& w, std::string_view id, Rect rect, bool scroll = true)
            : m_W(&w) {
            w.BeginChild(id, rect, scroll);
        }
        ~Child() { if (m_W) m_W->EndChild(); }
        Child(const Child&) = delete;
        Child& operator=(const Child&) = delete;
        Child(Child&& o) noexcept : m_W(o.m_W) { o.m_W = nullptr; }
        Child& operator=(Child&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
    };

    class Clip {
    public:
        Clip(UIWidgets& w, Rect rect, float padding = 4.0f)
            : m_W(&w) {
            w.BeginRegion("##clip", rect, padding, false);
        }
        ~Clip() { if (m_W) m_W->EndRegion(); }
        Clip(const Clip&) = delete;
        Clip& operator=(const Clip&) = delete;
        Clip(Clip&& o) noexcept : m_W(o.m_W) { o.m_W = nullptr; }
        Clip& operator=(Clip&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
    };

    class ID {
    public:
        ID(UIWidgets& w, std::string_view label) : m_W(&w) { w.PushID(label); }
        ID(UIWidgets& w, uint64_t id) : m_W(&w) { w.PushIDInt(id); }
        ~ID() { if (m_W) m_W->PopID(); }
        ID(const ID&) = delete;
        ID& operator=(const ID&) = delete;
        ID(ID&& o) noexcept : m_W(o.m_W) { o.m_W = nullptr; }
        ID& operator=(ID&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
    };

    class Tree {
    public:
        Tree(UIWidgets& w, std::string_view label, bool selected = false, bool leaf = false, bool defaultOpen = false)
            : m_W(&w), m_Open(w.TreeNode(label, selected, leaf, defaultOpen)) {}
        ~Tree() { if (m_W && m_Open) m_W->TreePop(); }
        explicit operator bool() const { return m_Open; }
        Tree(const Tree&) = delete;
        Tree& operator=(const Tree&) = delete;
        Tree(Tree&& o) noexcept : m_W(o.m_W), m_Open(o.m_Open) { o.m_W = nullptr; o.m_Open = false; }
        Tree& operator=(Tree&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
        bool m_Open = false;
    };

    class PropertyGrid {
    public:
        explicit PropertyGrid(UIWidgets& w) : m_W(&w) { w.BeginPropertyGrid(); }
        ~PropertyGrid() { if (m_W) m_W->EndPropertyGrid(); }
        PropertyGrid(const PropertyGrid&) = delete;
        PropertyGrid& operator=(const PropertyGrid&) = delete;
        PropertyGrid(PropertyGrid&& o) noexcept : m_W(o.m_W) { o.m_W = nullptr; }
        PropertyGrid& operator=(PropertyGrid&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
    };

    class Combo {
    public:
        Combo(UIWidgets& w, std::string_view label, std::string_view preview)
            : m_W(&w), m_Open(w.BeginCombo(label, preview)) {}
        ~Combo() { if (m_W && m_Open) m_W->EndCombo(); }
        explicit operator bool() const { return m_Open; }
        Combo(const Combo&) = delete;
        Combo& operator=(const Combo&) = delete;
        Combo(Combo&& o) noexcept : m_W(o.m_W), m_Open(o.m_Open) { o.m_W = nullptr; o.m_Open = false; }
        Combo& operator=(Combo&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
        bool m_Open = false;
    };

    class Popup {
    public:
        Popup(UIWidgets& w, std::string_view id)
            : m_W(&w), m_Open(w.BeginPopup(id)) {}
        ~Popup() { if (m_W && m_Open) m_W->EndPopup(); }
        explicit operator bool() const { return m_Open; }
        Popup(const Popup&) = delete;
        Popup& operator=(const Popup&) = delete;
        Popup(Popup&& o) noexcept : m_W(o.m_W), m_Open(o.m_Open) { o.m_W = nullptr; o.m_Open = false; }
        Popup& operator=(Popup&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
        bool m_Open = false;
    };

    class MenuBar {
    public:
        explicit MenuBar(UIWidgets& w) : m_W(&w), m_Open(w.BeginMenuBar()) {}
        ~MenuBar() { if (m_W && m_Open) m_W->EndMenuBar(); }
        explicit operator bool() const { return m_Open; }
        MenuBar(const MenuBar&) = delete;
        MenuBar& operator=(const MenuBar&) = delete;
        MenuBar(MenuBar&& o) noexcept : m_W(o.m_W), m_Open(o.m_Open) { o.m_W = nullptr; o.m_Open = false; }
        MenuBar& operator=(MenuBar&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
        bool m_Open = false;
    };

    class Menu {
    public:
        Menu(UIWidgets& w, std::string_view label)
            : m_W(&w), m_Open(w.BeginMenu(label)) {}
        ~Menu() { if (m_W && m_Open) m_W->EndMenu(); }
        explicit operator bool() const { return m_Open; }
        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;
        Menu(Menu&& o) noexcept : m_W(o.m_W), m_Open(o.m_Open) { o.m_W = nullptr; o.m_Open = false; }
        Menu& operator=(Menu&&) = delete;

    private:
        UIWidgets* m_W = nullptr;
        bool m_Open = false;
    };

} // namespace burnhope::ui
