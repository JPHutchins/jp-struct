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
            [
                "src/record.c",
                "src/annotations.c",
                "src/compare.c",
                "src/construct.c",
                "src/fields.c",
                "src/hash.c",
                "src/meta.c",
                "src/mixin.c",
                "src/repr.c",
            ],
            # -Wno-unused-parameter: CPython slot signatures are fixed by the
            # API and routinely ignore an argument.
            extra_compile_args=["-std=c2x", "-O2", "-Werror", "-Wdouble-promotion", "-Wall", "-Wextra", "-Wno-unused-parameter"],
        )
    ],
)
