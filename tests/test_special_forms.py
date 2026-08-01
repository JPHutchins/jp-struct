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
    ("text", "form"),
    [
        ("ClassVar[int]", "ClassVar"),
        ("ClassVar", "ClassVar"),
        ("typing.ClassVar[int]", "ClassVar"),
        ("t.ClassVar[int]", "ClassVar"),
        ("InitVar[int]", "InitVar"),
        ("Annotated[ClassVar[int], 'meta']", "ClassVar"),
        ("Annotated[InitVar[int], 'meta']", "InitVar"),
        ("ClassVar ", "ClassVar"),
        ("ClassVar [int]", "ClassVar"),
        ("Annotated[ ClassVar[int], 'meta' ]", "ClassVar"),
        ("Annotated[ ClassVar ]", "ClassVar"),
        ("Annotated[InitVar]", "InitVar"),
        ("Optional[ClassVar[int]]", "ClassVar"),
        ("x\x00ClassVar[int]", "ClassVar"),
        ("'ClassVar[int]'", "ClassVar"),
        ("ClassVar\u20ac", "ClassVar"),
        ("\u20acClassVar", "ClassVar"),
        pytest.param(chr(0xD800) + "ClassVar", "ClassVar", id="lone-surrogate"),
    ],
)
def test_the_source_text_form_is_refused_too(text, form):
    """`from __future__ import annotations` leaves the annotation as its own
    source, where the spelling is all there is to go on.

    The spacings are legal Python and stored verbatim, and an earlier boundary
    rule that enumerated openers and closers separately let every one of them
    through -- accepting a space before the form and not after it.

    A str may hold a NUL, and scanning with `strstr` stopped at it, leaving the
    rest unread and the guard silently off. The quoted one is a nested forward
    reference and has to be refused for the same reason the bare spelling is.

    The euro sign is not an identifier character and a lone surrogate cannot be
    encoded to UTF-8; both used to reach the guard as bytes, where the first
    looked like part of a name and the second failed the encode and was waved
    through. The surrogate is built with chr() rather than written as a literal,
    because a source file holding one cannot be compiled.

    The expected form is asserted, not just the refusal: matching only "salix
    does not support" would pass with the two `names_form` calls swapped.
    """

    with pytest.raises(TypeError, match=f"annotated {form}"):
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
        "théClassVar",
        "ClassVaré",
        "ÄClassVar",
        "ClassVar\u0301",
        "ClassVar\u00b7",
    ],
)
def test_a_name_that_merely_contains_the_form_is_a_field(text):
    """The boundary is an identifier character on either side, so widening what
    counts as a separator must not widen what counts as the form.

    The last five are legal identifiers under PEP 3131, and the last two are why
    the boundary asks Python rather than a table: a combining acute and a middle
    dot both continue an identifier without being letters, which is the kind of
    character a hand-written rule gets wrong in the silent direction.
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


def test_a_plain_annotated_is_still_a_field():
    """The positive control for the object path's Annotated handling: every
    other Annotated test here is a refusal, so a walk that started reporting a
    form for everything would pass all of them.
    """

    class Tagged(Struct):
        v: Annotated[int, "meta"]
        w: Annotated[list, "meta", "more"]

    assert Tagged.__struct_fields__ == ("v", "w")
    assert Tagged(1, []).v == 1


def test_a_real_future_annotations_module_takes_the_text_path(tmp_path):
    """Every other text-path case here hands salix a string it built itself.
    This one makes the compiler produce the annotations, which is the only way
    to know the path is reachable the way a user reaches it.
    """

    module = tmp_path / "future_struct.py"
    module.write_text(
        "from __future__ import annotations\n"
        "from typing import ClassVar\n"
        "from salix import Struct\n"
        "\n"
        "class Registry(Struct):\n"
        "    instances: ClassVar[list] = []\n"
        "    name: str\n"
    )

    with pytest.raises(TypeError, match="annotated ClassVar"):
        exec(compile(module.read_text(), str(module), "exec"), {"__name__": "future_struct"})


def test_a_real_future_annotations_module_still_builds_ordinary_fields():
    """The other half: the text path must not refuse a class that names no form.
    Without this, refusing everything would pass the test above.
    """

    source = (
        "from __future__ import annotations\n"
        "from typing import Annotated\n"
        "from salix import Struct\n"
        "\n"
        "class Frame(Struct):\n"
        "    length: int\n"
        "    tag: Annotated[str, 'meta'] = 'x'\n"
    )
    namespace: dict[str, object] = {"__name__": "future_ordinary"}

    exec(compile(source, "<future_ordinary>", "exec"), namespace)
    Frame = namespace["Frame"]

    assert Frame.__struct_fields__ == ("length", "tag")  # type: ignore[attr-defined]
    assert Frame(1).tag == "x"  # type: ignore[operator]


@pytest.mark.skipif(
    sys.version_info < (3, 11),
    reason="typing refuses a bare special form as an Annotated argument before 3.11",
)
@pytest.mark.parametrize("form", [ClassVar, InitVar])
def test_a_bare_form_inside_annotated_is_refused_on_the_object_path(form):
    """`Annotated[ClassVar, 'meta']` -- the form unsubscripted, one hop down.
    The subscripted shape is covered above; this is the one where `__origin__`
    reaches the form object itself rather than a `_GenericAlias` of it.
    """

    with pytest.raises(TypeError, match="salix does not support"):
        type(Struct)("Wrapped", (Struct,), {"__annotations__": {"v": Annotated[form, "meta"]}})


@pytest.mark.parametrize(
    "trailing",
    ["́", "·", "‌", "‍", "々", "s", "_", "1", " ", "[", "€"],
    ids=["acute", "middot", "zwnj", "zwj", "iteration-mark", "letter", "underscore",
         "digit", "space", "bracket", "euro"],
)
def test_the_boundary_agrees_with_python_about_identifiers(trailing):
    """Not a table of expected answers: the assertion is that salix reaches the
    same verdict `str.isidentifier` does, on whatever interpreter is running.

    That matters because the answer moves. CPython made ZWNJ and ZWJ continue an
    identifier in 3.13, so `ClassVar‌` is one name there and two tokens on
    3.12 -- and salix refuses it on 3.12 and accepts it on 3.13 for exactly the
    right reason. A hardcoded expectation here would pin one of those and call
    the other a bug.
    """

    text = "ClassVar" + trailing
    part_of_a_longer_name = ("a" + trailing).isidentifier()

    try:
        type(Struct)("Boundary", (Struct,), {"__annotations__": {"v": text}})
        refused = False
    except TypeError:
        refused = True

    assert refused is not part_of_a_longer_name
