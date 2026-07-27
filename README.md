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
make build      # sync .venv + compile record.*.so in place
make test       # pytest
make bench      # compare import / type-creation / instantiation vs the field
make compiledb  # build/compile_commands.json for clangd
make help       # list targets
```

The package is **not installed** into the venv: `build_ext --inplace` lands the
compiled `record.*.so` under `src/`, imported from there (tests via the root
`conftest.py`, the benchmark via `PYTHONPATH`). Edit the C under `src/`,
`make build`, rerun — no reinstall step.

## Editor

The C under `src/` needs `Python.h` on the include path, so clangd needs a
compilation database. `make compiledb` produces one at `build/compile_commands.json`, which
`.clangd` points at; `.vscode/settings.json` disables the C/C++ extension's rival
IntelliSense engine and gives clangd a `--query-driver` for the host `cc`.

Run it once after cloning, and again whenever `setup.py` or the interpreter changes
— it is derived from the real build, not a second copy of the flags.

## Layout

| path | what |
|---|---|
| `src/record.c` | module definition and init |
| `src/meta.c` | `RecordMeta`: type creation, GC, slot offsets |
| `src/fields.c` | field order, inheritance, defaults |
| `src/construct.c` | per-type vectorcall — instance creation |
| `src/hash.c`, `repr.c`, `compare.c`, `mixin.c` | the dunders |
| `src/annotations.c` | annotation extraction (PEP 649 aware) |
| `src/types.h` | `RecordType` + the shared slot accessors |
| `src/result.h` | `RESULT_OK` / `RESULT_ERR` |
| `setup.py` | declares the `record` extension |
| `pyproject.toml` | metadata + dev/bench deps (`[dependency-groups]`) |
| `Makefile` | `build` / `test` / `bench` / `compiledb` / `clean` |
| `conftest.py` | puts the in-place `.so` on `sys.path` for pytest |
| `tests/test_record.py` | correctness tests |
| `bench/bench.py` | benchmark harness (mirrors `python-struct-profiling`) |
| `tools/compile_commands.py` | records the real `build_ext` compile line into a clangd database |
| `.clangd`, `.vscode/` | point clangd at `build/compile_commands.json` |

## What it is / isn't

Fields come from class-body annotations (like dataclass/msgspec/NamedTuple).
Records are immutable, hashable, slotted, and define `__eq__` (structural),
`__hash__`, `__repr__`, `__match_args__`, and single-base field inheritance.

It deliberately does **not** include serialization/validation (that's msgspec),
and a class body can't express positional-only / keyword-only / `*args` /
`**kwargs` (that's the `record-type` decorator). Not yet seeded but easy to add:
`dataclass_transform` stubs for type-checkers, pickle/copy/`__replace__`,
`ClassVar` exclusion, `kw_only`.
