"""camas task definitions for jp-struct — the single source of truth for local and CI."""

from pathlib import Path

from camas import Claude, Config, Parallel, Project, Sequential, Task, by_suffix

C_SOURCES = by_suffix((".c", ".h"), default=tuple(sorted(str(p) for p in Path("src").glob("*.[ch]"))))

# Analysis takes translation units; a header is reached through the unit that
# includes it, and compiling one alone is an error under -Werror.
C_TRANSLATION_UNITS = by_suffix(
    (".c",), default=tuple(sorted(str(p) for p in Path("src").glob("*.c")))
)
NIX_SOURCES = by_suffix(
    (".nix",),
    default=tuple(sorted(str(p) for p in (*Path(".").glob("*.nix"), *Path("nix").glob("*.nix")))),
)
PYTHONS = tuple(Path(".python-version").read_text().split())
OLDEST = min(PYTHONS, key=lambda python: tuple(map(int, python.split("."))))
NEWEST = max(PYTHONS, key=lambda python: tuple(map(int, python.split("."))))

# --no-project: camas's dev group floors above the oldest interpreter here.
UV = "uv run --no-project --managed-python --python {PY} --with setuptools --with pytest"

NIX_INPUTS = ("src/", "nix/", "tools/", "tests/", "flake.nix", "flake.lock", "pyproject.toml")

build = Task(UV + " python setup.py build_ext --inplace", mutates=True)
pytest = Task(UV + " python -m pytest")
# --no-sync: analyze reaches this from ci, and CI never installs the project.
compile_flags = Task("uv run --no-sync python tools/compile_flags.py", mutates=True)

# Everything untracked, less the environment, camas's timings and local settings.
clean = Task("git clean -xdf -e .venv -e .camas -e .claude", mutates=True)
update_python_targets = Task("uv run python tools/update_python_targets.py", mutates=True)

c_format = Task("jphfmt -i {paths}", paths=C_SOURCES, mutates=True)
c_format_check = Task("jphfmt --check {paths}", paths=C_SOURCES)
nix_format = Task("nixfmt {paths}", paths=NIX_SOURCES, mutates=True)
nix_format_check = Task("nixfmt --check {paths}", paths=NIX_SOURCES)
format = Parallel(c_format, nix_format)
format_check = Parallel(c_format_check, nix_format_check)

# Two engines rather than one: they are independent implementations, and the
# flags carry -Werror, so gcc also holds the build to a second compiler.
c_tidy = Task("clang-tidy --quiet {paths}", paths=C_TRANSLATION_UNITS)
c_analyzer = Task(
    "gcc -fanalyzer -fsyntax-only @compile_flags.txt {paths}", paths=C_TRANSLATION_UNITS
)
analyze = Sequential(compile_flags, Parallel(c_tidy, c_analyzer))

bench = Project("bench")

wheels = Task("nix build .#default --out-link result-wheels", when=NIX_INPUTS, mutates=True)
flake_check = Task("nix flake check", when=NIX_INPUTS)

test = Parallel(Sequential(build, pytest), matrix={"PY": PYTHONS})

# Kept out of .python-version, which is also the wheel matrix, and there are no
# free-threaded wheels yet. A module that does not declare Py_mod_gil silently
# re-enables the GIL, so the declaration needs a build that would notice.
FREE_THREADED = "3.14t"
free_threaded_build = Task(
    UV.format(PY=FREE_THREADED) + " python setup.py build_ext --inplace", mutates=True
)
free_threaded_pytest = Task(UV.format(PY=FREE_THREADED) + " python -m pytest")
free_threaded = Sequential(free_threaded_build, free_threaded_pytest)
benchmark = Sequential(Task("uv run python setup.py build_ext --inplace", mutates=True), bench)
check = Parallel(test, free_threaded, format_check, analyze)

# Installed, not compiled: MSVC has no __attribute__((cleanup)), so the Windows
# leg cannot build this source at all.
wheel_test = Task(
    "uv run --no-project --managed-python --python {PY} --find-links result-wheels --with jp-struct"
    " --with pytest python -m pytest"
)

# Sampled rather than crossed, to stay inside the OSS concurrency limit.
coverage = Parallel(
    wheel_test,
    variants=tuple({"OS": "ubuntu-latest", "PY": python} for python in PYTHONS)
    + (
        {"OS": "macos-latest", "PY": OLDEST},
        {"OS": "macos-latest", "PY": NEWEST},
        {"OS": "windows-latest", "PY": OLDEST},
        {"OS": "windows-latest", "PY": NEWEST},
    ),
)

ci = Parallel(flake_check, free_threaded, format_check, analyze)

_ = Config(default_task=check, github_task=ci, agent=Claude(fix=format, check=check))
