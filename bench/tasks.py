"""camas tasks for the benchmark member.

--package: msgspec and record-type belong to this member, and a root-level
`uv run` neither installs nor guarantees them.
"""

from camas import Config, Task

run = Task("uv run --package jpstruct-bench python bench.py")

_ = Config(default_task=run)
