"""Argument binding: what reaches a slot, and what is rejected."""

import pytest

from salix import Struct


class Point(Struct):
    x: int
    y: int


class Mutable(Struct, frozen=False):
    a: int


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
    gets. Exactly four builtins are copied at construction, and everything else
    is shared -- which is cheaper and indistinguishable for a value that cannot
    be mutated, and simply sharing for one that can. `array.array`, `deque`, a
    writable `memoryview` and the subclasses of the four are all in the second
    group; #51 argues for hashability as the test that would replace the list.
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
        """The int and the str are built at runtime rather than written as
        literals: a small int and an interned literal are shared by CPython
        whatever salix does, so asserting identity on those proves nothing.

        `"not interned " * 2` is not enough -- the peephole optimiser folds it
        to one constant and two evaluations give the same object, which is the
        trap this docstring named and the code then fell into. `str.join` runs.
        """

        class Inner(Struct):
            a: int

        uncached_number = 10**20
        uncached_text = "".join(["not ", "interned"])  # noqa: FLY002 -- see above

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

    def test_the_class_keeps_its_own_copy_of_the_declared_default(self):
        """The emptiness the refusal checked has to stay true. A module-level
        container declared as the default is still the module's to append to,
        and the class would otherwise be holding the same object -- so every
        instance would get a shallow copy of something the refusal rejected.

        msgspec severs the same alias by replacing the default with a Factory.
        """

        shared = []

        class Holder(Struct, frozen=False):
            xs: list = shared

        shared.append([2])

        assert Holder.__struct_defaults__[0] is not shared
        assert Holder().xs == []

    def test_construction_and_inheritance_agree_after_that_mutation(self):
        """One copies and one refuses, so they have to be looking at the same
        thing: the stored default, which nothing outside the class can fill.
        """

        shared = []

        class Holder(Struct, frozen=False):
            xs: list = shared

        shared.append([2])

        class Inheriting(Holder):
            pass

        assert Inheriting().xs == []

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

    @pytest.mark.parametrize(
        "value",
        [
            pytest.param(__import__("array").array("i", [1, 2]), id="array"),
            pytest.param(__import__("collections").deque([1, 2]), id="deque"),
            pytest.param(memoryview(bytearray(b"abc")), id="memoryview"),
            pytest.param(Mutable(1), id="a_mutable_struct"),
        ],
    )
    def test_a_mutable_container_outside_the_four_is_shared_and_not_refused(self, value):
        """The boundary is the four exact types, not mutability, so these are
        neither copied nor refused. Every one is unhashable, which is the test
        #51 argues should replace the list -- salix's own `frozen=False` struct
        included, since `eq` without `frozen` sets `__hash__` to None.

        `hash(value)` rather than `isinstance(value, Hashable)`: the ABC asks
        whether `__hash__` is non-None, and a writable memoryview has one that
        raises when called. Only the call is the test.
        """

        with pytest.raises((TypeError, ValueError)):
            hash(value)

        class Holder(Struct, frozen=False):
            v: object = value

        assert Holder().v is Holder().v
        assert Holder.__struct_defaults__[0] is value

    def test_a_set_default_is_refused_before_its_elements_are_hashed(self):
        """Copying a set hashes every element, so copying before refusing would
        run user code at class definition on the way to an error -- and let an
        element's exception replace the TypeError the refusal promises.
        """

        hashed = []

        class Counts:
            def __hash__(self) -> int:
                hashed.append(1)
                return 0

        contents = {Counts()}
        hashed.clear()  # building the set literal hashed it once, before salix

        with pytest.raises(TypeError, match="non-empty set"):

            class Holder(Struct):
                v: object = contents

        assert hashed == []

    def test_a_body_init_does_not_exempt_the_declared_default(self):
        """Its constructor never reads the default, so nothing is shared -- but
        the declaration is still a promise the class makes through
        __struct_defaults__, and dataclasses refuses it under init=False for the
        same reason. The message names both hooks because __post_init__ is not
        one of them here: a body __init__ displaces the generated constructor,
        and run_post_init goes with it.
        """

        with pytest.raises(TypeError, match=r"non-empty list.*your own __init__"):

            class Holder(Struct, frozen=False):
                xs: list = ["a", "b"]  # noqa: RUF012 -- the refusal is the assertion

                def __init__(self) -> None:
                    self.xs = []

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
