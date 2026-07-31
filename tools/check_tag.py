"""Assert a release tag names the version the project declares.

The tag triggers the release and pyproject.toml decides what gets uploaded
under it. Nothing reconciles the two, and a disagreement is a typo in one of
them -- caught here, before an upload that cannot be taken back.

    $ python tools/check_tag.py v1.2.3
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import tomllib

# PEP 440's canonical form, and nothing else. PyPI accepts `1.0.0-rc1` and
# publishes `1.0.0rc1`, so comparing a tag against a declared version that is
# not already normalised is the disagreement this gate exists to catch. Local
# versions are absent because PyPI refuses them.
CANONICAL_VERSION = re.compile(
    r"([1-9][0-9]*!)?(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*))*"
    r"((a|b|rc)(0|[1-9][0-9]*))?(\.post(0|[1-9][0-9]*))?(\.dev(0|[1-9][0-9]*))?"
)


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

    if CANONICAL_VERSION.fullmatch(declared) is None:
        raise SystemExit(
            f"pyproject.toml declares {declared}, which is not a canonical PEP 440 "
            f"version, so PyPI would publish it under a different string than the "
            f"tag names. Write the normalised form: 1.0.0rc1, not 1.0.0-rc1."
        )

    if tag != f"v{declared}":
        raise SystemExit(f"tag {tag} names something other than v{declared}")

    print(f"{tag} matches the declared version {declared}")


if __name__ == "__main__":
    main()
