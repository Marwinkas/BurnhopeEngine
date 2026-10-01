#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

uint16_t splitTarget(const UiState& s, uint16_t split) {
    const uint16_t parent = s.parentOf[split];
    if (parent == kUiNone || s.yoga[parent] == nullptr || s.yoga[split] == nullptr) {
        return s.sideId;
    }
    const uint32_t count = YGNodeGetChildCount(s.yoga[parent]);
    for (uint32_t i = 0; i < count; ++i) {
        if (YGNodeGetChild(s.yoga[parent], i) != s.yoga[split] || i == 0) {
            continue;
        }
        YGNodeRef prev = YGNodeGetChild(s.yoga[parent], i - 1);
        for (uint16_t id = 0; id < s.count; ++id) {
            if (s.yoga[id] == prev) {
                return id;
            }
        }
    }
    return s.sideId;
}

} // namespace burnhope
