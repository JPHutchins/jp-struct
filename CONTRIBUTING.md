# Contributing

## The environment

```sh
nix develop
```

That is not a convenience. `clang-tidy`, `gcc -fanalyzer`, `nixfmt` and
`jphfmt` all come from the dev shell, the shell deliberately carries no Python
so that `uv` supplies every interpreter the matrix names, and CI installs Nix
and enters the same shell. A checkout without it can build the extension and
little else.

## The loop

```sh
uv run camas check      # the default task
uv run camas ci         # exactly what CI runs
```

`camas` is one definition of the task tree shared by this repository and its
workflows, so `ci` locally is the same tree GitHub runs, not an approximation
of it. `uv run camas --list` names the rest; the ones you are most likely to
want alone are `test`, `type_check`, `analyze` and `format`.

`check` is the inner loop: six CPython versions plus free-threaded 3.14, the
linters, the analyzers and the three type checkers. `ci` adds `nix flake
check`, which builds the wheels and takes minutes.

Run `uv run camas format` before committing, or `check` will tell you to.

## What the tests are

- `tests/` is pytest, and every supported interpreter runs all of it.
- `tests/typing/accepted.py` and `tests/typing/rejected.py` are assertions
  about mypy, pyright and ty rather than about runtime behaviour. `rejected.py`
  asserts by inversion: each `# type: ignore` is a claim that the checker
  reports something there, and `--warn-unused-ignores` makes a suppression that
  stops being needed an error.
- `README.md` is collected by pytest as doctests, so its examples are run and
  not proof-read.
- `src/*.c` carry in-file C tests, built and run by `nix build .#c-tests`.

A fix for a crash needs a test that asserts the guarded outcome — an
exception, or the right answer — because a segfault takes the test runner with
it and proves nothing.

## Style

Follow the code already in the file you are editing. The C is formatted by
`jphfmt` and there is no Python formatter on purpose; `ruff` is configured to
find defects, not to have opinions about layout.

The C targets C23 and builds with `-Werror -Wall -Wextra -Wdouble-promotion`.

## Commits and pull requests

Conventional prefixes — `feat:`, `fix:`, `build:`, `refactor:`, `style:`,
`chore:`. The body is where reasoning belongs: what was measured, what was
tried and rejected, and why. That is context the source cannot carry.

Every pull request is reviewed automatically once CI finishes. The review runs
from the default branch and comments on the pull request.

Work authored by an LLM agent must say so, in the commit trailer and in
anything it posts:

```
Co-Authored-By: <model-id> <noreply@<provider>>
```
