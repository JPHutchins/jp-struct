"""A field holds an arbitrary object, so the suite should pass arbitrary objects.

Split by hashability, because that is the one property of a field value the
struct itself is sensitive to.
"""

import array
import dataclasses
import datetime
import decimal
import enum
import fractions
import typing

from jpstruct import Struct


class Colour(enum.Enum):
    RED = 1
    GREEN = 2


class Flags(enum.IntFlag):
    NONE = 0
    ONE = 1


class Coordinate(typing.NamedTuple):
    latitude: float
    longitude: float


@dataclasses.dataclass(frozen=True)
class Frozen:
    label: str


@dataclasses.dataclass
class Mutable:
    label: str

    __hash__ = None  # type: ignore[assignment]


class Opaque:
    """Identity equality and identity hash, like most objects."""


class ByValue:
    def __init__(self, key: object) -> None:
        self.key = key

    def __eq__(self, other: object) -> bool:
        return isinstance(other, ByValue) and self.key == other.key

    def __hash__(self) -> int:
        return hash(self.key)

    def __repr__(self) -> str:
        return f"ByValue({self.key!r})"


class Inner(Struct):
    value: object


class Outer(Struct):
    inner: object
    tag: object


def _hashable() -> tuple[object, ...]:
    return (
        None,
        True,
        False,
        0,
        -1,
        2**128,
        3.5,
        -0.0,
        float("inf"),
        complex(1, 2),
        "",
        "text",
        "\N{SNOWMAN}\N{ROCKET}",
        b"bytes",
        (),
        (1, ("nested",)),
        frozenset({1, 2}),
        range(3),
        Colour.RED,
        Flags.ONE,
        Coordinate(1.0, 2.0),
        Frozen("label"),
        Opaque(),
        ByValue("key"),
        decimal.Decimal("1.5"),
        fractions.Fraction(1, 3),
        datetime.date(2026, 7, 27),
        memoryview(b"bytes"),
        Inner(1),
        Inner(Inner("deep")),
        int,
        len,
        Ellipsis,
        NotImplemented,
    )


def _unhashable() -> tuple[object, ...]:
    return (
        [],
        [1, 2],
        {},
        {"key": "value"},
        {1, 2},
        bytearray(b"bytes"),
        array.array("i", [1, 2]),
        memoryview(bytearray(b"bytes")),
        Mutable("label"),
    )


HASHABLE = _hashable()
UNHASHABLE = _unhashable()
EVERY = HASHABLE + UNHASHABLE


def identify(value: object) -> str:
    """A stable, readable parametrize id for values whose repr is unwieldy."""

    return f"{type(value).__name__}-{id(value) % 9973:04d}"
