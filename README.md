# jp-struct

A C-backed, **inheritable** `Struct` base class — `class Point(Struct): ...`, the
shape of `typing.NamedTuple`, built for programs that pay for their imports:
CLIs, build tooling, anything where startup is the budget.

The trade is import cost. Defining a type is competitive with the fastest thing
available; importing the library is two orders of magnitude cheaper.
`uv run camas benchmark` prints the current numbers against msgspec,
`typing.NamedTuple`, `dataclass` and `record-type` on your machine. They are not
repeated here — a number in a README is a number nothing checks.

```python
>>> from jpstruct import Struct

>>> class Point(Struct):
...     x: float
...     y: float = 0.0

>>> Point(1.0, 2.0)
Point(x=1.0, y=2.0)
>>> Point(1.0, 2.0) == Point(1.0, 2.0)
True
>>> hash(Point(1.0, 2.0)) == hash((1.0, 2.0))
True
>>> Point(1.0, 2.0).x = 9.0
Traceback (most recent call last):
    ...
TypeError: Point object does not support attribute assignment

```

Frozen, hashable and slotted by default; equality is structural rather than
nominal, so two structs with the same field names and values are equal whatever
their classes. Class keywords choose the rest, and `__post_init__` runs once
every field is written:

```python
>>> class Reading(Struct, order=True):
...     celsius: float
...     def __post_init__(self) -> None:
...         if self.celsius < -273.15:
...             raise ValueError("below absolute zero")

>>> sorted([Reading(20.0), Reading(-40.0)])
[Reading(celsius=-40.0), Reading(celsius=20.0)]
>>> Reading(-300.0)
Traceback (most recent call last):
    ...
ValueError: below absolute zero

```

`jpstruct/__init__.pyi` is the API: it lists every class keyword, and a type
checker reads it, so it cannot drift from what the extension does. The examples
above are run by the test suite for the same reason.

> The C under `src/` began as the **seed** copied from the feasibility prototype
> ([JPHutchins/record-type#1](https://github.com/JPHutchins/record-type/pull/1)),
> since split by hand into one translation unit per concern. It is still meant
> to be rewritten — this repo is the scaffolding so the toolchain is out of your
> way.

## Working in the repo

The Nix dev shell is the only supported environment. It supplies and pins uv,
zig, nixfmt, jphfmt and the C analysers.

```sh
nix develop
uv run camas check
```

`tasks.py` is the single source of truth for what runs, locally and in CI, via
[`camas`](https://github.com/JPHutchins/camas). Run it through `uv run` — a
`camas` on `PATH` is whatever version happens to be installed globally, and a
mismatch surfaces as an error that looks like a bug in `tasks.py`.

`camas check` is the default task. It compiles the extension in place to
`jpstruct/__init__.<abi>.so`, beside the stub, once per interpreter in
`.python-version`, and runs the suite against each. That build is what
`tests/conftest.py` puts on the path when nothing else provides `jpstruct` —
deliberately from `tests/` rather than the repo root, so a leg meant to exercise
an installed wheel cannot silently pick up the working tree instead.

The in-file C unit tests live at the bottom of each `.c` behind `TESTING` and
need libpython and unity on the link line, so nix builds them: `camas c_test`.

## Wheels

```sh
nix build .#default
```

Cross-compiles every wheel with `zig cc` from one machine — manylinux_2_17
x86_64/aarch64, macOS x86_64/arm64, Windows amd64/arm64 — for every interpreter
in `.python-version`. `nix flake check` builds them all, verifies each payload
against the tag it ships under, and installs the native one to run the suite
against it.

Windows is wheel-only: `__attribute__((cleanup))` has no MSVC equivalent, so
there is no source install to fall back to.

## Editor

The C under `src/` needs `Python.h` on the include path. `camas compile_flags`
writes `compile_flags.txt`, which clangd and clang-tidy both read; it derives
the flags from the same `build_config.py` the build uses. Re-run it when the
interpreter changes.

## What it deliberately is not

No serialization, no validation, no type coercion — that is msgspec, and it is
most of why msgspec costs what it does to import. A class body also cannot
express positional-only, keyword-only, `*args` or `**kwargs`; the `record-type`
decorator is the answer to that shape.

`kw_only` is unimplemented and rejected rather than ignored. Pickle, copy and
`__replace__` are not seeded.

## License

MIT
