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


OUTCOMES = tuple((bases, build(bases)) for bases in ARRANGEMENTS)
ALL = tuple((bases, outcome) for bases, outcome in OUTCOMES if is_a_class(outcome))
ALONE = {
    shape: built for shape in SHAPES if is_a_class(built := build((shape,)))
}


def test_the_sweep_reaches_every_shape_at_both_widths():
    assert {base for bases, _ in ALL for base in bases} == set(SHAPES)
    assert {len(bases) for bases, _ in ALL} == {2, 3}
    assert set(ALONE) == set(SHAPES)


def test_salix_refuses_no_arrangement_of_these_shapes():
    """The guard that keeps the sweep from shrinking in silence.

    Every combination here is buildable or Python's own to refuse, so a change
    that makes salix decline one takes it out of `ALL` -- and every invariant
    below would stay green over the smaller space, which is the sampling this
    file exists to stop. Asserting the shapes each appear *somewhere* does not
    catch that: they would still each appear somewhere.

    It guards the `PYTHONS_OWN` substring test as well. That is a heuristic
    over CPython's wording, and a release that rephrases an MRO or lay-out
    conflict would start classifying its own refusals as salix's -- which shows
    up here as a refusal rather than as a combination quietly leaving the
    sweep.
    """

    refusals = [
        f"{named(bases)}: {outcome.message}"
        for bases, outcome in OUTCOMES
        if isinstance(outcome, Refused)
    ]

    assert refusals == []


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

    violations = []

    for bases, cls in ALL:
        behaviour = observe(cls)

        if behaviour.equality == "value" and not behaviour.frozen and cls.__hash__ is not None:
            violations.append(named(bases))

    assert violations == []


def test_a_frozen_promise_is_kept_by_every_arrangement_that_inherits_one():
    """What the class records against what it does, for the one option the
    differential deliberately excludes.

    `frozen` is not the first base's preference but a promise every fielded
    base made separately, so the rule is the strongest of them: frozen if the
    first base alone is, or if any base with fields is. Nothing else here
    compares that to behaviour -- a write and a delete are only checked against
    each other, and the unhashable rule fires only for value equality -- so a
    regression in the forced rebind could leave a class recording the promise
    and taking every write, with all of it green.
    """

    def promised(bases: tuple[type, ...]) -> bool:
        return any(base.__struct_fields__ and observe(base).frozen for base in bases)

    violations = [
        f"{named(bases)}: frozen={observe(cls).frozen} against a promise"
        for bases, cls in ALL
        if observe(cls).frozen != (observe(ALONE[bases[0]]).frozen or promised(bases))
    ]

    assert violations == []


ORDERS_ALONE = {shape: observe(built).order for shape, built in ALONE.items()}
EQUALITY_ALONE = {shape: observe(built).equality for shape, built in ALONE.items()}


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


def answers_equal_to_everything(bases: tuple[type, ...]) -> bool:
    """Whether the later base the lookup takes `__eq__` from is one that calls
    two differing instances equal.

    The hash disagreement needs that much: a later base binding `__eq__` is
    what puts the record and the behaviour at odds, but a binder that compares
    by *identity* still calls two differing instances unequal, so no pair of
    equal-and-differently-hashed objects exists to find. Only a permissive
    binder produces one.

    Without the clause the bucket is 224 wide to hold 112 real violations, and
    the other half sits in a test that is expected to fail, where a regression
    would be invisible.
    """

    if "__eq__" in vars(bases[0]):
        return False

    binder = next((base for base in bases[1:] if "__eq__" in vars(base)), None)

    return binder is not None and EQUALITY_ALONE.get(binder) == "everything"


SOUND_HASH = tuple(
    (bases, cls)
    for bases, cls in ALL
    if not (answers_equal_to_everything(bases) and cls.__hash__ is not None)
)
CONTESTED_HASH = tuple(
    (bases, cls)
    for bases, cls in ALL
    if answers_equal_to_everything(bases) and cls.__hash__ is not None
)


@pytest.mark.parametrize(
    ("sound", "broken"),
    [
        (SOUND_REPR, CONTESTED_REPR),
        (SOUND_EQ, CONTESTED_EQ),
        (SOUND_ORDER, CONTESTED_ORDER),
        (SOUND_HASH, CONTESTED_HASH),
    ],
    ids=["__repr__", "__eq__", "__lt__", "__hash__"],
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

    assert hash_disagreements(SOUND_HASH) == []


@pytest.mark.parametrize(
    ("broken", "violations"),
    [
        (CONTESTED_REPR, lambda rows: behaviour_differs_from_the_first_base_alone(rows, "repr")),
        (CONTESTED_EQ, lambda rows: behaviour_differs_from_the_first_base_alone(rows, "equality")),
        (CONTESTED_ORDER, lambda rows: behaviour_differs_from_the_first_base_alone(rows, "order")),
        (CONTESTED_HASH, hash_disagreements),
    ],
    ids=["__repr__", "__eq__", "__lt__", "__hash__"],
)
def test_every_contested_combination_really_is_broken(broken, violations):
    """The half of the split the strict xfails cannot police, and the reason
    each bucket is narrowed until it is exact.

    An xfail over a bucket where everything already fails emits one bit: it
    flips only when the bucket becomes *entirely* clean. A partial fix, or a
    new violation among combinations that were sound, leaves it just as failed
    and just as green. Asserting the bucket is wholly broken is the other
    bound, and between them the count cannot move without a test noticing.

    Delete this with the four xfails below when #76 lands; they describe the
    same defect from opposite sides.
    """

    assert len(violations(broken)) == len(broken)


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
    assert hash_disagreements(CONTESTED_HASH) == []


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

    `Weak` alone is in here beside the arrangements because the single base is
    the smallest form of it: `class C(Weak, weakref=False)` builds today and
    the reference still works. A fix aimed only at the multi-base reading would
    turn every other case here red and leave that one passing unexercised.
    """

    violations = []

    for bases, cls in (*ALL, *(((shape,), built) for shape, built in ALONE.items())):
        without = build((cls,), field="fresh", weakref=False)

        if not is_a_class(without):
            continue

        if observe(without).weakref:
            violations.append(named(bases))

    assert violations == []


def test_a_single_base_asking_to_drop_an_inherited_weakref_slot_is_the_same_bug():
    """#78's smallest form, stated on its own so that it is covered whether or
    not the sweep above still reaches it. The slot is inherited and cannot be
    removed, so the request is one salix should refuse rather than accept and
    drop; today it is accepted and dropped.
    """

    without = build((Weak,), field="fresh", weakref=False)

    assert is_a_class(without)
    assert observe(without).weakref is True
