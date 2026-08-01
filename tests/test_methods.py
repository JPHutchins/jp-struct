"""Methods on a struct, in the shapes people actually write them.

A struct's body is an ordinary class body: the generated `__init__`, `__eq__`,
`__hash__` and `__repr__` do not displace anything it defines. These are the
codec-shaped methods that motivate reaching for a struct in the first place --
`to_bytes`, `to_string`, a `from_bytes` classmethod -- plus the descriptors that
sit alongside them.
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
        assert Frame(258, 7, 1).to_bytes() == HEADER.pack(258, 7, 1)

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
        assert Frame.header_size() == HEADER.size
        assert Frame(1, 1).header_size() == HEADER.size

    def test_a_property_reads_the_fields(self):
        assert Frame(0, 0).is_empty
        assert not Frame(1, 0).is_empty

    def test_a_dunder_the_body_defines_is_kept(self):
        assert len(Frame(258, 7)) == 258

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

        assert tagged.header_size() == HEADER.size
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
        is nowhere for cached_property to put the value. The same is true of
        `dataclass(slots=True)` and NamedTuple; only a dataclass that carries a
        __dict__ can do it.
        """

        class Cached(Struct):
            x: int

            @functools.cached_property
            def expensive(self) -> int:
                return self.x * 100

        with pytest.raises(TypeError, match="No '__dict__' attribute"):
            _ = Cached(2).expensive

    def test_a_field_is_the_place_to_cache_instead(self):
        """What a struct can do that a slotted dataclass cannot: the cache is a
        declared field, and __post_init__ fills it through the frozen wall.
        """

        calls = []

        class Cached(Struct):
            x: int
            expensive: int = 0

            def __post_init__(self) -> None:
                calls.append(1)
                set_field(self, "expensive", self.x * 100)

        instance = Cached(2)

        assert instance.expensive == 200
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


class TestNameCollisions:
    def test_a_method_named_after_a_field_is_dropped(self):
        """The slot descriptor wins and the method is discarded. Not a salix
        quirk: dataclass, dataclass(slots=True) and NamedTuple all do this.
        """

        class Collide(Struct):
            x: int

            def x(self) -> str:  # type: ignore[no-redef]
                return "method"

        assert Collide(1).x == 1

    def test_a_property_named_after_a_field_is_dropped_too(self):
        class Collide(Struct):
            y: int

            @property
            def y(self) -> str:  # type: ignore[no-redef]
                return "property"

        assert Collide(1).y == 1
