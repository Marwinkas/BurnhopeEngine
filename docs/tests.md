# Тесты и лог кадра

Этот файл читают перед новым тестом, перед прогоном демо и когда элемент стоит не там или кнопка не срабатывает. Закон и список виджетов — `docs/now.md` и `docs/widgets.md`.

## Запуск

Из корня репозитория. Сборка и прогон идут вне песочницы: каталог `build-core/_deps/yoga-src/.vscode` ломает монтирование.

```
cmake --build build-core --target BurnhopeTest -j
./build-core/BurnhopeTest
```

Зелёный прогон заканчивается строкой `Status: SUCCESS!` и `0 failed`. Предупреждения Flecs `is_trivial` и неиспользуемые функции в `Files.cpp` сборку не роняют.

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
