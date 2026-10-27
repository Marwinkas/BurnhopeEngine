# BurnhopeEngine architectural laws

Desktop x86_64 only (Linux and Windows, AVX2). Vulkan 1.4, data-oriented layout, Flecs, C++26. No mobile, no WebAssembly target, no legacy OS. `core/wasm/WasmHost` is an in-process bytecode interpreter.

Layer 3 reads like one line of Qt/Slate. The machine path stays AAA.

A module owns its memory, its lifetime, and a typed interface. Siblings do not mutate each other, share ad-hoc mutable memory, or trigger hidden work in another subsystem. A failure stays inside that subsystem: `BH_ASSERT` in debug, a typed `false` or error to the caller.

Ship complete, compilable code. No glue, one-off workarounds, stubs, placeholders, `// TODO`, or elided bodies. No warning pragmas that hide a violation. No `const_cast` that breaks a const contract. No `reinterpret_cast` across non-trivial types. No `void*` adapter for a type that sits in the wrong level: move the type down to the level that may own it.

This document is the single law. When a bug is fixed, append one line: `NEVER [action] (causes [failure]); ALWAYS [alternative] [slots, bytes, alignment].` Do not restate a ban that already exists.

---

## 1. Seven levels (`N → <N`)

Every file has exactly one level. It may include, forward-declare, name, or call only a strictly lower level. No cycles. No upward leak. An include banned in the `.hpp` and added in the `.cpp` is the same violation. Modules on the same level do not call each other. Shared setup, layout, or parsing moves down.

```
LEVEL 6  Application and tools
         demo/app/  core/files/  core/host/
         Thin declarative assembly. Controllers under 300 lines (tools under 350, prefer under 300).
         Sees 5..0. No RHI, Yoga, or vertex internals.
LEVEL 5  Frame pipelines
         engine/ (gpu passes, SceneFrame) and the UI GPU bridge in core/rhi/ui/
         (Canvas, Font, UiRenderer, UiPhoto)
         Emit Vulkan 1.4 commands. Depend on RHI and assets. No widget layout math.
LEVEL 4  Interaction traits
         core/gui interaction, virtual pool, overlay
         Drag, click, scroll, focus, text edit, virtual recycling, popups.
         Each mechanic exists once. No Vulkan, no OS window, no vertex writes.
LEVEL 3  UI geometry
         core/gui layout, style, text, hit-test
         Yoga, style classes, scissors, text measure. Boxes and abstract primitives.
         No concrete widget types, no domain, no GPU draw.
LEVEL 2  World and assets
         engine/gpu_scene/  core/asset/  core/gfx/  core/stream/  core/scene/
         Geometry, materials, lights, import, physics, audio. No UI.
LEVEL 1  Platform and RHI, side by side
         core/platform/  knows SDL3, does not know RHI
         core/rhi/       knows Vulkan, does not know Window
         They meet only at a raw VkSurfaceKHR.
LEVEL 0  Foundation
         Str, File, Json, Time, FrameArena, Color, Check, math, SIMD
         No Vulkan, no SDL3, no Yoga, no Flecs, no UI.
```

Scene Flecs lives in `engine/gpu_scene`. `core/rhi` and `core/platform` contain no ECS. A render pass never includes a UI model header. Do not invent a parallel `src/` tree.

Level 1 split:

- `deviceCreate` takes a raw `VkSurfaceKHR`.
- Timing lives in `core/platform/PlatformUtil.hpp`.
- `InputFrame` lives in `core/platform/Input.hpp`.
- `kFramesInFlight` is a standalone constant (`DescriptorSlots.hpp` or `DeviceCaps.hpp`). The descriptor heap does not include the swapchain.
- `Action.hpp` includes `Input.hpp` for scancodes. It does not include `Window.hpp` or Vulkan.
- One input snapshot. No second mouse, key, gamepad, or `InputQueue`.

Level 3 and 4:

- Infrastructure registers draw, overlay, input, spawn, and role-finish hooks. It does not name Tree, Button, or File APIs.
- `PrimitiveBatch` / the GUI emitter does not include `<yoga/Yoga.h>`. Visibility is `nodeShown` plus the culled bitmask.
- Traits take abstract models, callbacks, and Flecs components. No `"list_inner"`, `"status"`, `"BistroScene"`, or demo-only slots inside input, tree, or list code.

Level 6 facades. Application code never writes `UiState` fields (`menu`, `fieldMenu`, `topOn`, `ghost`, `opOpacity`, `contextOf`, `parentOf`, `altLook`, `layoutDirty`, `visualDirty`, `opMinimize`, `opMaximize`). Call `uiSetField`, `uiSetMenu`, `uiSetFieldMenu`, `uiRequestTop`, `uiRequestOpacity`, `uiSetContextMenu`, `uiParent`, `uiApplyLook`, `uiNotifyLayoutDirty`, `uiNotifyVisualDirty`, `uiRequestMinimize`, `uiRequestMaximize`. Host chrome goes through `windowMinimize(host, slot)` / `windowMaximize(host, slot)`.

Canonical level-6 shape:

```cpp
auto [treePane, gridPane] = uiSplitter(s, rootNode, { .width = 240.0f });
uiFolderTree(s, treePane, {
    .model = &fileModel,
    .onSelect = [&](const auto& path) { fileModel.navigateTo(path); }
});
uiVirtualGrid(s, gridPane, {
    .items = fileModel.currentEntries(),
    .onActivate = [&](size_t idx) { fileModel.execute(idx); }
});
```

There is one UI loader. Screen JSON and widget blueprints share it. A second importer (`PropImport` beside `Template`) is a duplicate engine.

---

## 2. Memory, DOD, C++26

`Tick`, `Layout`, `Emit`, `Draw`, `Consume`, and `HitTest` do not call `malloc`, `free`, `new`, `delete`, `realloc`, `std::string`, `std::vector::push_back`, `std::map`, `std::unordered_map`, or `std::make_shared`. Per-frame memory comes from `core/memory/FrameArena.hpp` (reset in O(1)), a ring, a fixed stack array, or `UiVirtualPool`.

Hot data is trivially copyable POD, `sizeof % 16 == 0` or `sizeof % 64 == 0`, with `alignas(16)` / `alignas(64)` and `static_assert` on the GPU size. Prefer SoA and flat arrays. Hot fields sit together. Cold strings sit apart. Trees and ASTs use `uint16_t`, `uint32_t`, or `flecs::entity`.

GPU-facing sizes:

- `GizmoGpu` 320 bytes.
- `FrameView` 384 bytes, binding 0.
- `FrameSun` 1920 bytes, binding 49, `HeapBuf::Sun`.
- Slider blocks, bindings 50–57, 64 bytes each.
- `AtmosphereParams` is `float pad[26]`, 192 bytes.
- `InstanceData` 80 bytes.
- UI primitive record 64 bytes.
- SHaRC voxel atom is 16 bytes at offsets 0, 4, 8, 12. The resolved record is 32 bytes with 12 bytes of float RGB.
- Radiance-cascade probe header slice is 256-byte aligned.

Designated initializers on every aggregate. Non-owning arguments are `std::string_view` or `std::span<const T>`. Constraints are concepts. A file includes only the headers it uses. `Detail.hpp` is not a mega-header. POSIX headers (`<sys/mman.h>`, `<unistd.h>`, `<fcntl.h>`, `<spawn.h>`, `<dlfcn.h>`, `<dirent.h>`, `<sys/stat.h>`) never appear in a platform-agnostic header. A `.cpp` that must use them guards with `#if !defined(_WIN32)`.

No function-local `static` mutable state. State lives on `Host`, `UiState`, `GuiContext`, or the window. `platformPace(hz, uint64_t& nextTick)` stores the deadline on the window (`host.paceNext`).

No `std::this_thread::sleep_for`, spin-sleep, or lock-free polling used as a sync protocol. Sync is a timeline semaphore, a wait-free ring, or a condition variable with a real predicate.

`jobsRunAndWait` holds its mutex until workers finish. Wait on `idle` until `current == nullptr` and `engaged == 0`. Do not publish the next batch over a live one.

Scene CPU buffers (`meshletGpu`, `visibleCpu`, `ownedVert`, `textures`) are `std::vector`. HUD hit records live on `Engine::hud` as a `std::deque`.

Frame time is a `uint64` counter delta over the clock frequency, the mean of the last 32 `vkQueuePresentKHR` calls. A host tick that timed out a flight fence is not a displayed frame.

---

## 3. Single source of truth

| Concern | Authority |
| :--- | :--- |
| JSON | `core/io/JsonCursor.hpp`, `core/io/Json.hpp` (zero-copy, `\uXXXX`, scientific notation) |
| Strings and numbers | `core/Str.hpp`, `std::from_chars`, `std::to_chars`, bounded stack `snprintf` |
| Input snapshot | `core/platform/Input.hpp` |
| Virtualization | One pool: total count, row stride, about 30 live slots (`UiVirtualPool` / `kUiPoolCap`) |
| Text measure, wrap, caret | One text engine, real FreeType advances |
| Ellipsis | The emitter, from glyph advances |
| Files | `fileMapReadOnly` (`mmap` / `MapViewOfFile`). A failed map is the missing-file path |
| Images | `core/image/`: decode the mapped span (SDR, HDR, GIF, DDS), compress BC1/BC3/BC4/BC5/BC7/BC6H, upload by Host Image Copy or staging |
| Time, cache, exe, user dir, host meters | `core/platform/PlatformUtil.hpp` |
| Shader directory | `engine/include/render/RenderDefs.hpp` |
| Shader math | `engine/shader/core/` and `engine/shader/camera.slang` |
| Descriptor slots | `core/rhi/DescriptorSlots.hpp` |
| Field setup | `uiFieldInit` |
| Node names | `uiName` / `uiFindName` while building. Never inside `engineTick` |
| Hotkeys | `core/platform/Action.hpp` |
| Asserts | `core/platform/Check.hpp` (`BH_ASSERT`, cpptrace) |
| Tracy | `core/platform/Trace.hpp` |
| CPU spikes | `core/debug/CpuScope.hpp` |
| Scissors | One clip stack |
| Drag | One drag engine (sliders, splitters, scrollbar thumbs, color pickers, window resize, numeric scrub) |
| Scroll | One scroll engine |
| Click, hover, focus | One of each |
| Text editing | One engine (caret, selection, undo, clipboard) |
| Popups, tooltips, light-dismiss | One overlay |
| UI JSON load | One loader, with `$include` |
| Model thumbnails | `core/asset` (`assetFetchModelThumb`) |
| GPU block offsets and uniform fill | `engine/include/render`, `engine/src` |

---

## 4. Flecs

Components are POD. No logic in constructors or destructors. Behavior is free functions and queries. Child lifetime is the `ChildOf` relationship.

Hot loops read `flecs::column<T>`. No `entity.get<T>()` or `try_get<T>()` in `Tick`, `Layout`, `Emit`, `Draw`, or cull. Empty tags (`UiSplitter`, `UiMenuBar`, `UiResizable`) are tested with `entity.has<Tag>()`.

A single-entity hit or write reads the component on `world.ent[index]`. Worlds are unbounded: `std::vector<flecs::entity>` and `sceneActorAdd`. Play mode copies every component on the actor through `ecs_get_type`, sized by `ecs_get_type_info`.

Identity is a component: `UiSplitter`, `UiTitleBar`, `UiMenuBar`, `UiResizable`, `UiImage`. Clicks go through `uiBind` of a `UiAction`. Multi-stop gradients set `paint.ramp = kRampColorWheel`. Banned identities: paint tags and magic integers `-5`, `-6`, `-7`, `-15`, `-90`, `-70..-78`, `-100`, and any `tag == N` / `type == 999` sentinel.

Button, check, and field are one entity that owns its `UiText` and `UiPaint`. `Hit` tests `hitBox` only. Scroll thumbs and splitter grips run in the interaction hook. A widget never writes `UiHostNote`. Publish `ColorBook.present` through `uiSetColorBook`.

The interactive surface is components, not a widget class and not a 200-line flex function:

- `UiFlex` (Yoga handle), `UiBox` (screen rect), `UiPaint` (token, border, radius, photo), `UiText`, `UiStyle` (class id).
- Tags and traits: hover, hovered, clickable, draggable, scrollable, focusable, focused, editable, virtual, clip-children, drop target.

A slider, a splitter, and a scrollbar thumb are the same drag trait plus a style class. A tree, a grid, and a dropdown share the same virtual pool. A clear button is a child with a click trait, not a special case inside the field. Variants are style classes (`btn_default`, `tool_button`, `slot_button`), not extra C++ entry points.

Visible rows come from the virtual pool. Ten items and ten million items share the same recycler. On recycle, if the slot held keyboard focus, clear that focus. Banned ceilings: `wTree[12]`, `wCell[18]`, `crumbs.seg[8]`, `pop.row[12]`, `UiNode treeSlots[12]`, `char pathSegments[8][64]`, `kCrumbCap`, `kSceneActorCap`, `flecs::entity ent[16]`. Breadcrumbs are a `std::vector` of paths.

Spawn stops at 2048 nodes and returns `kUiNone`. Command slots are 128.

List and tree rows are structured fields (`depth`, `icon`, `chevron`, `text`), one line, wrap off, trailing ellipsis. Depth step is 12 px. The tree column is at least about 220 px wide. Chevron click toggles the branch. Label click navigates and keeps the branch open. Navigating in the grid expands the parent chain.

---

## 5. Declarative UI

Three inputs, kept apart:

1. Skeleton JSON (`assets/ui/core/*.json` and screen files): structure, flex, text role, Flecs traits. No pixel sizes in the skeleton.
2. `assets/ui/styles.json`: every height, width, padding, gap, radius, border, icon inset, and color token.
3. C++: data binding, clicks, caret, drag response. No coordinate math, no float RGB, no padding writes.

Screens are small JSON modules joined by `$include` (shell, top bar, inspector, files, menus, material, launcher, editor). Each module stays under about 250 lines. C++ fallback metrics exist only when `styles.json` is missing from disk. A number changed in JSON changes the layout with no recompile.

A widget owns its content: label, padding, alignment of that content. The parent owns `grow`, `shrink`, percent width, percent height, and `alignSelf`. Fill flags do not live on the button config. Rare programmatic stretch calls a parent setter (`uiSetGrow`) on the node.

A horizontal row does not hold a 100% width slider beside a label. Stack them. Role `Scroll` requires an inner. `cacheBox` is O(1) from the parent bounds. `uiLayout` is O(N) over Yoga flex-drop children. Scroll content size includes grandchildren. Every container clips through the one scissor stack. A node inherits its parent clip. Only an ancestor with clip-children restricts descendants. A node’s own box does not clamp its glow, border, or shadow.

Coordinates scale by `canvas.dpi`. No assumed 1920×1080, 60 Hz, or DPI 1.0.

Chrome, borders, fills, and accents use tokens:

```cpp
paintToken(s.look, UiRole::Button, UiToken::Button, s.look.radiusSm);
paint->token = static_cast<uint8_t>(isSelected ? UiToken::Accent : UiToken::Item);
```

Levels 4 and 6 do not write raw RGB. If a token is non-zero, the batcher uses the palette and ignores `r, g, b`. A true custom color sets `UiToken::Custom` and then the floats. Theme changes go through `uiApplyLook` / `uiRestyle` / `guiHostSetTheme`. Palette variation reads `s.look.accent[i]`.

Signature theme: background `#0e0e11`, charcoal `#18181b`, card `#222227`, card border `#2e2e36`, accent `#f97316`. Secondary text on that background is `#a1a1aa` or lighter (at least 4.5:1). Selection, active tree rows, and slider fills use the accent.

A menu button that already owns a caption child is updated on that child. Skip measure when the Yoga node has children. The generic text pass does not emit glyphs for a `UiTextInput`; the field decor owns that run. A number fill is drawn from the under hook, after the panel and before glyphs. Number glyphs are left-aligned (`align 0`, `fieldSyncText`) once the caret is on the left. The number field’s height is the row height.

Property layout: a full-width section header above its rows, one shared label width. Rows are hover-only, not focusable, so the column grip and the pane splitter still receive the press. The grip is parented to the scroll view at `posX = label width + indent`, with a pixel height of the viewport starting at the filter bottom, and its screen box is stamped after layout. A property menu’s width includes the arrow and the pads. Changing actor calls `scrollReset` on that view. The inspector shows and hides the scroll view that is the direct child of the inspector. A material page keeps section bit 64 until an actor click clears it. Pivot reads the `Pivot` component. Fold open-height is sampled while the section is idle and open. The weather gizmo is cleared when the actor is not Weather.

Color UI: the popup is parented to node 0 after the root exists. Title text is the dark paper color; the close icon is icon 3. Header buttons are hoverable and focusable, close on the right. The hex field is a text input and focusable. Stats readouts parent to node 0 at the toolbar bottom plus 8. Toolbar popup dismiss goes through the menu slide so dismiss reveals cleanly.

A splitter grip ignores a press that already hit another focusable control. Breadcrumb segment 0 is the root when a root is set. An inline editor is placed on the live virtual-pool slot. Post sliders record the node id in `UiPropBinder` when the row is created and bind the float the pass reads. A bound field is not written while that node is the capture or the focus. A numeric scrub adds `pointer.dx` after the deadzone latch. A hidden look cursor stays pinned at the grab point for the whole relative look. File icons store heap slots 662 and 663 and pass `slot - kUiDescPhoto` (one byte, range 0..7).

Preserve behavior while splitting files: context menus, inline F2 rename, rubberband selection, Ctrl/Shift range and toggle, hierarchy chevrons, forward/back history, docking, form validation, SVG toolbar icons, material preview sphere, texture thumbnails, project launcher, debug-view menu.

Public GUI surface:

- `guiHostBind` by a semantic key (`actor.pos.x`, `mat.roughness`, `file.new`).
- `guiHostSetTheme`, `guiHostSetClass`, `guiHostSpawnSnippet`, `guiHostDestroyNode`.
- Application code does not set pixel rects. Yoga and the overlay own coordinates.

`core/gui/include/gui/` is the public facade (`GuiHost.hpp` and the POD contracts: icons, row, drag payload, loader, style sheet). `core/gui/internal/` is private to `bh_gui`. Public headers do not include `internal/`. They forward-declare. `GuiHost.cpp` is a `bh_gui` translation unit. `demo/` and `core/host/` do not see internal headers. No vtable facade. CMake:

```cmake
target_include_directories(bh_gui
    PUBLIC  core/gui/include
    PRIVATE core/gui/internal)
```

Level 6 does not include `Engine.hpp` from a controller, and controller functions do not take `Engine&`. `engineTick` passes `BrowserState&`, `GuiHost&`, `SceneFrame&`, and the other concrete objects. Controllers do not store an `Engine*`. Node identity in C++ is the semantic name (`browser.tree`, `browser.grid`), never a path built with `::`. Clicks bind with a local callback or a per-controller action, not one global numeric command enum.

`Engine` is the compositor: window, scene, canvas, cached ids, host meters, scene load, file drop. It does not become the include root of every panel.

Build-time hygiene (`bh_hygiene`) fails configuration if `demo/` contains private GUI includes, `kGlyphPx` or `fitName`, tooltip or drag-ghost layout (`kTipDelay`, `pinNear`, `guiLayoutSetRect`), packed row control bytes, a `::` DOM path, or a `demo/app` `.cpp` over 300 lines.

Input path, in order, all of it real: OS text event, `WindowEvents` fills `InputFrame::text`, the canvas feeds the GUI, the text trait inserts into the focused editable. An editable is focusable. A focused field has `SDL_StartTextInput` on. Scene masking must not zero `text` before the GUI reads it. Pointer-over-GUI blocks orbit, pan, and zoom for every button and the wheel, including the file dock and the inspector. A bug is fixed only after that pipeline is traced. A unit test of an isolated insert does not prove the window path.

Clipboard text is read on explicit paste (Ctrl+V).

One UI draw per frame from the mapped host-visible SSBO ring. The UI GPU bridge binds that buffer at slot 48 (`kUiDescPrims`, canvas range 48..63). Scene passes never write slot 48. SDF font atlas is slot 661. UI photos are slots 662–669.

---

## 6. Frame phases (`core/host/Host.cpp`)

```
Phase 1 main              Phase 2 jobsRunAndWait        Phase 3 main
SDL poll, DPI, resize,    Yoga, Flecs, SSBO batch,      deferred callbacks,
arena reset,              command buffers               clipboard, vkQueueSubmit2,
input snapshot                                          timeline wait, vkQueuePresentKHR
```

Phase 2 workers never call SDL, change window state, read the clipboard, or run `onDrop` / `onOps`. Those callbacks are queued for Phase 3.

---

## 7. Vulkan 1.4

Required: `vkCmdBeginRendering` / `vkCmdEndRendering`, `VK_EXT_shader_object` via `vkCmdBindShadersEXT`, `VK_EXT_mesh_shader` via `vkCmdDrawMeshTasksEXT`, `VK_EXT_descriptor_heap` plus buffer device address, `vkCmdPipelineBarrier2`, timeline semaphores, Host Image Copy. Lighting is radiance cascades, a clustered light grid, and two-phase GPU Hi-Z.

Banned: `VkRenderPass`, `VkFramebuffer`, runtime `VkPipeline`, `vkCreateGraphicsPipelines`, `VkDescriptorPool`, `VkDescriptorSet`, `vkCmdPipelineBarrier` (v1), binary fences per pass, `vkDeviceWaitIdle` and CPU fence waits inside the frame loop and on resize, `VK_KHR_mesh_shader`, `vkCmdUpdateBuffer` for per-dispatch constants, a `VkPipelineLayout` stored on a shader-object pass. After bind, `vkCmdPushConstants` uses `VK_NULL_HANDLE`. The push block is 16 bytes.

The scene ends color on `LOAD_CLEAR` and signals only `uiSem`. The canvas waits `uiSem`, begins with `LOAD_OP_LOAD`, transitions the swapchain image to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, and signals present. The scene never calls `swapchainToPresent`.

`shaderCreate` failure destroys the pass and returns `false`. A missing optional device feature (no ray query) may stay off and return `true`.

`HeapBuf::Rc` is slot 18. One shader per pass family. A 16-byte push selects the slot: `flight` or `flight + mip`, `face + stride`, `cascade` into `HeapBuf::ShadowAll` at binding 18, `light` into `PointVisible` at binding 19. Index as `base_slot + push.index`. Banned shader names: `CULL_FLIGHT`, `HIZ_MIP`, `CUBE_FACE`, `visbuffer.shadow0/1/2`, `visbuffer.point0/1/2`, `cube0..5`, `bloom0..3`. Radiance cascade index is dword 0 of that push. Two 256-byte-aligned Rc slices, flight in bit 0, cleared before allocation. Atomics run in the device-local slot 18 arena. One staging copy carries the 256-byte header and positions.

Accumulation (`hdrA` through Sun, Lamps, Reflect, Fog, Clouds, Radiance): `discard = true` only on the first transition of an uninitialized image (`oldLayout = UNDEFINED`). Every later barrier uses `discard = false` and `oldLayout = tracked.layout`. After the vis pass, vis and depth leave attachment layout and `release()` to `SHADER_READ_ONLY`, including the late draw.

Resize does not call `sceneFrameReleaseGpu` and does not destroy static vertex buffers, instance buffers, BLAS, or material textures. Recreate transient targets only (`Vis`, `Depth`, `HdrA`, `HdrB`, `Ao`, `Contact`, `Reflect`, `HiZMax`, `Bloom`), then `bindHeap` in O(1). FSR target recreation waits on the other flight’s fence, 500 ms, then recreates, and logs quality and both extents. A scene pass dispatches at the scene extent (`gFrame.extent`). Display size lives in `padExt`, 8 bytes.

Blend: additive is `SRC = ONE`, `DST = ONE`. Multiply is `SRC = DST_COLOR`, `DST = ZERO`.

`meshletPackNormal` is octahedral and keeps Z. Hi-Z occlusion samples a conservative 2×2. The cluster grid may be 16×9×24 cells. World positions are transformed by the scene AABB. Volumetric noise is one GPU dispatch of `cloud_noise.comp` into `HeapBuf::CloudNoise`.

FSR jitter: `jitter X = +jx`, `jitter Y = -jy`, with `m[8] = -2·jx/width` and `m[9] = +2·jy/height`.

HUD and tick code use cached `uint16_t` ids (`HudIds`: `mode[15]`, `stat[12]`, and the slider ids). No `snprintf` and no `uiFindName` inside `engineTick`. Sun cast-off is bit 31 of the decal-count dword. The cluster reads the low 31 bits as the decal count. SHaRC indirect scale is `gSun.pad.w`.

A moving prop writes its 64-byte world matrix and leaves the acceleration structure. The city BLAS is not rebuilt because one instance moved.

---

## 8. Visibility, materials, shade

Meshlets first, then `partitionClusters` of 8, then `meshopt_simplify` with `SimplifyLockBorder` on that cluster. Reject a triangle only when `area2` is at or below `1e-24`. Keep a sub-pixel triangle when a pixel center lies inside its screen bounds. UV gradients are the perspective-correct derivatives of the three clip positions.

Projection Y is negated, so a positive framebuffer area is the back face. Single-sided materials drop `area >= 0`. When the face normal opposes the authored vertex normals, swap the last two indices. `disableBackface`, `disableScreen`, and `disableBehind` are the three spare `FrameView` dwords.

Alpha test runs only when material flags bit 32 is set. A material whose flag 32 is clear and whose alpha is at or below 0.02 is opaque to sun rays. Keep the current sun visibility when history is brighter by more than 0.35.

Albedo is the authored factor times the map. `baseColor.a` applies only when flag 4096 is set. Roughness is ORM green, metal is ORM blue, including a file named Specular. A metal map in `pad3` contributes its brightest channel. A missing ORM path clears the slot. Albedo none is `0xFFFFFFFF`. Clamp a negative barycentric weight to the triangle before the normal blend.

Editor color maps are a full mip chain of `R8G8B8A8_SRGB`. Data maps are a full chain of `R8G8B8A8_UNORM`. Scene color is BC7 sRGB, normals are BC5, ORM (rough, metal, AO) and height are BC7 UNORM, full mip chain, DDS cache. BC5 source is two bytes RG per pixel, stride `width * 2`, cached as `.bc5.dds`. Rebuild Z from unscaled XY, then multiply XY by `normalScale`. Photos are the texels on slots 8 and 9, loaded with `imageLoadFile` for jpg, png, tga, bmp, and hdr. Bind editor images with `heapImage` on `SceneFrameImpl::gpuHeap`, then a host-to-descriptor barrier on that flight’s resource buffer before the draw. A world-aligned map whose UV derivative is longer than 0.05 uses `SampleLevel` with lod clamped to 0..7. Sampler `maxLod` is 12.

Wall quads store the face normal, 12 bytes xyz. Sky ambient is subtracted only when the +Y ray hits within 80 m. While radiance cascades run, `HdrA` holds direct sun, the cascade shadow, and lamps. Sky enters on a miss of the last cascade, and as albedo times sky chroma times sun intensity 0.16 in `rc_shade` after the probe sum. That 0.16 term stays out of the trace input. With cascades off, outdoor ambient is albedo times sky chroma times sun intensity 0.16. Tonemap uses the Rec.709 AgX inset, noon sun `(1, 0.92, 0.76)`, a pale sky fill at 0.055 of the sun, and one EV before AgX. Color maps are `BC*_SRGB`.

XeGTAO: `NoiseIndex` 0. Denoise 0..3 maps beta to 10000, 1.2, 0.55, 0.22. Visibility is `lerp(1, visibility, strength)` times material AO, from `saturate(packed/255)`. No second power, no extra 1.5 divide.

Point lights use Filament windowed inverse-square: `smoothFactor^2 / max(dist^2, 1e-4)`. A shadow ray aims at a point on the 0.15 m source disk, last 4 bytes of the light, `TMax` to that point. The 512 map uses a 5-tap blocker search and a 3×3 PCF whose radius is `sourceRadius * (receiver - blocker) / (blocker * range)`, clamped to 1..8 texels. One candidate comes from the 16×9×24 cluster, stored in an 8-byte reservoir, with a checkerboard ray.

ReSTIR sun: skip the cascade cull and the cascade draw, and force direct to 0. Shade the receiver and multiply by scalar visibility, using one meshlet cut of the original mesh and the depth-plane normal. Trace every geometry pixel. Rebuild a missing checkerboard sample inside the visibility shader and write that result once. Visibility is a running average up to 32 frames, that average alone after 16 still frames, clamped to the same-surface neighborhood and filtered by a 3×3. Extend the cross to 2 px where the local visibility range is above 0.2, and to 3 px while history length is under 4. Widen the depth test to 6% when `N·V` is 0. Reject a neighbor below normal power 32. Mix neighbor reservoirs outdoors only. Indoor history cap is 64.

Fog is the Wenzel height integral `(atCam - atEnd) / (falloff * view.y)` times the ray length, Beer-Lambert, one sun inscatter.

An emissive instance is one point light with flag bit 2. The shadow ray continues through that material. SHaRC adds `baseColor * emissive * 8`.

TAA stores the resolve by storage-loading `hdrA`, writing `hdrB`, and copying `hdrB` back into `hdrA`. History is sampled in `SHADER_READ_ONLY`. When history alpha is below 0.5, the current color is the result.

Clock sun: azimuth is `hour * 15` degrees, elevation is `sin((hour - 6) * pi / 12) * 62`. The solar disk is a smooth limb at full resolution in the sky pass. `shade_sky` writes the LUT sample and composites clouds in debug modes 0 and 4. Cloud shape is a weather-warped sample blended with a second phase at the tile edge. Cloud shadow is a 1024² top-down transmittance at slot 936, 2 bytes per texel, sampled before the sun term.

---

## 9. Radiance cascades, VXGI, SHaRC

Split RC, final deposit:

- One identity TLAS instance, mask `0xFF`, device address at header byte 112.
- Probe origin is the surface closest to the world cell center, `surf1` normal, 12 bytes xyz. Rays start 1 cm along the depth normal, not the shading normal and not the cell center.
- A hit closer than 5 cm continues. Sun visibility on a world hit is a ray toward the sun, origin 2 cm along the outward normal, `TMin` 5 cm. Shadow-map sampling at a screen hit is biased only by the sun texel slope.
- Cell is `floor(world / spacing)` at lod 0 and `baseSpacing`. Spacing is `Δs·2^n` at the surface. The same hit radiance is written to that cell and the next coarser cell. `TMax` comes from push `baseLength`. Ray intervals are `t0·4^n` with `t0` in the push.
- Hash mixes x, y, and z and lod. c0 cap 32768, hash 131072. A full fine hash falls back to the world cell at 4× spacing, inserted before the fine one. A zero hit stays unmeasured. The next lit world cell shades it.
- While the bbox product is within 16384, every cell holds a probe. `J` is stored on cascade `n` for `t` in `(t_{n-1}, t_n]` with `t_n = 1.6·Δs·4^n`, and `β = 1` on each lower cascade. `I = J + β·(sum of the four children) / 4`, the mean of children whose valid flag is set. At most 32 steps per axis. A far hit is also written into the shooting probe’s cascade 0.
- `J = albedo · N · L · sunVis` in 0..1. Shade multiplies by the receiver albedo. A shaded screen hit divides by sun intensity. A miss stores `J = 0` and stays unmeasured (`valid = 0` when `count = 0`). `I = J + β·parent` only for a measured bin. Sky is only a miss of the last cascade, and it is not the sky LUT.
- Screen hits use a 5×5 view-depth match, then a depth march when the BLAS misses. When the hit matches screen depth, read `hdrDirect`. Off screen, sun times the shadow map. Lamps stay in debug modes 0 and 4. Debug mode 4 writes irradiance times white 0.8 and omits direct sun. The trace still reads the shaded hdr from before that replace.
- Probe origin pixels: `atomicMin` of the pixel index at meta+28, initialized to `0xFFFFFFFF`. Rays leave the surface stored at meta+32. Keep `baseColor` when the albedo sample is empty.
- Each flight is baked once from every triangle whose indices sit inside the vertex and index buffers, into its 256-byte-aligned slice. Do not rebuild from screen pixels every frame.

VXGI: three 128³ tori, two flights, scroll in the 64-byte cascade block, and only the slab whose camera voxel entered. Resolve the scrolled texel, 16 bytes rgba. Each voxel takes a barycentric UV, normal map, and ORM. Lod 0 is the 0.125 m cascade.

SHaRC, final:

- One stable hemisphere ray from the 8×8 tile center into that world cell. Reject a hit closer than 15 cm. Divide direct by sun intensity. Four stable strata on a 0.5 m cell, then the mean. Metal tint uses `f0`. Every tile of that cell deposits. The query itself is trilinear and traces no ray. Three probe steps evict a voxel stale for 16 frames. One subgroup atomic per voxel.
- World spacing is a power of two. The same energy goes to the 3×3×3 around the point, and to the coarser cell when the fine one is empty. The dominant axis of the normal, 2 bits, sign omitted, separates wall and floor. A vertical face stays in its own Y bin. The surface 3×3 is only in the plane of the normal. Sky (pale, 0.055 of the sun) is written to the center cell only. A hit is stored times 2.5 in that surface 3×3.
- A sample whose luma is 0 is skipped. Sample luma is clamped to 20 before the atomic add. The resolved cell blends toward that mean with 0.08 and returns the trilinear of the 8 corners. Accept a cache hit only beyond `voxelSize * sqrt(3)` and a footprint above the voxel: one BRDF throughput times `SharcGetCachedRadiance`, not multiplied by receiver albedo again. Sun visibility is the opaque sun ray and `sharcSunEnergy`. With the denoiser off, that same query color is composited per channel into `HdrA`.
- History stores irradiance, not albedo and not the GI slider. Divide by `max(albedo, 0.01)` before the filter and multiply that floor back per channel when writing `HdrA`. The slider is `gShade.pad1` at that write. Images are `R16G16B16A16_SFLOAT`. The denoiser receives the same `add.r`, `add.g`, `add.b` the query stored.
- Reproject with the previous view and blend `histLen/(histLen+1)`. Sample the reprojected texel when the camera moves. Skip the new ray only while that offset stays under 0.5 px. After 16 frames with reprojection under 1.5 px, write that history texel alone. Keep the average while the normal matches. A pixel with history under 16 blurs at 4 times the step. Clamp is the unweighted same-surface 3×3 mean ± 1.5 sigma. `sigma_luma` comes from the 3×3 luminance deviation, widened while history length is under 16. A-trous steps are 1 and 2, weighted by kernel, normal, and depth. An empty center is filled from neighbors by normal and depth. The spatial pass is one half-resolution 8-tap plus a bilateral depth upsample. The temporal pass writes the next history.

---

## 10. Reflections, weather, simulation, gizmos

SSR marches the FFX hierarchy on the reverse-Z MAX atlas, 64 steps, thickness `gSsr.thick` defaulting to 0.02. `gSsr.thick` and `gSsr.cutoff` come from the sliders. Each pixel keeps its own roughness and metal. `alpha` is roughness squared. VNDF is sampled above roughness 0.06. A perfect mirror is roughness at or below 0.06. A metal at or above `gSsr.b1` still reflects. The march clamps at the coarsest atlas mip and keeps advancing. Step past the reflecting plane and accept the hit only after the march refines below mip 0. Reject a step whose view depth sits behind the surface by more than its thickness. Fade the outer 24 px. The lobe seed is the pixel coordinate. History reprojects the hit through the previous view and clamps to the current 3×3. A screen hit that matches depth shades from the `HdrA` texel. Off screen, shade `sharcDirect` plus the SHaRC cache, plus Fresnel. Hardware rays run only below roughness 0.25. An 8×8 tile with no sample at or below 0.65 is skipped. A rougher miss writes the sky, except a miss whose up-ray hits geometry: that sample stores 0 radiance and 0 length. A miss that leaves the screen otherwise traces one opaque TLAS ray, sun visibility plus `Sharc_SampleRadiance`, blended by screen confidence. The mirror sits on the lowest vertices of the tree, 2 m to the side, cube bottom on the ground. Self-intersection uses the FFX 2 px test.

Rain lives in a world cylinder 9 m across and 18 m tall, 4096 slots, amount 0..4. A streak is 10..22 cm along velocity, shrinks with its life over 0.26 s, and leaves a flat puddle of 3.5..6 cm. Wetness is hermite value noise, accumulated from rain times cloud cover over 60 s. The roof test is the mean of five rays spread 0.7 m. A roof-file change crossfades for 1.8 s and keeps both weights above 0.16. Normals are the slope of stretched hermite noise, with a stream only on the ridge. Wall drips are many thin streams, each a smooth bead along projected gravity, darkening the wall by 3%. Fog thickens as Celsius falls below 12 and the ground dries faster as it rises. The look bob is the same vertical offset on the eye and the target. The phase wraps to two pi.

Physics is a Jolt static mesh of every non-prop instance plus a ground slab, and a dynamic box once the gizmo lets go. An imported instance keeps its transform. The eye is placed at `maxZ + max(extent * 2.2, 0.35)`, near plane `extent * 0.02`. A loaded model has one `Prop` and a `MeshSpan` over every instance.

Gizmos: screen length is `distance * tan(fov/2) * 108` px. Cones on move, cubes on scale, a 32×8 torus, and a center cube. Icons are 72 px within 8 m, lerping to 36 px by 48 m, clamped to 32..80, from that marker’s own depth. A selected actor draws a 12-edge box centered on the actor position, half extents from `ActorBounds` times scale. Axis picking runs only when the origin projects, then the other actors are tested. The center handle’s screen solve is scaled by meters per pixel.

---

## 11. Platform and assets

User directory: `platformUserDir` (`USERPROFILE` / `HOME`). Cache: `platformCachePath` (`%LOCALAPPDATA%` or `%USERPROFILE%` / `$HOME/.cache/`). Executable: `platformExePath` (`GetModuleFileNameA` / `/proc/self/exe`). Host meters: `platformHostMeters`. Application, scene, and render code do not call `getenv("HOME")`, `readlink("/proc/self/exe")`, or read `/proc`.

Directories: `std::filesystem::create_directories` and `directory_iterator`. Process launch lives in `core/platform/Desktop.cpp`: `posix_spawnp` on Linux, `ShellExecuteA` / `CreateProcessA` on Windows. Dynamic libraries go through the platform loader (`LoadLibraryA` / `dlopen`). Dialogs: `core/platform/Dialog.cpp` (SDL3). Windows: `core/platform/Window.cpp` (SDL3).

No hardcoded absolute paths or screen sizes. Resolve from `platformExePath`, the project root, and `platformCachePath`. Textures resolve beside the active `.bhop`, then the adjacent `textures/` directory, then `assets/textures/`, then `uploadDummy`. A leaf after `X/` is found by a depth-4 walk from the working directory and the executable’s project, skipping `cache` and `build`. A pass never hardcodes `newscenetest` or `testscene/assets/bistro`. A missing `.bhop` keeps the procedural scene from `sceneFrameCreate` / `testSceneBuild`. A pass never names a fallback glTF. Parsers are bounds-checked and terminate on bad syntax. There is no fake fallback UI card.

File history steps only to a path that stays under the project root. A drop is tested against the host box. The footer is a 96-byte line with the file type and the size. A `.gltf` and its companion `.bin` are one tile. Delete, duplicate, and move apply to both. Thumbnails are cached on disk under `cache/thumbs/` by path hash and mtime and blitted into atlas slot 10. Model baking (LOD0, bounds fit to `[-1, 1]`, 64×64) lives in `core/asset`.

Browser behavior that stays: single click selects, double click enters a folder or opens a `.bhmat` / scene, Ctrl toggles, Shift ranges, empty-grid drag is a rubber band. Tile names hide the extension, one line, ellipsis. Ctrl+wheel clamps tile size to 48..160, and the settings slider uses that same range. Grid and compact list are both available. Search filters live, with a muted placeholder. The path is a dark pill. The second header button imports through the native open dialog and copies into the current directory. The up control is an arrow, 22×22, inside the pill. Status counts and total size sit on the left. The selection detail sits on the right. Strings are English. Hover shows name, type, size, and modified time. Overflow shows a real vertical thumb. Toolbar and file-bar icon buttons use about 4 px of padding.

---

## 12. Canonical homes

**App.** `demo/app/Main.cpp`. `Engine.hpp/.cpp` composes window, scene, canvas, and controllers. `Shell` is menus, inspector, tabs, docking, extra windows, expressed as JSON plus bind calls. `demo/Sample.cpp` is nested lists, compact rows, context menus, field validation.

**GUI.** Public: `core/gui/include/gui/`. Private: `core/gui/internal/` split by mechanic (layout, scissor, style, text, hit, hover, click, drag, scroll, focus, text edit, virtual pool, overlay, emitter, loader). Facade: `GuiHost`.

**UI GPU bridge.** `core/rhi/ui/`: `Canvas`, `Font`, `UiRenderer`, `UiPhoto`. Slot 48 submission and the FreeType atlas live here. They do not include GUI internals beyond the public facade.

**GPU scene.** `engine/gpu_scene/`: `Mat4` (column-major, reverse-Z), `Camera`, `LightMath`, `InstanceData`, `InstanceCull` (SSE2 frustum, LOD0), `MeshletGpu`, `MeshletCull`, `PointShadow`, `SunShadow`, `SceneObject`. `core/scene/FlyCam.hpp`.

**Render.** `engine/include/render/`: `RenderDefs`, `HeapBind`, `FrameUniforms`, `PassCommon`, `PassAllocators`, `PassBufferSync`, `PassRendering`, `PassShaderGroup`, `PassTargets`, `PassDebug`, `VisResolve`, `SceneFrame` / `SceneFrameImpl`, `Cube`, `Cull`, `Hiz`, `Gizmo`, `Radiance`, `Shade`, `Sky`, `Tonemap`, `Visbuffer`, and the scene-block map and fill. Translation units under `engine/src/`: `SceneFrame`, `Cube`, `Cull`, `Hiz`, `PassGizmo`, `Radiance`, `Shade`, `Sky`, `Tonemap`, `Visbuffer`, `PassClusters`, `PassCull`, `PassPoints`, `PassRadiance`, `PassShade`, `PassSun`, `PassTonemap`, `PassVis`.

**Platform and I/O.** `core/platform/`: `Check`, `Trace`, `Window`, `Dialog`, `Desktop`, `Input`, `Action`, `Jobs`, `PlatformUtil`. `core/debug/CpuScope.hpp`. `core/memory/FrameArena.hpp`. `core/io/`: `File`, `Blob`, `JsonCursor`, `Json`. `core/Str.hpp`. `core/color/Color.hpp`. `core/time/Time.hpp`. `core/doc/UndoStack.hpp`, `Document.hpp`. `core/wasm/WasmHost.hpp`.

**RHI and images.** `core/rhi/`: `VolkVma`, `DeviceCaps`, `Device`, `GpuBuffer`, `GpuImage`, `Barrier`, `DescriptorHeap`, `DescriptorSlots`, `ShaderObject`, `Swapchain`, `MeshDraw`, `GpuProfiler`, `UiPrimitive`. `core/image/`: `ImageDecode`, `ImageCompress`, `ImageUpload`, `ImageFile`.

**Scene data.** `core/gfx/`: `Light`, `Material`, `Meshlet`, `PrimitiveGen`, `MeshletBuilder`, `ClusterGrid`, `HiZ`, `Portal`, `Probe`, `Decal`. `core/asset/`: `SceneBlob`, `AssimpImporter`, `ModelThumb`. `core/scene/TestScene.hpp`. `core/stream/`: `StreamQueue`, `ResidencyTable`, `StreamRing`. `core/debug/DebugStats.hpp`. `core/text/TextLayout.hpp`. `core/anim/Animation.hpp`. `core/input` is not a second input queue.

**Tools.** `core/ui/Domain.hpp` only as a level-6 contract if it does not pull widgets upward. `core/host/Host.hpp`. `core/files/`: `Model`, `Format`, `FileDomain`, `Browser`, `Files`, `Project`. `core/Units.hpp`.

Known piles that mix several jobs and have to stay split by the homes above: a thousand-line UI template loader beside a second property importer, a field implementation that still owns clear buttons and padding, a material editor split across many app files that still build cards in C++, `SceneGeometry` mixing allocation and meshlet build and upload, `SceneUniforms` hand-filling every block, `SceneGizmo` mixing mesh generation and ray tests, `Node` mixing lifetime with name scans and scissor caches, and a menu that computes its own screen position and animation.

---

## 13. Verification

A change is done when every line holds:

1. Dependencies point downward. Platform and RHI do not include each other. GUI internals are invisible outside `bh_gui`. The app does not include `Engine.hpp` from a controller. No render pass includes a UI model.
2. No second JSON parser, string helper, input queue, virtualizer, text measurer, drag, scissor, overlay, or shader-math copy.
3. `Tick`, `Layout`, `Emit`, `Draw`, `Consume`, and `HitTest` do not allocate.
4. Hot structs are aligned PODs addressed by id. No magic tags, fake fixed arrays, or function-local `static` mutables.
5. UI color comes from tokens and `styles.json`. Level 6 uses the facades and semantic binds.
6. Intermediate image barriers keep `discard = false` after the first undefined transition. One SPIR-V per pass family. Resize recreates transient targets only.
7. No unguarded POSIX headers, `getenv("HOME")`, or hardcoded asset roots.
8. Existing behavior (menus, rename, rubberband, modifiers, history, docking, icons, thumbnails, launcher, material preview) is still present.
9. `bh_hygiene` passes.

```bash
cmake --build build-core --target BurnhopeTest BurnhopeEngine -j && ./build-core/BurnhopeTest
```

Zero warnings on the C++26 toolchain in use.
