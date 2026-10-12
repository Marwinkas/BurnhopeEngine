#pragma once

#include <cstdint>
#include <vector>

namespace burnhope {

enum class JsonKind : uint8_t { Null = 0, Bool = 1, Number = 2, String = 3, Array = 4, Object = 5 };

inline constexpr uint32_t kJsonNone = 0;

struct JsonNode {
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t next = 0;
    uint32_t textOff = 0;
    uint32_t textLen = 0;
    double number = 0.0;
    uint16_t order = 0;
    uint8_t kind = 0;
    uint8_t boolean = 0;
    char key[48]{};
};

struct JsonDocument {
    std::vector<JsonNode> nodes;
    char* arena = nullptr;
    uint32_t used = 0;
    uint32_t cap = 0;
    uint32_t root = kJsonNone;
};

void jsonRelease(JsonDocument& doc);
[[nodiscard]] uint32_t jsonLoadFile(JsonDocument& doc, const char* path);
[[nodiscard]] bool jsonSaveFile(const JsonDocument& doc, const char* path);
[[nodiscard]] uint32_t jsonFind(const JsonDocument& doc, uint32_t parent, const char* key);
[[nodiscard]] uint32_t jsonAt(const JsonDocument& doc, uint32_t parent, uint32_t index);
[[nodiscard]] bool jsonString(const JsonDocument& doc, uint32_t node, const char*& data, uint32_t& len);
[[nodiscard]] bool jsonNumber(const JsonDocument& doc, uint32_t node, double& out);

} // namespace burnhope
