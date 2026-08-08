"""A base that is not a struct, ahead of one that is.

salix decides whether to answer for `__hash__` by asking whether the `__eq__`
the class resolves is one it bound. That question used to be put to the struct
base alone, and a non-struct base earlier in the MRO can supply the `__eq__`
the class actually gets -- so two instances compared equal by a class body and
hashed by salix's fields, which is the same contract break as a dict key whose
hash moves, reached through a base salix never looked at.

The question is now put to every base, in the order C3 searches them.
"""

import pytest

from salix import Struct


class Base(Struct):
    x: object


class Equal:  # noqa: PLW1641 -- the absent __hash__ is the assertion
    """Equality without a hash, so Python's own rule makes it unhashable and
    salix has to leave that alone rather than bind a hash beside it.
    """

    def __eq__(self, other: object) -> bool:
        return True


class Paired:
    def __eq__(self, other: object) -> bool:
        return True

    def __hash__(self) -> int:
        return 7


class Inert:
    pass


class Raising:
    def __get__(self, instance: object, owner: type | None = None) -> object:
        raise RuntimeError("no equality here")


class Hostile:  # noqa: PLW1641 -- the __eq__ that raises is the shape
    __eq__ = Raising()  # type: ignore[assignment]


def test_a_non_struct_eq_ahead_of_the_struct_base_takes_the_hash_with_it():
    """The reported shape. Before, `len({B(1), B(2)}) == 2` for two instances
    that compared equal -- so neither could find the other in a dict.
    """

    class B(Equal, Base):
        y: object = 0

    assert B.__eq__ is Equal.__eq__
    assert B(1, 0) == B(2, 0)

    with pytest.raises(TypeError, match="unhashable type: 'B'"):
        hash(B(1, 0))


def test_a_struct_base_does_not_change_what_the_class_body_asked_for():
    """Not salix's rule but Python's, which is the argument for following it: a
    body that defines __eq__ and not __hash__ is unhashable.

    Stated as a comparison rather than as a fact about `Plain` alone -- on its
    own that is CPython semantics with no salix in it, and would pass with the
    package removed. What is worth asserting is that adding a struct base does
    not change the answer.
    """

    class Plain(Equal):
        pass

    class WithAStructBase(Equal, Base):
        y: object = 0

    assert Plain.__hash__ is None
    assert WithAStructBase.__hash__ is Plain.__hash__


def test_a_base_that_pairs_them_keeps_its_pair():
    """Equality and the hash beside it both come from the base, so they agree
    -- which is the whole requirement. salix binds neither.
    """

    class B(Paired, Base):
        y: object = 0

    assert B(1, 0) == B(2, 0)
    assert (B(1, 0) != B(2, 0)) is False
    assert hash(B(1, 0)) == hash(B(2, 0)) == 7
    assert len({B(1, 0), B(2, 0)}) == 1


def test_a_base_with_no_equality_of_its_own_decides_nothing():
    """object's __eq__ is the absence of one, so the search passes over it and
    the struct base behind still answers.
    """

    class B(Inert, Base):
        y: object = 0

    assert B(1, 0) == B(1, 0)
    assert B(1, 0) != B(2, 0)
    assert hash(B(1, 0)) == hash(B(1, 0))


def test_and_it_does_not_stop_the_search_short():
    """Inert first, then the base that does answer: the loop has to keep
    looking rather than take the first base it is handed.
    """

    class B(Inert, Equal, Base):
        y: object = 0

    assert B(1, 0) == B(2, 0)

    with pytest.raises(TypeError, match="unhashable type"):
        hash(B(1, 0))


def test_the_struct_base_behind_it_still_answers_when_it_comes_first():
    """The order that was never broken, asserted so the fix cannot be read as
    "a non-struct base always wins". _StructMixin is an ancestor of the struct
    base and so precedes an unrelated co-base in the MRO.
    """

    class B(Base, Equal):
        y: object = 0

    assert B.__eq__ is not Equal.__eq__
    assert B(1, 0) != B(2, 0)
    assert hash(B(1, 0)) == hash(B(1, 0))


def test_a_subclass_inherits_the_deferral():
    """The class records that its equality came from a body, so its own
    children do not bind a hash beside it either.
    """

    class B(Equal, Base):
        y: object = 0

    class Child(B):
        z: object = 0

    assert Child(1, 0, 0) == Child(2, 0, 0)
    assert Child.__hash__ is None


def test_turning_equality_off_takes_the_class_back():
    """A class that changes the eq option has salix's binding written into its
    own namespace, which the lookup reaches before any base -- so the co-base's
    equality is overridden rather than deferred to, and identity is consistent
    with the identity hash bound beside it.
    """

    class B(Equal, Base, eq=False):
        y: object = 0

    assert B(1, 0) != B(1, 0)
    assert len({B(1, 0), B(1, 0)}) == 2


def test_asking_for_the_equality_it_already_has_does_not_take_it_back():
    """`eq=True` over a base already recording eq=True is not a transition, so
    nothing is rebound and the co-base's __eq__ still answers. That a keyword
    matching the inherited value does nothing is #76 and is not this fix's to
    change; what this fix changes is the hash beside it, which no longer
    contradicts the equality that won.
    """

    class B(Equal, Base, eq=True):
        y: object = 0

    assert B(1, 0) == B(2, 0)
    assert B.__hash__ is None


def test_a_struct_with_no_co_base_is_untouched():
    """The regression this fix caused once and the tests caught: _StructMixin
    is the only base Struct itself has, and its metaclass is plain `type`, so
    it is not a struct class and reaches the same lookup a co-base does. Its
    __eq__ is salix's, not a body's -- counted wrongly, every struct in the
    world inherits "my equality came from a class body" from the root.
    """

    class Mutable(Struct, frozen=False):
        x: object

    class Frozen(Struct):
        x: object

    assert Mutable.__hash__ is None
    assert Frozen.__hash__ is not None
    assert hash(Frozen(1)) == hash(Frozen(1))


def test_equality_and_inequality_agree_when_a_co_base_answers():
    """The half the first version of this fix left behind: it deferred the
    hash to the co-base and left `__ne__` resolving to the mixin's structural
    one, so `a == b` and `a != b` were both true -- #58's shape reached through
    an inherited __eq__ rather than a body-written one.
    """

    class B(Equal, Base):
        y: object = 0

    one, other = B(1, 0), B(2, 0)

    assert one == other
    assert (one != other) is False


def test_a_co_base_supplying_only_a_hash_still_gets_salixs():
    """Deliberate, though not because the co-base's hash would be *wrong*: a
    constant hash is a legal partner for structural equality, since the rule is
    only that equal instances hash alike.

    The reason is that the co-base supplies no `__eq__`, so the struct base's
    structural equality answers and salix owns the pair -- and its half is the
    one that tells values apart.

    Asserted as structural rather than merely "not the co-base's", which an
    identity hash would satisfy too.
    """

    class Hashed:
        __hash__ = 5

    class H(Hashed, Base):
        y: object = 0

    assert H(1, 0) == H(1, 0)
    assert hash(H(1, 0)) == hash(H(1, 0))
    assert hash(H(1, 0)) != hash(H(2, 0))
    assert hash(H(1, 0)) == hash((1, 0))


def test_a_base_whose_equality_cannot_be_looked_up_fails_the_class():
    """The cost of asking every base, stated rather than discovered.

    Deciding the hash means reading each non-struct base's `__eq__`, and that
    runs whatever the base put under the name. A descriptor that raises used to
    go unnoticed, because the struct base was the only one consulted; now it
    stops the class being defined.

    Propagating beats demoting because demoting is a lie about what was
    learned: a probe that could not look has not found out that the base
    supplies no equality. It is not that the class is unusable otherwise --
    demote and it builds with salix's structural equality, ignoring Hostile's
    entirely, which is worse for being quiet. It is a class that built before,
    so it is pinned either way.
    """

    with pytest.raises(RuntimeError, match="no equality here"):

        class B(Hostile, Base):
            y: object = 0


def test_only_the_equality_is_read_off_a_base_that_did_not_supply_one():
    """Every attribute read off a co-base is a class that can fail to define,
    so the walk reads what the decision turns on and nothing else.

    A base supplying no `__eq__` has already answered -- it adds nothing to the
    lookup -- so its `__hash__` and `__ne__` are never asked for, and a
    descriptor raising under either of those names cannot stop a class that
    salix was going to answer for anyway.
    """

    class RaisingHash:
        __hash__ = Raising()  # type: ignore[assignment]

    class RaisingNotEqual:
        __ne__ = Raising()  # type: ignore[assignment]

    class H(RaisingHash, Base):
        y: object = 0

    class N(RaisingNotEqual, Base):
        y: object = 0

    assert hash(H(1, 0)) == hash((1, 0))
    assert hash(N(1, 0)) == hash((1, 0))


def test_a_class_that_throws_that_equality_away_is_not_asked_for_it():
    """The bases are read only where the answer can be used.

    `eq=False` rebinds all six comparison names into this class's own
    namespace, so whatever the co-bases resolve is discarded -- and the
    justification above does not carry here either, because `==` is object's
    and never reaches the descriptor. Asking anyway refused a class with
    nothing wrong with it.
    """

    class B(Hostile, Base, eq=False):
        y: object = 0

    assert B(1, 0) != B(1, 0)


@pytest.mark.xfail(strict=True, reason="#76: a later struct base's body __eq__ is not seen")
def test_a_later_struct_base_answering_equality_is_not_covered_by_this():
    """The limit of the walk, pinned so the claim above stays honest.

    The search ends at the first struct base, which is exact while there is one
    of them. With two, C3 puts a later struct base's own `__eq__` ahead of the
    mixin -- the mixin is a shared ancestor, so it is deferred to the tail --
    and the class resolves an equality this walk never saw. Structural hash
    beside a body equality, which is the same break one level over.
    """

    class WithBodyEq(Base):
        def __eq__(self, other: object) -> bool:
            return True

        def __hash__(self) -> int:
            return 7

    class Plain(Base):
        pass

    class B(Plain, WithBodyEq):
        pass

    assert B(1) == B(2)
    assert hash(B(1)) == hash(B(2))


def test_a_co_base_that_paired_them_itself_keeps_its_own_inequality():
    """The other half of the __ne__ fix, and the half nothing pinned.

    A co-base supplying `__eq__` alone gets object's derived `__ne__` bound
    over the mixin's structural one. A co-base that wrote both has already
    paired them, and salix binds nothing -- forcing the flag true keeps every
    other test in this file green, so without this the guard could go.
    """

    class Paired_:  # noqa: PLW1641 -- the absent __hash__ is not what is under test
        def __eq__(self, other: object) -> bool:
            return True

        def __ne__(self, other: object) -> bool:
            return True

    class B(Paired_, Base):
        y: object = 0

    assert B.__ne__ is Paired_.__ne__
    assert "__ne__" not in vars(B)
    assert (B(1, 0) == B(2, 0)) is True
    assert (B(1, 0) != B(2, 0)) is True


def test_a_co_base_derived_from_the_mixin_does_not_count_as_having_paired_them():
    """The mixin is a permitted base, so a co-base can derive from it and still
    write its own `__eq__`. Its `__ne__` then resolves to the *structural* one,
    which is the answer being displaced rather than a pairing to leave alone --
    and reading it as a pairing left `==` and `!=` both true.
    """

    class FromTheMixin(Struct.__mro__[1]):  # type: ignore[misc,name-defined]  # noqa: PLW1641
        __slots__ = ()

        def __eq__(self, other: object) -> bool:
            return True

    class B(FromTheMixin, Base):
        y: object = 0

    assert (B(1, 0) == B(2, 0)) is True
    assert (B(1, 0) != B(2, 0)) is False


def test_a_co_base_with_a_raising_inequality_beside_a_real_equality_fails_too():
    """`__ne__` is read only off a base that supplied `__eq__`, so this is the
    one shape where a name other than `__eq__` can refuse a class. Same trade
    as the equality probe, pinned so it is not a surprise.
    """

    class RealEqualityRaisingNotEqual:  # noqa: PLW1641 -- the absent __hash__ is not what is under test
        def __eq__(self, other: object) -> bool:
            return True

        __ne__ = Raising()  # type: ignore[assignment]

    with pytest.raises(RuntimeError, match="no equality here"):

        class B(RealEqualityRaisingNotEqual, Base):
            y: object = 0


@pytest.mark.xfail(strict=True, reason="#76: a co-base between two struct bases is not seen")
def test_a_co_base_between_two_struct_bases_is_not_seen_either():
    """The same limit as the later-struct-base case, reached by a co-base
    rather than by a struct one: C3 defers the shared mixin to the tail, so a
    plain base sitting between two struct bases supplies the equality the class
    resolves, and the walk ended at the first struct base without seeing it.

    This is #47's own break in a shape that needs two struct bases to reach, so
    it goes with the rest of #76 rather than here.
    """

    class Second(Struct):
        pass

    class Equality:  # noqa: PLW1641 -- the absent __hash__ is not what is under test
        def __eq__(self, other: object) -> bool:
            return True

    class B(Second, Equality, Base):
        pass

    assert B(1) == B(2)
    assert hash(B(1)) == hash(B(2))
