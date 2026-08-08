"""Every buildable combination of two and three struct bases, and what has to
be true of all of them.

The tests beside this one in test_multiple_bases.py pin shapes: this base in
front of that one answers so. Nine of those shapes arrived one review round at
a time while #9 was being fixed, each the same mechanism wearing a different
hat, because a handful of examples samples the space rather than covering it.

So these say what must hold of *any* combination, and sweep the whole space
looking for somewhere it does not. A shape nobody thought of is covered the day
it becomes buildable.

The rule the differential tests state is salix's own: options are inherited
from the **first** struct base, whose branch of the MRO is searched first, and
only `frozen` and `weakref` are facts about every base rather than preferences
of that one. So a class with several struct bases must behave like the same
class built on its first struct base alone.

Where that fails today it fails for one reason, filed as #76: a struct base
binds a dunder only where its own creation transitioned an option, so a later
base can bind a name the first one left to the MRO, and then the record and the
behaviour are two different answers. Those combinations are split off by asking
`vars()` which bases bind the name -- a structural question, not a list of
known-bad examples -- and the tests over them are strict xfails, so #76 turns
them red the day it lands.
"""

import itertools
import operator
import weakref
from typing import Any, NamedTuple

import pytest

from salix import Struct


class Fieldless(Struct):
    pass


class MutableFieldless(Struct, frozen=False):
    pass


class Weak(Struct, weakref=True):
    pass


class Fields(Struct):
    a: int
    b: int


class OneField(Struct):
    a: int


class MutableFields(Struct, frozen=False):
    m: int


class Unrepresented(Struct, repr=False):
    pass


class ByIdentity(Struct, eq=False):
    pass


class Ordered(Struct, order=True):
    o: int


class BodyEq(Struct):
    def __eq__(self, other: object) -> bool:
        return True

    def __hash__(self) -> int:
        return 7


class Derived(Fieldless):
    pass


SHAPES = (
    Fieldless,
    MutableFieldless,
    Weak,
    Fields,
    OneField,
    MutableFields,
    Unrepresented,
    ByIdentity,
    Ordered,
    BodyEq,
    Derived,
)

ARRANGEMENTS = tuple(
    arrangement
    for width in (2, 3)
    for arrangement in itertools.permutations(SHAPES, width)
)

_names = itertools.count()


class Refused(NamedTuple):
    """Salix declined the class."""

    message: str


class Impossible(NamedTuple):
    """Python declined it, which is not this suite's business: an MRO or
    lay-out conflict says these bases could never have been combined, whoever
    was building them.
    """

    message: str


Outcome = type | Refused | Impossible
Combination = tuple[tuple[type, ...], type]

PYTHONS_OWN = ("consistent method resolution", "lay-out conflict")
NEW_FIELD = "z"


def build(bases: tuple[type, ...], field: str = NEW_FIELD, **keywords: bool) -> Outcome:
    try:
        return type(Struct)(
            f"Combination{next(_names)}",
            bases,
            {"__annotations__": {field: int}},
            **keywords,
        )
    except TypeError as failure:
        if any(reason in str(failure) for reason in PYTHONS_OWN):
            return Impossible(str(failure))
        return Refused(str(failure))


def is_a_class(outcome: Outcome) -> bool:
    return isinstance(outcome, type)


def instance(cls: type, start: int = 0) -> Any:
    return cls(*range(start, start + len(cls.__struct_fields__)))


class Behaviour(NamedTuple):
    """What a class does, as opposed to what it recorded. Only the second is
    salix's to choose, and the whole of #76 is the two disagreeing.
    """

    repr: bool
    equality: str
    order: bool
    frozen: bool
    weakref: bool


def observe(cls: type) -> Behaviour:
    """`equality` is three-valued because equality has three possible sources.
    A body `__eq__` that answers True to everything looks exactly like the
    structural one until it is shown two instances that differ, which is the
    shape #71's round-6 review found and four rounds before it did not.
    """

    one, twin, other = instance(cls), instance(cls), instance(cls, 100)

    try:
        operator.lt(one, twin)
        orders = True
    except TypeError:
        orders = False

    try:
        weakref.ref(one)
        weak = True
    except TypeError:
        weak = False

    try:
        setattr(instance(cls), cls.__struct_fields__[0], 99)
        frozen = False
    except TypeError:
        frozen = True

    return Behaviour(
        repr=repr(one).startswith(f"{cls.__name__}("),
        equality=(
            "everything" if one == other else "value" if one == twin else "identity"
        ),
        order=orders,
        frozen=frozen,
        weakref=weak,
    )


def answered_by_a_later_base(bases: tuple[type, ...], name: str) -> bool:
    """Whether the MRO finds this dunder somewhere other than where the options
    were read from.

    A struct base carries a dunder in its own dict exactly where its creation
    transitioned an option away from what it inherited; everything else it
    leaves to the mixin at the end of the MRO. So a later base binding a name
    the first one did not is the whole of #76's mechanism, asked of the classes
    themselves rather than assumed from a list.
    """

    return name not in vars(bases[0]) and any(name in vars(base) for base in bases[1:])


def named(bases: tuple[type, ...]) -> str:
    return " + ".join(base.__name__ for base in bases)


ALL = tuple(
    (bases, outcome) for bases in ARRANGEMENTS if is_a_class(outcome := build(bases))
)


def test_the_sweep_reaches_every_shape_at_both_widths():
    """A guard on the sweep itself. A change that made some shape unbuildable
    in every arrangement would stop testing it without a word, and every test
    below would stay green over the space that was left.
    """

    assert {base for bases, _ in ALL for base in bases} == set(SHAPES)
    assert {len(bases) for bases, _ in ALL} == {2, 3}


def test_the_fields_are_the_layout_bases_followed_by_the_new_one():
    """The offsets salix hands out are the base's slots plus the new ones, so
    the base it reads them from has to be the one CPython placed them after.
    """

    violations = [
        f"{named(bases)}: {cls.__struct_fields__} after {cls.__base__.__name__}"
        f"{cls.__base__.__struct_fields__}"
        for bases, cls in ALL
        if cls.__struct_fields__ != (*cls.__base__.__struct_fields__, NEW_FIELD)
    ]

    assert violations == []


def test_every_field_reads_back_what_it_was_given():
    violations = [
        named(bases)
        for bases, cls in ALL
        if [getattr(instance(cls), name) for name in cls.__struct_fields__]
        != list(range(len(cls.__struct_fields__)))
    ]

    assert violations == []


def test_match_args_is_the_fields():
    violations = [
        f"{named(bases)}: {cls.__match_args__} against {cls.__struct_fields__}"
        for bases, cls in ALL
        if getattr(cls, "__match_args__", None) != cls.__struct_fields__
    ]

    assert violations == []


def test_a_write_and_a_delete_agree_about_whether_the_class_is_frozen():
    """Two routes to the same promise. A class that refuses one and allows the
    other is frozen against an assignment and not against a `del`.
    """

    def writes(cls: type) -> bool:
        try:
            setattr(instance(cls), cls.__struct_fields__[0], 99)
            return True
        except TypeError:
            return False

    def deletes(cls: type) -> bool:
        try:
            delattr(instance(cls), cls.__struct_fields__[0])
            return True
        except (TypeError, AttributeError):
            return False

    violations = [named(bases) for bases, cls in ALL if writes(cls) != deletes(cls)]

    assert violations == []


def test_a_value_that_compares_by_value_and_can_still_move_is_unhashable():
    """Python's rule, and the reason `frozen` and `eq` between them settle the
    hash: a key whose hash moves is not a key.
    """

    violations = [
        named(bases)
        for bases, cls in ALL
        if observe(cls).equality == "value"
        and not observe(cls).frozen
        and cls.__hash__ is not None
    ]

    assert violations == []


ORDERS_ALONE = {
    shape: observe(alone).order
    for shape in SHAPES
    if is_a_class(alone := build((shape,)))
}


def contested(bases: tuple[type, ...], name: str) -> bool:
    """Whether #76's mechanism can reach this name for these bases.

    Split per name rather than once for all of them: a combination where a
    later base binds `__repr__` says nothing about equality, and folding them
    together would put hundreds of sound cases in a bucket that is expected to
    fail, where a regression in one of them would change nothing.

    `__lt__` needs the extra clause because ordering is answered by the
    recorded option at comparison time, not by the binding -- so a later base's
    `__lt__` only shadows an answer there was one of. That reads off the first
    base alone, which is the side of the comparison being trusted, so it is a
    property of the reference rather than of the class under test.
    """

    if not answered_by_a_later_base(bases, name):
        return False

    return ORDERS_ALONE.get(bases[0], False) if name == "__lt__" else True


def halves(name: str) -> tuple[tuple[Combination, ...], tuple[Combination, ...]]:
    return (
        tuple((bases, cls) for bases, cls in ALL if not contested(bases, name)),
        tuple((bases, cls) for bases, cls in ALL if contested(bases, name)),
    )


SOUND_REPR, CONTESTED_REPR = halves("__repr__")
SOUND_EQ, CONTESTED_EQ = halves("__eq__")
SOUND_ORDER, CONTESTED_ORDER = halves("__lt__")


@pytest.mark.parametrize(
    ("sound", "broken"),
    [
        (SOUND_REPR, CONTESTED_REPR),
        (SOUND_EQ, CONTESTED_EQ),
        (SOUND_ORDER, CONTESTED_ORDER),
    ],
    ids=["__repr__", "__eq__", "__lt__"],
)
def test_both_halves_of_the_split_are_reached(sound, broken):
    """An empty sound half would make the test over it vacuous. An empty broken
    half cannot hide: its test is a strict xfail, so it turns red instead.
    """

    assert sound != ()
    assert broken != ()


def behaviour_differs_from_the_first_base_alone(
    combinations: tuple[Combination, ...],
    field: str,
) -> list[str]:
    violations = []

    for bases, multi in combinations:
        alone = build((bases[0],))

        if not is_a_class(alone):
            continue

        together, apart = getattr(observe(multi), field), getattr(observe(alone), field)

        if together != apart:
            violations.append(f"{named(bases)}: {together} against {apart} alone")

    return violations


def hash_disagreements(combinations: tuple[Combination, ...]) -> list[str]:
    return [
        named(bases)
        for bases, cls in combinations
        if cls.__hash__ is not None
        and instance(cls) == instance(cls, 100)
        and hash(instance(cls)) != hash(instance(cls, 100))
    ]


def test_the_repr_is_the_first_struct_bases():
    assert behaviour_differs_from_the_first_base_alone(SOUND_REPR, "repr") == []


def test_equality_is_the_first_struct_bases():
    assert behaviour_differs_from_the_first_base_alone(SOUND_EQ, "equality") == []


def test_ordering_is_the_first_struct_bases():
    assert behaviour_differs_from_the_first_base_alone(SOUND_ORDER, "order") == []


def test_equal_instances_hash_equal():
    """The dict-key invariant, and the one worth the whole sweep: two objects
    that compare equal and hash differently cannot find each other in a dict.
    """

    assert hash_disagreements(SOUND_EQ) == []


@pytest.mark.xfail(strict=True, reason="#76: the record and the MRO disagree")
def test_the_repr_is_the_first_struct_bases_when_a_later_one_binds_it():
    assert behaviour_differs_from_the_first_base_alone(CONTESTED_REPR, "repr") == []


@pytest.mark.xfail(strict=True, reason="#76: the record and the MRO disagree")
def test_equality_is_the_first_struct_bases_when_a_later_one_binds_it():
    assert behaviour_differs_from_the_first_base_alone(CONTESTED_EQ, "equality") == []


@pytest.mark.xfail(strict=True, reason="#76: the record and the MRO disagree")
def test_ordering_is_the_first_struct_bases_when_a_later_one_binds_it():
    assert behaviour_differs_from_the_first_base_alone(CONTESTED_ORDER, "order") == []


@pytest.mark.xfail(strict=True, reason="#76: a later base's __eq__ against salix's hash")
def test_equal_instances_hash_equal_when_a_later_base_answers_equality():
    assert hash_disagreements(CONTESTED_EQ) == []


@pytest.mark.xfail(strict=True, reason="#77: the frozen pin is armed from one base and directed by another")
def test_the_frozen_pin_does_not_depend_on_the_order_of_the_bases():
    """Whether a class may be frozen is a question about which of its bases
    made a promise, and no ordering of the same bases changes the answer.
    """

    violations = []

    for bases in ARRANGEMENTS:
        for wanted in (True, False):
            forwards = build(bases, frozen=wanted)
            backwards = build(tuple(reversed(bases)), frozen=wanted)

            if isinstance(forwards, Impossible) or isinstance(backwards, Impossible):
                continue

            if isinstance(forwards, Refused) != isinstance(backwards, Refused):
                violations.append(f"{named(bases)} frozen={wanted}")

    assert violations == []


@pytest.mark.xfail(strict=True, reason="#78: weakref is recorded from one base and slotted from another")
def test_the_weakref_option_and_the_slot_agree():
    """CPython cannot take an inherited `__weakref__` away, so `weakref=False`
    over a base that has one is a request salix cannot honour -- and accepting
    it silently leaves the class recording one answer and giving another.
    """

    violations = []

    for bases, cls in ALL:
        without = build((cls,), field="fresh", weakref=False)

        if not is_a_class(without):
            continue

        if observe(without).weakref:
            violations.append(named(bases))

    assert violations == []
