#pragma once
#include "UIContext.h"
#include "UI/UI.hpp"
#include <string>

namespace burnhope {
    class IUIWindow {
    public:
        std::string m_Name;
        bool m_IsOpen = true;

        IUIWindow(const std::string& name) : m_Name(name) {}
        virtual ~IUIWindow() = default;

        // `contentRect` is the panel's content area, computed by UIDockspace
        // for whichever dock slot this panel is the active tab of.
        virtual void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) = 0;
    };
}