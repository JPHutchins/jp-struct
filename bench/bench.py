"""Benchmark `record` against the real alternatives.

Methodology mirrors JP Hutchins' python-struct-profiling / importtime_sweep.py
so the numbers are directly comparable to the interstellar-inclination article:

  * dependency import (ms, cumulative): `python -X importtime -c "import LIB"`,
    median of N fresh interpreters, warm (.pyc cached).
  * per-type creation (us/type, warm): write a module of K identical 3-field
    classes, import it under `-X importtime` in a fresh interpreter, take the
    module's *self* time / K, median of N. Each construct gets a unique module
    name (never a real library name) so it can't shadow the dependency.
  * instantiation (ns/op): timeit, min of N repeats, 3-field construct.

Run with `make bench` (which builds first), or directly after `make build`:
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
from pathlib import Path
from typing import Callable, NamedTuple

K = 200
RUNS = 5

# The in-tree record.*.so lives under src/ (or the repo root); make it
# importable here and in every subprocess we spawn (subprocesses inherit
# PYTHONPATH, not sys.path).
_ROOT = Path(__file__).resolve().parent.parent
_PATHS = [str(_ROOT / "src"), str(_ROOT)]
for _p in reversed(_PATHS):
    if _p not in sys.path:
        sys.path.insert(0, _p)
_ENV = {**os.environ, "PYTHONPATH": os.pathsep.join(
    _PATHS + [os.environ.get("PYTHONPATH", "")]).rstrip(os.pathsep)}


class Construct(NamedTuple):
    key: str          # unique module name (must differ from any real library)
    label: str
    dep: str | None   # the dependency library to import (None = no dependency)
    header: str
    body: Callable[[int], str]


def _record(i: int) -> str:
    return f"class C{i}(Record):\n    a: int\n    b: int\n    c: int\n"


def _msgspec(i: int) -> str:
    return f"class C{i}(msgspec.Struct):\n    a: int\n    b: int\n    c: int\n"


def _namedtuple(i: int) -> str:
    return f"class C{i}(NamedTuple):\n    a: int\n    b: int\n    c: int\n"


def _dc_frozen(i: int) -> str:
    return (f"@dataclass(frozen=True, slots=True)\n"
            f"class C{i}:\n    a: int\n    b: int\n    c: int\n")


def _record_type(i: int) -> str:
    return f"@record\ndef C{i}(a: int, b: int, c: int) -> None: ...\n"


CONSTRUCTS = [
    Construct("gen_record", "record (this project)", "record",
              "from record import Record", _record),
    Construct("gen_msgspec", "msgspec.Struct", "msgspec",
              "import msgspec", _msgspec),
    Construct("gen_namedtuple", "typing.NamedTuple", "typing",
              "from typing import NamedTuple", _namedtuple),
    Construct("gen_dcfrozen", "dataclass (frozen+slots)", "dataclasses",
              "from dataclasses import dataclass", _dc_frozen),
    Construct("gen_recordtype", "record-type (@record)", "records",
              "from records import record", _record_type),
]

_LINE = re.compile(r"import time:\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(.*)")


def _importtime(stmt: str, target: str, cwd: Path) -> tuple[int, int]:
    proc = subprocess.run(
        [sys.executable, "-X", "importtime", "-c", stmt],
        capture_output=True, text=True, cwd=str(cwd), env=_ENV,
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


def build_constructors() -> dict[str, Callable]:
    """Real in-process 3-field constructors (avoids exec/PEP-649 quirks)."""
    out: dict[str, Callable] = {}

    from record import Record

    class RC(Record):
        a: int
        b: int
        c: int
    out["gen_record"] = RC

    import msgspec

    class MS(msgspec.Struct):
        a: int
        b: int
        c: int
    out["gen_msgspec"] = MS

    from typing import NamedTuple

    class NT(NamedTuple):
        a: int
        b: int
        c: int
    out["gen_namedtuple"] = NT

    from dataclasses import dataclass

    @dataclass(frozen=True, slots=True)
    class DC:
        a: int
        b: int
        c: int
    out["gen_dcfrozen"] = DC

    from records import record

    @record
    def RT(a: int, b: int, c: int):
        ...
    out["gen_recordtype"] = RT

    return out


def instantiate_ns(ctor: Callable) -> float:
    best = min(timeit.repeat(lambda: ctor(1, 2, 3), repeat=RUNS, number=1_000_000))
    return best / 1_000_000 * 1e9


def main() -> None:
    print(f"python {sys.version.split()[0]} | K={K} | median of {RUNS} fresh "
          f"interpreters (warm)\n")
    ctors = build_constructors()
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for c in CONSTRUCTS:
            rows.append((c.label, dep_ms(c, work), per_type_us(c, work),
                         instantiate_ns(ctors[c.key])))

    w = max(len(r[0]) for r in rows)
    print(f"{'construct':<{w}} {'import ms':>10} {'us/type':>9} {'inst ns':>9}")
    print("-" * (w + 31))
    for label, dep, us, ns in rows:
        print(f"{label:<{w}} {dep:>10.3f} {us:>9.1f} {ns:>9.1f}")

    record_row = rows[0]
    nt = next(r for r in rows if r[0].startswith("typing.NamedTuple"))
    ms = next(r for r in rows if r[0].startswith("msgspec"))
    print("\nTotal startup to define N record types = import_ms + N * us/type/1000")
    for n in (1, 10, 100, 1000):
        def total(r):
            return r[1] + n * r[2] / 1000
        print(f"  N={n:<5}  record {total(record_row):8.3f} ms   "
              f"NamedTuple {total(nt):8.3f} ms   msgspec {total(ms):8.3f} ms")


if __name__ == "__main__":
    main()
