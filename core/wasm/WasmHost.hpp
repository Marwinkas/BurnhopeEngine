#pragma once

#include "memory/FrameArena.hpp"

#include <cstdint>

namespace burnhope {

enum class WasmStatus : uint8_t { Ok = 0, Trap = 1, Missing = 2 };

// Host function. args are the wasm params, already popped. Return value is pushed if the type has one result.
using WasmHostFn = int32_t (*)(void* user, const int32_t* args, uint32_t n);

constexpr int kWasmFuncs = 8;
constexpr int kWasmExports = 8;
constexpr int kWasmBinds = 4;
constexpr int kWasmTypes = 8;

struct WasmType {
    uint8_t params = 0;
    uint8_t results = 0;
};

struct WasmFunc {
    const uint8_t* code = nullptr;
    uint32_t size = 0;
    uint8_t type = 0;
    bool imported = false;
    uint8_t bind = 0;
};

struct WasmExport {
    char name[16]{};
    uint8_t kind = 0;
    uint32_t index = 0;
};

struct WasmBind {
    char module[16]{};
    char field[16]{};
    WasmHostFn fn = nullptr;
    void* user = nullptr;
    bool used = false;
};

// Fixed tables. Load fills them once. Invoke only walks them.
struct WasmHost {
    const uint8_t* bytes = nullptr;
    uint32_t byteSize = 0;
    uint8_t* memory = nullptr;
    uint32_t memoryBytes = 0;
    WasmType types[kWasmTypes]{};
    uint8_t typeCount = 0;
    WasmFunc funcs[kWasmFuncs]{};
    uint8_t funcCount = 0;
    WasmExport exports[kWasmExports]{};
    uint8_t exportCount = 0;
    WasmBind binds[kWasmBinds]{};
    uint8_t bindCount = 0;
};

// One-time load. Linear memory is one page taken from `arena`. `bytes` must stay alive.
// wasmInvoke does not allocate.
[[nodiscard]] bool wasmLoad(WasmHost& host, const uint8_t* bytes, uint32_t size, FrameArena& arena);
void wasmReset(WasmHost& host);

// Register before wasmLoad. module/field match the import, for example "env" / "tick".
[[nodiscard]] bool wasmBind(WasmHost& host, const char* module, const char* field, WasmHostFn fn, void* user);

[[nodiscard]] uint8_t* wasmMemory(WasmHost& host, uint32_t& bytes);
// Direct pointer into the module page. outSize is the byte length (one 64KB page).
[[nodiscard]] uint8_t* wasmGetMemory(WasmHost* host, uint32_t* outSize);

[[nodiscard]] WasmStatus wasmInvoke(WasmHost& host, const char* name, const int32_t* args, uint32_t argCount, int32_t& result);

} // namespace burnhope
