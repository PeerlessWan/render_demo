#!/usr/bin/env python3
"""Generate lua_api_reg.inc and a whitelist line from api.json."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
API = ROOT / "api.json"
INC = ROOT / "lua_api_reg.inc"
WL = ROOT / "whitelist.txt"


def generate() -> str:
    data = json.loads(API.read_text(encoding="utf-8"))
    lines = []
    for item in data:
        lines.append('{"' + item["name"] + '", ' + item["c"] + "},")
    lines.append("{nullptr, nullptr},")
    return "\n".join(lines) + "\n"


def whitelist() -> str:
    data = json.loads(API.read_text(encoding="utf-8"))
    return "、".join("`" + item["name"] + "`" for item in data) + "。\n"


def main() -> int:
    check = "--check" in sys.argv
    body = generate()
    if check:
        current = INC.read_text(encoding="utf-8") if INC.exists() else ""
        if current.replace("\r\n", "\n") != body.replace("\r\n", "\n"):
            print("lua_api_reg.inc is out of date; run gen_lua_api.py", file=sys.stderr)
            return 1
        return 0
    INC.write_text(body, encoding="utf-8")
    WL.write_text(whitelist(), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
