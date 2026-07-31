"""Benchmark `salix` against the real alternatives.

Methodology mirrors JP Hutchins' python-struct-profiling / importtime_sweep.py
so the numbers are directly comparable to the interstellar-inclination article:

  * dependency import (ms, cumulative): `python -X importtime -c "import LIB"`,
    median of N fresh interpreters, warm (.pyc cached).
  * per-type creation (us/type, warm): write a module of K identical 3-field
    classes, import it under `-X importtime` in a fresh interpreter, take the
    module's *self* time / K, median of N. Each construct gets a unique module
    name (never a real library name) so it can't shadow the dependency.
  * instantiation (ns/op): timeit, min of N repeats, 3-field construct.

Run with `uv run camas benchmark` (which builds first), or directly after a build:
    python bench/bench.py
"""

from __future__ import annotations

import os
import re
import statistics
import subprocess
import sys
import tempfile
import timeit
from collections.abc import Callable
from importlib.util import find_spec
from pathlib import Path
from typing import NamedTuple

K = 200
RUNS = 5

# The in-tree salix.*.so lives under src/ (or the repo root); make it
# importable here and in every subprocess we spawn (subprocesses inherit
# PYTHONPATH, not sys.path).
_ROOT = Path(__file__).resolve().parent.parent
_PATHS = [str(_ROOT / "src"), str(_ROOT)]
for _p in reversed(_PATHS):
    if _p not in sys.path:
        sys.path.insert(0, _p)
_ENV = {**os.environ, "PYTHONPATH": os.pathsep.join(
    [*_PATHS, os.environ.get("PYTHONPATH", "")]).rstrip(os.pathsep)}


class Construct(NamedTuple):
    key: str          # unique module name (must differ from any real library)
    label: str
    dep: str | None   # the dependency library to import (None = no dependency)
    header: str
    body: Callable[[int], str]
    ctor: Callable[[], Callable[[int, int, int], object]]


class Row(NamedTuple):
    label: str
    dep_ms: float
    per_type_us: float
    instantiate_ns: float


def _struct(i: int) -> str:
    return f"class C{i}(Struct):\n    a: int\n    b: int\n    c: int\n"


def _msgspec(i: int) -> str:
    return f"class C{i}(msgspec.Struct):\n    a: int\n    b: int\n    c: int\n"


def _namedtuple(i: int) -> str:
    return f"class C{i}(NamedTuple):\n    a: int\n    b: int\n    c: int\n"


def _dc_frozen(i: int) -> str:
    return (f"@dataclass(frozen=True, slots=True)\n"
            f"class C{i}:\n    a: int\n    b: int\n    c: int\n")


def _record_type(i: int) -> str:
    return f"@record\ndef C{i}(a: int, b: int, c: int) -> None: ...\n"


# The import belongs to the constructor and not to the module, so a construct
# whose dependency is absent costs nothing to skip.
def _struct_ctor() -> Callable[[int, int, int], object]:
    from salix import Struct

    class C(Struct):
        a: int
        b: int
        c: int
    return C


def _msgspec_ctor() -> Callable[[int, int, int], object]:
    import msgspec

    class C(msgspec.Struct):
        a: int
        b: int
        c: int
    return C


def _namedtuple_ctor() -> Callable[[int, int, int], object]:
    class C(NamedTuple):
        a: int
        b: int
        c: int
    return C


def _dc_frozen_ctor() -> Callable[[int, int, int], object]:
    from dataclasses import dataclass

    @dataclass(frozen=True, slots=True)
    class C:
        a: int
        b: int
        c: int
    return C


def _record_type_ctor() -> Callable[[int, int, int], object]:
    from records import record  # type: ignore[import-untyped]

    # record reads the return annotation and refuses one that is not None,
    # so this signature is the decorator's requirement, not an oversight.
    @record  # type: ignore[untyped-decorator]
    def C(a: int, b: int, c: int):  # type: ignore[no-untyped-def]
        ...
    return C  # type: ignore[no-any-return]


# An unbuilt salix/ still holds py.typed and the stub, which makes it a
# namespace package: find_spec answers, and the module it names is empty.
def _installed(dep: str | None) -> bool:
    return dep is None or ((spec := find_spec(dep)) is not None and spec.loader is not None)


# A rival to measure against, not a requirement: record-type carries a
# python_version>='3.11' marker and is simply absent below that.
CONSTRUCTS = [
    c
    for c in (
        Construct("gen_salix", "salix (this project)", "salix",
                  "from salix import Struct", _struct, _struct_ctor),
        Construct("gen_msgspec", "msgspec.Struct", "msgspec",
                  "import msgspec", _msgspec, _msgspec_ctor),
        Construct("gen_namedtuple", "typing.NamedTuple", "typing",
                  "from typing import NamedTuple", _namedtuple, _namedtuple_ctor),
        Construct("gen_dcfrozen", "dataclass (frozen+slots)", "dataclasses",
                  "from dataclasses import dataclass", _dc_frozen, _dc_frozen_ctor),
        Construct("gen_recordtype", "record-type (@record)", "records",
                  "from records import record", _record_type, _record_type_ctor),
    )
    if _installed(c.dep)
]

# Which rows the closing summary compares, when they survived the filter.
HEADLINE = ("gen_salix", "gen_namedtuple", "gen_msgspec")

_LINE = re.compile(r"import time:\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(.*)")


def _importtime(stmt: str, target: str, cwd: Path) -> tuple[int, int]:
    proc = subprocess.run(
        [sys.executable, "-X", "importtime", "-c", stmt],
        capture_output=True, text=True, cwd=str(cwd), env=_ENV, check=True,
    )
    for line in proc.stderr.splitlines():
        m = _LINE.match(line)
        if m and m.group(3).strip() == target:
            return int(m.group(1)), int(m.group(2))
    raise RuntimeError(f"no importtime line for {target!r}\n{proc.stderr[-1500:]}")


def per_type_us(c: Construct, work: Path) -> float:
    cdir = work / c.key
    cdir.mkdir(exist_ok=True)
    mod = c.header + "\n\n\n" + "\n\n".join(c.body(i) for i in range(K)) + "\n"
    (cdir / f"{c.key}.py").write_text(mod)
    stmt = f"import {c.key}"
    _importtime(stmt, c.key, cdir)  # prime .pyc
    samples = [_importtime(stmt, c.key, cdir)[0] for _ in range(RUNS)]
    return statistics.median(samples) / K


def dep_ms(c: Construct, work: Path) -> float:
    if c.dep is None:
        return 0.0
    samples = [_importtime(f"import {c.dep}", c.dep, work)[1] for _ in range(RUNS)]
    return statistics.median(samples) / 1000


def build_constructors() -> dict[str, Callable[[int, int, int], object]]:
    """Real in-process 3-field constructors (avoids exec/PEP-649 quirks)."""

    return {c.key: c.ctor() for c in CONSTRUCTS}


def instantiate_ns(ctor: Callable[[int, int, int], object]) -> float:
    best = min(timeit.repeat(lambda: ctor(1, 2, 3), repeat=RUNS, number=1_000_000))
    return best / 1_000_000 * 1e9


def main() -> None:
    print(f"python {sys.version.split()[0]} | K={K} | median of {RUNS} fresh "
          f"interpreters (warm)\n")
    ctors = build_constructors()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        rows = {
            c.key: Row(c.label, dep_ms(c, work), per_type_us(c, work),
                       instantiate_ns(ctors[c.key]))
            for c in CONSTRUCTS
        }

    w = max(len(r.label) for r in rows.values())
    print(f"{'construct':<{w}} {'import ms':>10} {'us/type':>9} {'inst ns':>9}")
    print("-" * (w + 31))
    for r in rows.values():
        print(f"{r.label:<{w}} {r.dep_ms:>10.3f} {r.per_type_us:>9.1f} {r.instantiate_ns:>9.1f}")

    print("\nTotal startup to define N struct types = import_ms + N * us/type/1000")
    def total(r: Row, n: int) -> float:
        return r.dep_ms + n * r.per_type_us / 1000

    headline = [(rows[k].label.split()[0], rows[k]) for k in HEADLINE if k in rows]
    for n in (1, 10, 100, 1000):
        print(f"  N={n:<5}  "
              + "   ".join(f"{name} {total(row, n):8.3f} ms" for name, row in headline))


if __name__ == "__main__":
    main()
