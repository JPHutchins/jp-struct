"""Methods on a struct, in the shapes people actually write them.

A struct's body is an ordinary class body, and the methods it defines are kept
-- except a name that collides with a field, which is not kept and not simply
lost either: it becomes that field's default. These are the codec-shaped methods
that motivate reaching for a struct in the first place -- `to_bytes`,
`to_string`, a `from_bytes` classmethod -- plus the descriptors that sit
alongside them, and the dunders that salix would otherwise generate.

Methods are the subject; `__slots__` and `__match_args__` are salix's whatever
the body says, and `TestBindingsSalixOwns` is where that is pinned.
"""

import functools
import struct as struct_module

import pytest

from salix import Struct, set_field

HEADER = struct_module.Struct("<HBB")


class Frame(Struct):
    """A wire frame, of the kind a firmware protocol hands around."""

    length: int
    kind: int
    flags: int = 0

    def to_bytes(self) -> bytes:
        return HEADER.pack(self.length, self.kind, self.flags)

    def to_string(self) -> str:
        return f"{self.kind:02x}:{self.length:04x}:{self.flags:02x}"

    @classmethod
    def from_bytes(cls, payload: bytes) -> "Frame":
        return cls(*HEADER.unpack(payload))

    @classmethod
    def empty(cls) -> "Frame":
        return cls(0, 0)

    @staticmethod
    def header_size() -> int:
        return HEADER.size

    @property
    def is_empty(self) -> bool:
        return self.length == 0

    def __len__(self) -> int:
        return self.length


class TestMethods:
    def test_an_instance_method_reads_the_fields(self):
        """The expected bytes written out rather than re-derived from `to_bytes`'
        own expression, so the assertion depends on the field values reaching
        the method in the right order and not merely on reaching it.
        """

        assert Frame(258, 7, 1).to_bytes() == b"\x02\x01\x07\x01"

    def test_a_second_instance_method_is_not_displaced_by_the_first(self):
        assert Frame(258, 7, 1).to_string() == "07:0102:01"

    def test_a_classmethod_constructs_through_the_generated_constructor(self):
        frame = Frame.from_bytes(HEADER.pack(258, 7, 1))

        assert frame == Frame(258, 7, 1)
        assert isinstance(frame, Frame)

    def test_a_round_trip_returns_an_equal_struct(self):
        original = Frame(4096, 3, 2)

        assert Frame.from_bytes(original.to_bytes()) == original

    def test_a_classmethod_taking_no_arguments_works_too(self):
        assert Frame.empty() == Frame(0, 0, 0)

    def test_a_staticmethod_needs_no_instance(self):
        """`<HBB` is four bytes; comparing against HEADER.size would compare the
        method's own return value with itself.
        """

        assert Frame.header_size() == 4
        assert Frame(1, 1).header_size() == 4

    def test_a_property_reads_the_fields(self):
        assert Frame(0, 0).is_empty
        assert not Frame(1, 0).is_empty

    def test_a_dunder_the_body_defines_is_kept(self):
        assert len(Frame(258, 7)) == 258

    def test_a_body_repr_and_init_displace_the_generated_ones(self):
        """Two different mechanisms reaching the same place. `__repr__` is a
        dunder the mixin binds and the body's rebinding wins in the MRO;
        `__init__` is not a dunder salix writes at all -- the constructor is a
        vectorcall, and `defines_own_init` drops it for `PyType_GenericNew` when
        the body defines one.

        `set_field` rather than `self.x = ...` because the struct is frozen by
        default, and the frozen wall does not open for its own `__init__` --
        test_custom_init.py pins both halves of that.
        """

        class Custom(Struct):
            x: int

            def __repr__(self) -> str:
                return "custom repr"

            def __init__(self, x: int) -> None:
                set_field(self, "x", x * 10)

        instance = Custom(1)

        assert repr(instance) == "custom repr"
        assert instance.x == 10
        assert Custom(1) == Custom(1)
        assert hash(Custom(1)) == hash((10,))

    def test_the_generated_dunders_survive_alongside_them(self):
        frame = Frame(258, 7, 1)

        assert repr(frame) == "Frame(length=258, kind=7, flags=1)"
        assert frame == Frame(258, 7, 1)
        assert hash(frame) == hash((258, 7, 1))

    def test_the_docstring_the_body_wrote_is_the_class_docstring(self):
        assert Frame.__doc__ is not None
        assert "wire frame" in Frame.__doc__


class TestInheritedMethods:
    def test_a_subclass_inherits_them(self):
        class Tagged(Frame):
            tag: int = 0

        tagged = Tagged(258, 7, 1, 9)

        assert tagged.header_size() == 4
        assert tagged.is_empty is False
        assert tagged.to_string() == "07:0102:01"

    def test_a_subclass_may_override_one(self):
        class Loud(Frame):
            def to_string(self) -> str:
                return super().to_string().upper()

        assert Loud(258, 10, 1).to_string() == "0A:0102:01"

    def test_a_classmethod_on_a_subclass_builds_the_subclass(self):
        class Tagged(Frame):
            tag: int = 0

        built = Tagged.empty()

        assert isinstance(built, Tagged)
        assert built.tag == 0


class TestCaching:
    def test_functools_cached_property_is_not_supported(self):
        """A struct's fields are its slots and it has no instance dict, so there
        is nowhere for cached_property to put the value.

        `dataclass(slots=True)` fails with the same message. NamedTuple fails
        too, but for its own reason below 3.13 -- `Cannot use cached_property
        instance without calling __set_name__ on it`, because the NamedTuple
        machinery never makes that call -- and with the missing-dict message
        from 3.13 on. Only a dataclass carrying a __dict__ succeeds.
        """

        class Cached(Struct):
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        with pytest.raises(TypeError, match="No '__dict__' attribute"):
            _ = Cached(2).expensive

    def test_a_field_is_the_place_to_cache_instead(self):
        """The cache is a declared field, and __post_init__ fills it through the
        frozen wall.

        A slotted dataclass can do this too -- directly when it is mutable, and
        through `object.__setattr__` when it is frozen. What set_field buys is
        that it is the supported path rather than an escape hatch around the
        class's own rules.

        test_post_init.py pins the mechanism; what is here is the caching claim
        -- that the value is computed once, at construction, and read from a
        slot afterwards.
        """

        calls = []

        class Cached(Struct):
            x: int
            expensive: int = 0

            def __post_init__(self) -> None:
                calls.append(1)
                set_field(self, "expensive", self.x * 100)

        instance = Cached(2)

        assert [instance.expensive for _ in range(3)] == [200, 200, 200]
        assert len(calls) == 1

    def test_functools_cache_on_a_method_still_works(self):
        """It caches on the function rather than the instance, so the absent
        __dict__ is not in its way -- at the cost of holding the struct alive.
        """

        calls = []

        class Computed(Struct):
            x: int

            @functools.cache  # noqa: B019 -- the lifetime cost is the point
            def slow(self) -> int:
                calls.append(1)
                return self.x * 100

        instance = Computed(2)

        assert instance.slow() == 200
        assert instance.slow() == 200
        assert len(calls) == 1

    def test_that_cache_is_shared_by_every_equal_instance(self):
        """The entry is keyed by `self`, and a struct hashes by field values, so
        two structs that compare equal are one key. Right for a pure function of
        the fields, and wrong the moment the method reads anything else.
        """

        calls = []

        class Computed(Struct):
            x: int

            @functools.cache  # noqa: B019 -- the lifetime cost is the point
            def slow(self) -> int:
                calls.append(1)
                return self.x * 100

        first, second = Computed(2), Computed(2)

        assert first is not second
        assert first.slow() == 200
        assert second.slow() == 200
        assert len(calls) == 1


class TestBindingsSalixOwns:
    """Not every body binding is the body's to keep. These two are salix's
    whatever the class writes, and neither collides with a field name.
    """

    def test_a_body_slots_is_replaced_by_the_fields(self):
        class Slotted(Struct):
            x: int
            __slots__ = ("extra",)

        assert Slotted.__slots__ == ("x",)

    def test_a_body_match_args_is_replaced_by_the_fields(self):
        class Matched(Struct):
            x: int
            __match_args__ = ("nope",)

        assert Matched.__match_args__ == ("x",)


class TestNameCollisions:
    """A method named after a field is not discarded -- it becomes that field's
    default, which is worse. `append_declared` reads the class-body value bound
    to the field name, and a `def` is a class-body value bound to a name.

    So the class builds, `Collide.x` is the slot descriptor, and `Collide()`
    hands back an instance whose int field holds a function. #54 is the issue.
    """

    def test_the_method_is_gone_from_the_class(self):
        class Collide(Struct):
            x: int

            def x(self) -> str:
                return "method"

        assert type(Collide.x).__name__ == "member_descriptor"
        assert Collide(1).x == 1

    def test_but_it_became_the_default_and_the_class_is_constructible(self):
        """The half `Collide(1).x == 1` cannot see. Constructing with no
        argument is accepted, because the field now has a default.
        """

        class Collide(Struct):
            x: int

            def x(self) -> str:
                return "method"

        (default,) = Collide.__struct_defaults__

        assert callable(default)
        assert Collide().x is default

    def test_a_property_collides_the_same_way(self):
        class CollideProp(Struct):
            y: int

            @property
            def y(self) -> str:
                return "property"

        (default,) = CollideProp.__struct_defaults__

        assert isinstance(default, property)
        assert CollideProp().y is default
        assert CollideProp(1).y == 1
