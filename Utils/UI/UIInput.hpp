#pragma once
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <set>

// Minimal SDL3 input tracker for the immediate-mode UI (replaces
// ImGui_ImplSDL3_ProcessEvent + ImGuiIO). Deliberately small: just enough
// state for hover/active/click tracking and keyboard shortcuts.
namespace burnhope::ui {

    class UIInput {
    public:
        void BeginFrame() {
            for (auto& c : m_MouseClicked) c = false;
            for (auto& c : m_MouseReleased) c = false;
            for (auto& c : m_MouseDoubleClicked) c = false;
            m_KeysPressedThisFrame.clear();
            m_MouseWheel = 0.0f;
            m_MouseDelta = {0.0f, 0.0f};
            m_TextInput.clear();
        }

        void ProcessEvent(const SDL_Event& event) {
            switch (event.type) {
                case SDL_EVENT_MOUSE_MOTION:
                    m_MousePos = {event.motion.x, event.motion.y};
                    if (m_SkipNextMouseDelta) m_SkipNextMouseDelta = false;
                    else m_MouseDelta += glm::vec2(event.motion.xrel, event.motion.yrel);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button - 1 < m_MouseDown.size()) {
                        m_MouseDown[event.button.button - 1] = true;
                        m_MouseClicked[event.button.button - 1] = true;
                        m_MouseDoubleClicked[event.button.button - 1] =
                            event.button.clicks >= 2;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button - 1 < m_MouseDown.size()) {
                        m_MouseDown[event.button.button - 1] = false;
                        m_MouseReleased[event.button.button - 1] = true;
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    m_MouseWheel += event.wheel.y;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (!event.key.repeat) m_KeysPressedThisFrame.insert(event.key.scancode);
                    m_KeysDown[event.key.scancode] = true;
                    m_CtrlDown = (event.key.mod & SDL_KMOD_CTRL) != 0;
                    m_ShiftDown = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                    break;
                case SDL_EVENT_KEY_UP:
                    m_KeysDown[event.key.scancode] = false;
                    m_CtrlDown = (event.key.mod & SDL_KMOD_CTRL) != 0;
                    m_ShiftDown = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    m_TextInput += event.text.text;
                    break;
                default: break;
            }
        }

        // SDL_BUTTON_* minus 1: 0 = left, 1 = middle, 2 = right.
        static constexpr int kLeft = 0;
        static constexpr int kMiddle = 1;
        static constexpr int kRight = 2;

        glm::vec2 MousePos() const { return m_MousePos; }
        glm::vec2 MouseDelta() const { return m_MouseDelta; }
        void SkipNextMouseDelta() { m_SkipNextMouseDelta = true; }
        void SetMousePos(glm::vec2 p) { m_MousePos = p; }
        bool MouseDown(int button = 0) const { return button < (int)m_MouseDown.size() && m_MouseDown[button]; }
        bool MouseClicked(int button = 0) const { return button < (int)m_MouseClicked.size() && m_MouseClicked[button]; }
        bool MouseReleased(int button = 0) const { return button < (int)m_MouseReleased.size() && m_MouseReleased[button]; }
        bool MouseDoubleClicked(int button = 0) const { return button < (int)m_MouseDoubleClicked.size() && m_MouseDoubleClicked[button]; }
        bool MouseRightDown() const { return MouseDown(kRight); }
        bool MouseRightClicked() const { return MouseClicked(kRight); }
        bool MouseRightReleased() const { return MouseReleased(kRight); }
        float MouseWheel() const { return m_MouseWheel; }

        bool KeyDown(SDL_Scancode key) const { auto it = m_KeysDown.find(key); return it != m_KeysDown.end() && it->second; }
        bool KeyPressed(SDL_Scancode key) const { return m_KeysPressedThisFrame.count(key) > 0; }
        bool Ctrl() const { return m_CtrlDown; }
        bool Shift() const { return m_ShiftDown; }
        const std::string& TextInput() const { return m_TextInput; }

        // Immediate-mode hot/active widget id tracking (id = hash of label).
        void SetHot(uint64_t id) { m_HotId = id; }
        void ClearHot() { m_HotId = 0; }
        void SetActive(uint64_t id) { m_ActiveId = id; }
        uint64_t Hot() const { return m_HotId; }
        uint64_t Active() const { return m_ActiveId; }
        bool IsActive(uint64_t id) const { return m_ActiveId == id; }
        bool IsHot(uint64_t id) const { return m_HotId == id; }
        void ClearActive() { m_ActiveId = 0; }

    private:
        glm::vec2 m_MousePos{0.0f};
        glm::vec2 m_MouseDelta{0.0f};
        bool m_SkipNextMouseDelta = false;
        std::array<bool, 5> m_MouseDown{};
        std::array<bool, 5> m_MouseClicked{};
        std::array<bool, 5> m_MouseReleased{};
        std::array<bool, 5> m_MouseDoubleClicked{};
        float m_MouseWheel = 0.0f;

        std::unordered_map<SDL_Scancode, bool> m_KeysDown;
        std::set<SDL_Scancode> m_KeysPressedThisFrame;
        bool m_CtrlDown = false;
        bool m_ShiftDown = false;
        std::string m_TextInput;

        uint64_t m_HotId = 0;
        uint64_t m_ActiveId = 0;
    };
}
