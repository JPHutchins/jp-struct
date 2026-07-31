"""What the struct machinery does when handed something that is not a struct.

`_StructMixin` is a permitted base and `StructMeta` is exported, so both are
reachable without `Struct` anywhere in the bases. Everything asserted here used
to be a segfault or a class that reported its fields and behaved like `object`.

The crashes cannot be asserted directly -- a segfault takes pytest with it -- so
each one is pinned by the guarded outcome it now produces.
"""

import pytest

import salix
from salix import Struct

MIXIN = Struct.__mro__[1]


class Point(Struct):
    x: int


class Ordered(Struct, order=True):
    x: int


@pytest.fixture
def impostor():
    """A mixin subclass. The co-base supplies the tp_new the mixin withholds,
    which is what makes one of these constructible at all.
    """

    return type("Impostor", (MIXIN, list), {"__slots__": ("z",)})()


class TestAMixinSubclass:
    def test_a_struct_does_not_compare_equal_to_one(self, impostor):
        assert Point(1) != impostor
        assert impostor != Point(1)
        assert (Point(1) == impostor) is False

    def test_it_does_not_order_against_a_struct(self, impostor):
        with pytest.raises(TypeError):
            _ = Ordered(1) < impostor

        with pytest.raises(TypeError):
            _ = impostor < Ordered(1)

    def test_it_reprs_as_the_object_it_is(self, impostor):
        assert "Impostor object at" in repr(impostor)

    def test_it_hashes_by_identity(self, impostor):
        assert hash(impostor) == object.__hash__(impostor)

    def test_it_is_not_frozen(self, impostor):
        impostor.z = 1

        assert impostor.z == 1

    def test_it_has_no_field_names_to_report(self, impostor):
        with pytest.raises(TypeError, match="__struct_fields__ is defined on structs"):
            _ = impostor.__struct_fields__

    def test_it_has_no_defaults_to_report(self, impostor):
        with pytest.raises(TypeError, match="__struct_defaults__ is defined on structs"):
            _ = impostor.__struct_defaults__

    def test_set_field_still_refuses_it(self, impostor):
        with pytest.raises(TypeError, match="expects a struct"):
            salix.set_field(impostor, "z", 1)


class TestTheMetaclassWithoutTheMixin:
    def test_a_class_body_naming_it_is_refused(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):

            class Bare(metaclass=salix.StructMeta):
                x: int

    def test_calling_it_with_no_bases_is_refused(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):
            salix.StructMeta("Bare", (), {})

    def test_a_co_base_that_is_not_a_struct_does_not_smuggle_one_through(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):
            salix.StructMeta("Bare", (list,), {})

    def test_calling_it_with_a_struct_base_still_works(self):
        built = salix.StructMeta("Extended", (Point,), {"__annotations__": {"y": int}})

        assert built.__struct_fields__ == ("x", "y")
        assert built(1, 2) == built(1, 2)


class TestAMetaclassSubclass:
    def test_one_that_delegates_produces_a_struct(self):
        class Delegating(salix.StructMeta):
            pass

        class Delegated(Struct, metaclass=Delegating):
            a: int

        assert Delegated(1).__struct_fields__ == ("a",)
        assert type(Delegated) is Delegating

    @pytest.mark.parametrize("produced", ["not a type", bytearray(8192), 0])
    def test_one_that_returns_something_else_is_refused(self, produced):
        """type.__new__ hands off to the winning metatype, so the object
        create_class gets back is whatever this __new__ chose to return.
        """

        calls = []

        class Wrong(salix.StructMeta):
            def __new__(mcls, *args, **keywords):
                calls.append(1)

                if len(calls) == 1:
                    return super().__new__(mcls, *args, **keywords)

                return produced

        class Seeded(Struct, metaclass=Wrong):
            a: int

        with pytest.raises(TypeError, match="is not a struct class"):
            salix.StructMeta("Z", (Seeded,), {"__annotations__": {"b": int}})
