"""camas task definitions for jp-struct — the single source of truth for local and CI."""

from camas import Config, Sequential, Task

build = Task("uv run python setup.py build_ext --inplace", mutates=True)
pytest = Task("uv run python -m pytest")
run_benchmark = Task("uv run python bench/bench.py")
compile_commands = Task("uv run python tools/compile_commands.py", mutates=True)
clean = Task("uv run python tools/clean.py", mutates=True)
test = Sequential(build, pytest)
benchmark = Sequential(build, run_benchmark)

_ = Config(default_task=test, github_task=test)
