"""Annotations that name something other than a field.

`ClassVar` and `InitVar` are not fields, and `dataclass_transform` already tells
a checker so. Treating them as fields is how checked code and running code come
to disagree about what the first positional argument means, so they are refused
until there is a way to ask for them.
"""

from dataclasses import InitVar
from typing import ClassVar, Final

import pytest

from salix import Struct


def test_a_class_var_is_refused():
    """The shape a dataclass port arrives with, which used to fail with a
    message about default ordering that never mentioned ClassVar.
    """

    with pytest.raises(TypeError, match="annotated ClassVar"):

        class Registry(Struct):
            instances: ClassVar[list] = []
            name: str


def test_a_bare_class_var_is_refused():
    with pytest.raises(TypeError, match="annotated ClassVar"):

        class Bare(Struct):
            marker: ClassVar


def test_an_init_var_is_refused():
    with pytest.raises(TypeError, match="annotated InitVar"):

        class Seeded(Struct):
            seed: InitVar[int]


def test_each_says_what_to_do_instead():
    with pytest.raises(TypeError, match="without an annotation"):
        type(Struct)("A", (Struct,), {"__annotations__": {"v": ClassVar[int]}})

    with pytest.raises(TypeError, match="set_field"):
        type(Struct)("B", (Struct,), {"__annotations__": {"v": InitVar[int]}})


def test_an_ordinary_annotation_of_the_same_shape_is_untouched():
    """`Final[int]` is a generic alias too, and is nobody's special case."""

    class Ordinary(Struct):
        a: Final[int] = 1
        b: str | None = None

    assert Ordinary.__struct_fields__ == ("a", "b")


def test_a_field_named_after_the_form_is_still_a_field():
    """The annotation is what is inspected, never the name."""

    class Named(Struct):
        ClassVar: int = 1
        InitVar: int = 2

    assert Named.__struct_fields__ == ("ClassVar", "InitVar")
    assert Named().ClassVar == 1
