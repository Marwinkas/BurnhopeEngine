#include "../Header/TextureStreamer.h"
#include <fstream>
#include <iostream>

TextureStreamer::TextureStreamer() {
    glGenBuffers(1, &pbo);
    isRunning = true;
    workerThread = std::thread(&TextureStreamer::WorkerThreadLoop, this);
}

TextureStreamer::~TextureStreamer() {
    isRunning = false;
    if (workerThread.joinable()) workerThread.join();
    glDeleteBuffers(1, &pbo);
}

void TextureStreamer::StreamTextureAsync(const std::string& filepath, Texture* outTexture) {
    TextureLoadTask* task = new TextureLoadTask();
    task->filepath = filepath;
    task->targetTexture = outTexture;

    std::lock_guard<std::mutex> lock(queueMutex);
    taskQueue.push(task);
}
static GLenum GetGLFormat(const BHTexHeader& header) {
    if (header.format == 2) return GL_COMPRESSED_RG_RGTC2; // BC5
    if (header.format == 1) return header.isSRGB
        ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
        : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    // format == 0
    return header.isSRGB
        ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
        : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
}
void TextureStreamer::WorkerThreadLoop() {
    while (isRunning) {
        TextureLoadTask* currentTask = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!taskQueue.empty()) {
                currentTask = taskQueue.front();
                taskQueue.pop();
            }
        }

        if (currentTask != nullptr) {
            std::ifstream file(currentTask->filepath, std::ios::binary);
            if (file.is_open()) {
                file.read(reinterpret_cast<char*>(&currentTask->header), sizeof(BHTexHeader));

                size_t currentOffset = 0;
                for (uint32_t i = 0; i < currentTask->header.mipCount; ++i) {
                    uint32_t dataSize;
                    file.read(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));

                    currentTask->mipSizes.push_back(dataSize);
                    currentTask->mipOffsets.push_back(currentOffset); // Запоминаем, где лежит этот мип

                    currentOffset += dataSize;

                    size_t curVecSize = currentTask->rawData.size();
                    currentTask->rawData.resize(curVecSize + dataSize);
                    file.read(currentTask->rawData.data() + curVecSize, dataSize);
                }
                file.close();

                // Начинаем загрузку с САМОГО МАЛЕНЬКОГО мипмапа (последнего)
                currentTask->currentMipToUpload = currentTask->header.mipCount - 1;

                std::lock_guard<std::mutex> lock(queueMutex);
                readyQueue.push(currentTask);
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// Вспомогательная функция для заливки одного уровня
void TextureStreamer::UploadSingleMip(TextureLoadTask* task, int mipLevel) {
    size_t size = task->mipSizes[mipLevel];
    size_t offset = task->mipOffsets[mipLevel];

    int w = std::max(1, (int)task->header.width >> mipLevel);
    int h = std::max(1, (int)task->header.height >> mipLevel);

    GLenum glFormat = GetGLFormat(task->header); // ← теперь правильный формат

    glBufferData(GL_PIXEL_UNPACK_BUFFER, size,
        task->rawData.data() + offset, GL_STREAM_DRAW);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, mipLevel,
        0, 0, w, h, glFormat, size, 0);
}

// Вызывается каждый кадр в main.cpp
bool TextureStreamer::Update() {
    bool anyLoaded = false;

    while (true) {
        TextureLoadTask* task = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (readyQueue.empty()) break;
            task = readyQueue.front();
        }

        GLenum glFormat = GetGLFormat(task->header); // ← и тут тоже

        glGenTextures(1, &task->textureID);
        glBindTexture(GL_TEXTURE_2D, task->textureID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
            task->header.wrapS ? task->header.wrapS : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
            task->header.wrapT ? task->header.wrapT : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        float maxAniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 0.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);

        glTexStorage2D(GL_TEXTURE_2D, task->header.mipCount, glFormat,
            task->header.width, task->header.height);

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        for (uint32_t m = 0; m < task->header.mipCount; m++) {
            UploadSingleMip(task, m);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        task->bindlessHandle = glGetTextureHandleARB(task->textureID);
        glMakeTextureHandleResidentARB(task->bindlessHandle);

        task->targetTexture->ID = task->textureID;
        task->targetTexture->handle = task->bindlessHandle;

        std::cout << "[Стриминг] Загружена: " << task->filepath << std::endl;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            readyQueue.pop();
        }
        delete task;
        anyLoaded = true;
    }

    return anyLoaded;
}