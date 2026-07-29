# jp-struct

A C-backed, inheritable `Struct` base class for Python, for programs that pay
for their imports.

```python
>>> from jpstruct import Struct

>>> class Point(Struct):
...     x: float
...     y: float = 0.0

>>> Point(1.0, 2.0)
Point(x=1.0, y=2.0)

```

`jpstruct/__init__.pyi` is the API. `src/jpstruct.c` says what this is and is
not. `uv run camas benchmark` says what it costs.

## Working in the repo

```sh
nix develop
uv run camas check
```

`tasks.py` is what runs, here and in CI. Reach camas through `uv run` — a
`camas` on `PATH` is whatever version is installed globally.

## License

MIT
