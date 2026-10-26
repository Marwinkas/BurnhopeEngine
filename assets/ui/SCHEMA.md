# assets/ui schema

Four kinds of file, each with one job:

| File | Holds |
| :--- | :--- |
| `theme.json` | Colour palettes (`default`, `ink`, `warm`): the `UiLook` tokens. |
| `styles.json` | The design system: every number and token a widget uses, as named classes. |
| `core/*.json` | Blueprints: the structure of one widget (parts, internal flex, text, traits). |
| `editor.json`, `inspector.json`, `material.json` | Layouts: where widgets sit on a page. |

Press F5 in the running editor: theme, sheet, blueprints and every layout are re-read from disk.

## styles.json

```
{ "tokens":  { "space": { "md": 8 }, "ctl_h": 26 },
  "classes": { "name": { "extends": "other", "h": "$ctl_h", "gap": "$space.md", "bg": "muted" } } }
```

- A number may be written `"$name"` or `"$group.name"` to read a token. An unknown token fails the load.
  `"radius": "$theme.radius"` / `"$theme.radiusSm"` follows the active palette instead of a fixed number.
- `extends` copies an earlier class first; the class's own keys then win. A sheet holds at most 192 classes; names are at
  most 23 characters; the 128-bit key mask limits the key list (`core/ui/StyleSheet.hpp`, `UiStyleKey`, `uiStyleKeyName`).
- Token-valued keys: `bg` (the fill), `activeBg` (the fill while `uiSetActive(node, true)`), `borderBg` (the border).
  Names: `window dock title side content button field menu text muted item accent close chrome slider check preview`.
- The sheet is the only copy of these numbers. `uiStyle(state, "class", UiStyleKey::Key)` reads it; a missing class or
  key is a bug (BH_ASSERT in debug). `TestUiStyle` scans `core/` and `demo/` and fails when code names a class or
  blueprint that does not exist, and `TestUiGolden` fails when a layout, the colour picker or the calendar moves.
- A load error leaves the previous sheet in place and is logged with the class and key. `uiStyleLoadOver` lays a document
  over the loaded sheet (tests, tools); F5 reloads the whole asset.
- Classes that are only read by code (`ui`, `minimap`, `menu`, `tree`, `grid`, `scroll_bar`, `splitter_grip`, ...) use the
  same keys with the meaning their widget documents; classes a blueprint names also size and paint the node.

## core/*.json blueprints

`assets/ui/core/_manifest.json` lists them (`"core/button"` is `assets/ui/core/button.json`). Load order matters only
for `use`. A blueprint:

```
{ "role": "button", "class": "btn_default",
  "flex": { "dir": "row", "align": "center", "justify": "center", "gap": 4, "pad": 2, "shrink": 0 },
  "text": { "align": "center", "valign": "center", "ellipsis": "end", "wrap": false, "leading": 1 },
  "traits": ["hoverable", "focusable", "clip"],
  "parts": [ { "name": "icon", "class": "...", "flex": { "grow": 1, "self": "stretch", "wp": 100 } },
             { "use": "core/button", "name": "day", "class": "cal_day", "count": 7 } ],
  "slots": { "icon": "icon" } }
```

- The root's `flex` may hold `dir align justify wrap gap shrink pad*`. Sizes live in the class; placement (`grow`, `self`,
  `w`, `h`, margins, position) belongs to whoever spawns the blueprint. A part below the root may also carry `grow self
  wp hp`, because its own blueprint places it.
- The class is read when the blueprint is spawned, so a sheet reload shows on the next spawn. `uiSpawnTemplate(state,
  parent, "core/button", "ghost_button")` lays a second class over the first.
- `traits`: `hoverable focusable text_input pressable draggable nohit pass titlebar resizable clip scroll visible`;
  widgets register more with `uiTemplateTrait`.
- `use` (first key) copies an earlier one-part blueprint into the part; `count` repeats a part with its children.
- `slots` name parts (`uiTemplateSlot(state, template, root, "icon")`). Part `i` of a blueprint becomes node `root + i`.
- A broken blueprint fails the whole load, keeps the previous registry and logs `file:line:col: message`.

## theme.json

`{ "themes": { "ink": { "window": [0.07, 0.07, 0.08], "accent0": [...], "radius": 3 } } }`. A theme starts from
`UiLook{}` and overrides what it lists (colour keys: `window title side content button field menu close text muted item
preview status slider check chrome tool toolHover toolActive accent0..2`; numbers: `radius radiusSm motion hover`).

## Layout files

`{ "<name>": [ node, ... ] }`, loaded with `propImportAsset(state, parent, asset, "<name>", ...)`. A node is an object:

- `role` (required unless `use` is given): `section fold group slider swatch vec2 vec3 menu panel sep scroll split filter
  host label button menubutton icon slot choice number range list spacer check segment preview pick`, or a host role.
- `use`: a blueprint (`"use": "core/button"`) spawned in place of a role; the node then supplies `label`, `icon`,
  `class` (laid over the blueprint's), `name`, `command` and the placement keys (`grow shrink w h wp hp minW minH posX
  posY absolute`).
- Identity and text: `name label caption command glyph class paint icon photo items showWhen`.
- Layout: `dir` (`row`, `column`, `col`) `align self` (`center`, `start`) `w h wp hp minW minH grow shrink gap pad padL padR
  padT padB posX posY inset absolute wrap`.
- Behaviour: `bind cat owner kind group slot channel cell value value2 value3 min max step g b on reset bar slide hide
  center ghost pass fill proportional vertical iconbtn`.
- `children`: an array of nodes.
- `class` copies `h w minW pad padL padR gap` from a style class onto fields the node leaves out; on `button`, `icon`,
  `slot` roles it also picks the button look (`tool_button`, ...).
- `paint`: `hidden dock content sep line glyph accent veil`; `dock content sep line accent` read the `paint_*` classes.
- Keys may appear in any order. An unknown role, key, class or enum word, or a value of the wrong type, fails the
  import and logs `file:line:col: message`.
