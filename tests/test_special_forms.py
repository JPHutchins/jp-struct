"""Annotations that name something other than a field.

`ClassVar` and `InitVar` are not fields, and `dataclass_transform` already tells
a checker so. Treating them as fields is how checked code and running code come
to disagree about what the first positional argument means, so they are refused
until there is a way to ask for them.
"""

import sys
from dataclasses import InitVar
from typing import Annotated, ClassVar, Final

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


def test_a_bare_init_var_is_refused_like_a_bare_class_var():
    """`InitVar` unsubscripted is the class itself, not an instance of it."""

    with pytest.raises(TypeError, match="annotated InitVar"):

        class Seeded(Struct):
            seed: InitVar


def test_an_annotated_class_var_does_not_hide_the_form():
    """Annotated reaches the form two hops down __origin__, and wrapping it was
    otherwise the same position-swallowing bug wearing a wrapper.
    """

    with pytest.raises(TypeError, match="annotated ClassVar"):
        type(Struct)(
            "Wrapped", (Struct,), {"__annotations__": {"v": Annotated[ClassVar[int], "meta"]}}
        )


@pytest.mark.skipif(
    sys.version_info < (3, 11), reason="typing refuses Annotated[InitVar[...]] before 3.11"
)
def test_an_annotated_init_var_does_not_hide_the_form_either():
    annotation = Annotated[InitVar[int], "meta"]

    with pytest.raises(TypeError, match="annotated InitVar"):
        type(Struct)("Wrapped", (Struct,), {"__annotations__": {"v": annotation}})


@pytest.mark.parametrize(
    "text",
    [
        "ClassVar[int]",
        "ClassVar",
        "typing.ClassVar[int]",
        "t.ClassVar[int]",
        "InitVar[int]",
        "Annotated[ClassVar[int], 'meta']",
        "Annotated[InitVar[int], 'meta']",
        "ClassVar ",
        "ClassVar [int]",
        "Annotated[ ClassVar[int], 'meta' ]",
        "Annotated[ ClassVar ]",
        "Optional[ClassVar[int]]",
    ],
)
def test_the_source_text_form_is_refused_too(text):
    """`from __future__ import annotations` leaves the annotation as its own
    source, where the spelling is all there is to go on.

    The spacings are legal Python and stored verbatim, and an earlier boundary
    rule that enumerated openers and closers separately let every one of them
    through -- accepting a space before the form and not after it.
    """

    with pytest.raises(TypeError, match="salix does not support"):
        type(Struct)("Textual", (Struct,), {"__annotations__": {"v": text}})


@pytest.mark.parametrize(
    "text",
    [
        "int",
        "MyClassVarThing",
        "ClassVarish",
        "ClassVar_",
        "_ClassVar",
        "list[int]",
        "dict[str, int]",
        'Annotated[int, "x"]',
    ],
)
def test_a_name_that_merely_contains_the_form_is_a_field(text):
    """The boundary is an identifier character on either side, so widening what
    counts as a separator must not widen what counts as the form.
    """

    Ordinary = type(Struct)("Ordinary", (Struct,), {"__annotations__": {"v": text}})

    assert Ordinary.__struct_fields__ == ("v",)


def test_re_annotating_an_inherited_field_stays_a_no_op():
    """The guard runs after the inheritance check, so this is what it always
    was -- no new slot, no swallowed argument, and now no new refusal either.
    """

    class Base(Struct):
        x: int

    class Sub(Base):
        x: ClassVar[int]

    assert Sub.__struct_fields__ == ("x",)
    assert Sub(1).x == 1


def test_a_renamed_import_is_not_resolved_in_the_source_text_form():
    """`from typing import ClassVar as CV` gives `CV[int]`, which names nothing
    the text can match. Recorded as the known hole in the heuristic rather than
    guessed at, which is what dataclasses does against sys.modules.
    """

    Escaped = type(Struct)("Escaped", (Struct,), {"__annotations__": {"v": "CV[int]"}})

    assert Escaped.__struct_fields__ == ("v",)
