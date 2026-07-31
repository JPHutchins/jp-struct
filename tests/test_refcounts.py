"""Reference-count invariants.

Nothing else here would notice a leak or an over-release: a leaked reference
just grows memory, and an over-release corrupts something far from the cause.
Skipped on a free-threaded build, where deferred and biased reference counting
make getrefcount an unreliable probe.
"""

import gc
import sys
import sysconfig

import pytest

from salix import Struct

pytestmark = pytest.mark.skipif(
    bool(sysconfig.get_config_var("Py_GIL_DISABLED")),
    reason="getrefcount is not a reliable probe without the GIL",
)


class Sentinel:
    """Never immortal, never cached, never interned."""


class Pair(Struct):
    first: object
    second: object


class Defaulted(Struct):
    required: object
    optional: object = Sentinel()


def test_the_probe_detects_a_retained_reference():
    """A negative control: without this, every test below could pass vacuously."""

    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)
    keeper = [sentinel]

    assert sys.getrefcount(sentinel) == before + 1

    del keeper

    assert sys.getrefcount(sentinel) == before


def test_construction_takes_one_reference_per_field():
    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)
    pair = Pair(sentinel, sentinel)

    assert sys.getrefcount(sentinel) == before + 2

    del pair

    assert sys.getrefcount(sentinel) == before


def test_keyword_binding_takes_the_same_count_as_positional():
    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)
    pair = Pair(first=sentinel, second=sentinel)

    assert sys.getrefcount(sentinel) == before + 2

    del pair


def test_a_default_is_shared_by_every_instance():
    default = Defaulted.__struct_defaults__ if hasattr(Defaulted, "__struct_defaults__") else None
    sentinel = Defaulted(None).optional
    before = sys.getrefcount(sentinel)
    instances = [Defaulted(None) for _ in range(10)]

    assert sys.getrefcount(sentinel) == before + 10

    del instances, default

    assert sys.getrefcount(sentinel) == before


def test_reading_a_field_does_not_accumulate():
    sentinel = Sentinel()
    pair = Pair(sentinel, None)
    before = sys.getrefcount(sentinel)

    for _ in range(100):
        assert pair.first is sentinel

    assert sys.getrefcount(sentinel) == before

    del pair


@pytest.mark.parametrize(
    "operation",
    # PLR0124: comparing an instance with itself is what exercises the dunder.
    [repr, hash, lambda pair: pair == pair, lambda pair: pair != pair],  # noqa: PLR0124
    ids=["repr", "hash", "eq", "ne"],
)
def test_the_dunders_do_not_leak(operation):
    sentinel = Sentinel()
    pair = Pair(sentinel, sentinel)
    before = sys.getrefcount(sentinel)

    for _ in range(100):
        operation(pair)

    assert sys.getrefcount(sentinel) == before

    del pair


def test_a_construction_that_fails_late_releases_what_it_already_bound():
    """The unwind path: positional slots are written before keywords are checked."""

    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)

    with pytest.raises(TypeError, match="unexpected keyword argument"):
        Pair(sentinel, sentinel, nope=1)

    assert sys.getrefcount(sentinel) == before


def test_a_construction_short_an_argument_releases_what_it_already_bound():
    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)

    with pytest.raises(TypeError, match="missing required argument"):
        Pair(sentinel)

    assert sys.getrefcount(sentinel) == before


def test_a_rejected_class_body_releases_its_defaults():
    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)

    with pytest.raises(TypeError, match="non-default field"):

        class Bad(Struct):
            a: object = sentinel
            b: object

    gc.collect()

    assert sys.getrefcount(sentinel) == before


def test_discarding_a_class_releases_its_defaults():
    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)

    class Temporary(Struct):
        a: object = sentinel

    assert sys.getrefcount(sentinel) > before

    del Temporary
    gc.collect()

    assert sys.getrefcount(sentinel) == before


def test_discarding_a_class_releases_its_post_init():
    """The class holds the hook it resolved, so it has one more to give back."""

    def hook(self: object) -> None:
        return None

    before = sys.getrefcount(hook)

    class Temporary(Struct):
        a: object
        __post_init__ = hook

    assert sys.getrefcount(hook) > before

    del Temporary
    gc.collect()

    assert sys.getrefcount(hook) == before


def test_a_post_init_that_raises_releases_the_fields_it_already_saw():
    class Rejects(Struct):
        a: object

        def __post_init__(self) -> None:
            raise ValueError("no")

    sentinel = Sentinel()
    before = sys.getrefcount(sentinel)

    with pytest.raises(ValueError, match="no"):
        Rejects(sentinel)

    gc.collect()

    assert sys.getrefcount(sentinel) == before
