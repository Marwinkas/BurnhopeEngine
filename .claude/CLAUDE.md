# BurnhopeEngine Architectural Constitution (`CLAUDE.md`)

Be uncompromising and thorough. Build fault-tolerant systems. Ship complete, compilable code.
- **Zero placeholders:** Never write `// TODO`, `// ... rest unchanged ...`, fake stubs, or partial implementations.
- **Zero crutches:** No warning pragmas to hide violations, no `const_cast`, no `reinterpret_cast` across non-trivial types, no backwards-compatibility wrappers. If a type sits in the wrong layer, move it down to the layer that may legally own it.
- **Black-box modularity:** Every module owns its memory, lifecycle, and typed interface. Sibling modules never mutate each other's internals, include each other's private headers, or trigger side effects in unrelated subsystems. Failures stay inside the subsystem's recovery contract (`BH_ASSERT` in debug, typed `false`/error return to caller).

---

## 1. Mission & Core Axioms

BurnhopeEngine is a from-scratch 3D engine and declarative GUI targeting Desktop x86_64 (Linux and Windows, AVX2). Layer 3 client code reads like a single line of Qt/Slate; the execution path is pure AAA: Vulkan 1.4, Data-Oriented Design (DOD), Flecs ECS, C++26, AVX2.

1. **Zero Hot-Path Allocation:** `Tick`, `Layout`, `Emit`, `Draw`, `Consume`, and `HitTest` never call `malloc`, `free`, `new`, `delete`, `std::string`, `std::vector::push_back`, `std::map`, `std::unordered_map`, or `std::make_shared`. Per-frame memory comes from `core/memory/FrameArena.hpp` (O(1) reset), ring buffers, fixed stack arrays, or `UiVirtualPool`.
2. **DOD & Cache Alignment:** Hot data is trivially copyable POD, `sizeof % 16 == 0` or `sizeof % 64 == 0`, with `alignas(16)` / `alignas(64)` and `static_assert` on GPU struct sizes. Trees and ASTs use `uint16_t` / `uint32_t` indices or `flecs::entity`, never `Node*`, `std::vector<Node*>`, or `std::unique_ptr<Node>`.
3. **Zero-Copy I/O:** Assets, SPIR-V, layouts, fonts, and textures load through `fileMapReadOnly` (`core/io/File.hpp`: `mmap` / `MapViewOfFile`). Numbers parse via `std::from_chars`. Formatting uses `core/Str.hpp`, `std::to_chars`, or bounded stack `snprintf`. No `sscanf`, no locale CRT parsers, no `std::string` concatenation.
4. **Flecs ECS Composition:** A widget or scene actor is a `flecs::entity` (or 16-bit ID) plus POD components. No vtables, no `QObject`, no ImGui.
5. **Vulkan 1.4 Exclusivity:** Dynamic rendering, shader objects, mesh shaders, bindless descriptor heaps, BDA, Synchronization2, timeline semaphores.

---

## 2. The Modular Pyramid (`N → <N`)

Dependencies point strictly downward. A file may include, forward-declare, or call only a strictly lower layer. Banning an include in `.hpp` and sneaking it into `.cpp` is the same violation.

LAYER 3   Domain Tools & Apps (demo/app/, core/files/, core/host/, editor/)
          Declarative coordinators (< 300–350 LOC). Sees Layers 2, 1, 0.
          Zero RHI, Yoga, or raw vertex internals. Uses Layer 1/2 facades only.
LAYER 2B  Composite Widgets & Kits (core/ui/widget/, core/ui/kit/)
          VirtualGrid, TreeView, PropGrid, ColorPicker, Calendar, Docking, Chrome,
          Breadcrumbs, AssetSlot, DragDrop, Rubberband.
          Built strictly by composing Layer 2A bricks—never via raw spawn() loops.
LAYER 2A  Atomic Widget Bricks (core/ui/widget/, core/ui/Panel)
          Panel, Label, Button, Check, Field, Slider, Number, Scroll, Splitter,
          Menu, Fold, Tip, Curve, Image, Thumb.
          One brick = one responsibility. Zero peer-to-peer coupling between bricks.
LAYER 1B  UI Engine Runtime (core/ui/, core/ui/rhi/)
          State, Node, LayoutBridge, Hit, Focus, Hover, Motion, Text, PrimitiveBatch,
          Input, Font, UiPhoto, UiWindowBridge, UiBin, Json, Canvas, UiRenderer.
          Zero knowledge of concrete widgets (no Button, Tree, Splitter, Field, or TitleBar).
LAYER 1A  Pure UI POD Contracts (core/ui/)
          Geometry, Flex, Style, Typography, Primitive, Metrics, Interaction.
          Pure data structs and enums. Zero widget logic.
LAYER 0   Hardware, OS, Memory, I/O (core/rhi/, core/platform/, core/io/, core/image/, etc.)
          Vulkan 1.4, SDL3, memory mapping, math, jobs. Zero knowledge of UI, Yoga, or ECS.
GRAPHICS  3D Engine & Passes (engine/gpu_scene/, engine/include/render/, engine/src/, engine/shader/)
          Built on Layer 0. Scene Flecs lives in engine/gpu_scene/. Zero UI or widget includes.

### 2.1 CMake Targets per Tier

Each `bh_*` target is an OBJECT-library module (`bh_module(name ...)`). Links point strictly downward.

| Target | Tier | Owns | Links |
| :--- | :--- | :--- | :--- |
| `bh_foundation` | 0 | Check, PlatformUtil, Jobs, Desktop, FrameArena, File, Json, WasmHost, pure POD `Input.hpp` | cpptrace (Tracy). No SDL3, no volk. |
| `bh_platform` | 1 | Window, WindowEvents, Dialog, Action | `bh_foundation`, SDL3 |
| `bh_rhi` | 0 | Device, Swapchain, GpuImage, GpuBuffer, DescriptorHeap, ShaderObject, MeshDraw, GpuProfiler | volk, Vulkan. No SDL3: the host passes surface extensions to `deviceCreateInstance`. |
| `bh_image` | 0 | ImageDecode, ImageCompress, ImageUpload | `bh_rhi`, `bh_foundation` |
| `bh_material` | 2 | Material (`.bhmat` read/write) | `bh_foundation` |
| `bh_asset` | 2 | AssimpImporter, ProjectDoc | `bh_foundation`, `bh_material`, assimp, meshoptimizer |
| `bh_audio` | 2 | Audio | `bh_foundation` |
| `bh_gpu_scene` | Graphics | SceneObject, SceneGizmo, Physics (Jolt) | flecs, Jolt |
| `bh_render` | Graphics | passes plus Scene{Frame,Actors,Geometry,CullUpload,Procedural,Targets,Textures,Uniforms,Lights} | `bh_gpu_scene`, `bh_audio`, volk |
| `bh_ui_core` | 1A/1B | Command, Node, NodeDump, Hit, Focus, Layout, Text, ... | `bh_foundation`, yoga, flecs |
| `bh_ui_bricks` | 2A | Button, Check, Field, FieldDraw, Slider, ... | `bh_ui_core`, `bh_foundation` |
| `bh_ui_kits` | 2B | Color, ColorPopup, ColorDraw, Calendar, Docking, Virtual, ... | `bh_ui_bricks` |
| `bh_ui_rhi` | 1B | Canvas, Font, UiPhoto, UiRenderer | `bh_ui_core`, `bh_rhi`, `bh_image` |
| `bh_ui_inspector` | 2B/3 | PropBind, PropImport, PropImportParse, PropGrid, PropFilter (declarative property inspector) | `bh_ui_kits`, `bh_ui_bricks`, `bh_ui_core`, `bh_foundation`. No RHI, SDL3, Window or Host. |
| `bh_files` | 3 | Files, FileSession, FileBind, ... | `bh_foundation`, UI targets, `bh_material`. No `bh_asset`. |
| `bh_host` | 3 | Host, SceneMask (window, swapchain and canvas runtime) | `bh_ui_inspector`, UI targets, `bh_rhi`, `bh_platform` |
| `bh_app` | 3 | `demo/app/*` coordinators | UI targets, `bh_files`, `bh_gpu_scene` |

Only `bh_host` and the executables link `bh_platform`; every other target stays SDL3-free.

### 2.2 `bh_firewall` Rules (CMake configure-time, FATAL_ERROR)

- `core/rhi`: no `platform/Window.hpp`, `ui/`, `files/`, `host/`, `SDL3/`.
- `core/ui`: no `rhi/`, `image/`, `ui/rhi/`, `ui/widget/`, `ui/kit/`, `files/`, `host/`.
- `core/ui/widget`, `core/ui/kit`: no `rhi/`, `image/`, `ui/rhi/`, `files/`, `host/`, `yoga/`.
- No `.hpp` in `core/`, `engine/` or `demo/` (except `core/ui/State.hpp` itself) may include `ui/State.hpp`; headers use `ui/Types.hpp` and the `.cpp` includes State.
- Widget headers and `Command.hpp`: no `flecs.h`.
- Atomic bricks: no composite kit headers.
- Tier 0 (foundation files): no `rhi/`, `image/`, `Window.hpp`, `ui/`, `files/`, `host/`, `gfx/`, `asset/`, `audio/`, volk, `SDL3/`.
- Tier 1 (platform without `Window*.cpp`): no `rhi/`, `image/`, `ui/`, `files/`, `host/`, `gfx/`, `asset/`, `audio/`, volk.
- Tier 2 (`gfx`, `asset`, `audio`, `scene`, `stream`) and Graphics (`engine/`): no `ui/`, `files/`, `host/`, `Window.hpp`.
- `core/files`: no `asset/` (material files go through `gfx/Material.hpp`).
- `core/image`, `core/files`, `core/audio`: no `platform/Window.hpp`, `Action.hpp`, `Dialog.hpp`, `SDL3/`.
- `PropGrid*`, `PropFilter*`, `PropBind*`, `PropImport*` (`bh_ui_inspector`): no `rhi/`, `platform/Window.hpp`, `ui/rhi/`, `host/Host.hpp`.
- `engine/include/render/*.hpp`: no `flecs.h`, no `Rtxdi/DI/ReSTIRDI.h` (only `Restir.cpp` includes it; the header holds a forward-declared `rtxdi::ReSTIRDIContext*`). Forward-declare `flecs::world`/`flecs::entity`.
- Rule of thumb: no source file exceeds 1,000 lines. Split along responsibility, with shared declarations in an internal header (`ColorInternal.hpp`, `PropImportInternal.hpp`).

---

## 3. Single Source of Truth (SSOT)

Never duplicate parsers, string helpers, math, or virtualization across modules. Push shared capabilities down to the canonical authority:

| Concern | Canonical Authority |
| :--- | :--- |
| JSON parsing | `core/io/JsonCursor.hpp`, `core/io/Json.hpp` (zero-copy). |
| Strings & numbers | `core/Str.hpp`, `std::from_chars`, `std::to_chars`. No `uiStrCopy` or `copyCap` duplicates. |
| Input snapshot | `core/platform/Input.hpp`. |
| Virtualization | `core/ui/widget/Virtual.hpp` (`UiVirtualBase`, `UiVirtualPool`, `kUiPoolCap`). |
| Text wrap & measure | `uiWrapCut` in `core/ui/Text.hpp`. |
| File mapping | `fileMapReadOnly` in `core/io/File.hpp`. Fonts use `fileMapReadOnly` + `FT_New_Memory_Face`. |
| Image decode/compress/upload | `core/image/` (decode from mapped span, BC1/3/4/5/6H/7, Host Image Copy or staging). |
| Time, paths, host meters | `core/platform/PlatformUtil.hpp` (`platformUserDir`, `platformCachePath`, `platformExePath`). |
| Shader directory & math | `engine/include/render/RenderDefs.hpp`; `engine/shader/core/` and `camera.slang`. |
| Descriptor slots | `core/rhi/DescriptorSlots.hpp` and `engine/include/render/HeapBind.hpp`. |
| Node names | `uiName` / `uiFindName` in Layer 1 `Node.cpp` (build-time only; banned inside `engineTick`). |
| UI callback registries | `UiHookList` (fixed cap `kUiHookCap`, no heap) in `core/ui/State.hpp`. Per-node hidden/overlay/clip results live in `UiState::vis`, filled top-down by `uiCacheVisibility`; `hitTest` and `uiClipBox` read them in O(1). |
| Worker pool & device-owned state | `deviceJobs(Device&)` (job pool) and `Device::compress` (GPU block compressor) are torn down by `deviceDestroy`; no process-wide statics. |
| Asserts & profiling | `core/platform/Check.hpp` (`BH_ASSERT`), `Trace.hpp`, `core/debug/CpuScope.hpp`. |

**C++26 & Header Hygiene:**
- Use designated initializers on aggregates. Use `std::string_view` and `std::span<const T>` for non-owning parameters. Use concepts instead of `std::enable_if_t`.
- Include only used leaf headers (`Geometry.hpp`, `Style.hpp`, etc.) rather than mega-headers.
- Guard POSIX headers with `#if !defined(_WIN32)` inside `.cpp` files only; never expose them in headers.

---

## 4. Flecs ECS & UI Architecture Invariants

### 4.1 Flecs Rules
- **Unbounded worlds:** Never cap scene actors with fixed arrays (`flecs::entity ent[16]`). Use `std::vector<flecs::entity>` and dynamic queries.
- **Hot-loop access:** Use `flecs::column<T>` in `Tick`, `Layout`, `Emit`, `Draw`, and cull.
- **Empty tag checks:** Always test tag components (`UiSplitter`, `UiMenuBar`, `UiResizable`, `UiTitleBar`, `UiTextInput`, `UiRowHover`, `UiFocusable`) with `entity.has<Tag>()`. Never call `try_get<Tag>()` on an empty tag (it returns `nullptr`).
- **Play-mode snapshots:** Copy all Flecs components on an actor dynamically through `ecs_get_type` and `ecs_get_type_info`.
- **Zero magic tags:** Negative paint tags (`-5`, `-6`, `-15`, `-100`) and fake fixed arrays (`wTree[12]`, `seg[32]`) are banned.

### 4.2 UI Layout, Batching & Widget Rules
- **Node & command limits:** `spawn()` stops at `kUiCap` (`2048`) and returns `kUiNone`. Command capacity `kUiCmdCap` is `128`.
- **Single-entity controls:** `Button`, `IconBtn`, `MenuButton`, `Check`, `Field`, and `TreeRow` are single entities owning their `UiPaint` (including `paint.icon`) and `UiText`. Never spawn phantom child panels just to hold an icon or caption.
- **Yoga invariants:**
  - Retrieve child IDs from `YGNodeGetContext(node)` in $O(1)$ (never scan `0..s.count`).
  - Walk subtrees via `YGNodeGetChildCount` / `YGNodeGetChild` in $O(1)$ per child.
  - Skip setting the Yoga text `measureFunc` if a node has children (Yoga forbids measured nodes from owning children).
  - Never place a 100% width `uiSlider` horizontally beside a label; stack vertically.
  - Scroll views preserve the screen `top`/`left` computed by their parent layout.
- **Tokens & Colors:**
  - All widget styling uses `paintToken(s.look, UiRole::..., UiToken::..., radius)` and `s.look.accent[i]`. Raw RGB literals are banned in Layers 2 and 3.
  - Custom colors (e.g., color swatches) must set `paint->token = static_cast<uint8_t>(UiToken::Custom)`.
- **Text & Field Rendering:**
  - Never emit glyphs when the scroll clip intersection is empty.
  - `UiTextInput` nodes skip generic `uiEmitTextNode` (the field decorator owns text and caret emission).
  - Number-field progress fills emit in the `under` hook (after panel background, before glyphs and caret). Number fields use left alignment (`align = 0`) and match the parent row height.
- **Property Grid & Splitter:**
  - Property rows use `UiRowHover` (never `UiFocusable`) so column and pane splitters remain draggable.
  - Splitter grips ignore pointer presses that hit a different focusable control.
  - Property section headers span full width above their rows; `uiShowOnly` toggles the direct child scroll view of the inspector; changing actors calls `scrollReset`.
  - `UiPropBinder` caches node IDs at creation (zero `uiFindName` in tick) and never overwrites a field while it holds focus or pointer capture.
- **File Browser & Drag-Drop:**
  - File icons bind at descriptor heap slots `662` and `663` (passing local photo index `slot - kUiDescPhoto` in `0..7`).
  - Breadcrumbs treat segment `0` as the root when `root` is set.History navigation verifies `underRoot` before changing `cwd`.
  - File drops test the pointer against the host bounding box.

---

## 5. Threading & Frame Pacing (`core/host/Host.cpp`)

1. **Phase 1 (Main Thread):** Poll SDL3 events, update DPI/resize, reset `FrameArena`, snapshot `InputFrame`.
2. **Phase 2 (`jobsRunAndWait` Workers):** Run Yoga layout, Flecs systems, SSBO vertex batching, and Vulkan command recording. Workers **never** call SDL, mutate OS window state, read the clipboard, or fire `onDrop`/`onOps`.
3. **Phase 3 (Main Thread):** Fire deferred callbacks, handle clipboard (lazy read on `Ctrl+V` only), call `vkQueueSubmit2`, await timeline semaphores, call `vkQueuePresentKHR`.

- `jobsRunAndWait` holds its lock and waits on `idle` until `current == nullptr` and `engaged == 0`.
- Frame rate measurement uses 64-bit counter deltas over clock frequency between `vkQueuePresentKHR` calls (mean of last 32 presents).
- Relative mouse-look pins the cursor at the grab point for the entire gesture; number scrubbing accumulates `pointer.dx` once the deadzone latches.

---

## 6. Vulkan 1.4, Rendering & Shader Invariants

### 6.1 Core RHI Contracts
- **One shader per pass family:** Never compile duplicate SPIR-V binaries per flight, mip, cube face, cascade, or light. Select indices dynamically via a 16-byte Push Constant block (`vkCmdPushConstants` with `VK_NULL_HANDLE` layout).
- **Accumulation barriers:** On intermediate lighting passes (`hdrA` across Sun → Lamps → Reflect → Fog → Clouds → GI), set `discard = false` (`oldLayout = tracked.layout`). `discard = true` is allowed only on the initial transition of an uninitialized target.
- **Non-destructive resize:** Window resize recreates transient screen targets only (`Vis`, `Depth`, `HdrA`, `HdrB`, `Ao`, `Contact`, `Reflect`, `HiZMax`, `Bloom`) and rebinds the heap in $O(1)$. Never destroy static geometry, BLAS, or material textures on resize. When resizing FSR targets, wait on the other flight fence (`vkWaitForFences`, 500 ms) first; dispatch scene passes at `sceneExtent` and store display size in `padExt`.
- **Submission handshake:** Scene rendering ends with `uiSem` signal. Canvas waits on `uiSem`, renders UI with `LOAD_OP_LOAD` from slot **48** (`kUiDescPrims`), transitions the swapchain to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, and signals present.

### 6.2 Fixed Slots & Aligned Struct Sizes
- Slot **18**: `HeapBuf::Rc` (device-local GI arena, two 256-byte-aligned flight slices).
- Slot **48**: `kUiDescPrims` (UI primitive SSBO ring).
- Slot **49**: `HeapBuf::Sun` (`FrameSun`, **1920** bytes).
- Slots **50–57**: Slider uniform blocks (**64** bytes each).
- Slot **661**: SDF font atlas. Slots **662–669**: UI photos. Slot **936**: $1024^2$ cloud shadow transmittance map.
- Binding **0**: `FrameView` (**384** bytes). `GizmoGpu`: **320** bytes. `AtmosphereParams`: **192** bytes (`float pad[26]`). `InstanceData`: **80** bytes.

### 6.3 Textures, Materials & Geometry
- **Scene textures:** Compress color maps to `BC7_SRGB_BLOCK`, normal maps to 2-channel `RG8` (`stride = width * 2`) `BC5_UNORM_BLOCK` (`.bc5.dds`), and ORM/roughness/metallic/AO/height (including packed `Specular` files) to `BC7_UNORM_BLOCK` with full mip chains.
- **Editor live textures:** Upload full mip chains (`R8G8B8A8_SRGB` for color, `R8G8B8A8_UNORM` for data) into `SceneFrameImpl::gpuHeap` via `heapImage` followed by a host-to-descriptor barrier on the flight's resource buffer.
- **Normal reconstruction:** Rebuild $Z = \sqrt{\max(0.0, 1.0 - x^2 - y^2)}$ from unscaled $XY$ first, then multiply $XY$ by `normalScale`.
- **Material shading:** Multiply authored scalar factors by texture samples. Apply alpha cutout only when material flag bit `32` is set; apply `baseColor.a` opacity only when flag `4096` is set.
- **Meshlets & Visbuffer:** Build meshlets first (`partitionClusters` of 8), then run `meshopt_simplify` with `SimplifyLockBorder`. Reject only zero-area triangles (`area2 <= 1e-24`). Flip triangle winding if the face normal opposes vertex normals. Because projection Y is negated, cull single-sided backfaces when `area >= 0`. Compute perspective-correct UV derivatives from clip positions (clamp `SampleLevel` to `0..7` if derivative $> 0.05$).

### 6.4 Lighting, GI, Reflections, Weather & Post
- **Color & Tonemapping:** Noon sun `(1.0, 0.92, 0.76)`, pale sky `(0.78, 0.82, 0.88) * sunIntensity * 0.055` (omitted when upward ray hits geometry within 80 m), +1 EV exposure, Rec.709 AgX inset.
- **XeGTAO:** `NoiseIndex = 0`, denoise 0..3 maps beta `{10000, 1.2, 0.55, 0.22}`. Final AO is `lerp(1.0, saturate(packed / 255.0), strength) * materialAO`.
- **Direct Shadows & ReSTIR:** Point lights use Filament windowed inverse-square falloff and 5-tap blocker search + $3 \times 3$ PCSS (1..8 texels). ReSTIR samples from the $16 \times 9 \times 24$ light cluster (`8-byte` reservoir) and traces shadow rays to a `0.15 m` solar disk. When ReSTIR sun rays are enabled, skip CSM shadow culling and rasterization.
- **Global Illumination (SHaRC / Split RC):** Two flight slices in `HeapBuf::Rc` (slot 18), 32-byte records (float RGB + count), normal dominant axis (2 bits) included in the cell key so walls and floors never mix, sample luma clamped to 20, temporal reprojection + half-resolution 8-tap spatial filter + bilateral depth upsample. Scale indirect radiance by `gSun.pad.w` when compositing into `HdrA`.
- **SSR & Reflections:** FFX hierarchical Hi-Z march on the reverse-Z MAX atlas (`64` steps, slider-driven thickness and roughness cutoff, GGX $\alpha = \text{roughness}^2$). Reproject reflection history using the hit point's previous view projection. Below roughness `0.25`, fallback misses trace one opaque TLAS ray shaded with sun visibility + GI cache.
- **Weather, Clouds & Fog:** Height fog uses Wenzel's closed-form integral with Beer-Lambert transmittance. Volumetric clouds sample weather-warped $128^3$ GPU Worley noise (`cloud_noise.comp`) and write top-down transmittance to slot 936. Rain updates 4096 streaks inside a $9\text{ m} \times 18\text{ m}$ world cylinder, accumulates Hermite surface wetness over 60 seconds, and fades roof edges with a 5-ray spread.
- **FSR & TAA:** Jitter $X = +jx, Y = -jy$ with projection offsets `m[8] = -2*jx/width` and `m[9] = +2*jy/height`. Blend TAA history only when `history.a >= 0.5`; resolve into `hdrB` and copy back to `hdrA`.

### 6.5 Scene, Gizmo & Physics
- **Dynamic props:** Moving a prop updates its 64-byte TLAS instance matrix in-place without rebuilding the static scene BLAS. Releasing a prop switches its Jolt body to dynamic against the static scene mesh.
- **Gizmo:** Constant screen size `distance * tan(fov / 2) * 108 px`; translation solves scale by meters-per-pixel; selection wireframe uses `ActorBounds * scale` centered on actor position.

---

## 7. Verification Protocol

Every code edit must compile cleanly with zero warnings and pass all tests before completion:

cmake --build build-core --target BurnhopeTest BurnhopeEngine -j && ./build-core/BurnhopeTest

---

## 8. Canonical Modules (Split Files)

| File | Responsibility |
| :--- | :--- |
| `core/platform/Window.cpp` / `WindowEvents.cpp` | Window lifecycle, surface, `windowVulkanExtensions()` / event pump, clipboard, hold timing. |
| `core/ui/Node.cpp` / `NodeDump.cpp` | Node tree and names / node dump. |
| `core/ui/widget/Field.cpp` / `FieldDraw.cpp` | Field behavior and undo (id-based snapshots) / field painting. |
| `core/ui/kit/Color.cpp` / `ColorPopup.cpp` / `ColorDraw.cpp` | Picker body and binding / popup, color book, eyedropper / swatch and ramp painting. Shared: `ColorInternal.hpp`. |
| `core/host/PropImport.cpp` / `PropImportParse.cpp` | Role spawn table and node building / JSON item and show parsing. Shared: `PropImportInternal.hpp`. |
| `core/host/PropFilter.cpp` | Property row filtering (`bh_ui_inspector`). |
| `engine/gpu_scene/SceneObject.cpp` / `SceneGizmo.cpp` | Actor storage and transforms / projection, picking, gizmo drag math. |
| `engine/src/SceneFrame.cpp` | Frame orchestration. |
| `engine/src/SceneActors.cpp` | Play-mode actor capture/restore and the actor API (select, name, count). |
| `engine/src/SceneGeometry.cpp` / `SceneCullUpload.cpp` | `.bhop` load, append, meshlet setup / prop links, audio baking, static upload, culling. |
| `engine/src/SceneProcedural.cpp` | Procedural primitive meshes (cube, plane, sphere, cylinder) and cube planting. |
| `engine/src/SceneTargets.cpp` | Transient screen targets and resize. |
| `engine/src/SceneTextures.cpp` | Material and texture upload. |
| `engine/src/SceneUniforms.cpp` / `SceneLights.cpp` | Camera, weather, block and grade uniforms / light upload (`uploadLights`). |

---

## 9. Additional Architectural Laws

- **Law 1 (No Guesswork Bugfixing):** Never claim a bug is fixed based on a mocked unit test. If keyboard input, clicking, or camera leaks fail, you MUST trace the real data pipeline from SDL3 events (`WindowEvents.cpp`) through `InputFrame` to the GUI entity.
- **Law 2 (No Multiline in Tree/List Items):** Nodes in `list_tree`, `list_hier`, and table rows MUST strictly enforce single-line text (`wrap = false`, `white-space: nowrap`). Under no circumstances may a word break across lines in a hierarchy row. If it overflows, truncate cleanly with trailing ellipsis (`...`) or clip.
- **Law 3 (Readable Contrast Standards):** Muted/secondary text (file types, subtitles, dates) on dark backgrounds MUST NOT use dark gray (`#71717a`). Use `#a1a1aa` or lighter (minimum 4.5:1 contrast ratio).
- **Law 4 (Promote Generic Interactions to Core):** Generic UI mechanisms (Drag & Drop ghost nodes, drag deadzones, drop-target highlights) belong strictly in `core/gui/`, NEVER ad-hoc in `demo/`.

## 10. Architectural Retrospective (Permanent Laws)

- **Law 5 (Never Re-invent Text Truncation in Application Code):** BANNED: `kGlyphPx = 7.2f`, manual `fitName()` string cutting, or any pixel-to-character division in controllers. Truncation (`...`) is the exclusive job of `GuiEmitter` / the GUI text engine, which know the real FreeType glyph advances. Controllers pass the full display name.
- **Law 6 (No Magic Control-Code String Packing for Hierarchy Rows):** BANNED: packing control bytes (icon, column, indent) into a `char text[]` of a tree or list item. Rows carry typed fields (`GuiRow`: `depth`, `icon`, `chevron`, `col1`, `col2`) next to the plain `text`.
- **Law 7 (Tooltips & Floating Windows Belong in Core GUI):** BANNED: tooltip delays, tooltip sizes and cursor-clamp logic in `demo/`. A node with `"tooltip": "Text"` shows a screen-clamped popup after the core delay (`core/gui/overlay/GuiTooltip.cpp`); lists only supply the text of an item through `guiHostBindTooltip`.
- **Law 8 (Decouple C++ from DOM Paths):** BANNED: rigid DOM path strings in C++ (`main::matPanel::cols::left::albedo::row::ctl::scale`). Nodes that C++ drives carry `"bind": "actor.pos.x"` in JSON; C++ addresses them by that key so the layout hierarchy can change freely.
- **Law 9 (Bundle Multi-File Asset Companions):** a `.gltf` and its same-stem `.bin` are ONE asset in the file explorer: the `.bin` is hidden, and duplicate / trash / move carry it along. Never show orphan buffers.
- **Law 10 (Controllers under 300 LOC):** a controller file that grows past 300 lines is split along responsibility with a shared internal header.
- **Law 11 (Overflowing Tile Text Is Never Centred):** NEVER center overflowing text in constrained tiles; ALWAYS fall back to left-align with FreeType glyph-measured ellipsis (...) when text width exceeds container bounds. Tile captions are scissored to their own cell; padding, gutter and caption heights come from the `tile.*` tokens of `assets/ui/styles.json`.
- **Law 12 (Text Input Is Never Dropped):** NEVER clear or mask InputFrame::text before the active GUI text-edit trait consumes it (causes dropped keystrokes); ALWAYS maintain persistent SDL_StartTextInput while a field holds focus. A frame the canvas skips must not report `wantsText = false` (root cause of the dropped-keystroke bug: `canvasConsume` zeroed the feedback on skipped frames and the host called `SDL_StopTextInput`).
- **Law 13 (No Raw Colour Plates in C++):** NEVER place raw RGB plate constants in C++ headers; ALWAYS resolve through design tokens and styles.json (`guiHostColor8` for palette roles, `guiHostToken` for sizes).
