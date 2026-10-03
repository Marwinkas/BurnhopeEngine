#include "platform/Check.hpp"

#include <cpptrace/cpptrace.hpp>

#include <cstdio>

namespace burnhope {

void bhFail(const char* message) {
    std::fprintf(stderr, "assert: %s\n", message != nullptr ? message : "");
    cpptrace::generate_trace().print();
    __builtin_trap();
}

} // namespace burnhope
