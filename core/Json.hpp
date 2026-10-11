#pragma once

#include <flecs.h>

#include <cstdint>

namespace burnhope {

enum class JsonKind : uint8_t { Null = 0, Bool = 1, Number = 2, String = 3, Array = 4, Object = 5 };

struct JsonNode {
    uint64_t parent = 0;
    uint64_t first = 0;
    uint64_t next = 0;
    uint16_t order = 0;
    uint8_t kind = 0;
    uint8_t boolean = 0;
    double number = 0;
    uint32_t textOff = 0;
    uint32_t textLen = 0;
    char key[48]{};
};

// Owns its Flecs world and string arena. No UiState.
struct JsonDocument {
    flecs::world world{};
    char* arena = nullptr;
    uint32_t used = 0;
    uint32_t cap = 0;
    flecs::entity root{};
};

void jsonRelease(JsonDocument& doc);
[[nodiscard]] uint32_t jsonLoadFile(JsonDocument& doc, const char* path);
[[nodiscard]] bool jsonSaveFile(const JsonDocument& doc, const char* path);
[[nodiscard]] flecs::entity jsonFind(flecs::entity parent, const char* key);
[[nodiscard]] flecs::entity jsonAt(const JsonDocument& doc, flecs::entity parent, uint32_t index);
[[nodiscard]] bool jsonString(const JsonDocument& doc, flecs::entity e, const char*& data, uint32_t& len);
[[nodiscard]] bool jsonNumber(flecs::entity e, double& out);

} // namespace burnhope
