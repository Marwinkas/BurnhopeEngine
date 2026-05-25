# Burnhope Engine — AI Rules (2026)

**Scope:** AI coding only. Human roadmap: `path.md` (do not use). On conflict, this file wins.

**Stack:** C++26 · Vulkan 1.4+ · Flecs · Jolt · Slang · DoD (no OOP in hot paths)

**Module pattern:** Read SoA → AVX-512 or GPU Compute → write ReBAR / lock-free POD → no mutex, heap alloc, or virtual in loop.

---

## FORBIDDEN (hot path / game loop)

### C++ / ECS
- `virtual`, vtables, RTTI (`dynamic_cast`, `typeid`)
- `shared_ptr` / `weak_ptr` / `enable_shared_from_this` (use handles + pools)
- Virtual compute/pipeline inheritance (`BaseComputeShader`) — one data-driven `ComputePipeline`
- `new` / `malloc` / `make_shared` / `make_unique` (FrameArena, TLSF)
- `vector::push_back` without prior `reserve` / pool
- `string` ops, paths, regex, `cout`/`printf`/`ostream` (use `string_view`, hashes, spdlog/fmt off-loop)
- `mutex` / `lock_guard` between subsystems (lock-free MPMC + thread-local queues)
- Direct subsystem calls (Physics→Audio etc.) — POD events only
- `sleep_for` / OS thread sleep (fiber/coroutine yield)
- Sync disk I/O (`fstream`, `fread`)
- Entity pointers (64-bit MurmurHash3 GUIDs)
- `entity.add/remove` inside query iteration
- Deep `child_of` hierarchies for thousands of dynamic entities
- `GetComponent()` / string component lookup

### Vulkan
- API < 1.4
- `VkPipeline` → `VK_EXT_shader_object`
- `VkDescriptorSet` / `VkDescriptorPool` / `VK_EXT_descriptor_buffer` → `VK_EXT_descriptor_heap` + BDA
- Monolithic `vkCmdPipelineBarrier2` → split `vkCmdSetEvent2` / `vkCmdWaitEvents2`
- `vkAllocateMemory` in loop (VMA pools, defrag offline)
- CPU read of HOST_VISIBLE upload heap
- Two ALU-bound queues at once
- `vkCreateShadersEXT` during gameplay (warmup on load only)
- CPU command recording where DGC applies
- `VkRenderPass` / `VkFramebuffer` → `VK_KHR_dynamic_rendering`

### Shaders (Slang / SPIR-V 1.6+)
- `#ifdef LOW_END` → specialization constants
- `discard` alpha-test → `demote` (`VK_EXT_shader_demote_to_helper_invocation`)
- `pow`/`sin`/`cos` in PBR → FMA poly / 1D LUT
- Additive normals (`n1+n2`)
- `mat4` in shaders → quat / `mat4x3`
- `texture()` for 2×2 PCF → `textureGather`
- sRGB `pow(2.2)` in ALU → `VK_FORMAT_*_SRGB`
- `atomicAdd` if `subgroupBallot` enough
- `shared[]` without `+32` bank-conflict pad
- Dynamic loops with `texture()`/BDA + runtime trip count (unroll only)
- `sin`/`cos`/`atan` in loops without LUT/FMA
- Fetch→use back-to-back (ILP: fetch → ~15 ALU → use)
- Manual LDS zero-init → `VK_KHR_zero_initialize_workgroup_memory`
- Bindless index without `nonuniformEXT`
- `texture()` inside dynamic `if/else` → `textureLod` + explicit LOD
- `groupMemoryBarrier` for wave data → `subgroupBarrier`
- Per-wave scalar dup → `subgroupBroadcastFirst`
- 32-bit bool in SSBO → bitmasks (32 flags/uint)
- Repeated SSBO reads in loop → cache locals
- Arbitrary struct align → std430, 16-byte, explicit pad
- `float` + `float16_t` in one expr
- Div by variable in loop → precompute `rcp`
- `rayQueryProceedEXT` without max steps
- No subgroup ops where applicable
- Shared mem without explicit LDS layout

### Rendering / lighting
- Screen-space as **primary** GI/reflections (world must light off-screen)
- ReSTIR/PT for **hard geometric shadows** (use VSM + contact shadows)
- Per-frame CPU model matrices for all instances (build matrix in Compute from packed transform)
- Honest RT reflections/GI beyond ~20 m (SSR + baked)
- RT shadows for debris/grass
- Per-frame BLAS rebuild for distant NPC (>50 m)

### Physics
- Collider per bullet (hitscan: Jolt raycast batch; visual tracer only)
- Same physics rate for all objects (High/Medium/Low tiers + sleep required)
- Jolt from render thread (read-only buffers only)
- Sync raycasts for VFX (GPU SDF)
- Runtime small RigidBody spawn (pools)
- CCD on all layers (projectiles only)
- SoftBody >30 m (rigid proxy)
- Jolt fixed-step on render thread
- `memcpy` transforms to GPU (ReBAR direct write)

### Assets / runtime
- JSON/XML parse in game runtime (`simdjson` = Cooker/editor only)
- Manual level deserialize in loop (mmap `.bhscene` zero-copy)
- Text formats at runtime
- Sync editor asset import

### Other
- CPU transparency sort
- Particle count readback to CPU (indirect / work graphs)
- CPU billboard
- ImGui release HUD
- Runtime font parse
- Per-agent A* every frame
- Virtual BT / OOP AI
- Sample-by-sample audio
- ADPCM/Opus on render thread
- Full-object Undo copy (XOR deltas)

---

## DEPRECATED (never use)

`VK_EXT_descriptor_buffer`, `VK_KHR_push_descriptor`, `VkPipeline`, classic descriptor sets, Vulkan <1.4, render-pass subpasses, CPU-side CB building where DGC exists, manual descriptor set management.

---

## REQUIRED

### Platform & memory
- SDL3, Volk, FrameArena 16–32 MB/frame, TLSF pools, mimalloc fallback, VMA (ReBAR, defrag)
- GSL `owner<>` / `span<>` / `not_null<>`
- `VK_EXT_descriptor_heap`, `VK_KHR_buffer_device_address`, dedicated alloc, memory priority, pageable VRAM

### C++26 (hot path)
| Use | For |
|-----|-----|
| `mdspan` | SoA columns, 3D grids |
| `span` / `byte` | mmap slices |
| `expected` | Load/init errors (no exceptions in loop) |
| Concepts | `requires Component<T>`, POD-only |
| `consteval` | Cooker hashes, pack tables |
| `bit_cast` | Pack/unpack |
| `popcount` / `countr_zero` | Dirty masks |
| `enum class : uint8_t` | Flags, formats |
| `alignas(64)` | Queues, hot columns |
| `[[likely]]` | Profiled queries only |
| Coroutines/fibers | Taskflow, not sleep |

**Avoid in loop:** `shared_ptr`, `function`, `any`, `string`, virtual, RTTI, unreserved `vector` growth, `format` in loop.

```cpp
struct alignas(16) Transform12 { int16_t px,py,pz, rx,ry,rz; uint8_t sx,sy,sz; };
// Cache queries:
static flecs::query<Transform12, const Visible> g_q = world.query_builder<...>().build();
```

### Flecs
- Archetype SoA, `alignas(64)` hot data
- Cached `flecs::query`, free-function systems
- Tags: `Static`, `Active`, `TransformChanged`, `LightChanged`, `PhysicsHigh`, `PhysicsMedium`, `PhysicsLow`
- `TransformHistory { current, previous }` for parallel CPU + motion vectors
- `ecs_changed()` chunk bitmasks
- Deferred structural changes; ReBAR for GPU-read components
- Relationships for hierarchy (no CPU matrix chain)

### Threading
- Taskflow + fibers, 1 worker/core, `alignas(64)` queues
- Thread-local event queues → periodic gather
- 3-stage frame: N physics/input → N-1 CB → N-2 GPU
- Timeline semaphores, present_wait, split barriers
- Never two ALU-bound queue workloads simultaneously

### IDs & I/O
- MurmurHash3 64-bit GUIDs (no runtime paths)
- Runtime: mmap `.bhscene` / `.bhmesh` (FlatBuffers/bitsery Cooker-only)
- Blake3 CAS, xxHash hot-reload, DirectStorage → VRAM

### Vulkan 1.4 core extensions
**Must:** `VK_EXT_descriptor_heap`, `VK_EXT_shader_object`, `VK_KHR_dynamic_rendering`, `VK_KHR_dynamic_rendering_local_read`, `VK_EXT_device_generated_commands`, `VK_KHR_device_address_commands`, `VK_AMDX_shader_enqueue`, `VK_KHR_ray_query`, `VK_KHR_timeline_semaphore`, `VK_KHR_present_wait`, `VK_EXT_mesh_shader`, `VK_KHR_cooperative_matrix`, `VK_KHR_fragment_shading_rate`, `VK_KHR_pipeline_binary`, `VK_NV_compute_shader_derivatives`, `VK_NV_shader_subgroup_partitioned`, `VK_KHR_shader_float_controls2`, `VK_KHR_shader_subgroup_rotate`, `VK_KHR_shader_expect_assume`, `VK_KHR_shader_float16_int8`, `VK_KHR_zero_initialize_workgroup_memory`, `VK_KHR_workgroup_memory_explicit_layout`, `VK_EXT_shader_demote_to_helper_invocation`, `VK_NV_memory_decompression`, `VK_EXT_host_image_copy`.

### Math
- DirectXMath (XM* compute, XMFLOAT* storage)
- Editor/logic: float32; GPU instances: packed per `WorldFormatConfig`
- Octahedral normals → uint32; dual-quat bones FP16
- Large worlds: floating origin preferred over Jolt DP

### Physics (Jolt)
- Fiber job bridge, FrameArena temp alloc, ReBAR transform write
- Layers: NON_MOVING, MOVING, DEBRIS, SENSOR, PROJECTILE
- Tick tiers + sleep; raycast batching; CCD projectiles only

### Lighting pipeline (hybrid — mandatory split)
| Task | Solution | Not |
|------|----------|-----|
| Hard shadows | VSM 128×128 tiles, dirty page cache | ReSTIR/PT |
| Direct light | ReSTIR DI | Thousands forward lights |
| GI | ReSTIR GI + sparse **world-space** Radiance Cascades | Screen-space GI primary |
| Reflections | SSR (HZB) + RT fallback ≤15–20 m | Full PT |
| Contact | 1–2 step stochastic march | Heavy SS |

**ReSTIR:** per-pixel reservoir `{s, w_sum, M, W}` → initial RIS → temporal (motion vectors) → spatial → **one** shadow ray.

**Lazy:** static sectors / idle camera skip ray gen; reuse cascade/VSM cache.

### Transform packing (bake)
| Field | Editor | Small world bake | Large |
|-------|--------|------------------|-------|
| Pos | f32×3 | i8×3 (3 B) | f32×3 |
| Rot | quat f32 | i16 Euler×3 (6 B) | f32 quat |
| Scale | f32×3 | u8×3 (3 B) | f32×3 |
| **Total** | ~40 B | **~12 B** | 40 B |

Logic/physics stay float; pack on GPU buffer write. Baker picks `WorldFormatConfig` from scene extrema. Matrices built in **Compute only**.

### Compute pipelines
- Single `ComputePipeline` struct: shader path, layout, push constants = heap indices
- Flecs component or system calls `Dispatch()`
- Global bindless heap always bound

### Scene binary `.bhscene`
`Header | offset table | SoA blocks | string pool` — mmap, no runtime parse.

### Physics tiers
| Tier | Rate | Examples |
|------|------|----------|
| High | every frame | player, combat, raycast shots |
| Medium | 15–20 Hz | decor, distant NPC |
| Low | 2–3 Hz | debris, triggers |
| Sleep | 0 | vel < ε |
| Distance | broad/off | far chunks |

Bullets: raycast at fire; visual entity has no collider.

### Static vs dynamic
- **Static** (editor flag): never moves — no `TransformHistory`, excluded from dirty systems, baked aggressively.
- **Dynamic**: auto-freeze when `current == previous`; wake on hit/script; VSM/ReSTIR invalidate on change.

### Engine profiles
- **AAA:** full cascades, ReSTIR GI, full VSM, f32 world if needed.
- **Low-Poly:** i8/u8 transforms, 2–3 cascades, low ReSTIR M, aggressive sleep/VRS.

### Tools (off hot path)
- spdlog, Tracy, fmt, simdjson (Cooker), `std::execution::par` for bake
- Debug: `--use-classic-indirect` bypasses DGC

### Defer until core stable
- Neural GI, semantic asset search, multiple ML subsystems, Jolt DP without need.

---

## SHADER RULES (AI-generated code)

Vulkan **1.4+ only**. GPU-driven: descriptor heaps, BDA, modular Slang, aligned std430.

- Slang preferred; Shader Objects not pipelines
- Bindless: `textures[nonuniformEXT(id)]`; `textureLod` in branches
- Wave: `subgroupBarrier`, `subgroupBroadcastFirst`, `subgroup_rotate`, `expect_assume`
- Flags: bitmasks; loop: cache SSBO reads; `rcp` not div
- RT: max steps on `rayQueryProceedEXT`; cap distance; validate leaks via SDF
- Max offline bake; runtime assembles buffers

---

## AI CHECKLIST (before submitting code)

1. No virtual / shared_ptr / string / JSON / VkPipeline / VkDescriptorSet in hot path?
2. POD + SoA + handles + mmap + push constants + compute-first?
3. Shadows = VSM dirty tiles; GI = ReSTIR + world cascades; not SS-primary?
4. Transform from `WorldFormatConfig`; matrix in shader?
5. Physics tiers + sleep + raycast bullets?
6. AAA vs Low-Poly profile affects formats and light quality?
7. C++26 only (`-std=c++2c`); no C++17 patterns?

**Design answer template:** data layout → storage (SoA/GPU) → reader (system/shader) → dirty condition → forbidden items above.
