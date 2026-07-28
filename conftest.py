"""Make the in-place-built `jpstruct` extension importable during tests.

`build_ext --inplace` drops `jpstruct.*.so` under src/ rather than installing it,
so src/ goes on the path -- but only when nothing already provides `jpstruct`, so
that a run against an installed wheel is never silently shadowed by a stale
local build.
"""

import importlib.util
import sys
from pathlib import Path

if importlib.util.find_spec("jpstruct") is None:
    _root = Path(__file__).parent

    for _directory in (_root / "src", _root):
        sys.path.insert(0, str(_directory))
