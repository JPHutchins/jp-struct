"""Build the `record` C extension.

Run via the Makefile (`make build`), or directly:

    python setup.py build_ext --inplace

This produces `record.*.so` in the repo root, importable as `record`.
Project metadata and dev dependencies live in pyproject.toml.
"""

from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "record",
            ["src/record.c"],
            # Strict-ish by default; add "-Werror"/"-Wdouble-promotion" when you
            # harden. Python's headers are not always -Wextra-clean across
            # compilers, so -Werror is left off the baseline on purpose.
            extra_compile_args=["-O2", "-Wall", "-Wextra"],
        )
    ],
)
