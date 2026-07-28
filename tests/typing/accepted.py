"""What a type checker must accept and infer. Never executed -- the checkers
are the assertion, and tests/typing is not collected by pytest.
"""

from typing import Literal

from typing_extensions import assert_type

from jpstruct import Struct


class Point(Struct):
    x: int
    y: str


class Defaulted(Struct):
    a: int
    b: str = "b"


class Mutable(Struct, frozen=False):
    value: int


class Explicit(Struct, frozen=True):
    value: int


def the_constructor_is_synthesised_from_the_annotations() -> None:
    assert_type(Point(1, "two").x, int)
    assert_type(Point(1, "two").y, str)
    assert_type(Point(x=1, y="two").x, int)


def a_default_makes_its_argument_optional() -> None:
    assert_type(Defaulted(1).b, str)
    assert_type(Defaulted(1, "other").b, str)


def a_mutable_struct_accepts_a_write() -> None:
    instance = Mutable(1)
    instance.value = 2


def inheritance_extends_the_signature() -> None:
    class Point3D(Point):
        z: float

    assert_type(Point3D(1, "two", 3.0).z, float)


def introspection_is_typed_on_both_the_class_and_the_instance() -> None:
    assert_type(Point.__struct_fields__, tuple[str, ...])
    assert_type(Point(1, "two").__struct_fields__, tuple[str, ...])
    # Narrower than the stub declares: the transform synthesises the literal
    # names, which is what makes a positional pattern check.
    assert_type(Point.__match_args__, tuple[Literal["x"], Literal["y"]])


def a_struct_is_a_struct() -> None:
    def take(struct: Struct) -> Struct:
        return struct

    take(Point(1, "two"))
    take(Explicit(1))
