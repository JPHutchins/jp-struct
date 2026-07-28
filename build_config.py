"""What the extension is built from.

setup.py imports this for the local build and nix/wheel.nix shells out to it for
the cross builds, so a flag or a new translation unit lands in both.
"""

from typing import Final, NamedTuple


class BuildConfig(NamedTuple):
    sources: tuple[str, ...]
    c_flags: tuple[str, ...]


BUILD: Final = BuildConfig(
    sources=(
        "src/jpstruct.c",
        "src/annotations.c",
        "src/compare.c",
        "src/construct.c",
        "src/fields.c",
        "src/hash.c",
        "src/meta.c",
        "src/mixin.c",
        "src/options.c",
        "src/repr.c",
    ),
    # -Wno-unused-parameter: CPython slot signatures are fixed by the API and
    # routinely ignore an argument.
    c_flags=(
        "-std=c2x",
        "-O2",
        "-Werror",
        "-Wdouble-promotion",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
    ),
)


if __name__ == "__main__":
    import sys

    match sys.argv[1:]:
        case ["sources"]:
            print("\n".join(BUILD.sources))
        case ["c-flags"]:
            print("\n".join(BUILD.c_flags))
        case _:
            raise SystemExit("usage: build_config.py {sources|c-flags}")
