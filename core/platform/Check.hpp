#pragma once

namespace burnhope {

[[noreturn]] void bhFail(const char* message);

}

#if !defined(NDEBUG)
#define BH_ASSERT(expr, msg)            \
    do {                                \
        if (!(expr)) {                  \
            burnhope::bhFail(msg);      \
        }                               \
    } while (0)
#else
#define BH_ASSERT(expr, msg) ((void)0)
#endif

#define BH_VERIFY(expr) BH_ASSERT(expr, #expr)
