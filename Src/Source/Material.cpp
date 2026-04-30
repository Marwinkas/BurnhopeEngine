#include "../Header/Material.h"
#include <windows.h>
#include <string>
#include <filesystem>
#include <iostream>
std::string getExecutablePath()
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::filesystem::path exePath(buffer);
	std::string path = exePath.parent_path().string();

	// Лёша, это поможет нам понять, не "врет" ли путь
	std::cout << "[DEBUG] Путь к EXE: " << path << std::endl;

	return path;
}
MaterialGPUData Material::getGPUData() {
    // СКОБКИ ОБЯЗАТЕЛЬНЫ! Они заполняют всю структуру (и отступы) чистыми нулями.
    MaterialGPUData data = {};

    data.albedoHandle = (hasalbedo && albedo.handle != 0) ? albedo.handle : 0;
    data.normalHandle = (hasnormal && normal.handle != 0) ? normal.handle : 0;
    data.heightHandle = (hasheight && height.handle != 0) ? height.handle : 0;
    data.metallicHandle = (hasmetallic && metallic.handle != 0) ? metallic.handle : 0;
    data.roughnessHandle = (hasroughness && roughness.handle != 0) ? roughness.handle : 0;
    data.aoHandle = (hasao && ao.handle != 0) ? ao.handle : 0;

    data.hasAlbedo = (hasalbedo && albedo.handle != 0) ? 1 : 0;
    data.hasNormal = (hasnormal && normal.handle != 0) ? 1 : 0;
    if (hasnormal) {
        std::cout << "[MAT] hasNormal=" << hasnormal
            << " handle=" << normal.handle << std::endl;
    }
    data.hasHeight = (hasheight && height.handle != 0) ? 1 : 0;
    data.hasMetallic = (hasmetallic && metallic.handle != 0) ? 1 : 0;
    data.hasRoughness = (hasroughness && roughness.handle != 0) ? 1 : 0;
    data.hasAO = (hasao && ao.handle != 0) ? 1 : 0;

    data.triplanarScale = 0.1f;
    data.useTriplanar = 0;
    data.uvScale = uvScale;

    return data;
}

// Универсальный внутренний помощник для загрузки (теперь со Стримером!)
bool SafeLoad(std::string path, bool& flag, Texture& tex, const char* type, int slot, int format, TextureStreamer* streamer) {
    if (path.empty() || !fs::exists(path) || fs::is_directory(path)) {
        std::cout << "[ERROR] Текстура не найдена: " << path << std::endl;
        return false;
    }

    if (flag && tex.handle != 0) {
        glMakeTextureHandleNonResidentARB(tex.handle);
        glDeleteTextures(1, &tex.ID);
    }

    if (streamer != nullptr && path.ends_with(".bhtex")) {
        // БЕЗОПАСНАЯ ИНИЦИАЛИЗАЦИЯ (Без копирования объектов)
        tex.ID = 0;
        tex.handle = 0;
        streamer->StreamTextureAsync(path, &tex);
    }
    else {
        tex = Texture(path.c_str(), type, slot, format);
    }

    flag = true;
    return true;
}

void Material::setAlbedo(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasalbedo, albedo, "diffuse", 0, 0, streamer); }
void Material::setNormal(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasnormal, normal, "normal", 1, 1, streamer); }
void Material::setHeight(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasheight, height, "height", 2, 2, streamer); }
void Material::setMetallic(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasmetallic, metallic, "metallic", 3, 1, streamer); }
void Material::setRoughness(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasroughness, roughness, "roughness", 4, 1, streamer); }
void Material::setAO(std::string path, TextureStreamer* streamer) { SafeLoad(path, hasao, ao, "ao", 5, 1, streamer); }