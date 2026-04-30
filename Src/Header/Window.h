#ifndef WINDOW_CLASS_H
#define WINDOW_CLASS_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <iostream>
#include <stb_image.h>
// Структура для удобной передачи всех настроек окна
struct WindowSettings {
    std::string title = "BurnHope Engine";
    int width = 1920;
    int height = 1080;
    bool vSync = false;
    bool fullscreen = false;
    bool resizable = true;
    int samples = 4;

    // --- НОВЫЕ КЛАССНЫЕ НАСТРОЙКИ ---
    bool decorated = true;       // Показывать ли рамку Windows
    bool maximized = false;      // Запускать ли сразу развернутым на весь экран
    bool startHidden = true;    // Спрятать окно при запуске (для долгой загрузки)
    bool floating = false;       // Поверх всех окон
};

class Window {
public:
    GLFWwindow* window = nullptr;
    WindowSettings settings;

    // Конструктор берет настройки. Если ничего не передать, использует стандартные
    Window(const WindowSettings& props = WindowSettings()) {
        Init(props);
    }

    // В деструкторе аккуратно всё подчищаем
    ~Window() {
        if (window) {
            glfwDestroyWindow(window);
        }
        // glfwTerminate() лучше вызывать в Main.cpp при выходе из программы,
        // но если у тебя окно живет всё время работы движка, можно оставить и тут.
    }

    void Init(const WindowSettings& props) {
        settings = props;

        if (!glfwInit()) {
            std::cout << "[ERROR] Не удалось инициализировать GLFW!" << std::endl;
            return;
        }

        // Настраиваем профиль OpenGL
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Применяем наши гибкие настройки
        glfwWindowHint(GLFW_RESIZABLE, settings.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_SAMPLES, settings.samples);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);

        glfwWindowHint(GLFW_DECORATED, settings.decorated ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_MAXIMIZED, settings.maximized ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, settings.startHidden ? GLFW_FALSE : GLFW_TRUE);
        glfwWindowHint(GLFW_FLOATING, settings.floating ? GLFW_TRUE : GLFW_FALSE);
        
        // Если полноэкранный режим — запрашиваем основной монитор
        GLFWmonitor* monitor = settings.fullscreen ? glfwGetPrimaryMonitor() : nullptr;

        // Создаем окно
        window = glfwCreateWindow(settings.width, settings.height, settings.title.c_str(), monitor, nullptr);
        if (!window) {
            std::cout << "[ERROR] Не удалось создать окно GLFW!" << std::endl;
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(window);

        // Загружаем OpenGL функции
        if (!gladLoadGL()) {
            std::cout << "[ERROR] Не удалось инициализировать GLAD!" << std::endl;
            return;
        }

        // Применяем настройки VSync
        SetVSync(settings.vSync);

        // Привязываем указатель на этот класс к окну GLFW, чтобы коллбеки могли получить к нему доступ
        glfwSetWindowUserPointer(window, this);

        // Устанавливаем коллбек, который будет вызываться при изменении размера окна
        glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

        // Базовые настройки OpenGL
        glViewport(0, 0, settings.width, settings.height);
        glEnable(GL_DEPTH_TEST);

        if (settings.samples > 1) {
            glEnable(GL_MULTISAMPLE);
        }
    }
    // Показать окно, когда всё загрузилось
    void Show() {
        glfwShowWindow(window);
    }
    void SetIcon(const std::string& imagePath) {
        if (!window) return; // На всякий случай проверяем, что окно существует

        GLFWimage images[1];
        // Загружаем картинку: принудительно запрашиваем 4 канала (RGBA)
        images[0].pixels = stbi_load(imagePath.c_str(), &images[0].width, &images[0].height, 0, 4);

        if (images[0].pixels) {
            // Передаем иконку в GLFW
            glfwSetWindowIcon(window, 1, images);
            // Обязательно освобождаем память, GLFW уже скопировал данные себе
            stbi_image_free(images[0].pixels);
        }
        else {
            std::cout << "[WARNING] Не удалось загрузить иконку окна: " << imagePath << std::endl;
        }
    }
    // Спрятать курсор и заблокировать его для вращения 3D камеры
    void LockCursor(bool lock) {
        if (lock) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
    // Метод для включения/выключения вертикальной синхронизации "на лету"
    void SetVSync(bool enable) {
        settings.vSync = enable;
        glfwSwapInterval(enable ? 1 : 0);
    }

    // Удобная проверка, не пора ли закрываться
    bool ShouldClose() const {
        return glfwWindowShouldClose(window);
    }

private:
    // Статическая функция-коллбек для GLFW
    static void FramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        // Достаем указатель на наш класс Window
        Window* win = (Window*)glfwGetWindowUserPointer(glfwWindow);
        if (win) {
            win->settings.width = width;
            win->settings.height = height;
        }
        // Сообщаем видеокарте новый размер области для рисования
        glViewport(0, 0, width, height);
    }
};

#endif