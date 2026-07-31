"""Build the `salix` C extension.

Run via camas (`uv run camas build`), or directly:

    python setup.py build_ext --inplace

This produces `salix/__init__.*.so`, importable as `salix`. The
extension is the package's __init__ so that py.typed and the stub have a
package directory to live in; there is no Python module in the import path.
"""

import os
import sys
from pathlib import Path

from setuptools import Extension, setup

# A PEP 517 build runs with the source tree off the path, and build_config is
# not a distribution to be resolved.
sys.path.insert(0, str(Path(__file__).parent))

from build_config import BUILD, STRICT

# Opt-in, because this same file builds the sdist on a stranger's machine.
# camas sets it; nothing else does.
STRICTLY = STRICT if os.environ.get("SALIX_STRICT") else ()

setup(
    ext_modules=[
        Extension(
            "salix.__init__", list(BUILD.sources), extra_compile_args=[*BUILD.c_flags, *STRICTLY]
        ),
    ],
)
