# Burnhope Engine — Coding Rules & Standards (2026)
## Vulkan 1.4 Ultimate Optimized Engine Specification

## ЯЗЫК: C++20/23 | ГРАФИКА: Vulkan 1.4 | ПАРАДИГМА: Data-Oriented Design (DOD)

---

# ЧАСТЬ 1: СТРОГО ЗАПРЕЩЕНО (FORBIDDEN)

## ООП / Виртуальные вызовы
- ❌ `virtual` функции / `vtables` в game loop и ECS системах
- ❌ `dynamic_cast` / `typeid` (RTTI)
- ❌ Наследование с виртуальными методами в критических путях
- ❌ `std::enable_shared_from_this` в ECS компонентах
- ❌ Паттерны Visitor/Observer/Strategy с виртуальными вызовами
- ❌ ООП деревья поведения (Behavior Trees) с виртуальными методами
- ❌ Наследование компонентов (Component OOP)
- ❌ ООП DSP (виртуальные функции для аудио-фильтров)

## Память / Аллокации
- ❌ `new` / `malloc` / `std::make_shared` / `std::make_unique` в game loop
- ❌ `vkAllocateMemory` в game loop
- ❌ `std::vector::push_back` без `reserve()` в game loop
- ❌ `std::string` операции, конкатенация, regex в game loop
- ❌ Неиспользование FrameArena для временных аллокаций
- ❌ Динамические контейнеры внутри компонентов (`std::vector`, `std::string`, `std::shared_ptr`)
- ❌ Динамическое выделение памяти для путей ИИ
- ❌ Аллокации внутри lock-free структур Event Bus
- ❌ Динамическая аллокация частиц на CPU (использовать кольцевые буферы)
- ❌ Глобальные аллокации в Update(), Tick(), ECS системах
- ❌ Копирование массивов трансформаций через memcpy (использовать swapping указателей)

## Синхронизация / Потоки
- ❌ `std::mutex` / `std::lock_guard` между подсистемами движка
- ❌ `std::this_thread::sleep_for` / `std::this_thread::sleep_until` в game loop
- ❌ Прямые вызовы между подсистемами (Physics→Audio, ECS→Renderer)
- ❌ Создание/уничтожение потоков в game loop
- ❌ OS-level thread sleeping (только coroutine-yield)
- ❌ Аллокации и блокировки в аудио-потоке
- ❌ Синхронное чтение с диска (fread, std::ifstream)

## Строки / Идентификаторы
- ❌ `std::string` пути к файлам в runtime (`"textures/grass.dds"`)
- ❌ Строковые сравнения/поиск в game loop
- ❌ `std::cout` / `std::cerr` / `std::ostream` для логирования
- ❌ `printf` / `fprintf` (использовать `fmt::format` / `spdlog`)
- ❌ Утечка строковых имен в Cooker (все строки должны быть захэшированы)
- ❌ Форматирование строк в цикле рендера (sprintf, to_string, format)
- ❌ Форматирование строк в профайлере (динамические строки в зонах)
- ❌ Синхронное логгирование в файл (std::ofstream)

## Vulkan (КРИТИЧЕСКИЕ ЗАПРЕТЫ 2026)
- ❌ `VkPipeline` (использовать Shader Objects `VK_EXT_shader_object`)
- ❌ `VkDescriptorSet` / `VkDescriptorPool` (использовать BDA + VK_EXT_descriptor_heap)
- ❌ `VK_EXT_descriptor_buffer` (DEPRECATED 2026 — использовать VK_EXT_descriptor_heap)
- ❌ Монолитные `vkCmdPipelineBarrier2` (использовать Split Barriers: `vkCmdSetEvent2`/`vkCmdWaitEvents2`)
- ❌ `vkAllocateMemory` в game loop (использовать VMA virtual aliasing)
- ❌ CPU read из HOST_VISIBLE upload heap (PCIe cache miss)
- ❌ Два одновременных ALU-bound задания на очередях
- ❌ `vkAllocateMemory` при структурных изменениях (использовать VMA defrag / DMA bulk moves)
- ❌ `vkCreateShadersEXT` во время геймплея (только warmup на load screen)
- ❌ Любые Vulkan API ниже версии 1.4 (только Vulkan 1.4+)
- ❌ Использование старых расширений заменённых новыми (см. раздел DEPRECATED EXTENSIONS)
- ❌ Ручное управление descriptor sets (использовать Descriptor Heaps)
- ❌ CPU-side command generation (использовать Device Generated Commands)
- ❌ Отсутствие GPU autonomous scheduling (использовать VK_AMDX_shader_enqueue)

## Шейдеры (КРИТИЧЕСКИЕ ЗАПРЕТЫ 2026)
- ❌ `#ifdef LOW_END` в шейдерах (использовать Specialization Constants `layout(constant_id=X)`)
- ❌ `discard` для alpha-test (использовать `demote` — `VK_EXT_shader_demote_to_helper_invocation`)
- ❌ `pow/sin/cos` в PBR шейдерах (использовать FMA полиномиальные аппроксимации)
- ❌ Аддитивное смешивание нормалей (`n1+n2`)
- ❌ `mat4` в шейдерах (использовать `quaternion` или `mat4x3`)
- ❌ `texture()` для 2×2 PCF (использовать `textureGather`/`OpImageGather`)
- ❌ sRGB `pow(color,2.2)` в ALU (использовать `VK_FORMAT_..._SRGB`)
- ❌ `atomicAdd` где достаточно `subgroupBallot`
- ❌ `shared[]` массив без `+32` padding (bank conflict)
- ❌ Циклы с `texture()`/BDA доступом и runtime iteration count (только unroll)
- ❌ Стандартные тригонометрические функции (sin, cos, atan) в циклах без 1D LUT или FMA проверки
- ❌ Тяжелые resource fetch с немедленным использованием (использовать ILP: fetch → 15 ALU → use)
- ❌ Ручное LDS zero-initialization в циклах (использовать VK_KHR_zero_initialize_workgroup_memory)
- ❌ Динамическая индексация текстурных массивов без nonuniformEXT
- ❌ `texture()/Sample()` внутри динамических if/else веток (использовать textureLod с явным LOD)
- ❌ `groupMemoryBarrier()` для intra-wave данных (использовать subgroupBarrier())
- ❌ Скалярное дублирование регистров для per-wave данных (использовать subgroupBroadcastFirst())
- ❌ bool флаги как 32-bit переменные в SSBO (упаковывать в bitmasks)
- ❌ Множественные чтения одной SSBO переменной в тяжелых циклах (кешировать локально)
- ❌ Структуры с произвольным выравниванием (использовать std430, 16-byte alignment, явный padding)
- ❌ Смешивание float и float16_t в одном выражении
- ❌ Деление на переменную внутри циклов (предвычислять 1.0/x)
- ❌ `rayQueryProceedEXT()` без лимита шагов (предотвращает infinite loops)
- ❌ `discard/demote` в OIT hair шейдерах (использовать VK_KHR_fragment_shader_barycentric density)
- ❌ Использование старых Vulkan pipeline подходов (использовать Shader Objects)
- ❌ Отсутствие subgroup оптимизаций (обязательно использовать subgroup ops)
- ❌ Использование shared memory без явного layout (использовать explicit layout)
- ❌ Динамические ветвления без branch prediction hints (использовать expect/assume)
- ❌ Отсутствие FP16/INT8 нативной поддержки (использовать cooperative matrix)
- ❌ Ручное управление LDS для blur/neighbor ops (использовать subgroup rotate)

## Рендеринг / Ray Tracing
- ❌ Ray Tracing для отражений/GI дальше 20м (использовать SSR + baked maps)
- ❌ Cloth/SoftBody симуляция для HZB-culled объектов (LOD tickrate или freeze)
- ❌ Честные тени от мелкого динамического мусора (использовать screen-space contact shadows)
- ❌ Honest RT для отражений/GI за пределами 20м
- ❌ Per-object BLAS rebuild каждый кадр для NPC > 50м
- ❌ RT для теней от мелкого динамического мусора

## Физика
- ❌ Вызовы Jolt API из Render-потока (рендер должен иметь доступ только к Read-Only буферам)
- ❌ Синхронные лучевые проверки (Raycasts) для визуальных эффектов (использовать GPU через Global SDF)
- ❌ Создание мелких RigidBody в реалтайме (использовать предварительно аллоцированный пул)
- ❌ CCD на всех слоях (ограничить LAYER_FAST_PROJECTILES только)
- ❌ SoftBody на дистанции >30м (swap to RigidBody)
- ❌ Jolt fixed-step на render thread (decouple, State Buffering + GPU interpolation)
- ❌ Копирование трансформаций через memcpy для GPU (использовать ReBAR direct write)

## VFX / Частицы
- ❌ Сортировка полупрозрачности на процессоре (исключительно локальная на GPU)
- ❌ Чтение количества частиц обратно на CPU (использовать vkCmdDrawIndirectCount или Work Graphs)
- ❌ Биллбординг на CPU (вычислять строго в Mesh/Vertex шейдере)

## UI / Типографика
- ❌ Immediate Mode (ImGui) для релизного HUD
- ❌ Загрузка шрифтов в рантайме (парсинг .ttf/.otf во время игры)
- ❌ Альфа-блендинг перекрытых окон (100% перекрытые слои не рисовать)

## AI / Навигация
- ❌ Индивидуальный перерасчет пути (Per-Agent A*)
- ❌ Прямое чтение координат игрока каждым кадром
- ❌ CPU Raycasts для AI vision (использовать Global SDF distance checks)
- ❌ ООП деревья поведения с виртуальными методами

## ECS
- ❌ Структурные изменения в горячем цикле (entity.add/remove/destruct внутри итерации)
- ❌ Глубокие иерархии сущностей (entity.child_of для тысяч динамических объектов)
- ❌ Указатели между сущностями (использовать 64-bit GUIDs)

## Asset Cooker
- ❌ Текстовые форматы в рантайме (JSON должен компилироваться в бинарный)
- ❌ Неявное выравнивание памяти (использовать явное выравнивание)
- ❌ Синхронный парсинг исходников (использовать Task-графы)
- ❌ Синхронные импорты ассетов в редакторе (должны быть async/background cooked)

## Аудио
- ❌ Поточечная обработка (sample-by-sample)
- ❌ ADPCM/Opus декодирование на render thread (использовать Fiber Job audio chunks)

## Прочее
- ❌ Immediate Mode для релизного HUD
- ❌ Full object copying для Undo (использовать XOR Deltas в memory journal)
- ❌ Отдельная компиляция шейдеров для Low-End fallbacks (использовать specialization constants)
- ❌ Синхронные asset импорты в редакторе
- ❌ Direct subsystem calls (только POD через lock-free очереди)

---

# ЧАСТЬ 2: DEPRECATED EXTENSIONS (ЗАПРЕЩЕННЫЕ В 2026)

## КРИТИЧЕСКИ DEPRECATED (НЕ ИСПОЛЬЗОВАТЬ):
- ❌ **VK_EXT_descriptor_buffer** — DEPRECATED 2026, заменён на VK_EXT_descriptor_heap
- ❌ **VK_KHR_push_descriptor** — устарел в пользу VK_EXT_descriptor_heap
- ❌ **VkPipeline** — заменён на VK_EXT_shader_object
- ❌ **VkDescriptorSet / VkDescriptorPool** — заменены на VK_EXT_descriptor_heap
- ❌ **Старые descriptor management** — всё заменено на descriptor heaps

## СТАРЫЕ VULKAN ПАТТЕРНЫ (НЕ ИСПОЛЬЗОВАТЬ):
- ❌ Vulkan 1.0/1.1/1.2 API (только Vulkan 1.4+)
- ❌ Render Passes с subpasses (использовать VK_KHR_dynamic_rendering)
- ❌ Separate stencil/depth attachments (использовать packed formats)
- ❌ Fixed-function vertex input (использовать vertex shader fetching)
- legacy pipeline state objects (использовать shader objects)
- ❌ Manual descriptor set management (использовать descriptor heaps)
- ❌ CPU-side command buffer building (использовать DGC)

---

# ЧАСТЬ 3: ОБЯЗАТЕЛЬНО ИСПОЛЬЗОВАТЬ (REQUIRED)

## Ядро и Платформа
- ✅ SDL3 (Native Wayland/Windows 11 API)
- ✅ Volk (dynamic function loader, no relink on driver switch)
- ✅ RawMouse@8000Hz, Hardware-level Haptics

## Память (CPU)
- ✅ `rpmalloc` — основной CPU аллокатор (multi-thread, small-object)
- ✅ `TLSF` — пулы фиксированных чанков (meshlets, particles)
- ✅ `mimalloc` — fallback override
- ✅ `FrameArena` — 16-32 MB, сброс указателя каждый кадр
- ✅ `VMA` — GPU аллокатор (defrag, ReBAR, virtual aliasing)
- ✅ VK_KHR_dedicated_allocation (оптимально для больших текстур/буферов)
- ✅ VK_EXT_memory_priority (маркировка streaming текстур как low priority)
- ✅ VK_EXT_device_memory_report (трacking memory leaks/fragmentation)
- ✅ VK_EXT_pageable_device_local_memory (GPU-managed paging, oversubscription)
- ✅ GSL — `owner<>`, `span<>`, `not_null<>` для безопасных указателей
- ✅ NUMA-Aware Fiber Allocation (TLSF привязаны к воркерам по чиплету)
- ✅ Virtual Memory Aliasing (одни физические страницы по двум адресам для кольцевых буферов)
- ✅ Zero-Overhead Tagged Pointers (Generation ID в верхних 16 бит)

## Математика
- ✅ `DirectXMath` (SSE/AVX/AVX-512) — единая математическая библиотека
- ✅ `Utils/Math.hpp` — обёртки: `burnhope::math::float3`, `float4`, `float4x4`, `quaternion`
- ✅ `XMFLOAT3`/`XMFLOAT4`/`XMFLOAT4X4` для хранения
- ✅ `XMMATRIX`/`XMVECTOR` для вычислений
- ✅ `DirectX::PackedVector::HALF` для half-float
- ✅ World position: float32, Normals/UV/color/roughness: float16_t, Physics large world: double
- ✅ OpDP4A (8-bit dot product для normals/color packing)
- ✅ FMA (fused multiply-add для замены sin/cos/pow в PBR)
- ✅ packHalf2x16 / packSnorm2x16 (register compression в шейдерах)
- ✅ Normal Encoding: Octahedral projection → 1× uint32_t
- ✅ Quaternion: Dual Quaternion, 16-bit half, 16 bytes/bone
- ✅ FastNoiseLite (Perlin, Simplex, Cellular)
- ✅ PCG Hash (inline GLSL/HLSL для film grain, GI eviction)

## ECS
- ✅ `Flecs` — Archetype-Based SoA (Structure of Arrays)
- ✅ `alignas(64)` для AVX-512 кэш-линий
- ✅ 64-bit GUIDs (MurmurHash3) — никаких указателей между сущностями
- ✅ BDA (Buffer Device Address) для GPU-данных
- ✅ Dirty-Flags Batching для синхронизации CPU→GPU
- ✅ ReBAR-Native Components (GPU-reading components в ReBAR-памяти)
- ✅ Custom Fiber OS-API (подмена ecs_os_api на Fiber Job System)
- ✅ Compile-Time Archetype Sealing (блокировка графа архетипов)
- ✅ Decoupled Systems (flecs::query вместо встроенных systems)
- ✅ Dirty Chunk Bitmasks (ecs_changed() на уровне блоков памяти)

## Многопоточность
- ✅ `Taskflow` + C++20 coroutines (Fiber pool)
- ✅ 1 worker на физическое ядро
- ✅ `alignas(64)` для очередей (false-sharing prevention)
- ✅ Lock-Free MPMC ring queue (EventBus)
- ✅ `SpinLock` с `_mm_pause` (только где абсолютно необходимо)
- ✅ Timeline Semaphores для CPU-GPU синхронизации
- ✅ 3-stage Frame Pipeline (AI/Input/Physics → CB Build → GPU Execute)
- ✅ Split Barriers (vkCmdSetEvent2/vkCmdWaitEvents2)
- ✅ Async Overlap Rule (ALU-bound с Bandwidth-bound, никогда два ALU одновременно)
- ✅ VK_KHR_synchronization2 (Memory_Barrier_2 для fine-grained control)

## Логирование / Отладка / Профилирование
- ✅ `spdlog` — thread-safe, async логирование
- ✅ `fmt::format` / `std::format` — форматирование строк
- ✅ `Tracy` — CPU+GPU профайлер
- ✅ `VK_AMD_buffer_marker` — breadcrumbs для GPU crash detection
- ✅ `VK_EXT_device_fault` — дамп регистров GPU при TDR
- ✅ `Backward-cpp` — stack trace с file+line
- ✅ `Crashpad` — out-of-process minidump
- ✅ Flight Data Recorder / Blackbox (50 MB cyclic buffer для crash recovery)
- ✅ Hardware Performance Counters (PMU tracking: Cache Misses, Branch Mispredictions)
- ✅ Shader Occupancy Telemetry (Wave Occupancy, divergence counters)
- ✅ VK_KHR_calibrated_timestamps (микросекундная корреляция CPU/GPU времени)

## Ассеты / Сериализация
- ✅ `FlatBuffers` — zero-copy ECS state, prefabs
- ✅ `bitsery` — bit-level network serialization
- ✅ `simdjson` — SIMD JSON парсинг
- ✅ `Blake3` — content-addressable dedup
- ✅ `xxHash` — hot-reload, buffer diff
- ✅ `Meow Hash` — AES-NI I/O hashing
- ✅ `MurmurHash3` — 64-bit GUIDs
- ✅ Zero-Copy Packaging (align to 64KB NVMe chunks, Blake3 dedup)
- ✅ GDeflate 64 KB chunks + .bhdict

## Vulkan 1.4 2026 КРИТИЧЕСКИЕ EXTENSIONS

### BREAKING CHANGES 2026 (ОБЯЗАТЕЛЬНЫ)
- ✅ `VK_EXT_descriptor_heap` — полная замена descriptor системы (DEPRECATES descriptor_buffer)
  - Console-like descriptor system
  - Direct memory access without descriptor sets
  - Supersedes VK_EXT_descriptor_buffer
- ✅ `VK_KHR_device_address_commands` — GPU command generation via BDA, CPU-free processing
  - GPU сам генерирует команды через прямые адреса памяти
  - Полностью устраняет CPU involvement в command processing
- ✅ `VK_AMDX_shader_enqueue` — Hardware Work Graphs, GPU autonomous task scheduling
  - GPU автономное планирование задач
  - Zero CPU involvement в task scheduling
  - Hardware-level task graphs

### Rendering & Shaders (ОБЯЗАТЕЛЬНЫ)
- ✅ `VK_EXT_shader_object` — Shader Objects вместо Pipeline (устраняет VkPipeline)
- ✅ `VK_KHR_buffer_device_address` — BDA (Buffer Device Address для прямого доступа)
- ✅ `VK_EXT_device_generated_commands` — DGC (GPU сам генерирует команды)
- ✅ `VK_KHR_dynamic_rendering` — Dynamic Rendering (без render passes)
- ✅ `VK_KHR_dynamic_rendering_local_read` — read current render target in same pass
- ✅ `VK_KHR_present_wait` + `VK_KHR_present_id` — zero-latency frame pacing
- ✅ `VK_KHR_timeline_semaphore` — Timeline Semaphores (CPU-GPU sync)
- ✅ `VK_EXT_host_image_copy` — UI tex → VRAM без staging
- ✅ `VK_KHR_ray_query` — inline ray tracing из Compute (без pipeline)
- ✅ `VK_KHR_ray_tracing_position_fetch` — точная позиция хита без any-hit шейдера
- ✅ `VK_KHR_fragment_shading_rate` — VRS (Variable Rate Shading)
- ✅ `VK_KHR_cooperative_matrix` — tensor cores (FP16/INT8)
- ✅ `VK_KHR_cooperative_matrix 2.0` — FP8/INT4 support (2x-4x inference speed)
- ✅ `VK_EXT_mesh_shader` — Mesh/Task шейдеры (GPU-driven geometry)
- ✅ `VK_KHR_opacity_micromap` — OMM (hardware alpha-test в RT)
- ✅ `VK_EXT_opacity_micromap` — displacement micromaps (DMM)
- ✅ `VK_KHR_fragment_shader_interlock` — atomic fragment operations, OIT без sorting
- ✅ `VK_EXT_shader_atomic_float` / `VK_EXT_shader_atomic_float2` — atomic float16/float32/float64
- ✅ `VK_EXT_shader_image_atomic_int64` — 64-bit atomics для изображений
- ✅ `VK_KHR_shader_expect_assume` — branch prediction hints для GPU
- ✅ `VK_KHR_shader_maximal_reconvergence` — improved convergence после divergence
- ✅ `VK_KHR_shader_subgroup_extended_types` — extended types для subgroup ops
- ✅ `VK_KHR_shader_subgroup_uniform_control_flow` — reduced divergence penalty
- ✅ `VK_EXT_shader_tile_image` — direct framebuffer access в fragment shader
- ✅ `VK_EXT_early_fragment_tests` — depth test перед fragment shader execution
- ✅ `VK_NV_shader_subgroup_partitioned` — dynamic wave partitioning (zero divergence)
- ✅ `VK_NV_compute_shader_derivatives` — dFdx/dFdy в compute шейдерах
- ✅ `VK_KHR_shader_float_controls2` — absolute float behavior control (fast rounding)
- ✅ `VK_KHR_shader_subgroup_rotate` — subgroup data rotation (eliminates LDS)
- ✅ `VK_KHR_shader_relaxed_extended_instruction` — relaxed math variants
- ✅ `VK_EXT_shader_replicated_composites` — hardware structure replication
- ✅ `VK_EXT_shader_long_vector` — long math vectors в registers (physics)
- ✅ `VK_NV_compute_occupancy_priority` — compute occupancy priority
- ✅ `VK_KHR_shader_abort` — soft shader abort на error (prevents GPU hangs)
- ✅ `VK_KHR_shader_float16_int8` — native FP16/INT8 arithmetic
- ✅ `VK_KHR_zero_initialize_workgroup_memory` — automatic LDS zero-init
- ✅ `VK_KHR_workgroup_memory_explicit_layout` — explicit LDS layout (bank conflict avoidance)
- ✅ `VK_EXT_shader_demote_to_helper_invocation` — demote вместо discard
- ✅ `VK_EXT_conservative_rasterization` — conservative raster
- ✅ `VK_EXT_depth_clip_control` — Depth Bounds Test
- ✅ `VK_EXT_depth_bias_control` — programmable depth bias granularity
- ✅ `VK_EXT_sample_locations` — programmable MSAA sample positions
- ✅ `VK_KHR_load_store_op_none` — skip load/store для unused attachments
- ✅ `VK_EXT_graphics_pipeline_library` — pipeline caching (faster compilation)
- ✅ `VK_KHR_pipeline_binary` — native shader binary caching (eliminates stutter)
- ✅ `VK_KHR_pipeline_library` — pipeline binary caching across applications
- ✅ `VK_EXT_pipeline_properties` — pipeline compilation feedback

### Memory & Synchronization
- ✅ `VK_EXT_pageable_device_local_memory` — GPU-managed paging
- ✅ `VK_KHR_dedicated_allocation` — optimal allocation для больших текстур/буферов
- ✅ `VK_EXT_memory_priority` — priority marking для streaming текстур
- ✅ `VK_EXT_device_memory_report` — memory leak/fragmentation tracking
- ✅ `VK_KHR_maintenance5` — descriptor indexing improvements
- ✅ `VK_KHR_external_fence_win32` — cross-process GPU sync
- ✅ `VK_EXT_nested_command_buffers` — command buffer reusability
- ✅ `VK_KHR_internally_synchronized_queues` — parallel queue submission
- ✅ `VK_NV_push_constant_bank` / `VK_KHR_shader_constant_data` — separate constant banks
- ✅ `VK_EXT_image_compression_control` — hardware compression control

### Display & Output
- ✅ `VK_EXT_hdr_metadata` — HDR10+ metadata для displays
- ✅ `VK_KHR_swapchain_mutable_format` — dynamic swapchain format change
- ✅ `VK_EXT_color_write_enable` — per-color-channel write masks
- ✅ `VK_EXT_line_rasterization` — accurate line rendering (Bresenham)
- ✅ `VK_EXT_provoking_vertex` — provoking vertex control
- ✅ `VK_NV_low_latency2` — NVIDIA Reflex integration

### Textures & Images
- ✅ `VK_EXT_image_2d_view_of_3d` — 2D view of 3D texture slice без copy
- ✅ `VK_EXT_image_view_min_lod` — min LOD clamp для texture streaming
- ✅ `VK_EXT_attachment_feedback_loop_layout` — feedback loops без separate passes
- ✅ `VK_EXT_attachment_feedback_loop_dynamic_state` — dynamic feedback loops
- ✅ `VK_NV_memory_decompression` — hardware texture decompression, UASTC support
- ✅ `VK_EXT_robustness2` — robustBufferAccess2/robustImageAccess2

### Performance
- ✅ `VK_NV_raw_access_chains` — raw memory access для SSBO
- ✅ `VK_EXT_descriptor_indexing` — partially bound descriptors, update-after-bind

## Физика
- ✅ Jolt Physics (multi-thread, Double Precision для больших миров)
- ✅ Jolt SSE42/AVX2/AVX512 отдельные static libs
- ✅ CPUID runtime dispatch → оптимальная ветка
- ✅ Fiber Job Bridge (наследует JPH::JobSystem)
- ✅ Temp Allocator: FrameArenaAllocator replaces Jolt default
- ✅ Zero-Copy GPU Sync (Jolt пишет в ReBAR DEVICE_LOCAL | HOST_VISIBLE)
- ✅ Broadphase Layers: NON_MOVING, MOVING, DEBRIS, SENSOR, PROJECTILE
- ✅ Tickrate LOD (≤50m: 60-120 Hz, 50-200m: 30 Hz, >200m: Sleep)
- ✅ State Interpolation (StatePrevious[], StateCurrent[] + GPU interpolation shader)
- ✅ Character Controller (JPH::CharacterVirtual)
- ✅ Vehicle (JPH::VehicleConstraint, JPH::TrackedVehicleController)
- ✅ Soft Bodies (Jolt v5.0+ native LBD, LOD swap >30m)
- ✅ Raycast Batching (накопить все raycasts → single batch)
- ✅ Determinism (JPH_ENABLE_DETERMINISM + FMA disabled)
- ✅ State Recorder (JPH::StateRecorder → binary dump для rollback)

## Геометрия / Модели
- ✅ glTF 2.0: fastgltf (SIMD, async read)
- ✅ FBX: OpenFBX (lightweight)
- ✅ USD: OpenUSD/Pixar
- ✅ OBJ: Assimp (cooker-only), tinyobjloader (fast)
- ✅ meshoptimizer (Forsyth reorder, Overdraw Minimization, Vertex Fetch)
- ✅ .BHMESH формат (GPU-Driven Geometry, BDA access, meshlets)
- ✅ .BHBONE формат (Skeleton, Dual Quaternion half, 16 bytes/bone)
- ✅ .BHANIM формат (Animation, Hermite splines, temporal chunks)
- ✅ .BHTEX формат (Texture, GDeflate 64KB chunks, SVT)
- ✅ .BHMAT формат (Material, bindless BDA pointers)
- ✅ .BHPFAB формат (Prefab, Morton Z-curve sorted instances)

## Анимация
- ✅ Ozz-animation (DOD, no OOP)
- ✅ TinySoothe (Mocap smooth)
- ✅ GPU Skinning (Compute Shader, Dual Quaternion blend)
- ✅ VK_KHR_cooperative_matrix для matrix palette skinning
- ✅ Motion Matching (phase-space FFT vectors)
- ✅ Visibility-Driven Skinning (Meshlet Culling FIRST → Skinning SECOND)

## Шейдеры (MICRO-OPTIMIZATION)
- ✅ Target Metric: Occupancy (Wave fill rate), Goal: 100% ALU utilization
- ✅ VGPR Reduction (≤64 VGPR budget)
- ✅ Subgroup Replace Shared (subgroupQuadSwap, subgroupMin, subgroupBallot)
- ✅ Branchless (step()/mix(), VK_KHR_shader_maximal_reconvergence)
- ✅ FP16 Global (GL_EXT_shader_explicit_arithmetic_types_float16)
- ✅ FP8/INT8 Optimization (VK_KHR_cooperative_matrix 2.0)
- ✅ Transcendental Elimination (FMA polynomial, Taylor series, 1D LUT)
- ✅ Memory Coalescing (pack structs into uvec4 → single 128-bit OpLoad)
- ✅ ILP Pattern (texture fetch → 10-15 ALU → apply data)
- ✅ Loop Unroll (Specialization Constant + #pragma unroll)
- ✅ LDS Bank Conflict Fix (shared float data[1024 + 32]; data[index + (index / 32)])
- ✅ Subgroup Size Control (VK_EXT_subgroup_size_control)
- ✅ Expect Assume Branching (VK_KHR_shader_expect_assume)
- ✅ Atomic Float Reductions (VK_EXT_shader_atomic_float)
- ✅ Shader Interlock OIT (VK_KHR_fragment_shader_interlock)
- ✅ Zero Init LDS (VK_KHR_zero_initialize_workgroup_memory)
- ✅ Explicit LDS Layout (VK_KHR_workgroup_memory_explicit_layout)
- ✅ Tile Image Access (VK_EXT_shader_tile_image)
- ✅ Sample Location Control (VK_EXT_sample_locations)
- ✅ Load Store None (VK_KHR_load_store_op_none)
- ✅ Float16 Int8 Native (VK_KHR_shader_float16_int8)
- ✅ Image Atomic Int64 (VK_EXT_shader_image_atomic_int64)
- ✅ Subgroup Partitioned (VK_NV_shader_subgroup_partitioned)
- ✅ Compute Derivatives (VK_NV_compute_shader_derivatives)
- ✅ Float Controls2 (VK_KHR_shader_float_controls2)
- ✅ Subgroup Rotate (VK_KHR_shader_subgroup_rotate)
- ✅ Relaxed Instructions (VK_KHR_shader_relaxed_extended_instruction)
- ✅ Replicated Composites (VK_EXT_shader_replicated_composites)
- ✅ Raw Access Chains (VK_NV_raw_access_chains)
- ✅ Long Vector (VK_EXT_shader_long_vector)
- ✅ Scalarize Waterfall (matID == subgroupMin(matID) для L1 bindless read)
- ✅ Constant Bank Isolation (VK_NV_push_constant_bank / VK_KHR_shader_constant_data)

## Шейдерная Компиляция
- ✅ Cooker Precompile (SPIR-V micro-database offline)
- ✅ Warmup на loading screen (vkCreateShadersEXT для всех permutations)
- ✅ Generic Fallback (Generic PBR shader → background compilation → atomic BDA replace)
- ✅ Pipeline Library Caching (VK_KHR_pipeline_library)
- ✅ Pipeline Properties Query (VK_EXT_pipeline_properties)
- ✅ Graphics Pipeline Library (VK_EXT_graphics_pipeline_library)
- ✅ Pipeline Binary Caching (VK_KHR_pipeline_binary)
- ✅ Shader Abort (VK_KHR_shader_abort)

## Материалы / BRDF
- ✅ Multi-Scattering GGX (energy conservation)
- ✅ Random Walk SSS (skin)
- ✅ Charlie BRDF (cloth/hair sheen)
- ✅ Dual GGX (wide+narrow specular для skin/plastic)
- ✅ Melanin physical model + UV Root-to-Tip (hair color)
- ✅ Iris Depth Parallax + Limbal Ring Specular Mask (eyes)
- ✅ FFT displacement + Single Scattering (ocean)
- ✅ Height-based terrain blend
- ✅ Texture Space Shading (heavy grass/fur lighting cache)
- ✅ D-Buffer decals (correct normal-aware lighting)

## Ray Tracing / GI
- ✅ VK_KHR_ray_query из Compute Shader (inline RT)
- ✅ VK_KHR_opacity_micromap (foliage/alpha-test hardware rejection)
- ✅ VK_NV_ray_tracing_invocation_reorder (SER)
- ✅ ReSTIR DI (Spatial + Temporal reuse)
- ✅ ReSTIR GI (spatiotemporal reservoir reuse)
- ✅ Hash Grid GI (World-Space Radiance Cache)
- ✅ Surfel GI (spatial hash surfel sampling)
- ✅ Neural GI (MLP inference via VK_KHR_cooperative_matrix)
- ✅ SG Lightmaps (BC6H_UFLOAT + BC5_UNORM, BDA access)
- ✅ DDGI Probes (dynamic probes для NPC)
- ✅ Bent Normals baked (sky accessibility + AO)
- ✅ VXGI Fallback (voxelization + cone tracing без RT cores)
- ✅ Full Path Tracing (ReSTIR PT, infinite bounces via Radiance Caching)
- ✅ Position Fetch (VK_KHR_ray_tracing_position_fetch)

## Тени
- ✅ VSM (Virtual Shadow Maps) — Sparse Binding, 128×128 pages
- ✅ SMRT (Shadow Map Ray Tracing на VSM)
- ✅ Capsule Shadows (аналитическое cone-capsule intersection)
- ✅ Screen Contact Shadows (Ray March over depth buffer)

## Постобработка
- ✅ UBER POST (single Compute Shader, 1 pixel read)
- ✅ AgX Tonemapping (linear space)
- ✅ Subgroup Histograms (~0.01 ms exposure)
- ✅ 3D LUT Color Grading (VK_FORMAT_A2B10G10R10_UNORM_PACK32)
- ✅ VRS (VK_KHR_fragment_shading_rate, 1×1/1×2/2×2/4×4)
- ✅ GTAO (deinterleaved, HZB-stepped ray)
- ✅ HZB SSRT (screen-space reflections via HZB pyramid)
- ✅ Froxel Volumetrics (160×90×64 grid, temporal 3D TAA)
- ✅ Atmosphere LUT (precomputed Transmittance + In-scatter)
- ✅ Dynamic Resolution (GPU timing + PID controller)
- ✅ Upscalers (DirectSR auto-selects FSR/DLSS/XeSS)
- ✅ Frame Generation (velocity-based)
- ✅ HDR10/ST.2084 PQ output
- ✅ VK_EXT_hdr_metadata
- ✅ Stylization/NPR (Kuwahara, Hatching, Edge Detection)

## UI / Типографика
- ✅ ImGui (Docking Branch) — для редактора
- ✅ ImGuizmo (3D manipulators)
- ✅ nfd-extended (native file dialogs)
- ✅ Yoga/Meta (Flexbox C++ layout engine)
- ✅ Rive C++ Runtime (GPU vector animation)
- ✅ HarfBuzz (Arabic, CJK, ligatures)
- ✅ .BHFONT format (MSDF 2.0, HarfBuzz GPU shaping)
- ✅ VK_KHR_fragment_shader_barycentric (subpixel edge reconstruction)
- ✅ Pre-Baked Static HUD (Cooker анализирует макет, запекает в .BHMESH)
- ✅ Zero-CPU Asset Upload (DirectStorage → VRAM)
- ✅ Analytical Mesh Shader Primitives (центр, ширина, высина, радиус → Mesh Shader)

## Звук
- ✅ miniaudio (header-only, low-latency, DSP filters)
- ✅ .BHAUD format (ADPCM для SFX, Opus для streams)
- ✅ DirectStorage streaming (64 KB chunks)
- ✅ RT Audio (ray-traced audio via acoustic voxel octree)
- ✅ Spatial Audio GPU (Async Compute, VK_KHR_ray_query)
- ✅ Spatial Audio CPU (Jolt RaycastBatcher)
- ✅ DSP Mixing (directed graph, SIMD batch processing, AVX2/AVX-512)
- ✅ HRTF (Head-Related Transfer Function, AVX vectorized)
- ✅ Physics Audio Sync (Lock-Free Event Bus)
- ✅ Pre-Baked Acoustic Portals
- ✅ GPU-Accelerated Convolution Reverb
- ✅ Predictive Amplitude Culling
- ✅ Procedural Impact Synthesis
- ✅ Block Processing (SoA, 256/512 samples)
- ✅ Voice Pooling (fixed pool на старте движка)

## VFX / Частицы
- ✅ GPU Particle System (Compute Shader, fixed-size arrays)
- ✅ PBD GPU (Position Based Dynamics в Compute)
- ✅ Particle SDF Collisions (baked scene SDF R8_UNORM 3D texture)
- ✅ VK_EXT_shader_atomic_float для particle physics
- ✅ NanoVDB/NVIDIA (sparse volumes, GPU ray march)
- ✅ FastNoiseLite (terrain, clouds, destruction)
- ✅ LibWFC (Wave Function Collapse, tile-logic)
- ✅ Hardware Work Graphs (VK_AMDX_shader_enqueue)
- ✅ Mesh-Shader Particle Culling (Task Shader groups 64 particles)
- ✅ Compute-Based Frustum Binning (16×16 tile grid)
- ✅ Subgroup Bitonic Sort (VK_KHR_shader_subgroup_rotate)
- ✅ VRS-Driven Overdraw Mitigation

## AI / Навигация
- ✅ .BHNAV format (3D Flow Fields, Morton ordered)
- ✅ Behavior GOAP Vectorized (bitmasks, subgroupBallot)
- ✅ Neural AI (VK_KHR_cooperative_matrix inference)
- ✅ SDF-Vision (Ray-less, Global SDF distance checks)
- ✅ GPU RVO (Reciprocal Velocity Obstacles)
- ✅ Tensor Influence Maps (convolution на tensor cores)
- ✅ Render-Aware Aggression (NPC читают HZB/VisBuffer)
- ✅ Macro-Micro Routing (CPU macro-graph, GPU detailed flow field)
- ✅ Trajectory-Driven Animation Coupling
- ✅ Data-Driven BT (compiled bytecode)
- ✅ Blackboard как shared memory (flat arrays)
- ✅ Async Sensor Queues (POD в lock-free queue)

## Виртуальная Геометрия / Террейн
- ✅ .BHVIRT format (DAG of Meshlet Clusters)
- ✅ LOD Selection (Task Shader, Wave32/64 evaluation)
- ✅ Hybrid Rasterization (Hardware >4px, Software <4px)
- ✅ GPU Page Faults (DirectStorage streaming)
- ✅ .BHTER format (Virtual Heightfield Mesh + Clipmaps)
- ✅ Foliage Instancing (Compute Shader + Biome Seed)

## Погода / Атмосфера
- ✅ Sky Dome (Analytical Rayleigh/Mie LUTs)
- ✅ Volumetric Clouds (.BHCLOUD, Sparse Ray-Marching)
- ✅ Weather System (GVF 3D Texture wind, Precipitation Mesh Shader)
- ✅ Fluid Simulation (.BHSIM, Shallow Water Equations, Hybrid SPH)
- ✅ VK_KHR_cooperative_matrix для fluid/clouds

## Деформируемые Объекты
- ✅ Hair Strands (.BHHAR, Guide Hair XPBD, Marschner BRDF)
- ✅ Cloth Simulation (Multi-Layer XPBD, Tiled Constraint Solving)
- ✅ Soft Bodies (Cluster-Based PBD, tetrahedral volume preservation)
- ✅ VK_KHR_cooperative_matrix для constraints

## Asset Cooker
- ✅ Semantic Meshlet Clustering
- ✅ Neural Micro-Baking (MLP восстанавливает детали)
- ✅ Pre-Calculated GPU Commands
- ✅ Symbolic Sanity Validation
- ✅ Global Dictionary Training
- ✅ Zero-Copy Ready structures (C++ == runtime byte-perfect)
- ✅ Hot/Cold data separation
- ✅ Deterministic builds

## IO / Streaming
- ✅ DirectStorage (NVMe→PCIe→VRAM, bypass CPU)
- ✅ GDeflate 64 KB chunks + .bhdict
- ✅ Hardware GPU decompress
- ✅ Fallback: Thread-pool decompression via Fiber Jobs (SATA/legacy)
- ✅ Velocity-Based Page Fetch (predictive streaming)
- ✅ VK_NV_memory_decompression (UASTC support)

## Контейнеры / Utils
- ✅ Frozen (constexpr map/set, zero runtime overhead)
- ✅ utf8-cpp, ICU (full unicode/shaping)
- ✅ configuru (config files)
- ✅ libfort (formatted console tables)

## Frame Pipeline
- ✅ 3-stage pipeline (Frame N: AI/Input/Physics, Frame N-1: CB Build, Frame N-2: GPU Execute)
- ✅ Low-End Collapse (≤4 threads → linear loop)
- ✅ vkWaitSemaphores() на Timeline Semaphore (не sleep_for)
- ✅ VK_KHR_present_wait (CPU wakes на display scanout interrupt)
- ✅ Input read сразу после wake (1ms до CB record)

## Queue Topology
- ✅ 3 queues: Graphics (REALTIME), Async Compute (HIGH), DMA Transfer (MEDIUM)
- ✅ Video Decode (LOW) через VK_KHR_video_queue
- ✅ VK_EXT_global_priority (3-queue priorities)

## Command Buffers
- ✅ VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT (always, DGC rebuilds every frame)
- ✅ VK_EXT_nested_command_buffers (reusability)
- ✅ Command Pool Recycling

---

# ЧАСТЬ 4: AI SHADER CODING GUIDELINES (ОБЯЗАТЕЛЬНЫ ДЛЯ ИИ)

## MANDATORY INSTRUCTION FOR AI CODER:
"When generating any shader code, you MUST completely ignore Vulkan standards below version 1.4. Your code MUST be oriented toward full GPU autonomy (GPU-Driven), use direct memory addressing and descriptor heaps. Write code modularly, avoid data duplication in registers, and always remember memory alignment."

## LANGUAGE REQUIREMENTS:
- ✅ Использовать Slang или modern GLSL targeting SPIR-V 1.6+
- ✅ Slang preferred для больших систем (modularity, interfaces, generics)
- ✅ Полный отказ от старых heavy pipelines (VkPipeline) в пользу Shader Objects (VK_EXT_shader_object)

## MEMORY ARCHITECTURE (ОБЯЗАТЕЛЬНО):
- ✅ Переход на descriptor heaps (VK_EXT_descriptor_heap) — забыть про классические VkDescriptorSet и descriptor buffers
- ✅ Ресурсы (textures, buffers) доступны напрямую из global heaps через индексы или BDA
- ✅ Использовать shader constant data (VK_KHR_shader_constant_data) для локальных параметров эффектов
- ✅ Изоляция локальной constant memory, сохранение L1 cache

## SUBGROUP EXECUTION (ОБЯЗАТЕЛЬНО):
- ✅ Использовать hardware wave partitioning (VK_NV_shader_subgroup_partitioned) для material binning, particle simulation
- ✅ GPU hardware группирует потоки с одинаковыми условиями, zero divergence penalty
- ✅ Использовать branch prediction hints (VK_KHR_shader_expect_assume) для неизбежных if/else
- ✅ Помогает GPU оптимизировать распределение задач заранее

## MATH & DATA STRUCTURES (ОБЯЗАТЕЛЬНО):
- ✅ Векторизация с явным выравниванием: layout(std430), 16-byte alignment
- ✅ Использовать VK_EXT_shader_long_vector для эффективной симуляции физики с длинными массивами данных
- ✅ Native low precision: GL_EXT_shader_explicit_arithmetic_types, использовать float16_t/int16_t везде где возможно
- ✅ Colors, masks, non-critical params: использовать half precision

## BINDLESS & TEXTURES (ОБЯЗАТЕЛЬНО):
- ✅ Dynamic texture indexing: ВСЕГДА использовать nonuniformEXT (textures[nonuniformEXT(MaterialID)].sample())
- ✅ Dynamic branches: использовать textureLod с явным LOD level, precompute gradients
- ✅ Никогда texture()/Sample() внутри dynamic if/else

## SUBGROUP & EXECUTION (ОБЯЗАТЕЛЬНО):
- ✅ Intra-wave data: использовать subgroupBarrier(), НИКОГДА groupMemoryBarrier()
- ✅ Per-wave scalars: использовать subgroupBroadcastFirst() для общих данных (sun position, camera params)
- ✅ Subgroup rotate (VK_KHR_shader_subgroup_rotate): устраняет LDS для blur/neighbor operations

## MEMORY & BUFFERS (ОБЯЗАТЕЛЬНО):
- ✅ Bool flags: упаковывать в bitmasks (32 flags per uint), никогда 32-bit bool переменные
- ✅ Loop variables: кэшировать SSBO reads локально перед началом loop
- ✅ Alignment: строгий std430, 16-byte alignment, explicit padding
- ✅ Raw access chains (VK_NV_raw_access_chains): обходить сложную адресацию для SSBO

## MATHEMATICS (ОБЯЗАТЕЛЬНО):
- ✅ Division: предвычислять 1.0/x перед loop, умножать внутри
- ✅ Float mixing: избегать float/float16_t смешивания в одном выражении
- ✅ Trigonometry: sin/cos/atan в циклах → 1D LUT или FMA polynomial
- ✅ Float controls: VK_KHR_shader_float_controls2 для fast rounding в PBR

## RAY QUERY (ОБЯЗАТЕЛЬНО):
- ✅ ВСЕГДА добавлять max step limit к rayQueryProceedEXT() циклам (предотвращает GPU hangs)
- ✅ RT за пределами 20m: использовать SSR + baked maps, никогда honest RT

## OFFLINE BAKING PRIORITY (ОБЯЗАТЕЛЬНО):
- ✅ Maximum offline baking, minimum real-time computation
- ✅ Heavy work в Cooker, runtime только собирает готовые кадры

---

# ЧАСТЬ 5: VULKAN 1.4 2026 OPTIMIZATION CONCEPTS

## НОВЫЕ КОНЦЕПТЫ ОПТИМИЗАЦИИ 2026:

### ТЕКСТУРЫ (BHTEX) — Максимальная свобода и скорость:
- ✅ Аппаратный Sampler Feedback: видеокарта сама запрашивает нужные кусочки текстуры
- ✅ Поддержка UDIM прямо в SVT: одна логическая развертка, сетка квадратов для гигантского разрешения
- ✅ Процедурная генерация страниц "на лету": видеокарта рисует детали в пустые страницы виртуальной текстуры
- ✅ Оптимизированное цветовое пространство (YCoCg) для масок: сохраняет резкие детали без артефактов сжатия

### МАТЕРИАЛЫ (BHMAT) — Гибкость без потери производительности:
- ✅ Послойные материалы через Work Graphs (VK_AMDX_shader_enqueue): GPU сам разбивает экран на микро-задачи
- ✅ Глобальный массив профилей подповерхностного рассеивания (SSS): единственная LUT-текстура для всех материалов
- ✅ Bindless-декали в один проход: единый глобальный массив декалей

### ЗАПЕКАНИЕ ДЛЯ МОДЕЛЕЙ И ГЕОМЕТРИИ:
- ✅ Запеченная анимация для массовки (Vertex Animation Textures - VAT): движения запекаются в текстуру
- ✅ Октаэдрические импосторы для дальних планов: запекание со всех ракурсов в октаэдрические текстуры
- ✅ Сверхплотная упаковка вершин (10-10-10-2): упаковка нормалей и касательных
- ✅ Предвычисленная видимость помещений (PVS - Potentially Visible Sets): заранее просчитанная пространственная сетка
- ✅ Запеченные дистанционные поля геометрии (Mesh SDF): детализированный SDF запекается в .BHMESH

### РЕНДЕР БЕЗ RT (Классический путь):
- ✅ Запекание кластеров света (Static Froxel Grid): заранее прописанные индексы в пространственной сетке
- ✅ Гибридные порталы и PVS: заранее просчитанная видимость в закрытых пространствах
- ✅ Глобальные маски теней (Shadow Masks): свет и тень от статичных объектов запекаются в легкие текстуры

### RAY TRACING (Умный и бережный подход):
- ✅ Ограничение длины луча (Ray Length Capping): полноценные лучи только 10–15 метров вокруг камеры
- ✅ RT только для динамики (Hybrid RT Shadowing): тензорные ядра только для лучей от фонарика/вспышек
- ✅ Валидация лучей через толщину стен (Light Leak Prevention): луч сверяется с запеченным SDF

### ТЕНИ И ВИЗУАЛ:
- ✅ VSM (Виртуальные карты теней) — Заморозка страниц: страницы тени "замораживаются" если нет движущихся объектов
- ✅ Аналитические капсульные тени (Capsule Shadows): тень от толпы строится по математическим капсулам
- ✅ Запекание микро-теней (Bent Normals): изогнутые нормали в .bhmesh заменяют тяжелый SSAO

### РЕНДЕР ПРОЗРАЧНОСТЕЙ И VFX:
- ✅ OFF-SCREEN TRANSPARENCY: густой дым/огонь рендерятся в отдельный буфер в уменьшенном разрешении
- ✅ VFX TICKRATE DECOUPLING: логика частиц далеко от камеры обновляется не каждый кадр (15-30 Гц)
- ✅ STOCHASTIC ALPHA-TESTING: паттерн Байера или Blue Noise для листвы

### ОПТИМИЗАЦИЯ ГЕОМЕТРИИ НА ЭТАПЕ COOKER'А:
- ✅ STATIC CLUSTER MERGING: Cooker сшивает неподвижные объекты с одинаковым материалом в единый супер-кластер
- ✅ SHADOW PROXY MESHES: отдельный упрощенный невидимый меш для отбрасывания теней

### ПРЕДИКТИВНЫЙ СТРИМИНГ:
- ✅ VELOCITY-BASED PAGE FETCH: движок предсказывает куда посмотрит игрок через 1-2 секунды

### ШУМ И ТЕМПОРАЛЬНОЕ НАКОПЛЕНИЕ:
- ✅ INTERLEAVED GRADIENT NOISE (IGN): единый глобальный паттерн шума для всех ресурсоемких эффектов

### PURE COMPUTE SHADING:
- ✅ Compute-Only Visibility Resolution: расшифровка Visibility Buffer в Compute Shader с VK_NV_compute_shader_derivatives

### DECOUPLED LIGHTING FREQUENCIES:
- ✅ Split Diffuse & Specular Passes: Diffuse в половинном разрешении, Specular в полном

### MESH SHADER MICRO-CULLING:
- ✅ Sample-Point Rejection: треугольник не рисуется если промахивается мимо центра пикселя

### ASYNCHRONOUS ROTATIONAL REPROJECTION:
- ✅ Decoupling Camera from Render: при вращении камеры пропускается генерация G-Buffer для статики

### DATA-DRIVEN MATERIAL ELIMINATION:
- ✅ Distance-Based Material Downgrade: на дистанции >15-20м сложный PBR заменяется на базовый Lambert+Phong

### ИНТЕЛЛЕКТУАЛЬНЫЙ РАСТЕРИЗАТОР:
- ✅ TASK SHADER DYNAMIC ROUTING: если мешлет из микро-треугольников, Task Shader пишет данные в SSBO для SOFTWARE_RASTER
- ✅ SUB-PIXEL JITTER REJECTION: Compute-растеризатор учитывает сдвиг камеры до вычисления барицентрических координат

### АДАПТИВНЫЙ ШЕЙДИНГ (CONTENT-ADAPTIVE):
- ✅ COMPUTE-BASED VRS ANALYSIS: микро-Compute Shader анализирует Visibility Buffer и вычисляет VRS
- ✅ WAVE-MATCHED DEFERRED TEXTURING: subgroupPartitionNV для перегруппировки потоков по MaterialID

### УПРАВЛЕНИЕ ВРЕМЕНЕМ КАДРА:
- ✅ ASYNC PREDICTIVE CULLING: отсечение полностью в Async Compute параллельно с пост-процессингом
- ✅ STOCHASTIC CONTACT SHADOWS: 1-2 шага Ray Marching со случайным оффсетом вместо 8-12 тяжелых

### СВЕРХБЫСТРОЕ ОСВЕЩЕНИЕ:
- ✅ LIGHT GRID DEAD-ZONE CULLING: Compute Shader проверяет глубину HZB для отсечения мертвых зон
- ✅ ALBEDO-ONLY PASS: если кластер не содержит активных источников света, шейдинг переключается на микро-шейдер

### СВЕРХБЫСТРАЯ ПАМЯТЬ:
- ✅ SINGLE-PASS DOWNSAMPLE (SPD) для HZB: все 12 мип-уровней за один вызов благодаря subgroup ops
- ✅ ZERO-CPU ASSET STREAMING: DirectStorage (NVMe → PCIe) и VK_NV_memory_decompression

### КЭШИРОВАНИЕ И ЛОКАЛЬНЫЕ ОБНОВЛЕНИЯ:
- ✅ DIRTY-RECTANGLE SHADOW UPDATES: точный Bounding Box движущегося объекта внутри страницы VSM
- ✅ COMPUTE DECAL INJECTION: декали пакуются в пространственную кластерную сетку

### ЭКСТРЕМАЛЬНАЯ ПОСТОБРАБОТКА:
- ✅ TILE-MEMORY LOCAL READ: VK_KHR_dynamic_rendering_local_read для объединения Tonemapping, Color Grading, Film Grain
- ✅ ANALYTICAL MOTION BLUR: математическое растягивание Visibility-буфера в Compute Shader

### ЭВРИСТИКА ГЕОМЕТРИИ:
- ✅ DEPTH-ONLY PREPASS ДЛЯ ДИНАМИКИ: динамические объекты пускаются в микро-проход Depth-Only перед основным рендером

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ФИЗИКИ:
- ✅ HYBRID DEBRIS OFF-LOADING: Jolt обрабатывает только геймплейно-важные объекты, мелкие осколки передаются в GPU Compute
- ✅ ZERO-COST STATE DOUBLE-BUFFERING: подмена указателей в заголовке ECS-архетипа
- ✅ ASYNC AI SPATIAL QUERIES: тысячи запросов видимости от ИИ в SSBO → Async Compute Queue → GPU проверяет по Scene SDF

### ИЗОЛИРОВАННЫЕ ПРОСТРАНСТВА:
- ✅ RELATIVE ISLAND KINEMATICS: интерьер транспорта выделяется в отдельный изолированный экземпляр Jolt
- ✅ WAKE-UP PREDICTION: грубый математический Ray-March снаряда на шаг вперед в параллельном Fiber-воркере

### ОПТИМИЗАЦИЯ СЛОЖНЫХ СТРУКТУР:
- ✅ TENSOR-ACCELERATED JACOBIAN SOLVER: матрицы Якобиана отправляются в тензорные ядра через VK_KHR_cooperative_matrix
- ✅ TWO-WAY KINEMATIC-DYNAMIC COUPLING: процессор рассчитывает грубые "плавучие сферы", точная деформация на GPU

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ VFX:
- ✅ HARDWARE WORK GRAPHS ДЛЯ ЧАСТИЦ (VK_AMDX_shader_enqueue): GPU сам запускает узел-эмиттер, распределяет частицы
- ✅ MESH-SHADER PARTICLE CULLING: Task Shader группирует частицы по 64 штуки, отсекает по HZB
- ✅ COMPUTE-BASED FRUSTUM BINNING: Compute Shader разбивает экран на сетку 16×16 тайлов

### ОПТИМИЗАЦИЯ СМЕШИВАНИЯ:
- ✅ SUBGROUP BITONIC SORT: VK_KHR_shader_subgroup_rotate для аппаратной сортировки прозрачности
- ✅ VRS-DRIVEN OVERDRAW MITIGATION: автоматическое включение Variable Rate Shading 2×2/4×4 в густом дыму

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ЗВУКА:
- ✅ PRE-BAKED ACOUSTIC PORTALS: Cooker разбивает геометрию на "Акустические Зоны" и "Порталы"
- ✅ GPU-ACCELERATED CONVOLUTION REVERB: звуковой поток в VRAM, GPU накладывает эхо за микросекунды
- ✅ PREDICTIVE AMPLITUDE CULLING: легковесный Fiber-воркер просчитывает грубую громкость до декомпрессии
- ✅ PROCEDURAL IMPACT SYNTHESIS: 1 базовый сэмпл, SIMD-инструкции меняют Pitch/Attack/Envelope

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ASSET COOKER:
- ✅ SEMANTIC MESHLET CLUSTERING: Cooker группирует треугольники так, чтобы внутри одного мешлета был только один материал
- ✅ NEURAL MICRO-BAKING: Cooker обучает крошечную нейросеть восстанавливать микродетали
- ✅ PRE-CALCULATED GPU COMMANDS: в готовом .BHMESH лежит готовый бинарный блок команд для DMA-очереди
- ✅ SYMBOLIC SANITY VALIDATION: Cooker прогоняет каждый ассет через символическую валидацию
- ✅ GLOBAL DICTIONARY TRAINING: Cooker анализирует весь проект и создает один глобальный супер-словарь

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ИИ:
- ✅ TENSOR INFLUENCE MAPS: тактические карты как матричные свёртки на тензорных ядрах (VK_KHR_cooperative_matrix)
- ✅ RENDER-AWARE AGGRESSION: NPC читают пирамиду глубины (HZB) или Visibility Buffer
- ✅ MACRO-MICRO ROUTING: процессор считает "Макро-граф", видеокарта строит Векторное Поле в радиусе 50м
- ✅ TRAJECTORY-DRIVEN ANIMATION COUPLING: мозг NPC генерирует параметрическую кривую будущей траектории

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ UI:
- ✅ PRE-BAKED STATIC HUD: Cooker анализирует макет интерфейса, запекает неподвижные элементы в .BHMESH
- ✅ ZERO-CPU ASSET UPLOAD: текстуры интерфейса сразу в VRAM с аппаратной декомпрессией
- ✅ ANALYTICAL MESH SHADER PRIMITIVES: процессор отправляет только центр, ширину, высоту, радиус скругления
- ✅ PRE-SHAPED TEXT BATCHING: Cooker выполняет формовку текста оффлайн
- ✅ UI VISIBILITY CULLING: при полноэкранном инвентаре устанавливается глобальный битмаск окклюзии

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ FLECS ECS:
- ✅ REBAR-NATIVE COMPONENTS: компоненты ежекадров читаемые GPU размещаются в ReBAR-памяти
- ✅ CUSTOM FIBER OS-API: движок переопределяет ecs_os_api на Fiber Job System
- ✅ COMPILE-TIME ARCHETYPE SEALING: Cooker анализирует префабы, движок "запечатывает" граф архетипов
- ✅ DECOUPLED SYSTEMS: используются только закэшированные flecs::query
- ✅ DIRTY CHUNK BITMASKS: flecs::query с фильтром ecs_changed() на уровне блоков памяти

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ПАМЯТИ:
- ✅ NUMA-AWARE FIBER ALLOCATION: пулы памяти (TLSF) привязываются к воркерам по чиплету
- ✅ VIRTUAL MEMORY ALIASING: одни физические страницы отображены по двум адресам подряд для кольцевых буферов
- ✅ ZERO-OVERHEAD TAGGED POINTERS: Generation ID в верхних 16 бит указателя
- ✅ GPU UNIFIED HEAP DEFRAGMENTATION: Async DMA Queue для прозрачного сдвига блоков данных

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ПРОФИЛИРОВАНИЯ:
- ✅ FLIGHT DATA RECORDER / BLACKBOX: 50 МБ циклический "Черный ящик" для crash recovery
- ✅ HARDWARE PERFORMANCE COUNTERS (PMU TRACKING): чтение регистров процессора (Cache Misses, Branch Mispredictions)
- ✅ SHADER OCCUPANCY TELEMETRY: чтение статистики пайплайна (Wave Occupancy, divergence counters)

---

# ЧАСТЬ 6: ГЛАВНЫЙ ПАТТЕРН (MAIN PATTERN)

## The "2026 Way" для написания любого модуля:
1. ✅ Чтение данных из кэш-дружелюбного SoA (Structure of Arrays)
2. ✅ Вычисление с использованием AVX-512 или передача задачи в Async Compute на GPU
3. ✅ Запись результата в ReBAR или отправка POD-структуры в lock-free очередь
4. ✅ Полное отсутствие блокировок, аллокаций памяти и виртуальных вызовов
