    #ifndef MATERIAL_CLASS_H
    #define MATERIAL_CLASS_H
    #include<string>
    #include <filesystem>
    namespace fs = std::filesystem;
    #include"VAO.h"
    #include"EBO.h"
    #include"Camera.h"
    #include"Texture.h"
    #include "shaderClass.h"
#include "TextureStreamer.h"
    static int MaterialID;
    // Добавляем выравнивание!
 
    struct alignas(16) MaterialGPUData {
        uint64_t albedoHandle;     // 8 байт
        uint64_t normalHandle;     // 8 байт
        uint64_t heightHandle;     // 8 байт
        uint64_t metallicHandle;   // 8 байт
        uint64_t roughnessHandle;  // 8 байт
        uint64_t aoHandle;         // 8 байт (Итого: 48 байт)

        int hasAlbedo;             // 4 байт
        int hasNormal;             // 4 байт
        int hasHeight;             // 4 байт
        int hasMetallic;           // 4 байт
        int hasRoughness;          // 4 байт
        int hasAO;                 // 4 байт
        int useTriplanar;          // 4 байт
        float triplanarScale;      // 4 байт (Итого: 32 байта)
        glm::vec2 uvScale;
        glm::vec2 padding;          // 16 байт ПУСТОТЫ. Добиваем общий размер ровно до 96 байт!
    };
    class Material
    {
    public:
        glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);
        int ID;
        Material() {
            ID = MaterialID;
            MaterialID++;
            hasalbedo = false;
            hasnormal = false;
            hasheight = false;
            hasmetallic = false;
            hasroughness = false;
            hasao = false;
        };
        MaterialGPUData getGPUData();
        Texture albedo;
        Texture normal;
        Texture height;
        Texture metallic;
        Texture roughness;
        Texture ao;
        bool hasalbedo;
        bool hasnormal;
        bool hasheight;
        bool hasmetallic;
        bool hasroughness;
        bool hasao;
        void setAlbedo(std::string path, TextureStreamer* streamer = nullptr);
        void setNormal(std::string path, TextureStreamer* streamer = nullptr);
        void setHeight(std::string path, TextureStreamer* streamer = nullptr);
        void setMetallic(std::string path, TextureStreamer* streamer = nullptr);
        void setRoughness(std::string path, TextureStreamer* streamer = nullptr);
        void setAO(std::string path, TextureStreamer* streamer = nullptr);
        void Activate(Shader& shader);
    };
    #endif