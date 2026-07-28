"""Make the in-place-built `jpstruct` extension importable during tests.

It lives here rather than at the repo root for a reason that is not cosmetic:
pytest puts a collected conftest's own directory on sys.path, so a root-level
one silently adds the working tree -- where an in-place build leaves
jpstruct.<abi>.so -- ahead of anything installed. A leg meant to test a wheel
would then test the tree instead. See tests/test_packaging.py.
"""

import importlib.util
import sys
from pathlib import Path

if importlib.util.find_spec("jpstruct") is None:
    _root = Path(__file__).resolve().parent.parent

    for _directory in (_root / "src", _root):
        sys.path.insert(0, str(_directory))
