#pragma once

#include <charconv>
#include <cstdint>
#include <cstring>
#include <system_error>

namespace burnhope::json {

struct Cur {
    const char* p = nullptr;
    const char* end = nullptr;
};

inline void skip(Cur& c) {
    while (c.p < c.end) {
        const char ch = *c.p;
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            break;
        }
        ++c.p;
    }
}

inline bool eat(Cur& c, char ch) {
    skip(c);
    if (c.p < c.end && *c.p == ch) {
        ++c.p;
        return true;
    }
    return false;
}

inline bool appendUtf8(uint32_t cp, auto&& out) {
    if (cp < 0x80) {
        return out(static_cast<char>(cp));
    }
    if (cp < 0x800) {
        return out(static_cast<char>(0xC0 | (cp >> 6))) && out(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    if (cp < 0x10000) {
        return out(static_cast<char>(0xE0 | (cp >> 12))) && out(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))) &&
               out(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return false;
}

template<typename Emit>
inline bool parseStringBody(Cur& c, Emit&& emit, uint32_t maxLen, uint32_t& written) {
    written = 0;
    while (c.p < c.end && *c.p != '"') {
        unsigned char ch = static_cast<unsigned char>(*c.p++);
        if (ch == '\\' && c.p < c.end) {
            const char n = *c.p++;
            if (n == 'n') {
                ch = '\n';
            } else if (n == 't') {
                ch = '\t';
            } else if (n == 'r') {
                ch = '\r';
            } else if (n == 'u' && c.p + 4 <= c.end) {
                uint32_t cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = *c.p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp += static_cast<uint32_t>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp += static_cast<uint32_t>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp += static_cast<uint32_t>(h - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                if (!appendUtf8(cp, [&](char byte) {
                        if (written >= maxLen) {
                            return false;
                        }
                        if (!emit(byte)) {
                            return false;
                        }
                        ++written;
                        return true;
                    })) {
                    return false;
                }
                continue;
            } else {
                ch = static_cast<unsigned char>(n);
            }
        }
        if (written >= maxLen) {
            return false;
        }
        if (!emit(static_cast<char>(ch))) {
            return false;
        }
        ++written;
    }
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    return true;
}

inline bool parseStringCap(Cur& c, char* dst, uint32_t cap, uint32_t& len) {
    skip(c);
    if (c.p >= c.end || *c.p != '"' || cap == 0 || dst == nullptr) {
        return false;
    }
    ++c.p;
    const uint32_t maxBody = cap - 1;
    uint32_t n = 0;
    if (!parseStringBody(c, [&](char ch) {
            dst[n] = ch;
            return true;
        }, maxBody, n)) {
        return false;
    }
    len = n;
    dst[len] = '\0';
    return true;
}

inline bool parseNumber(Cur& c, double& out) {
    skip(c);
    if (c.p >= c.end) {
        return false;
    }
    const char* start = c.p;
    const auto [ptr, ec] = std::from_chars(c.p, c.end, out);
    if (ec != std::errc() || ptr == start) {
        return false;
    }
    c.p = ptr;
    return true;
}

inline bool skipString(Cur& c) {
    skip(c);
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    while (c.p < c.end) {
        const char ch = *c.p++;
        if (ch == '"') {
            return true;
        }
        if (ch != '\\') {
            continue;
        }
        if (c.p >= c.end) {
            return false;
        }
        if (*c.p == 'u') {
            if (c.p + 5 > c.end) {
                return false;
            }
            c.p += 5;
        } else {
            ++c.p;
        }
    }
    return false;
}

inline bool skipValue(Cur& c) {
    skip(c);
    if (c.p >= c.end) {
        return false;
    }
    const char ch = *c.p;
    if (ch == '"') {
        return skipString(c);
    }
    if (ch == '{') {
        ++c.p;
        skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return true;
        }
        for (;;) {
            if (!skipString(c) || !eat(c, ':') || !skipValue(c)) {
                return false;
            }
            skip(c);
            if (c.p < c.end && *c.p == '}') {
                ++c.p;
                return true;
            }
            if (!eat(c, ',')) {
                return false;
            }
        }
    }
    if (ch == '[') {
        ++c.p;
        skip(c);
        if (c.p < c.end && *c.p == ']') {
            ++c.p;
            return true;
        }
        for (;;) {
            if (!skipValue(c)) {
                return false;
            }
            skip(c);
            if (c.p < c.end && *c.p == ']') {
                ++c.p;
                return true;
            }
            if (!eat(c, ',')) {
                return false;
            }
        }
    }
    if (static_cast<std::size_t>(c.end - c.p) >= 4 && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        return true;
    }
    if (static_cast<std::size_t>(c.end - c.p) >= 5 && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        return true;
    }
    if (static_cast<std::size_t>(c.end - c.p) >= 4 && std::memcmp(c.p, "null", 4) == 0) {
        c.p += 4;
        return true;
    }
    double dummy = 0.0;
    return parseNumber(c, dummy);
}

} // namespace burnhope::json
