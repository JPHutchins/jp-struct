"""Artifact removal as Python, so `camas clean` runs the same way on any platform.

    python tools/clean.py
"""

from __future__ import annotations

import shutil
from collections.abc import Iterator
from pathlib import Path
from typing import Final

ROOT: Final = Path(__file__).resolve().parent.parent
KEEP_OUT_OF: Final = frozenset({".venv", ".git"})
DIRECTORIES: Final = ("build", ".pytest_cache", ".mypy_cache")
PRUNED_DIRECTORIES: Final = frozenset({"__pycache__"})
PRUNED_DIRECTORY_SUFFIXES: Final = frozenset({".egg-info"})
PRUNED_FILE_SUFFIXES: Final = frozenset({".so", ".o"})


def _walk(root: Path) -> Iterator[Path]:
    """Everything under `root`, skipping trees that are not ours to delete."""

    for path in root.iterdir():
        if path.name in KEEP_OUT_OF:
            continue

        yield path

        if path.is_dir() and not path.is_symlink():
            yield from _walk(path)


def main() -> None:
    for name in DIRECTORIES:
        shutil.rmtree(ROOT / name, ignore_errors=True)

    for path in tuple(_walk(ROOT)):
        if not path.exists():
            continue
        if path.is_dir():
            if path.name in PRUNED_DIRECTORIES or path.suffix in PRUNED_DIRECTORY_SUFFIXES:
                shutil.rmtree(path, ignore_errors=True)
        elif path.suffix in PRUNED_FILE_SUFFIXES:
            path.unlink()


if __name__ == "__main__":
    main()
