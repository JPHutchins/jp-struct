"""Build the `jpstruct` C extension.

Run via camas (`uv run camas build`), or directly:

    python setup.py build_ext --inplace

This produces `jpstruct.*.so` in the repo root, importable as `jpstruct`.
"""

import sys
from pathlib import Path

from setuptools import Extension, setup

# A PEP 517 build runs with the source tree off the path, and build_config is
# not a distribution to be resolved.
sys.path.insert(0, str(Path(__file__).parent))

from build_config import BUILD  # noqa: E402

setup(
    ext_modules=[
        Extension("jpstruct", list(BUILD.sources), extra_compile_args=list(BUILD.c_flags)),
    ],
)
