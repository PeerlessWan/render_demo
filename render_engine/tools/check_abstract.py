from pathlib import Path
import re

ROOT = Path(r"d:/workspace/media/render_demo/render_engine")


def find_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    n = len(text)
    in_str = in_char = in_line = in_block = False
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_line:
            if c == "\n":
                in_line = False
            i += 1
            continue
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if c == "\\" and nxt:
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\" and nxt:
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == "/" and nxt == "/":
            in_line = True
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block = True
            i += 2
            continue
        if c == '"':
            in_str = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise RuntimeError("unbalanced")


idev = (ROOT / "engine/include/engine/rhi/i_device.h").read_text(encoding="utf-8")
m = re.search(r"class IDevice \{", idev)
body = idev[m.end() : find_brace(idev, m.end() - 1)]
pure = []
for mm in re.finditer(r"virtual\s+([\s\S]*?)\s*=\s*0\s*;", body):
    chunk = mm.group(1)
    pm = re.search(r"(\w+)\s*\(", chunk)
    if pm:
        pure.append(pm.group(1))

hdr = (ROOT / "engine/backends/d3d12/d3d12_device_internal.h").read_text(encoding="utf-8")
print("pures", len(pure))
for name in pure:
    if f"{name}(" not in hdr:
        print("MISSING", name)

# Also check cpp definitions exist for public overrides
cpp_text = ""
for p in (ROOT / "engine/backends/d3d12").glob("d3d12_device*.cpp"):
    cpp_text += p.read_text(encoding="utf-8")
for name in pure:
    if f"D3D12Device::{name}" not in cpp_text and f"D3D12Device::~" not in name:
        # destructor special
        if name == "IDevice":
            continue
        print("NO DEF", name)
