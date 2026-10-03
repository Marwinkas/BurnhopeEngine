# Функции по файлам

Список того, что уже есть. Новый код зовёт эти функции. Второй копией их не пишут.

Два входа в интерфейс сходятся в `spawn`:

- статичный экран: `assets/ui/*.json` → `tools/uibake.py` → `.uib` → `uiImportBin`;
- руками: `Panel` / `canvasNode` / `spawn`.

Поведение узла из блоба вешается по имени: `uiBindName`, `canvasBindName`, `Panel::find`.

## `core/platform`

`Window.hpp` не включает SDL. `SDL_Window` только вперёд.

- `windowCreate` — окно по `WindowConfig` или по ширине, высоте и заголовку. При ошибке SDL не гасит остальные окна.
- `windowDestroy`, `windowPump`, `windowsPump` — список окон; первое получает выход.
- `windowSetCursor` — 0–7 прежние, 8–11 move, запрет, ожидание, прогресс.
- `windowRefreshSize`, `windowChrome` — пиксели шапки и кромки.
- `windowTitle`, `windowPosition`, `windowSize`, `windowMinSize`, `windowMaxSize`.
- `windowBordered`, `windowResizable`, `windowOpacity`, `windowAlwaysOnTop`, `windowFullscreen`.
- `windowMaximize`, `windowMinimize`, `windowRestore`, `windowToggleMaximize`.
- `windowShow`, `windowHide`, `windowRaise`, `windowParent`, `windowModal`, `windowFocusable`.
- `windowGrab`, `windowRelativeMouse`, `windowAspect`, `windowFlash`, `windowProgress`.
- `windowFillDocument`, `windowSync`, `windowSystemMenu`, `windowMouseRect`.
- `windowIcon`, `windowShape` — RGBA.
- `windowApply` — пакет команд за кадр. Отрицательное поле значит «не трогать».
- `windowQuery`, `windowDisplay`.
- `windowText` — текстовый ввод SDL.
- `windowClipboardSet`, `windowClipboardGet`.
- `windowAskFile` — открыть, сохранить, папка. Фильтры копируются. Вторая попытка, пока диалог открыт, возвращает false.
- `windowTakeFile` — готовый путь. Отмена пути не даёт.
- `platformSeconds`, `platformError`, `platformTheme` (0 неизвестно, 1 светлая, 2 тёмная), `platformScreensaver`, `platformShutdown`.

`Dialog.cpp` — диалог и память последней папки в `$HOME/.cache/burnhope-dialog.txt`.

## `core/rhi`

- `deviceCreate` — один Vulkan-device на первое окно.
- `deviceCreateSurface` — поверхность следующего окна. Единственный SDL вне платформы.
- `deviceDestroy`, `deviceDumpCaps`.
- `swapchainCreate` / `swapchainRecreate` / `swapchainDestroy`. Режим из `FrameDesc`: `Vsync`, `Mailbox`, `Immediate`, `Relaxed`. Нет режима — FIFO.
- `swapchainBegin`, `swapchainToPresent`, `swapchainSubmitPresent`.
- `gpuBufferCreate`, `gpuBufferDestroy`, `gpuBufferFlush`.
- `gpuImageCreate`, `gpuImageDestroy`.
- `shaderCreate`, `shaderCreateSpvFile`, `shaderDestroy`.
- `cmdBindMeshFrag`, `cmdBindVertFrag`, `cmdBindCompute`, `cmdSetGraphicsDynamic`.
- `descriptorHeapsCreate`, `descriptorHeapsDestroy`, `heapWriteBuffer`, `heapWriteImage`, `heapWriteSampler`, `heapBind`, `heapBufOffset`, `heapImgOffset`, `heapSampOffset`, `heapMap`, `heapAlign`.

Файлы: `Device.cpp`, `Swapchain.cpp`, `GpuBuffer.cpp`, `GpuImage.cpp`, `ShaderObject.cpp`, `DescriptorHeap.cpp`, `VolkVma.cpp`.

## `core/image`

- `imageLoadFile` — декод в `ImageCpu`.
- `imageCpuFree`.
- `gpuImageUploadRgba`, `gpuImageUploadHdr` — сжатие ISPC и отдача текстуры в `rhi`.

`ImageFile.cpp`.

## `core/ui` — холст

`Canvas.cpp` / `Canvas.hpp`.

- `canvasCreate`, `canvasDestroy` — буфер примитивов, шейдеры, шрифт по умолчанию через `canvasUseFont`.
- `canvasConsume` — ввод кадра, Yoga, один draw-набор.
- `canvasRecord` — записать draw в командный буфер.
- `canvasSetLook`, `canvasLook`.
- `canvasWantsText`, `canvasTakeWindowOps`, `canvasChrome`.
- `canvasSetBuilder`, `canvasReload`, `canvasBuildSample`, `canvasBuildColor`, `canvasSetBook`.
- `canvasImportJson` — документ или вёрстка, открытая в рантайме. Штампованный экран сюда не кладут.
- `canvasImportBin` — блоб `.uib` в родителя.
- `canvasFind`, `canvasBindName` — имя из блоба.
- `canvasNode` — создать узел. Это вход `Panel`.
- `canvasPage`, `canvasShow`, `canvasReparent`, `canvasPlace`, `canvasGrow`, `canvasSetHeight`.
- `canvasSetClick`, `canvasOnFocus`, `canvasBind`, `canvasFold`, `canvasListen`.
- `canvasRange`, `canvasDock`, `canvasCommitDock`, `canvasSetDrop`, `canvasOpenOs`.
- `canvasOfferFile`, `canvasLoadImage`, `canvasLoadJsonDoc`, `canvasSaveJsonDoc`.
- `canvasCursor`, `canvasSetIcon`, `canvasSelection`, `canvasBindContext`.
- `canvasMakeMenu`, `canvasAddCommand`, `canvasAddCommandTo`, `canvasMenuStyle`.
- `canvasText`, `canvasStamp`, `canvasTextStyle`, `canvasTextId`, `canvasIcon`.
- `canvasSetFont` — путь, который пришёл снаружи. В демо его не зовут с зашитым путём.
- `canvasUseFont` — DejaVu из известных мест системы.
- `canvasSetPaint`, `canvasBorder`, `canvasShadow`, `canvasPad`, `canvasHover`, `canvasDisable`, `canvasAnimate`.
- `canvasField`, `canvasFieldExtra`, `canvasCheck`, `canvasTakeClip`.
- `uiFieldMulti` — поле принимает перевод строки. Каретка и выделение считаются по строкам, ширина поля переносит текст. Стрелки вверх и вниз ходят по строкам.
- Заливка. `UiPaint.radius` — все углы. `radiusTr`, `radiusBr`, `radiusBl` меньше нуля значат «взять `radius`». Ноль — прямой угол. Примитив 80 байт, углы в `corner[4]` (слева сверху, справа сверху, справа снизу, слева снизу).
- `UiPaint.ramp` на панели — линейный градиент того же шейдера (режимы рампы 1–7, как у цвета).
- `UiPaint.shadow` — тень спадом SDF, не отдельным непрозрачным прямоугольником. `borderW` — толщина обводки, край сглаживает `fwidth`.
- `UiPaint.angle/scaleX/scaleY` (rotate/scale вокруг центра бокса) и `UiPaint.bright/contrast` (filter) — один draw, без второго прохода. Упаковка в `pad0`/`pad1` примитива (`kUiFlagTransform`, фиксированная точка 16/8 бит, `uiPackTransform`/`uiUnpackTransform` в `Model.hpp`, то же в `ui.slang`). Несовместимо с `kUiFlagClip` и `kUiFlagShadow`/`kUiFlagStroke` на одном примитиве — они тоже используют `pad0`/`pad1`; трансформированная/отфильтрованная панель с активным clip эти байты не трогает (панель рисуется без transform/filter, пока на ней есть clip). `Panel::transform(angle, scaleX, scaleY)` / `Panel::filter(bright, contrast)`, снизу `canvasTransform` / `canvasFilter`.
- `cmdSetGraphicsDynamic(..., blend)` — 0 прямой альфа, 1 premultiplied, 2 additive, 3 multiply, 4 screen. Прозрачное окно само ставит 1.

`Font.cpp` — SDF-атлас. Снаружи его зовёт `canvasSetFont`.

`Panel.cpp` — ручная сборка, первый гражданин:

- `Panel::make`, `Panel::at`, `Panel::find`.
- `child`, `label`, `textButton`, `button`, `field`, `check`, `radio`.
- `setText`, `color`, `animate`.

Каждый из них кончается в `canvasNode` → `spawn`.

## `core/ui` — данные и блоб

`Json.cpp` — документ JSON во Flecs, не картинка экрана.

- `uiImportJson`, `uiImportJsonFile` — разобрать текст в дерево виджетов, когда файл открыли в рантайме.
- `jsonLoadFile`, `jsonSaveFile`, `jsonRelease`, `jsonFind`, `jsonAt`, `jsonString`, `jsonNumber`.

`Layout.cpp` — клиент `spawn`. Читает BHUI версии 2.

- `uiImportBin`, `uiImportBinFile`.
- `uiName` — `UiName` и символ Flecs. `bindName` в `Json.cpp` зовёт её же.
- Роль `chrome` → `uiWindowChrome`. `calendar` → `uiBuildCalendar`. `color` → `fillColorBody`. `menu` — скрытая панель. `image` — панель и `UiImage`. Остальные роли — `spawn`.

`tools/uibake.py` — офлайн-пекарь. Кадр его не линкует.

`Chrome.cpp` — `uiWindowChrome`: свернуть, развернуть, закрыть. Действия ставят `opMinimize` / `opMaximize`.

## `core/ui/widget`

Общий вход — `Tree.cpp`:

- `spawn` — нода Flecs, бокс Yoga, заливка. Кнопка, поле, галочка и крестик получают ребёнка `Label`.
- `uiNode` — то же для внешнего кода.
- `addText`, `uiText`, `uiSetFieldText`, `uiStamp`, `uiTextStyle`, `uiTextId`, `uiIcon`.
- `uiBind`, `uiBindName`, `uiDrag`, `uiEdit`, `uiFold`, `uiFocus`.
- `uiShow`, `uiReparent`, `uiDrop`, `uiPlace`, `uiGrow`, `uiDockTo`.
- `uiCommandAdd`, `uiCommandAddTo`.
- `uiClear`, `uiLayout`, `uiRestyle`, `uiMotionTo`, `uiTickMotion`.
- `uiApplyInput`, `uiEmit`, `uiEmitText`, `uiEmitRun`, `uiListen`.
- `uiTip`, `uiTipAt`, `hitTest`.

По файлам, каждый рисует себя и не знает экран:

- `Button.cpp` — клик зовёт `UiAction`, затем общий `onClick`.
- `Field.cpp` — каретка, выделение, очистка, рамка. Байты правит `TextEdit.cpp`.
- `Text.cpp` — `textRun`: выравнивание, перенос, многоточие, декор, интервалы, регистр, размер.
- `TextEdit.cpp` — вставка и стирание UTF-8, слово, прокрутка каретки.
- `Slider.cpp` — число, дорожка, ползунок, рампа.
- `Scroll.cpp` — прокрутка содержимого.
- `Splitter.cpp` — перетаскивание границы.
- `Image.cpp` — слот текстуры, миникарта.
- `List.cpp` — строки как кнопки.
- `Menu.cpp` — панель пунктов. Команда лежит на `UiAction`.
- `Input.cpp` — разбор кадра ввода по ролям. Новые теги экрана сюда не добавляют.
- `Tip.cpp` — подсказка.

`Detail.hpp` — `px`, `paint` и внутренние помощники календаря, цвета и поля. Это не второй API для приложения. Приложение зовёт `Panel` и `canvas*`.

## `core/ui/kit`

`Calendar.cpp` — `uiBuildCalendar`, заливка месяца, клики дня, сегодня, стрелки, закрытие.

`Color.cpp` — `uiBuildColor`, `fillColorBody`, HSV, hex, книга цвета `ColorBook`. Колесо всё ещё узнаётся по тегам кадра. Новый канал туда не добавлять.

## `core/files`

`Browser.hpp`:

- `filesAttach` — вставляет проводник в уже созданный `fileFrame`.
- `filesMount`, `filesShow`, `filesGo`, `filesRoot`, `filesClick`, `filesStyle`, `filesOnOpen`, `filesShutdown`.

`Files.cpp` собран из тех же кнопок и полей. Сетку и низ пока не переписывать.

## `core/host`

- `hostInit`, `hostShutdown`.
- `hostOpen` — `ViewDesc`: имя приложения, `WindowConfig`, `FrameDesc`, `UiBuilder`, книга цвета, `onOps`, `onClose`. То же имя поднимает окно и не затирает кадр.
- `hostClose`, `hostSetFrame`, `hostFrame`, `hostTick`, `hostQuit`, `hostCanvas`.

Имена окон вроде `tool` и `color` живут в демо. Кадр их не толкует.

## `demo`

- `Main.cpp` — цикл, пока `hostQuit` ложен.
- `Engine.cpp` — три `ViewDesc`: `main`, `tool`, `color`. Режим present. Свой SDL и свой swapchain не пишет.
- `Shell.cpp` — `shellBuild` грузит экран через `canvasBuildSample`, потом из C++ дописывает YOGA GROW в `page_look` через `Panel::find`. `toolBuild` целиком на `Panel`. `colorBuild` зовёт `canvasBuildColor`.
- `Sample.cpp` — `uiBuildShell`: `uiImportBinFile`, поиск имён, `uiBindName`, команды меню, рамка проводника. Страницы и форма — смысл демо, не ядра.

## `engine`

В этот exe не входит.

- `visPassCreate`, `visPassDestroy`, `visPassResize`, `visPassRecord`.
- `shadePassCreate`, `shadePassDestroy`, `shadePassResize`, `shadePassRecord`.
- `ssrPassCreate`, `ssrPassDestroy`, `ssrPassRecord`.
- `tonemapPassCreate`, `tonemapPassDestroy`, `tonemapPassRecord`.
- `meshletGpuCreate`, `meshletGpuDestroy`, `meshletGpuWriteFrame`.

## Правила, которые держит эта сборка

1. Статичная картинка — JSON, на сборке `.uib`. Кадр блоб читает байтами.
2. Ручной C++ API (`Panel`, `spawn`, `child`, `button`) остаётся. Загрузчик блоба им пользуется.
3. Узел из блоба связывают по имени, не по номеру.
4. Путь шрифта по умолчанию пишет только `canvasUseFont`.
5. Хром, календарь, цвет, меню и картинка — роли ядра, не циклы в демо.
6. Виджет рисует себя и отдаёт событие. Смысл события пишется там, где узел создали или нашли по имени.
7. Ядро не включает `demo`, `engine`, `old`.

## Ручки, которые уже крутятся

- `WindowConfig` — заголовок, x, y, w, h, min, max, рамка, resize, hidden, поверх всех, fullscreen, focus, modal, прозрачность, opacity, aspect, вид окна (обычное, utility, tooltip, popup), родитель, `maximized`.
- `FrameDesc` — `Present` (`Vsync`, `Mailbox`, `Immediate`, `Relaxed`), `transparent`, `clipped`, `hz` (0 — без лимита, частоту держит present).
- `windowFullscreenKind` — 0 окно, 1 borderless, 2 exclusive.
- `InputFrame` — x, y, dx, dy, колесо X/Y, кнопки включая X1/X2, `keyEdge` / `keyDown` / `keyUp`, Ctrl, Shift, Alt, Super, Caps, Num, текст, композиция.
- Курсор `windowSetCursor` 0–11. Стек виджета — `uiCursorPush` / `uiCursorPop`. Цветной — `windowCursorRgba`. Сглаживание — `windowMouseSmooth`.
- `UiPaint` — цвет, `radius` и три остальных угла, `borderW`, цвет обводки, `shadow`, `ramp`, `disabled`, `tag`, hover-цвет, `angle`/`scaleX`/`scaleY` (rotate/scale), `bright`/`contrast` (filter).
- `UiField` — байты, caret, anchor, filter, clear, maxChars, readOnly, required, password, `multi`, hint, scroll, цвет текста, padL, padR.
- `UiFlex` — направление, wrap, выравнивание, размер px/percent/auto, grow, shrink, padding, gap, margin, absolute, overflow, aspect.
- `UiLook` — цвета кадра, радиусы, `glyphH`, `advance`, высота шапки, кромка resize.
- Смешивание кадра — аргумент `cmdSetGraphicsDynamic`: 0–4.
- Действие — до 4 аккордов, trigger press/release/hold, kind digital/axis, context global/modal/focus. Файл `$HOME/.cache/burnhope-actions.txt`.

# Сигнатуры ядра

Снято с заголовков `core/`. Новая публичная функция дописывается сюда в том же заходе, что и код.

### `core/platform/Check.hpp`
- `[[noreturn]] void bhFail(const char* message)` — печатает сообщение, стек `cpptrace`, затем `__builtin_trap()`. Только в debug (`NDEBUG` не задан).
- `BH_ASSERT(expr, msg)` — в debug зовёт `bhFail(msg)`, если `expr` ложно; в release — пустой `(void)0`, ничего не стоит.
- `BH_VERIFY(expr)` — то же с `msg = #expr`.
- Применён в `core/image/ImageFile.cpp`: `submitImage`/`submitImageHostCopy` проверяют `mipCount <= 16` (размер фиксированных массивов региона копии) — громкий отказ со стеком вместо тихой порчи памяти, если когда-то придёт вызов с другим источником числа мипов.
- `core/platform/Trace.hpp`: `BH_ZONE` — `tracy::ZoneScoped` под `BH_TRACY`, иначе `(void)0`. Используется в `hitTest` (`Tree.cpp`), `Canvas.cpp`, `Swapchain.cpp`.

### `core/platform/Window.hpp`
- `[[nodiscard]] bool windowCreate(Window& w, const WindowConfig& config)`
- `[[nodiscard]] bool windowCreate(Window& w, int width, int height, const char* title)`
- `void windowDestroy(Window& w)`
- `void windowPump(Window& w)`
- `void windowsPump(Window& primary, Window* extra = nullptr, Window* third = nullptr)`
- `void windowsPump(Window** list, int count)`
- `void windowSetCursor(Window& w, int kind)`
- `void windowRefreshSize(Window& w)`
- `void windowChrome(Window& w, float titleBarPx, float resizeBorderPx)`
- `bool windowTitle(Window& w, const char* title)`
- `bool windowPosition(Window& w, int x, int y)`
- `bool windowSize(Window& w, int width, int height)`
- `bool windowMinSize(Window& w, int width, int height)`
- `bool windowMaxSize(Window& w, int width, int height)`
- `bool windowBordered(Window& w, bool bordered)`
- `bool windowResizable(Window& w, bool resizable)`
- `bool windowOpacity(Window& w, float opacity)`
- `bool windowAlwaysOnTop(Window& w, bool onTop)`
- `bool windowFullscreen(Window& w, bool fullscreen)`
- `bool windowMaximize(Window& w)`
- `bool windowMinimize(Window& w)`
- `bool windowRestore(Window& w)`
- `bool windowToggleMaximize(Window& w)`
- `bool windowShow(Window& w)`
- `bool windowHide(Window& w)`
- `bool windowRaise(Window& w)`
- `bool windowParent(Window& w, Window* parent)`
- `bool windowModal(Window& w, bool modal)`
- `bool windowFocusable(Window& w, bool focusable)`
- `bool windowGrab(Window& w, bool keyboard, bool mouse)`
- `bool windowRelativeMouse(Window& w, bool on)`
- `bool windowAspect(Window& w, float minAspect, float maxAspect)`
- `bool windowFlash(Window& w, int op)`
- `bool windowProgress(Window& w, int state, float value)`
- `bool windowFillDocument(Window& w, bool fill)`
- `bool windowSync(Window& w)`
- `bool windowSystemMenu(Window& w, int x, int y)`
- `bool windowMouseRect(Window& w, int x, int y, int width, int height)`
- `bool windowIcon(Window& w, int width, int height, const uint8_t* rgba)`
- `bool windowShape(Window& w, int width, int height, const uint8_t* rgba)`
- `void windowApply(Window& w, const WindowCommand& command)`
- `[[nodiscard]] bool windowQuery(const Window& w, WindowState& out)`
- `[[nodiscard]] bool windowDisplay(const Window& w, DisplayInfo& out)`
- `void windowText(Window& w, bool on)`
- `void windowTextArea(Window& w, int x, int y, int width, int height)`
- `void actionBind(const char* name, int scancode, uint8_t mods)`
- `void actionBindChord(const char* name, int scancode, uint8_t mods, uint8_t trigger, uint8_t mouse, uint8_t kind, uint8_t context)`
- `[[nodiscard]] bool actionEdge(const InputFrame& in, const char* name)`
- `[[nodiscard]] bool actionHeld(const InputFrame& in, const char* name)`
- `[[nodiscard]] bool actionReleased(const InputFrame& in, const char* name)`
- `[[nodiscard]] float actionAxis(const InputFrame& in, const char* name)`
- `void actionPush(uint8_t context)`
- `void actionPop()`
- `void actionListen(const char* name)`
- `[[nodiscard]] bool actionSave(const char* path)`
- `[[nodiscard]] bool actionLoad(const char* path)`
- `void platformSleep(int ms)`
- `void platformPace(int hz)`
- `[[nodiscard]] uint64_t platformCounter()`
- `[[nodiscard]] uint64_t platformFrequency()`
- `[[nodiscard]] uint16_t windowHoldMs(const Window& w, int scancode)`
- `bool windowFullscreenKind(Window& w, int kind)`
- `bool windowCenter(Window& w)`
- `bool windowToDisplay(Window& w, int displayIndex)`
- `bool windowClickThrough(Window& w, bool on)`
- `bool windowMouseSmooth(Window& w, bool on)`
- `bool windowCursorRgba(Window& w, int width, int height, const uint8_t* rgba, int hotX, int hotY)`
- `void windowRemember(const Window& w, const char* name)`
- `void windowRecall(WindowConfig& config, const char* name)`
- `void windowClipboardSet(const char* text)`
- `[[nodiscard]] bool windowClipboardGet(char* out, int cap)`
- `[[nodiscard]] bool windowAskFile(Window& w, const FileFilter* filters, int count, FileAsk ask)`
- `[[nodiscard]] bool windowTakeFile(char* out, int cap)`
- `[[nodiscard]] float platformSeconds()`
- `[[nodiscard]] const char* platformError()`
- `[[nodiscard]] int platformTheme()`
- `void platformScreensaver(bool enabled)`
- `void platformShutdown()`

### `core/memory/FrameArena.hpp`
- `[[nodiscard]] bool frameArenaCreate(FrameArena& a, std::size_t bytes)`
- `void frameArenaDestroy(FrameArena& a)`
- `void frameArenaReset(FrameArena& a)`
- `[[nodiscard]] void* frameArenaAlloc(FrameArena& a, std::size_t bytes, std::size_t align = 16)`
- `return static_cast<T*>(frameArenaAlloc(a, sizeof(T), alignof(T)))`

### `core/rhi/Barrier.hpp`
- `vkCmdPipelineBarrier2(cmd, &dep)`
- `vkCmdPipelineBarrier2(cmd, &dep)`

### `core/rhi/DescriptorHeap.hpp`
- `[[nodiscard]] VkDeviceSize heapAlign(VkDeviceSize value, VkDeviceSize alignment)`
- `[[nodiscard]] bool descriptorHeapsCreate(DescriptorHeaps& h, Device& d, HeapLayout layout)`
- `void descriptorHeapsDestroy(DescriptorHeaps& h, Device& d)`
- `[[nodiscard]] bool heapWriteBuffer( DescriptorHeaps& h, Device& d, uint32_t slot, VkDescriptorType type, VkDeviceAddress address, VkDeviceSize size)`
- `[[nodiscard]] bool heapWriteImage( DescriptorHeaps& h, Device& d, uint32_t slot, VkDescriptorType type, const GpuImage& img, VkImageLayout layout, VkImageAspectFlags aspect)`
- `[[nodiscard]] bool heapWriteSampler( DescriptorHeaps& h, Device& d, uint32_t slot, const VkSamplerCreateInfo& ci)`
- `void heapBind(VkCommandBuffer cmd, const DescriptorHeaps& h)`
- `[[nodiscard]] uint32_t heapBufOffset(const DescriptorHeaps& h, uint32_t slot)`
- `[[nodiscard]] uint32_t heapImgOffset(const DescriptorHeaps& h, uint32_t slot)`
- `[[nodiscard]] uint32_t heapSampOffset(const DescriptorHeaps& h, uint32_t slot)`
- `[[nodiscard]] VkDescriptorSetAndBindingMappingEXT heapMap( uint32_t set, uint32_t binding, VkSpirvResourceTypeFlagsEXT resourceMask, uint32_t heapOffset, uint32_t heapStride)`

### `core/rhi/Device.hpp`
- `[[nodiscard]] bool deviceCreate(Device& d, Window& window)`
- `[[nodiscard]] bool deviceCreateSurface(Device& d, Window& window, VkSurfaceKHR& out)`
- `void deviceDestroy(Device& d)`
- `void deviceDumpCaps(const Device& d)`
- `void deviceName(Device& d, uint64_t handle, VkObjectType type, const char* name)`
- `void rhiCaptureToggle()`

### `core/rhi/DeviceCaps.hpp`
Обнаруживается в `fillCaps` (`Device.cpp`) один раз на старте, каждое поле — отдельная проверка расширения/фичи у физического устройства. Поле `true` только если расширение реально включено в `vkCreateDevice` — определение и включение держат одну и ту же переменную `d.caps`, нет пути, где `caps` сообщает `true`, а фича не запрошена у драйвера.
- `shaderObject`, `descriptorHeap` — обязательны, без них `deviceCreate` падает (M0).
- `presentWait`, `presentId`, `dynamicRendering`, `timelineSemaphore`, `bufferDeviceAddress`, `sync2` — ядро 1.3/1.2, уже требуются или включаются.
- `meshShader`, `rayQuery`, `dgc` — опциональные, под будущий GPU-driven/3D путь (`docs/now.md`).
- `maintenance6`, `hostImageCopy` — ядро Vulkan 1.4 (`VkPhysicalDeviceVulkan14Features`), включаются в `vkCreateDevice` только если обнаружены у физического устройства; `hostImageCopy` — прямая CPU→GPU загрузка без staging-буфера, будет использован при переносе сжатия текстур на GPU.
- `graphicsFamily`/`presentFamily`/`computeFamily`/`transferFamily` — индексы очередей, `deviceName`/`apiMajor`/`apiMinor`/`apiPatch` — для лога/диагностики.

### `core/rhi/GpuBuffer.hpp`
- `[[nodiscard]] bool gpuBufferCreate( GpuBuffer& b, Device& d, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible)`
- `void gpuBufferDestroy(GpuBuffer& b, Device& d)`
- `void gpuBufferFlush(const GpuBuffer& b, Device& d, VkDeviceSize offset, VkDeviceSize size)`

### `core/rhi/GpuImage.hpp`
- `[[nodiscard]] bool gpuImageCreate( GpuImage& img, Device& d, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mipLevels = 1)`
- `void gpuImageDestroy(GpuImage& img, Device& d)`

### `core/rhi/ShaderObject.hpp`
- `[[nodiscard]] bool shaderCreate(Device& d, const ShaderCreateDesc& desc, ShaderExt& out)`
- `[[nodiscard]] bool shaderCreateSpvFile( Device& d, const char* path, VkShaderStageFlagBits stage, VkShaderStageFlags nextStage, ShaderExt& out)`
- `void shaderDestroy(Device& d, ShaderExt& s)`
- `void cmdBindMeshFrag(VkCommandBuffer cmd, VkShaderEXT mesh, VkShaderEXT frag)`
- `void cmdBindVertFrag(VkCommandBuffer cmd, VkShaderEXT vert, VkShaderEXT frag, bool nullMesh)`
- `void cmdBindCompute(VkCommandBuffer cmd, VkShaderEXT cs)`
- `void cmdSetGraphicsDynamic( VkCommandBuffer cmd, VkExtent2D extent, uint32_t colorAttCount, bool depthTest, bool alphaBlend = false, uint8_t blendMode = 0)`

### `core/rhi/MeshDraw.hpp` (план Фаза 1/5)
Единая точка одного зарегистрированного draw-прохода — явные шейдеры и явный draw count на входе, ровно один `vkCmdDrawMeshTasksEXT`/`vkCmdDraw` на выходе, без скрытого состояния между проходами. `core/ui/Canvas.cpp` (mesh + vertex-pull fallback), `engine/render/Visbuffer.cpp`, `engine/render/Tonemap.cpp` (mesh-only) зовут одну и ту же функцию, второй копии паттерна `cmdBindMeshFrag`+`vkCmdDrawMeshTasksEXT` в коде не осталось.
- `struct MeshDrawDesc { VkShaderEXT mesh; VkShaderEXT vert; VkShaderEXT frag; bool vertNullMeshStage; uint32_t groupCount; uint32_t vertexCount; }`
- `void meshDrawRecord(VkCommandBuffer cmd, const MeshDrawDesc& desc)` — biнdit mesh-путь, если `mesh != VK_NULL_HANDLE && groupCount > 0`; иначе vertex-pull, если `vert != VK_NULL_HANDLE && vertexCount > 0`; иначе ничего не делает (шейдеры не биндит, draw не зовёт). Caller уже начал dynamic rendering и выставил `cmdSetGraphicsDynamic`.

### `core/rhi/Swapchain.hpp`
- `[[nodiscard]] bool swapchainCreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h)`
- `[[nodiscard]] bool swapchainCreate(Swapchain& sc, Device& d, VkSurfaceKHR surface, uint32_t w, uint32_t h, const FrameDesc& frame =`
- `void swapchainDestroy(Swapchain& sc, Device& d)`
- `[[nodiscard]] bool swapchainRecreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h)`
- `[[nodiscard]] bool swapchainBegin(Swapchain& sc, Device& d, FrameContext& fc)`
- `void swapchainToPresent(Swapchain& sc, const FrameContext& fc)`
- `[[nodiscard]] bool swapchainSubmitPresent(Swapchain& sc, Device& d, FrameContext& fc, bool pace = true)` — `pace=false` пропускает блокирующий `vkWaitForPresentKHR`, нужно для неглавных окон, чтобы они не ждали vsync друг друга по очереди в одном потоке.

### core/rhi/DeviceCaps.hpp — расширения плана 2026 (детект в `fillCaps`, включение в `deviceCreate`)

- `swapchainMaintenance1` — `VK_EXT_swapchain_maintenance1`. Требует инстанс-расширения `VK_KHR_get_surface_capabilities2` **и** `VK_EXT_surface_maintenance1` (проверяются и добавляются в `deviceCreate` до создания инстанса); если инстанс-пререквизит не поднялся, `caps.swapchainMaintenance1` принудительно сбрасывается в `false` сразу после `fillCaps`, чтобы само поле никогда не врало. Фича включается на устройстве; код, который её реально использует (frame pacing без пересборки свопчейна), ещё не написан — задел под Фазу 1.5.
- `nestedCommandBuffer` — `VK_EXT_nested_command_buffer`. Включена на устройстве, потребителя ещё нет — задел под Фазу 5+ (многопоточная запись командных буферов для 3D-сцены).
- `depthClampControl` — `VK_EXT_depth_clamp_control`. Включена, потребителя нет — задел под будущий 3D depth pass.
- `fragmentShaderBarycentric` — `VK_KHR_fragment_shader_barycentric` (не EXT — в реальном драйвере есть и `VK_NV_fragment_shader_barycentric`, выбран KHR). Включена, потребителя нет — задел под debug-визуализацию visbuffer (wireframe без отдельного буфера).
- `cooperativeMatrix` — `VK_KHR_cooperative_matrix`. Только обнаружение и лог (`cooperativeMatrix(discover-only)`), фича **не запрашивается** на устройстве — нет потребителя ни в одной текущей фазе. Задел под будущий ReSTIR-деноизер/нейро-апскейл (Северная звезда плана). Это единственное поле `DeviceCaps`, где "detect true" осознанно не равно "enable true" — и это не баг `maintenance6`, потому что ни один код в `deviceCreate` не притворяется, что фича включена.

### `core/ui/Canvas.hpp`
- `Canvas.ms` — `ShaderExt` для mesh-shader пути (`uiMs`). Null, если `DeviceCaps.meshShader` нет или создание `ShaderEXT` не удалось — тогда `canvasRecord` рисует через `Canvas.vs`/`uiVs` (vertex-pull), как раньше.

### core/image/ImageFile.cpp — host_image_copy (Фаза 2)

- `submitImage` сначала пробует внутреннюю `submitImageHostCopy`: `vkTransitionImageLayout` + `vkCopyMemoryToImage` + `vkTransitionImageLayout` — прямая CPU→GPU запись без staging-буфера, командного пула и `vkQueueSubmit2`/`vkQueueWaitIdle`. Включается только если `Device.caps.hostImageCopy` **и** формат объявляет `VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT` в `optimalTilingFeatures` (внутренняя `hostCopyOk`) — не все BC-форматы это умеют на всех драйверах. Любая неудача на этом пути возвращает `false` без побочных эффектов, `submitImage` молча падает на старый путь со staging-буфером. Байты, летящие в текстуру, те же самые (ISPC-сжатие не менялось) — меняется только механизм переноса.
- Регрессия — `tests/Core.cpp`: `"gpuImageUploadRgba round-trips through host_image_copy"`, реальный `Device` на скрытом `Window`, синтетическая 256×256 RGBA с переменной альфой (форсирует BC7, 7 мипов), проверяет успешную загрузку и размеры текстуры. Пропускается (не валит сборку), если в окружении нет дисплея/GPU.
- Обе функции создания образа (`submitImage` и `submitImageHostCopy`) теперь просят `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` в дополнение к `TRANSFER_DST`/`SAMPLED` — ничего не стоит на десктопных GPU, но даёт читать назад любую загруженную текстуру (тесты, будущий дебаг-дамп).

### core/platform/Jobs.hpp — job system (Фаза 1.5)

- `using JobFn = void (*)(void* user)`; `struct Job { JobFn fn; void* user; }`.
- `void jobsRunAndWait(Job* jobs, int count)` — fork-join: блокирует вызывающий поток, пока все `count` задач не выполнятся. `count<=1` — прямой вызов без пула. Пул ленивый, потоки создаются один раз на процесс (`hardware_concurrency()-1`, максимум 7).
- `int jobsWorkerCount()` — число потоков, которые реально тянут работу (пул + вызывающий), для логов/диагностики.
- Использование: `core/host/Host.cpp` — параллельная запись кадра разных открытых окон. Не привязан к рендеру — годен для любой CPU-параллельной работы (например, будущий CPU BC7 encode по блокам).

### core/rhi/Device.hpp — `queueMutex`

- `std::mutex queueMutex` — одна блокировка на (1) `vkQueueSubmit2`/`vkQueuePresentKHR` на общей `VkQueue` (все окна делят `graphicsQueue`/`presentQueue`/`transferQueue`) и (2) общий разовый `commandPool`/`transferPool` для upload текстур/шрифтов. Один мьютекс, не два — загрузка текстуры сама делает submit на ту же очередь, что и кадр; раздельные мьютексы не дали бы взаимного исключения между ними. Берут: `swapchainSubmitPresent` (`Swapchain.cpp`), `submitImage`/`gpuCompressBc1` (`ImageFile.cpp`), `uploadAtlas` (`Font.cpp`).

### core/image/compress.slang — GPU-компьют BC1 (Фаза 2)

- `csBc1` — один поток на блок 4×4, per-channel min/max (не ISPC-уровень перебора партиций, но корректный 4-цветный BC1). Источник: свой буфер `[width,height,pad,pad]` + RGBA8 с offset 16. Назначение: BC1-блоки по 8 байт. Гарантирует `color0_u16 > color1_u16` (всегда непрозрачный 4-цветный режим, никогда punch-through alpha).
- `gpuCompressBc1` (внутренняя, `ImageFile.cpp`) — создаёт свои `DescriptorHeaps` (2 буфера), свой `ShaderExt` на вызов, диспатчит, читает результат через `gpuBufferInvalidate` + `memcpy`. Используется только для формата BC1 (путь без альфы); BC7/BC3/BC6H остаются на CPU ISPC — нет простого корректного GPU-алгоритма с перебором партиций в этом бюджете. Неудача (нет compute, нет шейдера) молча падает на `compressRgbaLevel` (CPU).
- `gpuBufferInvalidate(const GpuBuffer&, Device&, offset, size)` — новая функция в `GpuBuffer.hpp`, зеркало `gpuBufferFlush` для чтения: `vmaInvalidateAllocation` перед CPU-чтением памяти, которую писал GPU. Не было нужно раньше — ничего не читало GPU-записи с хоста до этой фичи.
- Регрессия — `tests/Core.cpp`: `"gpuImageUploadRgba: GPU BC1 decodes close to the source"`. Не сравнение байт в байт с CPU (разные энкодеры законно выбирают разные конечные точки), а golden-критерий лоссового кодека: декодирует BC1-блоки вручную (эталонный декодер в тесте) и проверяет среднюю ошибку канала < 20/255 от исходных пикселей. На RTX 3090 — 2.4/255.
- `void (*builder)(Canvas&, void*) = nullptr`
- `[[nodiscard]] bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps)`
- `void canvasDestroy(Canvas& c, Device& d)`
- `[[nodiscard]] UiEvent canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, float timeSec)`
- `void canvasSetLook(Canvas& c, const UiLook& look)`
- `[[nodiscard]] UiLook canvasLook(const Canvas& c)`
- `[[nodiscard]] bool canvasWantsText(const Canvas& c)`
- `[[nodiscard]] bool canvasWantsRelative(const Canvas& c)`
- `[[nodiscard]] bool canvasFocusBox(const Canvas& c, float& x, float& y, float& w, float& h)`
- `void canvasApplyDpi(Canvas& c, float dpi, float previous)`
- `void canvasSetDpi(Canvas& c, float dpi)`
- `[[nodiscard]] bool canvasMouseClip(const Canvas& c, float& x, float& y, float& w, float& h)`
- `void canvasTakeWindowOps(Canvas& c, WindowOps& out)`
- `[[nodiscard]] bool canvasAddCommand(Canvas& c, const char* name, UiCommandFn fn, void* user)`
- `void canvasMenuStyle(Canvas& c, float width, float itemH, float pad, float gap)`
- `void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder)`
- `void canvasBuildSample(Canvas& c)`
- `void canvasBuildColor(Canvas& c)`
- `void canvasSetBook(Canvas& c, ColorBook* book)`
- `[[nodiscard]] uint16_t canvasImportJson(Canvas& c, uint16_t parent, const char* text)`
- `[[nodiscard]] uint16_t canvasFind(const Canvas& c, const char* name)`
- `void canvasBindName(Canvas& c, const char* name, UiClickFn fn, void* user)`
- `[[nodiscard]] uint16_t canvasImportBin(Canvas& c, uint16_t parent, const char* path)`
- `void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user)`
- `void canvasReload(Canvas& c)`
- `uint16_t canvasNode(Canvas& c, uint16_t parent, const UiFlex& flex, const UiPaint& paint)`
- `[[nodiscard]] uint16_t canvasPage(const Canvas& c, int page)`
- `void canvasShow(Canvas& c, uint16_t id, bool visible)`
- `void canvasSetClick(Canvas& c, UiClickFn fn, void* user)`
- `void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user)`
- `void canvasBind(Canvas& c, uint16_t id, UiClickFn fn, void* user)`
- `void canvasFold(Canvas& c, uint16_t id)`
- `bool canvasListen(Canvas& c, UiValueFn fn, void* user)`
- `[[nodiscard]] bool canvasRange(const Canvas& c, uint16_t id, float& value, float& minV, float& maxV)`
- `void canvasSetHeight(Canvas& c, uint16_t id, float h)`
- `void canvasReparent(Canvas& c, uint16_t id, uint16_t parent)`
- `void canvasPlace(Canvas& c, uint16_t id, bool absolute, float x, float y, float w, float h)`
- `void canvasGrow(Canvas& c, uint16_t id, float extraW, float h)`
- `void canvasDock(Canvas& c, uint16_t panel, uint16_t home, uint8_t edges = 15)`
- `void canvasCommitDock(Canvas& c, uint8_t edge)`
- `void canvasSetDrop(Canvas& c, uint16_t id, uint8_t drop)`
- `void canvasOpenOs(Canvas& c)`
- `void canvasOfferFile(Canvas& c, const char* path)`
- `[[nodiscard]] bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path)`
- `[[nodiscard]] uint32_t canvasLoadJsonDoc(Canvas& c, const char* path)`
- `[[nodiscard]] bool canvasSaveJsonDoc(const Canvas& c, const char* path)`
- `[[nodiscard]] int canvasCursor(const Canvas& c)`
- `void canvasSetIcon(Canvas& c, uint16_t id, uint8_t icon)`
- `void canvasSelection(const Canvas& c, uint16_t& id, char* name, uint8_t cap, uint8_t& len)`
- `void canvasBindContext(Canvas& c, uint16_t owner, uint16_t menu)`
- `[[nodiscard]] uint16_t canvasMakeMenu(Canvas& c)`
- `[[nodiscard]] bool canvasAddCommandTo(Canvas& c, uint16_t menu, const char* name, UiCommandFn fn, void* user)`
- `void canvasText(Canvas& c, uint16_t id, const char* text)`
- `void canvasStamp(Canvas& c, uint16_t id, UiToken token)`
- `[[nodiscard]] bool canvasSetFont(Canvas& c, const char* path, float pixelSize)`
- `[[nodiscard]] bool canvasUseFont(Canvas& c, float pixelSize)`
- `void canvasSetPaint(Canvas& c, uint16_t id, float r, float g, float b, float a)`
- `void canvasBorder(Canvas& c, uint16_t id, float width, float r, float g, float b, float radius)`
- `void canvasShadow(Canvas& c, uint16_t id, float shadow)`
- `void canvasTransform(Canvas& c, uint16_t id, float angle, float scaleX, float scaleY)`
- `void canvasFilter(Canvas& c, uint16_t id, float bright, float contrast)`
- `void canvasTextStyle(Canvas& c, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading)`
- `[[nodiscard]] uint16_t canvasTextId(const Canvas& c, uint16_t id)`
- `[[nodiscard]] uint16_t canvasIcon(Canvas& c, uint16_t parent, uint8_t slot, float w, float h)`
- `void canvasPad(Canvas& c, uint16_t id, float left, float right, float top, float bottom)`
- `void canvasHover(Canvas& c, uint16_t id, float r, float g, float b)`
- `void canvasDisable(Canvas& c, uint16_t id, bool disabled)`
- `void canvasField(Canvas& c, uint16_t id, const char* text, uint8_t filter, bool clear)`
- `void canvasFieldExtra(Canvas& c, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password)`
- `void canvasCheck(Canvas& c, uint16_t id, uint8_t kind, uint8_t group, bool on)`
- `bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap)`
- `void canvasAnimate(Canvas& c, uint16_t id, float r, float g, float b, float a)`
- `void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc)`

### `core/ui/Font.hpp`
- `[[nodiscard]] bool fontLoadSdf( UiState& ui, Device& device, DescriptorHeaps& heaps, GpuImage& atlas, const char* path, float pixelSize)`

### `core/ui/Model.hpp`
- `inline UiLook uiLookWarm()`
- `void (*fill)(void* user, uint32_t index, char* dst, uint8_t cap) = nullptr`
- `void (*click)(void* user, uint32_t index) = nullptr`
- `inline int uiGlyphIndex(uint32_t cp)`
- `return static_cast<int>(cp - 32)`
- `} if (cp == 0x0401)`
- `} if (cp >= 0x0410 && cp <= 0x044F)`
- `return 96 + static_cast<int>(cp - 0x0410)`
- `} if (cp == 0x0451)`
- `} inline uint32_t uiReadUtf8(const char* s, uint8_t len, uint8_t at, uint8_t& step)`
- `} const unsigned char c0 = static_cast<unsigned char>(s[at])`
- `} if ((c0 & 0xE0) == 0xC0 && static_cast<uint8_t>(at + 1) < len)`
- `return (static_cast<uint32_t>(c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[at + 1]) & 0x3Fu)`
- `} if ((c0 & 0xF0) == 0xE0 && static_cast<uint8_t>(at + 2) < len)`
- `return (static_cast<uint32_t>(c0 & 0x0F) << 12) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[at + 2]) & 0x3Fu)`

### `core/ui/Panel.hpp`
- `static Panel make(Canvas& canvas, uint16_t parent, const UiFlex& flex, const UiPaint& paint)`
- `static Panel at(Canvas& canvas, uint16_t id)`
- `static Panel find(Canvas& canvas, const char* name)`
- `[[nodiscard]] Panel child(const UiFlex& flex, const UiPaint& paint) const`
- `[[nodiscard]] Panel label(const char* text) const`
- `[[nodiscard]] Panel textButton(const char* text, int8_t tag) const`
- `[[nodiscard]] Panel button(const char* text, int8_t tag, float r, float g, float b, uint8_t icon) const`
- `[[nodiscard]] Panel field(const char* text, uint8_t filter, bool clear) const`
- `[[nodiscard]] Panel check(const char* text) const`
- `[[nodiscard]] Panel radio(const char* text, uint8_t group) const`
- `[[nodiscard]] Panel spin(float value, float minV, float maxV, float step) const`
- `[[nodiscard]] Panel curve() const`
- `[[nodiscard]] Panel gradient() const`
- `void setText(const char* text) const`
- `void color(float r, float g, float b, float a) const`
- `void animate(float r, float g, float b, float a) const`
- `void transform(float angle, float scaleX, float scaleY) const`
- `void filter(float bright, float contrast) const`

### `core/ui/State.hpp`
- `UiState()`
- `void uiFocus(UiState& s, uint16_t id)`
- `void uiFocusStep(UiState& s, int dir)`
- `void uiTab(UiState& s, uint16_t id, uint16_t index)`
- `void uiCursorPush(UiState& s, uint8_t kind)`
- `void uiCursorPop(UiState& s)`
- `void uiSyncInput(UiState& s)`
- `void uiMarkLayout(UiState& s, uint16_t id)`
- `void uiShowOnly(UiState& s, uint16_t id)`
- `void uiDragBegin(UiState& s, uint16_t source, const char* kind, const char* bytes)`
- `void uiDragEnd(UiState& s, uint16_t target)`
- `void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user)`
- `void uiBindName(UiState& s, const char* name, UiClickFn fn, void* user)`
- `void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user)`
- `void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user)`
- `void uiFold(UiState& s, uint16_t id)`
- `bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user)`
- `bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user)`
- `void uiShow(UiState& s, uint16_t id, bool visible)`
- `void uiReparent(UiState& s, uint16_t id, uint16_t parent)`
- `void uiDrop(UiState& s, uint16_t id)`
- `void uiTrace(const UiState& s, const char* fn, uint16_t id)`
- `void uiDumpLayout(const UiState& s)`
- `void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h)`
- `void uiGrow(UiState& s, uint16_t id, float extraW, float h)`
- `void uiDockTo(UiState& s, uint8_t edge)`
- `void uiBuildShell(UiState& s)`
- `void uiBuildColor(UiState& s)`
- `[[nodiscard]] uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text)`
- `[[nodiscard]] uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path)`
- `[[nodiscard]] uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size)`
- `[[nodiscard]] uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path)`
- `[[nodiscard]] uint16_t uiFindName(const UiState& s, const char* name)`
- `void uiName(UiState& s, uint16_t id, const char* name)`
- `uint16_t uiWindowChrome(UiState& s, uint16_t parent)`
- `void uiBuildCalendar(UiState& s, uint16_t parent, float x, float y, float w, float h)`
- `[[nodiscard]] uint16_t uiVirtual(UiState& s, uint16_t parent, float rowH)`
- `void uiVirtualSource(UiState& s, uint16_t id, uint32_t count, void (*fill)(void*, uint32_t, char*, uint8_t), void (*click)(void*, uint32_t), void* user)`
- `void uiVirtualRefresh(UiState& s, uint16_t id)`
- `[[nodiscard]] uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step)`
- `[[nodiscard]] uint16_t uiCurve(UiState& s, uint16_t parent)`
- `[[nodiscard]] uint16_t uiGradient(UiState& s, uint16_t parent)`
- `[[nodiscard]] uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text)`
- `void jsonRelease(UiState& s)`
- `[[nodiscard]] uint32_t jsonLoadFile(UiState& s, const char* path)`
- `[[nodiscard]] bool jsonSaveFile(const UiState& s, const char* path)`
- `[[nodiscard]] flecs::entity jsonFind(flecs::entity parent, const char* key)`
- `[[nodiscard]] flecs::entity jsonAt(const UiState& s, flecs::entity parent, uint32_t index)`
- `[[nodiscard]] bool jsonString(const UiState& s, flecs::entity e, const char*& data, uint32_t& len)`
- `[[nodiscard]] bool jsonNumber(flecs::entity e, double& out)`
- `void uiClear(UiState& s)`
- `uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint)`
- `void uiText(UiState& s, uint16_t id, const char* text)`
- `void uiSetFieldText(UiState& s, uint16_t id, const char* text)`
- `void uiStamp(UiState& s, uint16_t id, UiToken token)`
- `void uiShowPage(UiState& s, int page)`
- `void uiToggleCompact(UiState& s)`
- `void uiClearField(UiState& s)`
- `[[nodiscard]] UiEvent uiApplyInput(UiState& s, const InputFrame& in)`
- `void uiLayout(UiState& s, float w, float h)`
- `void uiRestyle(UiState& s)`
- `void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a)`
- `[[nodiscard]] bool uiTickMotion(UiState& s)`
- `[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap)`
- `bool uiListen(UiState& s, UiValueFn fn, void* user)`
- `void fieldPlaceText(UiState& s)`
- `[[nodiscard]] TextRun textRun(const UiState& s, const UiText& text, const UiBox& slot)`
- `[[nodiscard]] uint32_t uiEmitRun( UiState& s, UiPrimitive* dst, uint32_t n, uint32_t cap, const TextRun& run, float r, float g, float b, float a, const UiBox* clip)`
- `void placeChromeText(UiState& s)`
- `void uiTextStyle(UiState& s, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading)`
- `[[nodiscard]] uint16_t uiTextId(const UiState& s, uint16_t id)`
- `[[nodiscard]] uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h)`
- `[[nodiscard]] uint16_t uiTip(UiState& s, uint16_t parent)`
- `void uiTipAt(UiState& s, uint16_t id, const char* text, float x, float y)`
- `[[nodiscard]] bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb)`
- `void dragScroll(UiState& s, uint16_t id, float y)`
- `void textInk(float r, float g, float b, bool hot, float& tr, float& tg, float& tb)`
- `[[nodiscard]] UiBox fieldInset(const UiState& s, uint16_t id, const UiField& field)`
- `int fieldRows(const UiState& s, const UiField& field, float viewW, FieldRow* rows, int cap)`
- `float fieldOffset(const UiState& s, const UiField& field, uint8_t from, uint8_t to)`
- `uint8_t fieldCaretAt2(const UiState& s, const UiField& field, float localX, float localY, float viewW)`
- `void uiFieldMulti(UiState& s, uint16_t id)`
- `inline float uiAdvance(const UiState& s, uint32_t cp)`
- `int gi = uiGlyphIndex(cp)`
- `gi = static_cast<int>('?' - 32)`
- `} inline float uiMeasure(const UiState& s, const char* text, uint8_t len, uint8_t until)`
- `while (i < end)`
- `const uint32_t cp = uiReadUtf8(text, len, i, step)`
- `} w += uiAdvance(s, cp)`
- `i = static_cast<uint8_t>(i + step)`
- `} inline float uiFieldWidth(const UiState& s, const UiField& field, uint8_t until)`
- `return uiMeasure(s, field.bytes, field.len, until)`
- `while (i < end)`
- `uiReadUtf8(field.bytes, field.len, i, step)`
- `i = static_cast<uint8_t>(i + step)`
- `} return static_cast<float>(count) * uiAdvance(s, static_cast<uint32_t>('*'))`
- `} inline void uiPadOf(const UiFlex& flex, float& left, float& right, float& top, float& bottom)`
- `} } inline UiBox uiContent(const UiState& s, uint16_t id)`
- `} const UiFlex* flex = s.ent[id].try_get<UiFlex>()`
- `uiPadOf(*flex, left, right, top, bottom)`
- `} if (box.h < 0.0f)`
- `} inline bool uiIsOverlay(const UiState& s, uint16_t id)`
- `for (uint16_t p = id; p != kUiNone; p = s.parentOf[p])`
- `const UiFlex* flex = s.ent[p].try_get<UiFlex>()`

### `core/ui/widget/Detail.hpp`
- `inline void copyText(char* dst, uint8_t& len, const char* src)`
- `while (src[n] != '\0' && n < 31)`
- `} inline UiFlex px(float w, float h)`
- `f.widthMode = static_cast<uint8_t>(UiSize::Px)`
- `f.heightMode = static_cast<uint8_t>(UiSize::Px)`
- `} inline UiPaint paint(UiRole role, float r, float g, float b, float radius = 0.0f, int16_t tag = -1)`
- `out.role = static_cast<uint8_t>(role)`
- `} inline bool hitBox(const UiBox& b, float x, float y)`
- `} inline bool focusable(uint8_t role)`
- `} uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint)`
- `void addText(UiState& s, uint16_t id, const char* text)`
- `void tokenRgb(const UiLook& look, UiToken token, float& r, float& g, float& b)`
- `void stamp(UiState& s, uint16_t id, UiToken token)`
- `uint16_t hitTest(const UiState& s, float x, float y)`
- `void setNote(UiState& s, const char* msg)`
- `int daysInMonth(int year, int month)`
- `int weekDay(int year, int month, int day)`
- `void calFill(UiState& s)`
- `void writeDate(UiState& s, int day)`
- `void calPick(UiState& s, uint16_t id)`
- `void calOnOpen(void* user, uint16_t id, int16_t tag)`
- `void calOnToday(void* user, uint16_t id, int16_t tag)`
- `void calOnPrev(void* user, uint16_t id, int16_t tag)`
- `void calOnNext(void* user, uint16_t id, int16_t tag)`
- `void calOnClose(void* user, uint16_t id, int16_t tag)`
- `void calOnDay(void* user, uint16_t id, int16_t tag)`
- `void colorOnOpen(void* user, uint16_t id, int16_t tag)`
- `void activate(UiState& s, uint16_t id)`
- `void hsvToRgb(float h, float s, float v, float& r, float& g, float& b)`
- `void rgbToHsv(float r, float g, float b, float& h, float& s, float& v)`
- `void setRange(UiState& s, uint16_t id, float value)`
- `int hexNib(char ch)`
- `void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a, bool withAlpha)`
- `void applyColor(UiState& s, bool fromHsv, bool publish = true)`
- `void takeBook(UiState& s)`
- `void paintColor(UiState& s)`
- `void dragWheel(UiState& s, uint16_t id, float x, float y)`
- `bool readHex(const UiField& field, float& r, float& g, float& b, float& a)`
- `void writeInt(char* dst, uint8_t& len, int v)`
- `bool readInt(const UiField& field, int& out)`
- `bool nodeShown(const UiState& s, uint16_t id)`
- `void samplePixel(const UiState& s, float x, float y, float& r, float& g, float& b, float& a)`
- `void dragSlider(UiState& s, uint16_t id, float x)`
- `void dragSpin(UiState& s, uint16_t id, float x)`
- `void colorTakeSlider(UiState& s, uint16_t id)`
- `void colorOnValue(void* user, uint16_t id, float)`
- `uint16_t splitTarget(const UiState& s, uint16_t split)`
- `void scrollBy(UiState& s, uint16_t id, float wheel)`
- `uint16_t scrollOwner(const UiState& s, uint16_t id)`
- `bool fieldAllows(uint8_t filter, uint32_t cp)`
- `bool emailOk(const UiField& field)`
- `bool urlOk(const UiField& field)`
- `bool telOk(const UiField& field)`
- `bool fieldProblem(const UiField& field, const char*& msg)`
- `void fieldMark(UiState& s, uint16_t id)`
- `uint8_t fieldChars(const UiField& field)`
- `void fieldSelectWord(UiField& field, uint8_t at)`
- `void fieldDropSel(UiField& field)`
- `void fieldInsert(UiField& field, const char* src, uint8_t n)`
- `void fieldRemember(UiState& s, uint16_t id, const UiField& field)`
- `bool fieldUndo(UiState& s)`
- `bool fieldRedo(UiState& s)`
- `void fieldEraseBack(UiField& field)`
- `void fieldEraseForward(UiField& field)`
- `void fieldReveal(const UiState& s, UiField& field, float view)`
- `uint8_t fieldCaretAt(const UiState& s, const UiField& field, float local)`
- `void fieldCopy(UiState& s, const UiField& field)`
- `void publishColor(UiState& s)`
- `void setBtnLabel(UiState& s, uint16_t id, const char* text)`
- `void refreshColorChrome(UiState& s)`
- `void placeColorMenu(UiState& s)`
- `void syncColor(UiState& s)`
- `void photoClamp(UiImage& image)`
- `bool photoMinimap(const UiBox& box, float x, float y, float& u, float& v)`
- `void photoZoom(UiState& s, uint16_t id, float wheel, float x, float y)`
- `void photoPan(UiState& s, uint16_t id, float x, float y)`
- `void tickPhoto(UiState& s)`
- `UiField* editFieldOf(UiState& s)`
- `void cmdFieldCut(void* user)`
- `void cmdFieldCopy(void* user)`
- `void cmdFieldPaste(void* user)`
- `void cmdFieldAll(void* user)`
- `void cmdFieldWipe(void* user)`
- `uint16_t listItemOf(const UiState& s, uint16_t id)`
- `void moveListItem(UiState& s, uint16_t item, uint16_t before, bool after)`
- `void fillColorBody(UiState& s, uint16_t parent)`

### `core/files/Browser.hpp`
- `void filesAttach(UiState& s)`
- `void filesMount(UiState& s)`
- `void filesClick(UiState& s, uint16_t id)`
- `void filesShutdown(UiState& s)`
- `void filesShow(UiState& s, bool on)`
- `void filesGo(UiState& s, const char* path)`
- `void filesStyle(UiState& s, const FileLook& look)`
- `void filesOnOpen(UiState& s, FileOpenFn fn, void* user)`
- `void filesRoot(UiState& s, const char* path)`

### `core/image/ImageFile.hpp`
- `void imageCpuFree(ImageCpu& img)`
- `[[nodiscard]] bool imageLoadFile(ImageCpu& img, const char* path)`
- `[[nodiscard]] bool gpuImageUploadRgba( GpuImage& img, Device& d, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t frames = 1)`
- `[[nodiscard]] bool gpuImageUploadHdr( GpuImage& img, Device& d, const float* rgb, uint32_t width, uint32_t height)`

### `core/host/Host.hpp`
- `[[nodiscard]] bool hostInit(Host& host)`
- `void hostShutdown(Host& host)`
- `[[nodiscard]] int hostOpen(Host& host, const ViewDesc& desc)`
- `void hostClose(Host& host, int slot)`
- `[[nodiscard]] bool hostSetFrame(Host& host, int slot, const FrameDesc& frame)`
- `[[nodiscard]] FrameDesc hostFrame(const Host& host, int slot)`
- `[[nodiscard]] bool hostTick(Host& host)`
- `[[nodiscard]] bool hostQuit(const Host& host)`
- `[[nodiscard]] Canvas* hostCanvas(Host& host, int slot)`
