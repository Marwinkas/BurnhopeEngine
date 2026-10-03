#pragma once

#include <cstdint>

namespace burnhope {

constexpr uint32_t kUiBinMagic = 0x49554842u;
constexpr uint16_t kUiBinVersion = 2;

constexpr uint8_t kBinChrome = 10;
constexpr uint8_t kBinCalendar = 11;
constexpr uint8_t kBinColor = 12;
constexpr uint8_t kBinMenu = 13;
constexpr uint8_t kBinImage = 14;

constexpr uint8_t kBinClear = 1;
constexpr uint8_t kBinRequired = 2;
constexpr uint8_t kBinReadOnly = 4;
constexpr uint8_t kBinPassword = 8;
constexpr uint8_t kBinForm = 16;
constexpr uint8_t kBinMinimap = 32;
constexpr uint8_t kBinCheckOn = 64;
constexpr uint8_t kBinDisabled = 128;

#pragma pack(push, 1)
struct BinHead {
    uint32_t magic = kUiBinMagic;
    uint16_t version = kUiBinVersion;
    uint16_t count = 0;
    uint32_t pool = 0;
};

struct BinNode {
    uint16_t parent = 0xffff;
    uint8_t role = 0;
    uint8_t icon = 0;
    int16_t tag = -1;
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
    float width = 0;
    float height = 0;
    float grow = 0;
    float shrink = 0;
    float pad = 0;
    float gap = 0;
    float radius = 0;
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;
    float posX = 0;
    float posY = 0;
    float minV = 0;
    float maxV = 0;
    float value = 0;
    float shadow = 0;
    float borderW = 0;
    float br = 0;
    float bg = 0;
    float bb = 0;
    float padL = 0;
    float padR = 0;
    float padT = 0;
    float padB = 0;
    float minW = 0;
    float minH = 0;
    float marginL = 0;
    float marginR = 0;
    float marginT = 0;
    float marginB = 0;
    float basis = 0;
    float maxW = 0;
    float maxH = 0;
    float gapRow = -1;
    float aspect = 0;
    float posR = 0;
    float posB = 0;
    float textLeading = 0;
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

} // namespace burnhope
