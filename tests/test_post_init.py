"""`__post_init__`, which the constructor calls once every field is written.

There is no keyword for it: a class either defines one or it does not, and the
constructor was silently discarding the definition before. The hook is resolved
at class creation, so one bound to the class afterwards is not seen -- that is
the price of keeping the constructor's cost to a null check.
"""

import sys

import pytest
from values import EVERY, identify

from jpstruct import Struct


class Validated(Struct):
    x: int

    def __post_init__(self) -> None:
        if self.x < 0:
            raise ValueError("x must be non-negative")


class Derived(Struct, frozen=False):
    reading: int
    doubled: int = 0

    def __post_init__(self) -> None:
        self.doubled = self.reading * 2


def test_a_struct_without_one_is_unaffected():
    class Plain(Struct):
        x: int

    assert Plain.__struct_fields__ == ("x",)
    assert Plain(1).x == 1


def test_it_runs_during_construction():
    assert Validated(1).x == 1


def test_a_raise_reaches_the_caller():
    with pytest.raises(ValueError, match="x must be non-negative"):
        Validated(-1)


def test_every_field_is_written_before_it_runs():
    seen: list[tuple[object, ...]] = []

    class Observer(Struct):
        a: object
        b: object = "default"

        def __post_init__(self) -> None:
            seen.append((self.a, self.b))

    Observer(1)
    Observer(1, 2)

    assert seen == [(1, "default"), (1, 2)]


def test_a_mutable_struct_can_derive_a_field_from_the_others():
    assert Derived(21).doubled == 42


def test_object_setattr_on_a_frozen_struct_follows_the_interpreter():
    """The frozen-dataclass escape hatch reaches a struct only on 3.13 and up.

    Before that, CPython's setattr hackcheck walked past the heap types to the
    first static base and refused unless that one's tp_setattro was the generic
    one. For a dataclass that base is object; for a struct it is the mixin,
    whose slot is what freezing *is*. 3.13 dropped the check.

    So validation in `__post_init__` is portable and deriving a field is not --
    a struct that derives asks for frozen=False, as `Derived` does.
    """

    class Frozen(Struct):
        reading: int
        doubled: int = 0

        def __post_init__(self) -> None:
            object.__setattr__(self, "doubled", self.reading * 2)

    if sys.version_info >= (3, 13):
        assert Frozen(21).doubled == 42
    else:
        with pytest.raises(TypeError, match="can't apply this __setattr__"):
            Frozen(21)


def test_it_is_inherited():
    class Child(Derived):
        label: str = "child"

    assert Child(21) == Child(21, 42, "child")


def test_a_child_may_replace_it():
    class Child(Derived):
        def __post_init__(self) -> None:
            self.doubled = -1

    assert Child(21).doubled == -1


def test_the_hook_is_resolved_at_class_creation():
    """Stated rather than lamented: this is what buys the null check."""

    class Late(Struct):
        x: int

    Late.__post_init__ = lambda self: pytest.fail("should not run")  # type: ignore[attr-defined]

    assert Late(1).x == 1


@pytest.mark.parametrize("value", EVERY, ids=identify)
def test_it_sees_any_field_value(value):
    seen: list[object] = []

    class Observer(Struct):
        held: object

        def __post_init__(self) -> None:
            seen.append(self.held)

    Observer(value)

    assert seen[0] is value
