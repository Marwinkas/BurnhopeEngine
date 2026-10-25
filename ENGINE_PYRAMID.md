# Пирамида `engine` и `demo`

Просмотрены все 19 файлов `demo` и все 149 файлов `engine` (заголовки, исходники, шейдеры). Код не менялся. Ниже только нарушения философии: один файл делает одну работу, сосед не лезет в чужие поля, нижний кирпич ничего не знает о верхнем, верхний только собирает и настраивает нижние.

Целевая стопка для этих двух папок:

1. **Кадр GPU.** Один проход — один модуль. Видимость, свет, тень, GI, тонмап, погода, дождь. Читает уже лежащий блок чисел. Не знает кнопку, секцию, меню и имя ползунка.
2. **Сцена.** Актёр это сущность и POD. Гисмо, камера, физика, звук, загрузка `.bhop` — отдельные кирпичи. Кадр их вызывает, не хранит их жизнь у себя в полях.
3. **Блок данных.** Один POD на окно: пост, твики, флаги, поза актёра. Поля лежат массивом с постоянным смещением. Кадр копирует блок целиком.
4. **Привязка.** Таблица `{узел, смещение, вид, min, max}`. Её заполняет загрузчик раскладки один раз. Кадр обходит таблицу циклом.
5. **Раскладка.** JSON (или уже существующий bin). Панели, строки, меню, складки, видимость секций. Демо этот файл только называет.
6. **Демо.** Открыть хост, указать файл раскладки, указать блок данных, повесить имена команд (`Save Scene`, `Spawn Cube`) на функции. Ноль `uiPanel`, ноль `UiFlex`, ноль `try_get_mut<UiPaint>`.

Сейчас стопка лежит плашмя. `demo` сам строит редактор из виджетов и пишет им flex и краску. `SceneFrame` одновременно грузит текстуры, печёт меши, крутит физику, пишет строку статистики и копирует каждый ползунок по имени. Публичный заголовок рендера называется `HudPost` и `kSecMaterial`.

`core/ui` в этот файл не входит: его разбор уже лежит в `UI_PYRAMID.md`.

---

## Сквозные поломки

**Демо верстает.** Конституция: слой 3 только настраивает. Сейчас раскладка редактора написана на C++ в восьми файлах: `HudToolbar.cpp`, `HudView.cpp`, `HudStrip.cpp`, `HudInspector.cpp`, `HudPost.cpp`, `HudMaterial.cpp`, `Shell.cpp`, `Sample.cpp`. Каждый заводит `UiFlex`, красит `UiPaint`, ставит высоту, вешает меню. Повтор одного и того же «спрятанная краска / вуаль / полоска на 1 px» сидит в `HudToolbar`, `HudView`, `HudStrip`, `HudPost`.

**Демо лезет в поля виджета.** `canvas.state->ent[id].try_get_mut<UiPaint>()`, то же для `UiFlex`, `UiText`, `UiField`. Прямая посадка `UiMenuSlide`, `UiTitleBar`, `UiResizable`. Обход `for (id = 0; id < s.count)`. Это внутренности слоя 1 и 2. Снаружи остаются `uiButton`, `uiShow`, `uiBind`, `uiCommandAdd`.

**Один мешок на всё окно.** `HudContext` в `Engine.hpp` (285 строк заголовка) держит сцену, хост, книгу цвета, захват мыши, позу актёра, пост, твики, флаги, пути проекта, документ материала, списки кнопок и шесть `std::deque` ударов по кнопкам. Рядом `ShellDemo` делает второе окно-пример. Два приложения живут в одном `Engine`.

**Данные продублированы по имени.** Один и тот же набор ползунков объявлен трижды:

- `HudPost` и `SceneTweaks` в `render/SceneFrame.hpp` (рендер знает имена инспектора);
- те же поля ещё раз на `SceneFrameImpl` (`gtaoRadius` … `rainRipple`, около 60 штук);
- те же указатели ещё раз в `UiPropBinder` (`float* dst` на каждое поле).

Кадр в `sceneFrameTweaks` присваивает каждое поле отдельной строкой (примерно строки 2075–2193 `SceneFrame.cpp`). Демо в `pullProp` и `hudPushFrame` пишет каждое поле отдельной строкой. Сдвинуть ползунок в JSON нельзя: его нет в данных, он есть в исходнике.

**Потолки.** Конституция запрещает фиксированные карманы.

| Место | Потолок |
| :--- | :--- |
| `SceneFrameImpl` | `kSceneVerts = 512`, `kSceneTris = 256`, `kSceneInstances = 48`, `kPropCap = 16`, `sectors[32]`, `portals[64]`, `texJob[8]`, `fallbackMat[4]`, `cpuVerts[]` / `cpuIndex[]` / `cpuInstances[]` |
| `HudContext` | ~~`toolBtn[5]`, `toolIcon[5]`,~~ `snapBtn[3]`, `menuBtn[8]`, ~~`stat[40]`,~~ `matTex[4]`, `matHit[4]`, `liveSlot[7]`, `liveHit[7]`, ~~`matName[8][48]`, `matFile[8][260]`,~~ `propLast[15]` |
| `HudIds` | ~~`stat[40]`~~ |
| `UiPropBinder` | `kFloats = 384`, `kBools = 96`, `kSwatches = 8` |
| `SceneActors::count` | `uint8_t`, молчаливый потолок 255 |

После потолка кнопка, проп или строка статистики просто не рождается.

~~**Удар по кнопке — отдельная структура на каждую кнопку.** `ModeHit`, `GizmoHit`, `SnapHit`, `SpawnHit`, `FileAct`, `HudPick`, `HudSwatch` лежат в `std::deque`, потому что колбэк держит сырой `void*` и боится реаллокации.~~ ~~`ObjectHit` ушёл вместе со списком актёров.~~ `MatHit` ещё держит кнопки текстур.

~~**Маска проходов собирается лестницей `if`.** `hudPushFrame` в `HudPush.cpp` выставляет биты `passMask` по одному (`cluster`, `probes`, `giMode == 3`, `aa == 3`, …). Числа `262144`, `4194304`, `1048576` зашиты в демо. Бит прохода — данные рендера. Демо должно положить номер режима в блок, рендер сам включает проход.~~

**Заголовок рендера включает окно.** `SceneFrame.hpp` тянет `platform/Window.hpp`, `rhi/Swapchain.hpp`, `rhi/DescriptorHeap.hpp`, `scene/FlyCam.hpp`, декали, материалы, порталы. Любой, кому нужна поза актёра, видит свопчейн и окно. Публичный контракт сцены — актёры, блок чисел, запись кадра. Окно остаётся у хоста.

**`SceneFrameImpl.hpp` видит каждый проход.** Один заголовок включает cull, shade, sky, radiance, sharc, fsr, gtao, restir, voxel, gizmo, particles, fog, rain, tonemap, visbuffer, test scene, fly cam. Каждый `Pass*.cpp`, который включает этот заголовок, видит чужие проходы. Тонкий файл перестаёт быть тонким.

**Физика, погода и звук сидят в рендере.** `render/Physics.*` — это Jolt. `render/Weather.*` включает `<flecs.h>` и хранит `sliderWet`, `sliderZenith`, `sliderTime`: компонент климата знает ползунок. `SceneFrame.cpp` включает `audio/Audio.hpp`. Кадр GPU не владеет симуляцией.

~~**Две раскладки одного редактора.** `Sample.cpp` грузит bin (`BH_UI_SHELL`, `BH_UI_SAMPLE`) и потом ещё дописывает узлы руками. `sceneHud` в `HudToolbar.cpp` строит второй редактор с нуля, без этого bin. Оба пути живут в `Engine`. Пример виджетов и рабочее окно сцены — разные приложения, они склеены одним `engineTick`.~~

---

## `demo`

Слой 3 по конституции короче 350 строк и не пишет поля `UiState`. Ниже — кто этот предел и эту границу переходит. `Main.cpp` свою работу держит.

### `app/Main.cpp` (29)

Работа: создать `Engine`, крутить `engineTick`, выйти. Свою работу держит. Флаг `--import` тоже уместен здесь.

### `app/Engine.hpp` (285)

Работа: окно, сцена, холст, идентификаторы, которые кадр читает.

Сейчас это словарь редактора. `HudContext` перечисляет каждый узел тулбара, камеры, статистики, материала и поста. `HudFlags` дублирует режимы, которые уже есть в `SceneTweaks` (`tone`, `aa`, `giMode`, `directMode`, `reflectMode`, `aoMode`, `sun`, `sunCast`). `HudActor` дублирует `ActorPose` и `ActorPbr`. `StatsHit`, `ModeHit`, `ToolHit`, `GizmoHit`, `SnapHit`, `SpawnHit`, `ObjectHit`, `FileAct`, `HudPick`, `HudSwatch`, `MatHit` — типы колбэков, им место не в заголовке движка окна.

Заголовок включает `ui/widget/ColorPicker.hpp`. Окно не знает устройство пикера.

### `app/Engine.cpp` (366)

Работа: открыть виды хоста, отдать ввод сцене, отдать команду записи.

Смешано четыре работы:

- диалоги и виджеты: включает `Button`, `Field`, `PropGrid`, `Splitter`, `Label`, `Chrome`, `Menu`, `Scroll`, `Slider`, `TreeView`, `ColorPicker`, `Panel`, `Text`, `UiPhoto`;
- drop файла решает сразу за файлы, за фото, за JSON и за импорт модели (`shellDrop`);
- открытие окон `tool` и `color` зашито размерами 560×420 и 440×720;
- `sceneTick` сам маскирует ввод, держит look-grab и считает present. Маска ввода — `sceneMaskInput` в хосте. Захват взгляда — состояние камеры на сцене, не на HUD.

`engineTick` после этого ещё тянет синхронизацию инспектора. Кадр окна не должен знать, какой ползунок сейчас крутят.

### `app/HudBuild.hpp` (109)

Общий заголовок на все файлы HUD. Объявляет и верстку (`hudBuildInspector`, `hudBuildPost`, `hudBuildView`, `hudBuildStrip`), и материалы, и погоду, и проект, и иконки. Любой cpp HUD видит все остальные. Это скрытый диспетчер, тот же приём, что `widget/Widgets.cpp` для UI.

`UiPropCat` объявлен здесь второй раз относительно страницы свойств. Категория инспектора живёт в `core/host`, не в демо.

`kViews[]` — таблица режимов отладки. Это данные. Им место в JSON меню, не в заголовке.

### `app/HudToolbar.cpp` (153)

`sceneHud` собирает корень, полосу меню File/Edit/View, колонку вьюпорта и тулбар. Высоты 32, 26, 36, отступы 8 и 6, альфа дока `0.94` написаны здесь. Меню File зашито строками `"New Project..."`, `"Open Project..."`, `"Save Scene"`, `"Load Scene"`, `"Exit"`.

Иконки инструментов грузятся по именам файлов `"SelectMode.png"` и соседним. Разрешение файла — кирпич ассета. Демо называет слот и имя, не открывает файл само (`loadToolIcon` живёт в `HudPick.cpp`).

### `app/HudStrip.cpp` (299)

Работа, которая должна быть JSON: левая и правая группы тулбара, разделители, меню спавна из семи имён, четыре кнопки гизмо, снапы, play/pause.

Пишет чужое:

- `paint.a = 0.72` и `0.55` поверх токена;
- `canvas.state->ent[menu].add<UiMenuSlide>()`;
- на каждую кнопку спавна кладёт `SpawnHit` в `deque`;
- ~~`toolBtn[i]`, `toolIcon[i]`,~~ `snapBtn[3]` — потолки.

### `app/HudView.cpp` (350)

Собирает вьюпорт, сплиттеры, скролл инспектора, панель камеры 400×500, попап статистики.

Пишет чужое:

- у радиокнопок камеры затирает `paint->a = 0`;
- у числа камеры пишет `UiFlex::width = 72`, `grow`, `shrink`;
- у слайдера FOV пишет ширину flex;
- сажает `UiMenuSlide` на панель камеры;
- локальные `labeledNumber` и `rule` — это строка свойств, она уже есть в `uiPropVec3` / `bind.slide`. Вторая вёрстка строки.

Режимы вида и кнопки камеры должны быть пунктами JSON и одним массивом привязок, не полями `camProj[2]`, `camAxis[6]`, `camFov`, `camNear`, `camFar`, `camSpeed`.

### `app/HudInspector.cpp` (220)

`hudBuildInspector` создаёт секции Transform и Material полем за полем: `Location`, `Rotation`, `Scale`, `Pivot` — четыре вызова `vec(...)`. Дальше таблица из 20 `UiSlideSpec` на файл материала, четыре кнопки текстур, сорок пустых `uiLabel` под статистику.

`hudDrop` на каждый пункт меню делает `picks.push_back` и `uiCommandAddTo`. Пункт меню — `{подпись, значение, смещение в блоке}`. Сейчас это `HudPick` с указателем на весь `HudContext`.

`onHudSwatch` лезет в `UiColorPickerComponent` через `uiColor(s)` и пишет `book->r/g/b`. Образец цвета — виджет. Демо передаёт три float из блока.

~~`hudShowBodies` показывает девять тел по полям `HudFlags` (`directMode == 1`, `giMode == 3`, `aa == 3`, …). Видимость секции — правило в JSON: «покажи `diBody`, когда байт режима равен 1».~~

### `app/HudPost.cpp` (318)

Самый длинный список вёрстки. Секции GI, Direct, Indirect, Reflection, AO, FSR, bloom, fog, sun, weather набираются вызовами `slide`, `tick`, `hudDrop`, `uiFoldBody`, `uiPropSection`. Каждый ползунок — строка C++ с подписью, указателем, min и max.

Это и есть файл, который должен исчезнуть как код и остаться JSON. Логика «если режим Restir, показать тело» остаётся правилом видимости в том же файле, не функцией `group` + `uiShow`.

Сырые пороги (`kOpen = 3.4028234e+38`) и категории `UiPropCat::*` демо собирать не должно: страница свойств уже умеет секцию и число.

### `app/HudMaterial.cpp` (297)

Собирает слоты материала актёра и одновременно ведёт документ: `adopt`, `blankDoc`, `storeLive`, `onNewMat`, `onDupMat`, `onSaveMat`, `onResetUv`, drop пути, browse.

Две работы в одном файле. Раскладка слотов — JSON (`asset slot` уже есть в UI). Запись `.bhmat` — кирпич документа в `core/asset` или `core/doc`. Демо только называет команду Save.

`liveSlot[7]` — потолок слотов текстур. ~~`matName[8][48]`, `matFile[8][260]` — потолки списка материалов.~~

### `app/HudMat.cpp` (294)

Вторая половина материала: открыть файл, сравнить `MaterialInfo`, сохранить, синхронизировать с актором, drop в окно, бейдж перетаскивания. Дублирует `HudMaterial.cpp`. Один документ материала не должен иметь два фасада в демо.

`hudMaterialSync` и `hudPushLive` копируют поля документа по имени. Это тот же массив привязок, что у ползунков.

### `app/HudSync.cpp` (266)

Работа кадра: прочитать виджеты в блок и записать блок в сцену.

Сейчас `pullProp` вызывает `bind.write` пятнадцать раз по адресу поля (`&hud.actor.color[0]`, `pos+0`, …) и ещё заполняет `propLast[15]` и `propPivot[3]` теми же числами. `syncProp` собирает `float cur[18]` вручную и сравнивает с прошлым кадром по индексу. Индекс и поле живут в голове автора, не в таблице.

`setSlide` / `slideAt` / `checkOn` — третий способ читать виджет, рядом с `UiPropBinder`. Один обход таблицы.

### `app/HudPush.cpp` (337)

`hudPushFrame` — ручной компилятор инспектора в `SceneTweaks`:

- из `aa`, `lumScale`, `gamut` собирает `gradeFlags` суммой 1, 2, 4, 8;
- ~~из десятков флагов собирает `passMask` побитово;~~
- пишет зенит и азимут то в погоду, то в эйлер актёра Weather, с порогом `0.25f`;
- ~~режет `sceneFrameGpuText` на `char lines[40][96]` и раскладывает по `ids.stat[i]`.~~

~~Статистика — один текстовый узел или список строк из пула, не 40 заранее созданных лейблов.~~ ~~Маска проходов считается в рендере из байта режима. Демо кладёт байт.~~

`syncViewport` в том же файле двигает панель вьюпорта. Геометрия панели — раскладка, не тик.

### `app/HudAct.cpp` (273)

Колбэки тулбара: выбор актёра, гизмо, snap, pivot, play, pause, камера, вид, дубликат, спавн. Это законный слой команд, но каждая команда достаёт `Canvas`, лезет в узлы и зовёт `sceneFrame*` с вынутым из `HudContext` индексом.

Команда «спавн куба» принимает вид спавна из пункта меню (число в блоке или аргумент команды) и зовёт `sceneFrameSpawn`. Она не хранит `SpawnHit` на кнопку.

~~`addActorButton` сам делает кнопку объекта. Список актёров — виджет списка поверх массива имён, который сцена уже отдаёт через `sceneFrameActorName`.~~

### `app/HudPick.cpp` (138)

Пресеты погоды (`wxClear`, `wxOvercast`, `wxRain`, `wxStorm`) пишут coverage, density, rain, wind числами в коде и подпись в меню. Пресет — строка JSON `{name, coverage, density, rain, wind}` и одна команда «применить пункт».

`loadToolIcon` и `fontBesideExe` ищут файл рядом с exe. Путь шрифта и иконки решает платформа (`platformResolveAsset`). Демо называет логическое имя.

`onStats` показывает и прячет панель. Видимость — `uiShow` из команды, без знания, как панель сверстана. Это ближе к норме, чем вёрстка, и может остаться командой.

### `app/ProjectGate.cpp` (325)

Работа: новый проект, открыть, сохранить сцену, загрузить, импорт модели.

Это единственный кусок демо, который по роли похож на слой 3: он вызывает `sceneDocWrite`, диалог и `sceneFrameLoadBhop`. Ему не нужно знать узлы HUD. Сейчас он принимает `HudContext` целиком, потому что путь проекта лежит в `hud.projectRoot[512]`, а не в документе проекта.

`writeEmptyScene`, `saveScene`, `loadScene` собирают путь через `snprintf` в стековый буфер. Формат пути — `core/files` / `core/asset`. Демо передаёт папку, которую вернул диалог.

### `app/Shell.hpp` (33), `app/Shell.cpp` (512)

Второй редактор, пример виджетов. `shellBuild` находит узлы по имени (`scale`, `preview`, `page_look`) и дорисовывает поверх bin: чипы, nested layer, док, окно настроек, меню GROW/PING/WARM.

Пишет чужое:

- `uiSetHeight`, `uiSetPaint` сырыми каналами акцента;
- `s.ent[header].set<UiTitleBar>({})`;
- `s.ent[settings].set<UiResizable>({})`;
- `dragHeader` сам собирает шапку из панели, лейбла, спейсера и кнопки-складки. Шапка окна уже есть в хроме.

`toolBuild` и `colorBuild` собирают отдельные окна теми же руками: flex заголовка, кнопка закрытия, кривая, градиент. Окно «Settings» — JSON. Демо передаёт builder, который грузит этот JSON.

`Shell.cpp` длиннее лимита слоя 3 (350). Большая часть длины — вёрстка, которой здесь не должно быть.

### `Sample.cpp` (390)

Грузит `BH_UI_SHELL` и `BH_UI_SAMPLE`. Это правильная сторона: раскладка уже в bin. Дальше файл ломает границу.

- `setPage` пишет `UiText` и `UiFlex::width` у подписи страницы и зовёт `uiMeasure`.
- `applyCompact` обходит детей списка и пишет им `UiFlex` высоту.
- `formSubmit` сканирует все узлы мира в поисках `UiField::inForm`.
- `cmdClear` зануляет байты `UiField` (`len`, `caret`, `anchor`, `scroll`, `bytes[0]`). Очистка поля — `uiSetFieldText` или команда поля.
- `onPage` каждый клик зовёт `uiFindName` трижды. Имя ищется один раз при загрузке, узел кладётся в таблицу привязки.
- `uiBuildShell` после bin ещё раз обходит все узлы и вешает контекстное меню поля. Это делает загрузчик роли, не демо.
- хвостовой `spawn` дописывает кнопки руками, если bin не покрыл экран.

~~Пока `Sample.cpp` живёт внутри `BurnhopeEngine` и делит `Engine` с редактором сцены, у приложения два лица. Пример виджетов — отдельное демо. Редактор — JSON плюс команды.~~

---

## `engine`

Графика не включает UI. Это соблюдено: ни один файл `engine` не тянет `core/ui`. Утечка в другую сторону. Рендер выставил наружу форму инспектора, а один файл кадра владеет загрузкой, симуляцией и каждым проходом.

Шейдеры разобраны по файлу: отдельный `.slang` на один проход — кирпич. Смешение не в шейдере. Оно в C++, который склеивает проходы в один объект и копирует ползунки по имени. Ниже шейдеры сведены пачкой, исходники — по файлу.

### Кадр, который делает всё

#### `include/render/SceneFrame.hpp` (266)

Публичный фасад. Ему положено: создать кадр, загрузить сцену, тик, запись, уничтожить.

Сейчас фасад ещё:

- отдаёт редактор актёров (`sceneFrameActorSelect`, gizmo, snap, pivot, euler, duplicate, spawn, play/pause) — это сцена, не проход;
- объявляет `kSecTransform` … `kSecMatFile` — биты секций инспектора;
- объявляет `SceneTweaks` на ~100 полей с именами ползунков (`restirTemporalDepth`, `ssrThick`, `fogR`, `sunKelvin`, …);
- объявляет `HudPost` на ~40 полей (`grain`, `brightness`, `dofFocus`, …). Комментарий в файле прямо говорит: «ползунки панели»;
- `sceneFrameDebug` принимает десять `bool` подряд. Это один POD флагов;
- включает окно, свопчейн, кучу дескрипторов и `FlyCam`.

`sceneFrameTune` кладёт `HudPost` внутрь кадра. Проход читает блок по смещению. Имена полей инспектора в заголовок рендера не входят.

#### `include/render/SceneFrameImpl.hpp` (372)

Рабочая структура кадра. Ей положено держать буферы и хэндлы проходов, которые она оркестрирует.

Сейчас одно поле на всю жизнь приложения: камера, актёры, погода-сущность, гизмо, test scene, vis, shade, sky, radiance, sharc, restir, fsr, gtao, voxel, cube, tonemap, свет, декали, порталы, физика, звук, частицы, туман, дождь, пропы, текстурный индекс, статистика, и вторая копия почти всего `SceneTweaks` отдельными полями (~~`gtaoRadius` и `ssrCutoff` сняты~~, дальше от `sscsReach` до `rainRipple`).

`HudPost hud` лежит прямо в кадре.

Потолки перечислены в сквозном разделе. `cpuVerts[kSceneVerts]` рядом с `std::vector<GpuVertex> ownedVert`: два склада вершин в одном объекте.

Функции `framePass*` объявлены здесь и принимают `SceneFrameImpl&` целиком (`framePassCull`, `framePassSun`). Проход солнца видит дождь и FSR, потому что они поля того же объекта.

#### `src/SceneFrame.cpp` (4833)

Один файл на все эти работы. По местам в файле:

| Строки (порядок функций) | Чужая работа | Куда |
| :--- | :--- | :--- |
| `placeActors`, `pushBlob`, `armEditorRoom`, `writeCube` | расстановка процедурной комнаты и куба | сцена / `PrimitiveGen` |
| `resolveTexturePath`, `indexTextureDir`, `indexSceneTextures`, `loadTextures`, `uploadDummy` | поиск текстур, `std::unordered_map<std::string, std::vector<std::string>>`, `snprintf` | `core/asset` + `core/image` |
| `bindHeap`, `uploadLights`, `uploadStatic` | привязка кучи и заливка буферов | кадр, это его работа |
| `bakeAudio` | звук | `audio/` |
| `modelLabel`, `adoptLoadedModel`, `sceneFrameLoadBhop`, `sceneFrameAppendBhop` | импорт и подпись меша | `core/asset` |
| `sceneFramePlantCubes`, `writePlane`, `writeSphereMesh`, `writeCylinder`, `sceneFrameSpawn` | генерация примитивов и спавн | `core/gfx` / сцена |
| `sceneFrameActor*`, `sceneFrameCamera*`, `sceneFrameGizmo*`, `sceneFrameSnap` | редактор | `gpu_scene` актёры, уже начатый `SceneObject.hpp` |
| `captureActors` / `restoreActors`, play/pause | снимок симуляции | физика + сцена |
| `sceneFrameTweaks` | 120 строк полей по имени | одно копирование POD |
| `sceneFrameReleaseGpu` | глушит каждый проход и зовёт `vkDeviceWaitIdle` | каждый проход гасит себя; idle на уничтожении устройства, не внутри оркестратора кадра |
| `sceneFrameTick` | ввод, камера, физика, погода, пропы, гизмо | тик сцены вызывает кирпичи, не их тела |
| `sceneFrameStats`, набор `gpuText` | форматирует оверлей | строку собирает тот, кто показывает оверлей, из чисел кадра |
| `syncMeshActors`, `syncPropInstance`, `syncPropMaterial`, `followHandLamp` | гонит редактор в GPU-инстансы | сцена пишет инстанс, кадр только заливает буфер |

Пока это один `.cpp`, любой проход при правке инспектора пересобирает загрузчик текстур.

### Актёры и гизмо в одном заголовке

#### `gpu_scene/SceneObject.hpp` (1384)

Работа: POD компоненты актёра (`Xform`, `Transform`, `Material`, `Pivot`, `MeshBind`, …) и список сущностей.

Дальше в том же заголовке, инлайном:

- базис Эйлера, спин, снап (`actorBasis` … `actorSnap`);
- луч из пикселя, проекция, дистанция на экране (`actorRay` … `actorMetersPerPx`);
- размер иконки гизмо (`actorIconPx`);
- пик оси, кольцо, заполнение `GizmoGpu` (`sceneActorHit` около строки 893, `sceneActorFillGpu`, `sceneActorPointer` до конца файла);
- состояние захвата внутри `SceneActors`: `grabAngle`, `grabEuler`, `grabPx`, `snapOn[3]`, `viewProj`, базис камеры.

Компонент не хранит жест. Жест гизмо — свой модуль, он читает `Xform` и пишет `Xform`. `GizmoGpu` (320 байт) — uniform прохода, он лежит рядом с данными прохода (`render/Gizmo.hpp`), не среди компонентов.

`SceneActors::count` — `uint8_t`. Список актёров уже `std::vector<flecs::entity>`, потолок на счётчике лишний.

#### `include/render/Gizmo.hpp` (24), `src/PassGizmo.cpp` (159)

Проход рисует uniform. Заголовок включает `gpu_scene/SceneObject.hpp`, то есть шейдерный проход видит Flecs-актёра и жест. Проходу нужен `GizmoGpu` и буфер. Пик мыши живёт в модуле жеста.

`shader/gizmo.slang` — кирпич рисунка. Свою работу держит.

### Симуляция, записанная как проход

#### `include/render/Physics.hpp` (45), `src/Physics.cpp` (526)

Jolt: мир, меш, бокс, шаг, луч крыши. Это не рендер. Заголовок ещё включает `InstanceData.hpp` и `MeshletGpu.hpp`: физика знает GPU-вершину. Ей нужен свой POD позы и список треугольников, который сцена уже собрала. Жить файлу в `engine/sim/` (или рядом с `gpu_scene`), не в `include/render`.

`Physics.cpp` печатает лог через `vsnprintf`. Сообщение об ошибке шага — `spdlog` одной строкой, без своего форматтера.

#### `include/render/Weather.hpp` (62), `src/Weather.cpp` (209)

`Weather` — правильный POD климата (160 байт, выровнен). Портит его хвост полей `sliderWet`, `sliderZenith`, `sliderAzimuth`, `sliderTime`: это копия ползунка внутри симуляции. Ползунок пишет цель (`goalCoverage`, `goalRain`, …). Симуляция хранит состояние и цель, не «значение виджета».

`weatherSpawn(flecs::world&, flecs::entity&)` тянет Flecs в рендер. Спавн сущности делает сцена. Погода получает `Weather&` и `dt`.

`weatherGoals` принимает 14 float аргументов. Это один POD цели, тот же кусок блока данных.

Шейдеры погоды свою работу держат, каждый на свой проход: `cloud.slang`, `cloud_march.slang`, `cloud_density.slang`, `cloud_shadow.slang`, `cloud_up.slang`, `cloud_noise.slang`, `rain.slang`, `fog_volume.slang`, `shade_fog.slang`, `shade_sky.slang`, `sky_lut.slang`, `sky_probe.slang`, `core/atmosphere.slang`.

#### `include/render/Particles.hpp`, `src/Particles.cpp` (235)

Проход частиц. Шейдер `particle.slang` — кирпич. C++ включает свет (`gfx/Light.hpp`) и сам пишет кадр. Если частица читает уже залитый свет из кучи, заголовок света ей не нужен.

#### `include/render/Rain.hpp`, `src/Rain.cpp` (128)

Проход дождя. Ближе к кирпичу. Параметры дождя приходят из `Weather`, не из ползунка. Свою геометрию держит.

#### `include/render/Fog.hpp`, `src/Fog.cpp` (96)

Проход тумана. Ближе к кирпичу. Плотность и цвет читает из блока атмосферы.

### Проходы, которые уже почти кирпичи

Эти файлы делают один проход. Их трогать ради пирамиды не нужно, кроме мест, где они видят чужой заголовок через `SceneFrameImpl.hpp` или копируют чужое поле.

| Файл | Строк | Работа |
| :--- | ---: | :--- |
| `PassClusters.cpp` | 18 | вызов кластеров |
| `PassRestir.cpp` | 21 | вызов ReSTIR |
| `PassVoxel.cpp` | 25 | вызов вокселей |
| `PassRadiance.cpp` | 28 | вызов каскадов |
| `PassPoints.cpp` | 33 | вызов точечных теней |
| `PassSharc.cpp` | 40 | вызов SHaRC |
| `PassVis.cpp` | 53 | вызов vis |
| `PassTonemap.cpp` | 54 | вызов тонмапа |
| `Hiz.cpp` | 57 | Hi-Z |
| `PassSun.cpp` | 60 | вызов солнца |
| `PassCull.cpp` | 61 | вызов куллинга |
| `PassShade.cpp` | 77 | вызов shade |
| `Cull.cpp` / `Cull.hpp` | 157 | куллинг инстансов |
| `Cube.cpp` | 178 | куб и грани |
| `Tonemap.cpp` | 190 | тонмап, bloom, AA |
| `Sky.cpp` | 219 | небо и LUT |
| `Visbuffer.cpp` | 263 | vis-буфер |

Тонкие `Pass*.cpp` включают `SceneFrameImpl.hpp` (`PassSun.cpp`, `PassShade.cpp`). После разреза кадра они включают только свой проход и `PassCommon`.

`Shade.cpp` (350) при создании шейдера сам поднимает GTAO, туман и контакт (`p.gtao`, `p.fog`, `p.contactCs`). Это три прохода внутри shade. У них уже есть свои `.slang`: `gtao.slang`, `shade_fog.slang`, `contact.slang`. Создание шейдера остаётся в своём модуле. Shade вызывает их диспетчер, не хранит их `ShaderObject`.

`shader/shade_hit.slang`, `shade_sun.slang`, `shade_punctual.slang`, `shade_shadow.slang`, `shade_tile.slang`, `shade_sky.slang` — кирпичи одного семейства shade. Общая математика уже вынесена в `shader/core/` (`brdf`, `math`, `vis_surface`, `grade`, `atmosphere`, `hiz_atlas`) и `shader/camera.slang`. Это правильный низ.

### Проходы, которые склеили несколько работ

#### `src/Restir.cpp` (892), `include/render/Restir.hpp` (199)

Один объект на DI, солнце, GI и отражения. Лог сам перечисляет: «лучи DI, солн, GI, отр». Четыре режима — четыре записи в один проход с пушем индекса, или четыре тонких вызова одного шейдерного семейства. Сейчас это ещё и `std::vector<float> pdfCpu` на проходе, и прямое включение `Rtxdi/DI/ReSTIRDI.h` в заголовок, который видит весь кадр.

Шейдеры семейства свои: `restir_di.slang`, `restir_fast.slang`, `restir_rab.slang`, `restir_presample.slang`, `relax.slang`, `nrd_reproj.slang`. Кирпичи. C++ их склеил.

`kPassRestir = 1u << 20` объявлен в заголовке прохода. Бит маски кадра живёт в одном месте (`PassCommon` или блок флагов), не в каждом проходе своим числом. То же для битов, которые демо пишет вручную в `hudPushFrame`.

#### `src/Radiance.cpp` (947), `include/render/Radiance.hpp` (156)

Семейство каскадов. Шейдеры уже разрезаны правильно: `rc_trace_split`, `rc_merge`, `rc_shade`, `rc_alloc_probes`, `rc_geom`, `rc_surface`, `rc_prefix`, `rc_common`, `rc_octahedral`. C++ на 947 строк держит и запекание треугольников (`std::vector` позиций на кадр запекания), и пуш, и шейдеры. Запекание геометрии — отдельная функция сцены, проход только трассирует уже залитый буфер.

`src/PassRadiance.cpp` тонкий. Толщина живёт в `Radiance.cpp`.

#### `src/Sharc.cpp` (374), `include/render/Sharc.hpp` (120)

Один модуль на кэш, обновление, запрос, резолв и денуаз. Шейдеры уже разрезаны: `sharc_cache`, `sharc_update`, `sharc_query`, `sharc_resolve`, `sharc_hit`, `sharc_params`, `sharc_relax_spatial`, `sharc_relax_temporal`. C++ может остаться одним кирпичом SHaRC, если он не читает ползунок по имени, а читает `giGain` и флаг денуаза из блока. Сейчас кадр передаёт их аргументами `framePassSharc` из полей `SceneFrameImpl`, которые скопированы из `SceneTweaks`.

#### `src/Fsr.cpp` (824), `include/render/Fsr.hpp` (98)

Один модуль на движение, SPD, RCAS и апскейл (`FsrConst`, `SpdConst`, `RcasConst`, `MotionConst`). Для пирамиды это один кирпич «апскейл», размер большой из-за констант вендора. Нарушение другое: качество и резкость приходят из `SceneTweaks.fsrQuality` через копирование по имени, а лог пишет `snprintf` в `lastLog`, который кадр потом клеит в `gpuText`. Строка лога — отладочный канал прохода, не поле кадра, которое демо режет на 40 лейблов.

`shader/fsr_motion.slang`, `taa.slang`, `smaa.slang`, `cmaa2.slang` — отдельные проходы AA. Выбор между ними — байт в блоке, не `if (f.aa == 3)` в демо.

#### `src/Gtao.cpp` (378), `include/render/Gtao.hpp` (75)

Проход XeGTAO. Ближе к кирпичу. Радиус, шаги, денуаз читает из блока, не из шести полей `SceneFrameImpl`, продублированных с `SceneTweaks`.

#### `src/Voxel.cpp` (380), `include/render/Voxel.hpp` (74)

Проход вокселей. Шейдеры `vx_voxel`, `vx_trace`, `vx_resolve`, `vx_clear`, `vx_common` — кирпичи. C++ один. Ему не нужно видеть остальной кадр.

#### `shader/sigma.slang`, `reblur.slang`

Денуазеры. Кирпичи, если их вызывает тот проход, чью картинку они чистят, а не общий оркестратор по имени ползунка.

### Кучи, пуши, цели

Эти заголовки — низ кадра. Свою работу держат.

| Файл | Работа |
| :--- | :--- |
| `RenderDefs.hpp` | каталог шейдеров, 5 строк |
| `HeapBind.hpp` | слоты кучи |
| `PassCommon.hpp` | пуш 16 байт, `GpuTask` |
| `PassAllocators.hpp` | аллокаторы прохода |
| `PassBufferSync.hpp` | барьер буфера |
| `PassRendering.hpp` | begin/end rendering |
| `PassShaderGroup.hpp` | группа шейдеров |
| `PassTargets.hpp` | цели кадра |
| `PassDebug.hpp` | отладочные режимы |
| `VisResolve.hpp` | разбор vis |
| `gpu_scene/Mat4.hpp` | матрица |
| `gpu_scene/Camera.hpp` | камера |
| `gpu_scene/FrameUniforms.hpp` | UBO кадра |
| `gpu_scene/InstanceData.hpp` | инстанс |
| `gpu_scene/InstanceCull.hpp` | frustum |
| `gpu_scene/MeshletGpu.hpp` | мешлет |
| `gpu_scene/MeshletCull.hpp` | куллинг мешлета |
| `gpu_scene/LightMath.hpp` | свет |
| `gpu_scene/PointShadow.hpp` | точечная тень, данные |
| `gpu_scene/SunShadow.hpp` | тень солнца, данные |

`PassCommon.hpp` кроме пуша содержит ещё `GpuTask` (пул, буфер, fence, сабмит). Это второй кирпич в том же файле: разовый GPU-таск. Ему хватит своего заголовка, чтобы проход погоды не включал сабмит чужой очереди через общий зонт.

`shader/frame.slang`, `hiz.slang`, `cull.slang`, `shadowcull.slang`, `resolve.slang`, `material_map.slang`, `visbuffer.slang`, `probe.slang`, `cube.slang`, `cube_fill.slang`, `bloom.slang`, `tonemap.slang`, `contact.slang`, `reflect.slang` — один шейдер, один проход. Свою работу держат.

### Что в рендере уже является кирпичом

Не трогать ради пирамиды, кроме одной строки «не включать `SceneFrameImpl.hpp`»:

`Mat4`, `Camera`, `FrameUniforms`, `InstanceData`, `InstanceCull`, `MeshletGpu`, `MeshletCull`, `LightMath`, `SunShadow`, `PointShadow`, `HeapBind`, `PassTargets`, `PassBufferSync`, `PassRendering`, `PassShaderGroup`, `PassAllocators`, `RenderDefs`, `Fog` (проход), `Rain` (проход), `Hiz`, `Cull`, тонкие `Pass*.cpp` после отвязки от целого кадра, все `.slang` из списка выше и `shader/core/*`.

---

## План: раскладка в JSON, данные массивом

Цель: на экране редактора ни одной строки `uiPanel` / `uiSlider` / `uiLabel` в `demo`. Файл раскладки описывает дерево. Движок один раз строит узлы и таблицу привязок. Каждый кадр — один цикл по этой таблице и одно копирование блока в проходы.

Уже есть за что зацепиться, это не новый парсер:

- JSON UI читает `core/ui/Json.cpp` (`uiImportJsonFile` уже вызывается из `shellDrop`);
- bin читает `uiImportBinFile`, `Sample.cpp` так грузит шелл;
- роли виджетов регистрируют сами кирпичи (`uiXBin`);
- `UiPropBinder` уже хранит массивы `floats[]`, `bools[]`, `swatches[]` и умеет `slides()` пачкой.

Не хватает двух вещей. Привязка смотрит на `float*` в случайные поля `HudContext`, а не на смещение в одном POD. Раскладку редактора никто не описал данными: её испускает C++.

### Как выглядят данные

Один блок на окно сцены, выровненный, без указателей:

```
SceneBlock
  bytes  flags[32]     режимы: aa, gi, direct, reflect, ao, sun, …
  floats scalars[256]  пост, твики, поза, материал — плоский массив
```

Поле «GI brightness» — это `scalars[40]`, а не `&hud.tweaks.giGain`. Имя поля живёт только в JSON. Проход, которому нужен прежний `HudPost`, читает свой диапазон массива (например скаляры 0…39) в свой POD один раз за кадр, `memcpy` куска. `sceneFrameTweaks` со 120 присваиваниями уходит.

Зеркало на `SceneFrameImpl` (`gtaoRadius`, `ssrThick`, …) уходит вместе с ним. Проход получает `span<const float>` своего диапазона или маленький POD, собранный из диапазона. Второго списка имён нет.

~~`HudFlags` не дублирует режимы. Байт режима лежит в `flags[i]`. Видимость секции и бит `passMask` считаются из этого байта таблицей, а не лестницей `if` в `hudPushFrame`.~~

Поза актёра не копируется в `HudActor` по полям. Пока жест крутит число, виджет держит значение у себя (это уже правило UI). Когда жест отпущен, цикл привязки пишет `scalars[poseBegin + 0..11]`. Сцена читает эти 12 float как `ActorPose`. `propLast[15]` не нужен: сравнение «ползунок сдвинулся» делает цикл, у которого есть прошлое значение в самом слоте привязки.

### Как выглядит JSON

Один файл на окно, например раскладка сцены, раскладка цвета, раскладка примера. Узел — объект с ролью, которая уже есть в bin (`panel`, `button`, `slider`, `check`, `menu`, `fold`, `section`, `label`). Демо файл не разбирает.

Строка ползунка:

```json
{
  "role": "slider",
  "label": "GI brightness",
  "bind": 40,
  "min": 0,
  "max": 6,
  "value": 2
}
```

`bind` — индекс в `scalars` или в `flags`. Загрузчик, встретив `bind`, дописывает одну запись в таблицу:

```
Bind { uint16 node; uint16 index; uint8 kind; float min; float max; }
```

`kind`: float, bool, байт режима, образец из трёх подряд float, пункт меню. Таблица — один массив, не `UiFloatBind` + `UiBoolBind` + `UiBitBind` + `UiSwatchBind` и не потолок 384/96/8. Число строк равно числу узлов с `bind`. Память таблицы — вектор, заполненный при загрузке, на кадр не растёт.

Меню режима:

```json
{
  "role": "menu",
  "label": "Mode",
  "bind": 3,
  "items": [
    {"label": "None", "value": 0},
    {"label": "Restir", "value": 1}
  ]
}
```

Никакого `HudPick`, никакого `picks.push_back` на пункт. Клик пункта пишет байт `flags[3] = value` и подпись на кнопке. Это делает кирпич меню, не демо.

~~Видимость секции — правило на узле, не `hudShowBodies`:~~

```json
{"role": "panel", "name": "diBody", "showWhen": {"flag": 4, "eq": 1}}
```

~~Загрузчик кладёт такие правила вторым массивом `ShowRule { node, flagIndex, eq }`. Кадр обходит его после записи флагов. Девять `uiShow` с именами полей уходят.~~

~~Список актёров, сорок строк статистики, семь слотов текстур — повторитель, не копипаста:~~

```json
{"role": "list", "bind": "actors", "row": {"role": "button", "labelFrom": "name"}}
```

~~Имя `actors` — источник, который сцена уже умеет отдать массивом (`sceneFrameActorCount` + `sceneFrameActorName`). Виджет списка берёт массив записей `{name, kind, index}` и переиспользует пул строк. Демо не вызывает `addActorButton` в цикле и не создаёт 40 пустых `uiLabel`.~~

~~Команды File, Spawn и Gizmo зарегистрированы по имени. `FileAct`, `GizmoHit`, `ModeHit`, `SnapHit`, `SpawnHit` ушли.~~ `MatHit` ещё сидит на кнопках текстур. Функция команды по-прежнему получает `HudContext`, пока демо не сжато до хоста.

### Куда девается сегодняшний C++

| Сейчас | После |
| :--- | :--- |
| `HudToolbar`, `HudStrip`, `HudView`, `HudInspector`, `HudPost`, куски `HudMaterial` | один JSON |
| ~~`Shell.cpp` вёрстка, `toolBuild`, `colorBuild`~~ | ~~пример `BurnhopeSample`, не редактор~~ |
| ~~`Sample.cpp` дорисовка поверх bin~~ | ~~пример остаётся отдельным демо и не входит в редактор~~ |
| `UiPropBinder::slide/check/watch` по одному вызову | таблица `Bind`, заполненная загрузчиком |
| `pullProp`, `syncProp`, `hudPushFrame`, `sceneFrameTweaks` | цикл по `Bind` + `memcpy` диапазона в проход |
| ~~`hudShowBodies`~~ | ~~цикл по `ShowRule`~~ |
| ~~`passMask` из `if` в демо~~ | ~~рендер читает `flags[]` и включает свои проходы~~ |
| `HudContext` на 270 строк | блок `SceneBlock`, id корня окна, путь проекта |
| ~~`Engine.cpp`~~ | ~~открыть хост, загрузить JSON, зарегистрировать команды, тик~~ |

~~`core/host/PropBind.hpp` хранит индекс в блоке, не `float*`.~~ Страница свойств (`PropGrid`) для полей, которых нет в JSON, тоже пишет в эту таблицу, а не заводит второй механизм.

### Порядок работ

Делать слоями, каждый шаг компилируется. Код в этом файле не писался.

1. ~~**Собрать блок.** `HudPost`, `SceneTweaks`, `HudFlags` и `HudActor` пакуются в `SceneBlock` (`flags[64]`, `scalars[256]`) функцией `sceneBlockFill`. Кадр забирает блок через `sceneFrameSetBlock` и читает прежние имена геттерами диапазона `sceneBlockPost` / `sceneBlockTweaks`.~~
2. **Убрать зеркало.** `sceneFrameTweaks` заменить на копирование диапазонов в те POD, которые проход и так заливает в UBO (`GtaoConstants`, константы ReSTIR, атмосфера). Поля-дубли на `SceneFrameImpl` удалять пачками по проходу: ~~GTAO, SSR, туман, ReSTIR~~. После каждой пачки кадр берёт эти числа только из блока. Дальше остаются `sscs`, `ssrc`, каскады, дождь, солнце и блум.
3. ~~**Таблица привязки по индексам.** `UiPropBinder` хранит `uint16 index`, не `float*`. Спецификация слайдера несёт индекс. `pullProp` пишет 18 скаляров позы, начиная с `kScColor0`. `sceneBlockApply` раскладывает только привязанные строки обратно в именованные поля.~~
4. ~~**Описать инспектор JSON.** `assets/ui/inspector.json` держит секции, складки, слайдеры, галки и меню поста и файла материала. `propImport` по `bind` наполняет таблицу. `HudPost.cpp` только вызывает импорт.~~
5. ~~**Правила видимости и маска проходов.** `showWhen` вместо `hudShowBodies`. Биты проходов считает рендер из `flags[]` одной таблицей `{flagIndex, eq, bit}` рядом с проходом, не в демо.~~
6. ~~**Тулбар, меню, вьюпорт, камера.** Перенести `HudToolbar`, `HudStrip`, `HudView` в тот же JSON. Команды File / Spawn / Gizmo зарегистрировать по имени. `deque` ударов удалить.~~
7. ~~**Списки.** Актёры и строки статистики — повторитель поверх массива записей сцены. Снять `stat[40]`, `toolBtn[5]`, `matName[8][48]`.~~
8. ~~**Демо сжать до хоста.** `Engine.cpp` открывает окно, грузит JSON, отдаёт `SceneBlock`, крутит тик. `ProjectGate` остаётся командами документа и не видит узлы. `Sample.cpp` и `Shell.cpp` либо грузят свой JSON, либо выносятся в отдельный пример и перестают линковаться в редактор.~~
9. **Разрезать кадр.** Уже без давления HUD: текстуры в ассет, примитивы в gfx, физику и погоду из `include/render`, жест гизмо из `SceneObject.hpp`, оркестратор кадра оставить тонким. Это отдельная стопка, к JSON она не привязана, но пока `SceneFrame.cpp` на 4800 строк, любой новый ползунок снова впишут туда полем.

Шаги 1–3 не меняют картинку: те же числа, другая упаковка. Шаги 4–7 удаляют вёрстку из демо. Шаг 8 оставляет демо настройкой. Шаг 9 чинит пирамиду рендера.

Проверка каждого шага — прежняя: `cmake --build build-core --target BurnhopeTest BurnhopeEngine -j && ./build-core/BurnhopeTest`. На шагах 4–7 ещё ручной проход по редактору: секция открывается, ползунок двигает картинку, меню режима прячет чужое тело, список актёров растёт без нового исходника.
