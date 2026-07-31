"""Write `compile_flags.txt`, which clangd and clang-tidy both read directly.

One flag set covers every translation unit here, so the fixed-flags form does
the job a compilation database would, and neither tool needs the build to have
run. The flags are what setuptools composes: sysconfig's, then the project's.
"""

import shlex
import sys
import sysconfig
from pathlib import Path
from typing import Final

ROOT: Final = Path(__file__).resolve().parent.parent
DATABASE: Final = ROOT / "compile_flags.txt"

sys.path.insert(0, str(ROOT))

from build_config import BUILD, STRICT  # noqa: E402

FLAGS: Final = (
    *shlex.split(sysconfig.get_config_var("CFLAGS")),
    *shlex.split(sysconfig.get_config_var("CCSHARED")),
    f"-I{sysconfig.get_config_var('prefix')}/include",
    f"-I{sysconfig.get_paths()['include']}",
    *BUILD.c_flags,
    *STRICT,
)


if __name__ == "__main__":
    DATABASE.write_text("\n".join(FLAGS) + "\n")
    print(f"wrote {len(FLAGS)} flags to {DATABASE.relative_to(ROOT)}")
