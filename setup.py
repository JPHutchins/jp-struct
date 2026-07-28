"""Build the `jpstruct` C extension.

Run via camas (`uv run camas build`), or directly:

    python setup.py build_ext --inplace

This produces `jpstruct.*.so` in the repo root, importable as `jpstruct`. Sources
and compile flags come from pyproject.toml's [tool.jp-struct], which the nix
cross builds read too.
"""

import sys
from pathlib import Path

from setuptools import Extension, setup

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib

config = tomllib.loads(Path(__file__).with_name("pyproject.toml").read_text())["tool"]["jp-struct"]

setup(
    ext_modules=[
        Extension("jpstruct", config["sources"], extra_compile_args=config["c-flags"]),
    ],
)
