"""camas task definitions for jp-struct — the single source of truth for local and CI."""

from pathlib import Path

from camas import Claude, Config, Parallel, Sequential, Task, by_suffix

C_SOURCES = by_suffix((".c", ".h"), default=tuple(sorted(str(p) for p in Path("src").glob("*.[ch]"))))

build = Task("uv run python setup.py build_ext --inplace", mutates=True)
pytest = Task("uv run python -m pytest")
run_benchmark = Task("uv run python bench/bench.py")
compile_commands = Task("uv run python tools/compile_commands.py", mutates=True)
clean = Task("uv run python tools/clean.py", mutates=True)
c_format = Task("jphfmt -i {paths}", paths=C_SOURCES, mutates=True)
c_format_check = Task("jphfmt --check {paths}", paths=C_SOURCES)
test = Sequential(build, pytest)
benchmark = Sequential(build, run_benchmark)
check = Parallel(test, c_format_check)

_ = Config(default_task=check, github_task=check, agent=Claude(fix=c_format, check=check))
