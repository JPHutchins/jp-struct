"""Assert a release tag names the version the project declares.

The tag triggers the release and pyproject.toml decides what gets uploaded
under it. Nothing reconciles the two, and a disagreement is a typo in one of
them -- caught here, before an upload that cannot be taken back.

    $ python tools/check_tag.py v1.2.3
"""

from __future__ import annotations

import sys
import tomllib
from pathlib import Path


def declared_version(pyproject: Path) -> str:
    version: str = tomllib.loads(pyproject.read_text())["project"]["version"]

    return version


def main() -> None:
    match sys.argv[1:]:
        case [tag]:
            pass
        case _:
            raise SystemExit("usage: check_tag.py TAG")

    declared = declared_version(Path("pyproject.toml"))
    named = tag.removeprefix("v")

    if named != declared:
        raise SystemExit(f"tag {tag} names {named}, but pyproject.toml declares {declared}")

    print(f"{tag} matches the declared version {declared}")


if __name__ == "__main__":
    main()
