#pragma once
#include "UIContext.h"
#include <string>

namespace burnhope {
    class IUIWindow {
    public:
        std::string m_Name;
        bool m_IsOpen = true;

        IUIWindow(const std::string& name) : m_Name(name) {}
        virtual ~IUIWindow() = default;

        virtual void Draw(UIContext& context) = 0;
    };
}