"""Emit `build/compile_commands.json` for clangd.

    python tools/compile_commands.py

Runs a forced in-place `build_ext` with the compiler's `spawn` wrapped, so the
recorded argv is the one setuptools actually executed — no second guess at
`-I<python-include>`, the sysconfig `CFLAGS`, or `Extension.extra_compile_args`.
setup.py stays the only place those are declared.
"""

from __future__ import annotations

import json
import runpy
import sys
from collections.abc import Iterator, Sequence
from pathlib import Path
from typing import Any, Final, NamedTuple

import setuptools
from setuptools.command.build_ext import build_ext

ROOT: Final = Path(__file__).resolve().parent.parent
DATABASE: Final = ROOT / "build" / "compile_commands.json"


class CompileCommand(NamedTuple):
    directory: str
    file: str
    arguments: Sequence[str]


def _compilations(argv: Sequence[str]) -> Iterator[CompileCommand]:
    """A `spawn` argv is a compilation iff it is `-c`-mode over C sources."""

    yield from (
        CompileCommand(directory=str(ROOT), file=str(ROOT / arg), arguments=argv)
        for arg in (argv if "-c" in argv else ())
        if arg.endswith(".c")
    )


class _recording_build_ext(build_ext):
    recorded: list[CompileCommand] = []

    def build_extensions(self) -> None:
        spawn = self.compiler.spawn

        def recording_spawn(cmd: Sequence[str], **kwargs: Any) -> Any:
            _recording_build_ext.recorded.extend(_compilations(cmd))
            return spawn(cmd, **kwargs)

        self.compiler.spawn = recording_spawn
        super().build_extensions()


def main() -> int:
    setup = setuptools.setup

    def recording_setup(**kwargs: Any) -> Any:
        return setup(
            **kwargs,
            script_args=["build_ext", "--inplace", "--force"],
            cmdclass={**kwargs.pop("cmdclass", {}), "build_ext": _recording_build_ext},
        )

    sys.path.insert(0, str(ROOT))
    setuptools.setup = recording_setup
    try:
        runpy.run_path(str(ROOT / "setup.py"), run_name="__main__")
    finally:
        setuptools.setup = setup

    if not _recording_build_ext.recorded:
        print("no compilations recorded — did build_ext run?", file=sys.stderr)
        return 1

    DATABASE.parent.mkdir(parents=True, exist_ok=True)
    DATABASE.write_text(
        json.dumps([entry._asdict() for entry in _recording_build_ext.recorded], indent=2) + "\n"
    )
    print(f"wrote {len(_recording_build_ext.recorded)} entries to {DATABASE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
