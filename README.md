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

> The C in `src/record.c` is the **seed** copied verbatim from the feasibility
> prototype ([JPHutchins/record-type#1](https://github.com/JPHutchins/record-type/pull/1)).
> It's meant to be rewritten by hand — this repo is the scaffolding so the
> toolchain is out of your way.

## Quickstart

Requires [`uv`](https://docs.astral.sh/uv/) and a CPython with dev headers
(developed on 3.14; the annotation handling is PEP 649-aware and also handles
pre-3.14).

```sh
make build    # sync .venv + compile record.*.so in place
make test     # pytest
make bench    # compare import / type-creation / instantiation vs the field
make help     # list targets
```

The package is **not installed** into the venv: `build_ext --inplace` lands the
compiled `record.*.so` under `src/`, imported from there (tests via the root
`conftest.py`, the benchmark via `PYTHONPATH`). Edit `src/record.c`, `make build`,
rerun — no reinstall step.

## Layout

| path | what |
|---|---|
| `src/record.c` | the C extension (`RecordMeta` metaclass + `Record` base) — rewrite target |
| `setup.py` | declares the `record` extension |
| `pyproject.toml` | metadata + dev/bench deps (`[dependency-groups]`) |
| `Makefile` | `build` / `test` / `bench` / `clean` |
| `conftest.py` | puts the in-place `.so` on `sys.path` for pytest |
| `tests/test_record.py` | correctness tests |
| `bench/bench.py` | benchmark harness (mirrors `python-struct-profiling`) |

## What it is / isn't

Fields come from class-body annotations (like dataclass/msgspec/NamedTuple).
Records are immutable, hashable, slotted, and define `__eq__` (structural),
`__hash__`, `__repr__`, `__match_args__`, and single-base field inheritance.

It deliberately does **not** include serialization/validation (that's msgspec),
and a class body can't express positional-only / keyword-only / `*args` /
`**kwargs` (that's the `record-type` decorator). Not yet seeded but easy to add:
`dataclass_transform` stubs for type-checkers, pickle/copy/`__replace__`,
`ClassVar` exclusion, `kw_only`.
