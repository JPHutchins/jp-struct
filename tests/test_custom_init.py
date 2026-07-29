"""A class body that writes its own `__init__` keeps it.

The generated constructor is a vectorcall on the type, and a vectorcall answers
before `tp_call` ever reaches `__init__` -- so a body that defined one used to
have it discarded without a word. A class that defines one now declines the
vectorcall instead, which also means it declines everything the vectorcall does:
defaults and `__post_init__` included.
"""

import sys

import pytest

from jpstruct import Struct


class Generated(Struct):
    x: int
    y: int = 0


class Handwritten(Struct, frozen=False):
    x: int
    y: int = 0

    def __init__(self, both: int) -> None:
        self.x = both
        self.y = both


def test_the_generated_constructor_is_still_the_default():
    assert Generated(1, 2) == Generated(x=1, y=2)
    assert Generated(1).y == 0


def test_a_body_init_runs():
    assert Handwritten(7) == Handwritten(7)
    assert (Handwritten(7).x, Handwritten(7).y) == (7, 7)


def test_the_signature_is_the_body_s():
    with pytest.raises(TypeError, match="takes 2 positional arguments"):
        Handwritten(1, 2)


def test_the_struct_machinery_is_otherwise_untouched():
    assert Handwritten.__struct_fields__ == ("x", "y")
    assert Handwritten.__match_args__ == ("x", "y")
    assert repr(Handwritten(7)) == "Handwritten(x=7, y=7)"
    assert Handwritten(7) == Generated(7, 7)


def test_an_inherited_init_is_honoured_too():
    class Child(Handwritten):
        pass

    assert Child(3) == Handwritten(3)


def test_a_child_may_go_back_to_the_generated_one():
    """__init__ is a name like any other; object's is what un-defines it."""

    class Child(Handwritten):
        __init__ = object.__init__  # type: ignore[assignment]

    assert (Child(1, 2).x, Child(1, 2).y) == (1, 2)
    assert Child(1).y == 0


def test_defaults_are_not_filled_for_a_body_init():
    """The vectorcall is what fills them, and this class declined it."""

    class Partial(Struct, frozen=False):
        x: int
        y: int = 99

        def __init__(self) -> None:
            self.x = 1

    with pytest.raises(AttributeError):
        _ = Partial().y


def test_post_init_does_not_run_for_a_body_init():
    ran = []

    class Both(Struct, frozen=False):
        x: int

        def __init__(self) -> None:
            self.x = 1

        def __post_init__(self) -> None:  # pragma: no cover
            ran.append(True)

    Both()

    assert ran == []


def test_a_frozen_struct_with_a_body_init_cannot_write_its_fields():
    """Frozen is frozen; a struct whose __init__ assigns asks for frozen=False."""

    class Frozen(Struct):
        x: int

        def __init__(self) -> None:
            self.x = 1

    with pytest.raises(TypeError, match="does not support attribute assignment"):
        Frozen()


@pytest.mark.skipif(
    sys.version_info < (3, 13), reason="object.__setattr__ reaches a struct only on 3.13+"
)
def test_a_frozen_body_init_can_use_object_setattr_where_the_interpreter_allows_it():
    class Frozen(Struct):
        x: int

        def __init__(self) -> None:
            object.__setattr__(self, "x", 1)

    assert Frozen().x == 1


def test_the_mixin_stays_uninstantiable():
    """Only a class that declined the vectorcall gets tp_new, and it is a struct.

    Otherwise the mixin's dunders would be reachable over an object with no
    field table behind it.
    """

    with pytest.raises(TypeError, match="cannot create"):
        Struct.__mro__[1]()
