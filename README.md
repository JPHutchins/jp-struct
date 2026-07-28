# jp-struct

A C-backed, **inheritable** `Struct` base class for Python — `class Foo(Struct): ...`,
like `typing.NamedTuple`, but with near-zero import cost and faster type creation
than anything else measured. Optimized for **program startup** (CLIs, build tooling).

```python
from jpstruct import Struct

class Point(Struct):
    x: float
    y: float

p = Point(1.0, 2.0)        # fast C vectorcall
p.x                        # 1.0
p == Point(1.0, 2.0)       # True (structural)
hash(p)                    # immutable + hashable
# p.x = 9.0                # TypeError: immutable
```

> The C under `src/` began as the **seed** copied from the feasibility
> prototype ([JPHutchins/record-type#1](https://github.com/JPHutchins/record-type/pull/1)),
> since split by hand into one translation unit per concern. It's still meant
> to be rewritten — this repo is the scaffolding so the toolchain is out of
> your way.

## Quickstart

The Nix dev shell is the only supported environment. Enter it once, then work
inside it — it supplies uv, zig, nixfmt and jphfmt, and pins them.

```sh
nix develop
uv run camas --help
```

Tasks live in `tasks.py` and run with [`camas`](https://github.com/JPHutchins/camas),
so one definition drives both local development and CI. Interpreters come from
uv, driven by `.python-version`, which is also the matrix the wheels are built
for.

`build_ext --inplace` lands the compiled `jpstruct.*.so` under `src/`, and the
root `conftest.py` puts it on the path when nothing else provides `jpstruct`.
Never install the project to get it — installing means compiling, and on
Windows that is MSVC, which cannot build this source.

## Wheels

```sh
nix build .#default
```

Cross-compiles every wheel with `zig cc` from one machine: manylinux_2_17
x86_64/aarch64, macOS x86_64/arm64, and Windows amd64/arm64, for every
interpreter in `.python-version`. `nix flake check` builds them all, verifies
each payload against the tag it ships under, and installs the native one to run
the suite against it.

Windows has no source install: `__attribute__((cleanup))` has no MSVC
equivalent, so a wheel is the only way in.

## Editor

The C under `src/` needs `Python.h` on the include path, so clangd needs a
compilation database. `uv run camas compile_commands` writes one to
`build/compile_commands.json`, where `.clangd` points. It is recorded from the
real build rather than re-declared, so it cannot drift from `setup.py` — re-run
it when `setup.py` or the interpreter changes.

## What it is / isn't

Fields come from class-body annotations (like dataclass/msgspec/NamedTuple).
Structs are immutable, hashable, slotted, and define `__eq__` (structural),
`__hash__`, `__repr__`, `__match_args__`, and single-base field inheritance.

It deliberately does **not** include serialization/validation (that's msgspec),
and a class body can't express positional-only / keyword-only / `*args` /
`**kwargs` (that's the `record-type` decorator). Not yet seeded but easy to add:
`dataclass_transform` stubs for type-checkers, pickle/copy/`__replace__`,
`ClassVar` exclusion, `kw_only`.

## License

MIT
