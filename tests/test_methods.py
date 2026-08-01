"""Methods on a struct, in the shapes people actually write them.

A struct's body is an ordinary class body, and the methods it defines are kept
-- except a name that collides with a field, which is not kept and not simply
lost either: it becomes that field's default. These are the codec-shaped methods
that motivate reaching for a struct in the first place -- `to_bytes`,
`to_string`, a `from_bytes` classmethod -- plus the descriptors that sit
alongside them, and the dunders that salix would otherwise generate.

Methods are the subject. `__slots__` is salix's whatever the body says, and
`__match_args__` is unless the class opts out of it; `TestBindingsSalixOwns`
pins both, including where the body wins.
"""

import dataclasses
import functools
import struct as struct_module
import sys
from typing import NamedTuple

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
        own expression, so a wrong field order fails here instead of shifting
        both sides together. An inert method returning the same constant would
        pass; `test_a_round_trip_returns_an_equal_struct` is what needs the
        fields.
        """

        assert Frame(258, 7, 1).to_bytes() == b"\x02\x01\x07\x01"

    def test_a_second_instance_method_is_not_displaced_by_the_first(self):
        assert Frame(258, 7, 1).to_string() == "07:0102:01"

    def test_a_classmethod_constructs_through_the_generated_constructor(self):
        """Fed the same independent bytes `to_bytes` is checked against, so the
        two directions are pinned separately rather than against each other.
        """

        frame = Frame.from_bytes(b"\x02\x01\x07\x01")

        assert (frame.length, frame.kind, frame.flags) == (258, 7, 1)

    def test_a_round_trip_returns_an_equal_struct(self):
        """Composing pack with unpack holds by the struct module's own inverse
        property, so this pins the classmethod reaching `__eq__` with a real
        instance and nothing about the wire format. What pins the format is the
        pair that check `to_bytes` and `from_bytes` against independent bytes.
        """

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
        frame = Frame(258, 7)

        assert (len(frame), frame.kind) == (258, 7)

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
        assert instance == Custom(1)
        assert hash(instance) == hash((10,))

    def test_a_body_eq_and_hash_displace_the_generated_ones(self):
        """The header says the body keeps the dunders salix would have written,
        and these are the two a body is most likely to want back.

        `__ne__` does not come with `__eq__`, which is #58: salix binds the
        mixin's, so `a == b` and `a != b` are both true here. A dataclass and a
        plain class both derive `!=` from the body's `__eq__` and disagree with
        salix on the second assertion below. Pinned as it is, so the fix has to
        come back and change this line.
        """

        class Loose(Struct):
            x: int

            def __eq__(self, other: object) -> bool:
                return isinstance(other, Loose)

            def __hash__(self) -> int:
                return 0

        first, second = Loose(1), Loose(2)

        assert first == second
        assert first != second  # #58: both are true, and that is the bug
        assert hash(first) == hash(Loose(99)) == 0
        assert len({first, second}) == 1

    def test_the_generated_dunders_survive_alongside_them(self):
        frame = Frame(258, 7, 1)

        assert repr(frame) == "Frame(length=258, kind=7, flags=1)"
        assert frame == Frame(258, 7, 1)
        assert hash(frame) == hash((258, 7, 1))

    def test_the_docstring_the_body_wrote_is_the_class_docstring(self):
        assert Frame.__doc__ is not None
        assert "wire frame" in Frame.__doc__


class TestInheritedMethods:
    """`Tagged` is the one subclass these share: declared once, so a change to
    Frame's fields lands in one place.
    """

    @staticmethod
    def tagged_subclass():
        class Tagged(Frame):
            tag: int = 0

        return Tagged

    def test_a_subclass_inherits_them(self):
        tagged = self.tagged_subclass()(258, 7, 1, 9)

        assert tagged.tag == 9
        assert tagged.header_size() == 4
        assert tagged.is_empty is False
        assert tagged.to_string() == "07:0102:01"

    def test_a_subclass_may_override_one(self):
        class Loud(Frame):
            def to_string(self) -> str:
                return super().to_string().upper()

        assert Loud(258, 10, 1).to_string() == "0A:0102:01"

    def test_a_classmethod_on_a_subclass_builds_the_subclass(self):
        Tagged = self.tagged_subclass()
        built = Tagged.empty()

        assert isinstance(built, Tagged)
        assert built.tag == 0


class TestCaching:
    """Where a computed value can live, given no instance dict.

    Filling a declared field from `__post_init__` with `set_field` is the other
    answer and the one salix supports; test_post_init.py pins it, including that
    the hook runs exactly once per construction.
    """

    @staticmethod
    def counted_cache():
        """A fresh struct and the call log of its cached method. Fresh per test,
        because the cache holds its keys -- the instances -- alive, so entries
        one test made would still answer for the next.
        """

        calls: list[int] = []

        class Computed(Struct):
            x: int

            @functools.cache  # noqa: B019 -- the lifetime cost is the point
            def slow(self) -> int:
                calls.append(1)
                return self.x * 100

        return Computed, calls

    def test_functools_cached_property_is_not_supported(self):
        """A struct's fields are its slots and it has no instance dict, so there
        is nowhere for cached_property to put the value.

        The siblings are asserted below rather than described here, because
        every parity claim this file made in prose has turned out to be wrong at
        least once.
        """

        class Cached(Struct):
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        with pytest.raises(TypeError, match="No '__dict__' attribute"):
            _ = Cached(2).expensive

    def test_a_slotted_dataclass_fails_the_same_way(self):
        @dataclasses.dataclass(slots=True)
        class Cached:
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        with pytest.raises(TypeError, match="No '__dict__' attribute"):
            _ = Cached(2).expensive

    def test_a_namedtuple_fails_too_for_its_own_reason_below_3_13(self):
        """Below 3.13 the NamedTuple machinery never calls `__set_name__`, so
        the descriptor complains about that rather than about the missing dict.
        """

        class Cached(NamedTuple):
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        expected = (
            "No '__dict__' attribute"
            if sys.version_info >= (3, 13)
            else "without calling __set_name__"
        )

        with pytest.raises(TypeError, match=expected):
            _ = Cached(2).expensive

    def test_only_a_dataclass_carrying_a_dict_succeeds(self):
        @dataclasses.dataclass
        class Cached:
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        assert Cached(2).expensive == 200

    def test_functools_cache_on_a_method_still_works(self):
        """It caches on the function rather than the instance, so the absent
        __dict__ is not in its way -- at the cost of holding the struct alive.
        """

        Computed, calls = self.counted_cache()
        instance = Computed(2)

        assert instance.slow() == 200
        assert instance.slow() == 200
        assert len(calls) == 1

    def test_that_cache_is_shared_by_every_equal_instance(self):
        """The entry is keyed by `self`, and a struct hashes by field values, so
        two structs that compare equal are one key. Right for a pure function of
        the fields, and wrong the moment the method reads anything else.
        """

        Computed, calls = self.counted_cache()
        first, second = Computed(2), Computed(2)

        assert first is not second
        assert first.slow() == 200
        assert second.slow() == 200
        assert len(calls) == 1


class TestBindingsSalixOwns:
    """Not every body binding is the body's to keep, and neither of these
    collides with a field name. `__slots__` is taken unconditionally;
    `__match_args__` only while the class wants one.
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

    def test_opting_out_of_match_args_leaves_the_body_its_own(self):
        """`match_args=False` means salix writes none, not that it writes an
        empty one -- so the body keeps what it wrote, and `__slots__` does not.
        """

        class Matched(Struct, match_args=False):
            x: int
            __match_args__ = ("nope",)
            __slots__ = ("extra",)

        assert Matched.__match_args__ == ("nope",)
        assert Matched.__slots__ == ("x",)


class TestNameCollisions:
    """A method named after a field is not discarded -- it becomes that field's
    default, which is worse. `append_declared` reads the class-body value bound
    to the field name, and a `def` is a class-body value bound to a name.

    So the class builds, `Collide.x` is the slot descriptor, and `Collide()`
    hands back an instance whose int field holds a function. #54 is the issue.
    """

    @staticmethod
    def colliding_method():
        class Collide(Struct):
            x: int

            def x(self) -> str:
                return "method"

        return Collide

    def test_the_method_is_gone_from_the_class(self):
        Collide = self.colliding_method()

        assert type(Collide.x).__name__ == "member_descriptor"
        assert Collide(1).x == 1

    def test_but_it_became_the_default_and_the_class_is_constructible(self):
        """The half `Collide(1).x == 1` cannot see. Constructing with no
        argument is accepted, because the field now has a default.
        """

        Collide = self.colliding_method()
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
