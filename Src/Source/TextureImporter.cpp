#include "../Header/TextureImporter.h"
#include <iostream>
#include <fstream>
#include <cmath>

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_dxt.h>
bool TextureImporter::ReadHeader(const std::string& filePath, BHTexHeader& outHeader) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    file.read(reinterpret_cast<char*>(&outHeader), sizeof(BHTexHeader));
    return true;
}

bool TextureImporter::UpdateHeader(const std::string& filePath, const BHTexHeader& newHeader) {
    // Открываем файл ОДНОВРЕМЕННО для чтения и записи (чтобы не стереть пиксели!)
    std::fstream file(filePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekp(0); // Прыгаем в самый нулевой байт файла
    file.write(reinterpret_cast<const char*>(&newHeader), sizeof(BHTexHeader)); // Перезаписываем только шапку
    return true;
}
void TextureImporter::CompressToBC5(const unsigned char* rgbaData, int width, int height,
    std::vector<unsigned char>& outData) {
    // BC5 = два независимых BC4 блока (R и G каналы)
    // Каждый блок 4x4 = 8 байт, итого 16 байт на блок
    int paddedW = (width + 3) & ~3;
    int paddedH = (height + 3) & ~3;
    outData.resize((paddedW / 4) * (paddedH / 4) * 16);
    unsigned char* outPtr = outData.data();

    unsigned char blockR[16]; // только R канал
    unsigned char blockG[16]; // только G канал
    unsigned char blockRGBA[64];

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            // Извлекаем блок 4x4
            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 4; bx++) {
                    int srcX = std::min(x + bx, width - 1);
                    int srcY = std::min(y + by, height - 1);
                    int srcIdx = (srcY * width + srcX) * 4;
                    int dstIdx = by * 4 + bx;

                    blockR[dstIdx] = rgbaData[srcIdx + 0]; // R
                    blockG[dstIdx] = rgbaData[srcIdx + 1]; // G

                    // Для stb_compress_bc4 нужен rgba буфер
                    blockRGBA[dstIdx * 4 + 0] = rgbaData[srcIdx + 0];
                    blockRGBA[dstIdx * 4 + 1] = rgbaData[srcIdx + 1];
                    blockRGBA[dstIdx * 4 + 2] = rgbaData[srcIdx + 2];
                    blockRGBA[dstIdx * 4 + 3] = rgbaData[srcIdx + 3];
                }
            }

            // stb_compress_bc4_block сжимает один канал в 8 байт
            stb_compress_bc4_block(outPtr, blockR); // R → первые 8 байт
            stb_compress_bc4_block(outPtr + 8, blockG); // G → вторые 8 байт
            outPtr += 16;
        }
    }
}
bool TextureImporter::ImportTexture(const std::string& srcPath,
    const std::string& destPath,
    BHTexType texType) // ← новый параметр
{
    int width, height, channels;
    unsigned char* data = stbi_load(srcPath.c_str(), &width, &height, &channels, 4);
    if (!data) return false;

    BHTexHeader header;
    header.width = width;
    header.height = height;
    header.texType = (uint8_t)texType;
    header.mipCount = 1 + (int)std::floor(std::log2(std::max(width, height)));

    // Выбираем формат сжатия по типу текстуры
    if (texType == BHTexType::Normal) {
        header.format = 2; // BC5
        header.isSRGB = 0; // нормали НИКОГДА не sRGB
    }
    else if (texType == BHTexType::Color) {
        header.format = (channels == 4) ? 1 : 0; // DXT5 или DXT1
        header.isSRGB = 1;
    }
    else { // Linear: roughness, metallic, ao
        header.format = 0; // DXT1
        header.isSRGB = 0;
    }

    std::ofstream outFile(destPath, std::ios::binary);
    outFile.write(reinterpret_cast<const char*>(&header), sizeof(BHTexHeader));

    int currentW = width, currentH = height;
    unsigned char* currentData = data;
    std::vector<unsigned char> nextMipData;

    for (uint32_t mip = 0; mip < header.mipCount; mip++) {
        std::vector<unsigned char> compressed;

        if (texType == BHTexType::Normal) {
            CompressToBC5(currentData, currentW, currentH, compressed);
        }
        else {
            CompressToDXT(currentData, currentW, currentH,
                (header.format == 1), compressed);
        }

        uint32_t dataSize = (uint32_t)compressed.size();
        outFile.write(reinterpret_cast<const char*>(&dataSize), sizeof(uint32_t));
        outFile.write(reinterpret_cast<const char*>(compressed.data()), dataSize);

        if (mip < header.mipCount - 1) {
            int nextW = std::max(1, currentW / 2);
            int nextH = std::max(1, currentH / 2);
            nextMipData.resize(nextW * nextH * 4);
            stbir_resize_uint8_linear(currentData, currentW, currentH, 0,
                nextMipData.data(), nextW, nextH, 0,
                (stbir_pixel_layout)4);
            currentData = nextMipData.data();
            currentW = nextW;
            currentH = nextH;
        }
    }

    stbi_image_free(data);
    outFile.close();
    return true;
}

// Функция упаковки пикселей в блочный формат видеокарты
void TextureImporter::CompressToDXT(const unsigned char* rgbaData, int width, int height, bool hasAlpha, std::vector<unsigned char>& outData) {
    // DXT работает блоками 4x4 пикселя. 
    // Размер блока: DXT1 = 8 байт, DXT5 = 16 байт.
    int blockSize = hasAlpha ? 16 : 8;

    // Выравниваем размеры до кратности 4
    int paddedW = (width + 3) & ~3;
    int paddedH = (height + 3) & ~3;

    outData.resize((paddedW / 4) * (paddedH / 4) * blockSize);
    unsigned char* outPtr = outData.data();

    // Временный буфер для извлечения блока 4x4
    unsigned char block[64]; // 4 * 4 * 4(RGBA) = 64 байта

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            // Копируем пиксели в блок 4x4
            for (int by = 0; by < 4; ++by) {
                for (int bx = 0; bx < 4; ++bx) {
                    int srcX = std::min(x + bx, width - 1);
                    int srcY = std::min(y + by, height - 1);
                    int srcIdx = (srcY * width + srcX) * 4;
                    int dstIdx = (by * 4 + bx) * 4;

                    block[dstIdx] = rgbaData[srcIdx];
                    block[dstIdx + 1] = rgbaData[srcIdx + 1];
                    block[dstIdx + 2] = rgbaData[srcIdx + 2];
                    block[dstIdx + 3] = rgbaData[srcIdx + 3];
                }
            }

            // Сжимаем блок 4x4
            stb_compress_dxt_block(outPtr, block, hasAlpha ? 1 : 0, STB_DXT_NORMAL);
            outPtr += blockSize;
        }
    }
}