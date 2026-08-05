"""What the struct machinery does when handed something that is not a struct.

Neither the mixin nor the metaclass is exported, and both are one attribute
lookup from `Struct`, so both are reachable without `Struct` anywhere in the
bases. Most of what is asserted here used to be a segfault or a class that
reported its fields and behaved like `object`;
`TestTheUninstalledClassCannotBeBuilt` is the exception, pinning a CPython
guard that salix relies on and never crashed without.

The crashes cannot be asserted directly -- a segfault takes pytest with it -- so
each one is pinned by the guarded outcome it now produces.
"""

import pytest

import salix
from salix import Struct

MIXIN = Struct.__mro__[1]
META = type(Struct)


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
    def test_the_module_does_not_offer_it(self):
        """#15: reaching the metaclass takes `type(Struct)`, which is what the
        rest of this class does. What is gone is the invitation to.
        """

        assert not hasattr(salix, "StructMeta")
        assert META.__name__ == "_StructMeta"

    def test_a_class_body_naming_it_is_refused(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):

            class Bare(metaclass=META):
                x: int

    def test_calling_it_with_no_bases_is_refused(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):
            META("Bare", (), {})

    def test_a_co_base_that_is_not_a_struct_does_not_smuggle_one_through(self):
        with pytest.raises(TypeError, match="a struct class inherits salix"):
            META("Bare", (list,), {})

    def test_calling_it_with_a_struct_base_still_works(self):
        built = META("Extended", (Point,), {"__annotations__": {"y": int}})

        assert built.__struct_fields__ == ("x", "y")
        assert built(1, 2) == built(1, 2)


class TestTheUninstalledClassCannotBeBuilt:
    """`is_struct` asks the metaclass, so every instance of it has to have
    a field table. What guarantees it is CPython's own `tp_new_wrapper`: the
    most derived non-heap base of the requested type is the metaclass, whose
    `tp_new` is not `type`'s, so `type.__new__` refuses to build one.

    That guard is load-bearing and it is CPython's, not salix's, so these pin it
    on every supported version rather than trusting it. What a class that got
    past it would do to the guarded slots is not asserted here, because the
    guard cannot be turned off to find out -- only that the invariant every one
    of those slots reads holds.
    """

    @staticmethod
    def metaclass_subclass_that_builds_with_type_new():
        class Substituting(META):
            def __new__(mcls, *args, **keywords):
                return type.__new__(mcls, *args, **keywords)

        return Substituting

    def test_type_new_refuses_the_metaclass(self):
        with pytest.raises(TypeError, match="is not safe"):
            type.__new__(META, "S", (Point,), {})

    def test_type_new_refuses_a_metaclass_subclass(self):
        class Inheriting(META):
            pass

        with pytest.raises(TypeError, match="is not safe"):
            type.__new__(Inheriting, "S", (Point,), {})

    def test_a_metaclass_new_that_reaches_for_type_new_is_refused_too(self):
        """The substitution route into the same hole: the refusal follows the
        metatype rather than the call site.
        """

        Substituting = self.metaclass_subclass_that_builds_with_type_new()

        with pytest.raises(TypeError, match="is not safe"):
            type.__new__(Substituting, "S", (Point,), {})

        with pytest.raises(TypeError, match="is not safe"):
            Substituting("S", (Point,), {})

    def test_the_supported_route_still_installs(self):
        """types.new_class goes the long way round and comes out a real struct,
        so the refusals above are not refusing everything.
        """

        import types

        built = types.new_class("S", (Point,), {"metaclass": META})

        assert built.__struct_fields__ == ("x",)
        assert built(1) == built(1)


class TestAMetaclassSubclass:
    def test_one_that_delegates_produces_a_struct(self):
        class Delegating(META):
            pass

        class Delegated(Struct, metaclass=Delegating):
            a: int

        assert Delegated(1).__struct_fields__ == ("a",)
        assert type(Delegated) is Delegating

    def test_a_keyword_option_survives_the_handoff_to_a_derived_metatype(self):
        """type.__new__ would hand off to the derived metatype, which re-entered
        here with no keywords at all and planned the class without them.
        """

        class Delegating(META):
            pass

        class Base(Struct, metaclass=Delegating):
            x: int

        built = META("Built", (Base,), {"__annotations__": {"y": int}}, order=True)

        assert built(1, 2) < built(1, 3)
        assert type(built) is Delegating

    def test_two_unrelated_metatypes_still_raise_the_conflict(self):
        """Picking the winner ourselves must not swallow the case that has no
        winner: type_new is handed the requested metatype and says so.
        """

        class Left(META):
            pass

        class Right(META):
            pass

        class FromLeft(Struct, metaclass=Left):
            x: int

        class FromRight(Struct, metaclass=Right):
            y: int

        with pytest.raises(TypeError, match="metaclass conflict"):
            META("Both", (FromLeft, FromRight), {})

    def test_a_default_survives_the_handoff_to_a_derived_metatype(self):
        """The re-entered call read the namespace after drop_class_variables had
        taken the field names out of it, so every default declared in the body
        was gone by the time it planned the fields.
        """

        class Delegating(META):
            pass

        class Base(Struct, metaclass=Delegating):
            x: int

        namespace = {"__annotations__": {"y": int}, "y": 42}
        built = META("Built", (Base,), namespace)

        assert built(1).y == 42

    @pytest.mark.parametrize("produced", ["not a type", bytearray(8192), 0])
    def test_one_that_returns_a_non_type_is_refused(self, produced):
        """type.__new__ hands off to the winning metatype, so the object
        create_class gets back is whatever this __new__ chose to return.

        A __new__ that builds its substitute with an inline `type(name, bases,
        ns)` never gets here: the bases carry the same metaclass, so `type` re-
        enters it and CPython raises RecursionError before anything is returned.
        That is metaclass semantics rather than salix's -- the same __new__ on a
        plain `type` subclass recurses identically -- so the refusal covers what
        a __new__ returns, not every way one can fail to return.
        """

        calls = []

        class Wrong(META):
            def __new__(mcls, *args, **keywords):
                calls.append(1)

                if len(calls) == 1:
                    return super().__new__(mcls, *args, **keywords)

                return produced

        class Seeded(Struct, metaclass=Wrong):
            a: int

        with pytest.raises(TypeError, match=r"Wrong\.__new__ returned .*is not a struct class"):
            META("Z", (Seeded,), {"__annotations__": {"b": int}})
