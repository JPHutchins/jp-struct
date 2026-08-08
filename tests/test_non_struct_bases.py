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


def test_a_plain_class_behaves_the_same_way():
    """Not salix's rule but Python's, which is the argument for following it: a
    body that defines __eq__ and not __hash__ is unhashable, and the struct
    base does not change what the class body asked for.
    """

    class Plain(Equal):
        pass

    assert Plain.__hash__ is None


def test_a_base_that_pairs_them_keeps_its_pair():
    """Equality and the hash beside it both come from the base, so they agree
    -- which is the whole requirement. salix binds neither.
    """

    class B(Paired, Base):
        y: object = 0

    assert B(1, 0) == B(2, 0)
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
