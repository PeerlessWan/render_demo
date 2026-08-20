#!/usr/bin/env python3
"""Convert leading 2-space indentation to 4-space (leading tabs -> spaces).

Detection: treat file as 2-space when many lines have indent==2 and few have
odd indent. Conversion: levels = N // 2; new leading = levels*4 spaces, plus
one space if N was odd. Leading tabs are expanded to spaces (width 4) first.

Usage:
  python reindent_4space.py path/to/file.cpp [more...]
  python reindent_4space.py -n file.cpp          # dry-run
  python reindent_4space.py -f file.cpp          # force convert
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TAB_WIDTH = 4


def _split_ending(line: str) -> tuple[str, str]:
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n"):
        return line[:-1], "\n"
    if line.endswith("\r"):
        return line[:-1], "\r"
    return line, ""


def expand_leading_tabs(line: str) -> tuple[str, str]:
    """Expand tabs only in the leading indent; return (leading_spaces, rest)."""
    i = 0
    col = 0
    parts: list[str] = []
    while i < len(line):
        c = line[i]
        if c == " ":
            parts.append(" ")
            col += 1
            i += 1
        elif c == "\t":
            n = TAB_WIDTH - (col % TAB_WIDTH)
            parts.append(" " * n)
            col += n
            i += 1
        else:
            break
    return "".join(parts), line[i:]


def leading_space_count(line: str) -> int:
    leading, _ = expand_leading_tabs(line)
    return len(leading)


def looks_like_2space(lines: list[str]) -> bool:
    """Many indent-2 lines and few odd indents => 2-space style."""
    indent_eq_2 = 0
    indent_odd = 0
    indent_mod4_eq_2 = 0  # 2, 6, 10, ... strong 2-space signal

    for line in lines:
        if not line.strip():
            continue
        n = leading_space_count(line)
        if n == 0:
            continue
        if n == 2:
            indent_eq_2 += 1
        if n % 2 == 1:
            indent_odd += 1
        elif n % 4 == 2:
            indent_mod4_eq_2 += 1

    if indent_eq_2 >= 3 and indent_odd <= max(1, indent_eq_2 // 10):
        return True
    # Fallback: enough classic half-levels (2/6/10...) with almost no odds
    return indent_mod4_eq_2 >= 5 and indent_odd <= max(1, indent_mod4_eq_2 // 10)


def convert_indent(n: int) -> int:
    """2-space levels -> 4-space; keep a single leftover space if N is odd."""
    levels = n // 2
    rem = n % 2
    return levels * 4 + rem


def convert_line(line: str) -> str:
    body, ending = _split_ending(line)
    leading, rest = expand_leading_tabs(body)
    n = len(leading)
    if n == 0:
        return body + ending
    return (" " * convert_indent(n)) + rest + ending


def normalize_tabs_only(line: str) -> str:
    """Expand leading tabs to spaces without doubling indent."""
    body, ending = _split_ending(line)
    leading, rest = expand_leading_tabs(body)
    return leading + rest + ending


def reindent_text(text: str, force: bool = False) -> tuple[str, bool]:
    lines = text.splitlines(keepends=True)
    if not lines:
        return text, False

    plain = text.splitlines()
    if force or looks_like_2space(plain):
        out = [convert_line(line) for line in lines]
    else:
        out = [normalize_tabs_only(line) for line in lines]

    new_text = "".join(out)
    return new_text, new_text != text


def process_file(path: Path, *, dry_run: bool, force: bool) -> str:
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8")
        encoding = "utf-8"
    except UnicodeDecodeError:
        text = raw.decode("latin-1")
        encoding = "latin-1"

    new_text, changed = reindent_text(text, force=force)
    if not changed:
        return f"skip  {path} (not 2-space / unchanged)"
    if dry_run:
        return f"would {path}"

    path.write_bytes(new_text.encode(encoding))
    return f"ok    {path}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert leading 2-space indentation to 4-space."
    )
    parser.add_argument("files", nargs="+", type=Path, help="Files to reindent")
    parser.add_argument(
        "-n", "--dry-run", action="store_true", help="Do not write changes"
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Convert even if 2-space heuristic fails",
    )
    args = parser.parse_args(argv)

    status = 0
    for path in args.files:
        if not path.is_file():
            print(f"miss  {path}", file=sys.stderr)
            status = 1
            continue
        try:
            print(process_file(path, dry_run=args.dry_run, force=args.force))
        except OSError as exc:
            print(f"error {path}: {exc}", file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
