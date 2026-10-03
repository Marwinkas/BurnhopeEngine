#!/usr/bin/env python3
"""Bake a UI JSON tree into a flat BHUI blob. The frame never parses this JSON."""

import json
import struct
import sys

ROLES = {
    "panel": 0,
    "button": 1,
    "label": 2,
    "slider": 3,
    "check": 4,
    "field": 5,
    "close": 6,
    "scroll": 7,
    "hidden": 8,
    "progress": 9,
    "chrome": 10,
    "calendar": 11,
    "color": 12,
    "menu": 13,
    "image": 14,
}

DIRS = {"column": 0, "row": 1, "column-reverse": 2, "row-reverse": 3, "col": 0}

NODE = struct.Struct("<HBBh6H8B6B7f6f10f16f16B")
HEAD = struct.Struct("<IHH I")

JUST = {
    "start": 0, "center": 1, "end": 2,
    "between": 3, "space-between": 3,
    "around": 4, "space-around": 4,
    "evenly": 5, "space-evenly": 5,
}
ALIGN = {"stretch": 0, "center": 1, "start": 2, "end": 3, "baseline": 4}
CONTENT = {"stretch": 0, "center": 1, "start": 2, "end": 3, "between": 4, "around": 5, "evenly": 6}
SELF = {"auto": 0, "stretch": 1, "center": 2, "start": 3, "end": 4, "baseline": 5}
WRAP = {"nowrap": 0, "wrap": 1, "reverse": 2, "wrap-reverse": 2}
OVER = {"visible": 0, "hidden": 1, "scroll": 2}
BOX = {"border": 0, "border-box": 0, "content": 1, "content-box": 1}
POS = {"static": 0, "absolute": 1, "relative": 2}


def word(table, value, default=0):
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, str):
        return table.get(value, default)
    return default


def size_of(value):
    if value is None:
        return 0, 0.0
    if isinstance(value, str) and value.endswith("%"):
        return 2, float(value[:-1])
    return 1, float(value)


def flatten(node, parent, out, pool):
    role_name = node.get("role", node.get("type", "panel"))
    role = ROLES.get(role_name, 0)
    direction = word(DIRS, node.get("dir", node.get("direction", "column")))
    w_mode, width = size_of(node.get("w", node.get("width")))
    h_mode, height = size_of(node.get("h", node.get("height")))
    basis_mode, basis = size_of(node.get("basis")) if "basis" in node else (0, 0.0)
    maxw_mode, maxw = size_of(node.get("maxW")) if "maxW" in node else (0, 0.0)
    maxh_mode, maxh = size_of(node.get("maxH")) if "maxH" in node else (0, 0.0)
    flags = 0
    if node.get("clear"):
        flags |= 1
    if node.get("required"):
        flags |= 2
    if node.get("readOnly") or node.get("readonly"):
        flags |= 4
    if node.get("password"):
        flags |= 8
    if node.get("form"):
        flags |= 16
    if node.get("minimap", role == 14):
        flags |= 32
    if node.get("on"):
        flags |= 64
    if node.get("disabled"):
        flags |= 128
    fill = node.get("fill", [0.16, 0.18, 0.22, 1])
    while len(fill) < 4:
        fill.append(1 if len(fill) == 3 else 0)
    name = node.get("name", node.get("id", "")) or ""
    text = node.get("text", "") or ""
    hint = node.get("hint", "") or ""

    def put(text_value):
        if not text_value:
            return 0, 0
        off = len(pool)
        pool.extend(text_value.encode("utf-8"))
        pool.append(0)
        return off, len(text_value.encode("utf-8"))

    name_off, name_len = put(name)
    text_off, text_len = put(text)
    hint_off, hint_len = put(hint)
    index = len(out)
    out.append(
        {
            "parent": 0xFFFF if parent < 0 else parent,
            "role": role,
            "icon": int(node.get("icon", 0)),
            "tag": int(node.get("tag", -1)),
            "nameOff": name_off,
            "nameLen": name_len,
            "textOff": text_off,
            "textLen": text_len,
            "hintOff": hint_off,
            "hintLen": hint_len,
            "direction": direction,
            "wrap": word(WRAP, node.get("wrap", 0)),
            "justify": word(JUST, node.get("justify", 0)),
            "align": word(ALIGN, node.get("align", 0)),
            "widthMode": w_mode,
            "heightMode": h_mode,
            "position": word(POS, node.get("position", 0)),
            "filter": int(node.get("filter", 0)),
            "flags": flags,
            "checkKind": int(node.get("kind", 0)),
            "checkGroup": int(node.get("group", 0)),
            "maxChars": int(node.get("max", 0)),
            "shown": 0 if node.get("show") is False else 1,
            "textWrap": 1 if node.get("textWrap") else 0,
            "width": width,
            "height": height,
            "grow": float(node.get("grow", 0)),
            "shrink": float(node.get("shrink", 0)),
            "pad": float(node.get("pad", 0)),
            "gap": float(node.get("gap", 0)),
            "radius": float(node.get("radius", 0)),
            "r": float(fill[0]),
            "g": float(fill[1]),
            "b": float(fill[2]),
            "a": float(fill[3]),
            "posX": float(node.get("x", 0)),
            "posY": float(node.get("y", 0)),
            "minV": float(node.get("min", 0)),
            "maxV": float(node.get("maxv", node.get("maxValue", 0))),
            "value": float(node.get("value", 0)),
            "shadow": float(node.get("shadow", 0)),
            "borderW": float(node.get("border", 0)),
            "br": float(node.get("br", 0)),
            "bg": float(node.get("bg", 0)),
            "bb": float(node.get("bb", 0)),
            "padL": float(node.get("padL", 0)),
            "padR": float(node.get("padR", 0)),
            "padT": float(node.get("padT", 0)),
            "padB": float(node.get("padB", 0)),
            "minW": float(node.get("minW", 0)),
            "minH": float(node.get("minH", 0)),
            "marginL": float(node.get("marginL", 0)),
            "marginR": float(node.get("marginR", 0)),
            "marginT": float(node.get("marginT", 0)),
            "marginB": float(node.get("marginB", 0)),
            "basis": basis,
            "maxW": maxw,
            "maxH": maxh,
            "gapRow": float(node["gapRow"]) if "gapRow" in node else -1.0,
            "aspect": float(node.get("aspect", 0)),
            "posR": float(node.get("right", 0)),
            "posB": float(node.get("bottom", 0)),
            "textLeading": float(node.get("leading", 0)),
            "drop": int(node.get("drop", 0)),
            "content": word(CONTENT, node.get("content", 0)),
            "self": word(SELF, node.get("self", 0)),
            "basisMode": basis_mode,
            "maxWMode": maxw_mode,
            "maxHMode": maxh_mode,
            "overflow": word(OVER, node.get("overflow", 0)),
            "box": word(BOX, node.get("box", 0)),
            "inset": (1 if "right" in node else 0) | (2 if "bottom" in node else 0),
            "textAlign": int(node.get("textAlign", 0)),
            "textEllipsis": int(node.get("ellipsis", 0)),
            "textValign": int(node.get("valign", 0)),
            "textDeco": int(node.get("deco", 0)),
            "pad0": 0,
            "pad1": 0,
            "pad2": 0,
        }
    )
    for child in node.get("children", []):
        flatten(child, index, out, pool)


def pack_node(n):
    return NODE.pack(
        n["parent"],
        n["role"],
        n["icon"],
        n["tag"],
        n["nameOff"],
        n["nameLen"],
        n["textOff"],
        n["textLen"],
        n["hintOff"],
        n["hintLen"],
        n["direction"],
        n["wrap"],
        n["justify"],
        n["align"],
        n["widthMode"],
        n["heightMode"],
        n["position"],
        n["filter"],
        n["flags"],
        n["checkKind"],
        n["checkGroup"],
        n["maxChars"],
        n["shown"],
        n["textWrap"],
        n["width"],
        n["height"],
        n["grow"],
        n["shrink"],
        n["pad"],
        n["gap"],
        n["radius"],
        n["r"],
        n["g"],
        n["b"],
        n["a"],
        n["posX"],
        n["posY"],
        n["minV"],
        n["maxV"],
        n["value"],
        n["shadow"],
        n["borderW"],
        n["br"],
        n["bg"],
        n["bb"],
        n["padL"],
        n["padR"],
        n["padT"],
        n["padB"],
        n["minW"],
        n["minH"],
        n["marginL"],
        n["marginR"],
        n["marginT"],
        n["marginB"],
        n["basis"],
        n["maxW"],
        n["maxH"],
        n["gapRow"],
        n["aspect"],
        n["posR"],
        n["posB"],
        n["textLeading"],
        n["drop"],
        n["content"],
        n["self"],
        n["basisMode"],
        n["maxWMode"],
        n["maxHMode"],
        n["overflow"],
        n["box"],
        n["inset"],
        n["textAlign"],
        n["textEllipsis"],
        n["textValign"],
        n["textDeco"],
        n["pad0"],
        n["pad1"],
        n["pad2"],
    )


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: uibake.py in.json out.uib\n")
        return 1
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        tree = json.load(handle)
    nodes = []
    pool = bytearray()
    flatten(tree, -1, nodes, pool)
    if len(nodes) > 65535:
        sys.stderr.write("too many nodes\n")
        return 1
    blob = bytearray()
    blob += HEAD.pack(0x49554842, 2, len(nodes), len(pool))
    for node in nodes:
        blob += pack_node(node)
    blob += pool
    with open(sys.argv[2], "wb") as handle:
        handle.write(blob)
    sys.stderr.write(f"{sys.argv[2]}: {len(nodes)} nodes, {len(blob)} bytes, node {NODE.size}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
