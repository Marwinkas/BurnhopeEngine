#pragma once
#include <string>
#include <vector>
#include <filesystem>

// Наш кастомный заголовок для файла текстуры
// Наш кастомный заголовок для файла текстуры
enum class BHTexType : uint8_t {
    Color = 0,  // DXT1/DXT5 sRGB
    Normal = 1,  // BC5 или сырой RG
    Linear = 2,  // DXT1/DXT5 linear (roughness, metallic, ao)
};
struct BHTexHeader {
    char magic[4] = { 'B', 'H', 'T', 'X' };
    uint32_t version = 1;        // Версия формата (чтобы не сломать старые файлы в будущем)

    // Базовые данные
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
    uint32_t format = 0;         // 0 = DXT1 (без альфы), 1 = DXT5 (с альфой)
    uint8_t  texType;

    // Настройки сэмплера (OpenGL)
    uint32_t wrapS = 0x2901;     // GL_REPEAT (10497)
    uint32_t wrapT = 0x2901;     // GL_REPEAT (10497)
    uint32_t minFilter = 0x2703; // GL_LINEAR_MIPMAP_LINEAR (9987)
    uint32_t magFilter = 0x2601; // GL_LINEAR (9729)

    // --- НОВЫЕ ВАЖНЫЕ НАСТРОЙКИ ---
    uint32_t isSRGB = 1;         // 1 = Цвет (Albedo), 0 = Математика (Normal/Roughness)
    float maxAnisotropy = 1.0f;  // От 1.0 (выкл) до 16.0 (максимум)
    uint32_t textureType = 0;    // 0=Albedo, 1=Normal, 2=Metallic, 3=Roughness, 4=AO

    // Подушка безопасности! 16 байт пустого места на случай будущих идей.
    // Если захотим добавить новую переменную, просто заберем место отсюда.
    uint32_t reserved[4] = { 0, 0, 0, 0 };
};
class TextureImporter {
public:
    static bool ImportTexture(const std::string& srcPath,
        const std::string& destPath,
        BHTexType texType);

    // --- НОВЫЕ ФУНКЦИИ ---
    static bool ReadHeader(const std::string& filePath, BHTexHeader& outHeader);
    static bool UpdateHeader(const std::string& filePath, const BHTexHeader& newHeader);
    static void CompressToBC5(const unsigned char* rgbaData, int width, int height,
        std::vector<unsigned char>& outData);
private:
    static void CompressToDXT(const unsigned char* rgbaData, int width, int height, bool hasAlpha, std::vector<unsigned char>& outData);
};