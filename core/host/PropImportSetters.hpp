#pragma once

#include "host/PropImportInternal.hpp"

#include <cstring>
#include <initializer_list>
#include <type_traits>

// Value readers for the node key table (PropImportKeys.cpp): each takes the cursor at the value.
namespace burnhope::propimport {

using Setter = bool (*)(Cur& c, Import& in, Node& node);

inline bool literal(Cur& c, bool& out) {
    json::skip(c);
    if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
        out = true;
        c.p += 4;
        return true;
    }
    if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
        out = false;
        c.p += 5;
        return true;
    }
    return false;
}

template <auto M>
inline bool setStr(Cur& c, Import&, Node& n) {
    auto& dst = n.*M;
    uint32_t len = 0;
    return json::parseStringCap(c, dst, sizeof(dst), len);
}

template <auto M>
bool setF(Cur& c, Import&, Node& n) {
    double v = 0.0;
    if (!json::parseNumber(c, v)) {
        return false;
    }
    n.*M = static_cast<float>(v);
    return true;
}

template <auto M, auto Has>
bool setFHas(Cur& c, Import& in, Node& n) {
    n.*Has = true;
    return setF<M>(c, in, n);
}

template <auto M>
bool setInt(Cur& c, Import&, Node& n) {
    double v = 0.0;
    if (!json::parseNumber(c, v)) {
        return false;
    }
    n.*M = static_cast<std::remove_reference_t<decltype(n.*M)>>(v);
    return true;
}

template <auto M, auto Has>
bool setIntHas(Cur& c, Import& in, Node& n) {
    n.*Has = true;
    return setInt<M>(c, in, n);
}

template <auto M>
bool setBool(Cur& c, Import&, Node& n) {
    return literal(c, n.*M);
}

// One of `words`, read as a quoted string; returns its index or -1.
inline int word(Cur& c, std::initializer_list<const char*> words) {
    char text[16]{};
    uint32_t len = 0;
    if (!json::parseStringCap(c, text, sizeof(text), len)) {
        return -1;
    }
    int i = 0;
    for (const char* w : words) {
        if (std::strcmp(text, w) == 0) {
            return i;
        }
        ++i;
    }
    return -1;
}


} // namespace burnhope::propimport
