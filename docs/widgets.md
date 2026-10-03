# Что уже есть

Этот файл читают до любой новой фичи виджета. Если работа здесь названа, её вызывают, а не пишут второй раз. Новая дырка дописывается сюда в том же заходе, вместе с `docs/now.md`.

Баг внутри текста чинится в `textRun`. Кнопку, поле и список после этого не переписывают: они только держат номер подписи и прямоугольник.

## Как обратиться к части виджета

Виджет с буквами создаёт ребёнка `Label`. Это отдельная сущность Flecs, не поле внутри кнопки.

- `uiTextId(state, id)` и `canvasTextId(canvas, id)` возвращают номер подписи. У самой подписи возвращают её саму.
- Строку задают `uiText` / `canvasText` на хозяине (кнопка, галочка, крестик). Кадр копирует байты на подпись.
- Стиль задают на сущности из `uiTextId`: компонент `UiText`. Кадр его не затирает. Цвет букв — `UiPaint` той же сущности. Кнопка сама ставит контрастный цвет, если заливка светлая или тёмная.
- Поле каждый кадр само пишет показанную строку (текст, маска пароля или подсказка), левый край, без переноса и прокрутку каретки. Остальной стиль подписи поля сохраняется. Каретка считается от левого края, поэтому выравнивание поля не отдают наружу.

Прямоугольник подписи ставит хозяин. Подпись не меняет ширину кнопки.

## Текст — `core/ui/widget/Text.cpp`

Одна функция `textRun`. Рисует общий кадр через `uiEmitRun`. Правка байтов — `TextEdit.cpp`, она не рисует.

Уже есть, поля `UiText`:

| Поле | CSS | Что делает |
| --- | --- | --- |
| `align` 0/1/2/3 | text-align | лево, центр, право, по ширине (пробелы последней строки переноса не растягиваются) |
| `valign` 0/1/2 | vertical-align блока | верх, центр, низ слота |
| `wrap` | white-space / text-wrap | 0 одна строка, 1 перенос по ширине, в том числе по `\n` |
| `ellipsis` 0/1/2 | text-overflow | нет, «...» в конце, «...» в середине |
| `deco` биты 0, 1, 2 | text-decoration-line | подчёркивание, зачёркивание, надчёркивание |
| `thick` | text-decoration-thickness | толщина линии, 0 значит 1 px |
| `under` | text-underline-offset | сдвиг подчёркивания вниз |
| `leading` | line-height сверх em | добавка между строками |
| `tracking` | letter-spacing | px к каждому символу |
| `wordGap` | word-spacing | px сверх ширины пробела |
| `indent` | text-indent | отступ первой строки при левом крае и justify |
| `transform` 0–3 | text-transform | нет, верхний, нижний, первая буква слова. Латиница и кириллица |
| `breakAll` | word-break: break-all | разрыв внутри слова |
| `tabs` | tab-size | 0 значит 4 пробела |
| `size` | font-size | множитель em, 0 значит 1. Если слот ниже строки, буквы уменьшаются |
| `shadowX`, `shadowY` | text-shadow | сдвиг тёмной копии, 0 и 0 значит нет тени |
| `scroll` | — | без переноса сдвиг влево, с переносом вверх |

Нет и сюда не добавлять вторым шрифтом или вторым шейпингом:

- жирный, курсив, другое семейство — один SDF-атлас;
- переносы по словарю (`hyphens`);
- вертикальное письмо, bidi, направление справа налево;
- волнистая линия декора, акцент-точки, ruby;
- висячая пунктуация, widows/orphans.

Цвет и альфа букв — `UiPaint` подписи, не поля `UiText`.

## Виджеты

Фон любой роли — общая заливка. Обводка (`borderW`, `canvasBorder`) — кольцо по краю, бокс не сжимает. Тень (`shadow`, `canvasShadow`) — примитив вокруг бокса. Ребёнок рисуется и нажимается только внутри предков.

Transform и filter — один draw, без второго примитива и без второго прохода Vulkan:

| Поле | CSS | Что делает |
| --- | --- | --- |
| `angle` | transform: rotate() | градусы, поворот вокруг центра бокса, `canvasTransform`/`Panel::transform` |
| `scaleX`, `scaleY` | transform: scale() | множитель вокруг центра бокса, по умолчанию 1 |
| `bright` | filter: brightness() | множитель цвета после заливки, по умолчанию 1 |
| `contrast` | filter: contrast() | растяжение вокруг 0.5, по умолчанию 1 |

Упаковано в свободные `pad0`/`pad1` примитива (`kUiFlagTransform`) — несовместимо на одном примитиве с clip (`kUiFlagClip`) и с тенью/обводкой (`kUiFlagShadow`/`kUiFlagStroke`), те тоже держат данные в `pad0`/`pad1`. Панель с активным clip трансформацию/фильтр не получает, пока clip стоит. Blur для `kUiFlagPhoto` (сэмпл существующего мипа текстуры вместо второго прохода) не сделан — `docs/open.md`.

### Button, Close

Ребёнок `Label`, номер в `uiTextId`. Кнопка ставит слот (с отступом, если есть `icon`) и контрастный цвет. Иконка 1–7 — маска в шейдере, не файл. Нажатие вызывает `UiAction` (`uiBind` в месте создания). Если её нет, зовётся общий `onClick` оболочки. Кнопка не знает календарь, цвет, страницу и форму. Крестик — роль Close: кадр возвращает закрытие окна, свою команду не хранит.

### Check

Ребёнок `Label` слева после квадрата. Свои данные: `UiCheck` (`on`, `kind` 0 квадрат / 1 круг, `group`). Рисует только отметку. Круг выключает остальных в той же группе. После переключения зовёт `UiAction`, если её задали.

### Field

Ребёнок `Label` для показа. Байты, каретка, фильтр, пароль, подсказка — `UiField`. Вставка и стирание UTF-8 — `TextEdit.cpp`. Само поле рисует выделение, каретку, крестик очистки и рамку фокуса или ошибки. После правки байтов зовёт `UiEdit` (`uiEdit`), если её задали. Что делать с числом или hex, решает сборка, не поле.

### Label

Сама строка. Стиль — таблица выше. Если это не чужая подпись, `uiTextId` возвращает её саму.

### Slider

`UiRange`: число, min, max. Дорожка и ползунок. Если `UiPaint.ramp` не 0, дорожка — рампа этого номера, оттенок в цвете заливки. Слайдер не знает, какой это канал. Жест пишет число и зовёт `canvasListen`. Перетаскивание чужого узла — `UiDrag` (`uiDrag`): ввод только вызывает функцию, колесо цвета её задаёт само.

### Progress

Та же `UiRange`, без ползунка и без перетаскивания. Заливка — дорожка, акцент — заполненная доля.

### Scroll

`UiScroll.inner` — прокручиваемый ребёнок. Колесо двигает `offset`. Если содержимое выше окна, справа ползунок, его тащат. Клип — бокс предка.

### Splitter

Тянет ширину предыдущего соседа. Тег -5. Своей подписи нет.

### Image

`UiImage`: слот 0–7, кадр GIF, окно view, миникарта. `uiIcon` / `canvasIcon` — тот же виджет без миникарты, клик проходит родителю. Файл в слот кладёт `canvasLoadImage` (`core/image`), не этот виджет.

### Tip

`uiTip` создаёт панель с тенью, обводкой и подписью. `uiTipAt` пишет строку и ставит панель. Подпись — `uiTextId` этой панели.

### List, Menu

Панель из кнопок. Отдельных букв нет: у каждой кнопки свой `uiTextId`.

## Бокс — `UiFlex`, считает Yoga

Один компонент на узел. Кадр каждый раз переносит его в Yoga (`applyFlex`). Обводка рисуется поверх и в размер бокса не входит. `box` 1 — content-box, 0 — border-box.

| Поле | CSS | Значения |
| --- | --- | --- |
| `direction` | flex-direction | 0 column, 1 row, 2 column-reverse, 3 row-reverse |
| `wrap` | flex-wrap | 0 nowrap, 1 wrap, 2 wrap-reverse |
| `justify` | justify-content | 0 start, 1 center, 2 end, 3 space-between, 4 space-around, 5 space-evenly |
| `align` | align-items | 0 stretch, 1 center, 2 start, 3 end, 4 baseline |
| `content` | align-content | 0 stretch, 1 center, 2 start, 3 end, 4 between, 5 around, 6 evenly |
| `self` | align-self | 0 auto, 1 stretch, 2 center, 3 start, 4 end, 5 baseline |
| `width` / `height` + mode | width, height | 0 auto, 1 px, 2 percent |
| `basis` + `basisMode` | flex-basis | те же режимы |
| `grow`, `shrink` | flex-grow, flex-shrink | shrink по умолчанию 1 |
| `minW`, `minH` | min-width, min-height | px, 0 значит не задан, кроме grow |
| `maxW` / `maxH` + mode | max-width, max-height | 0 нет, 1 px, 2 percent |
| `pad`, `padL/R/T/B` | padding | если сторона > 0, берутся стороны, иначе общий `pad` |
| `gap`, `gapRow` | column-gap, row-gap | `gapRow` < 0 значит оба равны `gap` |
| `marginL/R/T/B` | margin | px |
| `position` | position | 0 static, 1 absolute, 2 relative |
| `posX`, `posY` | left, top | px |
| `posR`, `posB`, `inset` | right, bottom | бит 0 включает right, бит 1 — bottom |
| `aspect` | aspect-ratio | 0 нет |
| `overflow` | overflow | 0 visible, 1 hidden, 2 scroll. Роль Scroll всё равно прячет |
| `box` | box-sizing | 0 border-box, 1 content-box |
| `drop` | — | 1 прячет последнего ребёнка, который не влезает, 2 — первого |

JSON-вёрстка читает те же слова: `dir` (`row`, `column`, `row-reverse`, `column-reverse`), `wrap`, `justify`, `align`, `content`, `self`, `basis`, `maxW`, `maxH`, `gapRow`, `aspect`, `overflow`, `box`, `position`, `right`, `bottom`. Плюс прежние `w`, `h`, `grow`, `pad`, `gap`.

Нет в Yoga и сюда не писать: `order`, `z-index` как свойство раскладки (порядок рисунка — порядок детей и абсолютный слой), отдельный `writing-mode`.

### Panel

Прямоугольник с этим боксом. Сам букв не создаёт. Скругление — `radius` заливки, не отступ Yoga.

## Разметка — JSON на сборке, блоб в кадре

Файл `assets/ui/shell.json` описывает статичный экран. `tools/uibake.py` пишет плоский блоб (BHUI, версия 2, узел 204 байта, пул строк). Кадр читает его через `uiImportBin` / `uiImportBinFile` / `canvasImportBin` и не парсит этот JSON. `uiImportJson` остаётся для документа, который открыли в рантайме.

Загрузчик вызывает `spawn`, `addText`, `uiName`, `uiWindowChrome`, `uiBuildCalendar`, `fillColorBody`. Это тот же C++ API, что `Panel::child` и `Panel::button`.

Ключи узла: `name` или `id`, `role` или `type`, `text`, `hint`, `dir`, `w`/`h` (число или `"100%"`), `grow`, `shrink`, `pad`, `padT`/`padB`, `gap`, `gapRow`, `radius`, `fill`, `tag`, `icon`, `children`, `show`, `x`/`y`, `right`/`bottom`, `min`/`minW`/`minH`, `maxv`, `maxW`/`maxH`, `basis`, `value`, `shadow`, `border`, `br`/`bg`/`bb`, `padL`/`padR` (отступ текста поля), `filter`, `max` (длина поля), `clear`, `required`, `readonly`, `password`, `form`, `on`, `kind`, `group`, `minimap`, `drop`, `content`, `self`, `overflow`, `box`, `aspect`, `marginL`/`marginR`/`marginT`/`marginB`, `textWrap`, `textAlign`, `ellipsis`, `valign`, `deco`, `leading`. Слова `center`, `row`, `wrap`, `absolute` пекарь понимает так же, как числа.

Роли сверх обычных виджетов: `chrome`, `calendar`, `color`, `menu`, `image`. Имя становится `UiName` и символом Flecs.

Связь с узлом из блоба: `uiFindName`, `uiBindName`, `canvasFind`, `canvasBindName`, `Panel::find`. Дальше в найденный узел можно звать `child` и `button`.

## Сборка руками — `Panel`

`Panel::make`, `at`, `find`, `child`, `label`, `textButton`, `button`, `field`, `check`, `radio`, `spin`, `setText`, `color`, `animate`. Под ними `canvasNode` → `spawn`. Этот слой не заменяется блобом. Окно `tool` и блок YOGA GROW собраны им.

`uiTrace` пишет в канал `UI` одну строку `ui fn`: имя функции, id, имя узла, роль, тег и бокс. `uiDumpLayout` пишет `ui layout`, затем `ui box` для именованных узлов и кнопок, полей, слайдеров, галочек, крестиков и скроллов. `ui miss` — бокс пустой, NaN или за пределами кадра. Кадр вызывает дамп на первом кадре и по F3 (`kScanF3`). `uiDrop` снимает поддерево: Yoga-узел уходит из родителя и освобождается вместе с детьми, сущности Flecs уничтожаются, номер слота не переиспользуется. `uiReparent` переносит узел. `uiShow(id, false)` ставит Yoga `display: none`, хит такой узел не берёт. `uiVirtual` — пул из 12 строк. Скролл меняет индекс данных, ноды не плодятся. Проводник его ещё не зовёт. `uiSpin` / `Panel::spin` — число, которое тянут мышью; на время тяги хост включает относительную мышь и возвращает курсор. `uiCurve` / `Panel::curve` — кубическая кривая. Вершинный шейдер из четырёх опорных точек строит ленту (`kUiFlagRibbon`): стык 0 круглый (капсула), 1 bevel, 2 miter. Ручки остаются квадами. `uiGradient` / `Panel::gradient` — полоса из двух остановок. `uiTreeRow` — строка и скрытое тело. `uiShowOnly` прячет соседей. `uiFocusStep` — Tab и Shift+Tab по `UiTab`. `uiMarkLayout` считает поддерево Yoga. `uiDragBegin` / `uiDragEnd` — шина переноса, список ждёт 4 px, `UiDropTarget` фильтрует kind. `uiCursorPush` / `uiCursorPop` — стек курсора. `uiTab` задаёт порядок фокуса. Escape закрывает меню и сбрасывает перенос. Поле помнит восемь снимков (Ctrl+Z, Ctrl+Y). Тройной клик выделяет всё поле. `uiFieldMulti` включает перевод строки, каретку по строкам и перенос по ширине. У заливки четыре радиуса: `radius`, `radiusTr`, `radiusBr`, `radiusBl`. Панель с `ramp` красится градиентом шейдера. `actionBind` / `actionBindChord` / `actionListen` / `actionSave` / `actionLoad` — аккорды и JSON. Незакрытый список — `docs/open.md`.

## Сборки ядра — `core/ui/kit`

Цвет и календарь. Дерево виджетов и своя политика. Календарь собирает `uiBuildCalendar`. Цвет собирает `fillColorBody`. Колесо, яркость и образец всё ещё ветки по тегам в общем кадре. Новый цвет туда не добавлять: это дырка, не образец.

Хром окна — `uiWindowChrome`: свернуть, развернуть, закрыть. Демо эти кнопки не спавнит.

## Демо — `demo/`

Страницы SCENE, LOOK, INPUT (`demo/Sample.cpp`) и оболочка (`demo/app`). Ядро этого экрана не знает и не может его подключить. Смена фокуса снаружи — `canvasOnFocus`.

## Старое — `old/`

Редактор, шейдеры G-buffer, модели и ImGui до этой сборки. Не читать и не переносить в `core`, `demo`, `engine`.

## Движок — `engine/`

Visbuffer, shade, SSR, сцена. Подключает `core/rhi` снаружи. В демо-exe не входит. Ядро его не видит.

## Картинка — `core/image`

Декод stb и сжатие ISPC BC1/BC7/BC3/BC6H. В виджет не копировать.

## Проводник — `core/files`

Собран из кнопок, поля, слайдера и подписей, но сетка и низ ещё дорисовываются отдельно. Пока не переписывать. Следующая сборка — только вызов виджетов из этого файла.

## Оболочка демо — `demo/app`

Задаёт окна и функции сборки. Свой swapchain и свой SDL не пишет. `Shell` слушает слайдер масштаба и клики файлов. Свою кнопку не рисует.

## Кадр — `core/host`

До восьми окон. `hostOpen` принимает `ViewDesc`: id, `WindowConfig`, `FrameDesc`, `UiBuilder`, необязательный `ColorBook`, `onOps`, `onClose`. Id принадлежит приложению, кадр его не толкует. То же id поднимает окно. `FrameDesc.present`: `Vsync`, `Mailbox`, `Immediate`, `Relaxed`. `hostSetFrame` меняет режим у живого окна. Первое окно главное. Minimize, maximize, непрозрачность, поверх всех, курсор, буфер обмена, текстовый ввод, F5 и диалог файла делает кадр. Команду, которую кадр не знает, отдаёт `onOps`.

## Окно — `core/platform`

Весь SDL, кроме поверхности Vulkan (`deviceCreateSurface` в `core/rhi`). Создание и последующая настройка: положение, размер, минимум, максимум, рамка, resize, скрытие, полный экран, поверх всех, фокус, модальность, родитель, непрозрачность, aspect, utility/tooltip/popup, grab, относительная мышь, вспышка, прогресс, иконка, форма, системное меню. Ввод за кадр — `InputFrame`: кнопки включая X1/X2, колесо X, Super, Caps, Num, `keyDown`/`keyUp`, `windowHoldMs`. Диалог — `windowAskFile` / `windowTakeFile`. Геометрия — `windowRemember` / `windowRecall`. Полный экран — `windowFullscreenKind` (окно, borderless, exclusive). Лимит кадра — `platformPace`.
