"""Build the `jpstruct` C extension.

Run via camas (`uv run camas build`), or directly:

    python setup.py build_ext --inplace

This produces `jpstruct/__init__.*.so`, importable as `jpstruct`. The
extension is the package's __init__ so that py.typed and the stub have a
package directory to live in; there is no Python module in the import path.
"""

import sys
from pathlib import Path

from setuptools import Extension, setup

# A PEP 517 build runs with the source tree off the path, and build_config is
# not a distribution to be resolved.
sys.path.insert(0, str(Path(__file__).parent))

from build_config import BUILD

setup(
    ext_modules=[
        Extension(
            "jpstruct.__init__", list(BUILD.sources), extra_compile_args=list(BUILD.c_flags)
        ),
    ],
)
