"""Make the in-place-built `record` extension importable during tests.

`make build` (build_ext --inplace) drops `record.*.so` under src/ rather than
installing the package, so we prepend src/ (and the repo root, as a fallback) to
sys.path here. pytest imports this conftest before collecting tests, so
`import record` works without an install.
"""

import sys
from pathlib import Path

# `build_ext --inplace` lands record.*.so under src/ (src-layout) or the repo
# root depending on setuptools config; put both on the path so import works
# either way without installing the package.
_root = Path(__file__).parent
for _d in (_root / "src", _root):
    sys.path.insert(0, str(_d))
