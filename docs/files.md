# Файлы и зависимости

Живой код: `core/`, `engine/`, `demo/`, `tests/`. `old/` не читается и не входит сюда. Закон кадра — `docs/law.md`. Этот список не значит, что изоляция закончена.

## Почему холст пропадал через секунду

Примитивы холста лежат в буферном слоте кучи `22` (`kUiDescPrims`). Туда же писался хеш RC (`HeapBuf::Rc`). Сборка BLAS встаёт в очередь после первого present, примерно через секунду после загрузки сцены, и затирала дескриптор холста. Лог при этом продолжал писать `ui check draw=900`: процессор примитивы считал, видеокарта читала уже чужой буфер.

Слот RC теперь `18`. `static_assert` в `Radiance.cpp` не даёт снова занять `22`.

## Четыре узла, которые ещё общие

Изоляция не закончена, пока они такие.

1. `engine/render/shade.slang` — один compute считает солнце, лампы, контактную тень и туман. Запись этих проходов уже в разных файлах (`PassSun`, `PassPoints`, `PassShade`). Шейдер — нет. Ломать его правкой лампы всё ещё можно. Разрез: солнце пишет свет, лампы добавляют своим проходом. Атмосфера в этот файл не входит.
2. Панель и сцена держат `HudPost`. В шейдер уходят уже именованные блоки. Мёртвый `PostPack` внутри `FramePost` оставлен, чтобы сдвиг байт соседа не поехал.
3. Одна куча дескрипторов на окно. Карты, глубина, HDR и текстуры — номера слотов. Хост держит запас 64 буфера и не знает имён эффектов. Слот, который занял холст или другой проход, второй раз не берётся. Пересоздание картинки при ресайзе обязано записать тот же слот, не новый.
4. BLAS один, после present, сцена статическая. Отдельного TLAS нет, пока объекты не двигаются. Атмосфера не начата: промах луча — плоский фон. Небо будет своим модулем и готовым цветом солнца, не кодом внутри `shade.slang`.

Базового класса модуля нет. Модуль — свои данные и функция записи. Ядро (`core/rhi`, `core/host`, `core/ui`) эффекты по имени не знает.

## Слои

| Папка | Делает | Не знает |
| :--- | :--- | :--- |
| `core/platform` | окно, ввод, диалоги | виджеты, кадр |
| `core/rhi` | устройство, свопчейн, шейдер, барьер, куча | имена проходов |
| `core/image` | декод и сжатие | сцену |
| `core/ui` | виджеты и один draw холста | что значит клик |
| `core/host` | тик окна, present, запас кучи | SSR, RC, солнце |
| `engine/render` | проходы кадра | холст и SDL |
| `demo` | какие окна открыть и куда писать ползунок | как записать командный буфер |

Ниже каждый исходник живого дерева и первая строка шапки, если она есть.

- `core/anim/Animation.hpp` — Keyframe tracks. UiAnim stays the one-shot color blend. This samples a slice of keys.
- `core/asset/AssimpImporter.cpp` — без шапки
- `core/asset/AssimpImporter.hpp` — без шапки
- `core/asset/SceneBlob.hpp` — без шапки
- `core/color/Color.hpp` — без шапки
- `core/debug/CpuScope.hpp` — без шапки
- `core/debug/DebugStats.hpp` — без шапки
- `core/doc/Document.hpp` — без шапки
- `core/doc/UndoStack.hpp` — без шапки
- `core/files/Browser.hpp` — File manager is a layer above widgets. It plugs into the canvas and builds
- `core/files/Files.cpp` — без шапки
- `core/gfx/ClusterGrid.hpp` — без шапки
- `core/gfx/Decal.hpp` — Oriented box. axis* is the full half-extent vector, not a unit axis.
- `core/gfx/HiZ.hpp` — без шапки
- `core/gfx/Light.hpp` — без шапки
- `core/gfx/Material.hpp` — без шапки
- `core/gfx/Meshlet.hpp` — без шапки
- `core/gfx/MeshletBuilder.hpp` — без шапки
- `core/gfx/Portal.hpp` — без шапки
- `core/gfx/PrimitiveGen.hpp` — без шапки
- `core/gfx/Probe.hpp` — Nine L2 bands, RGB interleaved. Band 0 is the constant term.
- `core/host/Host.cpp` — без шапки
- `core/host/Host.hpp` — Приложение задаёт размер окна и функцию сборки холста. Кадр, swapchain и SDL живут здесь.
- `core/image/ImageFile.cpp` — без шапки
- `core/image/ImageFile.hpp` — Still images and GIF. Frames are stacked top to bottom in rgba.
- `core/image/compress.slang` — GPU-компьют BC1 (план рендера, Фаза 2). Один поток на блок 4x4, без alpha (только непрозрачный
- `core/input/Input.hpp` — без шапки
- `core/io/Blob.hpp` — без шапки
- `core/io/File.cpp` — без шапки
- `core/io/File.hpp` — без шапки
- `core/memory/FrameArena.cpp` — без шапки
- `core/memory/FrameArena.hpp` — Init/load: TLSF/mimalloc later. Frame: bump, reset every frame. No new in loop.
- `core/platform/Check.cpp` — без шапки
- `core/platform/Check.hpp` — без шапки
- `core/platform/Dialog.cpp` — без шапки
- `core/platform/Jobs.cpp` — без шапки
- `core/platform/Jobs.hpp` — Минимальный fork-join job system. Не очередь задач на кадр вперёд — один вызов
- `core/platform/Trace.hpp` — без шапки
- `core/platform/Window.cpp` — без шапки
- `core/platform/Window.hpp` — One sample per frame. Edges are this pump only.
- `core/rhi/Barrier.hpp` — без шапки
- `core/rhi/DescriptorHeap.cpp` — без шапки
- `core/rhi/DescriptorHeap.hpp` — Slot counts are the caller's. RHI does not name passes (vis, UI, …).
- `core/rhi/Device.cpp` — без шапки
- `core/rhi/Device.hpp` — без шапки
- `core/rhi/DeviceCaps.hpp` — Фаза 1.5: frame pacing/present-режим без пересборки свопчейна.
- `core/rhi/GpuBuffer.cpp` — без шапки
- `core/rhi/GpuBuffer.hpp` — Зеркало gpuBufferFlush для чтения: вызывать перед CPU-чтением mapped-памяти, которую писал
- `core/rhi/GpuImage.cpp` — без шапки
- `core/rhi/GpuImage.hpp` — без шапки
- `core/rhi/GpuProfiler.cpp` — без шапки
- `core/rhi/GpuProfiler.hpp` — без шапки
- `core/rhi/MeshDraw.cpp` — без шапки
- `core/rhi/MeshDraw.hpp` — One registered draw pass (plan Фаза 1/5 contract): explicit shaders in, explicit draw
- `core/rhi/ShaderObject.cpp` — без шапки
- `core/rhi/ShaderObject.hpp` — без шапки
- `core/rhi/Swapchain.cpp` — без шапки
- `core/rhi/Swapchain.hpp` — 0 ждёт кадр (FIFO), 1 mailbox, 2 без ожидания, 3 FIFO relaxed. Нет режима — остаётся FIFO.
- `core/rhi/VolkVma.cpp` — без шапки
- `core/scene/FlyCam.hpp` — без шапки
- `core/scene/TestScene.hpp` — без шапки
- `core/stream/ResidencyTable.hpp` — без шапки
- `core/stream/StreamQueue.hpp` — без шапки
- `core/stream/StreamRing.hpp` — без шапки
- `core/text/TextLayout.hpp` — без шапки
- `core/text/msdf.slang` — One mesh group is one glyph: 4 vertices, 2 triangles. No CPU vertex buffer.
- `core/time/Time.hpp` — без шапки
- `core/ui/Canvas.cpp` — без шапки
- `core/ui/Canvas.hpp` — Yoga layout + Flecs columns + one quad draw.
- `core/ui/Font.cpp` — без шапки
- `core/ui/Font.hpp` — SDF-атлас один раз на шрифт. Кадр только сэмплирует R8.
- `core/ui/Json.cpp` — без шапки
- `core/ui/Layout.cpp` — без шапки
- `core/ui/Layout.hpp` — без шапки
- `core/ui/Model.hpp` — Слот 22 буфера кучи. Движок его не занимает: HeapBuf::Rc лежит в 18.
- `core/ui/Panel.cpp` — без шапки
- `core/ui/Panel.hpp` — Невиртуальная панель для кода приложения. Горячий кадр её не вызывает.
- `core/ui/PropertyInspector.hpp` — без шапки
- `core/ui/State.hpp` — A higher module (files, later others) plugs in here. Widgets never name it.
- `core/ui/Style.hpp` — без шапки
- `core/ui/kit/Calendar.cpp` — без шапки
- `core/ui/kit/Color.cpp` — без шапки
- `core/ui/ui.slang` — One draw. Glyph coverage is a FreeType SDF (0.5 = edge). Swapchain is UNORM.
- `core/ui/widget/Button.cpp` — без шапки
- `core/ui/widget/Chrome.cpp` — без шапки
- `core/ui/widget/Curve.cpp` — без шапки
- `core/ui/widget/Detail.hpp` — без шапки
- `core/ui/widget/Field.cpp` — без шапки
- `core/ui/widget/Image.cpp` — без шапки
- `core/ui/widget/Input.cpp` — без шапки
- `core/ui/widget/List.cpp` — без шапки
- `core/ui/widget/Menu.cpp` — Пункт меню — кнопка. Команда задаётся в uiCommandAdd и лежит на UiAction.
- `core/ui/widget/Scroll.cpp` — без шапки
- `core/ui/widget/Slider.cpp` — без шапки
- `core/ui/widget/Splitter.cpp` — без шапки
- `core/ui/widget/Text.cpp` — без шапки
- `core/ui/widget/TextEdit.cpp` — без шапки
- `core/ui/widget/Tip.cpp` — без шапки
- `core/ui/widget/Tree.cpp` — без шапки
- `core/ui/widget/Virtual.cpp` — без шапки
- `core/wasm/WasmHost.cpp` — без шапки
- `core/wasm/WasmHost.hpp` — Host function. args are the wasm params, already popped. Return value is pushed if the type has one result.
- `demo/Sample.cpp` — без шапки
- `demo/app/Engine.cpp` — без шапки
- `demo/app/Engine.hpp` — без шапки
- `demo/app/Main.cpp` — без шапки
- `demo/app/Shell.cpp` — без шапки
- `demo/app/Shell.hpp` — Пример приложения. Другое приложение передаёт свой UiBuilder в hostOpen и не трогает окно, rhi и present.
- `engine/gpu_scene/Camera.hpp` — One pose. Later phases read CameraCull, they do not recompute look-at.
- `engine/gpu_scene/InstanceCull.hpp` — Sphere bounds. Same planes as the instance path. No allocation.
- `engine/gpu_scene/InstanceData.hpp` — One drawable. Affine world is 3 rows × float4 (xyz + translation). 80 bytes, 16-aligned.
- `engine/gpu_scene/Mat4.hpp` — без шапки
- `engine/gpu_scene/MeshletCull.hpp` — без шапки
- `engine/gpu_scene/MeshletGpu.cpp` — без шапки
- `engine/gpu_scene/MeshletGpu.hpp` — Вид, проекция, каскады солнца. Размер заморожен: новый ползунок сюда не вставляется.
- `engine/gpu_scene/PointShadow.hpp` — без шапки
- `engine/gpu_scene/SunShadow.hpp` — без шапки
- `engine/render/Cube.cpp` — без шапки
- `engine/render/Cube.hpp` — без шапки
- `engine/render/Cull.cpp` — без шапки
- `engine/render/Cull.hpp` — без шапки
- `engine/render/HeapMaps.hpp` — Visbuffer frame slots. The heap itself is untyped; these indices are this pass.
- `engine/render/PassCommon.hpp` — разовая задача GPU, группы compute. Барьеры остаются в `core/rhi/Barrier.hpp`.
- `engine/render/PassClusters.cpp` — без шапки
- `engine/render/PassCull.cpp` — без шапки
- `engine/render/PassDebug.hpp` — Same weights as shade, SSR and SSRC: pixel center against the projected triangle.
- `engine/render/PassPoints.cpp` — без шапки
- `engine/render/PassRadiance.cpp` — без шапки
- `engine/render/PassShade.cpp` — без шапки
- `engine/render/PassSsr.cpp` — без шапки
- `engine/render/PassSun.cpp` — без шапки
- `engine/render/PassTonemap.cpp` — без шапки
- `engine/render/PassVis.cpp` — без шапки
- `engine/render/Radiance.cpp` — без шапки
- `engine/render/Radiance.hpp` — World-space hash of radiance intervals. One BLAS of the static scene, ray query on RT cores.
- `engine/render/SSR.cpp` — без шапки
- `engine/render/SSR.hpp` — без шапки
- `engine/render/SceneFrame.cpp` — без шапки
- `engine/render/SceneFrame.hpp` — Биты как у галок по умолчанию. 128 (RC) и 64 (заморозка) выключены: иначе первый кадр,
- `engine/render/SceneFrameImpl.hpp` — без шапки
- `engine/render/Shade.cpp` — без шапки
- `engine/render/Shade.hpp` — без шапки
- `engine/render/Ssrc.cpp` — без шапки
- `engine/render/Ssrc.hpp` — без шапки
- `engine/render/Tonemap.cpp` — без шапки
- `engine/render/Tonemap.hpp` — без шапки
- `engine/render/VisResolve.hpp` — без шапки
- `engine/render/Visbuffer.cpp` — без шапки
- `engine/render/Visbuffer.hpp` — без шапки
- `engine/render/bloom.slang` — без шапки
- `engine/render/camera.slang` — Screen to world. Included after frame.slang. Passes do not each keep a copy.
- `engine/render/cluster.slang` — без шапки
- `engine/render/cmaa2.slang` — CMAA2: trace a color edge and blend only the pixels that sit on it.
- `engine/render/cube.slang` — без шапки
- `engine/render/cull.slang` — Two-phase occlusion. Phase early draws what was visible last frame.
- `engine/render/frame.slang` — FrameView — binding 0, 2240. Ползунки — восемь блоков по 64, свои binding 50–57.
- `engine/render/gtao.slang` — без шапки
- `engine/render/hiz.slang` — без шапки
- `engine/render/radiance.slang` — Sparse hash of world cells. A ray from a visible surface is split by distance
- `engine/render/resolve.slang` — Fullscreen resolve. One R32 vis pixel: 20b instance, 12b primitive.
- `engine/render/shade.slang` — Compute shade: visbuffer + depth + meshlet SSBO → HDR.
- `engine/render/shadowcull.slang` — Cascade cull. The ortho matrix already covers casters whose shadow
- `engine/render/ssr.slang` — без шапки
- `engine/render/ssrc.slang` — Screen-space radiance cascades. Probes sit on the screen. Cascade 0 is dense
- `engine/render/tonemap.slang` — HDR → swapchain. AgX, two-scale Karis bloom, grain, vignette, chromatic aberration.
- `engine/render/visbuffer.slang` — visbuffer — one meshlet per group. 64 vertices, 124 triangles.
- `tests/Core.cpp` — без шапки
- `tests/HostTick.cpp` — без шапки
