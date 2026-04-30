#pragma once
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <glad/glad.h>
#include "Texture.h" // Твой класс BHTexHeader
#include "TextureImporter.h"

// Задача на загрузку одной текстуры
struct TextureLoadTask {
    std::string filepath;
    GLuint textureID; // ID "скелета" текстуры, куда мы зальем данные
    uint64_t bindlessHandle;
    Texture* targetTexture;
    // Данные, которые заполнит фоновый поток
    BHTexHeader header;
    std::vector<char> rawData;
    std::vector<uint32_t> mipSizes;
    std::vector<size_t> mipOffsets; // <--- НОВОЕ: Сдвиг в байтах для каждого мипмапа
    bool isStorageAllocated = false;
    int currentMipToUpload; // <--- НОВОЕ: Какой мип грузим сейчас
    std::atomic<bool> isReadyForGPU{ false };
};

class TextureStreamer {
public:
    TextureStreamer();
    ~TextureStreamer();

    // 1. Создает "пустую" текстуру и ставит её в очередь на чтение с диска
    void StreamTextureAsync(const std::string& filepath, Texture* outTexture);

    // 2. Вызывается каждый кадр в главном цикле. Заливает готовые данные в VRAM через PBO.
    bool Update();
    bool HasPendingWork() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return !taskQueue.empty() || !readyQueue.empty();
    }
private:
    void WorkerThreadLoop(); // То, что работает в фоне
    void UploadSingleMip(TextureLoadTask* task, int mipLevel); // Помощник для заливки
    std::queue<TextureLoadTask*> taskQueue;
    std::queue<TextureLoadTask*> readyQueue; // Очередь для отправки на GPU

    std::mutex queueMutex;
    std::atomic<bool> isRunning;
    std::thread workerThread;

    GLuint pbo; // Наш магический буфер для асинхронной заливки
};