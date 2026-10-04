# Тесты и лог кадра

Этот файл читают перед новым тестом, перед прогоном демо и когда элемент стоит не там или кнопка не срабатывает. Закон и список виджетов — `docs/now.md` и `docs/widgets.md`.

## Запуск

Из корня репозитория. Сборка и прогон идут вне песочницы: каталог `build-core/_deps/yoga-src/.vscode` ломает монтирование.

```
cmake --build build-core --target BurnhopeTest -j
./build-core/BurnhopeTest
```

Зелёный прогон заканчивается строкой `Status: SUCCESS!` и `0 failed`. `gpu profiler table names the slow pass` проверяет `FrameView` (2240), `FramePost` (512) и блоки GTAO/SSR по 64 байта, и текст таблицы зон. Предупреждения Flecs `is_trivial` и неиспользуемые функции в `Files.cpp` сборку не роняют.

Демо:

```
cmake --build build-core --target BurnhopeEngine -j
timeout 2 ./build-core/BurnhopeEngine
```

Код выхода `124` — это `timeout`, окно живое. В логе должны быть `ui: N flecs nodes, yoga + 1 draw` и `window opened main present mailbox`. Строк `vk:` быть не должно. Сразу после открытия идут `ui layout`, строки `ui box` / `ui miss` и `ui draw`.

Санитайзер, отдельное дерево, зависимости уже скачаны в `build-core/_deps`:

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBH_ASAN=ON \
  -DFETCHCONTENT_SOURCE_DIR_VOLK=build-core/_deps/volk-src \
  -DFETCHCONTENT_SOURCE_DIR_VMA=build-core/_deps/vma-src \
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=build-core/_deps/spdlog-src \
  -DFETCHCONTENT_SOURCE_DIR_FLECS=build-core/_deps/flecs-src \
  -DFETCHCONTENT_SOURCE_DIR_YOGA=build-core/_deps/yoga-src
cmake --build build-asan --target BurnhopeTest -j
./build-asan/BurnhopeTest
```

`BH_TSAN` — то же с другим каталогом. Обычный `build-core` санитайзер не включает.

## Замеры (Фаза 0 плана рендера)

Бенчмарк-тесты не падают по времени (машины разные) — они печатают число в лог канала doctest/spdlog, чтобы было с чем сравнивать до/после оптимизации. Текущие:

- `"uiLayout timing on a near-cap tree"` в `tests/Core.cpp` — строит ~900 кнопок почти до `kUiCap`, меряет `uiLayout` через `std::chrono::steady_clock`, печатает `bench uiLayout nodes=N ms=X.XXX`. Запускать так же, как обычный `BurnhopeTest`, искать строку `bench uiLayout` в выводе.
- `"uiEmit timing on a near-cap tree"` — то же дерево, меряет вторую половину кадра (`uiEmit`, сборка примитивов в SSBO), печатает `bench uiEmit nodes=N prims=M ms=X.XXX`. На этой машине: layout ≈ 8.5 ms, emit ≈ 1.3 ms на 510 узлов — emit на порядок дешевле layout, следующая оптимизация имеет смысл только в Yoga/layout, не в сборке примитивов.

## Реальный GPU в тестах (Фаза 2 и дальше)

`BurnhopeTest` может поднять настоящий `Device` на скрытом `Window` (`WindowConfig.hidden = true`), не только headless UI-логику — зависит от `bh_rhi`/`bh_image`, уже слинкованы. Тест обязан сам проверять `windowCreate`/`deviceCreate` и тихо выходить (`spdlog::warn` + `return`, без `CHECK`), если в окружении нет дисплея или GPU — так тест не валит сборку там, где его нельзя выполнить, но ловит регрессию там, где можно.

- `"gpuImageUploadRgba round-trips through host_image_copy"` — синтетическая 256×256 RGBA с переменной альфой (форсирует BC7, 7 мипов) через реальный путь `submitImageHostCopy`. Проверяет, что текстура создалась нужного размера. Ловит порчу механизма переноса (host image copy), не порчу самого сжатия BC.
- `"golden image: panel fill is exact and mesh/vertex paths match"` — offscreen-рендер (не свопчейн). Панель без радиуса даёт точный RGBA; mesh-shader путь и vertex-pull путь сравниваются пиксель-в-пиксель (`rgbaMiss`, допуск 0). Это первый настоящий golden-image тест в проекте — читает `docs/open.md`, если ищешь, можно ли так же проверить что-то ещё.
- Job system (`core/platform/Jobs.hpp`) и параллельная запись окон (`core/host/Host.cpp`, Фаза 1.5) инструментальным тестом не покрыты — проверка только через реальный запуск `BurnhopeEngine` с несколькими открытыми окнами (главное + Files) и грепом лога на `vk:`/`error`. Санитайзер потоков (`-DBH_TSAN=ON`) не прогонялся под многооконной нагрузкой — честно записано в `docs/open.md`.
- `"gpuImageUploadRgba: GPU BC1 decodes close to the source"` — синтетический градиент без альфы (форсирует BC1 → GPU-компьют путь). Декодирует BC1-блоки эталонным декодером внутри теста, сравнивает со источником: средняя ошибка канала должна быть < 20/255. Это golden-критерий лоссового кодека — не байт-в-байт с CPU ISPC (легальны разные конечные точки), а «результат близок к оригиналу».
- `"gpuImageUploadRgba: GPU BC7 mode 6 decodes close to the source"` — тот же градиент с переменной альфой (форсирует BC7). Каждый блок mip 0 обязан декодироваться как mode 6 (если это ISPC, режимы смешаны и `mode6 == blocks` падает). Средняя ошибка канала RGBA < 12/255.
- `"gpuImageUploadHdr: GPU BC6H mode 11 decodes close to the source"` — float-градиент 0..8 через `gpuImageUploadHdr`. Каждый блок mip 0 — mode 11 (`0b00011`). Средняя абсолютная ошибка канала < 0.05. Строка `hdr compress` — замер Фазы 2.
- `"gpuCompressLevel: BC3 BC4 BC5 decode close to the source"` — 32×32 RGBA через `gpuCompressLevel`. BC4 (R) и BC5 (RG) — средняя ошибка < 8/255, BC3 (цвет+альфа) < 12/255. У BC4 `e0 > e1`.
- `"photo blur packs one lod into the photo primitive"` — `canvasBlur` клампит 1.5 → 1, `uiEmit` даёт ровно один `kUiFlagPhoto`, blur float в `pad1`. `uiPhotoLod(0.5, 8) == 3.5`.
- `"camera cull feeds frustum cascades and froxels"` — сфера в кадре видна, сфера за камерой нет, 4 каскада, 16 froxel, конус от камеры отсекается.
- `"cullInstances keeps spheres inside the frustum"` — 12 инстансов, 8 перед камерой остаются индексами 0..7, 4 за камерой выпадают.
- `"anim track samples linear loop and cubic"` — ключи 0→100 за 1 с. t=0.5 даёт 50. t=1.5 в `Loop` тоже 50. Кубическая Безье на 0.5 около 50, на 0.25 ниже линейной. `Step` держит левый ключ.
- `"fixed step consumes remainder and clamps a spike"` — шаг 0.02 с и кадр 0.05 с дают два тика, остаток 0.01, `alpha` 0.5. Кадр в 1 с режется до `maxAccumulation` 0.1 и даёт пять тиков.
- `"style cascade keeps padding and replaces danger fields"` — `button` задаёт фон, padding 8 и radius 4. `danger` с большим приоритетом меняет фон и `border_width`. Padding и radius остаются. Yoga-узел получает padding 8.
- `"input queue tracks pointer keys pad pen touch and tray"` — два `MouseMove` дают позицию (110, 215) и дельту (10, 15). `Key_A` живёт после `inputBeginFrame`, край кадра гаснет. Стик и кнопка геймпада, перо 0.75 / наклон 0.2, касание и клик трея. 129-е событие в кольцо не входит.
- `"srgb linear hsv and packed rgba8"` — белый sRGB даёт линейную 1 и пакуется обратно. Красный канал лежит в младшем байте. HSV (0, 1, 1) даёт красный.
- `"document insert delete undo and redo"` — `Burn` + `hope`, срез хвоста, undo возвращает слово, ещё undo оставляет `Burn`, redo собирает слово снова.
- `"msdf line places glyphs for mesh draw"` — `Burnhope` на виде 200×100. `posMin.x` растёт в клипе и остаётся в диапазоне кадра. Push в `MeshDrawDesc` — 16 байт, групп столько, сколько глифов.
- `"mapped file reads the bytes that were written"` — временный файл, `fileMapReadOnly`, те же 8 байт, `fileUnmap` гасит указатель.
- `"material blob roundtrips through the asset header"` — `BHOP`, тип Material, `memcmp` туда и обратно. Порченый байт не проходит checksum.
- `"inspector drag writes the bound float"` — сдвиг мыши на 40 поднимает roughness с 0.5 до 0.7. Строка статистики содержит `60.0`.
- `"meshlet cone culls a sphere that faces away"` — полусфера смотрит в +Z. Глаз спереди видит, сзади нет. Hi-Z: ближняя сфера видна, дальняя за тайлом 0.4 скрыта.
- `"cluster grid keeps a point light in its sphere"` — лампа в (8.5, 4.5, 12.5) радиусом 0.25 лежит только в ячейке (8, 4, 12).
- `"meshlet blob maps without a copy"` — `meshletView` указывает внутрь `mmap`, радиус 2.
- `"frustum keeps the sphere in front of the camera"` — сфера в нуле проходит, сфера на z=20 нет.
- `"visbuffer resolve reads the material by instance id"` — пиксель инстанса 0 и примитива 5 достаёт материал 2.
- `"cube meshlet stays inside the vertex and triangle caps"` — куб 8 вершин и 12 треугольников. Пол: 4 вершины, индексы `0,1,2` и `2,3,0`, `y = 0`. Кубы стоят выше пола. Три лампы сцены лежат в разных ячейках. Тень солнца 2048.
- `"look straight down keeps a finite view"` — глаз над целью, правый вектор конечный.
- `"meshlet partition stays inside the caps and the bhop header matches"` — 500 треугольников, каждый мешлет ≤ 64/124, конус отсекает взгляд снизу, `BistroScene` читается обратно.
- `"a sector stays visible until a portal is marked and leaves the frustum"` — сектор без порталов виден. Проём за плоскостью прячет зал, проём внутри плоскости оставляет.
- Интервалы `ssrcInterval` стыкуются: каскад 0 это `[0, base]`, каждый следующий начинается на конце предыдущего. `screenBary` центра треугольника даёт три равных веса, ребро даёт нулевой третий вес, схлопнутая проекция ставит `flat`. `ssrcThickness(0.1)` равен 0.004 и меньше старых 0.012, на 100 м равен 0.0004.
- `"stream ring wraps after the first chunks are released"` — четыре чанка, освобождение первых двух, следующий берётся с нуля.
- `"stream queue picks the nearest asset"` — из трёх запросов выбирается меньший priority.
- `"stream commit marks the mapped blob resident"` — байты из mmap попадают в стейджинг, слот становится Resident.
- `"fly camera moves forward and yaws with the mouse"` — W уменьшает z, сдвиг мыши растит yaw и pitch, пробел поднимает y и не двигает z, один кадр крутит yaw не больше чем на 240·0.004.
- `"point light shadow face and atlas index"` — точечная лампа получает срез 0, направленная остаётся −1, глубина на радиусе равна 1.
- `"wasm add and apply through linear memory"` — встроенный модуль. `add(15, 27) == 42`. Число пишется в страницу через `wasmGetMemory`, `apply` прибавляет `env.tick` и кладёт результат обратно. Адрес 70000 — `Trap`, страница не портится.
- `"device lost flag stops draws until cleared"` — `deviceMarkLost` ставит флаг, повторный вызов не сбрасывает его.
- `BurnhopeHostTick` — сначала два потока `jobsRunAndWait` (общий счётчик под мьютексом, ждёт 8000). Потом два скрытых окна. Под `-DBH_TSAN=ON` пул проходит без гонки; дальше процесс падает SIGSEGV в ThreadSanitizer из драйвера NVIDIA на `vkCreateDevice`.

## Как писать тест

Файл — `tests/Core.cpp`. Doctest собран с `DOCTEST_CONFIG_NO_EXCEPTIONS`. `REQUIRE` обрывает процесс. Пишут `CHECK`. Если дальше по нулевому указателю идти нельзя, после неуспешного `CHECK` ставят `return`.

Тест не линкует `demo/`. В `Core.cpp` уже есть пустой `uiBuildShell`: `Canvas.cpp` его зовёт. Второй раз не объявлять.

Проверяют данные, не картинку:

- Бокс после `uiLayout`: `state.box[id].x/y/w/h`. Корень Yoga растягивается на размер `uiLayout`. Размер из JSON у корня не проверяют. Фиксированный размер проверяют у ребёнка.
- Хит: `hitTest`. Точка на правом краю (`x + w`) уже снаружи. Точка `x + w - 1` внутри. Скролл, который сам является корнем, растягивается на весь кадр и ножницы не проверяет. Скролл сажают в панель.
- Слайдер: `UiRange` после `uiApplyInput` с `pressed` и `down` = `kPointerLeft`. Второе событие без `pressed`, с тем же `down`, двигает захват.
- Блоб: `uiImportBin` на битом буфере возвращает `kUiNone` и не растит `count`. Живой блоб ищут по имени `uiFindName`, не по номеру сущности.
- Проводник: `filesAttach`, `filesMount`, `filesShow`, `filesRoot` во временный каталог, клик в бокс кнопки `NEW`, потом каталог на диске. В конце `filesShutdown` и удаление каталога. Цель `BurnhopeTest` линкует `bh_files`.
- Удаление: `uiDrop`, затем `yoga == nullptr`, `is_alive() == false`, родитель Yoga без этого ребёнка, следующий `uiLayout` не падает. Слоты не уплотняются. Мёртвую сущность не передают в `try_get`.

`uiApplyInput` помечен `nodiscard`. Результат пишут в переменную или снимают `(void)`.

## Лог, который кидают вместо рассказа

Канал spdlog `UI`, та же консоль. Префиксы стабильные, их ищут по тексту:

| Строка | Когда |
| --- | --- |
| `ui fn=press` | Левый клик дошёл до виджета. Дальше имя, роль, тег, родитель, бокс. |
| `ui fn=press-eaten` | Клик забрал проводник (`filesHandle` вернул true). Виджетный `activate` не вызывается. |
| `ui fn=activate` | Кнопка, галочка или поле вызвали действие. |
| `ui fn=slider 0.420` | Ползунок записал число. В строке ещё его бокс. |
| `ui fn=show` / `hide` | `uiShow`. |
| `ui fn=drop` / `reparent` / `place` | Снятие, смена родителя, новая позиция. У `place` в имени функции запрошенные x, y, w, h. |
| `ui fn=folder` / `folder-fail` | Проводник создал каталог или не смог. |
| `ui fn=scroll` | Колесо сдвинуло прокрутку. Высота бокса скролла после этого не должна стать нулём. |
| `ui layout` | Сколько узлов, размер Yoga в точках, dpi. |
| `ui box` | Имя, роль, родитель, бокс. Пишутся именованные узлы и кнопки, поля, слайдеры, галочки, крестики, скроллы. Родитель `65535` — корень. |
| `ui miss` | Тот же узел, но ширина или высота меньше 1, NaN, или бокс целиком за кадром. |
| `ui draw N` | Сколько примитивов ушло в единственный draw. |

Дамп пишется сам на первом кадре (`UiState.dumpLayout` стартует единицей). F3 (`kScanF3`, сканкод 60) ставит флаг снова, следующий кадр повторяет дамп.

Как читать поломку:

- Кнопка не нажимается, в логе `press-eaten` и бокс кнопки внутри вида файлов. Клик съела геометрия проводника. Зоны должны совпасть с `ui box` этой кнопки.
- Кнопка не нажимается, в логе `press` на другом id. Сверху другой узел. Смотрят `parent` и боксы обоих.
- В логе нет `press`. Хит пустой: курсор в точках не попал в бокс. Сверяют `ui box` и `ui layout` (dpi и laid).
- Слайдер визуально на месте, но нет `ui fn=slider`. Захват не дошёл до `dragSlider`. Ищут `press` на этом id.
- `ui miss` у видимой кнопки. Yoga дала пустой бокс или унесла узел за кадр. Это баг разметки, не клика.
- `ui draw 0` при живых `ui box`. Примитивы не собрались, смотрят `visualDirty` и шейдер, не координаты.

Пока этих строк нет, положение элемента не выдумывают.
