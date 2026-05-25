#!/usr/bin/env python3
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[1] / "shaders"

STAGES = {
    ".comp": ("compute", "[numthreads(8, 8, 1)]"),
    ".vert": ("vertex", ""),
    ".frag": ("fragment", ""),
    ".task": ("task", "[numthreads(32, 1, 1)]"),
    ".mesh": ("mesh", "[numthreads(128, 1, 1)]"),
}

SKIP = {"culling.slang"}

for src in sorted(SHADERS.iterdir()):
    if src.suffix not in STAGES or src.name.endswith(".spv"):
        continue
    dst = src.with_suffix(src.suffix + ".slang")  # foo.comp -> foo.comp.slang
    if dst.name in SKIP or dst.exists():
        continue
    stage, threads = STAGES[src.suffix]
    lines = [f"// Slang stub (from {src.name})", f'[shader("{stage}")]', threads, "void main() {}"]
    dst.write_text("\n".join(lines) + "\n")
    print(dst.name)
