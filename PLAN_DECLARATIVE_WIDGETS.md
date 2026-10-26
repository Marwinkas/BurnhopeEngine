# PLAN_DECLARATIVE_WIDGETS.md — Data-Driven Blueprint Templates for BurnhopeEngine UI

> On approval, step 0 of execution is to copy this file verbatim to `/home/marwinka/Музыка/BurnhopeEngine/PLAN_DECLARATIVE_WIDGETS.md` (plan mode only permits writing this plan file). No engine code has been touched.

## 0. Context

Layout, padding, alpha and colour decisions for widgets are made in C++ (`Button.cpp`, `Panel.cpp`, `Hover.cpp`, …) instead of in `assets/ui/*.json`. Three consequences: (1) children dictate parent policy (`fillWidth`, `grow`, `shrink`); (2) magic numbers duplicate `styles.json` (`StyleSchema.cpp` kDefaults, `UiMetrics`, `kUiTriggerH`); (3) a visual tweak needs a rebuild instead of F5. Goal: widgets own only intrinsic content; everything else is a blueprint template plus a style class, resolved from JSON, F5-reloadable.

Audit scope actually covered (three read-only passes): `core/ui/` top level (44 files), `core/ui/widget/` (**65** files / 9,091 LOC, not 52), `core/ui/kit/` (5 files / 1,641 LOC), `core/ui/rhi/` (8 files / 1,143 LOC) → **122 files**. Items marked *(sampled)* were grep-audited, not read line by line; the first task of Phase 0 is to complete them.

### Key audit findings that shape the design

1. **Infrastructure already exists and is reusable.** `styles.json` has tokens + 47 classes with `extends`; `UiStyleSheet` is a flecs singleton with hash-sorted binary search (`StyleSheet.cpp`); F5 → `canvasReload` (`rhi/Canvas.cpp:148`) re-loads the sheet then re-runs the app builder; `propImportAsset` + `spawnRole` (`host/PropImportRoles.cpp:121`) is the single dispatch point for JSON → nodes; goldens (`tests/ui/TestUiGolden.cpp`) dump every node box.
2. **No template/prototype concept exists.** `extends` copies numbers only. `applyClass` (`PropImportParse.cpp:128`) copies just 7 of 25 `UiStyleKey`s onto a node; widgets read the rest via `uiStyle()` imperatively. Only 8 classes are used by widget code (`choice, dropdown_trigger, fold_body, fold_header, input_field, tool_icon, btn_default, btn_ghost`).
3. **Hard limits to lift:** `UiStyleClass.has` is a 32-bit mask (25 used → widen to 64-bit); `kUiStyleCap = 64` (47 used → 128); `Node` role/class name buffers (16/24 chars).
4. **Firewall:** `core/ui/*` may not include `ui/widget`, `ui/kit`, `host/`. Therefore the template registry/instantiator lives in `core/ui` (Layer 1B, widget-agnostic); widget-specific attach logic stays in `core/ui/widget` (via existing `uiRegisterSpawnHook`/`PropRole`) and composite import in `host/`.
5. **Duplicated truth to delete:** `StyleSchema.cpp` kDefaults (~300 values), `UiMetrics` (`Metrics.hpp`), `kUiTriggerH` (`Style.hpp:42`), `kUiButtonIcon` (`Primitive.hpp:17`), `kUiToolIconSide` (`Style.hpp:294`), `kUiToolActive`, 150 lines of RGB in `uiLookInk/uiLookWarm`, `Disabled 0.45` in both `Hover.cpp:49` and `Panel.cpp:138`.
6. **Pre-existing inconsistency to fix, not copy:** default `shrink` is 1 in `UiFlex`, 0 in `Json.cpp:157` and `px()`, and `FlexAttr.cpp:172` forces `shrink=1` when `grow` is set. Templates must state `shrink` explicitly; one default wins (decision: `UiFlex` default, importer matches).

---

## 1. The 3-Pillar Architecture (adapted to the code as it is)

| Pillar | Artefact | Notes |
|---|---|---|
| 1 Blueprints | `assets/ui/core/*.json` (14 files) | Intrinsic structure of a widget: parts, internal flex, text defaults, traits, default class. **No** extrinsic keys (`grow`, `shrink`, `self`, `w:"100%"`, margins) allowed — enforced by the template parser (error `file:line:col`). |
| 2 Style catalog | `assets/ui/styles.json` | All metrics, tokens, hover/disabled alphas, icon insets, shadow, radii. Classes grow from 47 → ~110. New `UiStyleKey`s listed in §3.2. |
| 3 Thin C++ | per-widget `~20–30 LOC` | `uiSpawnTemplate` + bind label/icon/callbacks. Zero float math, zero `paint->r=`. |

**Honesty clause on "<30 LOC":** this binds the *spawn/layout* function of each widget. Behaviour engines that are not layout (Field text editing 772+548 LOC, Splitter drag math, Virtual pool, TreeView, Menu popup placement) keep their event/edit logic; only their layout/paint literals leave. Target is stated per file in §2.

**The Law (enforced by a new `bh_firewall` regex, Phase 7):** in `core/ui/widget/**` and `core/ui/kit/**` the tokens `fillWidth|fillHeight|widthMode|heightMode|\.grow|\.shrink|\.padL|\.padR|paint->r|paint->g|paint->b|paintGhost|paintToolIcon|paintButton` are build errors.

---

## 2. The Audit Matrix (122 files)

Legend: **A** extrinsic leaks · **B** hardcoded layout/pad/content-guessing · **C** magic constants · **D** colour surgery · **E** wrappers. `—` = grep-clean. `*` = sampled. LOC from `wc -l`. "Target" = post-migration state.

### 2.1 `core/ui/` substrate (44)

| File | LOC | A | B | C | D | E | Target |
|---|---|---|---|---|---|---|---|
| Panel.cpp | 246 | `uiSetGrow` 66, `uiSetSize` 39, `uiAddWidth` 82 | `uiSetPad` 55, `uiSetGap` 75 | icon `fit>18?fit-4:14` 185, clamp 12/72 186, labeled icon 14/16 195, inset −6 201, ghost test `a<=0.02&&border<0.4` 184, shadow dy `sh*0.15` / size `+sh*2` / α `0.42` / r `+sh` 140-144, disabled `0.45` 138, border α 179 | icon ink pick 214-216, `uiSetAlpha` 102, `uiSetToken` 15 | `uiSetSize/WidthPercent/Pad/Grow/Gap/AddWidth/Spacer/SetAlpha/SetIcon/SetToken`, `uiStyleWidth` | emit reads resolved `UiPaintStyle` (icon side/inset/shadow/disabled); all wrappers deleted |
| Panel.hpp | 52 | decls 27-37 | | | | wrapper decls | shrinks to `uiPanel` + `uiSpawnTemplate` |
| Hover.cpp | 70 | | `kidHot` walk 14-25 | `0.45` 49, `0.08` 61, `0.12` 65, `0.28` 66 | `r/g/b+=lift` 62; `hr/hg/hb/ha` 37-58 | | read `hoverLift/hoverAlpha/disabledAlpha` from paint style; `kidHot` → precomputed flag in `UiNodeVis` |
| Hover.hpp | 13 | — | | | | | unchanged |
| Node.cpp | 865 | `uiPlace` w=100%+shrink0 524, `uiSetWidth` 570, `uiFlexLimit` 589, `uiFlexShrink` 601, `uiCenterChildren` self=Center+margins 642 | `uiRowIndent` 553 | `100.0f` 524 | `stamp` 85, restyle token loop 187, `uiSetPaint` 357, `uiPaintRgb` 773, `uiBlur` 318, `uiFilter` 344 | `uiSetHeight/Width/FlexLimit/FlexShrink/RowIndent/PlaceAbsolute/Place/Grow/CenterChildren/SetPaint/PaintRgb/Stamp` | keeps `spawn`, names, restyle; setters removed; adds `uiSpawnTemplate` hook |
| Nodes.hpp | 112 | decls 48-57 | | | `uiPaintRgb`… | same list | trimmed |
| Style.hpp | 395 | | `kUiTriggerH=26` 42 | `UiPaint` default border 49-51; `paintRaised` b2/s7/lift .34 222; `paintSunken` b2/s3/×.35 231; axis colours 268; tool r4/icon20/idle .75,.78,.84/active 1,.55,.10 293-297; `uiLookInk/Warm` ~150 lines RGB 310-395 | `paintGhost` 284, `paintToolIcon` 299, `paintTint`, `paintPopup` 259, `paintButton`, `paintField`, `paintLabel` | `paintX` family | RGB → `assets/ui/theme.json` palettes (loaded by `StyleTokens`); `paintX` deleted; `UiPaint` keeps POD only |
| Metrics.hpp | 52 | | | whole table duplicates styles.json (buttonH 28, fieldH 36, rowH 28, …, ghostA .88, selectedA .92, headerA .95) | | | deleted; consumers use `uiStyle()` |
| Text.cpp | 616 | | baseline `em*fit*0.84` 143; centre `*0.5` | luma `.30/.59/.11`+`0.72` 246 (dup in ui.slang:247), ellipsis `tracking*3` 361 | `textInk` luminance pick 245 | | luma/threshold → one `UiStyleKey::InkThreshold`; shader reads same value from FrameView |
| Text.hpp 100, Typography.hpp 143 | | — | `kUiLineCap=8` | `kUiFontGlyphs 192`, `fontPx 32` | | | unchanged (fixed engine format) |
| PrimitiveBatch.cpp | 548 | | minimap placement 232 | `kMapW 72 kMapH 48 kMargin 8`, backing `.02,.02,.03,.9 r3` 240, `kSide=12` 496 | bar icon `iconR/G/B` 505 | | minimap/bar icon → style classes `minimap`, `bar_icon` |
| PrimitiveBatch.hpp 81, Primitive.hpp 147, Geometry.hpp 41 | | — | | `kUiUnset=-1`, `0.5` eps | | | `kUiButtonIcon` (Primitive.hpp:17) removed |
| Bar.hpp | 77 | | | | per-bar RGBA | | tokens, not RGBA |
| Motion.cpp | 204 | | `uiReveal` writes height/minH 32 | `kRevealSec .18`, sliver `<4` 164 | `uiMotionTo` copies paint r..a 66 | `uiMotionTo` | durations/ease → `motion` style class; anim target = token |
| Flex.hpp | 175 | | `px()` sets shrink=0 158; `uiPadOf` implicit rule 163 | `shrink=1` default | | `px()` | `px()` removed; single `shrink` default |
| FlexAttr.cpp / .hpp | 247 / 57 | `grow` ⇒ `shrink=1` 172 | strcmp ladder | | | | ladder → table-driven schema (shared by templates + PropImport) |
| LayoutBridge.cpp / .hpp | 514 / 39 | `applyFlex` 101-205 | `minW/minH` forced 0 160-166; pad-vs-sides 171 | | | | stays; sole `UiFlex→Yoga` site; template instantiation feeds it |
| Interaction.hpp | 118 | | | `kUiCap 2048`, `kUiCmdCap 128` | | | add `UiTemplateRef` POD |
| State.hpp | 343 | | | | | hook lists | add template registry singleton (like `UiStyleSheet`) |
| Json.cpp | 411 | `shrink=0` default 157 | hand-parsed keys 240-255; `flexToBin` | | | | shares schema table with templates |
| UiBin.cpp / .hpp | 326 / 138 | `grow/shrink` copy 97 | | clear-role zeroing 171 | | `uiSetPass` | templates bake to `.uib` in a later phase (optional) |
| StyleSheet.cpp / .hpp | 122 / 99 | | | caps 64/64/24 | `popup*` from `window_panel` 132 | | caps 128; `has` → 64-bit; new keys |
| StyleSchema.cpp | 107 | | | ~300 built-in values mirroring styles.json | | | **deleted**; `styles.json` embedded via `UiBin`/`fileMapReadOnly` fallback; parity test becomes "sheet loads" |
| StyleParse.cpp 210, StyleTokens.cpp 111, StyleInternal.hpp 33 | | — | | | | | extended: `theme` palettes, `states` block |
| Focus.cpp 73, Hit.cpp 112, Input.cpp 222, Command.* 53/49, Icon.* 74/48, IconNames.inc 31, UiWindowBridge.* 54/19, Types.hpp 11, NodeDump.cpp 112 | | — | Hit.cpp:101 click-through rule | `0.02` α; Icon.cpp:25 enum→svg switch | | | Icon switch → `IconNames.inc` table (data); others unchanged |
| ui.slang | 353 | | | luma `.30/.59/.11`, `.55`, ink `.94,.94,.92` 247; radii 6 144; halfW 3 91 | shader tint 249 | | constants come from `FrameView`/paint style, not literals |

### 2.2 `core/ui/widget/` (65)

| File | LOC | A | B | C | D | E / API leak | Target |
|---|---|---|---|---|---|---|---|
| **Button.cpp/.hpp** | 239/45 | `fillHeight` 19, `fillWidth` 27 (+`grow=1`), `shrink=0` 16, Px 34/40; Cfg `fillWidth/fillHeight` | icon-only width guess 36-43; pad zeroing 62-68 & 102-110; duplicated empty-label test 58/84; text style hardcoded 25 | `kUiButtonIcon` 59, `kUiToolIconSide` 118/151, `a=0` 136/183, `plate=1` 123 | `paintButton` 46, `paintGhost` 77, `r/g/b=look.text` 79, tint 131-136, mark token 230 | `uiIconBtn 90, uiSlotBtn 98, uiButtonTintIcon 126, uiToolFrame 142, uiToolButtonState 157, uiButtonMark 220, uiButtonFitText 174, uiButtonTextWidth 165, uiButtonIconChild 188`, `adornButton/adornClose` (identical) | **≈25 LOC**: `uiButton(s,parent,{.cls,.label,.icon,.onClick})` |
| Label.cpp/.hpp | 25/12 | `shrink=0` 10 | | | `paintLabel` 11 | | `core/label` template, ~8 LOC |
| Tip.cpp/.hpp | 48/10 | Px 8×8, `shrink=0` 12 | `pad=6`, `+16`, min w 24, `glyphH+12` | `shadow 10`, `border 1` | `br/bg/bb=muted` 21 | | class `tooltip` |
| Search.cpp/.hpp | 64/12 | `width<0`→100%+grow 27 | height from `metrics.searchH`; fallbacks 180/160 | | `paintField` | `width` sentinel | `core/field` + class `search_field`; `width` param removed |
| **Field.cpp/.hpp** | 772/183 | 100%/Px/`shrink=0` 373 | clear btn w=22 ×4 (166,460,498,678); `lineH` fallback 18 | | border `br/bg/bb` from accent/muted 120 | positional `uiField(…)` + `uiBindFieldExtra` | layout/paint part ≈25 LOC; edit engine stays but `input_field`/`input_field_error` states from sheet |
| FieldDraw.cpp | 185 | | offsets 6/7/10; caret 1.5 | α .45/.9 | `bar.r/g/b` 23, ink 155, caret/selection colours | | colours via `selection`/`caret` tokens |
| FieldFilter.cpp 176*, TextEdit.cpp 548* | | | `lineH` 18 (343,500), `view=8` 366 | | | | magic → `Field` style keys |
| Number.cpp/.hpp, NumberEdit.cpp | 150/27, 204 | `shrink` **parameter** (hpp:18), Px 77 | height 30 78; dead-zone ±3; step fallbacks | shadow 4, border 2, fill α .85 | `br/bg/bb=text` 84, fill `muted` 134 | `width,height,shrink` params | `core/field` variant `number_field`; params dropped |
| **Slider.cpp/.hpp** | 219/21 | Px/grow0 / Percent 38-47, `shrink=0` | | inset 2, track 4, thumb 14 (h−4) , α .7/1 | `body.a=0`… 48; colours 150-158 | `width` sentinel | **≈25 LOC**; track/thumb dims = `slider` class keys |
| Check.cpp/.hpp | 250/24 | 100%/Px 130 | `padL 36 padR 8` 134; content guess `padL<32→32` 56 | side 22, dot 10, offsets 8/40, 1.5, r3 | `paintGhost` 42, `r/g/b` 43-49, edge 73 | `kind` int encodes variant | `core/checkbox` with variants `checkbox/radio/toggle` classes |
| Choice.cpp/.hpp | 219/38 | Px/grow0/shrink0/minW0 100,193 | `width>1` sentinel 61,101; re-sets pad/gap after build 199 | | | `width=0 ⇒ class` | `core/choice`; class `choice`, `dropdown_trigger` |
| Segment.cpp/.hpp | 77/33 | `shrink=0` | gap 2 | itemW 56 itemH 24 in Cfg | | | class `segment`, `segment_item` |
| Fold.cpp/.hpp | 205/27 | clip/body/header 100%/Px/shrink0 (57-69,166) | `padL=iconPad+icon+gap` 174; open heuristic `>8` | rot 90 | icon 132, token 188 | | `core/fold` with parts `header, clip, body` |
| Tabs.cpp/.hpp | 25/9* | | | | | | `core/tabs` |
| Scroll.cpp/.hpp | 340/42 | | thumb x 8, w 6 | min thumb 18, wheel 28, r2, α .95 | `r/g/b=accent[0]` 254 | | class `scroll_bar` |
| Splitter.cpp/.hpp | 451/42 | pane `uiFlexShrink` 80; reads grow/heightMode to guess fixed pane 26,44,177; grip `shrink=0` 406 | `base 64`, `kHit 8`, `minSize 48` | grip thickness 2, α .9/.62/.55 | `paintToken` swaps 297-326 | | class `splitter_grip` + `states.hot`; drag math stays |
| Popover.cpp/.hpp | 60/9 | | margins 4 | `kBarMaxH 64` | | | class `popover` |
| Breadcrumbs.cpp/.hpp | 152/20 | grow1/shrink1 42, Auto/Px 50 | pad 4, gap 2, h 30, seg w `measure+8` | sep `px(10,14)` | `paintLabel` | | `core/breadcrumb`, classes `crumb`, `crumb_sep` |
| AssetSlot.cpp/.hpp | 129/48 | grow1/shrink1 21; frame Px 54 | `padR = onClear?36:8` (reserved space) | h28 gap4 side−2, clear 18/28×24 | token+rgb 62, 99 | | class `asset_slot`, clear button = child template `tool_button` |
| Thumb.cpp/.hpp, Image.cpp/.hpp | 46/11, 163/19 | Px, `shrink=0` | | r4, view .08, minimap 72×48, zoom .85/1.18 | `a=0`, `r/g/b=text`, `paintTint` | | classes `thumb`, `image` |
| Virtual.cpp/.hpp | 264/166 | 100%/Px 29, host grow1/shrink1 221, spacer 233 | `itemH>4?itemH−4` 32,137; fallbacks 36/48/160/16 | | `paintToken(Item)` 35 | | `core/list` row template |
| VirtualGrid.cpp/.hpp | 499/72 | `shrink` on glyph/label/sub 72-98 | per-mode pad zeroing 265; align/justify by `rows` 262; magic `-4 -gap +2` 253; list icon 22 | click thr 16 | glyph rgb 82; `paint->a=selectedA/headerA` 299 | 14-field Cfg | `core/grid` with `cell_tile`/`cell_row` templates; mode = choose template |
| TreeView.cpp/.hpp | 237/37 | 100%/Px 35, caption grow1 75 | `itemH−2/−6` 38,67,193 | | `borderW=0 shadow=0`, token 60 | | `core/tree` row template |
| Menu.cpp/.hpp | 623/48 | separator/item/menu/trigger sizing 142-232,386,578 | align/justify, `padL 8`, width 220, clamp 4, anchor 8, `32+2*pad` 411 | `kUiMenuMinW 96`, shadow, α 0 | `kUiToolActive` 57, `paintPopup` 42, `paintButtonFill` 588 | `uiMenuStyle(width,itemH,pad,gap)` | `core/menu` (+`menu_item`, `menu_sep`); placement math stays |
| Chrome.cpp/.hpp | 352/50 | window/row/bar sizing 29-329 | pad 10 gap 6; row 28/4/8; bar pad 8; min sizes 220×120, 280×180 | btns `px(28,28)`, mark 22×22 | tokens + icon 62-68,296,330 | raw panels instead of `uiButton` | `core/dialog` (window+titlebar) — window controls become `tool_button` instances |
| Docking.cpp/.hpp | 215/31 | edge panel Px/100% 28 | hit zones .72/.28, hint .34/.66 | α .35/.95, r4 | `bar.r/g/b=accent` 125 | | class `dock_hint` |
| DragDrop.cpp/.hpp | 314/48 | | ghost width `letters*7+…` 289 | `kGhost*` ×8 (15-22) | `br/bg/bb=muted` 89 | | class `drag_ghost`; width via measure |
| Rubberband.cpp/.hpp | 152/25 | | | `px(1,1)`, α .25, r2, b2 | `r/g/b=accent` 50 | | class `rubberband` |
| Curve.cpp/.hpp, Gradient.cpp/.hpp | 112/11 ×2 | Px, `shrink=0` | h 120 / 36 | handle 8 / 10, 24 bands, r6 | accent/text read | | classes `curve`, `gradient` |
| Widgets.cpp/.hpp, BinSlots.hpp, Calendar.hpp, ColorPicker.hpp, Mark.hpp, Range.hpp, CurveData.hpp, GradientData.hpp | 71/9, 27, 38, 113, 17, 26, 14, 18 | * | * | * | * | `uiWidgetsInit` registers bin roles | `Widgets.cpp` → registers *templates* + attach hooks |

(Widget dir has 65 files vs. the 52 in the request; the 13 extras are `.hpp` pairs/`CurveData`-style headers and are folded into the rows above.)

### 2.3 `core/ui/kit/` (5)

| File | LOC | Findings | Target |
|---|---|---|---|
| Color.cpp | 800 | `fillColorBody` 555-660 hand-builds picker tree from `UiFlex` (wheelRow 220/gap 10, side gap 8, swatch 72, num field 24, hex 32, pad 10/gap 6, label `len*advance+20` 349); `colorInk().r/g/b` model writes (OK, model not paint) | `core/color_picker.json` template; C++ keeps model/hit logic; **<350 LOC** (split rule) |
| ColorInternal.hpp | 41 | `kColorSide/Wheel/Val/Gap/Pad/Title/W/H` (96/220/22/10/12/36/…/634) | constants → class `color_picker*`; header deleted |
| ColorPopup.cpp | 270 | root sized `kColorW/H`; pad/gap 8; `samplePixel` `0.15915494f` | popup = `dialog` template; trig constants → `std::numbers::inv_pi/2` |
| ColorDraw.cpp | 110 | `bar.r/g/b` 23; white handles 14/8/6 r7/4/2 56-81; `6.2831853f` 53; ramp paint recolour 61,75 | handle dims → class `color_handle`; colour via `Ink`/`Custom` token paint |
| Calendar.cpp | 420 | panel pad 10 gap 6 r8; header 32/8; nav btn `n*adv+24`×28; week hdr 20/4, cell 32×18; rows 32/4, day 32×28; selected `paint->r=accent[1]` 162 | `core/calendar.json` with `day_cell` class; selected = state `selected` |

### 2.4 `core/ui/rhi/` (8)

| File | LOC | Findings | Target |
|---|---|---|---|
| UiRenderer.cpp / .hpp | 252/36 | clear colour `0.102,0.106,0.122` 205 ignores `look.window` | use `look.window` token |
| Canvas.cpp / .hpp | 415/97 | font 18.0 (72); titleBar 40 / resizeBorder 6 fallbacks (113); hardcoded `"assets/ui/styles.json"` (148-161) | `canvasReload` loads `theme+styles+templates` manifest; defaults from sheet |
| Font.cpp/.hpp, UiPhoto.cpp/.hpp | 162/31, 116/34 | — | unchanged |

No widget knowledge leaks into the renderer (only descriptor-slot names).

### 2.5 External callers to migrate (from widget audit)
`uiButton` 25 sites (`demo/app/Shell.cpp` 18, `Launcher.cpp`, `PropFilter.cpp:109`, `PropImportMake.cpp:116`, `PropImportMenu.cpp:89`); `uiIconBtn` 5; `uiSlotBtn` 1; `uiToolFrame` 2; `uiToolButtonState` 2; `uiButtonFitText` 3; `uiButtonTextWidth` 2; `uiSetGrow/uiSetSize` in `PropBind.cpp:147,162,288,340`; `uiSpacer` in `Chrome.cpp:288,323`, `PropImportInputs.cpp:98`; raw `uiPanel` 66 sites.

---

## 3. Pillar 2 — Master Style Catalog (`assets/ui/styles.json`)

### 3.1 Schema additions
```json
{
  "theme": "burnhope_dark",
  "palettes": { "burnhope_dark": { "window": "#1A1B1F", "text": "#E0E3EA", "muted": "#8A90A0",
                "accent0": "#FF8C1A", "accent1": "#4C8DFF", "accent2": "#E5534B" } },
  "tokens": { "space.xs": 2, "space.sm": 4, "space.md": 8, "space.lg": 12,
              "radius.sm": 3, "radius.md": 4, "radius.lg": 6, "ctl_h": 26, "icon_btn": 28 },
  "classes": { "...": {} }
}
```
`palettes` replaces `uiLookInk/uiLookWarm` RGB literals (Style.hpp:310-395). Per-state overrides use a `states` sub-object keyed `hover|active|disabled|selected|error|focus`, each holding `bg/alpha/lift` only (no layout).

### 3.2 New `UiStyleKey`s (25 → 49; `has` mask → `uint64_t`, cap 128 classes)
`IconSide, IconInset, IconInsetLabeled, ShadowDy, ShadowAlpha, ShadowGrow, DisabledAlpha, HoverLift, HoverFallbackAlpha, HoverMinAlpha, Gravity (align), Justify, TextAlign, Ellipsis, Plate, BorderAlpha, Margin, MaxH, MinH, CaretW, SelectionAlpha, TrackH, ThumbSide, HandleSide`. Existing `HoverAlpha`/`HoverGain` are generalised to apply to every class (today only `prop_row`, `vector_row`, `menu_bar`).

Defaults for the new keys are the *current literals* so Phase 1 is a no-op visually: `DisabledAlpha .45`, `HoverLift .08`, `HoverMinAlpha .12`, `HoverFallbackAlpha .28`, `ShadowDy .15` (×shadow), `ShadowAlpha .42`, `IconInset 6`, icon rule `max(12, min(72, fit>18 ? fit−4 : 14))` becomes explicit `IconSide` per class (+ `IconSide: "fit-4"` expression allowed only as the single documented formula `fit-N`).

### 3.3 Class catalogue (new + migrated; values from the audit, existing values retained)
```json
"btn_default":   { "extends": "control", "pad": "$space.md", "padL": 12, "padR": 12, "h": "$ctl_h", "radius": "$radius.md", "bg": "Button", "iconSide": 16, "iconInsetLabeled": 6 },
"ghost_button":  { "extends": "btn_default", "bg": "Ghost", "plate": 0, "hoverAlpha": 0.28 },
"tool_button":   { "w": "$icon_btn", "h": "$icon_btn", "pad": 0, "radius": "$radius.md", "iconSide": 20, "bg": "Ghost",
                   "states": { "active": { "bg": "Accent" } } },
"slot_button":   { "w": 32, "h": 32, "pad": 0, "iconSide": 20, "radius": "$radius.md", "bg": "Ghost" },
"close_button":  { "extends": "tool_button", "w": 22, "h": 22, "iconSide": 14, "bg": "Close" },
"clear_button":  { "extends": "tool_button", "w": 22, "h": 22, "iconSide": 14 },
"spacer":        { "w": 0, "h": 0, "pad": 0 },
"input_field":   { "h": 36, "pad": 8, "radius": "$radius.md", "border": 1, "errorBorder": 1, "bg": "Field",
                   "caretW": 1.5, "selectionAlpha": 0.45, "clearIcon": 14,
                   "states": { "error": { "bg": "FieldError" }, "disabled": { "alpha": 0.45 } } },
"search_field":  { "extends": "input_field", "h": 26 },
"number_field":  { "extends": "input_field", "h": 30, "border": 2, "shadow": 4 },
"slider":        { "h": 18, "trackH": 4, "thumbSide": 14, "inset": 2, "bg": "Track" },
"checkbox":      { "h": 36, "padL": 36, "padR": 8, "iconSide": 22, "bg": "Ghost" },
"radio":         { "extends": "checkbox" },
"toggle":        { "extends": "checkbox" },
"choice":        { "...": "unchanged from sheet" },
"dropdown_trigger": { "...": "unchanged from sheet" },
"segment":       { "gap": 2, "h": 24 },
"segment_item":  { "w": 56, "h": 24 },
"fold_header":   { "...": "unchanged" },
"fold_body":     { "...": "unchanged" },
"splitter_grip": { "w": 2, "hit": 8, "min": 48, "states": { "hot": { "alpha": 0.9 } }, "alpha": 0.55 },
"scroll_bar":    { "w": 6, "inset": 8, "minThumb": 18, "wheel": 28, "radius": 2, "alpha": 0.95 },
"tooltip":       { "pad": 6, "shadow": 10, "border": 1, "minW": 24, "bg": "Menu" },
"popover":       { "margin": 4, "maxH": 64 },
"menu":          { "minW": 96, "w": 220, "pad": 4, "gap": 2, "shadow": 10, "bg": "Menu" },
"menu_item":     { "h": 30, "padL": 8, "padR": 10 },
"menu_sep":      { "h": 1 },
"window_panel":  { "...": "unchanged (veil)", "pad": 10, "gap": 6, "minW": 220, "minH": 120 },
"title_bar":     { "h": 32, "padL": 8, "padR": 4, "gap": 8 },
"crumb":         { "h": 22, "pad": 4 },
"crumb_sep":     { "w": 10, "h": 14 },
"asset_slot":    { "h": 28, "gap": 4, "padL": 8, "padR": 8 },
"thumb":         { "radius": 4 },
"list_row":      { "h": 36, "bg": "Item" },
"tree_row":      { "h": 28, "bg": "Item", "indent": 16 },
"cell_tile":     { "w": 84, "h": 96, "pad": 0, "gap": 4 },
"cell_row":      { "h": 28, "pad": 8, "gap": 6, "iconSide": 22, "states": { "selected": { "alpha": 0.92 }, "header": { "alpha": 0.95 } } },
"curve":         { "h": 120, "radius": 6, "handleSide": 8 },
"gradient":      { "h": 36, "radius": 6, "handleSide": 10 },
"dock_hint":     { "alpha": 0.35, "radius": 4, "hoverAlpha": 0.95 },
"drag_ghost":    { "pad": 6, "iconSide": 16, "gap": 6, "radius": 4, "maxW": 180, "minW": 52 },
"rubberband":    { "alpha": 0.25, "radius": 2, "border": 2, "bg": "Accent" },
"color_picker":  { "pad": 12, "gap": 10, "w": 258, "h": 634 },
"color_wheel":   { "w": 220, "h": 220 },
"color_value":   { "w": 22, "h": 220 },
"color_handle":  { "w": 14, "h": 14, "radius": 7 },
"calendar":      { "pad": 10, "gap": 6, "radius": 8 },
"day_cell":      { "w": 32, "h": 28, "states": { "selected": { "bg": "Accent" } } },
"minimap":       { "w": 72, "h": 48, "margin": 8, "radius": 3, "alpha": 0.9 }
```
Existing 47 classes remain; `StyleSchema.cpp` kDefaults is removed, so every class above must exist in the shipped sheet (checked by a test: every `uiStyle(cls,…)` string literal in C++ resolves — a grep-based CTest in Phase 7).

---

## 4. Pillar 1 — Blueprint Catalog (`assets/ui/core/*.json`)

### 4.1 Template schema
A template is `{ "role", "class", "flex"?, "text"?, "traits"?, "parts"? , "slots"? }`.
- `flex`: **intrinsic only** — `dir, align, justify, wrap, gap, pad*` (and only when not already supplied by the class). Banned: `grow, shrink, self, w:"N%"`, `margin`, `posX/posY`, `absolute` (parser error).
- `text`: `align, valign, ellipsis, scale, wrap` → `UiText`.
- `traits`: Flecs tags: `hoverable, focusable, pressable, clip, scroll, resizable, row_hover, text_input, draggable`.
- `parts`: ordered child skeleton `{ "name", "role", "class", "flex"?, "text"?, "traits"? }`; `parts[0]` is the root's first child. `slots` names the parts the C++ attaches into (`content`, `header`, `body`).
- Instantiation parameters: `class` (override), `label`, `icon`, `name`; nothing else.
- Placement (`grow`, `w:"100%"`, `self`) is given by the **instance** node in layout JSON: `{ "use": "core/button", "label": "Apply", "grow": 1 }` — the one place extrinsic keys remain legal.

### 4.2 Drafts (all 14)
`assets/ui/core/button.json`
```json
{ "role": "button", "class": "btn_default",
  "flex": { "dir": "row", "align": "center", "justify": "center" },
  "text": { "align": "center", "valign": "center", "ellipsis": "end" },
  "traits": ["hoverable", "focusable", "pressable"] }
```
`field.json`
```json
{ "role": "field", "class": "input_field",
  "flex": { "dir": "row", "align": "center" },
  "text": { "align": "left", "valign": "center", "ellipsis": "end" },
  "traits": ["hoverable", "focusable", "text_input"],
  "parts": [ { "name": "clear", "role": "button", "class": "clear_button", "traits": ["hoverable", "pressable"] } ] }
```
`slider.json`
```json
{ "role": "slider", "class": "slider",
  "flex": { "dir": "row", "align": "center" },
  "traits": ["hoverable", "focusable", "draggable"] }
```
`checkbox.json`
```json
{ "role": "check", "class": "checkbox",
  "flex": { "dir": "row", "align": "center" },
  "text": { "align": "left", "valign": "center", "ellipsis": "end" },
  "traits": ["hoverable", "focusable", "pressable"] }
```
`choice.json`
```json
{ "role": "choice", "class": "choice",
  "flex": { "dir": "row", "align": "center", "justify": "start" },
  "text": { "align": "left", "valign": "center", "ellipsis": "end" },
  "traits": ["hoverable", "focusable"],
  "parts": [ { "name": "chevron", "role": "icon", "class": "choice_chevron" } ] }
```
`fold.json`
```json
{ "role": "fold", "class": "fold_body", "flex": { "dir": "column" },
  "parts": [
    { "name": "header", "role": "button", "class": "fold_header", "flex": { "dir": "row", "align": "center" },
      "text": { "align": "left", "valign": "center", "ellipsis": "end" }, "traits": ["hoverable", "pressable"] },
    { "name": "clip", "role": "panel", "class": "fold_clip", "traits": ["clip"],
      "parts": [ { "name": "content", "role": "panel", "class": "fold_body", "flex": { "dir": "column" } } ] } ],
  "slots": { "content": "clip/content", "header": "header" } }
```
`splitter.json`
```json
{ "role": "splitter", "class": "splitter_grip", "flex": { "dir": "row" },
  "traits": ["hoverable", "draggable", "resizable"] }
```
`scroll.json`
```json
{ "role": "scroll", "class": "scroll_view", "flex": { "dir": "column" }, "traits": ["scroll", "clip"],
  "parts": [ { "name": "content", "role": "panel", "class": "scroll_content", "flex": { "dir": "column" } },
             { "name": "bar", "role": "panel", "class": "scroll_bar", "traits": ["hoverable", "draggable"] } ],
  "slots": { "content": "content" } }
```
`tabs.json`
```json
{ "role": "tabs", "class": "tabs", "flex": { "dir": "column" },
  "parts": [ { "name": "strip", "role": "panel", "class": "tab_strip", "flex": { "dir": "row", "align": "center", "gap": 2 } },
             { "name": "page", "role": "panel", "class": "tab_page", "flex": { "dir": "column" } } ],
  "slots": { "strip": "strip", "content": "page" } }
```
`panel.json`
```json
{ "role": "panel", "class": "panel", "flex": { "dir": "column" } }
```
`dialog.json`
```json
{ "role": "window", "class": "window_panel", "flex": { "dir": "column" }, "traits": ["resizable", "draggable"],
  "parts": [
    { "name": "title", "role": "titlebar", "class": "title_bar", "flex": { "dir": "row", "align": "center", "gap": 8 },
      "text": { "align": "left", "valign": "center", "ellipsis": "end" }, "traits": ["draggable"],
      "parts": [ { "name": "close", "role": "button", "class": "close_button", "traits": ["hoverable", "pressable"] } ] },
    { "name": "content", "role": "panel", "class": "panel", "flex": { "dir": "column", "gap": 6 } } ],
  "slots": { "content": "content" } }
```
`tree.json`
```json
{ "role": "tree", "class": "side_panel", "flex": { "dir": "column" }, "traits": ["scroll", "clip"],
  "parts": [ { "name": "row", "role": "treerow", "class": "tree_row", "flex": { "dir": "row", "align": "center" },
               "text": { "align": "left", "valign": "center", "ellipsis": "end" }, "traits": ["hoverable", "row_hover"] } ],
  "slots": { "row": "row" } }
```
`grid.json`
```json
{ "role": "grid", "class": "grid_host", "flex": { "dir": "row", "wrap": true, "align": "start" }, "traits": ["scroll", "clip"],
  "parts": [ { "name": "tile", "role": "cell", "class": "cell_tile", "flex": { "dir": "column", "align": "center" },
               "text": { "align": "center", "ellipsis": "end" }, "traits": ["hoverable", "focusable", "draggable"] },
             { "name": "row", "role": "cell", "class": "cell_row", "flex": { "dir": "row", "align": "center" },
               "text": { "align": "left", "ellipsis": "end" }, "traits": ["hoverable", "focusable", "draggable"] } ],
  "slots": { "tile": "tile", "row": "row" } }
```
`menu.json`
```json
{ "role": "menu", "class": "menu", "flex": { "dir": "column" }, "traits": ["clip"],
  "parts": [ { "name": "item", "role": "button", "class": "menu_item", "flex": { "dir": "row", "align": "center", "justify": "start" },
               "text": { "align": "left", "valign": "center", "ellipsis": "end" }, "traits": ["hoverable", "pressable"] },
             { "name": "sep", "role": "panel", "class": "menu_sep" } ],
  "slots": { "item": "item", "sep": "sep" } }
```
Additional small templates reused by the above: `label.json`, `tip.json`, `crumb.json`, `calendar.json`, `color_picker.json` (Phase 6). The 14 requested files are listed first; these four are extras.

---

## 5. Template Loader Engine Design (`core/ui/Template.cpp/.hpp`, in `bh_ui_core`)

**Choice: pre-parsed archetype, not prototype-entity cloning.** Flecs entity clone copies all components and re-parents generic children; we already need Yoga node + `s.yoga[id]`/`parentOf`/`yogaCtx` bookkeeping that only `spawn()` performs, so a clone would be followed by fix-ups. A flat POD archetype instantiated through `spawn()` is O(parts) with no parsing, no allocation, trivially F5-reloadable and fits the SoA layout.

```cpp
struct alignas(16) UiTplPart {            // 64 bytes (static_assert)
    UiFlex      flex;                     // fully resolved, class values already merged
    uint16_t    parent;                   // index into parts[]; 0xFFFF = root
    uint16_t    role;                     // UiRole / bin-role id
    uint32_t    classHash;
    uint32_t    traits;                   // bitmask -> tags
    UiTextStyle text;
    char        name[16];
};
struct UiTemplate { uint32_t nameHash; uint16_t first, count; uint8_t slotIdx[4]; };
struct UiTemplateSheet {                   // flecs singleton next to UiStyleSheet
    UiTemplate tpl[32]; UiTplPart part[256]; uint16_t nTpl, nPart; };
```
- **API (header uses `ui/Types.hpp`, no State.hpp):**
  `bool uiTemplatesLoad(UiState&, std::string_view manifestAsset);`
  `uint16_t uiSpawnTemplate(UiState&, uint16_t parent, UiTemplateId, UiClass cls = {}); // kUiNone if cap hit`
  `uint16_t uiTemplateSlot(UiState&, uint16_t root, UiTemplateSlot);` // O(1) from `slotIdx`
- **Load:** `fileMapReadOnly` + `JsonCursor` (zero-copy), table-driven key schema shared with `FlexAttr`; atomic like `uiStyleLoad` (failure leaves previous sheet). Extrinsic key in a template ⇒ error `file:line:col`. Class values merged into `flex` at load, so spawn never calls `uiStyle` on the hot path; on `uiStyleLoad` re-run, templates re-resolve (called in the same `canvasReload` step).
- **Instantiate:** loop `part[first..first+count)`, `spawn(s, idOf[parent], flex, paint)`; set text style, tags via a trait→tag table; `idOf[]` is a stack array (`uint16_t[16]`). Zero heap, no `uiFindName` (names only for build-time lookup per Node.cpp rule).
- **Role behaviour:** `role` maps to existing `uiRegisterSpawnHook`/`PropRole`/`kRoleSpawns`; widgets attach callbacks after `uiSpawnTemplate` (stays in `core/ui/widget`, firewall-legal since widget includes core/ui, not the reverse).
- **Layout JSON integration:** `spawnRole` gains the key `"use": "core/<name>"` (and role names `button`, `field`… resolve through the template registry first, so existing `inspector.json` keeps working). Extrinsic keys on the instance node apply *after* instantiation to the root only.
- **Reload:** `canvasReload` order becomes `uiClear → uiThemeLoad → uiStyleLoad → uiTemplatesLoad → builder`. Manifest `assets/ui/core/_manifest.json` lists the files (no directory enumeration needed). A broken template keeps the previous registry and logs `file:line:col`.
- **Caps (documented, asserted):** 32 templates, 256 parts, depth 8; instantiation respects `kUiCap` (returns `kUiNone`).
- **Firewall:** `Template.*` includes only `core/io`, `ui/Flex`, `ui/Style`, `ui/StyleSheet`; added to `ui_core` source list in `CMakeLists.txt:361` by hand.

---

## 6. Target C++ Transformations

### 6.1 Button.cpp (239 → ~40 LOC incl. includes)
Before (excerpt): `fillHeight/fillWidth` → Percent/grow; icon-only guess `flex.width = Icon + padX*2; padL=padR=0`; `paintButton(s.look)` / `paintGhost` / `paint->r=look.text[0]`; plus 9 wrappers.
After:
```cpp
uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg) {
    const uint16_t id = uiSpawnTemplate(s, parent, kTplButton, cfg.cls);   // btn_default | ghost_button | tool_button | slot_button
    if (id == kUiNone) return kUiNone;
    uiSetText(s, id, cfg.label);            // Yoga measures text; no width math
    if (cfg.icon != UiIcon::None) uiSetIcon(s, id, cfg.icon);
    if (cfg.photo < kPhotoSlots)  uiSetPhoto(s, id, cfg.photo);
    uiOnClick(s, id, cfg.onClick, cfg.user);
    return id;
}
struct UiButtonConfig { UiClass cls{}; const char* label{}; UiIcon icon{}; uint8_t photo{255}; UiAction onClick{}; void* user{}; };
```
Removed: `fillWidth/fillHeight/width/height/padX/ghost`, `uiIconBtn/SlotBtn/ToolFrame/ToolButtonState/ButtonTintIcon/ButtonMark/ButtonFitText/ButtonTextWidth/ButtonIconChild`, `adornButton/Close`. Callers: icon-only → `cls = "tool_button"`; active state → `uiSetState(s,id,UiState::Active)` (flag consumed by `states.active` in the sheet); fit-text → default (Yoga auto width, `Width` absent from class); `fillWidth` → `"grow": 1` on the JSON instance or parent `align: stretch`.

### 6.2 Field.cpp (layout/paint part)
Before: `widthMode=Percent, width=100, heightMode=Px, shrink=0` (373); clear-button `width=22` ×4; border `br/bg/bb` from accent/muted.
After:
```cpp
uint16_t uiField(UiState& s, uint16_t parent, const UiFieldConfig& c) {
    const uint16_t id = uiSpawnTemplate(s, parent, kTplField, c.cls);       // input_field | search_field | number_field
    if (id == kUiNone) return kUiNone;
    uiFieldInit(s, id, c);              // buffer, filter, placeholder, maxChars, readOnly, password (data only)
    uiSetClearButton(s, id, c.clear);   // toggles the template's "clear" part hidden/visible
    return id;
}
```
Error state: `uiSetState(id, Error)` → sheet `states.error`; `FieldDraw` reads `caretW/selectionAlpha/ink` tokens from the resolved paint style rather than `look.*`. Edit/undo engine unchanged.

### 6.3 Hover.cpp (70 → ~35)
Before: `a *= 0.45f`; `lift = look.hover > 0 ? look.hover : 0.08f; r+=lift…`; `if (a < 0.12f) a = 0.28f`; `kidHot` walk.
After:
```cpp
void uiHoverTint(UiState& s, uint16_t id, UiPaint& p, const UiPaintStyle& st) {
    const float t = uiHoverAmount(s, id);            // 0..1, from UiNodeVis.hotMask (precomputed in uiCacheVisibility)
    if (s.disabled(id)) { p.a *= st.disabledAlpha; return; }
    p.a = std::max(p.a, st.hoverMinAlpha ? lerp(p.a, st.hoverFallbackAlpha, t) : p.a);
    p.rgb += st.hoverLift * t;                       // saturate in shader via UiPaint flag
}
```
`UiPaintStyle` (POD, 32 B) is resolved once per node at restyle time (Node.cpp restyle loop) from class + state, so Hover/Panel never read `s.look` or literals.

### 6.4 Panel.cpp `uiEmitPanel` (246 → ~120)
Before: `fit>18 ? fit-4 : 14`, clamp 12/72, `box.w - side - 6`, shadow `sh*0.15f / 0.42f`, disabled `0.45`, ghost test `a<=0.02&&border<0.4`.
After: `const UiPaintStyle& st = s.style[id];` → `side = st.iconSide`, `x = box.x + box.w - side - st.iconInset`, shadow `{dy = st.shadowDy, a = st.shadowAlpha, grow = st.shadowGrow}`, `ghost = st.plate == 0`. All arithmetic of "fit" is replaced by the single documented expression `iconSide: "fit-4"` pre-evaluated at layout. Wrappers `uiSetSize/Pad/Grow/Gap/AddWidth/Spacer/SetAlpha/SetIcon/SetToken` deleted (spacer = template `panel` + class `spacer`; sizing = class or instance JSON).

### 6.5 Slider.cpp (219 → ~60 incl. draw)
Before: Px/Percent branch on `width`, `shrink=0`, `body.a=0; borderW=0; shadow=0`, literals 2/4/14/.7, `look.muted/accent/text` reads.
After:
```cpp
uint16_t uiSlider(UiState& s, uint16_t parent, const UiSliderConfig& c) {
    const uint16_t id = uiSpawnTemplate(s, parent, kTplSlider, c.cls);
    if (id == kUiNone) return kUiNone;
    uiSliderBind(s, id, c.value, c.min, c.max, c.onChange, c.user);   // data + callback only
    return id;
}
// draw: track/thumb dims and colours from s.style[id] (trackH, thumbSide, inset, tokens)
```
No `width` parameter: caller declares `"w": 120` or `"grow": 1`.

---

## 7. Phased Migration Roadmap

**Gate G (every phase, per CLAUDE.md §7):** `cmake --build build-core --target BurnhopeTest BurnhopeEngine -j && ./build-core/BurnhopeTest` — 0 warnings, 100 % pass; golden dumps unchanged unless the phase says otherwise (intentional diffs reviewed, then `BH_UPDATE_GOLDEN=1`); **live F5 check**: launch editor, edit one value in `styles.json` and one template, press F5, confirm change without rebuild and no error in log; `bh_firewall` configure clean; no file >1,000 lines, kit/app coordinators <350.

| # | Phase | Scope | Exit criteria |
|---|---|---|---|
| 0 | **Infrastructure, zero visual change** | Complete the sampled-file audit; widen `UiStyleClass.has`→64-bit, caps 128, add the 24 new keys with current-literal defaults; `Template.*` registry+loader; `core/*.json` stubs; `use` key in `spawnRole`; `uiThemeLoad` palettes (mirror current RGB); extend `canvasReload`. New tests `TestUiTemplate` (parse errors, extrinsic-key rejection, instantiate = golden-equal to hand-built, atomic failure). | goldens byte-identical; new tests green; F5 reloads templates |
| 1 | **Render-path constants** | `Hover.cpp`, `Panel.cpp` emit, `Text.cpp` luma, `PrimitiveBatch` minimap/bar icon, `ui.slang` constants, `UiRenderer` clear colour, `Motion` durations → `UiPaintStyle`/style keys. `kidHot` → `UiNodeVis`. | goldens identical; screenshot diff of editor ≤ 0; Hover/Panel literal-free (grep gate) |
| 2 | **Buttons** | `Button.cpp` rewrite; add classes `tool_button/slot_button/ghost_button/close_button/clear_button`; migrate 25+ call sites (`Shell.cpp`, `Launcher.cpp`, `HudView/HudAct/HudPick`, `PropImportMake/Menu`, `PropFilter`); delete wrappers & `fillWidth/fillHeight`; Chrome window controls reuse `uiButton`. | no `fillWidth` in tree; goldens identical (or reviewed diffs); toolbar F5-tweakable |
| 3 | **Inputs** | Field/Search/Number/Slider/Check/Choice/Segment layout+paint to templates; drop `width/shrink` params and `width<0` / `width>1` sentinels (callers use JSON `w`/`grow`); `FieldDraw` colours via tokens. | `TestPropImport`, `TestUiWidgets` green; inspector golden identical |
| 4 | **Containers** | Fold, Tabs, Scroll, Splitter (pane-policy guess removed: pane fixedness declared `"fixed": true` on the pane instance), Popover, Tip, Menu, Chrome/dialog, Docking. | Splitter drag tests green; menu placement unchanged |
| 5 | **Virtualized & composite** | Virtual, VirtualGrid (mode = choose template), TreeView, AssetSlot (`padR` reserve removed—clear button is an overlay child), Breadcrumbs, DragDrop, Rubberband, Curve/Gradient, Thumb/Image. | file-browser tests (`TestFileBrowser`) green |
| 6 | **Kits** | `Color.cpp` → `color_picker.json` (+ delete `ColorInternal.hpp` constants); `Calendar.cpp` → `calendar.json`. **Before starting add golden dumps of the current picker/calendar** (no coverage today) so parity is provable. | new goldens captured pre-change then identical; `Color.cpp` <350 LOC |
| 7 | **Purge & enforce** | Delete `Metrics.hpp`, `StyleSchema.cpp` kDefaults, `paintX` helpers, `uiSet*` wrappers, `kUiButtonIcon/ToolIconSide/ToolActive/TriggerH`; add `bh_firewall` rule from §1; add CTest that every `uiStyle("…")` literal resolves; add `SCHEMA.md` template section; unify `shrink` default (fix `Json.cpp:157`, `FlexAttr.cpp:172`). | grep for banned tokens = 0; docs updated; final full gate |

Rollback unit: each phase is one reviewable commit series on `dev2`; phases 2–6 are independent after 0–1 and can be reordered.

---

## 8. Risks & Decisions to Confirm

1. **Hover/paint-style resolve cost:** `UiPaintStyle` per node adds 32 B × 2048 = 64 KB SoA; resolved at restyle (build/reload), not per frame — preserves zero-hot-path-alloc.
2. **`has` mask and Node name buffers** (role 16 / class 24 chars): template/class names must fit; add static_asserts.
3. **Behavioural coupling hidden in "guesses":** Splitter fixed-pane inference, Fold open heuristic (`>8`), AssetSlot reserved `padR`, Check `padL≥32` — each needs an explicit declarative replacement (listed per phase); these are the likeliest source of golden diffs.
4. **Coverage gap:** no golden for colour picker, calendar, popups → Phase 6 prerequisite.
5. **Unsampled files** (`TextEdit`, `FieldFilter`, `Tabs`, `Widgets`, several `.hpp`) are audited in Phase 0 before their phases begin.

## 9. Verification (end-to-end)
- Per-phase gate G above; final: full `BurnhopeTest`, `BH_UPDATE_GOLDEN` not set, 0 warnings.
- Manual F5 script: edit `styles.json` (`tool_button.w 28→32`), `core/button.json` (`text.align`), and an instance `grow`; each must update on F5 with no rebuild; introduce a deliberate typo → previous sheet/registry retained and `file:line:col` logged.
- Grep gates (CI): banned-token regex in `core/ui/widget|kit`; no `uiStyle("literal")` unresolved; no source >1,000 lines.

---

## 10. Execution Log

### Phase 0 — done (build 0 warnings, 164/164 tests, goldens byte-identical)
Shipped: `UiStyleClass.has` is 64-bit, `kUiStyleCap` 128, 24 new `UiStyleKey`s (names ≤15 chars: the JSON key buffer is 16); `core/ui/Template.{hpp,cpp}` (registry singleton, atomic loader, trait table, `uiSpawnTemplate`, `uiTemplateSlot`); `assets/ui/core/{_manifest,button,field,choice,fold}.json`; `"use"` key in layouts (`PropImportMake.cpp:makeUse`); `canvasReload` loads templates right after the sheet; `tests/ui/TestUiTemplate.cpp` (9 cases).
Deviations from the plan above:
- `UiTplPart` is `alignas(16)` and holds a full `UiFlex`, so it is not 64 bytes; the registry singleton is ~60 KB (32 templates, 256 parts).
- Only the four templates whose classes already exist ship now. The rest arrive with the phase that introduces their classes, because every sheet class must still exist in `StyleSchema.cpp` until Phase 7 (`TestUiStyle` parity).
- Slots name a unique part (`"content": "body"`), not a path.
- Class values win over the blueprint's own `flex` block when both set the same key.
- Moved to Phase 1: `uiThemeLoad` palettes (they replace the `uiLookInk/Warm` literals that Phase 1 purges) and the line-by-line audit of the sampled files (`TextEdit`, `FieldFilter`, `Tabs`, `Widgets`, headers).
- Not verifiable headless: the live F5 reload check (needs the GPU window).

### Phase 1 — done (full build 0 warnings, 167/167 tests, goldens byte-identical)
Shipped: `UiPaintStyle` (Style.hpp) held once per `UiState` (`s.paintStyle`), refreshed by `uiStyleLoad` from the new `ui` and `minimap` classes (in `styles.json` and mirrored in `StyleSchema.cpp` until Phase 7). `Hover.cpp` (disabled alpha, hover lift, min/fallback alpha), `Panel.cpp` `uiEmitPanel` (disabled alpha, shadow dy/alpha/spread, icon fit/clamp/inset/left side), `Text.cpp` `textInk` (ink threshold, hover lift; luma weights are named Rec.601 constants), `PrimitiveBatch.cpp` minimap (size, margin, pad, alpha, radius, minimum box) and `UiRenderer` clear colour (now `look.window`, passed in by `canvasRecord`) no longer carry style literals. `kUiInvisibleAlpha` / `kUiBorderMinW` name the 0.02 / 0.4 thresholds (also used by `Hit.cpp`). `core/ui/Theme.{hpp,cpp}` + `assets/ui/theme.json` replace `uiLookInk` / `uiLookWarm` (deleted); `canvasReload` loads the theme first; `uiApplyLook`, `HudToolbar`, `Shell` and `Sample` read palettes by name. Tests: palette parity against a reference copy of the old literals, theme strictness, paint style following the sheet.
Deviations: the style is global per state, not per node (a per-node 32-byte style would add 64 KB for values that do not vary per node; per-node variation stays in `UiPaint`). Deferred: `kidHot` ancestor walk → `UiNodeVis` (a hover-time change, not a literal), `ui.slang` constants (needs a GPU struct change that cannot be verified headless), the `paintRaised/Sunken/Popup` literals in `Style.hpp` (deleted with the `paintX` family in Phase 7), `kRevealSec`/`Motion.cpp` (already a named constant).
Note: the Phase 0 build output I checked contained no compiler warnings, but the Phase 1 rebuild reported two in `Template.cpp` (an unused variable in release, a `snprintf` truncation); both are fixed. Treat the Phase 0 "0 warnings" as unverified.

### Phase 2 — done (full build 0 warnings, 168/168 tests, goldens byte-identical to the pre-phase dumps)
Shipped: `uiButton` is `uiSpawnTemplate("core/button", cls)` + label, glyph, photo and click (`UiButtonConfig{cls,label,iconSlot,icon,onClick,user}`); `fillWidth/fillHeight/width/height/padX/ghost`, the padding-zeroing and icon-only width guesses, `uiIconBtn`, `uiSlotBtn`, `uiToolFrame`, `uiToolButtonState`, `uiButtonTintIcon`, `uiButtonMark` and the `UiIconChrome` tag (no reader existed) are gone. `uiSetActive` (Panel) switches a node between the class's `bg` / `activeBg` tokens and the glyph ink; Tabs, Segment and the toolbar use it. New classes in `styles.json` (mirrored in `StyleSchema.cpp`): `btn_default` (now carries `shadow`, `bg`, `activeBg`), `ghost_button`, `tool_button` (+`_sm` 22, `_xs` 18), `menu_trigger`, `bar_button`, `crumb`, `title_close`, `asset_x`, `launcher_button`, `cal_day`, `segment_item`, `pick_button`, `demo_text/tool/tool_icon`; `btn_ghost` and `tool_icon` removed. New style keys `shadow`, `toolIcon`, class field `activeBg`; `UiLook` gained `tool/toolHover/toolActive` (the hard-coded tool ink constants, themable) and `UiPaint` the `idleToken/activeToken` pair. Callers migrated: PropImport (`button`, `icon`, `slot`, bar triggers), Segment, Breadcrumbs, AssetSlot, PropBind, Launcher, Calendar, Color, Shell, HudAct/HudView, Files.
Changes from the plan text: blueprints now resolve their class at spawn time (a sheet reload shows on the next spawn, no re-bake step), and a blueprint's flex may carry `shrink` (a control that must keep its content width); `uiWidgetsInit` loads the shipped theme, sheet and blueprints so every `UiState` can build buttons; `"fill": true` on a JSON button and the Color PICK button's vertical grow stay with the importer / the kit, which are the buttons' parents.
Kept on purpose: `uiButtonTextWidth` / `uiButtonFitText` (the menu triggers and the sample still size themselves by text; they go with the menu blueprint in Phase 4). A button with a declared JSON `w` no longer drops its side padding; the goldens show no box changes.
Not verifiable headless: how the rebuilt buttons look on screen (shadow, active accent, tool ink), and the live F5 reload.

### Phase 3 — done (full build 0 warnings, 168/168 tests, goldens byte-identical)
Shipped: `uiField`, `uiSearchField`, `uiNumber` (via `uiField`), `uiSlider` and `uiCheck` spawn blueprints (`core/field`, `core/search`, `core/slider`, `core/checkbox`); their `Percent 100` width, `width`/`height`/`shrink` parameters, the `width<0` and `width>1` sentinels, `s.metrics.{searchH,sliderH,checkH}` and the hard-coded `padL 36 / padR 8` are gone. Heights, borders, shadows, dim borders and the clear-button width (`clearW`, was `22` in four places) come from classes `input_field` (+ `input_box`, which only zeroes the Yoga padding because `UiField` insets its own text), `search_field`, `slider`, `checkbox`. New class keys `borderDim`, `clearW`; a class with `hoverAlpha` now makes its node hover to its own colour at that alpha (checkbox rows, menu triggers; the imperative hover block in the bar importer is gone). The dead `uiSpin` is deleted. Segment lost `itemW/itemH` in Phase 2.
Callers now say what they want: `uiNumber` sizing is a `uiFlexLimit` call by the parent (PropBind, PropGrid, MatRows), plain JSON `check`s and the Color sliders / hex field fill their row with `uiSetWidthPercent`, the property filter's search box fills the bar, a binary-layout search box keeps its own width (default 180, named constant).
Deferred to Phase 4: `Choice` (it is a `uiMenuButton` and sizes itself with `uiChoiceFit`; it moves with the menu blueprint). Left as is: the bin-loader `Check` path (`clearRow` / `adornCheck` act on raw bin nodes), `FieldDraw` / `Slider` draw code (they already read `look` tokens, no RGB literals), the `lineH` fallback `18` (font fallback).

### Phase 4 — done (full build 0 warnings, 168/168 tests, goldens byte-identical)
Shipped: **Fold** (`core/fold_header`, `core/fold_clip` with a `body` slot; the header counter-pads its container, the clip's reveal height stays code because it is animation state), **Chrome** (`core/panel`, `core/row`, `core/cluster` plus classes `window_float`, `chrome_cluster`, `chrome_button`, `chrome_close`, `fold_title`, `fold_toggle`, `title_bar`, `title_mark`; `uiTitleBar`'s `height` is now an override, default 0 = class), **Menu** (`core/menu`, `core/menu_item`, `core/menu_sep`, `core/menu_button`; classes `menu` with `w/rowH/pad/gap/popupMinW/shadow/offset/anchorGap/screenGap/flyoutW`, `menu_item`, `menu_sep`; `UiMenuMetrics` defaults, `s.metrics.menuShadow/menuPadX`, `kUiMenuMinW` and the 8/4/220 literals are gone), **Choice** (`core/choice` extends `dropdown_trigger`; `UiChoiceConfig::width` and its sentinels removed — callers size the choice), **Scroll / Splitter / Popover / Docking** metrics (`scroll_bar`, `splitter_grip`, `popover`, `dock_panel` classes; `uiCalcScrollMetrics` takes the minimum thumb), **Tabs** (`uiSetActive`). The dead **Tip** widget is deleted.
New engine pieces: `uiApplyTemplate` re-skins an existing one-part node (menu rows, separators), class `borderBg`, `$theme.radius` / `$theme.radiusSm` values that follow the active palette, `uiStyleBg`, a 128-bit `UiKeyMask` (the 64-bit mask was full), and `tools/schema_sync.py`-style regeneration of `StyleSchema.cpp` from `styles.json` (kept in the scratchpad, not in the repo; Phase 7 deletes the schema file anyway).
Deviations: menu buttons no longer fill their row on their own — the importer / `uiPropMenu` / tests do it (`fillRow`); `uiButtonTextWidth/FitText` are still used by the HUD view dropdown and the sample (their removal needs the view popup rebuilt around an auto-width trigger, left to Phase 7); a few menu-placement literals remain as named code constants (16 and 32 px list insets); Splitter/Menu *behaviour* (fixed-pane inference, flyout placement) is unchanged.
Test-harness note: redirecting `BurnhopeTest` to a file without line buffering got the process killed (exit 137) twice; run it as `stdbuf -oL ./build-core/BurnhopeTest`.

### Phase 5 — done (full build 0 warnings, 168/168 tests, goldens byte-identical)
Shipped: `UiMetrics` / `Metrics.hpp` is **deleted** — every reader (TreeView, VirtualGrid, DragDrop, FileBind, Files, Menu) now asks a class (`tree`, `grid`, `drag_ghost`, `menu`, `search_field`). Blueprints: `core/grid_cell` (icon / label / sub parts, `cell_*` classes; the cell's pad/gap come from `cell_tile`), `core/tree_row` (chevron / icon / label parts, `tree_*` classes), `core/crumbs` + `core/mark` (separator) with label-like `crumb` buttons whose width is now Yoga's text width + the class padding (the `measure + 8` and `22` px literals are gone), `core/asset_slot` (class `asset_slot_clear` replaces `padR = onClear ? 36 : 8`), `tex_frame` / `tex_clear` for the texture slot, `core/ghost` (drag ghost, its `kGhost*` constants are class values), `rubberband`, `curve`, `gradient` (heights, handle sides) on `core/panel`, `list_row` for pooled virtual rows. A part below a blueprint root may now carry `grow`, `self`, `wp`, `hp` (it is placed by its own blueprint, not by a foreign parent); new trait `nohit` and `visible`.
Left as is: `Thumb` / `Image` (their spawn is a single `uiPanel` whose paint needs a white photo tint on a Content token, which no class can say), the virtual **host** viewport (`uiVirtualHostCreate`, grow/shrink are the viewport's placement), the per-frame tile/row re-flex in `uiVirtualGridRefresh` (behaviour, now fed from classes), draw-time proportions in Curve / Gradient (the 0.35 / 0.3 band, ±4 px), `Field` / `Slider` draw code.

### Phase 6 — done (full build 0 warnings, 168/168 tests; new kit goldens match the pre-migration output byte for byte)
Prerequisite met first: `tests/ui/golden/kit_color_picker.txt` and `kit_calendar.txt` were captured from the hand-built kits before any change, and the migrated kits reproduce them exactly.
Shipped: **Calendar** is `core/calendar` (bar, today row, weekday header with `count: 7` labels, six `count: 7` week rows of `cal_day` buttons) and `uiBuildCalendar` only wires text, click targets and `UiCalDay` cells onto it; **Colour picker** body and head are `core/color_wheel_row` (wheel, value bar, side column with swatch and PICK), `core/color_line` ×7 (label, slider, number field), `core/color_hex`, `core/color_head`, the popup root / body are `core/panel` with classes `color_popup` and `color_body`; the `kColor*` constants and `ColorInternal.hpp` sizes are gone; `ColorDraw` marker sizes come from `color_marks`. New blueprint features: a part may `use` an earlier one-part blueprint (first key) and `count` repeats a part with its children; class key `ramp` (UiRamp of a colour-ramp panel); `core/label` (`uiLabel` is now a blueprint); the `titlebar` and `resizable` traits are registered by `uiInstallChromeBin`, and `uiWidgetsInit` loads theme / sheet / blueprints at its end so every trait exists first; `uiSliderInit` splits slider node setup from spawning, and `uiField` is `spawn + uiBindField`. Template caps: 512 parts, 8 slots.
Not done: `Color.cpp` is 727 lines (the plan asked for under 350; the remaining code is picker model, hit testing and HSV/RGB sync — splitting it into `ColorModel.cpp` is a mechanical follow-up); the bin-layout colour popup (`uiBuildColor`) still builds its own title row; calendar day colours (empty / selected / today / item) are still chosen by token in code (they are state, not style).

### Phase 7 — done with the exceptions listed (full build 0 warnings, 171/171 tests, goldens byte-identical)
Shipped: `StyleSchema.cpp` (the ~300 duplicated built-in class values) is **deleted** — `styles.json` is the only copy; `uiStyle` / `uiStyleResolve` read the loaded sheet, `uiWidgetsInit` loads the shipped sheet, theme and blueprints, `uiStyleLoadOver` lays a document over the sheet for tests and tools; `kUiStyleCap` is 192. `UiMetrics`, `Tip`, `uiSpin`, `uiSetAlpha`, `uiGrow`, `uiStamp`, `paintButton`, `paintToolIcon`, `paintSunken`, `paintField`, `paintButtonFill`, `uiIconBtn` & co. are gone. New guards: `bh_banned` in `CMakeLists.txt` fails configure when the removed API (`uiIconBtn uiSlotBtn uiToolFrame uiToolButtonState uiButtonTintIcon uiButtonMark UiIconChrome paintToolIcon fillWidth fillHeight UiMetrics StyleSchema`) reappears under `core/ui`, `core/host`, `core/files` or `demo`, and when any migrated widget builds a raw `UiFlex x{}` (list of 23 files); `TestUiStyle` scans `core/` and `demo/` for style-class and blueprint names and fails on any that the sheet or registry lacks. `assets/ui/SCHEMA.md` documents theme, sheet, blueprints and `use`.
Not done, with reasons:
- **Per-field placement setters** (`uiSetSize`, `uiSetWidthPercent`, `uiSetGrow`, `uiSetWidth`, `uiFlexLimit`, ...) still exist: ~100 call sites in the kits, importer and demo use them to place a child in a parent, which is exactly the "instance placement" the plan keeps outside widgets. Collapsing them into one placement struct is a mechanical follow-up, not a blueprint question.
- **Unifying the `shrink` default** (`UiFlex` 1, `Json.cpp` and `px()` 0, `grow` ⇒ 1 in `FlexAttr`): changing it moves layouts that only a GPU run can judge (the sample shell, binary layouts); blueprints and classes now state `shrink` explicitly where it matters.
- `UiBin`'s per-node raw paint (`paintRaised`, `Check`'s bin `clearRow` with `paintGhost`) and the generic JSON importer are baked-layout paths, left alone.
- Remaining raw `UiFlex` blocks: the virtual-list viewport (`Virtual.cpp`), the splitter grip (`Splitter.cpp`), the colour bin node (`Color.cpp: loadColorBin`); remaining `widthMode/heightMode/grow/shrink/padL/padR` reads in Splitter / Scroll / Menu / Field are behaviour (pane inference, reveal animation, caret layout), not style.
- ui.slang constants, the `kidHot` ancestor walk, a live F5 check and an on-screen look at every migrated widget: need the GPU window, which this environment cannot open.

### Incident log
While rewriting tests I truncated three files with a faulty one-line Python (`open(p,'w').write(open(p).read()...)` opens for writing first): `tests/tools/TestEditorTools.cpp`, `tests/ui/TestUiTemplate.cpp`, `core/host/PropGridMetrics.hpp`. All three were rebuilt: `TestUiTemplate.cpp` rewritten from this session; `PropGridMetrics.hpp` from the Write recorded in the earlier session transcript plus its later `propVecClick` declaration; `TestEditorTools.cpp` from `HEAD` plus the one regex edit recorded in the transcript (the four-argument `uiStyle(..., 28.0f)` fallbacks removed) plus this session's two changes (its diff against `HEAD` is 7 insertions / 6 deletions). If you had any other uncommitted edit in `TestEditorTools.cpp`, it is not in that file — compare with your editor's history.
