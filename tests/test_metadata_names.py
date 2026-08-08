"""What the field metadata is called, and why it is called two things.

`_struct_fields_` and `_struct_defaults_` are salix's. The language reference
reserves `__name__` for "the interpreter and its implementation, including the
standard library", and says any other such use is "subject to breakage without
warning"; PEP 8 puts it as never invent one, only use a documented one. A
library that mints a dunder for itself is taking a name it was not offered.

`__struct_fields__` and `__struct_defaults__` are msgspec's, and they are here
because code written against msgspec reads them. That is the half PEP 8
permits: using another project's documented name is not inventing one.

So the two are not equal in standing, and the tests below say which is which
rather than treating them as interchangeable spellings.
"""

import pytest

from salix import Struct

SALIX = ("_struct_fields_", "_struct_defaults_")
MSGSPEC = ("__struct_fields__", "__struct_defaults__")
PAIRS = tuple(zip(SALIX, MSGSPEC, strict=True))
MIXIN = Struct.__mro__[1]
META = type(Struct)


class Point(Struct):
    x: int
    y: int = 2


@pytest.mark.parametrize(("ours", "theirs"), PAIRS)
def test_the_alias_answers_what_the_sunder_does_on_the_class(ours, theirs):
    assert getattr(Point, theirs) == getattr(Point, ours)


@pytest.mark.parametrize(("ours", "theirs"), PAIRS)
def test_the_alias_answers_what_the_sunder_does_on_an_instance(ours, theirs):
    assert getattr(Point(1), theirs) == getattr(Point(1), ours)


def test_the_sunder_reports_the_fields_and_the_defaults():
    assert Point._struct_fields_ == ("x", "y")
    assert Point._struct_defaults_ == (2,)


@pytest.mark.parametrize("name", SALIX + MSGSPEC)
def test_neither_spelling_may_be_assigned(name):
    """Both are getsets with no setter, on the class and on the instance."""

    with pytest.raises(AttributeError):
        setattr(Point, name, ("z",))

    with pytest.raises(TypeError, match="does not support attribute assignment"):
        setattr(Point(1), name, ("z",))


@pytest.mark.parametrize("name", SALIX + MSGSPEC)
def test_both_spellings_are_descriptors_rather_than_values_in_each_class(name):
    """A getset on the metaclass answers for the class and one on the mixin
    answers for the instance, so no class dict carries either -- which is what
    stops a subclass shadowing one spelling and leaving the other to report
    something the first no longer agrees with.
    """

    assert name not in vars(Point)
    assert name in vars(META)
    assert name in vars(MIXIN)


def test_a_subclass_reports_its_own_fields_under_both_names():
    class Extended(Point):
        z: int = 3

    assert Extended._struct_fields_ == ("x", "y", "z")
    assert Extended.__struct_fields__ == Extended._struct_fields_
    assert Extended._struct_defaults_ == (2, 3)
    assert Extended.__struct_defaults__ == Extended._struct_defaults_
