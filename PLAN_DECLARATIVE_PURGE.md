# PLAN_DECLARATIVE_PURGE — Deep-Purge Blueprint

(On approval, copy verbatim to `/home/marwinka/Музыка/BurnhopeEngine/PLAN_DECLARATIVE_PURGE.md`. No code is changed by this plan.)

## 0. Context

Layer 2 widgets (`core/ui/widget/`, `core/ui/kit/`) and `core/host/` still carry four legacy pathologies: extrinsic layout leaks and negative-margin hacks, drawing magic numbers, raw palette-float surgery, and raw `spawn()` / imperative `UiFlex`. Three read-only audits (greps plus file reads) produced the inventory below. Goal: widgets own intrinsic behavior only; geometry and metrics live in `assets/ui/styles.json`; structure lives in `assets/ui/core/*.json` blueprints; color comes from tokens only.

### Audit corrections to the brief
- `fillWidth` / `fillHeight`: **zero** occurrences. The firewall rule is added anyway as a future-proof ban.
- **Blocker:** `kUiTemplateCap = 32` (`core/ui/Template.hpp:15`) and 30 blueprints are already registered. Three new blueprints make 33. Raise the cap to 48 first (`Template.cpp:641,699,715,732-733` depend on it).
- Blueprint files are `assets/ui/core/*.json`, registered in `assets/ui/core/_manifest.json`, not `core/*.json`.
- `Template.cpp` rejects `w`, `h`, `minW`, margins and `position` in blueprints, and `wp`/`hp`/`grow`/`self` are allowed on parts only. Several fixes below therefore need small schema extensions (Phase 1).
- `tests/ui/TestUiStyle.cpp:76-77` fails if code names a blueprint or class that does not exist, so JSON must land before or with the C++ that uses it.
- Some audit line numbers do not match the stated file LOC. Examples: Docking.cpp (216 LOC) was cited at lines 371-441, Curve.cpp (111 LOC) at lines 558-608, and Gradient.cpp at 111 LOC. Those hits may come from sibling files. **Re-run each grep per file at the start of its phase** and trust the fresh result over this table.

## 1. Grep Audit Report

### Category 1: Extrinsic layout leaks and negative margins
| File | Violations | Fix |
|---|---|---|
| `widget/Fold.cpp` (186) | L63-64 `uiSetWidthPercent`; L171/174 `marginL=-left`, `marginR=-right`; L164 width Percent + position; L65-69, L121-122, L143-144 height/overflow open-close state | `"wp":100` in `fold_clip.json`/`fold_header.json`; new `bleed` attr; open/close state moves into `uiReveal` / `uiFoldClose` |
| `widget/Choice.cpp` (211) | `uiChoiceFit` L190-195 writes widthMode, width, minW, grow, shrink, self; L63 `uiSetWidth`; callers `PropImportInputs.cpp:43`, `demo/app/MatRows.cpp:147` | `"w":"fit"` attr plus layout-node `self`/`grow`; helper keeps only menu-width sync |
| `widget/Breadcrumbs.cpp` (135) | L46 `uiSetGrow(s,id,1,1)`; L67-75 imperative crumb pair | `grow`/`shrink` on instance; `core/crumb_item` |
| `widget/Chrome.cpp` (304) | L259-261, L276-278 `uiSetWidthPercent`/`uiSetSize`; L158 `try_get_mut<UiFlex>` | `core/window_chrome` + `wp` on instance; class height |
| `widget/Scroll.cpp` (339) | L122, L154, L215 `marginT = -offset` (negative margin as scroll) | Second negative-margin hack. Add a layout-engine `scrollY` offset field, drop `marginT` use |
| `widget/Virtual.cpp` (263) | L37 `uiSetWidthPercent`; L220-221, L232-239, L250-255 host/spacer flex | `virtual_host` and `virtual_spacer` blueprints; dynamic Y/height stay behind a sanctioned setter |
| `widget/VirtualGrid.cpp` (466) | L66-76, L224-254 cell/icon/label size, `self=Stretch` | `grid_cell.json` parts carry `self`/`grow`; sizes from classes |
| `widget/TreeView.cpp` (206) | L46 `uiSetWidthPercent`; L49-57 heights | `"wp":100` in `tree_row.json` |
| `widget/Menu.cpp` (575) | L152, L191-194 percent/height; L147, L208 `spawn(...UiFlex{})` | `menu_item.json` `wp`; separator/item via blueprint |
| `widget/Splitter.cpp` (453) | L408-417 grip flex incl. `self=Stretch`, `marginT/L=inset` | `splitter_grip` blueprint; thickness in class |
| `widget/Docking.cpp` (216) | L24-43 `shrink/grow/width/height` per orientation | `dock_panel_h` / `dock_panel_v` classes swapped at relayout |
| `widget/Image.cpp`, `AssetSlot.cpp`, `Button.cpp`, `Search.cpp` | `uiSetSize`, `uiSetGrow`, `uiSetWidth` | instance `w`/`grow` keys or class `w/h` |
| `kit/Color.cpp` (703) | L350-351 content-measured width; L591 `uiSetWidthPercent`; L628-636 flat build | `"w":"fit"`; `color_hex.json` `wp`; blueprint |
| `kit/ColorPopup.cpp` (258) | L154 `uiSetGrow(bodyId,1,1)` | `grow` on body part |
| `host/PropBind.cpp`, `PropImportMake.cpp`, `PropImportPaint.cpp`, `PropImportInputs.cpp`, `PropFilter.cpp`, `PropGrid*.cpp`, `Launcher.cpp` | dozens of `uiSetWidthPercent`/`uiSetGrow`/`uiSetSize` and local `UiFlex` | `core/column`/`core/row` blueprints; `Launcher` becomes `assets/ui/launcher.json`. `PropImportPaint.cpp:47-72` is the legitimate JSON-to-flex applier and stays. |
| `demo/app/Shell.cpp`, `MatRows.cpp`, `MatSlot.cpp`, `HudMaterial.cpp`, `HudView.cpp`, `demo/Sample.cpp` | hand-built strips/rows | migrate into `shell.json` / `material.json` |

Legitimate dynamic exemptions (documented, kept behind named setters, not banned): Popover/Menu anchoring, Splitter pane sizes, virtual slot Y, Shell preview animation.

### Category 2: Drawing hardcode, magic offsets and thresholds
| File | Magic values to move |
|---|---|
| `widget/FieldDraw.cpp` (185) | `+6/-12` selection inset; caret `1.5` width, `+7/-14`; IME `-10`, h `2`, alpha `0.9`; selection alpha `0.45`, radius `2`; blink `1.6`; `lineH 18` fallback; `<0.4f` |
| `widget/Image.cpp` (159) | `kMapW 72`, `kMapH 48`, `kMargin 8`, `120x90` (all already in `minimap` class: drift); zoom `0.85`/`1.18`; `0.08` min view |
| `widget/Check.cpp` (240) | `22` box, `10` dot, `32` pad (class says 36), `40`/`8` compact, `1.5`/`1`/`3` strokes and radius, `1`/`2` glyph inset |
| `widget/Chrome.cpp` (304) | `6` resize band, `220x120` min, `280x180` default, `40` guard, `16` drag slop |
| `widget/Docking.cpp` (216) | `0.28/0.72` triggers, `0.34/0.66` hint fractions, `0.35`/`0.95` alpha, radius `4` |
| `widget/Slider.cpp` | `8`, `2`, `4`, `0.7`, `16/14/4` thumb |
| `widget/Number.cpp`, `NumberEdit.cpp` | slop `3`, step px `8` vs `6` (disagree), fill `0.85` |
| `widget/Gradient.cpp`, `Curve.cpp` | `24` bands, `0.35/0.3`, `4/8/0.9/2`, `16` steps |
| `widget/Menu.cpp` | `64/16/32` screen gaps, `0.4f` |
| `widget/VirtualGrid.cpp`, `Virtual.cpp`, `TreeView.cpp`, `Rubberband.cpp`, `Search.cpp`, `AssetSlot.cpp` | `84`, `22`, `0.30s`, `16`, `36` (class says 28), `160`, `25` slop, `180` (class says 160) |
| `kit/Color.cpp`, `ColorDraw.cpp`, `ColorPopup.cpp` | `+20`, `24/48`, `6/8` gaps, `6/4/10` pads, `10/6` pad/gap (class says 12/8), third copy of `pushBar` |
| `ui/PrimitiveBatch.cpp` | minimap backing `0.02`, frame `2`/`0.95` |
| `host/Launcher.cpp`, `PropBind.cpp`, `PropFilter.cpp`, `Host.hpp` | `kCardW 560`, `kRowH 44`, `kHexW 96`, `kChoiceW 160`, chrome `40`/`6` duplicating `UiLook` |

`pushBar` exists three times (`FieldDraw.cpp:14-32`, `Docking.cpp` `push`, `ColorDraw.cpp:16-32`). `Check.cpp:246-266` also has raw `UiPrimitive` pushes duplicated in `Panel.cpp` and `PrimitiveBatch.cpp`.

### Category 3: Raw palette surgery
`Field.cpp:121-128` (`fieldMark`), `FieldDraw.cpp` (70, 101, 111, 127-157), `Check.cpp:203-209,233-255`, `Docking.cpp` `dockPlace` (`bar.r/g/b=accent[0]`), `Curve.cpp` (`ar/tr`), `Slider.cpp:137-145`, `Number.cpp:104`, `Gradient.cpp:82-84`, `Menu.cpp:61-63`, `Thumb.cpp:40-42`, `Calendar.cpp:163-165` (`accent[1]`), `Splitter.cpp:315`, `host/Launcher.cpp:110`, `PropGrid.cpp:112`, `PropImportMenu.cpp:102`, `PropImportPaint.cpp:129-139`, `PropGridVec.cpp:68`, `ColorPopup.cpp:42` (background sampling: acceptable, route through `tokenRgb`).

### Category 4: Raw `spawn()` and imperative `UiFlex`
- Raw `spawn`: `DragDrop.cpp:43-48` (`ensureMark`), `Menu.cpp:147,208`, `Splitter.cpp` (via `uiPanel`), `Virtual.cpp:219-258`, `Color.cpp:625-638`.
- `UiFlex` locals and `try_get_mut<UiFlex>`: `PropGrid*.cpp`, `PropFilter.cpp`, `PropBind.cpp`, `PropImportMake/Paint/Inputs.cpp`, `Launcher.cpp`, `demo/app/MatSlot/MatRows/Shell.cpp`, plus widget files listed in Category 1.
- Engine internals that legitimately keep `spawn`: `Node.cpp`, `Panel.cpp:13` (`uiPanel`), `Template.cpp:836`, `UiBin.cpp`, `Motion.cpp`.

## 2. Schema and Blueprint Additions

### 2.1 Engine/schema extensions (Phase 1)
1. `kUiTemplateCap` 32 to 48.
2. Allow `wp` / `hp` on the blueprint root.
3. `bleed: "x"|"y"|"xy"` flex attr: child ignores parent padding on that axis (kills Fold negative margins).
4. `w: "fit"`: content-measured width (replaces `uiChoiceFit` flex writes and `Color.cpp:351`).
5. Layout-engine scroll offset (`scrollY`) so `Scroll.cpp` stops using negative `marginT`.
6. A `popup` trait (sets `UiPopup`) so `drag_mark` needs no post-spawn component add.
7. A `UiStyleKey` addition set (about 12, within the 128 cap), each with an `uiStyleKeyName` entry in `StyleSheet.cpp`:
   `lineH`, `caretInset`, `imeOffset`, `imeH`, `imeAlpha`, `selectionInset`, `blinkRate`, `zoomIn`, `zoomOut`, `trigger`, `hintFrac`, `lineAlpha`, `steps`, `bands`.
   Reuse the existing unused keys `caretW`, `selectionAlpha`, `trackH`, `thumbSide`, `dragSlop`, `hitSlop`, `headerA`, `hoverAlpha`, `inner`, `iconInset`.
8. Token-valued keys (`fillBg`, `markBg`, `hoverBg`, `errorBg`, `lineBg`, `handleBg`, `trackBg`, `thumbBg`, `todayBg`) parsed like `bg` / `borderBg` in `StyleParse.cpp`.
9. New `UiToken` entries `Accent1`, `Accent2` (calendar today, field error), plus `ToolActive` if `Menu.cpp:61` needs it.
10. Shared helpers (SSOT): `uiBarPush(...)` (replaces 3 `pushBar` copies), `emitIcon(...)` in `PrimitiveBatch` (replaces raw `UiPrimitive` pushes in `Check.cpp`/`Panel.cpp`), a `UiLook` line-height accessor (replaces `glyphH>1?glyphH:18` in 8 places), and constants `kUiBorderMinW` / `kUiInvisibleAlpha` used everywhere instead of `0.4f` / `0.02f`.

### 2.2 New blueprints (`assets/ui/core/`, add to `_manifest.json`)
```json
// core/window_chrome.json
{ "role":"cluster", "class":"chrome_cluster",
  "flex":{"dir":"row","align":"center","justify":"center","shrink":0},
  "parts":[
    {"use":"core/panel","name":"min","class":"chrome_button","traits":["hoverable"]},
    {"use":"core/panel","name":"max","class":"chrome_button","traits":["hoverable"]},
    {"use":"core/panel","name":"close","class":"chrome_close","traits":["hoverable"]}],
  "slots":{"min":"min","max":"max","close":"close"} }

// core/crumb_item.json
{ "role":"crumb", "flex":{"dir":"row","align":"center","shrink":0},
  "parts":[{"use":"core/button","name":"seg","class":"crumb"},
           {"use":"core/mark","name":"sep","class":"crumb_sep"}],
  "slots":{"seg":"seg","sep":"sep"} }

// core/drag_mark.json
{ "role":"drag_mark", "class":"drag_mark", "flex":{"shrink":0}, "traits":["nohit","popup"] }
```
Also add `core/column` / `core/row` percent variants if the existing ones lack `wp`, plus `splitter_grip`, `virtual_host`, `virtual_spacer`, `dock_panel`, `launcher` (as a layout JSON, not a blueprint).

Consequences to handle: `Breadcrumbs.cpp` `crumbAt` and the `kids += 2` stepping assume the old child layout, so update them together with `crumb_item`. `Chrome.cpp` keeps runtime `uiSetIcon` for the three icons.

### 2.3 `assets/ui/styles.json` additions
New classes: `image_viewer` (`minView`, `zoomIn 0.85`, `zoomOut 1.18`), `dock_zone` (`trigger 0.28`, `hintFrac 0.34`, `alpha 0.35`, `lineAlpha 0.95`, `radius 4`, `bg "accent"`), `drag_mark` (`border 1`, `w 1`, `h 1`, absolute), `gradient`, `number_field`, `grid`, `list`, `tree`, `launcher_card/row/date`, `prop_filter`.
Extended classes:
- `input_field`: `lineH 18`, `caretW 1.5`, `caretInset 7`, `selectionAlpha 0.45`, `selectionInset 6`, `imeOffset 10`, `imeH 2`, `imeAlpha 0.9`, `blinkRate 1.6`, `errorBg`, `textBg`, `hintBg`, `clearBg`.
- `checkbox` / `check_box`: `padL`, `inner 10`, `markInset 1`, `borderRadio 1.5`, `compactW 40`, `markBg`, `markActiveBg`, `hoverBg`.
- `window_float`: `minW 220`, `minH 120`, `w 280`, `h 180`, `dragSlop 4`, `hitSlop 6`, `minDragW 40`.
- `slider`: `trackBg`, `fillBg`, `thumbBg`, `trackAlpha 0.7`, `thumbPad 4`, `minDraw 8`.
- `curve`: `lineBg`, `handleBg`, `steps`.
- `cal_day`: `todayBg`.

Drift to reconcile first, taking the sheet value as authority where it exists: `checkbox.padL` 36 vs 32; `search_field.w` 160 vs 180; `list.rowH` 28 vs 36; `color_flat.pad/gap` 12/8 vs 10/6; Number drag 8 vs 6 px; `minimap` duplicated in `Image.cpp`; `UiLook.titleBar/resizeBorder` vs `Host.hpp` chrome 40/6; `Motion.cpp` reveal 0.18 vs `UiLook.motion` 0.14. Where the code value is the intended one, update `styles.json` and note it in the commit message.

## 3. Widget and Draw-Hook Refactoring Specs (before and after)

- **FieldDraw.cpp**: before: local `pushBar`, `accent[0]`/`text`/`muted` indexing, literals `6/12/7/14/1.5/10/18`. After: `uiBarPush` plus `paintToken` colors; every metric via `uiStyle(s,"input_field",UiStyleKey::X)`, cached once per draw into a stack struct. The `lineH` fallback goes through the `UiLook` accessor.
- **Field.cpp `fieldMark`**: replace `paint->br = accent[2][0]` with `input_field.errorBg` / `borderBg` token resolution via `paintFromClass`.
- **Fold.cpp**: before: reads parent padding, sets `marginL=-left`, `marginR=-right`, calls `uiSetWidthPercent`. After: `fold_header` declares `"wp":100,"bleed":"x"`; `fold_clip` declares `"wp":100,"overflow":"hidden"`; open/close height state lives only in `uiReveal`/`uiFoldClose`. Zero margin writes in `Fold.cpp`.
- **Image.cpp**: drop `kMapW/H/Margin`, `120x90`, zoom literals. `photoMinimap` takes the resolved `UiPaintStyle` (the `minimap` class) and `image_viewer` supplies zoom/min-view.
- **Check.cpp**: `clearRow` becomes `paintFromClass("checkbox")` (it already has `bg:"text"`, `hoverAlpha:0`) with no `paint->r/hr/ha` writes. `adornCheck` reads `check_box.w`, `inner`, `padL`, `borderRadio`; the raw `UiPrimitive` block moves to `emitIcon`.
- **Chrome.cpp**: `core/window_chrome` blueprint; all `220/120/280/180/16/6/40` read from `window_float`; no `uiSetWidthPercent`/`uiSetSize`.
- **Docking.cpp**: `dock_zone` metrics; `dockPlace` pushes via `uiBarPush` with token color; `dock_panel` orientation handled by class swap.
- **Breadcrumbs.cpp**: `core/crumb_item` per segment; trail takes `grow` from its instance or the parent bar blueprint; no `uiSetGrow`.
- **Choice.cpp**: `uiChoiceFit` shrinks to menu-width sync only; `self/grow/shrink/w` are declared on the layout node (`"w":"fit"`).
- **DragDrop.cpp**: `ensureMark` becomes `uiSpawnTemplate(s,kUiNone,"core/drag_mark")`; `px(1,1)` and `borderW` come from the class.
- **Splitter.cpp / Virtual.cpp / Menu.cpp / Color.cpp**: raw `spawn`/`UiFlex` replaced with blueprints. `Virtual` keeps only its dynamic Y/height writes behind one named setter.

## 4. Color.cpp Decomposition (703 LOC to under 250 each)

`ColorInternal.hpp` already exists. Add prototypes for `setRange`, `writeHex`, `paintColor`, `syncColor`, `dragWheel`, `readHex`, `readInt`, `colorOnValue`, `slideChannel`, `slideR/G/B/A/H/S/V`, `colorOnDrag`, `colorOnEdit`, `colorGrab`, `pickerOwns`, `setBtnLabel`. Put internals in an anonymous namespace or mark them `static` where they do not cross files.

| New file | Contents | Target LOC |
|---|---|---|
| `ColorRamps.cpp` | `colorRampPart`, `writeHex`, `setRange`, `paintColor`, `dragWheel` and wheel/value math, `applyColor`, `syncColor` | about 130 |
| `ColorHex.cpp` | `readHex`, `readInt`, `colorOnValue`, `slideChannel`, `slideR/G/B/A/H/S/V`, `colorOnEdit`, `colorOnDrag`, `colorGrab` | about 170 |
| `ColorPicker.cpp` | `pickerOwns`, `pickerFor`, `hostPicker`, `colorBind`, `publishColor`, `setBtnLabel` (now `"w":"fit"`), `refreshColorChrome`, `colorHead`, `placeColorMenu`, `fillColorBody`, `uiBuildColor`, `loadColorBin` (blueprint), `uiInstallColorBin` | about 230 |
| `ColorQuery.cpp` | `uiColorFind`, `uiColorSetTarget`, `uiColorCollect`, `uiColorAt` | about 50 |

`Color.cpp` is deleted. `BH_UI_WIDGET_SOURCES` is a GLOB_RECURSE, so it picks the new files up, but add them to `BH_UI_BLUEPRINT_SOURCES`. Re-check the split line by line against the real file before moving code, since the ranges came from a read-only pass.

## 5. CMake Firewall Rule

Existing infrastructure in `CMakeLists.txt`: `bh_banned(rule pattern files...)` (about line 141) matches the pattern anywhere in the file text, and `BH_UI_BLUEPRINT_SOURCES` is an explicit list. The existing `UiFlex [A-Za-z0-9_]+\\{` ban misses `UiFlex box =` and `UiFlex{}`.

Add (enable only in Phase 8, after the migrations land, or configure fails):
```cmake
file(GLOB_RECURSE BH_PURGE_SOURCES core/ui/widget/*.cpp core/ui/kit/*.cpp core/ui/widget/*.hpp core/ui/kit/*.hpp)
bh_banned("negative margin hack; use bleed / scroll offset"  "margin[LRTB] *= *-"            ${BH_PURGE_SOURCES})
bh_banned("raw spawn in widgets; use uiSpawnTemplate"         "[^A-Za-z_]spawn\\("             ${BH_PURGE_SOURCES})
bh_banned("raw palette indexing; use UiToken"                 "look\\.(accent|text|muted)\\["  ${BH_PURGE_SOURCES})
bh_banned("raw paint color write; use paintToken"             "paint->(r|g|b|br|bg|bb|hr|hg|hb|ha) *=" ${BH_PURGE_SOURCES})
bh_banned("fill helpers are banned; declare grow / wp"        "fillWidth|fillHeight"           ${BH_PURGE_SOURCES})
bh_banned("raw UiFlex construction"                           "UiFlex [A-Za-z0-9_]+ *(=|\\{)|UiFlex\\{" ${BH_PURGE_SOURCES})
```
Documented exemptions go on a short allowlist (`Virtual.cpp`, `Splitter.cpp` pane size, `Popover.cpp`, `Menu.cpp` anchoring), so they are visible rather than silently skipped. `core/host/` gets the `spawn`/`UiFlex`/palette rules too, with `PropImportPaint.cpp` allowlisted as the JSON applier. Because `bh_banned` matches raw text, comments quoting these patterns also trip it; reword such comments.

## 6. Phased Execution Roadmap

Gate for every phase: `cmake --build build-core --target BurnhopeTest BurnhopeEngine -j` with **0 warnings**, then (separately, per `burnhope-test-run` memory) `stdbuf -oL ./build-core/BurnhopeTest`, with at least **171 passing tests** and no regressions. Goldens (`tests/ui/golden`) may only change when the visual change is intended and reviewed. One commit per phase.

1. **Foundations**: raise `kUiTemplateCap` to 48; the schema extensions in 2.1 (`wp` on root, `bleed`, `w:"fit"`, `popup` trait, scroll offset, new `UiStyleKey` and token keys); `UiLook` line-height accessor; shared `uiBarPush` / `emitIcon`; new tests in `TestUiTemplate` / `TestUiStyle` for each extension.
2. **Drift reconciliation and styles.json**: reconcile the drift list, add all classes and keys from 2.3. The `TestUiStyle` source scan must pass before any consumer code changes.
3. **Category 3 (palette)**: Field, FieldDraw, Check, Slider, Number, Gradient, Curve, Thumb, Menu, Calendar, Splitter, host files, switched to token keys. Goldens expected unchanged.
4. **Category 2 (metrics)**: FieldDraw, Image, Check, Chrome, Docking, then the remaining widgets, all through `uiStyle`. Image minimap dedupe and `pushBar` dedupe land here.
5. **Category 1 (layout)**: Fold (`bleed`), Choice (`fit`), Breadcrumbs plus `crumb_item`, Chrome plus `window_chrome`, TreeView / Menu / Virtual / VirtualGrid / Docking / Splitter, Scroll offset, then host and demo (`Launcher` to `launcher.json`, `Shell`/`Mat*` to JSON).
6. **Category 4 (spawn)**: DragDrop plus `drag_mark`, Menu, Splitter, Virtual, Color loaders.
7. **Color.cpp split** (4 files, each under 250 LOC; the split is behavior-neutral and does not change goldens).
8. **Firewall**: add the section 5 rules, fix any stragglers, verify configure passes from a clean build dir.
9. **Final sweep**: re-run all four grep families repo-wide (expect only allowlisted hits), confirm no file over 1000 LOC, delete the stale `PLAN_DECLARATIVE_WIDGETS.md` deletion from the working tree if not already committed.

## 7. Verification
- Per phase: the gate above. Phase 8 also needs a fresh `cmake -S . -B build-fw` that configures cleanly.
- Final: run the four grep families from section 1 and check that every remaining hit is on the allowlist.
- Visual: launch the app (`run` skill) and check Inspector, Material page, File browser, Color picker, a floating window, docking hints, a breadcrumb bar, and a Fold section in the property grid against pre-change screenshots, with F5 style reload working.
