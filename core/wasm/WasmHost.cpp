#include "wasm/WasmHost.hpp"

#include <cstring>

namespace burnhope {
namespace {

constexpr uint32_t kPage = 65536;
constexpr int kFuncs = kWasmFuncs;
constexpr int kExports = kWasmExports;
constexpr int kBinds = kWasmBinds;
constexpr int kTypes = kWasmTypes;
constexpr int kStack = 32;
constexpr int kDepth = 8;

bool readUleb(const uint8_t* p, uint32_t n, uint32_t& at, uint32_t& out) {
    uint32_t value = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (at >= n) {
            return false;
        }
        const uint8_t b = p[at++];
        value |= static_cast<uint32_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

bool readSleb(const uint8_t* p, uint32_t n, uint32_t& at, int32_t& out) {
    int32_t value = 0;
    uint32_t shift = 0;
    uint8_t b = 0;
    for (int i = 0; i < 5; ++i) {
        if (at >= n) {
            return false;
        }
        b = p[at++];
        value |= static_cast<int32_t>(b & 0x7f) << shift;
        shift += 7;
        if ((b & 0x80) == 0) {
            if (shift < 32 && (b & 0x40) != 0) {
                value |= static_cast<int32_t>(~0u << shift);
            }
            out = value;
            return true;
        }
    }
    return false;
}

bool readName(const uint8_t* p, uint32_t n, uint32_t& at, char* dst, uint32_t cap) {
    uint32_t len = 0;
    if (!readUleb(p, n, at, len) || at + len > n || len >= cap) {
        return false;
    }
    std::memcpy(dst, p + at, len);
    dst[len] = '\0';
    at += len;
    return true;
}

} // namespace

namespace {

WasmStatus run(WasmHost& host, uint32_t funcIndex, const int32_t* args, int32_t& result, int depth);

WasmStatus run(WasmHost& host, uint32_t funcIndex, const int32_t* args, int32_t& result, int depth) {
    if (funcIndex >= host.funcCount || depth > kDepth) {
        return WasmStatus::Trap;
    }
    const WasmFunc& fn = host.funcs[funcIndex];
    const WasmType& type = host.types[fn.type];
    if (fn.imported) {
        if (fn.bind >= host.bindCount || host.binds[fn.bind].fn == nullptr) {
            return WasmStatus::Trap;
        }
        result = host.binds[fn.bind].fn(host.binds[fn.bind].user, args, type.params);
        return WasmStatus::Ok;
    }
    int32_t stack[kStack]{};
    int sp = 0;
    uint32_t pc = 0;
    const uint8_t* code = fn.code;
    const uint32_t size = fn.size;
    if (code == nullptr || size == 0 || code[0] != 0 || type.params > 8) {
        return WasmStatus::Trap;
    }
    int32_t locals[8]{};
    if (type.params > 0 && args != nullptr) {
        std::memcpy(locals, args, sizeof(int32_t) * type.params);
    }
    pc = 1;
    auto inRange = [&](uint32_t base, uint32_t memOffset) -> bool {
        if (host.memory == nullptr || host.memoryBytes < 4) {
            return false;
        }
        if (memOffset > 0xffffffffu - base) {
            return false;
        }
        const uint32_t effective = base + memOffset;
        return effective <= host.memoryBytes - 4;
    };
    auto push = [&](int32_t v) -> bool {
        if (sp >= kStack) {
            return false;
        }
        stack[sp++] = v;
        return true;
    };
    auto pop = [&](int32_t& v) -> bool {
        if (sp <= 0) {
            return false;
        }
        v = stack[--sp];
        return true;
    };
    while (pc < size) {
        const uint8_t op = code[pc++];
        if (op == 0x0b) {
            if (type.results == 1) {
                int32_t v = 0;
                if (!pop(v)) {
                    return WasmStatus::Trap;
                }
                result = v;
            }
            return WasmStatus::Ok;
        }
        if (op == 0x20) {
            uint32_t index = 0;
            if (!readUleb(code, size, pc, index) || index >= type.params || !push(locals[index])) {
                return WasmStatus::Trap;
            }
        } else if (op == 0x21) {
            uint32_t index = 0;
            int32_t v = 0;
            if (!readUleb(code, size, pc, index) || index >= type.params || !pop(v)) {
                return WasmStatus::Trap;
            }
            locals[index] = v;
        } else if (op == 0x41) {
            int32_t v = 0;
            if (!readSleb(code, size, pc, v) || !push(v)) {
                return WasmStatus::Trap;
            }
        } else if (op == 0x6a) {
            int32_t b = 0;
            int32_t a = 0;
            if (!pop(b) || !pop(a) || !push(a + b)) {
                return WasmStatus::Trap;
            }
        } else if (op == 0x6b) {
            int32_t b = 0;
            int32_t a = 0;
            if (!pop(b) || !pop(a) || !push(a - b)) {
                return WasmStatus::Trap;
            }
        } else if (op == 0x10) {
            uint32_t callee = 0;
            if (!readUleb(code, size, pc, callee) || callee >= host.funcCount) {
                return WasmStatus::Trap;
            }
            const uint8_t narg = host.types[host.funcs[callee].type].params;
            int32_t callArgs[4]{};
            if (narg > 4) {
                return WasmStatus::Trap;
            }
            for (int i = static_cast<int>(narg) - 1; i >= 0; --i) {
                if (!pop(callArgs[i])) {
                    return WasmStatus::Trap;
                }
            }
            int32_t got = 0;
            const WasmStatus st = run(host, callee, callArgs, got, depth + 1);
            if (st != WasmStatus::Ok) {
                return st;
            }
            if (host.types[host.funcs[callee].type].results == 1 && !push(got)) {
                return WasmStatus::Trap;
            }
        } else if (op == 0x28 || op == 0x36) {
            uint32_t align = 0;
            uint32_t offset = 0;
            if (!readUleb(code, size, pc, align) || !readUleb(code, size, pc, offset)) {
                return WasmStatus::Trap;
            }
            (void)align;
            if (op == 0x28) {
                int32_t addr = 0;
                if (!pop(addr)) {
                    return WasmStatus::Trap;
                }
                const uint32_t at = static_cast<uint32_t>(addr);
                if (!inRange(at, offset)) {
                    return WasmStatus::Trap;
                }
                int32_t v = 0;
                std::memcpy(&v, host.memory + at + offset, 4);
                if (!push(v)) {
                    return WasmStatus::Trap;
                }
            } else {
                int32_t value = 0;
                int32_t addr = 0;
                if (!pop(value) || !pop(addr)) {
                    return WasmStatus::Trap;
                }
                const uint32_t at = static_cast<uint32_t>(addr);
                if (!inRange(at, offset)) {
                    return WasmStatus::Trap;
                }
                std::memcpy(host.memory + at + offset, &value, 4);
            }
        } else {
            return WasmStatus::Trap;
        }
    }
    return WasmStatus::Trap;
}

int findExport(const WasmHost& host, const char* name, uint8_t kind) {
    if (name == nullptr) {
        return -1;
    }
    for (uint8_t i = 0; i < host.exportCount; ++i) {
        if (host.exports[i].kind == kind && std::strcmp(host.exports[i].name, name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool parse(WasmHost& host) {
    const uint8_t* p = host.bytes;
    const uint32_t n = host.byteSize;
    if (p == nullptr || n < 8 || p[0] != 0 || p[1] != 'a' || p[2] != 's' || p[3] != 'm') {
        return false;
    }
    if (p[4] != 1 || p[5] != 0 || p[6] != 0 || p[7] != 0) {
        return false;
    }
    uint32_t at = 8;
    uint8_t funcTypes[kFuncs]{};
    uint8_t defined = 0;
    while (at < n) {
        const uint8_t id = p[at++];
        uint32_t size = 0;
        if (!readUleb(p, n, at, size) || at + size > n) {
            return false;
        }
        const uint32_t end = at + size;
        if (id == 1) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count) || count > kTypes) {
                return false;
            }
            host.typeCount = static_cast<uint8_t>(count);
            for (uint32_t i = 0; i < count; ++i) {
                if (at >= end || p[at++] != 0x60) {
                    return false;
                }
                uint32_t params = 0;
                uint32_t results = 0;
                if (!readUleb(p, end, at, params) || params > 4) {
                    return false;
                }
                at += params;
                if (!readUleb(p, end, at, results) || results > 1) {
                    return false;
                }
                at += results;
                host.types[i].params = static_cast<uint8_t>(params);
                host.types[i].results = static_cast<uint8_t>(results);
            }
        } else if (id == 2) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count)) {
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                char module[16]{};
                char field[16]{};
                if (!readName(p, end, at, module, sizeof(module)) || !readName(p, end, at, field, sizeof(field))) {
                    return false;
                }
                if (at >= end) {
                    return false;
                }
                const uint8_t kind = p[at++];
                if (kind != 0 || host.funcCount >= kFuncs) {
                    return false;
                }
                uint32_t typeIndex = 0;
                if (!readUleb(p, end, at, typeIndex) || typeIndex >= host.typeCount) {
                    return false;
                }
                int bind = -1;
                for (uint8_t b = 0; b < host.bindCount; ++b) {
                    if (std::strcmp(host.binds[b].module, module) == 0 && std::strcmp(host.binds[b].field, field) == 0) {
                        bind = b;
                        break;
                    }
                }
                if (bind < 0) {
                    return false;
                }
                WasmFunc& fn = host.funcs[host.funcCount];
                fn.imported = true;
                fn.type = static_cast<uint8_t>(typeIndex);
                fn.bind = static_cast<uint8_t>(bind);
                host.binds[bind].used = true;
                ++host.funcCount;
            }
        } else if (id == 3) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count) || host.funcCount + count > kFuncs) {
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t typeIndex = 0;
                if (!readUleb(p, end, at, typeIndex) || typeIndex >= host.typeCount) {
                    return false;
                }
                funcTypes[defined++] = static_cast<uint8_t>(typeIndex);
                WasmFunc& fn = host.funcs[host.funcCount++];
                fn.type = static_cast<uint8_t>(typeIndex);
            }
        } else if (id == 5) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count) || count != 1 || at >= end) {
                return false;
            }
            const uint8_t flags = p[at++];
            uint32_t pages = 0;
            if (!readUleb(p, end, at, pages) || pages != 1 || (flags & 1) != 0) {
                return false;
            }
        } else if (id == 7) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count) || count > kExports) {
                return false;
            }
            host.exportCount = static_cast<uint8_t>(count);
            for (uint32_t i = 0; i < count; ++i) {
                if (!readName(p, end, at, host.exports[i].name, sizeof(host.exports[i].name)) || at + 1 > end) {
                    return false;
                }
                host.exports[i].kind = p[at++];
                uint32_t index = 0;
                if (!readUleb(p, end, at, index)) {
                    return false;
                }
                host.exports[i].index = index;
            }
        } else if (id == 10) {
            uint32_t count = 0;
            if (!readUleb(p, end, at, count) || count != defined) {
                return false;
            }
            const uint8_t base = static_cast<uint8_t>(host.funcCount - defined);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t body = 0;
                if (!readUleb(p, end, at, body) || at + body > end || body == 0) {
                    return false;
                }
                WasmFunc& fn = host.funcs[base + i];
                fn.code = p + at;
                fn.size = body;
                fn.type = funcTypes[i];
                at += body;
            }
        }
        at = end;
    }
    return host.memory != nullptr && host.funcCount > 0;
}

} // namespace

bool wasmBind(WasmHost& host, const char* module, const char* field, WasmHostFn fn, void* user) {
    if (module == nullptr || field == nullptr || fn == nullptr || host.bindCount >= kBinds) {
        return false;
    }
    if (std::strlen(module) >= 16 || std::strlen(field) >= 16) {
        return false;
    }
    WasmBind& b = host.binds[host.bindCount++];
    std::strncpy(b.module, module, sizeof(b.module) - 1);
    std::strncpy(b.field, field, sizeof(b.field) - 1);
    b.fn = fn;
    b.user = user;
    return true;
}

bool wasmLoad(WasmHost& host, const uint8_t* bytes, uint32_t size, FrameArena& arena) {
    WasmBind saved[kBinds]{};
    const uint8_t binds = host.bindCount;
    std::memcpy(saved, host.binds, sizeof(WasmBind) * binds);
    host = {};
    host.bindCount = binds;
    std::memcpy(host.binds, saved, sizeof(WasmBind) * binds);
    host.bytes = bytes;
    host.byteSize = size;
    host.memory = static_cast<uint8_t*>(frameArenaAlloc(arena, kPage, 16));
    if (host.memory == nullptr) {
        return false;
    }
    host.memoryBytes = kPage;
    std::memset(host.memory, 0, kPage);
    return parse(host);
}

void wasmReset(WasmHost& host) {
    const uint8_t binds = host.bindCount;
    WasmBind saved[kBinds]{};
    std::memcpy(saved, host.binds, sizeof(WasmBind) * binds);
    host = {};
    host.bindCount = binds;
    std::memcpy(host.binds, saved, sizeof(WasmBind) * binds);
}

uint8_t* wasmMemory(WasmHost& host, uint32_t& bytes) {
    bytes = host.memoryBytes;
    return host.memory;
}

uint8_t* wasmGetMemory(WasmHost* host, uint32_t* outSize) {
    if (host == nullptr) {
        if (outSize != nullptr) {
            *outSize = 0;
        }
        return nullptr;
    }
    if (outSize != nullptr) {
        *outSize = host->memoryBytes;
    }
    return host->memory;
}

WasmStatus wasmInvoke(WasmHost& host, const char* name, const int32_t* args, uint32_t argCount, int32_t& result) {
    const int exp = findExport(host, name, 0);
    if (exp < 0) {
        return WasmStatus::Missing;
    }
    const uint32_t func = host.exports[exp].index;
    if (func >= host.funcCount) {
        return WasmStatus::Trap;
    }
    if (argCount != host.types[host.funcs[func].type].params) {
        return WasmStatus::Trap;
    }
    return run(host, func, args, result, 0);
}

} // namespace burnhope
