# Открыто

## Дальше (план, не трогать до своего черёда)

Порядок — что открывает следующую работу, не по сложности:

1. ~~**GPU BC6H**~~ — сделано: `csBc6h`, mode 11, golden в `tests/Core.cpp`. CPU ISPC остаётся запасным путём и для BC3, когда BC7 нет.
2. **-DBH_TSAN=ON прогон** — пул задач прогнан: `BurnhopeHostTick` два потока, общий `int` под мьютексом, `shared=8000`. Два окна до параллельного кадра не доходят: ThreadSanitizer падает SIGSEGV внутри своего `calloc` из `libnvidia-glcore` на `vkCreateDevice`. Это не гонка `Host.cpp`. Подавления `tools/tsan.supp` драйвер не спасают. Повтор, когда появится инструментация драйвера или CPU-рендер без NVIDIA.
3. **Параллелизация внутри одного окна** (разбить `uiLayout` одного дерева виджетов по потокам) — не начато, Фаза 1.5 распараллелила только между окнами. Нужно только если один экран с одним огромным деревом становится бутылочным горлом (пока не измерено на реальном контенте).
4. **MSDF/MTSDF атлас** — сейчас SDF FreeType на весь атлас при смене размера, нет ленивого глифа. Генератора MSDF в системе нет (надо ставить).
5. ~~CSS-подобные функции (transform/filter в `UiPrimitive`, План Фаза 4)~~ — сделано: `UiPaint.angle/scaleX/scaleY/bright/contrast`, упаковка в `pad0`/`pad1` через `uiPackTransform`/`uiUnpackTransform` (`Model.hpp`), `kUiFlagTransform` в `ui.slang` (один draw, обе ветки `uiVs`/`uiMs`), `Panel::transform`/`Panel::filter`.
5a. ~~**Blur для `kUiFlagPhoto`**~~ — сделано: `UiPaint.blur` (0..1) пишется float в `pad1` фото-примитива, `sampleLevel` в `ui.slang` делает `SampleLevel` с lod = базовый mip + `uiPhotoLod`. Второй проход не добавлялся.
6. ~~**CMAKE_CXX_STANDARD → 26**~~ — сделано: `CMAKE_CXX_STANDARD 26` и `-std=c++26`.
7. ~~**WASM-хост в процессе**~~ — сделано: `core/wasm`, страница из арены, `wasmInvoke` ловит trap и не роняет процесс. Отдельный процесс на модуль по-прежнему не сделан.
8. ~~**Фаза 7: треки и фиксированный шаг**~~ — сделано: `core/anim/Animation.hpp` и `core/time/Time.hpp`. `UiAnim` остаётся коротким цветовым переходом. `timeAdvance` копит шаг и режет скачок через `maxAccumulation`.
9. ~~**Фаза 8: таблица стилей**~~ — сделано: `core/ui/Style.hpp`. Класс — хеш, каскад пишет в `UiStyle`, padding/margin/width/height идут в Yoga. Разбор CSS-текста в кадре не делался.
10. ~~**Фаза 9: ввод**~~ — сделано: `core/input/Input.hpp`. Кольцо событий, клавиатура, мышь, геймпад, тач, перо, трей. `InputFrame` остаётся снимком SDL и в это кольцо сам не пишется.
11. ~~**Фаза 10: документ, цвет, текст**~~ — сделано: `core/color`, `core/doc`, `core/text`. HSV живёт в ядре, пикер его вызывает. Раскладка пишет клип, `glyphMs` строит квад. Генерация MSDF-атласа и shaping/accessibility в этот срез не входили.
12. ~~**Фаза 10.5: mmap, материал, инспектор**~~ — сделано: `core/io`, `core/gfx/Material.hpp`, `core/ui/PropertyInspector.hpp`, `core/debug/DebugStats.hpp`. Окно FILES с ячейками больше не рисует второй фон сетки.
13. ~~**Фаза 11: мешлеты, кластеры, Hi-Z**~~ — сделано: `Meshlet`, сетка света `16×9×24`, `hizMipForSphere` / `hizSphereVisible`. Три LOD, Forward+ и Lightmass в этот срез не входили.
14. ~~**Сквозной visbuffer**~~ — сделано: пак 20/12, `visResolve`, `sunShadowBuild`, `resolve.slang` с PCF 3×3, `testSceneBuild`. Каскады теней в этот срез не входили.
15. ~~**Фаза 12: стриминг**~~ — сделано: `StreamStagingRing`, `StreamQueue`, `ResidencyTable`. NVMe-очередь ядра и отдельный transfer-поток в этот срез не входили.
16. ~~**Окно сцены**~~ — `./build-core/BurnhopeEngine`. Мешлет 64/124, fly-камера, пол и кубы, солнце и три цветные лампы. Кубический атлас теней точечных ламп на GPU в этот срез не входил.

Правило: каждый пункт закрывается с тестом (golden-image/decode-verify для рендера, `BurnhopeTest` CHECK для логики) в той же правке, не отдельно потом.


Строка живёт, пока кусок не работает в коде. Сделанное отсюда вычёркивается.
Повтор одного и того же требования — одна строка.

Ножницы батча, трафарет, кольцо на 16 МБ, Dual Kawase и перенос сетки файлов на `uiVirtual` сюда не возвращать: это второй проход или вторая вёрстка. Запись — `docs/mistakes.md`.

## Поставить

Уже стоят и подключены: `vulkan-headers`, `vulkan-validation-layers`, `vulkan-tools`, `sdl3`, `freetype2`, `python`, `shader-slang`, `ispc`, `clang`, `gcc`, `doctest`, `cpptrace`, `backward-cpp`, `renderdoc`. Повторно ставить не нужно.

Tracy в репозиториях CachyOS нет. `cmake -DBH_TRACY=ON` забирает её через FetchContent с `https://github.com/wolfpld/tracy`. Без флага зоны пустые.

Санитайзеры: `-DBH_ASAN=ON` или `-DBH_TSAN=ON`. Обычный debug их не включает.

## Ещё нет

- MSDF / MTSDF атлас 2048 и ленивый глиф. Сейчас SDF FreeType на весь атлас при смене размера. Генератора MSDF в системе нет.
- Offscreen-рендер UI сделан (`tests/Core.cpp`, golden-image тесты) — устройству нужен только `Device`, не поверхность; раньше здесь была ошибочная запись, что это невозможно без surface. Сравнение с эталоном в виде PNG на диске не сделано (эталон пока только аналитический — точный цвет заливки и mesh-vs-vertex), но сама offscreen-труба (рендер в свою картинку без свопчейна) теперь есть.
- Остальные режимы BC7 (не mode 6) и signed BC6H на CPU ISPC. GPU закрывает ровно те режимы, которые Фаза 2 просила без перебора партиций: BC1, BC7 mode 6, BC6H mode 11.
- Изоляция скрипта внутри процесса сделана в `core/wasm`: trap возвращает `WasmStatus::Trap`. Отдельный процесс на модуль и полный WASM (таблицы, float, bulk-memory) ещё нет. `BH_ASSERT`/`bhFail` по-прежнему останавливает debug-сборку на баге ядра.
- Параллельная запись командных буферов по окнам (план, Фаза 1.5, «job system») — сделано: `core/platform/Jobs.hpp` (fork-join пул) + `core/host/Host.cpp` (3 фазы, параллельная фаза 2 на `jobsRunAndWait`) + `Device::queueMutex` (общая очередь и upload-пул). Проверено на реальном GPU с двумя окнами одновременно, 0 ошибок валидации. `-DBH_TSAN=ON`: пул задач чистый (`shared=8000`, гонки нет). Многооконный кадр под TSAN не исполняется — санитайзер падает в `libnvidia-glcore` на `vkCreateDevice` (пункт 2). Параллелизация внутри одного окна (разбить `uiLayout` одного дерева на потоки) не сделана — распараллелено только между окнами.
