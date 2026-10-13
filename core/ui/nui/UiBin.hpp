#pragma once

#include "core/ui/Interaction.hpp"
#include <cstdint>

namespace burnhope {

inline constexpr uint32_t kUiBinMagic = 0x49554842u; // "BHUI"
inline constexpr uint16_t kUiBinVersion = 2;

inline constexpr uint8_t kUiBinRoleCap = 16;
inline constexpr uint32_t kUiMaxBinFileSize = 8u * 1024u * 1024u;

inline constexpr uint8_t kUiNameCap = 32;
inline constexpr uint8_t kUiTextCap = 96;
inline constexpr uint8_t kUiHintCap = 40;

inline constexpr uint8_t kBinClear     = 1 << 0;
inline constexpr uint8_t kBinRequired  = 1 << 1;
inline constexpr uint8_t kBinReadOnly  = 1 << 2;
inline constexpr uint8_t kBinPassword  = 1 << 3;
inline constexpr uint8_t kBinForm      = 1 << 4;
inline constexpr uint8_t kBinMinimap   = 1 << 5;
inline constexpr uint8_t kBinCheckOn   = 1 << 6;
inline constexpr uint8_t kBinDisabled  = 1 << 7;

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
    int16_t reservedTag = 0;
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
    uint8_t filter = 0;
    uint8_t flags = 0;
    uint8_t checkKind = 0;
    uint8_t checkGroup = 0;
    uint8_t maxChars = 0;
    uint8_t shown = 1;
    uint8_t textWrap = 0;
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
    float minV = 0.0f;
    float maxV = 0.0f;
    float value = 0.0f;
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
    float textLeading = 0.0f;
    uint8_t drop = 0;
    uint8_t content = 0;
    uint8_t self = 0;
    uint8_t basisMode = 0;
    uint8_t maxWMode = 0;
    uint8_t maxHMode = 0;
    uint8_t overflow = 0;
    uint8_t box = 0;
    uint8_t inset = 0;
    uint8_t textAlign = 0;
    uint8_t textEllipsis = 0;
    uint8_t textValign = 0;
    uint8_t textDeco = 0;
    uint8_t pad0 = 0;
    uint8_t pad1 = 0;
    uint8_t pad2 = 0;
};
#pragma pack(pop)

struct UiState;

using UiCustomNodeLoader = uint16_t (*)(UiState& s, uint16_t parent, const BinNode& node, const char* name, void* user);

struct UiCustomBinLoader {
    UiCustomNodeLoader fn = nullptr;
    void* user = nullptr;
};

void uiRegisterCustomBinNode(UiState& s, uint8_t role, UiCustomNodeLoader loader, void* user);

uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size);
uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path);

} // namespace burnhope