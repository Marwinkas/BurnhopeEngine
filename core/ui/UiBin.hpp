#pragma once

#include "ui/Interaction.hpp"
#include <cstdint>

namespace burnhope {

inline constexpr uint32_t kUiBinMagic = 0x49554842u; // "BHUI"
inline constexpr uint16_t kUiBinVersion = 4;

inline constexpr uint8_t kUiBinRoleCap = 32;
inline constexpr uint8_t kBinRoleClear = 1 << 0;
inline constexpr uint8_t kBinRoleRaised = 1 << 1;
inline constexpr uint8_t kBinRolePass = 1 << 2;
inline constexpr uint8_t kBinRoleGhost = 1 << 3;
inline constexpr uint16_t kBinParentRoot = 0xFFFE;

inline constexpr uint8_t kBinMarkPopup = 1 << 0;
inline constexpr uint8_t kBinMarkToken = 1 << 1;
inline constexpr uint32_t kUiMaxBinFileSize = 8u * 1024u * 1024u;

inline constexpr uint8_t kUiNameCap = 32;
inline constexpr uint8_t kUiTextCap = 96;
inline constexpr uint8_t kUiHintCap = 40;
inline constexpr uint8_t kUiIconPathCap = 128;

inline constexpr uint8_t kBinDisabled = 1 << 7;

#pragma pack(push, 1)
struct BinHead {
    uint32_t magic = kUiBinMagic;
    uint16_t version = kUiBinVersion;
    uint16_t count = 0;
    uint32_t pool = 0;
};

struct BinNode {
    uint16_t parent = kUiNone;
    uint8_t role = 0;
    uint8_t icon = 0;
    uint16_t iconOff = 0; // pool offset of the asset-path icon (iconLen bytes), resolved to a photo slot at import
    uint16_t nameOff = 0;
    uint16_t nameLen = 0;
    uint16_t textOff = 0;
    uint16_t textLen = 0;
    uint16_t hintOff = 0;
    uint16_t hintLen = 0;
    uint8_t direction = 0;
    uint8_t wrap = 0;
    uint8_t justify = 0;
    uint8_t align = 0;
    uint8_t widthMode = 0;
    uint8_t heightMode = 0;
    uint8_t position = 0;
    uint8_t shown = 1;
    float width = 0.0f;
    float height = 0.0f;
    float grow = 0.0f;
    float shrink = 1.0f;
    float pad = 0.0f;
    float gap = 0.0f;
    float radius = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    float posX = 0.0f;
    float posY = 0.0f;
    float shadow = 0.0f;
    float borderW = 0.0f;
    float br = 0.0f;
    float bg = 0.0f;
    float bb = 0.0f;
    float padL = 0.0f;
    float padR = 0.0f;
    float padT = 0.0f;
    float padB = 0.0f;
    float minW = 0.0f;
    float minH = 0.0f;
    float marginL = 0.0f;
    float marginR = 0.0f;
    float marginT = 0.0f;
    float marginB = 0.0f;
    float basis = 0.0f;
    float maxW = 0.0f;
    float maxH = 0.0f;
    float gapRow = -1.0f;
    float aspect = 0.0f;
    float posR = 0.0f;
    float posB = 0.0f;
    uint8_t drop = 0;
    uint8_t content = 0;
    uint8_t self = 0;
    uint8_t basisMode = 0;
    uint8_t maxWMode = 0;
    uint8_t maxHMode = 0;
    uint8_t overflow = 0;
    uint8_t box = 0;
    uint8_t inset = 0;
    uint8_t marks = 0;
    uint8_t token = 0;
    uint8_t iconLen = 0;
};
inline constexpr uint16_t kBinTailBytes = 26;

struct BinTail {
    uint8_t raw[kBinTailBytes]{};
};
#pragma pack(pop)

struct UiBinRole {
    const char* name = nullptr;
    uint8_t token = 0;
    uint8_t flags = 0;
};

struct UiState;

using UiCustomNodeLoader = uint16_t (*)(UiState& s, uint16_t parent, const BinNode& node, const BinTail& tail, const char* name, const char* text, const char* hint, void* user);

struct UiCustomBinLoader {
    UiCustomNodeLoader fn = nullptr;
    void* user = nullptr;
};

void uiRegisterCustomBinNode(UiState& s, uint8_t role, UiCustomNodeLoader loader, void* user);
void uiBinRole(UiState& s, uint8_t role, const char* name, uint8_t token, uint8_t flags);
uint8_t uiBinRoleFind(const UiState& s, const char* name);
void uiBinBuiltin(UiState& s);
uint16_t uiSpawnDescribed(UiState& s, uint16_t host, const BinNode& bin, const BinTail& tail, const char* name, const char* text, const char* hint);

// Asks the host's icon resolver (installed by the RHI canvas) for the photo slot of `path` and gives node `id` that
// photo as a button-sized sprite. Returns false and leaves the node untouched when no resolver is set or the path fails.
bool uiBindIconPath(UiState& s, uint16_t id, const char* path);

uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size);
uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path);

} // namespace burnhope