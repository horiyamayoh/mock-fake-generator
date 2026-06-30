#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRS = {
    ".cache",
    ".cmake",
    ".git",
    "build",
    "CMakeFiles",
    "node_modules",
    "third_party",
}
CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
JSON_EXTENSIONS = {".json"}
TEXT_EXTENSIONS = {".md", ".yml", ".yaml"}
TEXT_FILENAMES = {
    ".clang-format",
    ".clang-tidy",
    ".gitignore",
    ".prettierignore",
}


def should_skip(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    return any(part in EXCLUDED_DIRS for part in relative.parts)


def iter_project_files() -> list[Path]:
    files: list[Path] = []
    for path in ROOT.rglob("*"):
        if not path.is_file() or should_skip(path):
            continue
        if (
            path.suffix in CPP_EXTENSIONS
            or path.suffix in JSON_EXTENSIONS
            or path.suffix in TEXT_EXTENSIONS
            or path.name in TEXT_FILENAMES
        ):
            files.append(path)
    return sorted(files)


def run_clang_format(paths: list[Path], fix: bool) -> int:
    cpp_files = [path for path in paths if path.suffix in CPP_EXTENSIONS]
    if not cpp_files:
        return 0

    tool = os.environ.get("CLANG_FORMAT", "clang-format")
    resolved_tool = shutil.which(tool)
    if resolved_tool is None:
        print(f"error: {tool} was not found", file=sys.stderr)
        return 1

    args = [resolved_tool]
    if fix:
        args.append("-i")
    else:
        args.extend(["--dry-run", "--Werror"])
    args.extend(str(path.relative_to(ROOT)) for path in cpp_files)

    completed = subprocess.run(args, cwd=ROOT, check=False)
    return completed.returncode


def expected_json_text(path: Path) -> str:
    with path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n"


def expected_text(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return text.rstrip("\n") + "\n"


def check_or_fix_text_files(paths: list[Path], fix: bool) -> int:
    status = 0
    for path in paths:
        if path.suffix in CPP_EXTENSIONS:
            continue

        try:
            if path.suffix in JSON_EXTENSIONS:
                expected = expected_json_text(path)
            else:
                expected = expected_text(path)
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            print(f"error: failed to inspect {path.relative_to(ROOT)}: {error}", file=sys.stderr)
            status = 1
            continue

        current = path.read_text(encoding="utf-8")
        if current == expected:
            continue

        if fix:
            path.write_text(expected, encoding="utf-8")
        else:
            print(f"error: {path.relative_to(ROOT)} is not formatted", file=sys.stderr)
            status = 1

    return status


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Format or check mockfakegen repository files.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="Check formatting without writing files.")
    mode.add_argument("--fix", action="store_true", help="Rewrite files in place.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    paths = iter_project_files()
    text_status = check_or_fix_text_files(paths, fix=args.fix)
    cpp_status = run_clang_format(paths, fix=args.fix)
    return 0 if text_status == 0 and cpp_status == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
