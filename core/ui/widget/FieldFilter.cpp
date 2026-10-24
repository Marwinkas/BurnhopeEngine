#include "ui/widget/Field.hpp"

namespace burnhope {
namespace {

bool allowAny(uint32_t cp) {
    return cp >= 32 && cp != 127;
}

bool allowDigits(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

bool allowLetters(uint32_t cp) {
    const bool latin = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    const bool cyr = (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
    return latin || cyr;
}

bool allowEmail(uint32_t cp) {
    return allowLetters(cp) || allowDigits(cp) || cp == '@' || cp == '.' || cp == '_' || cp == '+' || cp == '-';
}

bool allowTel(uint32_t cp) {
    return allowDigits(cp) || cp == '+' || cp == '-' || cp == ' ' || cp == '(' || cp == ')';
}

bool allowUrl(uint32_t cp) {
    return allowLetters(cp) || allowDigits(cp) || cp == ':' || cp == '/' || cp == '.' || cp == '?' || cp == '&' || cp == '='
        || cp == '%' || cp == '-' || cp == '_' || cp == '~';
}

bool checkEmail(const char* bytes, uint8_t len, const char*& msg) {
    int at = -1;
    for (uint8_t i = 0; i < len; ++i) {
        if (bytes[i] == ' ') {
            msg = "email name@host";
            return false;
        }
        if (bytes[i] == '@') {
            if (at >= 0 || i == 0) {
                msg = "email name@host";
                return false;
            }
            at = i;
        }
    }
    if (at < 1 || at + 2 >= len) {
        msg = "email name@host";
        return false;
    }
    bool dot = false;
    for (uint8_t i = static_cast<uint8_t>(at + 1); i < len; ++i) {
        if (bytes[i] == '.') {
            dot = i + 1 < len;
        }
    }
    if (!dot) {
        msg = "email name@host";
        return false;
    }
    return true;
}

bool checkUrl(const char* bytes, uint8_t len, const char*& msg) {
    bool colon = false;
    bool dot = false;
    for (uint8_t i = 0; i < len; ++i) {
        if (bytes[i] == ' ') {
            msg = "url needs a dot";
            return false;
        }
        if (bytes[i] == ':') {
            colon = true;
        }
        if (bytes[i] == '.') {
            dot = true;
        }
    }
    if (!colon && !dot) {
        msg = "url needs a dot";
        return false;
    }
    return true;
}

bool checkTel(const char* bytes, uint8_t len, const char*& msg) {
    uint8_t digits = 0;
    for (uint8_t i = 0; i < len; ++i) {
        if (bytes[i] >= '0' && bytes[i] <= '9') {
            ++digits;
        }
    }
    if (digits < 6) {
        msg = "tel needs 6 digits";
        return false;
    }
    return true;
}

using AllowFn = bool (*)(uint32_t);
using CheckFn = bool (*)(const char*, uint8_t, const char*&);

struct Gate {
    AllowFn allow = nullptr;
    CheckFn check = nullptr;
};

const Gate kGate[] = {
    {allowAny, nullptr},
    {allowDigits, nullptr},
    {allowLetters, nullptr},
    {allowEmail, checkEmail},
    {allowTel, checkTel},
    {allowUrl, checkUrl},
};

const Gate& gateAt(uint8_t filter) {
    if (filter >= sizeof(kGate) / sizeof(kGate[0])) {
        return kGate[0];
    }
    return kGate[filter];
}

} // namespace

bool fieldAllows(uint8_t filter, uint32_t cp) {
    if (cp < 32 || cp == 127) {
        return false;
    }
    return gateAt(filter).allow(cp);
}

bool fieldCheck(uint8_t filter, const char* bytes, uint8_t len, const char*& msg) {
    const CheckFn check = gateAt(filter).check;
    if (check == nullptr) {
        return true;
    }
    return check(bytes != nullptr ? bytes : "", len, msg);
}

} // namespace burnhope
