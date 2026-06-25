"""Tests for the `record` extension.

Seeded from the prototype's smoke test. Covers the features the annotation
form supports: construction by position/keyword, defaults, immutability,
structural equality, hashing, repr, __match_args__/__slots__/__annotations__,
single-base field inheritance, and metaclass identity.

Run with `make test` (or `python -m pytest`).
"""

import pytest

import record
from record import Record


class Point2D(Record):
    """A simple 2D point."""

    x: float
    y: float


class WithDefaults(Record):
    a: int
    b: int = 2
    c: int = 3


def test_basic_construct_and_access():
    p = Point2D(1.0, 2.0)
    assert p.x == 1.0
    assert p.y == 2.0
    assert Point2D(x=1.0, y=2.0).x == 1.0
    assert Point2D(1.0, y=2.0).y == 2.0


def test_slots_and_no_dict():
    p = Point2D(1.0, 2.0)
    assert not hasattr(p, "__dict__")
    assert Point2D.__slots__ == ("x", "y")


def test_match_args():
    assert Point2D.__match_args__ == ("x", "y")
    match Point2D(1.0, 2.0):
        case Point2D(x, y):
            assert x == 1.0 and y == 2.0
        case _:
            pytest.fail("pattern did not match")


def test_annotations():
    assert Point2D.__annotations__ == {"x": float, "y": float}


def test_record_fields_introspection():
    assert Point2D(1.0, 2.0).__record_fields__ == ("x", "y")
    assert WithDefaults(1).__record_defaults__ == (2, 3)


def test_defaults():
    assert WithDefaults(1) == WithDefaults(1, 2, 3)
    assert WithDefaults(1, b=20).b == 20
    assert WithDefaults(1, 0, 0).c == 0


def test_immutable():
    p = Point2D(1.0, 2.0)
    with pytest.raises(TypeError):
        p.x = 9.0
    with pytest.raises(TypeError):
        del p.x


def test_eq_structural():
    assert Point2D(1.0, 2.0) == Point2D(1.0, 2.0)
    assert Point2D(1.0, 2.0) != Point2D(1.0, 9.0)

    class Other2D(Record):
        x: float
        y: float

    # Structural equality: equal field names + values, regardless of class.
    assert Point2D(1.0, 2.0) == Other2D(1.0, 2.0)

    class Point1D(Record):
        x: float

    assert Point1D(1.0) != Point2D(1.0, 2.0)


def test_hash():
    assert hash(Point2D(1.0, 2.0)) == hash(Point2D(1.0, 2.0))
    assert hash(Point2D(1.0, 2.0)) == hash((1.0, 2.0))
    assert len({Point2D(1.0, 2.0), Point2D(1.0, 2.0)}) == 1


def test_repr():
    assert repr(Point2D(1.0, 2.0)) == "Point2D(x=1.0, y=2.0)"
    assert repr(WithDefaults(1)) == "WithDefaults(a=1, b=2, c=3)"


@pytest.mark.parametrize("bad", [
    lambda: Point2D(1.0),
    lambda: Point2D(1.0, 2.0, 3.0),
    lambda: Point2D(1.0, 2.0, z=3.0),
    lambda: Point2D(1.0, x=2.0),
])
def test_errors(bad):
    with pytest.raises(TypeError):
        bad()


def test_inheritance_extends_fields():
    class Point3D(Point2D):
        z: float

    p = Point3D(1.0, 2.0, 3.0)
    assert (p.x, p.y, p.z) == (1.0, 2.0, 3.0)
    assert Point3D.__match_args__ == ("x", "y", "z")
    assert p.__record_fields__ == ("x", "y", "z")


def test_empty_record():
    class Empty(Record):
        pass

    assert Empty() == Empty()
    assert repr(Empty()) == "Empty()"


def test_metaclass_identity():
    assert type(Point2D) is record.RecordMeta
    assert isinstance(Point2D(1.0, 2.0), Record)
