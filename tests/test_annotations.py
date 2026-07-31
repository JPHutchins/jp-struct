"""Forward references, which salix reads without resolving.

A field is a name and a position; the annotation beside it is never a type as
far as salix is concerned. PEP 649 is what makes that distinction reachable,
and every test here is a case where resolving would have failed.
"""

import sys

import pytest

from salix import Struct

pytestmark = pytest.mark.skipif(
    sys.version_info < (3, 14), reason="before PEP 649 an annotation is evaluated where it is written"
)


def test_a_bare_self_reference_is_a_field():
    class Node(Struct):
        value: int
        nxt: Node = None  # noqa: F821

    assert Node.__struct_fields__ == ("value", "nxt")
    assert Node(1, Node(2)).nxt.value == 2


def test_an_annotation_that_never_resolves_is_a_field_anyway():
    class Bare(Struct):
        x: NeverDefined  # noqa: F821

    assert Bare.__struct_fields__ == ("x",)
    assert Bare(1).x == 1


def test_two_classes_may_refer_to_each_other():
    class Left(Struct):
        right: Right = None  # noqa: F821

    class Right(Struct):
        left: Left = None

    assert Left(Right()).right.left is None


def test_the_order_survives_a_mix_of_resolvable_and_not():
    class Mixed(Struct):
        first: int
        second: Unresolvable  # noqa: F821
        third: str

    assert Mixed.__struct_fields__ == ("first", "second", "third")


def test_a_subclass_may_forward_reference_too():
    class Base(Struct):
        x: int

    class Child(Base):
        y: Child = None  # noqa: F821

    assert Child.__struct_fields__ == ("x", "y")


def test_the_annotations_still_resolve_once_the_class_exists():
    """Reading them early does not leave a ForwardRef behind for everyone else."""

    class Node(Struct):
        nxt: Node = None  # noqa: F821

    assert Node.__annotations__ == {"nxt": Node}


def test_an_annotation_that_fails_for_its_own_reasons_says_so():
    """Not resolving a name is the exemption; arbitrary failure is not."""

    with pytest.raises(ZeroDivisionError):

        class Broken(Struct):
            x: 1 / 0
