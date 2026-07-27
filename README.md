# jp-struct

A C-backed, **inheritable** `Record` base class for Python — `class Foo(Record): ...`,
like `typing.NamedTuple`, but with near-zero import cost and faster type creation
than anything else measured. Optimized for **program startup** (CLIs, build tooling).

```python
from record import Record

class Point(Record):
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

Requires [`uv`](https://docs.astral.sh/uv/) and a CPython with dev headers
(developed on 3.14; the annotation handling is PEP 649-aware and also handles
pre-3.14).

```sh
uv run camas --help
```

Tasks live in `tasks.py` and run with [`camas`](https://github.com/JPHutchins/camas),
so one definition drives both local development and CI.

`build_ext --inplace` lands the compiled `record.*.so` under `src/` and it is
imported from there (tests via the root `conftest.py`, the benchmark via
`PYTHONPATH`). `uv run` also installs the project, but that install is only a
`.pth` pointing at the same `src/`, so there is exactly one built extension
either way.

## Editor

The C under `src/` needs `Python.h` on the include path, so clangd needs a
compilation database. `uv run camas compile_commands` writes one to
`build/compile_commands.json`, where `.clangd` points. It is recorded from the
real build rather than re-declared, so it cannot drift from `setup.py` — re-run
it when `setup.py` or the interpreter changes.

## What it is / isn't

Fields come from class-body annotations (like dataclass/msgspec/NamedTuple).
Records are immutable, hashable, slotted, and define `__eq__` (structural),
`__hash__`, `__repr__`, `__match_args__`, and single-base field inheritance.

It deliberately does **not** include serialization/validation (that's msgspec),
and a class body can't express positional-only / keyword-only / `*args` /
`**kwargs` (that's the `record-type` decorator). Not yet seeded but easy to add:
`dataclass_transform` stubs for type-checkers, pickle/copy/`__replace__`,
`ClassVar` exclusion, `kw_only`.
