"""Argument binding: what reaches a slot, and what is rejected."""

import pytest

from salix import Struct


class Point(Struct):
    x: int
    y: int


class Defaulted(Struct):
    a: int
    b: int = 2
    c: int = 3


class Empty(Struct):
    pass


class Renamed(Struct):
    value_one: int
    value_two: int


def test_positional_and_keyword_reach_the_same_slots():
    assert Point(1, 2) == Point(x=1, y=2) == Point(1, y=2)


def test_defaults_fill_what_is_not_supplied():
    assert (Defaulted(1).b, Defaulted(1).c) == (2, 3)
    assert Defaulted(1, 9).b == 9
    assert Defaulted(1, c=9).c == 9


def test_a_value_is_stored_not_copied():
    value = [1]

    assert Point(value, 0).x is value


def test_none_is_a_value_rather_than_an_absence():
    assert Point(None, None).x is None
    assert repr(Point(None, None)) == "Point(x=None, y=None)"


def test_too_many_positional_arguments():
    with pytest.raises(TypeError, match="takes at most 2 positional arguments but 3 were given"):
        Point(1, 2, 3)


def test_missing_a_required_argument():
    with pytest.raises(TypeError, match="missing required argument 'y'"):
        Point(1)


def test_unexpected_keyword_argument():
    with pytest.raises(TypeError, match="unexpected keyword argument 'z'"):
        Point(1, 2, z=3)


def test_the_same_field_given_twice():
    with pytest.raises(TypeError, match="multiple values for argument 'x'"):
        Point(1, x=2)


def test_a_keyword_name_built_at_runtime_still_resolves():
    """The fast path compares interned names by identity; this misses it."""

    name = "".join(["value_", "one"])  # noqa: FLY002 -- a literal would be interned

    assert name is not "value_one"  # noqa: F632
    assert Renamed(**{name: 1}, value_two=2).value_one == 1


def test_an_empty_struct_accepts_nothing():
    assert Empty() == Empty()

    with pytest.raises(TypeError, match="takes at most 0 positional arguments"):
        Empty(1)

    with pytest.raises(TypeError, match="unexpected keyword argument"):
        Empty(nope=1)


def test_the_class_is_callable_through_the_slow_path_too():
    """tp_call routes to the same vectorcall, so apply() must agree with a call."""

    arguments = (1, 2)

    assert Point(*arguments) == Point.__call__(*arguments)


class TestMutableDefaults:
    """`xs: list = []` reads as an empty list per instance, and that is what it
    gets. The four builtins that mean "container I will mutate" are copied at
    construction; anything that cannot be changed out from under another
    instance is shared, which is cheaper and indistinguishable.
    """

    def test_a_list_default_is_not_shared(self):
        class Holder(Struct, frozen=False):
            xs: list = []  # noqa: RUF012 -- the copy is the feature under test

        first, second = Holder(), Holder()
        first.xs.append(1)

        assert second.xs == []

    def test_frozen_does_not_make_the_copy_unnecessary(self):
        """Frozen stops rebinding, not mutation -- the same as a frozen
        dataclass -- so the default has to be copied even here. Immutability of
        the struct is not immutability of what its field points at.
        """

        class Holder(Struct):
            xs: list = []  # noqa: RUF012 -- the copy is the feature under test

        first, second = Holder(), Holder()
        first.xs.append(1)

        assert second.xs == []

    @pytest.mark.parametrize("factory", [dict, set, bytearray])
    def test_the_other_mutable_builtins_are_copied_too(self, factory):
        class Holder(Struct):
            v: object = factory()

        assert Holder().v is not Holder().v

    def test_an_immutable_default_is_shared(self):
        """The int and the str are built rather than written as literals: a
        small int and an interned literal are shared by CPython whatever salix
        does, so asserting identity on those proves nothing.
        """

        class Inner(Struct):
            a: int

        uncached_number = 10**20
        uncached_text = "not interned " * 2

        class Holder(Struct):
            number: int = uncached_number
            text: str = uncached_text
            pair: tuple = (1, 2)
            nested: Inner = Inner(1)

        first, second = Holder(), Holder()

        assert first.number is second.number
        assert first.text is second.text
        assert first.pair is second.pair
        assert first.nested is second.nested

    def test_a_supplied_value_is_still_stored_rather_than_copied(self):
        """Only the default is the class's to hand out repeatedly."""

        class Holder(Struct):
            xs: list = []  # noqa: RUF012 -- the copy is the feature under test

        supplied = [1]

        assert Holder(supplied).xs is supplied

    def test_a_non_empty_container_is_refused_rather_than_shallow_copied(self):
        """Copying it could only be shallow, so every instance would get its own
        outer list around the same inner one -- the same bug one level down.
        """

        with pytest.raises(TypeError, match="non-empty list"):

            class Nested(Struct):
                xs: list = [[1]]  # noqa: RUF012 -- the refusal is the assertion

    @pytest.mark.parametrize("value", [{"k": 1}, {1, 2}, bytearray(b"x")])
    def test_every_non_empty_builtin_is_refused(self, value):
        with pytest.raises(TypeError, match="non-empty"):

            class Holder(Struct):
                v: object = value

    def test_a_subclass_of_a_mutable_builtin_is_shared_not_copied(self):
        """The copy has to preserve the type and PyDict_Copy of a defaultdict is
        a dict, so subclasses are left alone -- as msgspec leaves them.
        """

        from collections import defaultdict

        class Holder(Struct):
            d: object = defaultdict(list)

        assert Holder().d is Holder().d

    def test_an_inherited_default_is_copied_as_well(self):
        class Base(Struct, frozen=False):
            xs: list = []  # noqa: RUF012 -- the copy is the feature under test

        class Child(Base):
            y: int = 0

        first, second = Child(), Child()
        first.xs.append(1)

        assert second.xs == []
