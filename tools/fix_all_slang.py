#!/usr/bin/env python3
"""Batch-fix common Slang port issues."""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "shaders"


def strip_preamble(text: str) -> str:
    text = re.sub(r"^#version\s+\d+.*\n", "", text, flags=re.M)
    text = re.sub(r"^#extension\s+.*\n", "", text, flags=re.M)
    text = text.replace("taskPayloadSharedEXT ", "groupshared ")
    return text


def dedupe_struct(text: str, name: str) -> str:
    parts = text.split(f"struct {name} {{")
    if len(parts) <= 2:
        return text
    first = parts[0]
    body = parts[1].split("};", 1)[0]
    rest = parts[-1].split("};", 1)[1]
    return first + f"struct {name} {{" + body + "};" + rest


def fix_const(text: str) -> str:
    return re.sub(r"(?m)^const (float|int|uint) ", r"static const \1 ", text)


def fix_extra_paren(text: str) -> str:
    text = re.sub(r"(= float4\([^;]+\))\);", r"\1);", text)
    text = re.sub(r"(ssgiRaw\[[^\]]+\] = float4\([^;]+\))\);", r"\1);", text)
    return text


def fix_file(path: Path) -> None:
    t = strip_preamble(path.read_text(encoding="utf-8"))
    for n in ["ObjectData", "PackedVertexAnim", "MaterialData"]:
        t = dedupe_struct(t, n)
    t = fix_const(t)
    t = fix_extra_paren(t)
    t = t.replace(
        "mul(currentUbo.projection, mul(currentUbo.view, float4(rayPos, 1.0));",
        "mul(currentUbo.projection, mul(currentUbo.view, float4(rayPos, 1.0)));",
    )
    t = t.replace("pos = mul(ubo.invViewProj, pos;", "pos = mul(ubo.invViewProj, pos);")
    t = t.replace("unpackHalf2x16", "unpackHalf2x16")  # use import
    if path.name != "SlangBuiltins.slang" and "unpackHalf2x16" in t and "import common.SlangBuiltins" not in t:
        t = "import common.SlangBuiltins;\n" + t
    t = t.replace("atomicAdd(globalIndexCount", "InterlockedAdd(indexHead[0]")
    t = t.replace("dFdx(", "ddx(").replace("dFdy(", "ddy(")
    path.write_text(t, encoding="utf-8")


def compile_all() -> list[tuple[str, str]]:
    stages = {
        ".comp.slang": "compute",
        ".vert.slang": "vertex",
        ".frag.slang": "fragment",
        ".task.slang": "task",
        ".mesh.slang": "mesh",
    }
    files = sorted(SHADERS.glob("*.comp.slang"))
    files += sorted(SHADERS.glob("*.vert.slang"))
    files += sorted(SHADERS.glob("*.frag.slang"))
    files += sorted(SHADERS.glob("*.task.slang"))
    files += sorted(SHADERS.glob("*.mesh.slang"))
    fail = []
    for f in files:
        stage = next(v for k, v in stages.items() if f.name.endswith(k))
        r = subprocess.run(
            [
                "slangc",
                "-lang",
                "slang",
                "-target",
                "spirv",
                "-profile",
                "spirv_1_6",
                "-entry",
                "main",
                "-stage",
                stage,
                "-I",
                str(SHADERS),
                str(f),
                "-o",
                "/tmp/t.spv",
            ],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            fail.append((f.name, r.stderr[:900]))
    return fail


def main() -> None:
    for p in SHADERS.rglob("*.slang"):
        if p.name.startswith("."):
            continue
        fix_file(p)
    fail = compile_all()
    print(f"failures: {len(fail)}")
    for n, e in fail:
        print(f"\n=== {n} ===\n{e}")


if __name__ == "__main__":
    main()
